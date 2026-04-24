---
title: "Gestión eficiente de entrada y salida"
description: "Convertir un buffer lineal en el kernel en una cola circular real: I/O parcial, lecturas y escrituras no bloqueantes, mmap y las bases para una concurrencia segura."
partNumber: 2
partName: "Building Your First Driver"
chapter: 10
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "es-ES"
---
# Gestión eficiente de la entrada y salida

## Orientación al lector y resultados

El Capítulo 9 terminó con un driver pequeño pero honesto. Tu módulo `myfirst` se engancha como dispositivo Newbus, crea `/dev/myfirst/0` con un alias en `/dev/myfirst`, asigna estado por apertura, transfiere bytes a través de `uiomove(9)` y mantiene un buffer del kernel sencillo del tipo primero en entrar, primero en salir. Puedes escribir datos en él, leer esos mismos datos de vuelta, ver cómo suben los contadores de bytes en `sysctl dev.myfirst.0.stats` y observar cómo se ejecuta tu destructor por apertura cuando cada descriptor se cierra. Se trata de un driver completo, cargable y fundamentado en el código fuente real, y las tres etapas que construiste en el Capítulo 9 son la base sobre la que el Capítulo 10 va a edificar.

Sin embargo, hay algo insatisfactorio en ese buffer FIFO de la Etapa 3. Mueve los bytes correctamente, pero no aprovecha bien el buffer. Una vez que `bufhead` avanza, el espacio que deja atrás se desperdicia hasta que el buffer se vacía y `bufhead` vuelve a cero. Un productor constante y un consumidor equivalente pueden agotar la capacidad mucho antes de que ninguna de las dos partes haya terminado realmente su trabajo. Un buffer lleno devuelve `ENOSPC` de inmediato, aunque un lector esté a un milisegundo de vaciarlo a la mitad. Un buffer vacío devuelve cero bytes como un pseudofin de archivo, aunque el llamador esté dispuesto a esperar. Ninguno de esos comportamientos es incorrecto para un punto de control didáctico, pero ninguno escala.

Este capítulo es donde hacemos que la ruta de I/O sea eficiente.

Los drivers reales mueven bytes en segundo plano mientras se realiza otro trabajo. Un lector que llega al final de los datos actuales puede querer bloquearse hasta que lleguen más. Un escritor que encuentra el buffer lleno puede querer bloquearse hasta que un lector haya liberado espacio. Un llamador no bloqueante quiere un `EAGAIN` claro en lugar de una ficción amable. Una herramienta como `cat` o `dd` quiere leer y escribir en bloques que encajen con su propio tamaño de buffer, no con las restricciones internas del driver. Y el kernel quiere que el driver haga todo esto sin perder bytes, sin corromper el estado compartido y sin la maraña desordenada de índices y casos límite que los drivers de principiantes acumulan con tanta frecuencia.

La forma en que los drivers reales logran esto es mediante un puñado de patrones disciplinados. Un **buffer circular** reemplaza al buffer lineal de la Etapa 3 y mantiene toda la capacidad disponible. La **E/S parcial** permite que `read(2)` y `write(2)` devuelvan la porción de la solicitud que esté realmente disponible, en lugar de todo o nada. El **modo no bloqueante** permite que un llamador bien escrito pregunte «¿hay algún dato ya?» sin comprometerse a dormirse. Y una cuidadosa **refactorización** convierte el buffer, que antes era un conjunto ad hoc de campos dentro del softc, en una pequeña abstracción con nombre que el Capítulo 11 podrá proteger con primitivas de sincronización reales.

Todo esto se encuentra firmamente dentro del territorio de los dispositivos de caracteres. Seguimos escribiendo un pseudo-dispositivo, seguimos leyendo y escribiendo a través de `struct uio`, y seguimos cargando y descargando nuestro módulo con `kldload(8)` y `kldunload(8)`. Lo que cambia es la *forma* del plano de datos entre el buffer del kernel y el programa de usuario, y la calidad de las garantías que ofrece el driver.

### Por qué este capítulo merece su propio espacio

Sería posible tomar atajos con este material. Muchos tutoriales muestran un buffer circular en diez líneas, añaden un `mtx_sleep` en algún lugar y declaran la victoria. Ese enfoque produce código que supera una prueba y luego desarrolla errores misteriosos bajo carga. Los errores no suelen estar en los manejadores de lectura y escritura en sí mismos. Están en cómo el buffer circular hace el wrap-around, en cómo se llama a `uiomove(9)` cuando los bytes activos abarcan el final físico del buffer, en cómo se interpreta `IO_NDELAY`, en cómo `selrecord(9)` y `selwakeup(9)` interactúan con un llamador no bloqueante que nunca llama a `poll(2)`, y en cómo deben notificarse las escrituras parciales a un programa en espacio de usuario que está iterando en un bucle.

Este capítulo trabaja esos detalles uno a uno. El resultado es un driver al que puedes someter con `dd`, bombardear con un par productor-consumidor, inspeccionar a través de `sysctl` y entregar al Capítulo 11 como una base estable para el trabajo de concurrencia.

### Dónde dejó el driver el Capítulo 9

Comprueba el estado desde el que deberías estar trabajando. Si tu árbol de código fuente y tu máquina de laboratorio coinciden con este esquema, todo en el Capítulo 10 encajará perfectamente. Si no es así, vuelve y lleva la Etapa 3 a la forma que se describe a continuación antes de continuar.

- Un hijo Newbus bajo `nexus0`, creado por `device_identify`.
- Una `struct myfirst_softc` dimensionada para el buffer FIFO más las estadísticas.
- Un mutex `sc->mtx` con el nombre del dispositivo, que protege los contadores del softc y los índices del buffer.
- Un árbol sysctl en `dev.myfirst.0.stats` que expone `attach_ticks`, `open_count`, `active_fhs`, `bytes_read`, `bytes_written` y los valores actuales de `bufhead`, `bufused` y `buflen`.
- Un cdev principal en `/dev/myfirst/0` con propietario `root:operator` y modo `0660`.
- Un cdev alias en `/dev/myfirst` que apunta al principal.
- Estado por apertura mediante `devfs_set_cdevpriv(9)` y un destructor `myfirst_fh_dtor`.
- Un buffer FIFO lineal de `MYFIRST_BUFSIZE` bytes, con `bufhead` colapsando a cero cuando está vacío.
- `d_read` devolviendo cero bytes cuando `bufused == 0` (nuestra aproximación del EOF en esta etapa).
- `d_write` devolviendo `ENOSPC` cuando la cola alcanza `buflen`.

El Capítulo 10 toma ese driver y reemplaza el buffer FIFO lineal por un verdadero buffer circular. Luego amplía los manejadores `d_read` y `d_write` para gestionar correctamente la E/S parcial, añade una ruta consciente de `O_NONBLOCK` con `EAGAIN`, conecta un manejador `d_poll` para que `select(2)` y `poll(2)` comiencen a funcionar, y termina extrayendo la lógica del buffer a una abstracción con nombre lista para el bloqueo.

### Qué aprenderás

Una vez que hayas terminado este capítulo, serás capaz de:

- Explicar, en términos sencillos, qué ventajas aporta el buffering a un driver y en qué punto empieza a ser perjudicial.
- Diseñar e implementar un buffer circular de tamaño fijo orientado a bytes en espacio del kernel.
- Razonar correctamente sobre el wrap-around: detectarlo, dividir las transferencias a través de él y mantener la contabilidad de `uio` coherente mientras lo haces.
- Integrar ese buffer circular en el driver `myfirst` en evolución sin hacer retroceder ningún comportamiento anterior.
- Gestionar lecturas y escrituras parciales de la forma en que los programas UNIX clásicos esperan.
- Interpretar y respetar `IO_NDELAY` en tus manejadores de lectura y escritura.
- Implementar `d_poll` para que `select(2)` y `poll(2)` funcionen con `myfirst`.
- Probar bajo carga la E/S con buffer desde el espacio de usuario usando `dd(1)`, `cat(1)`, `hexdump(1)` y un pequeño par productor-consumidor.
- Reconocer los riesgos de lectura-modificación-escritura que contiene el driver actual y realizar las refactorizaciones que permitirán al Capítulo 11 introducir bloqueo real sin reestructurar el código.
- Leer `d_mmap(9)` y entender cuándo un driver de dispositivo de caracteres quiere permitir que el espacio de usuario mapee buffers directamente y cuándo genuinamente no lo desea.
- Hablar sobre zero-copy de una manera que distinga los ahorros reales de los eslóganes.
- Reconocer los patrones de readahead y write-coalescing cuando los veas en un driver, y describir por qué importan para el rendimiento.

### Qué construirás

Llevarás el driver de la Etapa 3 del Capítulo 9 a través de cuatro etapas principales, más una breve quinta etapa opcional que añade soporte para mapeo de memoria.

1. **Etapa 1, un buffer circular independiente.** Antes de tocar el kernel, construirás `cbuf.c` y `cbuf.h` en userland, escribirás un puñado de pruebas pequeñas contra ellos y confirmarás que el wrap-around, el estado vacío y el estado lleno se comportan como tu modelo mental dice que deben hacerlo. Esta es la única parte del capítulo que puedes desarrollar íntegramente en userland, y se amortizará cuando el driver empiece a fallar de formas que habrían sido detectadas por una prueba unitaria de tres líneas.
2. **Etapa 2, un driver con buffer circular.** Insertarás el buffer circular en `myfirst` para que `d_read` y `d_write` ahora impulsen la nueva abstracción. `bufhead` pasa a ser `cb_head`, `bufused` pasa a ser `cb_used`, los campos viven dentro de una pequeña `struct cbuf` y la aritmética del wrap-around es visible en un único lugar. Todavía no cambia ningún comportamiento visible para el espacio de usuario, pero el driver se comporta de inmediato mejor bajo carga sostenida.
3. **Etapa 3, E/S parcial y soporte no bloqueante.** Ampliarás los manejadores para gestionar correctamente las lecturas y escrituras parciales, interpretar `IO_NDELAY` como `EAGAIN` e introducir la ruta de lectura bloqueante con `mtx_sleep(9)` y `wakeup(9)`. El driver ahora recompensa a los llamadores educados con baja latencia y a los llamadores pacientes con semántica bloqueante.
4. **Etapa 4, consciente de poll y lista para refactorizar.** Añadirás un manejador `d_poll`, conectarás una `struct selinfo` y refactorizarás todo el acceso al buffer a través de un conjunto compacto de funciones auxiliares para que el Capítulo 11 pueda incorporar una estrategia de bloqueo real.
5. **Etapa 5, mapeo de memoria (opcional).** Añadirás un pequeño manejador `d_mmap` para que el espacio de usuario pueda leer el buffer a través de `mmap(2)`. Esta etapa se explora junto a los temas complementarios de la Sección 8 y el laboratorio correspondiente al final del capítulo. Puedes saltarla en una primera lectura sin perder el hilo; aborda el mismo buffer desde un ángulo diferente.

Ejercitarás cada etapa con las herramientas del sistema base, con un pequeño programa de userland `cb_test` que compilas en la Etapa 1, y con dos nuevos auxiliares: `rw_myfirst_nb` (un tester no bloqueante) y `producer_consumer` (un arnés de carga basado en fork). Cada etapa reside en disco bajo un directorio dedicado en `examples/part-02/ch10-handling-io-efficiently/`, y el README allí refleja los puntos de control del capítulo.

### Qué no cubre este capítulo

Vale la pena nombrar explícitamente lo que *no* vamos a intentar hacer aquí. Los debates más profundos sobre estos temas pertenecen a capítulos posteriores, y arrastrarlos aquí ahora difuminaría las lecciones de este.

- **Corrección real de la concurrencia.** El Capítulo 10 utiliza el mutex que ya existe en el softc, y usa `mtx_sleep(9)` con ese mutex como argumento de sincronización de suspensión. Eso es seguro por construcción. Pero este capítulo no explora todo el espacio de las condiciones de carrera, ni categoriza las clases de lock, ni enseña al lector cómo demostrar que un fragmento de estado compartido está correctamente protegido. Ese es el trabajo del Capítulo 11, y es la razón por la que la última sección aquí se llama «Refactorización y preparación para la concurrencia» en lugar de «Concurrencia en drivers».
- **`ioctl(2)`.** El driver todavía no implementa `d_ioctl`. Algunas primitivas de limpieza (vaciar el buffer, consultar su nivel de llenado) encajarían bien bajo `ioctl`, pero el Capítulo 25 es el lugar apropiado para eso.
- **`kqueue(2)`.** Este capítulo implementa `d_poll` para `select(2)` y `poll(2)`. El manejador complementario `d_kqfilter`, junto con la maquinaria `knlist` y los filtros `EVFILT_READ`, se introduce más adelante junto con los drivers impulsados por taskqueue.
- **mmap de hardware.** Construiremos un manejador `d_mmap` mínimo que permita al espacio de usuario mapear un buffer del kernel preasignado como páginas de solo lectura, y discutiremos las decisiones de diseño y lo que este patrón logra y no logra. No nos adentraremos en `bus_space(9)`, `bus_dmamap_create(9)` ni en la maquinaria `dev_pager`; esos son materiales de la Parte 4 y la Parte 5.
- **Dispositivos reales con contrapresión real.** El modelo de contrapresión aquí es «el buffer tiene una capacidad fija y se bloquea o devuelve `EAGAIN` cuando está lleno». Los drivers de almacenamiento y red tienen modelos más ricos (watermarks, crédito, colas BIO, cadenas de mbuf). Esos detalles pertenecen a sus propios capítulos.

Mantener el capítulo dentro de esos límites es lo que le da honestidad. El material que aprenderás es suficiente para escribir un pseudo-dispositivo respetable y para leer la mayoría de los drivers de dispositivo de caracteres del árbol con confianza.

### Tiempo estimado de dedicación

- **Solo lectura**: aproximadamente noventa minutos, quizás dos horas si te detienes en los diagramas.
- **Lectura más escritura de las cuatro etapas**: de cuatro a seis horas, divididas en al menos dos sesiones con uno o dos reinicios.
- **Lectura más todos los laboratorios y desafíos**: de ocho a doce horas a lo largo de tres sesiones. Los desafíos son genuinamente más ricos que los laboratorios principales y recompensan el trabajo paciente.

Al igual que en el Capítulo 9, haz un arranque limpio del laboratorio antes de empezar. No te apresures en las cuatro etapas. El valor de la secuencia está en observar cómo cambia el comportamiento del driver, un patrón a la vez, a medida que añades cada capacidad.

### Requisitos previos

Antes de comenzar este capítulo, comprueba lo siguiente:

- El código fuente de tu driver coincide con el ejemplo de la Fase 3 del Capítulo 9 que se encuentra en `examples/part-02/ch09-reading-and-writing/stage3-echo/`. Si no es así, detente aquí y ponlo en ese estado primero. El Capítulo 10 lo da por supuesto.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con un `/usr/src` correspondiente. Las APIs y la estructura de archivos que verás en este capítulo se corresponden con esa versión.
- Has leído el Capítulo 9 con detenimiento, incluido el Apéndice E (el resumen de una página). La «columna vertebral de tres líneas» para lecturas y escrituras que allí aparece es exactamente lo que vamos a ampliar.
- Te sientes cómodo cargando y descargando tu propio módulo, observando `dmesg`, leyendo `sysctl dev.myfirst.0.stats` y analizando la salida de `truss(1)` o `ktrace(1)` cuando una prueba te sorprende.

Si alguno de estos puntos te genera dudas, dedicar tiempo a resolverlos ahora es más provechoso que avanzar con el capítulo sin esa base.

### Cómo sacar el máximo partido de este capítulo

Tres hábitos resultan muy útiles.

En primer lugar, mantén `/usr/src/sys/dev/evdev/cdev.c` abierto en una segunda terminal. Es uno de los ejemplos más limpios del árbol de un dispositivo de caracteres que implementa un buffer circular, bloquea a los llamadores cuando el buffer está vacío, respeta `O_NONBLOCK` y despierta a los threads durmientes tanto a través de `wakeup(9)` como del mecanismo `selinfo`. Lo mencionaremos varias veces.

En segundo lugar, ten `/usr/src/sys/kern/subr_uio.c` a mano. El Capítulo 9 recorrió los internos de `uiomove` en ese archivo; aquí lo revisitaremos cuando el desbordamiento circular del buffer nos obligue a dividir una transferencia. Leer el código real refuerza el modelo mental correcto.

En tercer lugar, ejecuta tus pruebas bajo `truss(1)` de vez en cuando, no solo desde shells normales. Rastrear los valores de retorno de las syscalls es la forma más rápida de distinguir un driver que respeta la I/O parcial de uno que descarta bytes en silencio.

### Hoja de ruta del capítulo

Las secciones, en orden, son:

1. Qué es la I/O con buffer y por qué importa. Cubos y tuberías, patrones sin buffer frente a patrones con buffer, y dónde encaja cada uno en un driver.
2. Creación de un buffer circular. La estructura de datos, los invariantes, la aritmética de desbordamiento circular y una implementación independiente en espacio de usuario que probarás antes de introducirla en el kernel.
3. Integración de ese buffer circular en `myfirst`. Cómo cambian `d_read` y `d_write`, cómo gestionar la transferencia dividida en la vuelta circular y cómo registrar el estado del buffer para que la depuración no requiera conjeturas.
4. Lecturas y escrituras parciales. Qué son, por qué constituyen un comportamiento UNIX correcto, cómo notificarlas a través de `uio_resid` y los casos extremos que no debes pasar por alto.
5. I/O sin bloqueo. El flag `IO_NDELAY`, su relación con `O_NONBLOCK`, la convención `EAGAIN` y el diseño de un camino de lectura bloqueante sencillo mediante `mtx_sleep(9)` y `wakeup(9)`.
6. Prueba de la I/O con buffer desde el espacio de usuario. Un kit de pruebas consolidado: `dd`, `cat`, `hexdump`, `truss`, un pequeño comprobador sin bloqueo y un arnés productor-consumidor que bifurca un proceso lector y uno escritor contra `/dev/myfirst`.
7. Refactorización y preparación para la concurrencia. Dónde el código actual es vulnerable, cómo descomponer el buffer en funciones auxiliares y la forma en que querrás dejarlo para el Capítulo 11.
8. Tres temas complementarios: `d_mmap(9)` como mapeo mínimo de memoria del kernel (la Fase 5 opcional), consideraciones sobre copia cero para pseudodispositivos y los patrones que usan los drivers reales de alto rendimiento (lectura anticipada en el lado de lectura, coalescencia de escrituras en el lado de escritura).
9. Laboratorios prácticos, un conjunto de ejercicios concretos que deberías poder completar directamente sobre el driver.
10. Ejercicios de desafío que amplían las mismas habilidades sin introducir fundamentos completamente nuevos.
11. Notas de resolución de problemas para las clases de error que los patrones de este capítulo suelen generar.
12. Cierre del capítulo y puente hacia el Capítulo 11.

Si es tu primera lectura, sigue el orden lineal y realiza los laboratorios en secuencia. Si revisitas el material para afianzarlo, cada tema complementario numerado y la sección de resolución de problemas son independientes entre sí.

## Sección 1: Qué es la I/O con buffer y por qué importa

Todo driver que mueve bytes entre el espacio de usuario y una fuente de datos del lado del hardware o del kernel debe decidir *dónde* viven esos bytes en el intervalo. El driver de la Fase 3 del Capítulo 9 ya toma esa decisión de forma implícita. Los bytes de `bufused` que han sido escritos pero aún no leídos residen en un buffer del kernel. El lector los extrae por la cabeza; el escritor añade nuevos bytes por la cola. El driver `myfirst`, en ese sentido, ya opera con buffer.

Lo que cambia en este capítulo no es si tienes un buffer. Es *cómo* está diseñado el buffer, *qué parte* de su capacidad puedes mantener en uso a la vez y *qué promesas* hace el driver a sus llamadores sobre el comportamiento de ese buffer. Antes de examinar la estructura de datos, merece la pena detenerse en la diferencia entre la I/O sin buffer y la I/O con buffer a un nivel conceptual. Ese contraste guiará cada decisión que el resto del capítulo te pedirá que tomes.

### Una definición sencilla

En su formulación más simple, la **I/O sin buffer** significa que cada llamada a `read(2)` o `write(2)` accede directamente a la fuente o al destino subyacente. No existe almacenamiento intermedio que absorba ráfagas, ni un lugar donde el productor pueda dejar bytes para que el consumidor los recoja después, ni forma de desacoplar la velocidad a la que se producen bytes de la velocidad a la que se consumen. Cada llamada llega hasta el fondo.

La **I/O con buffer**, por el contrario, coloca una pequeña región de memoria entre el productor y el consumidor. Un escritor deposita bytes en el buffer; un lector los recoge. Mientras el buffer tenga espacio libre, los escritores no necesitan esperar. Mientras el buffer tenga bytes disponibles, los lectores no necesitan esperar. El buffer absorbe las discrepancias a corto plazo entre ambos lados.

Puede parecer una distinción menor, pero en el código de un driver suele ser la diferencia entre algo que funciona bajo carga y algo que no.

Conviene detenerse en un matiz pequeño pero importante. El propio kernel opera con buffer en varias capas por encima y por debajo de tu driver. La `stdio` de la biblioteca de C almacena las escrituras en buffer antes de que lleguen a la syscall `write(2)`. La capa VFS opera con buffer en la I/O de archivos regulares a través del buffer cache. Los drivers de disco de la Parte 7 operarán con buffer en el nivel de BIO y de cola. Cuando este capítulo habla de «I/O con buffer», se refiere a un buffer *dentro del driver*, situado entre los manejadores de lectura y escritura orientados al usuario y cualquier fuente o destino de datos que represente el driver. No estamos debatiendo si existe o no el almacenamiento en buffer; estamos decidiendo dónde colocar un buffer más y qué debe hacer.

### Dos imágenes concretas

Imagina primero un pseudodispositivo sin buffer. Piensa en un driver cuyo `d_write` entrega inmediatamente cada byte al código de nivel superior que los consume. Si el consumidor está ocupado, el escritor espera. Si el consumidor es rápido, el escritor avanza sin problemas. El sistema no tiene margen de maniobra. Una ráfaga en cualquiera de los dos lados se traduce directamente en presión sobre el otro.

Ahora imagina un pseudodispositivo con buffer. El mismo `d_write` deposita bytes en un pequeño buffer. El consumidor extrae bytes a su propio ritmo. Una breve ráfaga de escrituras puede completarse al instante porque el buffer las absorbe. Una breve pausa del consumidor no detiene al escritor porque el buffer retiene el acumulado pendiente. Ambos lados parecen funcionar con fluidez aunque sus velocidades no coincidan exactamente en cada instante.

El caso con buffer es el aspecto que tienen en la práctica la mayoría de los drivers útiles. No hay magia en ello: el buffer es finito, y cuando se llena el productor tiene que esperar o ceder. Pero le da al sistema un lugar donde tolerar la variabilidad normal, y esa tolerancia es lo que hace predecible el rendimiento.

### Cubos frente a tuberías

Una analogía útil aquí es la diferencia entre transportar agua en cubos y transportarla a través de una tubería.

Cuando transportas agua en cubos, cada transferencia es un evento discreto. Caminas hasta el pozo, llenas el cubo, regresas, vacías el cubo, vuelves a caminar. El productor (el pozo) y el consumidor (la cisterna) están fuertemente acoplados a través de tus dos brazos y tu ritmo de marcha. Si tropiezas, el sistema se detiene. Si la cisterna está ocupada, esperas junto a ella. Si el pozo está ocupado, esperas junto a él. Cada entrega requiere que ambos lados estén listos en el mismo instante.

Una tubería reemplaza ese acoplamiento con un tramo de caño. El agua entra por el extremo del pozo y sale por el extremo de la cisterna. La tubería contiene en todo momento cierta cantidad de agua en tránsito. El productor puede bombear mientras haya espacio en la tubería. El consumidor puede drenar mientras haya agua en la tubería. Sus ritmos ya no tienen que coincidir. Solo tienen que coincidir *en promedio*.

Un buffer de driver es exactamente esa tubería. Es un depósito finito que desacopla la velocidad del escritor de la del lector, siempre que ambas velocidades promediadas sean algo que la capacidad del buffer pueda absorber. El modelo de cubos corresponde a la I/O sin buffer. El modelo de tubería corresponde a la I/O con buffer. Ambos son válidos en situaciones distintas, y el trabajo del escritor de drivers es saber cuál construir.

### Rendimiento: llamadas al sistema y cambios de contexto

Las ventajas de rendimiento del almacenamiento en buffer dentro de un driver son reales, aunque indirectas. Provienen de tres fuentes.

La primera fuente es la **sobrecarga de las llamadas al sistema**. Cada `read(2)` o `write(2)` es una transición del espacio de usuario al espacio del kernel y de vuelta. Esa transición es barata en un procesador moderno, pero no es gratuita. Un escritor que llama a `write(2)` una vez con mil bytes paga una sola transición. Un escritor que llama a `write(2)` mil veces con un byte cada vez paga mil transiciones. Si el driver almacena en buffer internamente, los llamadores pueden emitir cómodamente lecturas y escrituras más grandes, y la sobrecarga por syscall se convierte en una fracción menor del coste total.

La segunda fuente es la **reducción de cambios de contexto**. Una llamada que debe esperar, porque en ese momento no hay nada disponible, suele provocar que el thread llamador se suspenda y que se planifique otro thread. Cada suspensión y reanudación es más costosa que una llamada al sistema. Un buffer absorbe las breves discrepancias que de otro modo forzarían una suspensión, y los threads de ambos lados siguen ejecutándose.

La tercera fuente son las **oportunidades de agrupación**. Un driver que sabe que tiene miles de bytes listos para enviar puede a veces pasarlos todos al siguiente nivel en una sola operación, mientras que un driver que procesa byte a byte tendría que realizar el mismo trabajo de preparación y cierre por cada transferencia. No veremos esto directamente con el pseudodispositivo de este capítulo, pero es el argumento subyacente de los patrones de coalescencia de lecturas y escrituras que examinaremos más adelante.

Ninguna de estas ventajas debe aplicarse a ciegas. El almacenamiento en buffer también añade latencia, ya que un byte puede permanecer en el buffer durante algún tiempo antes de que el consumidor lo note. Añade coste en memoria. Introduce un par de índices que deben mantenerse coherentes bajo acceso concurrente. Y fuerza un conjunto de decisiones de diseño sobre qué hacer cuando el buffer se llena (¿bloquear? ¿descartar? ¿sobreescribir?) y qué hacer cuando se vacía (¿bloquear? ¿lectura corta? ¿señalar fin de archivo?). Hay una razón por la que este capítulo dedica tiempo real a esas decisiones.

### Transferencias de datos con buffer en drivers de dispositivo

¿En qué casos exactamente resulta rentable el almacenamiento en buffer en un driver de dispositivo?

El caso más claro es el de un driver cuya fuente de datos produce ráfagas. Un driver de puerto serie recibe caracteres cada vez que el chip UART genera una interrupción; si el consumidor no está leyendo en ese momento, esos caracteres necesitan un lugar donde residir hasta que lo haga. Un driver de teclado recoge eventos de tecla en el manejador de interrupciones y los entrega al espacio de usuario a la velocidad que la aplicación esté dispuesta a leer. Un driver de red ensambla paquetes en buffers de DMA y los alimenta a la pila de protocolos tan pronto como puede. En cada caso, el driver necesita un lugar donde retener los datos entrantes entre el momento en que llegan y el momento en que pueden entregarse.

El caso inverso es el de un driver cuyo sumidero de datos absorbe ráfagas. Un driver de gráficos puede encolar comandos hasta que la GPU esté lista para procesarlos. Un driver de impresora puede aceptar un documento y entregarlo poco a poco al ritmo de la impresora. Un driver de almacenamiento puede recoger peticiones de escritura y dejar que un algoritmo de ascensor las reordene para el disco. De nuevo, el driver necesita un lugar donde retener los datos salientes entre el momento en que el usuario los escribe y el momento en que el dispositivo está listo.

El pseudodispositivo que estamos construyendo en este libro se sitúa en el punto intermedio entre esos dos patrones. No hay hardware real en ninguno de los extremos, pero la *forma* del camino de los datos refleja lo que hacen los drivers reales. Cuando escribes en `/dev/myfirst`, los bytes van a parar a un buffer que pertenece al driver. Cuando lees, los bytes salen de ese mismo buffer. Una vez que el buffer sea circular y los manejadores de I/O sepan respetar las transferencias parciales, puedes someter al driver a carga con `dd if=/dev/zero of=/dev/myfirst bs=1m count=10` desde un terminal y `dd if=/dev/myfirst of=/dev/null bs=4k` desde otro, y el driver se comportará de la misma manera que un dispositivo de caracteres real bajo una carga análoga.

### Cuándo usar I/O con buffer en un driver

Casi todo driver necesita algún tipo de buffer. La pregunta interesante no es si usar buffer, sino a qué *granularidad* y con qué *modelo de contrapresión*.

La granularidad tiene que ver con el tamaño del buffer en relación con las tasas de transferencia de cada lado. Un buffer demasiado pequeño se llena constantemente y obliga al escritor a esperar, lo que anula su propósito. Un buffer demasiado grande oculta problemas durante demasiado tiempo, permite que la memoria crezca sin límite y aumenta la latencia en el peor caso. El tamaño adecuado depende de para qué sirve el buffer: el buffer de un driver de teclado interactivo solo necesita almacenar unos pocos eventos recientes; el buffer de un driver de red puede necesitar almacenar miles de paquetes en momentos de carga máxima.

La contrapresión trata de qué hacer cuando el buffer se llena (o se vacía) y el patrón de llamadas no coincide con lo que el driver espera. Existen tres estrategias comunes, cada una adecuada para un contexto diferente.

La primera es **block**. Cuando el buffer está lleno, el escritor espera. Cuando el buffer está vacío, el lector espera. Esta es la semántica clásica de UNIX, y es el comportamiento por defecto adecuado para terminales, pipes y la mayoría de pseudodispositivos de propósito general. Implementaremos lecturas con bloqueo (y escrituras con bloqueo opcionales) en la Sección 5.

La segunda es **drop**. Cuando el buffer está lleno, el escritor descarta el byte (o registra un evento de desbordamiento) y continúa. Cuando el buffer está vacío, el lector recibe cero bytes y continúa. Este es el comportamiento adecuado para algunos escenarios en tiempo real o de alta tasa, donde esperar causaría más perjuicio que perder datos. Sin embargo, la pérdida debe ser observable; de lo contrario, el driver corromperá silenciosamente el flujo desde la perspectiva del usuario.

La tercera es **overwrite**. Cuando el buffer está lleno, el escritor sobreescribe los datos más antiguos con los nuevos. Cuando el buffer está vacío, el lector recibe cero bytes. Este es el comportamiento adecuado para un registro circular de eventos recientes: un historial similar al de `dmesg(8)` donde los bytes más recientes siempre se conservan a costa de los más antiguos.

El driver de este capítulo usa **block** para los llamantes en modo bloqueante y **EAGAIN** para los llamantes en modo no bloqueante, sin ruta de sobreescritura. Es el patrón más habitual en el árbol de código fuente de FreeBSD y el más fácil de razonar. Las otras dos estrategias aparecen en capítulos posteriores cuando sus casos de uso surgen de forma natural.

### Un primer vistazo al coste de un driver sin buffer

Vale la pena ser concretos sobre por qué el driver de la Etapa 3 del Capítulo 9 empieza a presentar problemas a escala.

Supón que tienes un `dd` que escribe bloques de 64 bytes a alta velocidad en el driver, y un `dd` paralelo que los lee de él. Con el buffer de 4096 bytes de la Etapa 3, tienes como máximo 64 bloques en tránsito antes de que el escritor reciba `ENOSPC` y se detenga. Si el lector se pausa por cualquier motivo (fallo de página en su buffer de destino, expulsión por el planificador, migración a otra CPU), el escritor queda parado de inmediato. En cuanto el lector reanude y vacíe un único bloque, el escritor podrá escribir uno más. El rendimiento global es el *mínimo* de lo que consiguen las dos mitades al avanzar en sincronía, más un flujo constante de errores `ENOSPC` que los programas en espacio de usuario no esperan recibir.

Un buffer circular del mismo tamaño almacena el mismo número de bloques en tránsito pero nunca desperdicia la capacidad restante. Un escritor no bloqueante que encuentra el buffer lleno recibe `EAGAIN` (la señal convencional de «inténtalo más tarde») en lugar de `ENOSPC` (la señal convencional de «este dispositivo no tiene espacio»), por lo que una herramienta como `dd` puede decidir si reintentar o esperar. Un escritor bloqueante que encuentra el buffer lleno se duerme en una variable de condición bien definida y se despierta en el instante en que un lector libera espacio. Cada uno de esos cambios es pequeño. Juntos hacen que un driver se sienta ágil en lugar de frágil.

### Hacia dónde vamos

Ya tienes las bases conceptuales. El resto del capítulo las traducirá en código. La Sección 2 recorre la estructura de datos, con diagramas y una implementación en userland que puedes probar antes de confiar en ella dentro del kernel. La Sección 3 traslada la implementación al driver, reemplazando la FIFO lineal. Las Secciones 4 y 5 amplían la ruta de I/O para que las transferencias parciales y la semántica no bloqueante funcionen como los usuarios esperan. La Sección 6 construye el arnés de pruebas que usarás durante el resto de la Parte 2 y la mayor parte de la Parte 3. La Sección 7 prepara el código para el trabajo de locking que define el Capítulo 11.

El orden importa. Cada sección asume que los cambios de la anterior ya están aplicados. Si te adelantas, acabarás en código que no compila o que se comporta de formas inesperadas. Como siempre, el camino lento es el camino rápido.

### Cerrando la Sección 1

Nombramos la diferencia entre I/O sin buffer y I/O con buffer, y enumeramos los costes y beneficios de cada uno. Elegimos una analogía (cubos frente a tubería) a la que podemos seguir recurriendo. Comentamos dónde merece la pena usar buffer en el código de un driver, qué estrategias de contrapresión son habituales y cuál vamos a aplicar durante el resto del capítulo. Y preparamos el terreno para la estructura de datos que sustenta todo esto: el buffer circular.

Si todavía no tienes claro qué estrategia de contrapresión debe usar tu driver, no te preocupes. El comportamiento por defecto que construiremos, «bloquear en el kernel y `EAGAIN` fuera de él», es la elección segura y convencional para pseudodispositivos de propósito general, y tendrás una estructura de código limpia a la que volver si necesitas una estrategia diferente más adelante. Estamos a punto de hacer real ese buffer.

## Sección 2: Crear un buffer circular

Un buffer circular es una de esas estructuras de datos cuya idea es más antigua que los sistemas operativos que usamos hoy en día. Aparece en chips serie, en colas de muestras de audio, en rutas de recepción de red, en colas de eventos de teclado, en buffers de traza, en `dmesg(8)`, en librerías de `printf(3)` y en casi cualquier otro lugar donde un fragmento de código quiere dejar bytes para que otro los recoja más tarde. La estructura es sencilla. La implementación es breve. Los errores que cometen los principiantes son predecibles. La construiremos una vez, en userland, con cuidado, y luego llevaremos la versión verificada al driver en la Sección 3.

### Qué es un buffer circular

Un buffer lineal es lo más sencillo que puede funcionar: una región de memoria más un índice de «siguiente byte libre». Escribes en él desde el principio y te detienes al llegar al final. Una vez lleno, o lo amplías, o lo copias, o dejas de aceptar nuevos datos.

Un buffer circular (también llamado ring buffer) es la misma región de memoria, pero con una diferencia en el comportamiento de los índices. Hay dos índices: el *head*, que apunta al siguiente byte por leer, y el *tail*, que apunta al siguiente byte por escribir. Cuando cualquiera de los índices alcanza el final de la memoria subyacente, vuelve al principio. El buffer se trata como si su primer byte fuera adyacente al último, formando un bucle cerrado.

Dos contadores derivados importan para usar la estructura correctamente. El número de bytes *activos* (cuántos están almacenados en ese momento) es lo que importa a los lectores. El número de bytes *libres* (cuánta capacidad está sin usar) es lo que importa a los escritores. Ambos contadores se pueden derivar del head y el tail, más la capacidad total, con un poco de aritmética.

Visualmente, la estructura tiene este aspecto cuando está parcialmente llena y la región activa no da la vuelta:

```text
  +---+---+---+---+---+---+---+---+
  | _ | _ | A | B | C | D | _ | _ |
  +---+---+---+---+---+---+---+---+
            ^               ^
           head           tail

  capacity = 8, used = 4, free = 4
```

Tras suficientes escrituras, el tail alcanza el final de la memoria subyacente y vuelve al principio. Ahora la región activa da la vuelta:

```text
  +---+---+---+---+---+---+---+---+
  | F | G | _ | _ | _ | _ | D | E |
  +---+---+---+---+---+---+---+---+
        ^               ^
       tail           head

  capacity = 8, used = 4, free = 4
  live region: head -> end of buffer, then start of buffer -> tail
```

El caso en que la región activa da la vuelta es el que pilla desprevenidos a los principiantes. Un `bcopy` ingenuo de los datos activos trata el buffer como si fuera lineal; los bytes que copias no son los que querías. La forma correcta de manejar este caso es realizar la transferencia en *dos partes*: desde `head` hasta el final del buffer, y luego desde el principio del buffer hasta `tail`. Codificaremos exactamente este patrón en las funciones auxiliares que aparecen más adelante.

### Gestionar los punteros de lectura y escritura

Los punteros head y tail (los llamaremos índices, porque son desplazamientos enteros en un array de tamaño fijo) siguen reglas sencillas.

Cuando lees `n` bytes, el head avanza `n` posiciones módulo la capacidad. Cuando escribes `n` bytes, el tail avanza `n` posiciones módulo la capacidad. Las lecturas eliminan bytes; el contador de bytes activos decrece en `n`. Las escrituras añaden bytes; el contador de bytes activos aumenta en `n`.

La pregunta interesante es cómo detectar las dos condiciones límite: vacío y lleno. Con solo `head` y `tail`, la estructura es *casi* suficiente por sí sola. Si `head == tail`, el buffer podría estar vacío (ningún byte almacenado) o lleno (toda la capacidad ocupada). Los dos estados son indistinguibles a partir de los índices por sí solos. Las implementaciones resuelven la ambigüedad de una de estas tres formas.

La primera consiste en mantener un **contador** separado de bytes activos. Con `used` disponible, `used == 0` es inequívocamente vacío y `used == capacity` es inequívocamente lleno. La estructura es ligeramente más grande, pero el código es breve y claro. Este es el diseño que usaremos en este capítulo.

La segunda consiste en **dejar siempre un byte sin usar**. Con esta regla, `head == tail` siempre significa vacío, y el buffer está lleno cuando `(tail + 1) % capacity == head`. La estructura tiene un byte menos y el código no necesita un campo `used`, pero cada transferencia implica un desfase de uno que es fácil de introducir. Este es el diseño utilizado en algunos códigos clásicos de sistemas embebidos; es válido, pero no ofrece ninguna ventaja real en nuestro contexto.

La tercera consiste en usar **índices monótonamente crecientes** que nunca se reducen módulo la capacidad, y calcular el contador de bytes activos como `tail - head`. El desbordamiento circular depende entonces de cómo se indexa el array (`tail % capacity`), no de cómo se avanza el puntero. Este es el diseño utilizado por `buf_ring(9)` en el kernel de FreeBSD, que emplea un contador de 32 bits y confía en que el desbordamiento se comporte correctamente. Es elegante, pero complica las operaciones atómicas y la depuración. No lo usaremos; el campo `used` explícito es la solución de compromiso adecuada para un driver de aprendizaje.

### Detectar buffer lleno y buffer vacío

Con el contador `used` explícito, las comprobaciones de los límites son triviales:

- **Vacío**: `cb->cb_used == 0`. No hay bytes disponibles para leer.
- **Lleno**: `cb->cb_used == cb->cb_size`. No hay espacio disponible para escribir.

La aritmética para los índices head y tail también es sencilla:

- Tras leer `n` bytes: `cb->cb_head = (cb->cb_head + n) % cb->cb_size; cb->cb_used -= n;`
- Tras escribir `n` bytes: `cb->cb_used += n;` (el tail se calcula cuando es necesario como `(cb->cb_head + cb->cb_used) % cb->cb_size`)

Mantendremos el tail de forma implícita, derivado de `head` y `used`. Algunas implementaciones rastrean el tail de forma explícita. Cualquier opción funciona siempre que te comprometas con ella de forma consistente. Con `tail` implícito, nunca tenemos que actualizar dos índices en una sola operación, lo que elimina toda una categoría de errores.

Dos cantidades derivadas de las funciones auxiliares aparecerán con frecuencia:

- `cb_free(cb) = cb->cb_size - cb->cb_used`: cuántos bytes se pueden escribir todavía antes de que el buffer se llene.
- `cb_tail(cb) = (cb->cb_head + cb->cb_used) % cb->cb_size`: dónde debe aterrizar la próxima escritura.

Ambas son funciones puras del head, el contador used y la capacidad. No tienen efectos secundarios y se pueden llamar en cualquier momento.

### Asignar un buffer circular de tamaño fijo

El buffer necesita tres valores de estado y un bloque de memoria de respaldo. Esta es la estructura que usaremos:

```c
struct cbuf {
        char    *cb_data;       /* backing storage, cb_size bytes */
        size_t   cb_size;       /* total capacity, in bytes */
        size_t   cb_head;       /* index of next byte to read */
        size_t   cb_used;       /* count of live bytes */
};
```

Tres funciones de ciclo de vida cubren lo fundamental:

```c
int   cbuf_init(struct cbuf *cb, size_t size);
void  cbuf_destroy(struct cbuf *cb);
void  cbuf_reset(struct cbuf *cb);
```

`cbuf_init` asigna el almacenamiento de respaldo, inicializa los índices y devuelve cero si tiene éxito o un errno positivo en caso de error. `cbuf_destroy` libera el almacenamiento de respaldo y pone a cero la estructura. `cbuf_reset` vacía el buffer sin liberar memoria; ambos índices vuelven a cero.

Tres funciones de acceso proporcionan al resto del código la información sobre los límites que necesita:

```c
size_t cbuf_used(const struct cbuf *cb);
size_t cbuf_free(const struct cbuf *cb);
size_t cbuf_size(const struct cbuf *cb);
```

Estas son funciones pequeñas y aptas para uso inline. No adquieren ningún lock; se espera que el llamador mantenga la sincronización que el sistema más amplio requiera. (En la etapa 4 de este capítulo, el mutex del driver proporcionará esa sincronización.)

Las dos funciones más interesantes son las primitivas de transferencia de bytes:

```c
size_t cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t cbuf_read(struct cbuf *cb, void *dst, size_t n);
```

`cbuf_write` copia hasta `n` bytes desde `src` hacia el buffer y devuelve el número que realmente se copiaron. `cbuf_read` copia hasta `n` bytes desde el buffer hacia `dst` y devuelve el número que realmente se copiaron. Ambas funciones gestionan internamente el caso de vuelta circular. El llamador proporciona una fuente o un destino contiguos; el buffer se encarga de dividir la transferencia cuando la región activa o la región libre cruza el final del almacenamiento subyacente.

Esa firma merece un momento de atención. Fíjate en que las funciones devuelven `size_t`, no `int`. Informan de progreso, no de error. Devolver menos bytes de los solicitados *no* es una condición de error; es la forma correcta de expresar que el buffer estaba lleno (en escrituras) o vacío (en lecturas). Esto refleja el funcionamiento de `read(2)` y `write(2)` en sí mismos: un valor de retorno positivo menor que la solicitud es una «transferencia parcial», no un fallo. Nos apoyaremos en esto en la Sección 4, cuando hagamos que el driver gestione correctamente las I/O parciales.

### Un recorrido por el wrap-around

La lógica del wrap-around es breve, pero merece la pena seguir un ejemplo con cuidado. Supongamos que el buffer tiene capacidad 8, `head` vale 6 y `used` vale 4. Los bytes activos se almacenan en las posiciones 6, 7, 0 y 1.

```text
  +---+---+---+---+---+---+---+---+
  | C | D | _ | _ | _ | _ | A | B |
  +---+---+---+---+---+---+---+---+
        ^               ^
       tail           head
  capacity = 8, used = 4, head = 6, tail = (6+4)%8 = 2
```

Ahora el llamador solicita 3 bytes mediante `cbuf_read`. La función realiza los pasos siguientes:

1. Calcula `n = MIN(3, used) = MIN(3, 4) = 3`. El llamador recibirá como máximo 3 bytes.
2. Calcula `first = MIN(n, capacity - head) = MIN(3, 8 - 6) = 2`. Ese es el bloque contiguo que comienza en `head`.
3. Copia `first = 2` bytes de `cb_data + 6` en `dst`. Son A y B.
4. Calcula `second = n - first = 1`. Esa es la parte de la transferencia que debe provenir del inicio del buffer.
5. Copia `second = 1` byte de `cb_data + 0` en `dst + 2`. Ese es C.
6. Avanza `cb_head = (6 + 3) % 8 = 1`. Decrementa `cb_used` en 3, dejándolo en 1.

El destino del llamador contiene ahora A, B, C. El estado del buffer tiene D como único byte activo, con `head = 1` y `used = 1`. La siguiente lectura devolverá D desde la posición 1.

La misma lógica se aplica a `cbuf_write`, con `tail` en el papel de `head`. La función calcula `tail = (head + used) % capacity`, luego `first = MIN(n, capacity - tail)`, copia `first` bytes de `src` a `cb_data + tail`, después copia el resto de `src + first` a `cb_data + 0`, y avanza `cb_used` por el total escrito.

Hay exactamente un paso en cada función que da la vuelta. O bien el destino da la vuelta (en una escritura) o bien la fuente da la vuelta (en una lectura), pero nunca ambos a la vez. Esta es la propiedad clave que hace que la implementación sea manejable: el wrap del buffer es una propiedad de los datos internos, no de los datos del llamador, de modo que la fuente y el destino del llamador siempre se tratan como memoria contigua ordinaria.

### Evitar sobreescrituras y pérdida de datos

Un error habitual entre principiantes es hacer que `cbuf_write` sobreescriba datos más antiguos cuando el buffer se llena, bajo la premisa de que "los datos más recientes son más importantes". Eso es a veces la política correcta, como señalamos en la sección 1, pero debe ser una elección de diseño *deliberada* y debe ser visible para el llamador, no una mutación silenciosa del estado. El comportamiento convencional por defecto es que `cbuf_write` devuelva el número de bytes que realmente escribió, y el llamador es responsable de comprobar el valor de retorno.

Lo mismo ocurre con `cbuf_read`: cuando el buffer está vacío, `cbuf_read` devuelve cero. El llamador debe interpretar cero como "no hay bytes disponibles ahora mismo", no como un error. Combinar esa señal con el `EAGAIN` del driver o con un sleep bloqueante es tarea del manejador de I/O, no del propio buffer.

Si alguna vez quieres un buffer circular con semántica de sobreescritura (un log al estilo de `dmesg`, por ejemplo), el enfoque más limpio es escribir una función `cbuf_overwrite` separada y dejar `cbuf_write` estricta. Dos nombres distintos expresan dos intenciones distintas, y un futuro lector del código no tendrá que adivinar qué comportamiento está en uso.

### Implementarlo en userland

La manera correcta de aprender esta estructura es escribirla una vez en userland y ejecutarla con unas pocas pruebas pequeñas, antes de pedirle al kernel que confíe en ella. El mismo código fuente podrá trasladarse luego al módulo del kernel casi sin cambios, salvo las llamadas de asignación y liberación de memoria.

A continuación se muestra el código fuente para userland. Está en `examples/part-02/ch10-handling-io-efficiently/cbuf-userland/`.

`cbuf.h`:

```c
/* cbuf.h: a fixed-size byte-oriented circular buffer. */
#ifndef CBUF_H
#define CBUF_H

#include <stddef.h>

struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);

size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);

size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
```

`cbuf.c`:

```c
/* cbuf.c: userland implementation of the byte-oriented ring buffer. */
#include "cbuf.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

int
cbuf_init(struct cbuf *cb, size_t size)
{
        if (cb == NULL || size == 0)
                return (EINVAL);
        cb->cb_data = malloc(size);
        if (cb->cb_data == NULL)
                return (ENOMEM);
        cb->cb_size = size;
        cb->cb_head = 0;
        cb->cb_used = 0;
        return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        free(cb->cb_data);
        cb->cb_data = NULL;
        cb->cb_size = 0;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
        return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
        return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
        return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
        size_t avail, tail, first, second;

        avail = cbuf_free(cb);
        if (n > avail)
                n = avail;
        if (n == 0)
                return (0);

        tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
        first = MIN(n, cb->cb_size - tail);
        memcpy(cb->cb_data + tail, src, first);
        second = n - first;
        if (second > 0)
                memcpy(cb->cb_data, (const char *)src + first, second);

        cb->cb_used += n;
        return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
        size_t first, second;

        if (n > cb->cb_used)
                n = cb->cb_used;
        if (n == 0)
                return (0);

        first = MIN(n, cb->cb_size - cb->cb_head);
        memcpy(dst, cb->cb_data + cb->cb_head, first);
        second = n - first;
        if (second > 0)
                memcpy((char *)dst + first, cb->cb_data, second);

        cb->cb_head = (cb->cb_head + n) % cb->cb_size;
        cb->cb_used -= n;
        return (n);
}
```

Hay dos cosas en este código que merecen atención.

La primera es que tanto `cbuf_write` como `cbuf_read` limitan `n` al espacio disponible o a los datos activos *antes* de realizar ninguna copia. Esa es la clave de la semántica de transferencia parcial: la función acepta hacer menos trabajo del solicitado y le comunica al llamador exactamente cuánto hizo. No hay ningún camino de error para "buffer lleno", porque eso no es un error.

La segunda es que la guarda `second > 0` alrededor del segundo `memcpy` no es estrictamente necesaria (`memcpy(dst, src, 0)` está bien definida y no hace nada), pero hace visible el razonamiento sobre el wrap-around de un vistazo. Un futuro lector puede ver que la segunda copia es condicional y que el caso de vuelta está contemplado.

### Un pequeño programa de prueba

El archivo `cb_test.c` que acompaña al código ejercita la estructura con un conjunto de casos pequeños pero significativos. Es suficientemente breve como para leerlo completo:

```c
/* cb_test.c: simple sanity tests for the cbuf userland implementation. */
#include "cbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) \
        do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)

static void
test_basic(void)
{
        struct cbuf cb;
        char in[8] = "ABCDEFGH";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 8) == 0, "init");
        CHECK(cbuf_used(&cb) == 0, "init used");
        CHECK(cbuf_free(&cb) == 8, "init free");

        n = cbuf_write(&cb, in, 4);
        CHECK(n == 4, "write 4");
        CHECK(cbuf_used(&cb) == 4, "used after write 4");

        n = cbuf_read(&cb, out, 2);
        CHECK(n == 2, "read 2");
        CHECK(memcmp(out, "AB", 2) == 0, "AB content");
        CHECK(cbuf_used(&cb) == 2, "used after read 2");

        cbuf_destroy(&cb);
        printf("test_basic OK\n");
}

static void
test_wrap(void)
{
        struct cbuf cb;
        char in[8] = "ABCDEFGH";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 8) == 0, "init");

        /* Push head forward by writing and reading 6 bytes. */
        n = cbuf_write(&cb, in, 6);
        CHECK(n == 6, "write 6");
        n = cbuf_read(&cb, out, 6);
        CHECK(n == 6, "read 6");

        /* Now write 6 more, which should wrap. */
        n = cbuf_write(&cb, in, 6);
        CHECK(n == 6, "write 6 after wrap");
        CHECK(cbuf_used(&cb) == 6, "used after wrap write");

        /* Read all of it back; should return ABCDEF. */
        memset(out, 0, sizeof(out));
        n = cbuf_read(&cb, out, 6);
        CHECK(n == 6, "read 6 after wrap");
        CHECK(memcmp(out, "ABCDEF", 6) == 0, "content after wrap");
        CHECK(cbuf_used(&cb) == 0, "empty after drain");

        cbuf_destroy(&cb);
        printf("test_wrap OK\n");
}

static void
test_partial(void)
{
        struct cbuf cb;
        char in[8] = "12345678";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 4) == 0, "init small");

        n = cbuf_write(&cb, in, 8);
        CHECK(n == 4, "write clamps to free space");
        CHECK(cbuf_used(&cb) == 4, "buffer full");

        n = cbuf_read(&cb, out, 8);
        CHECK(n == 4, "read clamps to live data");
        CHECK(memcmp(out, "1234", 4) == 0, "content of partial");
        CHECK(cbuf_used(&cb) == 0, "buffer empty after partial drain");

        cbuf_destroy(&cb);
        printf("test_partial OK\n");
}

int
main(void)
{
        test_basic();
        test_wrap();
        test_partial();
        printf("all tests OK\n");
        return (0);
}
```

Compila y ejecuta con:

```sh
$ cc -Wall -Wextra -o cb_test cbuf.c cb_test.c
$ ./cb_test
test_basic OK
test_wrap OK
test_partial OK
all tests OK
```

Las tres pruebas cubren los casos que importan: un ciclo básico de escritura/lectura, una escritura que da la vuelta al final del buffer y una transferencia parcial que toca el límite de capacidad. No son exhaustivas, pero son suficientes para detectar los errores de implementación más comunes. Los ejercicios de desafío al final del capítulo te piden que las amplíes.

### Por qué construirlo en userland primero

Puede parecer un rodeo escribir el buffer en userland cuando sabes que lo que importa es el driver. Hay tres razones que justifican ese rodeo.

La primera es que el kernel es un lugar hostil para depurar. Un bug en el buffer del kernel puede bloquear la máquina, provocar un panic del kernel o corromper silenciosamente estado no relacionado. El mismo bug en el código en userland es simplemente una prueba fallida que imprime un mensaje amigable.

La segunda es que las implementaciones del buffer para el kernel y para userland son prácticamente idénticas. Las únicas diferencias son la primitiva de asignación (`malloc(9)` con `M_DEVBUF` y `M_WAITOK | M_ZERO` frente a `malloc(3)` de libc) y la primitiva de liberación (`free(9)` frente a `free(3)` de libc). Una vez que la versión de userland es correcta, la versión del kernel es casi un copiar y pegar con un pequeño ajuste.

La tercera es que construir el buffer una vez en aislamiento te obliga a pensar en su API con calma. Cuando estés listo para integrarlo en el driver, ya sabrás qué devuelve `cbuf_write`, qué devuelve `cbuf_read`, qué significa `cbuf_used` y cómo se supone que funciona el wrap-around. Nada de eso tendrá que reaprenderse en medio de una sesión del kernel.

### Cosas que aún pueden salir mal

Incluso con los helpers anteriores, hay algunos errores que vale la pena señalar ahora para que no los cometas en la sección 3.

El primero es **olvidar limitar la petición al espacio disponible**. Si `cbuf_write` se llama con `n = 100` sobre un buffer con `free = 30`, la función devuelve 30, no 100. Los llamadores deben comprobar el valor de retorno y actuar en consecuencia. El `d_write` del driver traducirá esto en una escritura parcial dejando `uio_resid` en la cantidad no consumida. Seremos muy explícitos al respecto en la sección 4.

El segundo es **olvidar que `cbuf_used` y `cbuf_free` pueden cambiar entre dos comprobaciones**. En las pruebas de userland con un solo thread esto es imposible. En el kernel, un thread diferente puede modificar el buffer entre dos llamadas a función cualesquiera si no se mantiene ningún lock. La sección 3 mantiene el mutex del softc alrededor de todo acceso al buffer; la sección 7 explica el porqué.

El tercero es **confundir los índices**. Algunas implementaciones rastrean el tail explícitamente y el recuento implícitamente. Otras hacen lo contrario. Ambas funcionan. Mezclar las dos en un único buffer no funciona. Elige una y mantenla. Nosotros elegimos "head y used"; el tail siempre se deriva.

El cuarto es **el desbordamiento entero de los propios índices**. Con `size_t` y un buffer de unos pocos miles de bytes, los índices nunca pueden superar `cb_size`, y `(cb_head + n) % cb_size` siempre está bien definido. Si alguna vez amplías este código a un buffer mayor que `SIZE_MAX / 2`, eso ya no será cierto; necesitarías índices de 64 bits y aritmética modular explícita. Para nuestro pseudodispositivo con un buffer de 4 KB o 64 KB, la estructura básica es más que suficiente.

### Cerrando la sección 2

Ahora tienes un buffer circular limpio, probado y orientado a bytes. Limita las peticiones al espacio disponible, informa del tamaño real de la transferencia y gestiona el wrap-around en el único lugar donde tiene sentido: dentro del propio buffer. Las pruebas de userland te dan una pequeña evidencia de que la implementación se comporta tal como indicaban los diagramas.

La sección 3 lleva este código al kernel. La forma permanece casi idéntica; lo que cambia son la asignación de memoria y la sincronización. Al final de la siguiente sección, el `d_read` y el `d_write` de tu driver llamarán a `cbuf_read` y `cbuf_write` en lugar de hacer su propia aritmética, y la lógica que antes vivía directamente en `myfirst.c` tendrá un nombre.

## Sección 3: Integrar un buffer circular en tu driver

La implementación de `cbuf` en userland es el mismo código que estás a punto de introducir en el kernel. Casi. Hay tres pequeños cambios: el asignador, el liberador y un nivel de precaución que el kernel exige y que userland no requiere. Tras la integración, los manejadores de lectura y escritura del driver se reducen considerablemente, y la aritmética del wrap-around desaparece de `myfirst.c` hacia los helpers, donde le corresponde estar.

Esta sección recorre la integración con detenimiento. Empezaremos con la variante del buffer para el kernel, luego pasaremos a los cambios de integración dentro de `myfirst.c` y, por último, veremos cómo añadir algunos controles sysctl que hacen visible el estado interno del driver mientras depuras.

### Llevar `cbuf` al kernel

La cabecera `cbuf.h` para el kernel es idéntica a la de userland:

```c
#ifndef CBUF_H
#define CBUF_H

#include <sys/types.h>

struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);

size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);

size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
```

El archivo `cbuf.c` para el kernel es casi una copia del de userland con dos sustituciones. `malloc(3)` se convierte en `malloc(9)` desde `M_DEVBUF` con los flags `M_WAITOK | M_ZERO`. `free(3)` se convierte en `free(9)` desde `M_DEVBUF`. Las llamadas a `memcpy(3)` siguen siendo válidas en contexto de kernel: el kernel tiene sus propios símbolos `memcpy` y `bcopy`. Aquí está la versión completa para el kernel:

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include "cbuf.h"

MALLOC_DEFINE(M_CBUF, "cbuf", "Chapter 10 circular buffer");

int
cbuf_init(struct cbuf *cb, size_t size)
{
        if (cb == NULL || size == 0)
                return (EINVAL);
        cb->cb_data = malloc(size, M_CBUF, M_WAITOK | M_ZERO);
        cb->cb_size = size;
        cb->cb_head = 0;
        cb->cb_used = 0;
        return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
        if (cb == NULL || cb->cb_data == NULL)
                return;
        free(cb->cb_data, M_CBUF);
        cb->cb_data = NULL;
        cb->cb_size = 0;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
        return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
        return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
        return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
        size_t avail, tail, first, second;

        avail = cbuf_free(cb);
        if (n > avail)
                n = avail;
        if (n == 0)
                return (0);

        tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
        first = MIN(n, cb->cb_size - tail);
        memcpy(cb->cb_data + tail, src, first);
        second = n - first;
        if (second > 0)
                memcpy(cb->cb_data, (const char *)src + first, second);

        cb->cb_used += n;
        return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
        size_t first, second;

        if (n > cb->cb_used)
                n = cb->cb_used;
        if (n == 0)
                return (0);

        first = MIN(n, cb->cb_size - cb->cb_head);
        memcpy(dst, cb->cb_data + cb->cb_head, first);
        second = n - first;
        if (second > 0)
                memcpy((char *)dst + first, cb->cb_data, second);

        cb->cb_head = (cb->cb_head + n) % cb->cb_size;
        cb->cb_used -= n;
        return (n);
}
```

Tres puntos merecen un breve comentario.

El primero es `MALLOC_DEFINE(M_CBUF, "cbuf", ...)`. Esto declara una etiqueta de memoria privada para las asignaciones del buffer, de modo que `vmstat -m` pueda mostrar cuánta memoria está usando el código de cbuf por separado del resto del driver. La declaramos una vez, en `cbuf.c`, con enlace interno respecto al resto del módulo. El softc del driver sigue usando `M_DEVBUF`. Las dos etiquetas pueden coexistir; son etiquetas de contabilidad, no pools.

El segundo es el flag `M_WAITOK`. Como nunca llamamos a `cbuf_init` desde contexto de interrupción (la llamamos desde `myfirst_attach`, que se ejecuta en contexto normal de thread del kernel durante la carga del módulo), es seguro esperar por memoria si el sistema tiene poca momentáneamente. Con `M_WAITOK`, `malloc(9)` no devolverá `NULL`; si la asignación no puede completarse, dormirá hasta que pueda. Por tanto, no necesitamos comprobar si el resultado es `NULL`. Si alguna vez queremos llamar a `cbuf_init` desde un contexto donde el sleep está prohibido, tendríamos que cambiar a `M_NOWAIT` y gestionar un posible `NULL`. Para los propósitos del capítulo 10, `M_WAITOK` es la elección correcta.

El tercero es que **el `cbuf` del kernel no hace locking**. Es una estructura de datos pura. La estrategia de locking es responsabilidad del *llamador*. Dentro de `myfirst.c`, mantendremos `sc->mtx` durante cada llamada a `cbuf`. Eso mantiene la abstracción pequeña y le da al capítulo 11 un objetivo de refactorización limpio.

### Qué cambia en `myfirst.c`

Abre el archivo de la etapa 3 del capítulo 9 en tu editor. La integración implica los siguientes cambios:

1. Sustituir los cuatro campos del softc relacionados con el buffer (`buf`, `buflen`, `bufhead`, `bufused`) por un único miembro `struct cbuf cb`.
2. Eliminar la macro `MYFIRST_BUFSIZE` de `myfirst.c` (la conservamos, pero en una cabecera única para evitar duplicaciones).
3. Inicializar el buffer en `myfirst_attach` con `cbuf_init`.
4. Destruirlo en `myfirst_detach` y en el camino de fallo del attach con `cbuf_destroy`.
5. Reescribir `myfirst_read` para que llame a `cbuf_read` contra un bounce buffer en la pila y luego use `uiomove` para sacar el bounce buffer.
6. Reescribir `myfirst_write` para que use `uiomove` hacia un bounce buffer en la pila y luego `cbuf_write` hacia el anillo.

Los dos últimos cambios merecen una breve discusión antes de mirar el código. ¿Por qué un bounce buffer? ¿Por qué no llamar a `uiomove` directamente contra el almacenamiento de cbuf?

La respuesta es que `uiomove` no entiende los desbordamientos circulares. Espera un destino contiguo (en las lecturas) o un origen contiguo (en las escrituras). Si la región activa de nuestro buffer circular da la vuelta al inicio, llamar a `uiomove(cb->cb_data + cb->cb_head, n, uio)` copiaría más allá del final de la memoria subyacente y entraría en lo que se haya asignado a continuación. Es un bug de corrupción del heap que solo espera la ocasión para manifestarse. Existen dos formas seguras; puedes elegir cualquiera de las dos.

La primera forma segura consiste en llamar a `uiomove` *dos veces*, una por cada lado de la vuelta. El driver calcula el fragmento contiguo disponible en `cb->cb_data + cb->cb_head`, llama a `uiomove` para ese fragmento y luego llama a `uiomove` de nuevo para la porción que da la vuelta, en `cb->cb_data + 0`. Esto es eficiente porque no requiere ninguna copia adicional. Sin embargo, es también más complejo y más difícil de implementar correctamente: el driver tiene que llevar una contabilidad parcial de `uio_resid` entre las dos llamadas a `uiomove`, y cualquier cancelación a mitad (señal, fallo de página) deja el buffer en un estado parcialmente vaciado.

La segunda forma segura consiste en usar un **bounce buffer** en el kernel: una pequeña variable temporal en el stack que solo existe durante la duración de la llamada de I/O. El driver lee bytes del cbuf hacia el bounce buffer mediante `cbuf_read` y luego traslada el bounce buffer al espacio de usuario con `uiomove`. En la escritura el proceso es inverso: `uiomove` trae los datos del espacio de usuario al bounce buffer, y luego `cbuf_write` escribe el bounce buffer en el cbuf. El coste es una copia adicional en el kernel por fragmento; a cambio, se gana simplicidad, localidad en el manejo de errores y la posibilidad de concentrar toda la lógica de vuelta dentro del cbuf, que es donde pertenece.

El enfoque del bounce buffer es el que utilizaremos en este capítulo. Es el mismo que emplean drivers como `evdev/cdev.c` (con `bcopy` entre el anillo por cliente y una estructura `event` residente en el stack, antes de trasladar esa estructura al espacio de usuario con `uiomove`). El bounce residente en el stack es pequeño (256 o 512 bytes son más que suficientes), el bucle se ejecuta tantas veces como lo exija el tamaño de transferencia del usuario y cada iteración puede reiniciarse de forma independiente si `uiomove` falla. El coste en rendimiento es despreciable en todos los casos salvo en los drivers de hardware con un throughput muy elevado, y aun en esos casos la compensación suele valer la pena por la ganancia en legibilidad.

### El driver de la etapa 2: handlers refactorizados

Así quedan las partes relevantes del driver tras la integración. El código fuente completo está en `examples/part-02/ch10-handling-io-efficiently/stage2-circular/myfirst.c`. Mostraremos los handlers de E/S aquí mismo y luego repasaremos qué ha cambiado.

```c
#define MYFIRST_BUFSIZE         4096
#define MYFIRST_BOUNCE          256

struct myfirst_softc {
        device_t                dev;
        int                     unit;

        struct mtx              mtx;

        uint64_t                attach_ticks;
        uint64_t                open_count;
        uint64_t                bytes_read;
        uint64_t                bytes_written;

        int                     active_fhs;
        int                     is_attached;

        struct cbuf             cb;

        struct cdev            *cdev;
        struct cdev            *cdev_alias;

        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid      *sysctl_tree;
};

static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = cbuf_read(&sc->cb, bounce, take);
                if (got == 0) {
                        mtx_unlock(&sc->mtx);
                        break;          /* empty: short read or EOF */
                }
                sc->bytes_read += got;
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                room = cbuf_free(&sc->cb);
                mtx_unlock(&sc->mtx);
                if (room == 0)
                        break;          /* full: short write */

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = cbuf_write(&sc->cb, bounce, want);
                sc->bytes_written += put;
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                /*
                 * cbuf_write may store less than 'want' if another
                 * writer slipped in between our snapshot of 'room'
                 * and our cbuf_write call and consumed some of the
                 * space we had sized ourselves against.  With a single
                 * writer that cannot happen and put == want always.
                 * We still handle it defensively: a serious driver
                 * would reserve space up front to avoid losing bytes,
                 * and Chapter 11 will revisit this with proper
                 * multi-writer synchronization.
                 */
                if (put < want) {
                        /*
                         * The 'want - put' bytes we copied into 'bounce'
                         * with uiomove have already left the caller's
                         * uio and cannot be pushed back.  Record the
                         * loss by breaking out of the loop; the kernel
                         * will report the bytes actually stored via
                         * uio_resid.  This path is only reachable under
                         * concurrent writers, which the design here
                         * does not yet handle.
                         */
                        break;
                }
        }
        return (0);
}
```

Varios aspectos han cambiado respecto a la etapa 3 del capítulo 9.

El primer cambio es el **bucle**. Ahora ambos handlers iteran hasta que `uio_resid` llega a cero o hasta que el buffer no puede satisfacer la siguiente iteración. Cada iteración transfiere como máximo `sizeof(bounce)` bytes, que es el tamaño del bounce de pila. Para una petición pequeña, el bucle se ejecuta una sola vez. Para una petición grande, se ejecuta tantas veces como sea necesario. Esto es lo que hace que la E/S parcial funcione de forma limpia: los handlers producen de forma natural una lectura o escritura corta cuando el buffer alcanza un límite.

El segundo cambio es que **todos los accesos al buffer están delimitados por `mtx_lock`/`mtx_unlock`**. La estructura de datos `cbuf` no tiene conocimiento del locking; es el driver quien lo proporciona. Mantenemos el lock durante cada llamada a `cbuf_*` y durante cada actualización de los contadores de bytes. *No* mantenemos el lock durante la llamada a `uiomove(9)`. Mantener un mutex durante `uiomove` es un error real en FreeBSD: `uiomove` puede dormir ante un fallo de página, y dormir con un mutex retenido produce un pánico de tipo sleep-with-mutex. El recorrido del capítulo 9 ya trató este punto; ahora lo llevamos a la práctica separando el acceso a cbuf (bajo lock) del uiomove (sin lock).

El tercer cambio es que el **handler de lectura devuelve 0** cuando el buffer está vacío, tras haber transferido posiblemente algunos bytes. El comportamiento de la etapa 3 anterior era idéntico en este nivel. Lo que cambia es que la *siguiente* sección hace posible que la lectura se bloquee en su lugar, y la sección posterior añade un camino de `EAGAIN` para los llamadores no bloqueantes. La estructura presentada aquí es la base de ambas extensiones.

El cuarto cambio es que el **handler de escritura respeta las escrituras parciales**. Cuando `cbuf_free(&sc->cb)` devuelve cero, el bucle termina y el handler retorna 0 con `uio_resid` reflejando los bytes no consumidos. La llamada `write(2)` en el espacio de usuario verá un conteo de escritura corto, que es la forma convencional de UNIX de decir: «acepté estos bytes; llámame de nuevo con el resto más tarde». La sección 4 trata extensamente sobre por qué esto es importante y cómo escribir código de usuario que lo gestione.

### Actualización de `attach` y `detach`

Los cambios en el ciclo de vida son pequeños pero reales:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        struct make_dev_args args;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        sc->attach_ticks = ticks;
        sc->is_attached = 1;
        sc->active_fhs = 0;
        sc->open_count = 0;
        sc->bytes_read = 0;
        sc->bytes_written = 0;

        error = cbuf_init(&sc->cb, MYFIRST_BUFSIZE);
        if (error != 0)
                goto fail_mtx;

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_OPERATOR;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;

        error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
        if (error != 0)
                goto fail_cb;

        sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
        if (sc->cdev_alias == NULL)
                device_printf(dev, "failed to create /dev/myfirst alias\n");

        sysctl_ctx_init(&sc->sysctl_ctx);
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Lifetime number of opens");
        SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "active_fhs", CTLFLAG_RD,
            &sc->active_fhs, 0, "Currently open descriptors");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes drained from the FIFO");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_written", CTLFLAG_RD,
            &sc->bytes_written, 0, "Total bytes appended to the FIFO");
        SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_used",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_cb_used, "IU",
            "Live bytes currently held in the circular buffer");
        SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_free",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_cb_free, "IU",
            "Free bytes available in the circular buffer");
        SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_size", CTLFLAG_RD,
            (unsigned int *)&sc->cb.cb_size, 0,
            "Capacity of the circular buffer");

        device_printf(dev,
            "Attached; node /dev/%s (alias /dev/myfirst), cbuf=%zu bytes\n",
            devtoname(sc->cdev), cbuf_size(&sc->cb));
        return (0);

fail_cb:
        cbuf_destroy(&sc->cb);
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}

static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        if (sc->active_fhs > 0) {
                mtx_unlock(&sc->mtx);
                device_printf(dev,
                    "Cannot detach: %d open descriptor(s)\n",
                    sc->active_fhs);
                return (EBUSY);
        }
        mtx_unlock(&sc->mtx);

        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (0);
}
```

Los dos nuevos handlers de sysctl son breves:

```c
static int
myfirst_sysctl_cb_used(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        unsigned int val;

        mtx_lock(&sc->mtx);
        val = (unsigned int)cbuf_used(&sc->cb);
        mtx_unlock(&sc->mtx);
        return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
myfirst_sysctl_cb_free(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        unsigned int val;

        mtx_lock(&sc->mtx);
        val = (unsigned int)cbuf_free(&sc->cb);
        mtx_unlock(&sc->mtx);
        return (sysctl_handle_int(oidp, &val, 0, req));
}
```

Los handlers existen porque queremos una instantánea *consistente* del estado del buffer cuando el usuario lee `sysctl dev.myfirst.0.stats.cb_used`. Leer el campo directamente (como hacía la etapa 3 con `bufused`) introduce una condición de carrera: una escritura concurrente podría estar modificándolo mientras `sysctl(8)` lo lee, produciendo un valor inconsistente. El handler mantiene el mutex durante la lectura, de modo que el valor que ve el usuario es al menos *autoconsistente* (representa el estado del buffer en un momento determinado, no una actualización a medias). El buffer puede cambiar inmediatamente después de que el handler libere el lock, por supuesto; eso está bien, porque para cuando `sysctl(8)` formatea e imprime el número, el buffer ya ha cambiado en muchas ocasiones. Lo que estamos evitando es la lectura de un campo modificado parcialmente, no una lectura obsoleta.

### Registrar el estado del buffer para la depuración

Cuando el driver se comporta de forma inesperada, la primera pregunta casi siempre es: «¿qué estaba haciendo el buffer?». Añadir una pequeña cantidad de llamadas a `device_printf` en los handlers de E/S, protegidas por un flag de depuración controlado por sysctl, permite responder fácilmente a esa pregunta. Este es el patrón:

```c
static int myfirst_debug = 0;
SYSCTL_INT(_dev_myfirst, OID_AUTO, debug, CTLFLAG_RW,
    &myfirst_debug, 0, "Verbose I/O tracing for the myfirst driver");

#define MYFIRST_DBG(sc, fmt, ...) do {                                  \
        if (myfirst_debug)                                              \
                device_printf((sc)->dev, fmt, ##__VA_ARGS__);           \
} while (0)
```

Después, en los handlers de E/S, llama a `MYFIRST_DBG(sc, "read got=%zu used=%zu free=%zu\n", got, cbuf_used(&sc->cb), cbuf_free(&sc->cb));` tras un `cbuf_read` satisfactorio. Con `myfirst_debug` establecido en 0, la macro se reduce a una instrucción vacía y el camino de producción queda intacto. Con `sysctl dev.myfirst.debug=1`, cada transferencia imprime una traza de una línea en `dmesg`, lo cual es inestimable cuando el driver hace algo que no entiendes.

Sé prudente con la cantidad de traza que generas. Una sola línea de registro por transferencia es suficiente. Una línea de registro por cada byte transferido saturaría el ring buffer de `dmesg` en cuestión de segundos y alteraría la temporización del driver lo suficiente como para ocultar algunos errores. El patrón anterior registra una vez por llamada a `cbuf_read` o `cbuf_write`, que equivale a una vez por iteración del bucle, es decir, una vez por cada bloque de hasta 256 bytes. Esa es aproximadamente la granularidad adecuada.

Por último, recuerda establecer `myfirst_debug = 0` antes de cargar el driver en producción. Esa línea existe como ayuda para el desarrollo, no como una característica permanente.

### Por qué `cbuf` tiene su propia etiqueta de memoria

Cuando ejecutas `vmstat -m` en un sistema FreeBSD, ves una larga lista de etiquetas de memoria y la cantidad de memoria que tiene asignada cada una en ese momento. Las etiquetas son una herramienta de observabilidad esencial: si la memoria tiene una fuga en algún punto del kernel, la etiqueta cuyo contador sigue creciendo te indica dónde buscar. Dimos a `cbuf` su propia etiqueta (`M_CBUF`) para que sus asignaciones sean visibles de forma separada del resto de las asignaciones del driver.

Para ver el efecto, carga el driver de la etapa 2 y ejecuta:

```sh
$ vmstat -m | head -1
         Type InUse MemUse Requests  Size(s)
$ vmstat -m | grep -E '(^\s+Type|cbuf|myfirst)'
         Type InUse MemUse Requests  Size(s)
         cbuf     1      4K        1  4096
```

Los cuatro kilobytes corresponden a la única asignación de 4 KB que `cbuf_init` realizó para `sc->cb.cb_data`. Descarga el driver y el contador vuelve a cero. Si en algún momento el contador sube *sin* un attach del driver correspondiente, tienes una fuga en `cbuf_init` o `cbuf_destroy`. Este es el tipo de regresión que de otro modo sería invisible hasta que el sistema se quedara sin memoria horas después.

### Traza rápida de una transferencia alineada y otra con vuelta

Para hacer tangible el comportamiento de vuelta al inicio, tracemos dos escrituras a través del driver de la etapa 2. Supongamos que el buffer está vacío al inicio, la capacidad es de 4096 bytes, y el usuario llama a `write(fd, buf, 100)` seguido más tarde de `write(fd, buf2, 100)`.

La primera escritura pasa por `myfirst_write`:

1. `uio_resid = 100`, `cbuf_free = 4096`, `room = 4096`.
2. Iteración 1 del bucle: `want = MIN(100, 256, 4096) = 100`. `uiomove` copia 100 bytes del espacio de usuario en `bounce`. `cbuf_write(&sc->cb, bounce, 100)` devuelve 100, avanza `cb_used` a 100 y deja `cb_head = 0`. El índice de cola implícito es ahora 100.
3. `uio_resid = 0`. El bucle termina. El handler devuelve 0. El usuario ve un conteo de escritura de 100.

El estado del buffer es: `cb_data[0..99]` contiene los datos, `cb_head = 0`, `cb_used = 100`, `cb_size = 4096`.

Ahora llega la segunda escritura. Antes de que esto ocurra, supongamos que un lector ha consumido 80 bytes, dejando `cb_head = 80`, `cb_used = 20`. El índice de cola implícito está en la posición 100. `myfirst_write` se ejecuta:

1. `uio_resid = 100`, `cbuf_free = 4076`, `room = 4076`.
2. Iteración 1 del bucle: `want = MIN(100, 256, 4076) = 100`. `uiomove` copia 100 bytes del espacio de usuario en `bounce`. `cbuf_write(&sc->cb, bounce, 100)` avanza el índice de cola implícito de 100 a 200, establece `cb_used = 120` y devuelve 100.
3. `uio_resid = 0`. El handler devuelve 0. El usuario ve un conteo de escritura de 100.

Ambas transferencias estaban «alineadas» en el sentido de que ninguna cruzó el final del buffer subyacente. Imaginemos ahora un estado mucho más avanzado en que `cb_head = 4000` y `cb_used = 80`. Los bytes activos ocupan las posiciones 4000..4079, y el índice de cola implícito es 4080. La capacidad es 4096. El espacio libre es de 4016 bytes, pero se divide en dos tramos en la vuelta: 16 bytes contiguos a partir de la posición 4080 y luego 4000 bytes contiguos desde la posición 0.

El usuario llama a `write(fd, buf, 64)`:

1. `uio_resid = 64`, `cbuf_free = 4016`, `room = 4016`.
2. Iteración 1 del bucle: `want = MIN(64, 256, 4016) = 64`. `uiomove` copia 64 bytes en `bounce`. `cbuf_write(&sc->cb, bounce, 64)` realiza lo siguiente:
   - `tail = (4000 + 80) % 4096 = 4080`.
   - `first = MIN(64, 4096 - 4080) = 16`. Copia 16 bytes desde `bounce + 0` hasta `cb_data + 4080`.
   - `second = 64 - 16 = 48`. Copia 48 bytes desde `bounce + 16` hasta `cb_data + 0`.
   - `cb_used += 64`, que pasa a ser 144.
3. `uio_resid = 0`. El handler devuelve 0.

La vuelta fue gestionada dentro de `cbuf_write` y resultó invisible para el driver. Ese es precisamente el objetivo de encapsular la abstracción en su propio archivo. El código fuente de `myfirst.c` no contiene aritmética de vuelta; esta vive en `cbuf.c`, donde puede probarse de forma aislada.

### Lo que ve el usuario

Tras la integración, un programa en espacio de usuario no puede distinguir la forma del buffer. `cat /dev/myfirst` sigue imprimiendo todo lo que se haya escrito, en orden. `echo hello > /dev/myfirst` sigue almacenando `hello` para una lectura posterior. Los contadores de bytes en `sysctl dev.myfirst.0.stats` siguen incrementándose en uno por cada byte. Los nuevos sysctls `cb_used` y `cb_free` exponen el estado del buffer, pero el camino de los datos es idéntico byte a byte al de la etapa 3 del capítulo 9.

Lo que cambia es lo que ocurre *bajo carga*. Con la FIFO lineal, un escritor continuo acababa viendo `ENOSPC` incluso cuando un lector consumía bytes activamente, porque `bufhead` solo volvía a cero cuando `bufused` llegaba a cero. Con el buffer circular, el escritor puede continuar indefinidamente mientras el lector siga al ritmo, porque el espacio libre y los bytes activos pueden ocupar cualquier combinación de posiciones dentro de la memoria subyacente. La capacidad total del buffer es ahora realmente aprovechable.

Podrás apreciar esta diferencia con claridad en la sección 6 cuando ejecutemos `dd` contra el nuevo driver y comparemos los datos de rendimiento con los de la etapa 3 del capítulo 9. Por ahora, acéptalo así y termina la integración.

### Gestión de detach con datos activos en el buffer

Hay una sutileza en el momento del detach que merece atención. Con el buffer circular integrado, el buffer puede contener datos cuando el usuario ejecuta `kldunload myfirst`. El detach del capítulo 9 se negaba a descargarse mientras hubiera algún descriptor abierto; esa comprobación sigue vigente. Sin embargo, no se niega a descargarse si el buffer no está vacío pero no hay ningún descriptor abierto. ¿Debería hacerlo?

La respuesta convencional es no. Un buffer es un recurso transitorio. Si nadie está leyendo el dispositivo en ese momento, los bytes del buffer no van a ser leídos; el usuario ha aceptado implícitamente su pérdida al cerrar todos los descriptores. El camino de detach simplemente libera el buffer junto con todo lo demás. Si quisieras conservar los bytes a través de una descarga (en un archivo, por ejemplo), eso sería una característica nueva, no una corrección de errores, y correspondería al espacio de usuario, no al driver.

Por lo tanto, no realizamos ningún cambio en el ciclo de vida del detach. `cbuf_destroy` se llama de forma incondicional; los bytes se liberan junto con la memoria de respaldo.

### Cerrando la sección 3

El driver usa ahora un buffer circular real. La lógica de vuelta vive en una pequeña abstracción que tiene su propio header, su propio archivo fuente, su propia etiqueta de memoria y su propio programa de prueba en el userland. Los handlers de E/S en `myfirst.c` son más sencillos que en la etapa 3 del capítulo 9, y la aritmética complicada ya no está dispersa por ellos.

Lo que tienes en este punto todavía no gestiona las lecturas y escrituras parciales de forma elegante. Si el usuario llama a `read(fd, buf, 4096)` y el buffer contiene 100 bytes, el bucle se ejecutará exactamente una vez, transferirá 100 bytes y devolverá cero con `uio_resid` reflejando la porción no consumida. Ese es el comportamiento correcto, pero la *explicación* de qué debe esperar el usuario, qué devuelve `read(2)` y cómo itera un llamador bien escrito es de lo que trata la sección 4. También resolveremos la pregunta de qué debería hacer `d_read` cuando el buffer está vacío y el llamador está dispuesto a esperar, que es la puerta de entrada a la E/S no bloqueante en la sección 5.

## Sección 4: mejorar el comportamiento del driver con lecturas y escrituras parciales

El driver de la etapa 2 de la sección anterior ya implementa lecturas y escrituras parciales correctamente, casi de forma accidental. Los bucles en `myfirst_read` y `myfirst_write` terminan cuando el buffer circular ya no puede satisfacer la siguiente iteración, dejando `uio->uio_resid` en la porción de la petición que queda sin consumir. El kernel calcula el conteo de bytes visible para el usuario como el tamaño original de la petición menos ese residuo. Tanto `read(2)` como `write(2)` devuelven ese número al espacio de usuario.

Lo que no hemos hecho es *pensar con claridad* acerca de qué significan esas transferencias parciales desde ambos lados del límite de confianza. Esta sección se ocupa de ese análisis. Al terminarla, sabrás qué programas en espacio de usuario gestionan correctamente las lecturas y escrituras cortas, cuáles no lo hacen, qué debe informar tu driver cuando no hay nada disponible y qué significa la rara transferencia de cero bytes.

### Qué significa «parcial» en UNIX

Un `read(2)` devuelve una de tres cosas:

- Un *entero positivo* menor o igual al número de bytes solicitados: esa cantidad de bytes se ha copiado al buffer del llamador.
- *Cero*: fin de archivo. Este descriptor no producirá más bytes; el llamador debe cerrarlo.
- `-1`: se ha producido un error; el llamador examina `errno` para decidir qué hacer.

El primer caso es donde viven las transferencias parciales. Una lectura «completa» devuelve exactamente la cantidad solicitada. Una lectura «parcial» devuelve menos bytes. UNIX siempre ha permitido lecturas parciales, y cualquier programa que llame a `read(2)` y asuma que obtuvo exactamente lo que pedía está equivocado. Los programas robustos siempre miran el valor de retorno y o bien repiten la llamada hasta tener lo que necesitan, o bien aceptan el resultado parcial y continúan.

`write(2)` sigue la misma forma:

- Un *entero positivo* menor o igual al número de bytes solicitados: el kernel ha aceptado esa cantidad de bytes.
- A veces *cero* (raramente se da en la práctica; generalmente se trata como una escritura corta de cero bytes).
- `-1`: se ha producido un error.

Una escritura corta significa «he aceptado estos bytes; por favor, llámame de nuevo con el resto». Los productores robustos siempre repiten la llamada hasta haber ofrecido todo el payload.

### Por qué los drivers deben abrazar las transferencias parciales

Podría resultar tentador hacer que un driver satisficiera siempre la solicitud completa, aunque para ello tuviera que iterar internamente o esperar. Algunos drivers hacen esto en casos especiales (considera la lectura del driver `null`, que itera internamente para entregar fragmentos de `ZERO_REGION_SIZE` bytes hasta agotar la solicitud del llamador). Sin embargo, para la mayoría de los drivers, abrazar las transferencias parciales es la decisión de diseño correcta por varias razones.

La primera razón es la **capacidad de respuesta**. Un lector que pide 4096 bytes y recibe 100 ya tiene 100 bytes con los que puede empezar a trabajar de inmediato, en lugar de esperar otros 3996 bytes que quizá nunca lleguen. El kernel no tiene que adivinar cuánto tiempo está dispuesto a esperar el llamador.

La segunda razón es la **equidad**. Si `myfirst_read` itera internamente hasta satisfacer la solicitud completa, un único lector codicioso puede mantener el mutex del buffer durante un tiempo indefinido, dejando sin servicio a todos los demás threads que quieran acceder al driver. Un handler que retorna en cuanto no puede avanzar más permite al planificador del kernel preservar la equidad entre los threads que compiten.

La tercera razón es la **corrección frente a señales**. Un lector que está esperando puede recibir una señal (por ejemplo, `SIGINT` cuando el usuario pulsa Ctrl-C). El kernel necesita la oportunidad de entregar esa señal, lo que normalmente implica retornar de la syscall actual. Un handler que itera indefinidamente no le da al kernel esa oportunidad, y el `kill -INT` del usuario se retrasa o se pierde.

La cuarta razón es la **composición con `select(2)` / `poll(2)`**. Los programas que utilizan estas primitivas de disponibilidad asumen explícitamente la semántica de transferencia parcial. Esperan que se les indique «hay datos disponibles» y luego iterar sobre `read(2)` hasta que el descriptor devuelva cero o `EAGAIN`. Un driver que siempre devuelve el número exacto de bytes solicitados rompe el modelo de polling.

Por todas estas razones, los bucles del driver `myfirst` en la Sección 3 están diseñados para hacer un único recorrido por los datos disponibles en el buffer, transferir lo que puedan y retornar. La próxima vez que el llamador quiera más, volverá a llamar a `read(2)`. Esta es la forma convencional en UNIX.

### Informar de conteos de bytes precisos

El mecanismo por el cual el driver reporta una transferencia parcial es `uio->uio_resid`. El kernel lo establece al número de bytes solicitados antes de llamar a `d_read` o `d_write`. El handler es responsable de decrementarlo a medida que transfiere bytes. `uiomove(9)` lo decrementa automáticamente. Cuando el handler retorna, el kernel calcula el conteo de bytes como `original_resid - uio->uio_resid` y lo devuelve al espacio de usuario.

Esto significa que el handler debe hacer exactamente dos cosas de forma consistente:

1. Usar `uiomove(9)` (o uno de sus compañeros, `uiomove_frombuf(9)`, `uiomove_nofault(9)`) para cada movimiento de bytes que cruce el límite de confianza. Esto es lo que mantiene `uio_resid` en valores correctos.
2. Retornar cero cuando haya hecho todo lo que puede, independientemente de si `uio_resid` es ahora cero o algún número positivo.

Un handler que devuelve un *entero positivo* está equivocado. El kernel ignora los retornos positivos; el conteo de bytes se calcula a partir de `uio_resid`. Devolver un entero positivo sería un desperdicio silencioso. Un handler que devuelve un número *negativo*, o cualquier valor que no esté en `errno.h`, tiene comportamiento indefinido.

Una variante común y peligrosa de este error es devolver `EAGAIN` cuando el buffer está vacío *habiendo transferido ya algunos bytes* antes en la misma llamada. El `read(2)` en espacio de usuario vería `-1`/`EAGAIN`, y los bytes que ya estaban en el buffer del usuario se considerarían silenciosamente no entregados. El patrón correcto es: si el handler ha transferido algún byte, devuelve 0 y deja que el conteo parcial hable por sí solo; solo si ha transferido *cero* bytes puede devolver `EAGAIN`. La Sección 5 codificará esta regla cuando añadamos soporte para el modo no bloqueante.

### Fin de datos: ¿cuándo debe `d_read` devolver cero?

La regla «cero significa EOF» de UNIX tiene una consecuencia interesante para los pseudodispositivos. Un archivo regular tiene un final definido: cuando `read(2)` lo alcanza, el kernel devuelve cero. Un dispositivo de caracteres normalmente no tiene un final definido. Una línea serie, un teclado, un dispositivo de red, una cinta rebobinando más allá del final del soporte: cada uno de estos *podría* devolver cero en casos especiales, pero en funcionamiento normal «no hay datos disponibles ahora mismo» no es lo mismo que «no habrá datos nunca más».

Sin embargo, un `myfirst_read` ingenuo que devuelve cero siempre que el buffer está vacío es indistinguible, desde el punto de vista del llamador, de un archivo regular al final de su contenido. Un `cat /dev/myfirst` verá cero bytes, lo interpretará como EOF y terminará. Eso no es lo que queremos. Queremos que el lector espere hasta que lleguen más bytes, o que se le indique «no hay bytes ahora, pero inténtalo más tarde», dependiendo del modo del descriptor de archivo.

Hay dos estrategias habituales.

La primera estrategia es **bloquear por defecto**. `myfirst_read` espera en una cola de sleep cuando el buffer está vacío, y el escritor despierta la cola cuando añade bytes. La lectura devuelve cero solo si alguna condición señala un verdadero fin de archivo (el dispositivo ha sido extraído o el escritor ha cerrado explícitamente). Esto es lo que hacen la mayoría de los pseudodispositivos y la mayoría de los dispositivos de tipo TTY. Coincide con la expectativa de `cat` de que un terminal entregará líneas a medida que el usuario las teclea.

La segunda estrategia es **retornar inmediatamente con `EAGAIN` para llamadores no bloqueantes**. Si el descriptor fue abierto con `O_NONBLOCK` (o el usuario estableció el flag posteriormente con `fcntl(2)`), `myfirst_read` devuelve `-1`/`EAGAIN` en lugar de bloquearse. Esto permite a los programas basados en bucles de eventos utilizar `select(2)`, `poll(2)` o `kqueue(2)` para multiplexar muchos descriptores sin comprometerse a esperar en ninguno en particular.

La Sección 5 implementará ambas estrategias. El camino bloqueante es el predeterminado; el no bloqueante se activa cuando `IO_NDELAY` está establecido en `ioflag`. Por ahora, en la Etapa 2, el driver todavía devuelve cero cuando el buffer está vacío, igual que en el Capítulo 9. Es un estado temporal; nada en el espacio de usuario se mantiene estable cuando el camino de datos puede desaparecer en cualquier momento.

### Contrapresión en el lado de escritura

El espejo de «no hay datos ahora mismo» es «no hay espacio ahora mismo». Cuando el buffer está lleno y un escritor pide añadir más bytes, el driver tiene que decidir qué responder.

El driver de la Etapa 3 del Capítulo 9 devolvía `ENOSPC`, que es la señal convencional de «el dispositivo se ha quedado sin espacio, permanentemente». Era una elección defendible en el Capítulo 9 porque el FIFO lineal realmente no podía aceptar más datos hasta que el buffer se vaciara completamente. Con el buffer circular, sin embargo, «lleno» es un estado transitorio: el escritor solo necesita esperar hasta que un lector haya consumido algo. Por tanto, en estado estacionario la respuesta correcta no es `ENOSPC`; es o bien un sleep bloqueante hasta que haya espacio, o `EAGAIN` para llamadores no bloqueantes.

La implementación de la Etapa 2 ya gestiona correctamente el caso de escritura parcial: cuando el buffer se llena a mitad de transferencia, el bucle termina y el usuario ve un conteo de escritura menor que la solicitud. Lo que *no* hace todavía es lo correcto cuando el buffer está lleno *al inicio* de la llamada: devuelve 0 sin haber transferido ningún byte, lo que el kernel convierte en un retorno de `write(2)` de cero. Un retorno de cero desde `write(2)` es técnicamente válido, pero es algo extraño de ver, y la mayoría de los programas de usuario lo tratarán como un error o iterarán indefinidamente esperando que deje de ser cero.

La solución convencional, de nuevo, depende del modo. Un escritor bloqueante debe dormir hasta que haya espacio disponible; un escritor no bloqueante debe recibir `EAGAIN`. Los implementaremos ambos en la Sección 5. La estructura del bucle de la Etapa 2 ya es correcta para ambos casos; lo que falta es la decisión sobre qué hacer cuando no se ha producido *ningún* avance en la primera iteración.

### Lecturas y escrituras de longitud cero

Una lectura o escritura de longitud cero es una llamada perfectamente válida. `read(fd, buf, 0)` y `write(fd, buf, 0)` son syscalls válidas; existen explícitamente para que los programas puedan validar un descriptor de archivo sin comprometerse a una transferencia. El kernel las pasa al driver con `uio->uio_resid == 0`.

Tu handler no debe entrar en pánico, devolver un error ni iterar en este caso. El driver de la Etapa 2 hace naturalmente lo correcto: el bucle `while (uio->uio_resid > 0)` nunca se ejecuta, y el handler devuelve 0 con `uio_resid` todavía en 0. El usuario ve que `read(2)` o `write(2)` devuelve cero. Los programas que llaman a I/O de longitud cero para validar descriptores obtienen el resultado que esperan.

Ten cuidado con añadir retornos anticipados de «¿está vacía la solicitud?» al inicio del handler. Parecen una pequeña optimización, pero introducen ramificaciones que es fácil hacer mal. Se aplica la regla de la hoja de referencia del Capítulo 9: `if (uio->uio_resid == 0) return (EINVAL);` es un bug.

### Un recorrido en espacio de usuario por el bucle

Observar lo que hace un programa de usuario con una transferencia parcial es la mejor manera de interiorizar el contrato. Aquí tienes un pequeño lector escrito en estilo UNIX idiomático:

```c
static int
read_all(int fd, void *buf, size_t want)
{
        char *p = buf;
        size_t left = want;
        ssize_t n;

        while (left > 0) {
                n = read(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return (-1);
                }
                if (n == 0)
                        break;          /* EOF */
                p += n;
                left -= n;
        }
        return (int)(want - left);
}
```

`read_all` continúa llamando a `read(2)` hasta que tiene todos los `want` bytes, ve el fin de archivo o encuentra un error real. Las lecturas cortas se absorben de forma transparente. Un `EINTR` provocado por una señal causa un reintento. La función devuelve el número real de bytes obtenidos.

Un `write_all` correctamente escrito es la imagen especular:

```c
static int
write_all(int fd, const void *buf, size_t have)
{
        const char *p = buf;
        size_t left = have;
        ssize_t n;

        while (left > 0) {
                n = write(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return (-1);
                }
                if (n == 0)
                        break;          /* unexpected; treat as error */
                p += n;
                left -= n;
        }
        return (int)(have - left);
}
```

`write_all` llama a `write(2)` repetidamente hasta que el kernel ha aceptado todo el payload. Las escrituras cortas se absorben de forma transparente. Un `EINTR` causa un reintento. La función devuelve el número de bytes aceptados.

Ambos helpers deben estar en el mismo archivo (o en una cabecera de utilidades compartidas) porque casi siempre se usan juntos. Son cortos, robustos, y hacen que el código en espacio de usuario que habla con tu driver se comporte correctamente incluso cuando el driver realiza transferencias parciales. Usaremos ambos en los programas de prueba que construirás en la Sección 6.

### Lo que `cat`, `dd` y similares hacen realmente

Las herramientas del sistema base que has estado usando para probar el driver gestionan las lecturas y escrituras cortas de forma diferente. Vale la pena saber qué hace cada una para poder interpretar lo que observas.

`cat(1)` lee con un buffer de `MAXBSIZE` (16 KB en FreeBSD 14.3) y escribe lo que obtiene en un bucle. Las lecturas cortas del descriptor fuente se absorben; `cat` simplemente hace otra llamada a `read(2)`. Las escrituras cortas al descriptor de destino también se absorben; `cat` escribe la cola no consumida en una llamada posterior. Desde el punto de vista de `cat`, el tamaño de las transferencias no importa; simplemente sigue moviendo bytes hasta que ve el fin de archivo en la fuente.

`dd(1)` es más rígido. Lee bloques de `bs=` bytes (512 por defecto) y escribe exactamente lo que recibió con el mismo tamaño de bloque. Lo importante es que `dd` *no* itera sobre una lectura corta por defecto. Si `read(2)` devuelve 100 bytes cuando `bs=4096`, `dd` escribe un bloque de 100 bytes e incrementa su contador de lecturas cortas. La salida que ves al final (`X+Y records in / X+Y records out`) se divide entre registros completos (`X`) y registros cortos (`Y`). El número total de bytes es lo que importa; la división te indica si el origen estaba produciendo lecturas cortas.

Existe un flag de `dd`, `iflag=fullblock`, que hace que itere sobre el origen de la misma manera que `cat`. Úsalo cuando quieras medir el rendimiento sin el ruido de las lecturas cortas: `dd if=/dev/myfirst of=/dev/null bs=4k iflag=fullblock`. Sin ese flag, verás los registros divididos por cada lectura corta.

`hexdump(1)` lee un byte a la vez por defecto, pero se le puede indicar que lea bloques más grandes. No le afectan las lecturas cortas del origen.

`truss(1)` traza cada syscall, incluidos los conteos de bytes que devuelve cada una. Ejecutar un productor o consumidor bajo `truss` es la forma más directa de ver qué conteos de bytes está devolviendo tu driver. Si ejecutas `truss -f -t read,write cat /dev/myfirst`, la salida te dirá exactamente cuántos bytes devolvió cada `read(2)`, y podrás correlacionarlo con `cb_used` en `sysctl`.

### Errores frecuentes en el código de transferencia parcial

Los siguientes errores son los que aparecen con mayor frecuencia en el código de drivers escrito por principiantes. Todos tienen la misma forma: el handler hace algo que parece razonable en un caso de prueba individual y se comporta mal de forma silenciosa bajo carga.

**Error 1: devolver el recuento de bytes desde `d_read` o `d_write`.** Un handler que hace `return ((int)nbytes);` en lugar de `return (0);` está en un error. El kernel ignora el valor positivo (porque los valores de retorno positivos no son valores errno válidos) y calcula el recuento de bytes a partir de `uio_resid`. El handler que devuelve `nbytes` y *además* hace lo correcto con `uiomove` funciona de manera accidental; el handler que devuelve `nbytes` y omite el paso de `uiomove` corrompe datos de forma silenciosa. No inventes tu propia convención de retorno.

**Error 2: devolver `EAGAIN` tras una transferencia parcial.** Un handler que ya ha consumido algunos bytes de `uio` y luego devuelve `EAGAIN` porque no hay más disponibles descarta silenciosamente los bytes que el usuario ya recibió. La regla correcta es: si has transferido algún byte, devuelve 0; solo si no has transferido ningún byte puedes devolver un errno como `EAGAIN`.

**Error 3: rechazar transferencias de longitud cero.** Como se señaló anteriormente, `read(fd, buf, 0)` y `write(fd, buf, 0)` son operaciones válidas. Un handler que devuelve `EINVAL` cuando `uio_resid` es cero rompe los programas que usan I/O de longitud cero para validar descriptores.

**Error 4: ejecutar un bucle dentro del handler cuando el buffer está vacío.** Un handler que gira en un bucle dentro del kernel esperando que aparezcan datos bloquea el thread llamante *y* todos los threads que quieren adquirir el mismo lock. El mecanismo correcto para esperar es `mtx_sleep(9)` o `cv_wait(9)`, no un bucle de espera activa. La sección 5 trata este tema.

**Error 5: mantener el mutex del buffer durante la llamada a `uiomove`.** Este es, con diferencia, el error más frecuente en el código de drivers escrito por principiantes. `uiomove` puede dormirse en un fallo de página. Dormirse mientras se mantiene un mutex que no permite sleep provoca un pánico de `KASSERT` en kernels compilados con `INVARIANTS` y una advertencia de `WITNESS` en kernels que tienen `WITNESS` activado; en un kernel de producción compilado sin ninguna de las dos opciones, el mismo patrón puede igualmente provocar un deadlock en la máquina o corromper el estado de forma silenciosa cuando el fallo de página intenta cargar en memoria una página de usuario. En cualquier caso, el comportamiento es incorrecto, y el kernel de prueba debería detectarlo antes de que llegue a producción. Los handlers de la Etapa 2 liberan cuidadosamente el mutex antes de llamar a `uiomove`. Repite este patrón en todos los handlers que escribas.

**Error 6: no respetar la señal del usuario.** Un handler bloqueante que no pasa `PCATCH` a `mtx_sleep(9)` o `tsleep(9)` no puede ser interrumpido por una señal. El Ctrl-C del usuario se ignora silenciosamente, y solo `kill -9` liberará el thread. Permite siempre que las señales interrumpan una espera y gestiona siempre el `EINTR` resultante de forma limpia.

**Error 7: confiar en `uio->uio_resid` tras un error.** Cuando `uiomove` devuelve un error distinto de cero (por ejemplo, `EFAULT` porque el buffer de espacio de usuario no es válido), `uio_resid` puede haberse decrementado parcialmente o por completo, dependiendo de en qué punto de la transferencia se produjo el fallo. La convención es: propaga el error, no reintentes la operación y acepta que el recuento de bytes visible para el usuario puede incluir algunos bytes que llegaron antes del fallo. Esto es poco frecuente en la práctica, y el usuario obtiene `EFAULT` junto con un recuento de bytes que le permite recuperarse.

### Un ejemplo concreto: cómo observar lecturas parciales

Para que esto sea tangible, carga el driver de la Etapa 2, escribe unos cientos de bytes en él y observa cómo un pequeño lector los recoge por fragmentos. Con el driver cargado:

```sh
$ printf 'aaaaaaaaaaaaaaaaaaaa' > /dev/myfirst              # 20 bytes
$ printf 'bbbbbbbbbbbbbbbbbbbb' > /dev/myfirst              # 20 more
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 40
```

El buffer contiene 40 bytes. Ahora ejecuta un pequeño lector, rastreado con `truss`:

```c
/* shortreader.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
        int fd = open("/dev/myfirst", O_RDONLY);
        char buf[1024];
        ssize_t n;

        n = read(fd, buf, sizeof(buf));
        printf("read 1: %zd\n", n);
        n = read(fd, buf, sizeof(buf));
        printf("read 2: %zd\n", n);
        close(fd);
        return (0);
}
```

```sh
$ cc -o shortreader shortreader.c
$ truss -t read,write ./shortreader
... read(3, ...) = 40 (0x28)
read 1: 40
... read(3, ...) = 0 (0x0)
read 2: 0
```

El primer `read(2)` devolvió 40 aunque el usuario pidió 1024. Se trata de una lectura parcial, y es el comportamiento correcto. El segundo `read(2)` devolvió 0 porque el buffer estaba vacío. Con la Etapa 2, el cero es un sustituto de "no hay datos ahora mismo"; con la Etapa 3 (después de añadir el bloqueo) la segunda lectura se dormirá hasta que lleguen más datos.

Haz lo mismo con un buffer más pequeño para observar lecturas parciales en una transferencia mayor:

```sh
$ dd if=/dev/zero bs=1m count=8 | dd of=/dev/myfirst bs=4096 2>/tmp/dd-w &
$ dd if=/dev/myfirst of=/dev/null bs=512 2>/tmp/dd-r
```

Cuando el escritor produce bloques de 4096 bytes más rápido de lo que el lector consume bloques de 512 bytes, el buffer se llena. Las llamadas `write(2)` del escritor empiezan a devolver recuentos cortos, y `dd` registra cada llamada corta como un registro parcial. El lector sigue leyendo de 512 en 512. Cuando detengas ambos procesos, observa la línea `records out` en `/tmp/dd-w` y la línea `records in` en `/tmp/dd-r`; el segundo número de cada línea es el recuento de registros cortos.

Este es el comportamiento correcto. El driver está haciendo exactamente lo que debe hacer un dispositivo UNIX: dejar que cada extremo avance a su propio ritmo, informar de las transferencias parciales con honestidad y no bloquearse nunca cuando no hay nada que esperar. Sin la semántica de transferencia parcial, el escritor habría recibido `ENOSPC` (el comportamiento del Capítulo 9) y `dd` se habría detenido.

### Cerrando la sección 4

Los handlers de lectura y escritura del driver son ahora correctamente conscientes de las transferencias parciales. No hemos cambiado el código de la Sección 3; simplemente hemos hecho explícito el comportamiento y hemos construido el vocabulario necesario para hablar de él. Ya sabes qué devuelven `read(2)` y `write(2)` cuando solo algunos bytes están disponibles, sabes cómo escribir bucles en espacio de usuario que gestionen esos retornos y sabes qué herramientas del sistema base manejan las transferencias parciales de forma elegante y cuáles necesitan una opción específica.

Lo que todavía falta es el comportamiento correcto cuando el buffer está *completamente* vacío (para lecturas) o *completamente* lleno (para escrituras). El driver de la Etapa 2 todavía devuelve cero o se detiene sin progreso; eso es un sustituto del comportamiento más correcto que estamos a punto de añadir. La Sección 5 presenta las I/O no bloqueantes, la ruta de sleep bloqueante y `EAGAIN`. Después de eso, el driver se comportará correctamente bajo todas las combinaciones posibles de estado del buffer y modo del llamante.

## Sección 5: implementación de las I/O no bloqueantes

Hasta este punto del capítulo, el driver ha estado haciendo una de dos cosas cuando un llamante solicita una transferencia que no puede satisfacerse en este momento. Devuelve cero (en la lectura, imitando el fin de archivo) o se detiene a mitad del bucle sin haber transferido ningún byte (en la escritura, indicando al usuario "cero bytes aceptados"). Ninguno de estos comportamientos es lo que debe hacer un dispositivo de caracteres real. Esta sección reemplaza ambos con los dos comportamientos correctos: una espera bloqueante para el caso por defecto y un `EAGAIN` limpio para los llamantes que abrieron el descriptor en modo no bloqueante.

Antes de tocar el driver, asegurémonos de que entendemos qué significa "no bloqueante" desde cada lado del límite de confianza. Ese vocabulario es lo que une la implementación.

### Qué son las I/O no bloqueantes

Un descriptor **bloqueante** es aquel para el que `read(2)` y `write(2)` pueden dormirse. Si el driver no tiene datos disponibles, `read(2)` espera; si el driver no tiene espacio disponible, `write(2)` espera. El thread llamante queda suspendido, posiblemente durante mucho tiempo, hasta que se pueda hacer progreso. Este es el comportamiento por defecto de todos los descriptores de archivo en UNIX.

Un descriptor **no bloqueante** es aquel para el que `read(2)` y `write(2)` no deben dormirse *nunca*. Si el driver no tiene datos ahora mismo, `read(2)` devuelve `-1` con `errno = EAGAIN`. Si el driver no tiene espacio ahora mismo, `write(2)` devuelve `-1` con `errno = EAGAIN`. Se espera que el llamante haga otra cosa (normalmente llamar a `select(2)`, `poll(2)` o `kqueue(2)` para saber cuándo el descriptor estará listo) y luego reintente.

La bandera por descriptor que activa o desactiva el modo no bloqueante es `O_NONBLOCK`. Un programa la establece bien en el momento de `open(2)` (`open(path, O_RDONLY | O_NONBLOCK)`) o más tarde con `fcntl(2)` (`fcntl(fd, F_SETFL, O_NONBLOCK)`). La bandera reside en el campo `f_flag` del descriptor, que es privado de la estructura de archivo; el driver no ve la bandera directamente.

Lo que el driver *sí* ve es el argumento `ioflag` de `d_read` y `d_write`. La capa devfs traduce las banderas del descriptor a bits del `ioflag` que el handler puede comprobar. Concretamente:

- `IO_NDELAY` se activa cuando el descriptor tiene `O_NONBLOCK`.
- `IO_DIRECT` se activa cuando el descriptor tiene `O_DIRECT`.
- `IO_SYNC` se activa en `d_write` cuando el descriptor tiene `O_FSYNC`.

La traducción es incluso más sencilla de lo que parece. Un `CTASSERT` en `/usr/src/sys/fs/devfs/devfs_vnops.c` declara que `O_NONBLOCK == IO_NDELAY`. Los valores de los bits se eligen para que los dos nombres sean intercambiables, y puedes escribir `(ioflag & IO_NDELAY)` o `(ioflag & O_NONBLOCK)` según cuál de las dos convenciones te resulte más clara. Ambas funcionan. El árbol de código fuente de FreeBSD usa `IO_NDELAY` con más frecuencia, así que seguiremos esa convención.

### Cuándo es útil el comportamiento no bloqueante

El modo no bloqueante es el mecanismo subyacente que hace posibles los programas orientados a eventos. Sin él, un único thread que quiere leer de varios descriptores tiene que elegir uno, bloquearse en él e ignorar los demás hasta que se despierte. Con él, un único thread puede comprobar la disponibilidad de varios descriptores, procesar el que esté listo y volver al bucle sin comprometerse nunca a dormirse en ninguno de ellos.

Hay tres tipos de programas que dependen en gran medida de este modo. Un bucle de eventos clásico (`libevent`, `libev` o el patrón basado en `kqueue` que hoy es el estándar en FreeBSD) no hace más que esperar en `kevent(2)` un evento, despacharlo y volver al bucle. Un demonio de red (`nginx`, `haproxy`) usa la misma estructura para gestionar miles de conexiones por thread. Una aplicación en tiempo real (procesamiento de audio, control industrial) necesita una latencia máxima acotada y no puede permitirse un bloqueo prolongado.

Un driver que quiera funcionar bien con estos programas debe implementar correctamente el modo no bloqueante. Devolver el errno equivocado, dormirse cuando `IO_NDELAY` está activado o no notificar a `poll(2)` cuando cambia el estado son comportamientos que producen errores difíciles de diagnosticar.

### La bandera `IO_NDELAY`: cómo llega al driver

Sigue el flujo una vez para entender de dónde viene la bandera. El usuario llama a `read(fd, buf, n)` sobre un descriptor que tiene `O_NONBLOCK` activado. Dentro del kernel:

1. `sys_read` busca el descriptor de archivo y encuentra un `struct file` con `fp->f_flag` que contiene `O_NONBLOCK`.
2. `vn_read` o (para dispositivos de caracteres) `devfs_read_f` ensambla un `ioflag` enmascarando `fp->f_flag` con los bits que interesan a los drivers. En concreto, calcula `ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);`.
3. El `ioflag` calculado se pasa al `d_read` del driver.

Desde el punto de vista del driver, la traducción es completa: `ioflag & IO_NDELAY` es verdadero si y solo si el llamante quiere semántica no bloqueante. Un bit ausente significa bloquear si es necesario. Un bit presente significa no bloquear y devolver `EAGAIN` si es necesario.

En la parte de escritura se aplica el mismo patrón. `devfs_write_f` calcula `ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT | O_FSYNC);` y lo pasa. La comprobación del handler de escritura es simétrica: `ioflag & IO_NDELAY` significa "no bloquear".

### La convención de `EAGAIN`

Cuando el handler del driver decide que no puede hacer progreso y el llamante es no bloqueante, devuelve `EAGAIN`. La capa genérica del kernel lo propaga como `-1` / `errno = EAGAIN` en el nivel de usuario. Se espera que el usuario trate `EAGAIN` como "este descriptor no está listo; espera o inténtalo más tarde", no como un error en el sentido tradicional.

Hay dos detalles sobre `EAGAIN` que merece la pena memorizar.

El primero es que `EAGAIN` y `EWOULDBLOCK` tienen el mismo valor en FreeBSD. Son dos nombres para un único errno. Algunas páginas de manual más antiguas usan `EWOULDBLOCK` en contextos relacionados con sockets y `EAGAIN` en contextos relacionados con archivos; la compatibilidad es total, y cualquiera de los dos nombres es aceptable en el código del driver. El árbol de código fuente de FreeBSD usa `EAGAIN` de forma casi exclusiva para los drivers.

En segundo lugar, `EAGAIN` solo debe devolverse cuando el handler no ha transferido *ningún* byte. Si el handler ya ha movido algunos bytes mediante `uiomove` y luego quiere detenerse porque no pueden moverse más bytes en este momento, debe devolver 0 (no `EAGAIN`). El kernel calculará el número de bytes parciales a partir de `uio_resid` y se lo entregará al usuario. Una llamada posterior del usuario verá entonces `EAGAIN`, porque el buffer seguirá vacío. La regla es: `EAGAIN` significa "sin ningún progreso en esta llamada"; una transferencia parcial significa "hubo progreso, pero menos del solicitado, y ahora debes reintentar para obtener el resto".

Esta es exactamente la regla que introdujo la Sección 4. Aquí la ponemos en práctica en el código.

### La ruta de bloqueo: `mtx_sleep(9)` y `wakeup(9)`

La ruta de bloqueo es el comportamiento por defecto para un descriptor sin `O_NONBLOCK`. Cuando el buffer está vacío, el lector se duerme; cuando un escritor añade bytes, lo despierta. FreeBSD ofrece esto mediante un par de primitivas que se combinan con mutexes.

`mtx_sleep(void *chan, struct mtx *mtx, int priority, const char *wmesg, sbintime_t timo)` pone el thread llamante a dormir en el "canal" `chan` (una dirección arbitraria usada como clave), liberando `mtx` de forma atómica. Cuando el thread se despierta, vuelve a adquirir `mtx` antes de retornar. El argumento `priority` puede incluir `PCATCH` para permitir que la entrega de una señal interrumpa el sueño, y `wmesg` es un nombre corto legible por personas que aparece en `ps -AxH` y herramientas similares. El argumento `timo` especifica el tiempo máximo de espera; cero significa sin timeout.

`wakeup(void *chan)` despierta *todos* los threads que duermen en `chan`. `wakeup_one(void *chan)` despierta solo uno. Para un driver con un único lector, `wakeup` es suficiente; para un driver con múltiples lectores en el que queremos entregar un bloque de trabajo a uno solo, `wakeup_one` suele ser la opción correcta. Para `myfirst` usaremos `wakeup` porque tanto el productor como el consumidor pueden estar esperando, y queremos asegurarnos de que ninguno quede sin atención.

El contrato entre ambos es que el dormidor debe sostener el mutex, comprobar la condición y llamar a `mtx_sleep` *sin* soltar el mutex en el intervalo. `mtx_sleep` suelta el lock de forma atómica y se duerme; cuando retorna, el lock se vuelve a adquirir y el dormidor debe volver a comprobar la condición (los despertares espurios son posibles; un thread concurrente puede haber tomado el byte que esperábamos). El patrón es el clásico bucle `while (condition) mtx_sleep(...)`.

Una lectura de bloqueo mínima en nuestro driver tiene este aspecto:

```c
mtx_lock(&sc->mtx);
while (cbuf_used(&sc->cb) == 0) {
        if (ioflag & IO_NDELAY) {
                mtx_unlock(&sc->mtx);
                return (EAGAIN);
        }
        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
            "myfrd", 0);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error);
        }
        if (!sc->is_attached) {
                mtx_unlock(&sc->mtx);
                return (ENXIO);
        }
}
/* ... now proceed to read from the cbuf ... */
```

Cuatro aspectos merecen comentario.

El primero es la **condición en el bucle while**. Comprobamos `cbuf_used(&sc->cb) == 0`. Mientras eso sea cierto, dormimos. La comprobación en el while es esencial: `mtx_sleep` puede retornar por razones distintas a la aparición de datos (señales, timeouts, despertares espurios o que otro thread haya consumido los datos antes que nosotros). Tras cada retorno de `mtx_sleep`, debemos volver a comprobar.

El segundo es la **ruta EAGAIN**. Si el llamante es no bloqueante y el buffer está vacío, soltamos el lock y retornamos `EAGAIN` sin dormirnos. La comprobación debe realizarse *antes* de `mtx_sleep`, no después; de lo contrario, dormiríamos, nos despertaríamos y entonces descubriríamos que el llamante era no bloqueante desde el principio.

El tercero es el **PCATCH**. Con `PCATCH`, `mtx_sleep` puede retornar `EINTR` o `ERESTART` si se entrega una señal. Propagar ese retorno al usuario es el propósito completo de `PCATCH`: queremos que el Ctrl-C del usuario interrumpa realmente la lectura. Sin `PCATCH`, `SIGINT` queda retenido hasta que el sueño finaliza por alguna otra razón, y el usuario experimenta una espera larga e inexplicable.

El cuarto es la **comprobación de detach**. Después de que `mtx_sleep` retorne, es posible que `myfirst_detach` haya comenzado y que `sc->is_attached` sea ahora cero. Comprobamos esto y retornamos `ENXIO` en ese caso. Esto evita que una lectura continúe contra un driver parcialmente desmontado. La ruta de código del detach debe llamar a `wakeup(&sc->cb)` para liberar a los dormidores antes de destruir el mutex; añadiremos esa llamada más adelante.

### El lado del escritor

La ruta de escritura es la imagen especular:

```c
mtx_lock(&sc->mtx);
while (cbuf_free(&sc->cb) == 0) {
        if (ioflag & IO_NDELAY) {
                mtx_unlock(&sc->mtx);
                return (EAGAIN);
        }
        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
            "myfwr", 0);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error);
        }
        if (!sc->is_attached) {
                mtx_unlock(&sc->mtx);
                return (ENXIO);
        }
}
/* ... now proceed to write into the cbuf ... */
```

Los mismos cuatro puntos aplican: comprobar la condición en un bucle `while`, gestionar `IO_NDELAY` antes de dormir, pasar `PCATCH`, volver a comprobar `is_attached` tras el sueño. Observa que ambos dormidores usan el mismo "canal" (`&sc->cb`). Esto es deliberado. Cuando un lector transfiere bytes fuera del buffer, llama a `wakeup(&sc->cb)` para desbloquear a cualquier escritor que espere espacio. Cuando un escritor transfiere bytes al buffer, llama a `wakeup(&sc->cb)` para desbloquear a cualquier lector que espere datos. Un único canal que despierta "todo lo que está en este buffer" es simple y correcto.

Algunos drivers usan dos canales separados (uno para lectores, uno para escritores) para que el `wakeup` de un lector solo perturbe a los escritores y viceversa. Esto es una optimización válida cuando hay muchos lectores o muchos escritores. Para un pseudodispositivo cuyo uso previsto es un productor y un consumidor, un único canal es a la vez más simple y suficiente.

### Los manejadores completos de la fase 3

Incorporar las comprobaciones no bloqueantes a los manejadores de la fase 2 nos da la fase 3. Este es el aspecto completo:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                while (cbuf_used(&sc->cb) == 0) {
                        if (uio->uio_resid != nbefore) {
                                /*
                                 * We already transferred some bytes
                                 * in an earlier iteration; report
                                 * success now rather than block further.
                                 */
                                mtx_unlock(&sc->mtx);
                                return (0);
                        }
                        if (ioflag & IO_NDELAY) {
                                mtx_unlock(&sc->mtx);
                                return (EAGAIN);
                        }
                        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
                            "myfrd", 0);
                        if (error != 0) {
                                mtx_unlock(&sc->mtx);
                                return (error);
                        }
                        if (!sc->is_attached) {
                                mtx_unlock(&sc->mtx);
                                return (ENXIO);
                        }
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = cbuf_read(&sc->cb, bounce, take);
                sc->bytes_read += got;
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);        /* space may have freed for writers */

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

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

        nbefore = uio->uio_resid;

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                while ((room = cbuf_free(&sc->cb)) == 0) {
                        if (uio->uio_resid != nbefore) {
                                mtx_unlock(&sc->mtx);
                                return (0);
                        }
                        if (ioflag & IO_NDELAY) {
                                mtx_unlock(&sc->mtx);
                                return (EAGAIN);
                        }
                        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
                            "myfwr", 0);
                        if (error != 0) {
                                mtx_unlock(&sc->mtx);
                                return (error);
                        }
                        if (!sc->is_attached) {
                                mtx_unlock(&sc->mtx);
                                return (ENXIO);
                        }
                }
                mtx_unlock(&sc->mtx);

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = cbuf_write(&sc->cb, bounce, want);
                sc->bytes_written += put;
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);        /* data may have appeared for readers */
        }
        return (0);
}
```

Tres patrones de este código merecen un estudio cuidadoso.

El primero es la prueba **"¿se transfirió algún byte?"** al inicio del bucle interno. `uio->uio_resid != nbefore` es verdadero si alguna iteración anterior transfirió datos. Cuando esa condición se cumple y el buffer está ahora vacío (lectura) o lleno (escritura), retornamos 0 inmediatamente en lugar de bloquear. El kernel informará de la transferencia parcial al espacio de usuario, y la siguiente llamada decidirá si bloquear o retornar `EAGAIN`. Esta es la regla de la sección 4 en forma de código: un manejador que ya ha progresado debe retornar 0, no `EAGAIN`, y tampoco debe bloquear más.

El segundo es el **`wakeup` tras el progreso**. Cuando el lector drena bytes, se ha liberado espacio; el escritor puede estar esperando espacio y lo despertamos. Cuando el escritor añade bytes, han aparecido datos; el lector puede estar esperando datos y lo despertamos. Cada cambio de estado va acompañado de un `wakeup`. Omitir un `wakeup` hace que los threads duerman indefinidamente (o hasta que se dispare un temporizador, si existe alguno); las llamadas espurias a `wakeup` son inofensivas porque los bucles while vuelven a comprobar la condición.

El tercero es el **orden de `mtx_unlock` y `uiomove`**. El manejador sostiene el lock mientras manipula el cbuf, y luego lo suelta *antes* de llamar a `uiomove`. `uiomove` puede dormirse; dormirse bajo un mutex es un error. Observa también que en el lado de escritura, el manejador toma una instantánea de `room` mientras sostiene el lock, usa esa instantánea para dimensionar el bounce, y suelta el lock antes de `uiomove`. Si un thread concurrente ha modificado el buffer mientras el manejador copiaba desde el espacio de usuario, el posterior `cbuf_write` puede almacenar menos bytes de los indicados por `want` (el límite en `cbuf_write` garantiza que es seguro). En nuestro diseño actual de escritor único, esta condición de carrera nunca se activa, pero el código la gestiona sin coste adicional.

### Despertar a los dormidores en el detach

También necesitamos enseñar a `myfirst_detach` a liberar a los dormidores. El patrón es:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        if (sc->active_fhs > 0) {
                mtx_unlock(&sc->mtx);
                device_printf(dev,
                    "Cannot detach: %d open descriptor(s)\n",
                    sc->active_fhs);
                return (EBUSY);
        }
        sc->is_attached = 0;
        wakeup(&sc->cb);                /* release any sleepers */
        mtx_unlock(&sc->mtx);

        /* ... destroy_dev, cbuf_destroy, mtx_destroy, sysctl_ctx_free ... */
        return (0);
}
```

Dos detalles de este código son específicos del capítulo 10.

El primero es que establecemos `is_attached = 0` *antes* de llamar a `wakeup`. Un dormidor que se despierte ahora verá el indicador y retornará `ENXIO` en el bucle de bloqueo; un dormidor que aún no haya dormido verá el indicador y retornará `ENXIO` sin haberse dormido nunca. Establecer el indicador después de `wakeup` permitiría una condición de carrera en la que un dormidor vuelve a adquirir el lock, comprueba que la condición sigue siendo verdadera (buffer vacío) y vuelve a dormirse, *mientras* el detach espera para destruir el mutex.

El segundo es que el detach comprueba `active_fhs > 0` y se niega a continuar si algún descriptor está abierto. Esta es la misma comprobación del capítulo 9. Esto significa que un dormidor siempre mantiene un descriptor abierto, lo que implica que el detach no se ejecutará de forma concurrente con un dormidor. La llamada a `wakeup` está ahí como comprobación de doble seguridad: si en el futuro alguna refactorización permite el detach mientras un descriptor sigue abierto, los dormidores no quedarán bloqueados.

### Añadir `d_poll` para `select(2)` y `poll(2)`

Un llamante no bloqueante que recibe `EAGAIN` necesita algún mecanismo para ser notificado cuando el descriptor esté listo. `select(2)` y `poll(2)` son los mecanismos clásicos para ello; `kqueue(2)` es el moderno. Aquí implementaremos los dos clásicos y dejaremos `kqueue` para el capítulo 11 (donde corresponde la infraestructura de `d_kqfilter` y `knlist`).

El manejador `d_poll` tiene una forma simple:

```c
static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int revents = 0;

        mtx_lock(&sc->mtx);
        if (events & (POLLIN | POLLRDNORM)) {
                if (cbuf_used(&sc->cb) > 0)
                        revents |= events & (POLLIN | POLLRDNORM);
                else
                        selrecord(td, &sc->rsel);
        }
        if (events & (POLLOUT | POLLWRNORM)) {
                if (cbuf_free(&sc->cb) > 0)
                        revents |= events & (POLLOUT | POLLWRNORM);
                else
                        selrecord(td, &sc->wsel);
        }
        mtx_unlock(&sc->mtx);
        return (revents);
}
```

`d_poll` recibe los eventos que interesan al usuario y debe retornar el subconjunto que están disponibles en ese momento. Para `POLLIN`/`POLLRDNORM` (legible), retornamos listo si el buffer tiene algún byte. Para `POLLOUT`/`POLLWRNORM` (escribible), retornamos listo si el buffer tiene espacio libre. Si ninguno está listo, llamamos a `selrecord(td, &sc->rsel)` o `selrecord(td, &sc->wsel)` para registrar el thread llamante y poder despertarlo más adelante.

Se necesitan dos nuevos campos en el softc: `struct selinfo rsel;` y `struct selinfo wsel;`. `selinfo` es el registro del kernel, por condición, de los waiters pendientes de `select(2)`/`poll(2)`. Se declara en `/usr/src/sys/sys/selinfo.h`.

Los manejadores de lectura y escritura necesitan una llamada correspondiente a `selwakeup(9)` cada vez que el buffer pase de vacío a no vacío o de lleno a no lleno. `selwakeup(9)` es la forma básica; FreeBSD 14.3 también expone `selwakeuppri(9)`, que despierta los threads registrados con una prioridad especificada y es empleada habitualmente por código de red y almacenamiento que necesita despertares sensibles a la latencia. Para un pseudodispositivo de propósito general, el `selwakeup` básico es la opción predeterminada correcta. Añadimos las llamadas junto a las llamadas a `wakeup(&sc->cb)`:

```c
/* In myfirst_read, after a successful cbuf_read: */
mtx_unlock(&sc->mtx);
wakeup(&sc->cb);
selwakeup(&sc->wsel);   /* space is now available for writers */

/* In myfirst_write, after a successful cbuf_write: */
mtx_unlock(&sc->mtx);
wakeup(&sc->cb);
selwakeup(&sc->rsel);   /* data is now available for readers */
```

El attach inicializa los campos `selinfo` con `knlist_init_mtx(&sc->rsel.si_note, &sc->mtx);` y `knlist_init_mtx(&sc->wsel.si_note, &sc->mtx);` si planeas dar soporte a `kqueue(2)` más adelante. Para un soporte exclusivo de `select(2)`/`poll(2)`, la estructura `selinfo` queda inicializada a cero por la asignación del softc y no necesita configuración adicional.

El detach debe llamar a `seldrain(&sc->rsel);` y `seldrain(&sc->wsel);` antes de liberar el softc, para eliminar cualquier registro de selección pendiente.

Añade `.d_poll = myfirst_poll,` al inicializador de `myfirst_cdevsw` y el soporte de `select(2)`/`poll(2)` del driver quedará completo.

### Cómo usa todo esto un llamante no bloqueante

Juntando las piezas, así es como se ve un lector no bloqueante bien escrito contra `myfirst`:

```c
int fd = open("/dev/myfirst", O_RDONLY | O_NONBLOCK);
char buf[1024];
ssize_t n;
struct pollfd pfd = { .fd = fd, .events = POLLIN };

for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n > 0) {
                /* got some bytes; process them */
        } else if (n == 0) {
                /* EOF; our driver never reaches this case yet */
                break;
        } else if (errno == EAGAIN) {
                /* no data; wait for readiness */
                poll(&pfd, 1, -1);
        } else if (errno == EINTR) {
                /* signal; retry */
        } else {
                perror("read");
                break;
        }
}
close(fd);
```

El bucle lee hasta obtener datos o `EAGAIN`. Ante `EAGAIN`, llama a `poll(2)` para esperar a que el kernel informe de que el descriptor es legible y luego vuelve al inicio del bucle. El evento `POLLIN` se notificará cuando `myfirst_write` ejecute la llamada `selwakeup(&sc->rsel)` que sigue a un `cbuf_write` exitoso. El `d_poll` del driver es el puente entre la maquinaria `select/poll` del kernel y el estado del buffer.

Esta es la forma canónica de I/O UNIX orientado a eventos, y tu driver ahora participa en ella correctamente.

### Una nota sobre la composición de `O_NONBLOCK` con `select`/`poll`

Vale la pena entender cómo interactúan `select(2)` / `poll(2)` y `O_NONBLOCK`. La regla convencional es que un programa usa ambos juntos: registra el descriptor con `poll` y luego lee de él. Usar uno solo es válido pero menos habitual.

Si un programa usa `O_NONBLOCK` sin `poll`, entrará en un bucle de espera activa. Ante cada `EAGAIN` tendrá que llamar a `sleep` o `usleep` antes de reintentar, desperdiciando ciclos sin motivo. Esto casi siempre es incorrecto, pero funciona.

Si un programa usa `poll` sin `O_NONBLOCK`, el `poll` informa de la disponibilidad y luego `read(2)` realiza una llamada bloqueante. La llamada bloqueante se completará casi inmediatamente en el caso normal, porque la condición acaba de notificarse como disponible. Sin embargo, en el caso infrecuente en que el estado del kernel cambie entre el retorno de `poll` y la llamada a `read` (por ejemplo, que otro thread haya drenado el buffer), el `read` se bloqueará indefinidamente. Este es un error sutil, y la mayoría de las bibliotecas orientadas a eventos se defienden de él combinando siempre `poll` con `O_NONBLOCK`.

El driver `myfirst` soporta ambos patrones correctamente. Un programa bien escrito combina los dos; uno menos cuidadoso funcionará en casos simples y tendrá el caso extremo descrito arriba.

### Observar la ruta de bloqueo en acción

Carga el driver de la fase 3 y realiza un experimento rápido:

```sh
$ kldload ./myfirst.ko
$ cat /dev/myfirst &
[1] 12345
```

El `cat` está ahora bloqueado dentro de `myfirst_read`, durmiendo en `&sc->cb`. Puedes confirmarlo con `ps`:

```sh
$ ps -AxH | grep cat
12345  -  S+    0:00.00 myfrd
```

El estado `S+` indica que el proceso está durmiendo, y la columna `wmesg` muestra `myfrd`, que es exactamente la cadena que pasamos a `mtx_sleep`. Ahora escribe en el driver desde otro terminal:

```sh
$ echo hello > /dev/myfirst
```

El `cat` se despierta, lee `hello` y, o bien lo imprime y vuelve a bloquearse, o bien (si el escritor ha cerrado el dispositivo) llega al final del archivo y termina. En nuestro Stage 3 actual no existe ningún mecanismo de "escritor ha cerrado", por lo que el `cat` vuelve a bloquearse tras imprimir. Usa Ctrl-C en su terminal para interrumpirlo:

```sh
$ kill -INT %1
```

Como pasamos `PCATCH` a `mtx_sleep`, la señal despierta al proceso dormido, que devuelve `EINTR`, el cual se propaga hasta `cat` como un `read(2)` fallido. `cat` lo detecta, reconoce la señal y termina limpiamente.

Este es el camino de bloqueo completo en acción. No ha ocurrido nada misterioso; cada pieza es visible en el código fuente y en `ps`.

### Errores comunes en el camino de bloqueo

Dos errores son especialmente comunes en este material.

**Error 1: olvidar liberar el mutex antes de devolver `EAGAIN`.** El código anterior libera el lock explícitamente antes de cada `return` en el bucle de sleep. Si olvidas uno de esos unlocks, los intentos posteriores de tomar el mutex provocarán un panic o un deadlock. Un kernel con `WITNESS` lo detectará de inmediato en un entorno de laboratorio.

**Error 2: usar `tsleep(9)` cuando deberías usar `mtx_sleep(9)`.** `tsleep` no acepta un argumento mutex; asume que el llamador no mantiene ningún interlock. En un driver que usa `mtx_sleep`, el mutex se libera de forma atómica junto con el sleep; con `tsleep`, tendrías que liberar el mutex tú mismo y volver a adquirirlo tras despertar, lo que introduce una ventana de condición de carrera en la que un productor puede añadir datos y llamar a `wakeup` antes de que estés de nuevo en la cola de sleep. `mtx_sleep` es la primitiva correcta para cualquier caso en que mantengas un mutex y quieras hacer sleep liberándolo.

**Error 3: no gestionar los valores de retorno de `PCATCH`.** `mtx_sleep` con `PCATCH` puede devolver `0`, `EINTR`, `ERESTART` o `EWOULDBLOCK` (para timeouts). En el código de un driver, lo convencional es devolver `error` sin inspeccionarlo más; el kernel sabe cómo traducir `ERESTART` en un reinicio del syscall cuando la disposición de señales del proceso lo permite. Inspeccionar el valor y devolver `0` únicamente cuando `error == 0` es el patrón del código de la Etapa 3 anterior.

**Error 4: usar «canales» distintos para `mtx_sleep` y `wakeup`.** El durmiente usa `&sc->cb` como canal; el que despierta debe usar exactamente la misma dirección. Un error habitual es que un sitio use `sc` (el puntero al softc) y otro use `&sc->cb`. Los durmientes no despertarán nunca hasta que se dispare un timeout o un `wakeup` diferente coincida por casualidad. Comprueba siempre que cada par `mtx_sleep` / `wakeup` use el mismo canal.

### Cerrando la Sección 5

El driver gestiona ahora correctamente tanto las llamadas bloqueantes como las no bloqueantes. Un lector bloqueante duerme sobre un buffer vacío y despierta cuando un escritor deposita datos. Un lector no bloqueante recibe `EAGAIN` de inmediato cuando el buffer está vacío. El par simétrico se aplica a los escritores. `select(2)` y `poll(2)` están soportados a través de `d_poll` y el mecanismo `selinfo`, y un programa de bucle de eventos bien comportado puede ahora multiplexar `/dev/myfirst` con otros descriptores. El método detach libera a todos los durmientes antes de desmontar el driver.

Lo que has construido es un dispositivo de caracteres con un comportamiento correcto y completo. Mueve bytes de forma eficiente, coopera con las primitivas de disponibilidad y sleep del kernel, y respeta las convenciones de I/O UNIX orientadas al usuario. Lo que queda en el resto del capítulo es probarlo de forma rigurosa (Sección 6), refactorizarlo para el trabajo de concurrencia (Sección 7), y explorar tres temas complementarios que aparecen con frecuencia junto a este material en drivers reales (`d_mmap`, el concepto de zero-copy, y los patrones de rendimiento del readahead y la coalescencia de escrituras).

## Sección 6: Probando la I/O con buffer mediante programas de usuario

Un driver es tan fiable como las pruebas que ejecutas sobre él. Los capítulos 7 a 9 establecieron un pequeño kit de pruebas (un ejercitador breve `rw_myfirst`, junto con `cat`, `echo`, `dd` y `hexdump`). El Capítulo 10 lleva ese kit más lejos, porque el nuevo comportamiento que el driver exhibe ahora (bloqueo, no bloqueo, I/O parcial, wrap-around) solo se manifiesta bajo carga realista. Esta sección construye tres nuevas herramientas en espacio de usuario y describe un plan de pruebas consolidado que puedes seguir después de cada etapa.

Las herramientas de esta sección se encuentran en `examples/part-02/ch10-handling-io-efficiently/userland/`. Son deliberadamente pequeñas. La más larga tiene menos de 150 líneas. Cada una existe para ejercitar un patrón específico que el driver debe gestionar ahora, y cada una produce una salida que puedes leer y verificar.

### Tres herramientas que construiremos

`rw_myfirst_nb.c` es un probador no bloqueante. Abre el dispositivo con `O_NONBLOCK`, emite una lectura, espera `EAGAIN`, escribe algunos bytes, emite otra lectura, espera recibirlos, e imprime un resumen de una línea para cada paso. Es la herramienta más pequeña que ejercita el camino no bloqueante de extremo a extremo.

`producer_consumer.c` es un arnés de carga basado en fork. Lanza un proceso hijo que escribe bytes aleatorios en el driver a una tasa configurable, mientras el proceso padre los lee y verifica la integridad. El propósito es ejercitar el wrap-around del buffer circular y el camino bloqueante bajo carga concurrente real.

`stress_rw.c` (evolucionado a partir de la versión del Capítulo 9) es un probador de estrés de proceso único que recorre una tabla de combinaciones (tamaño de bloque, número de transferencias) e imprime estadísticas agregadas de temporización y contadores de bytes. El propósito es detectar caídas de rendimiento que una única prueba interactiva no revelaría.

Las tres se compilan con un Makefile breve que mostraremos al final.

### Actualización de `rw_myfirst` para entradas más grandes

El `rw_myfirst` existente del Capítulo 9 gestiona bien las transferencias de tamaño de texto, pero no somete al buffer a una carga de volumen. Una extensión sencilla le permite aceptar un argumento de tamaño en la línea de comandos:

```c
/* rw_myfirst_v2.c: an incremental improvement on Chapter 9's tester. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

static int
do_fill(size_t bytes)
{
        int fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        char *buf = malloc(bytes);
        if (buf == NULL)
                err(1, "malloc %zu", bytes);
        for (size_t i = 0; i < bytes; i++)
                buf[i] = (char)('A' + (i % 26));

        size_t left = bytes;
        ssize_t n;
        const char *p = buf;
        while (left > 0) {
                n = write(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("write at %zu left", left);
                        break;
                }
                p += n;
                left -= n;
        }
        size_t wrote = bytes - left;
        printf("fill: wrote %zu of %zu\n", wrote, bytes);
        free(buf);
        close(fd);
        return (0);
}

static int
do_drain(size_t bytes)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        char *buf = malloc(bytes);
        if (buf == NULL)
                err(1, "malloc %zu", bytes);

        size_t left = bytes;
        ssize_t n;
        char *p = buf;
        while (left > 0) {
                n = read(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("read at %zu left", left);
                        break;
                }
                if (n == 0) {
                        printf("drain: EOF at %zu left\n", left);
                        break;
                }
                p += n;
                left -= n;
        }
        size_t got = bytes - left;
        printf("drain: read %zu of %zu\n", got, bytes);
        free(buf);
        close(fd);
        return (0);
}

int
main(int argc, char *argv[])
{
        if (argc != 3) {
                fprintf(stderr, "usage: %s fill|drain BYTES\n", argv[0]);
                return (1);
        }
        size_t bytes = strtoul(argv[2], NULL, 0);
        if (strcmp(argv[1], "fill") == 0)
                return (do_fill(bytes));
        if (strcmp(argv[1], "drain") == 0)
                return (do_drain(bytes));
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        return (1);
}
```

Con esta herramienta, puedes someter al driver a tamaños realistas. Por ejemplo:

```sh
$ ./rw_myfirst_v2 fill 4096
fill: wrote 4096 of 4096
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 4096
$ ./rw_myfirst_v2 drain 4096
drain: read 4096 of 4096
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 0
```

Ahora intenta llenar el buffer más allá de su capacidad y observa qué ocurre en cada etapa.

### Por qué importa una prueba de ida y vuelta

Toda prueba seria que escribas debe tener un componente de *ida y vuelta*: escribe un patrón conocido en el driver, léelo de vuelta y compara. El patrón importa porque si escribes «Hello, world!» diez veces no puedes saber si el buffer recibió 140 bytes de «Hello, world!» o 130, o 150, o algún intercalado extraño. Un patrón único por posición (como `'A' + (i % 26)` anterior) te permite detectar de un vistazo los desalineamientos, los bytes que faltan y los bytes duplicados.

Las pruebas de ida y vuelta son especialmente importantes para los buffers circulares, porque la aritmética del wrap-around es lo que el código de principiante suele errar. Una escritura que supera el final del almacenamiento subyacente y una lectura que recoge datos desde antes del inicio son los dos modos de fallo que más deseas detectar. Ambos se manifiestan como «los bytes que leo no son los bytes que escribí», y una prueba de ida y vuelta los hace visibles de inmediato.

### Construcción de `rw_myfirst_nb`

Este es el probador no bloqueante. Es algo más largo que el archivo anterior, pero sigue siendo lo bastante corto para leerlo de una vez.

```c
/* rw_myfirst_nb.c: non-blocking behaviour tester for /dev/myfirst. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

int
main(void)
{
        int fd, error;
        ssize_t n;
        char rbuf[128];
        struct pollfd pfd;

        fd = open(DEVPATH, O_RDWR | O_NONBLOCK);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        /* Expect EAGAIN when the buffer is empty. */
        n = read(fd, rbuf, sizeof(rbuf));
        if (n < 0 && errno == EAGAIN)
                printf("step 1: empty-read returned EAGAIN (expected)\n");
        else
                printf("step 1: UNEXPECTED read returned %zd errno=%d\n", n, errno);

        /* poll(POLLIN) with timeout 0 should show not-readable. */
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        error = poll(&pfd, 1, 0);
        printf("step 2: poll(POLLIN, 0) = %d revents=0x%x\n",
            error, pfd.revents);

        /* Write some bytes. */
        n = write(fd, "hello world\n", 12);
        printf("step 3: wrote %zd bytes\n", n);

        /* poll(POLLIN) should now show readable. */
        pfd.events = POLLIN;
        pfd.revents = 0;
        error = poll(&pfd, 1, 0);
        printf("step 4: poll(POLLIN, 0) = %d revents=0x%x\n",
            error, pfd.revents);

        /* Non-blocking read should now succeed. */
        memset(rbuf, 0, sizeof(rbuf));
        n = read(fd, rbuf, sizeof(rbuf));
        if (n > 0) {
                rbuf[n] = '\0';
                printf("step 5: read %zd bytes: %s", n, rbuf);
        } else
                printf("step 5: UNEXPECTED read returned %zd errno=%d\n",
                    n, errno);

        close(fd);
        return (0);
}
```

La salida esperada frente a la Etapa 3 (soporte no bloqueante) es:

```text
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
step 3: wrote 12 bytes
step 4: poll(POLLIN, 0) = 1 revents=0x41
step 5: read 12 bytes: hello world
```

El `0x41` del paso 4 es `POLLIN | POLLRDNORM`, que es exactamente lo que nuestro handler `d_poll` establece cuando el buffer tiene bytes activos.

Si el paso 1 falla (es decir, `read(2)` devuelve `0` en lugar de `-1`/`EAGAIN`), tu driver sigue ejecutando la semántica de la Etapa 2. Vuelve atrás y añade la comprobación de `IO_NDELAY` en los handlers.

Si el paso 2 tiene éxito con `revents != 0`, tu `d_poll` está informando incorrectamente de que hay datos disponibles en un buffer vacío. Comprueba la condición en `myfirst_poll`.

Si el paso 4 devuelve cero (es decir, `poll(2)` no encontró el descriptor con datos disponibles), tu `d_poll` no refleja correctamente el estado del buffer, o falta la llamada a `selwakeup` en el camino de escritura.

Estos son los tres errores no bloqueantes más comunes. El probador los detecta todos en menos de cincuenta líneas de salida.

### Construcción de `producer_consumer.c`

Este es el arnés de carga basado en fork. La estructura es sencilla: hacer fork de un hijo que escribe, que el padre lea, y comparar lo que sale con lo que entró.

```c
/* producer_consumer.c: a two-process load test for /dev/myfirst. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define TOTAL_BYTES     (1024 * 1024)
#define BLOCK           4096

static uint32_t
checksum(const char *p, size_t n)
{
        uint32_t s = 0;
        for (size_t i = 0; i < n; i++)
                s = s * 31u + (uint8_t)p[i];
        return (s);
}

static int
do_writer(void)
{
        int fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "writer: open");

        char *buf = malloc(BLOCK);
        if (buf == NULL)
                err(1, "writer: malloc");

        size_t written = 0;
        uint32_t sum = 0;
        while (written < TOTAL_BYTES) {
                size_t left = TOTAL_BYTES - written;
                size_t block = left < BLOCK ? left : BLOCK;
                for (size_t i = 0; i < block; i++)
                        buf[i] = (char)((written + i) & 0xff);
                sum += checksum(buf, block);

                const char *p = buf;
                size_t remain = block;
                while (remain > 0) {
                        ssize_t n = write(fd, p, remain);
                        if (n < 0) {
                                if (errno == EINTR)
                                        continue;
                                warn("writer: write");
                                close(fd);
                                return (1);
                        }
                        p += n;
                        remain -= n;
                }
                written += block;
        }

        printf("writer: %zu bytes, checksum 0x%08x\n", written, sum);
        close(fd);
        free(buf);
        return (0);
}

static int
do_reader(void)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "reader: open");

        char *buf = malloc(BLOCK);
        if (buf == NULL)
                err(1, "reader: malloc");

        size_t got = 0;
        uint32_t sum = 0;
        int mismatches = 0;
        while (got < TOTAL_BYTES) {
                ssize_t n = read(fd, buf, BLOCK);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("reader: read");
                        break;
                }
                if (n == 0) {
                        /* Only reached if driver signals EOF. */
                        printf("reader: EOF at %zu\n", got);
                        break;
                }
                for (ssize_t i = 0; i < n; i++) {
                        if ((uint8_t)buf[i] != (uint8_t)((got + i) & 0xff))
                                mismatches++;
                }
                sum += checksum(buf, n);
                got += n;
        }

        printf("reader: %zu bytes, checksum 0x%08x, mismatches %d\n",
            got, sum, mismatches);
        close(fd);
        free(buf);
        return (mismatches == 0 ? 0 : 2);
}

int
main(void)
{
        pid_t pid = fork();
        if (pid < 0)
                err(1, "fork");
        if (pid == 0) {
                /* child: writer */
                _exit(do_writer());
        }
        /* parent: reader */
        int rc = do_reader();
        int status;
        waitpid(pid, &status, 0);
        int wexit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        printf("exit: reader=%d writer=%d\n", rc, wexit);
        return (rc || wexit);
}
```

La prueba funciona mejor frente a la Etapa 3 o la Etapa 4 del capítulo. Frente a la Etapa 2 (sin bloqueo), el escritor recibirá escrituras cortas y el lector verá ocasionalmente lecturas de cero bytes, y el total de bytes transferidos puede ser inferior a `TOTAL_BYTES`. Frente a la Etapa 3, ambos lados se bloquean y desbloquean en el momento correcto, la prueba llega a su fin y los dos checksums coinciden.

Una ejecución exitosa tiene este aspecto:

```sh
$ ./producer_consumer
writer: 1048576 bytes, checksum 0x12345678
reader: 1048576 bytes, checksum 0x12345678, mismatches 0
exit: reader=0 writer=0
```

Las discrepancias son el problema grave. Si el checksum del escritor coincide con el del lector pero hay discrepancias distintas de cero, significa que un byte se desplazó de posición durante el viaje de ida y vuelta (probablemente un error de wrap-around). Si los checksums difieren, un byte se perdió o se duplicó (probablemente un error de locking). Si la prueba se bloquea indefinidamente, la condición del camino bloqueante nunca se cumple (probablemente falta un `wakeup`).

### Uso de `dd(1)` para pruebas de volumen

El `dd(1)` del sistema base es la forma más rápida de hacer pasar volumen a través del driver sin escribir código nuevo. Algunos patrones son especialmente útiles.

**Patrón 1: solo escritor.** Introduce una gran cantidad de datos en el driver mientras un lector sigue el ritmo.

```sh
$ dd if=/dev/myfirst of=/dev/null bs=4k &
$ dd if=/dev/zero of=/dev/myfirst bs=4k count=100000
```

Esto genera 400 MB de tráfico a través del driver. Observa cómo crece `sysctl dev.myfirst.0.stats.bytes_written` y compáralo con `bytes_read`; la diferencia es aproximadamente el nivel de llenado del buffer.

**Patrón 2: con límite de tasa.** Algunas pruebas quieren estresar el driver a una tasa constante en lugar de al máximo rendimiento. Usa `rate` o la utilidad GNU `pv(1)` (disponible en `ports/sysutils/pv`) para limitarlo:

```sh
$ pv -L 10m < /dev/zero | dd of=/dev/myfirst bs=4k
```

Esto limita la tasa de escritura a 10 MB/s. Una tasa más baja te permite observar el nivel de llenado del buffer en `sysctl` y ver cómo el camino bloqueante entra en acción cuando la tasa se aproxima a la del consumidor.

**Patrón 3: bloque completo.** Como se mencionó en la Sección 4, el `dd` por defecto no itera en lecturas cortas. Usa `iflag=fullblock` para que lo haga:

```sh
$ dd if=/dev/myfirst of=/tmp/out bs=4k count=100 iflag=fullblock
```

Sin `iflag=fullblock`, el archivo de salida podría ser más corto que los 400 KB solicitados a causa de lecturas cortas.

### Uso de `hexdump(1)` para verificar el contenido

`hexdump(1)` es la herramienta adecuada para verificar el *contenido* de lo que entrega el driver. Si escribes una secuencia de bytes conocida y quieres confirmar que regresa intacta, `hexdump` te lo muestra.

```sh
$ printf 'ABCDEFGH' > /dev/myfirst
$ hexdump -C /dev/myfirst
00000000  41 42 43 44 45 46 47 48                           |ABCDEFGH|
$
```

La salida de `hexdump -C` es el formato canónico de «aquí están los bytes y su interpretación ASCII». Es especialmente útil cuando el driver emite datos binarios que las herramientas basadas en texto no pueden mostrar.

### Uso de `truss(1)` para ver el tráfico de syscalls

`truss(1)` traza las llamadas al sistema realizadas por un proceso. Ejecutar una prueba bajo `truss` te muestra exactamente lo que devolvió cada `read(2)` y `write(2)`, incluidas las transferencias parciales y los códigos de error.

```sh
$ truss -t read,write -o /tmp/trace ./rw_myfirst_nb
$ head /tmp/trace
read(3,0x7fffffffeca0,128)                       ERR#35 'Resource temporarily unavailable'
write(3,"hello world\n",12)                      = 12 (0xc)
read(3,0x7fffffffeca0,128)                       = 12 (0xc)
...
```

ERR#35 es `EAGAIN`. Verlo confirma que el camino no bloqueante está actuando. Ejecutar `producer_consumer` bajo `truss` muestra el patrón de escrituras cortas y lecturas cortas con mucha claridad; es un buen diagnóstico para depurar problemas de dimensionado del buffer.

Una herramienta relacionada es `ktrace(1)` / `kdump(1)`, que produce un trazado más detallado y decodificado, a costa de ser algo más verbosa. Cualquiera de las dos es adecuada para este nivel de trabajo.

### Uso de `sysctl(8)` para observar el estado en tiempo real

El árbol sysctl `dev.myfirst.0.stats.*` es el estado en tiempo real del driver. Observarlo durante una prueba te dice mucho sobre lo que está haciendo el driver.

```sh
$ while true; do
    clear
    sysctl dev.myfirst.0.stats | egrep 'cb_|bytes_'
    sleep 1
  done
```

Ejecútalo en una terminal mientras una prueba corre en otra. Verás cómo `cb_used` sube cuando el escritor se adelanta, baja cuando el lector lo alcanza, y oscila alrededor de algún nivel de estado estacionario. Los contadores de bytes solo crecen. Una prueba bloqueada se manifiesta como contadores congelados.

### Uso de `vmstat -m` para observar la memoria

Si sospechas de una fuga (quizás olvidaste `cbuf_destroy` en el camino de error de `attach`), `vmstat -m` te lo muestra:

```sh
$ vmstat -m | grep cbuf
         cbuf     1      4K        1  4096
```

Tras `kldunload`:

```sh
$ vmstat -m | grep cbuf
$
```

La etiqueta debería desaparecer por completo cuando el driver se descarga. Si algún contador no es cero, algo sigue manteniendo la asignación. Este es el tipo de regresión que quieres detectar de inmediato; empeora en silencio con el tiempo.

### Construcción del kit de pruebas

Aquí hay un Makefile que construye todos los programas de prueba en espacio de usuario de una vez. Colócalo en `examples/part-02/ch10-handling-io-efficiently/userland/`:

```make
# Makefile for Chapter 10 userland testers.

PROGS= rw_myfirst_v2 rw_myfirst_nb producer_consumer stress_rw cb_test

.PHONY: all
all: ${PROGS}

CFLAGS?= -O2 -Wall -Wextra -Wno-unused-parameter

rw_myfirst_v2: rw_myfirst_v2.c
	${CC} ${CFLAGS} -o $@ $<

rw_myfirst_nb: rw_myfirst_nb.c
	${CC} ${CFLAGS} -o $@ $<

producer_consumer: producer_consumer.c
	${CC} ${CFLAGS} -o $@ $<

stress_rw: stress_rw.c
	${CC} ${CFLAGS} -o $@ $<

cb_test: ../cbuf-userland/cbuf.c ../cbuf-userland/cb_test.c
	${CC} ${CFLAGS} -I../cbuf-userland -o $@ $^

.PHONY: clean
clean:
	rm -f ${PROGS}
```

Ejecutar `make` construye las cuatro herramientas. `make cb_test` construye únicamente la prueba `cbuf` independiente. Mantén los dos directorios en espacio de usuario (`cbuf-userland/` para el buffer, `userland/` para los probadores del driver) separados; el primero es el prerrequisito para las etapas posteriores, y construirlo de forma aislada refleja el orden en que los introdujimos en el capítulo.

### Un plan de pruebas consolidado

Con las herramientas ya en su sitio, aquí tienes un plan de pruebas que puedes ejecutar contra cada etapa del driver. Ejecuta cada pasada después de cargar el `myfirst.ko` correspondiente.

**Etapa 2 (buffer circular, sin bloqueo):**

1. `./rw_myfirst_v2 fill 4096; sysctl dev.myfirst.0.stats.cb_used` debería informar de 4096.
2. `./rw_myfirst_v2 fill 4097` debería mostrar una escritura corta (escribió 4096 de 4097).
3. `./rw_myfirst_v2 drain 2048; sysctl dev.myfirst.0.stats.cb_used` debería informar de 2048.
4. `./rw_myfirst_v2 fill 2048; sysctl dev.myfirst.0.stats.cb_used` debería informar de 4096, pero `cb_head` debería ser distinto de cero (lo que demuestra que el wrap-around funcionó).
5. `dd if=/dev/myfirst of=/dev/null bs=4k`: debería vaciar 4096 bytes y luego devolver cero.
6. `producer_consumer` con `TOTAL_BYTES = 8192`: debería completarse sin errores.

**Etapa 3 (soporte bloqueante y no bloqueante):**

1. `cat /dev/myfirst &` debería bloquearse.
2. `echo hi > /dev/myfirst` debería producir salida en el terminal del `cat`.
3. `kill -INT %1` debería desbloquear `cat` de forma limpia.
4. `./rw_myfirst_nb` debería imprimir la salida de seis líneas mostrada anteriormente.
5. `producer_consumer` con `TOTAL_BYTES = 1048576`: debería completarse sin discrepancias y con sumas de verificación coincidentes.

**Etapa 4 (soporte de poll, helpers refactorizados):**

Todas las pruebas de la Etapa 3, más:

1. El paso 4 de `./rw_myfirst_nb` debería mostrar `revents=0x41` (POLLIN|POLLRDNORM).
2. Un programa pequeño que abre un descriptor de solo lectura y sin bloqueo, lo registra con `poll(POLLIN)` con un timeout de -1 y llama a `write` desde el mismo proceso sobre un segundo descriptor: el `poll` debería retornar de inmediato con `POLLIN` activado.
3. `dd if=/dev/zero of=/dev/myfirst bs=1m count=10 &` combinado con `dd if=/dev/myfirst of=/dev/null bs=4k`: debería transferir 10 MB sin errores, aproximadamente en el tiempo que tarde el lado más lento.

Este plan no es en absoluto exhaustivo. La sección de laboratorios más adelante en el capítulo te ofrece una secuencia más profunda. Pero estas son las pruebas de humo: ejecútalas después de cada cambio no trivial, y si pasan, no habrás roto nada fundamental.

### Depuración cuando una prueba falla

Cuando una prueba falla, la secuencia de inspección suele ser la siguiente:

1. **`dmesg | tail -100`**: comprueba si hay advertencias del kernel, panics o tu propia salida de `device_printf`. Si el kernel informa de una violación de locking o una advertencia de `witness`, el problema es visible aquí antes de hacer cualquier otra cosa.
2. **`sysctl dev.myfirst.0.stats`**: compara los valores actuales con los que deberían ser. Si `cb_used` no es cero pero nadie tiene un descriptor abierto, algo falló en la ruta de cierre.
3. **`truss -t read,write,poll -f`**: ejecuta el test fallido bajo `truss` y observa los retornos de las syscalls. Un `EAGAIN` espurio (o su ausencia) aparece de inmediato.
4. **`ktrace`**: si `truss` no es suficiente, `ktrace -di ./test; kdump -f ktrace.out` ofrece una vista más profunda que incluye señales.
5. **Añadir `device_printf` al driver**: distribuye trazas de una línea al principio y al final de cada handler y reproduce la prueba. Este es el recurso final, y a veces es la única manera de ver qué hace el driver en los momentos que las herramientas del espacio de usuario no capturan.

Ten cuidado con el último paso. Cada `device_printf` pasa por el buffer de registro del kernel, que es a su vez un buffer circular de tamaño finito. Insertar un `device_printf` dentro de la función `cbuf_write` que se ejecuta por cada byte saturará el registro. Empieza con una línea de registro por llamada de I/O y auméntalo solo si es necesario.

### Cerrando la sección 6

Ahora dispones de un conjunto de pruebas que puede ejercitar cada comportamiento no trivial que el driver promete. `rw_myfirst_v2` cubre lecturas y escrituras de tamaño determinado y la corrección del ciclo completo. `rw_myfirst_nb` cubre la ruta no bloqueante y el contrato de `poll(2)`. `producer_consumer` cubre la carga concurrente entre dos partes con verificación del contenido. `dd`, `cat`, `hexdump`, `truss`, `sysctl` y `vmstat -m` juntos ofrecen visibilidad del estado interno del driver.

Ninguna de estas herramientas es nueva ni exótica. Son utilidades estándar del sistema base de FreeBSD y fragmentos de código breves que puedes escribir en una tarde. La combinación es suficiente para detectar la mayoría de los errores del driver antes de que lleguen a otras manos. La siguiente sección toma el driver que acabas de terminar de probar y prepara su forma de código para el trabajo de concurrencia del Capítulo 11.

## Sección 7: Refactorización y preparación para la concurrencia

El driver funciona. Almacena en buffer, bloquea, informa correctamente a poll y las pruebas del espacio de usuario de la Sección 6 confirman que los bytes fluyen correctamente bajo una carga realista. Lo que aún no hemos hecho es dar forma al código para el trabajo que realizará el Capítulo 11. Esta sección es el puente: identifica los lugares del código fuente actual que necesitarán atención desde un punto de vista real de concurrencia, refactoriza el acceso al buffer en un conjunto compacto de helpers y termina dejando al driver tan transparente como sea posible sobre su propio estado.

No introducimos aquí nuevas primitivas de locking. El Capítulo 11 explorará ese material en profundidad, incluyendo las alternativas a un único mutex (sleepable locks, sx locks, rwlocks, patrones lock-free), las herramientas de verificación (`WITNESS`, `INVARIANTS`) y las reglas en torno al contexto de interrupción, el sleep y el ordenamiento de locks. Lo que hacemos en la Sección 7 es dar al código la *forma* necesaria para que esas herramientas puedan aplicarse limpiamente cuando llegue el momento.

### Identificación de posibles condiciones de carrera

Una «condición de carrera» en el código de un driver es cualquier punto donde la corrección del código depende del orden en que se ejecuten dos threads, y ese orden no está garantizado por nada en el driver. El driver de la Etapa 4 tiene la *maquinaria* correcta en su lugar (un mutex, un canal de sleep, semántica de sleep-con-mutex a través de `mtx_sleep`) y los handlers de I/O la respetan. Pero todavía hay lugares donde merece la pena hacer una auditoría cuidadosa.

Recorramos las estructuras de datos y preguntemos, por cada campo compartido: «¿quién lo lee, quién lo escribe, qué protege el acceso?»

**`sc->cb` (el buffer circular).** Leído por `myfirst_read`, escrito por `myfirst_write`, leído por `myfirst_poll`, leído por los dos handlers de sysctl (`cb_used` y `cb_free`), leído por `myfirst_detach` (implícitamente a través de `cbuf_destroy`). Protegido por `sc->mtx` en todos los puntos donde se accede. *Parece seguro.*

**`sc->bytes_read`, `sc->bytes_written`.** Actualizados por los dos handlers de I/O bajo `sc->mtx`. Leídos por sysctl directamente a través de `SYSCTL_ADD_U64` (sin handler intermedio). La lectura de sysctl es una única carga de 64 bits en la mayoría de las arquitecturas, lo que supone un riesgo de lectura fragmentada en algunas plataformas de 32 bits, pero es atómica en amd64 y arm64. *Mayoritariamente seguro; véase la nota sobre lectura fragmentada más adelante.*

**`sc->open_count`, `sc->active_fhs`.** Actualizados bajo `sc->mtx`. Leídos por sysctl directamente. La misma consideración sobre lectura fragmentada.

**`sc->is_attached`.** Leído por cada handler al entrar, establecido por attach (sin lock, antes de `make_dev`), borrado por detach (bajo lock). La escritura sin lock en el momento del attach es segura porque nadie más puede ver el dispositivo todavía. El borrado bajo lock en el momento del detach está correctamente ordenado respecto al wakeup. *Parece seguro.*

**`sc->cdev`, `sc->cdev_alias`.** Establecidos por attach, borrados por detach. Una vez que el attach termina, son estables durante la vida del dispositivo. Los handlers acceden al softc a través de `dev->si_drv1` (establecido durante el attach) y nunca los desreferencian directamente durante I/O. *Seguro por construcción.*

**`sc->rsel`, `sc->wsel`.** La maquinaria de `selinfo` está bloqueada internamente (usa el `selspinlock` del kernel y `knlist` por mutex si se inicializa uno). Para uso puro de `select(2)`/`poll(2)`, las llamadas `selrecord` y `selwakeup` gestionan su propia concurrencia. *Seguro.*

**`sc->open_count` y sus compañeros, de nuevo.** La nota sobre lectura fragmentada anterior merece explicitarse. En plataformas de 32 bits (i386, armv7), un campo de 64 bits puede dividirse en dos operaciones de memoria, y una escritura concurrente puede producir una lectura que contenga la mitad alta de un valor y la mitad baja de otro (una «lectura fragmentada»). Este capítulo apunta a amd64, donde esto no es un problema, pero es el tipo de cosa en que un driver real debería pensar. La solución, si fuera necesaria, consiste en añadir un handler de sysctl (como el de `cb_used`) que tome el mutex alrededor de la carga.

La auditoría anterior da un resultado satisfactorio. Las mayores oportunidades de refactorización no son condiciones de carrera sino la *forma del código*: lugares donde la lógica del buffer se mezcla con la lógica de I/O, donde funciones helper aclararían la intención y donde el Capítulo 11 puede introducir nuevas clases de lock sin tocar los handlers de I/O.

### La refactorización: extrayendo el acceso al buffer en helpers

Los handlers de la Etapa 3 / Etapa 4 contienen una cantidad considerable de locking y contabilidad inline. Vamos a extraer eso en un pequeño conjunto de helpers. El objetivo es doble: los handlers de I/O pasan a ser obviamente correctos y el Capítulo 11 puede sustituir diferentes estrategias de locking en los helpers sin tocar `myfirst_read` o `myfirst_write`.

Define los siguientes helpers, todos en `myfirst.c` (o en un nuevo archivo `myfirst_buf.c` si prefieres una separación más clara):

```c
/* Read up to "n" bytes from the cbuf into "dst".  Returns count moved. */
static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
        size_t got;

        mtx_assert(&sc->mtx, MA_OWNED);
        got = cbuf_read(&sc->cb, dst, n);
        sc->bytes_read += got;
        return (got);
}

/* Write up to "n" bytes from "src" into the cbuf.  Returns count moved. */
static size_t
myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n)
{
        size_t put;

        mtx_assert(&sc->mtx, MA_OWNED);
        put = cbuf_write(&sc->cb, src, n);
        sc->bytes_written += put;
        return (put);
}

/* Wait, with PCATCH, until the cbuf is non-empty or the device tears down. */
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        mtx_assert(&sc->mtx, MA_OWNED);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);            /* signal caller to break */
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}

/* Wait, with PCATCH, until the cbuf has free space or the device tears down. */
static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        mtx_assert(&sc->mtx, MA_OWNED);
        while (cbuf_free(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);            /* signal caller to break */
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfwr", 0);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Las llamadas a `mtx_assert(&sc->mtx, MA_OWNED)` son una pequeña pero valiosa red de seguridad. Si un futuro llamador olvida adquirir el lock antes de llamar a uno de estos helpers, la aserción se activa (en un kernel con `WITNESS`). Una vez que confías en los helpers, puedes dejar de pensar en el lock en los puntos de llamada.

Los cuatro helpers juntos cubren todo lo que los handlers de I/O necesitan de la abstracción del buffer: leer bytes, escribir bytes, esperar datos, esperar espacio. Cada helper recibe el mutex por referencia y verifica que está retenido. Ninguno de ellos hace lock ni unlock.

Con los helpers definidos, los handlers de I/O se reducen considerablemente:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                error = myfirst_wait_data(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = myfirst_buf_read(sc, bounce, take);
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);
                selwakeup(&sc->wsel);

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

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

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                error = myfirst_wait_room(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                room = cbuf_free(&sc->cb);
                mtx_unlock(&sc->mtx);

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = myfirst_buf_write(sc, bounce, want);
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);
                selwakeup(&sc->rsel);
        }
        return (0);
}
```

Cada handler de I/O hace ahora las mismas tres cosas en el mismo orden: tomar el lock, pedir estado al helper, soltar el lock, hacer la copia, tomar el lock de nuevo, actualizar el estado, soltar, despertar. El patrón es lo suficientemente claro como para que un futuro lector pueda verificar la disciplina de locking de un vistazo.

El código de error `-1` devuelto por los helpers de espera es una pequeña convención: «no hay error que informar, pero el bucle debe terminar y el llamador debe devolver 0». Usar `-1` (que no es un errno válido) hace que la convención sea obvia sin añadir un tercer parámetro de salida. Es local al driver y nunca llega al espacio de usuario.

### Documentando la estrategia de locking

Un driver de este tamaño se beneficia de un comentario de un párrafo cerca de la parte superior del archivo que explique la disciplina de locking. El comentario es para la próxima persona que lea el código, y también para ti mismo dentro de tres meses. Añade esto cerca de la declaración de `struct myfirst_softc`:

```c
/*
 * Locking strategy.
 *
 * sc->mtx protects:
 *   - sc->cb (the circular buffer's internal state)
 *   - sc->bytes_read, sc->bytes_written
 *   - sc->open_count, sc->active_fhs
 *   - sc->is_attached
 *
 * Locking discipline:
 *   - The mutex is acquired with mtx_lock and released with mtx_unlock.
 *   - mtx_sleep(&sc->cb, &sc->mtx, PCATCH, ...) is used to block while
 *     waiting on buffer state.  wakeup(&sc->cb) is the matching call.
 *   - The mutex is NEVER held across uiomove(9), copyin(9), or copyout(9),
 *     all of which may sleep.
 *   - The mutex is held when calling cbuf_*() helpers; the cbuf module is
 *     intentionally lock-free by itself and relies on the caller for safety.
 *   - selwakeup(9) and wakeup(9) are called with the mutex DROPPED, after
 *     the state change that warrants the wake.
 */
```

Ese comentario es suficiente para que el Capítulo 11 pueda seguir la misma convención o cambiarla deliberadamente. Un driver que explica sus propias reglas facilita el mantenimiento futuro; un driver que no las explica obliga a cada futuro lector a inferirlas del código fuente, lo que resulta lento y propenso a errores.

### Separando `cbuf` de `myfirst.c`

En la Etapa 2 y la Etapa 3, el código fuente de `cbuf` convivía con `myfirst.c` en el mismo directorio del módulo pero en su propio archivo `.c`. El Makefile se actualiza para compilar ambos:

```make
KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
```

Vale la pena señalar dos detalles menores.

El primero es que `cbuf.c` declara su propio `MALLOC_DEFINE`. Cada `MALLOC_DEFINE` para la misma etiqueta en el mismo módulo sería una definición duplicada; por eso ponemos la declaración en exactamente un archivo fuente (`cbuf.c`) y una declaración `extern` en `cbuf.h` si fuera necesario. En nuestra configuración, la etiqueta es local a `cbuf.c` y no se necesita uso externo.

El segundo es que `cbuf.c` no necesita ninguno de los headers de `myfirst`. Es una biblioteca autocontenida que el driver utiliza en este caso. Si alguna vez quisieras compartir `cbuf` con un segundo driver, podrías extraerlo a su propio KLD o a `/usr/src/sys/sys/cbuf.h` y `/usr/src/sys/kern/subr_cbuf.c` (una ubicación hipotética). La disciplina de mantener `cbuf` autocontenido hace que eso sea posible.

### Convenciones de nomenclatura

Un patrón pequeño pero útil: nombra los campos y funciones relacionados con el buffer de forma coherente. Hemos usado `sc->cb` para el buffer, `cbuf_*` para las funciones del buffer y `myfirst_buf_*` para los wrappers del driver. El patrón permite a un lector recorrer el código y saber al instante si una función está tocando el buffer en bruto (`cbuf_*`) o pasando por los wrappers bloqueados del driver (`myfirst_buf_*`).

Evita mezclar estilos. Llamar al buffer `sc->ring` en algunos lugares y `sc->cb` en otros, o usar `cbuf_get` en unos y `cbuf_read` en otros, hace que el código sea más difícil de ojear. Elige un conjunto de nombres y úsalos en todo el código.

### Protección frente a sorpresas en el tamaño del buffer

La macro `MYFIRST_BUFSIZE` determina la capacidad del anillo. Actualmente está codificada de forma fija a 4096. No hay nada malo en eso, pero un parámetro `sysctl` (de solo lectura) que exponga el valor, junto con una anulación al estilo de `module_param` en el momento de carga del módulo, haría que el driver fuera más útil en las pruebas sin necesidad de recompilar.

Aquí está el patrón para una anulación en tiempo de carga usando `TUNABLE_INT`:

```c
static int myfirst_bufsize = MYFIRST_BUFSIZE;
TUNABLE_INT("hw.myfirst.bufsize", &myfirst_bufsize);
SYSCTL_INT(_hw_myfirst, OID_AUTO, bufsize, CTLFLAG_RDTUN,
    &myfirst_bufsize, 0, "Default buffer size for new myfirst attaches");
```

`TUNABLE_INT` lee el valor del entorno del kernel en el momento del boot o de `kldload`. Un usuario puede establecerlo desde el prompt del cargador (`set hw.myfirst.bufsize=8192`) o ejecutando `kenv hw.myfirst.bufsize=8192` antes de `kldload`. El flag `CTLFLAG_RDTUN` indica «solo lectura en tiempo de ejecución, pero ajustable en tiempo de carga». Después de la carga, `sysctl hw.myfirst.bufsize` muestra el valor elegido.

Luego, en `myfirst_attach`, usa `myfirst_bufsize` en lugar de `MYFIRST_BUFSIZE` en la llamada a `cbuf_init`. El cambio es pequeño pero útil: ahora puedes experimentar con diferentes tamaños de buffer sin reconstruir el módulo.

### Objetivos del siguiente hito

Hacia dónde lleva el Capítulo 11 al driver:

- El único mutex que tienes hoy lo protege todo. El capítulo 11 analizará si un único lock es el diseño correcto bajo contención elevada, si los locks que permiten dormir (`sx_*`) serían más adecuados, y cómo razonar sobre el orden de adquisición de los locks cuando intervienen varios subsistemas.
- La ruta de bloqueo usa `mtx_sleep`, que es la primitiva correcta para este tipo de trabajo. El capítulo 11 presentará `cv_wait(9)` (variables de condición) como una alternativa más estructurada para algunos patrones, y analizará cuándo es preferible cada una.
- La estrategia de despertar usa `wakeup(9)` (despierta a todos). El capítulo 11 analizará `wakeup_one(9)` y el problema del thundering herd, y cuándo es apropiado cada uno.
- El cbuf no es thread-safe por sí mismo, y eso es deliberado. El capítulo 11 volverá sobre esta decisión y analizará los compromisos de incorporar el locking *dentro* de la estructura de datos frente a dejarlo en manos del llamador.
- La regla "esperar a que los descriptores se cierren" del camino de detach es conservadora. El capítulo 11 analizará estrategias alternativas (revocación forzada, conteo de referencias a nivel de cdev, el mecanismo `destroy_dev_drain(9)`) para drivers que necesitan hacer detach pese a tener descriptores abiertos.

No necesitas conocer aún ninguno de estos temas. La clave es que la *forma* del código actual es lo que hace que esos temas resulten abordables en el capítulo 11. Puedes sustituir el mutex por un lock `sx` sin modificar las firmas de las funciones auxiliares. Puedes sustituir `wakeup` por `wakeup_one` con cambios de una sola línea. Puedes introducir un canal de sleep por lector sin reestructurar los manejadores de I/O. La refactorización da sus frutos en cuanto empiezas a plantearte las preguntas del capítulo siguiente.

### Un orden de lectura para el siguiente capítulo

Cuando empieces el capítulo 11, tres archivos en `/usr/src/sys` merecen una lectura atenta.

`/usr/src/sys/kern/subr_sleepqueue.c` es donde se implementan `mtx_sleep`, `tsleep` y `wakeup`. Léelo una vez para tener contexto. La implementación es más elaborada de lo que sugieren las páginas de manual, pero su núcleo (colas de suspensión indexadas por canal, desencolado atómico al despertar) es sencillo.

`/usr/src/sys/sys/sx.h` y `/usr/src/sys/kern/kern_sx.c` juntos explican el lock compartido-exclusivo con capacidad de suspensión. Mencionamos `sx` más arriba como alternativa a `mtx`; leer la implementación real es la mejor manera de entender las diferencias entre ambos.

`/usr/src/sys/sys/condvar.h` y `/usr/src/sys/kern/kern_condvar.c` documentan la familia de primitivas de variable de condición `cv_wait`. Al igual que `mtx_sleep`, se apoyan en la maquinaria de colas de suspensión del kernel en `subr_sleepqueue.c`, pero exponen una API estructurada diferente en la que cada punto de espera tiene su propia `struct cv` con nombre, en lugar de una dirección arbitraria como canal. El capítulo 11 explicará cuándo preferir cada una y por qué una `struct cv` dedicada suele ser la opción más limpia para una condición de espera bien definida.

No son lecturas obligatorias; son el siguiente paso en un camino largo en el que claramente ya te encuentras.

### Cerrando la sección 7

El driver tiene ahora la forma que quiere el capítulo 11. La abstracción del buffer está en su propio archivo, probada en userland y llamada desde el driver mediante un pequeño conjunto de wrappers con lock. La estrategia de locking está documentada en un comentario que indica exactamente qué protege el mutex y cuáles son las reglas. El camino de bloqueo es correcto, el camino sin bloqueo es correcto, el camino de poll es correcto, y el camino de detach espera correctamente y despierta a cualquier proceso dormido.

La mayor parte de lo que harás en el capítulo 11 se añadirá a esta base, no la reescribirá. Los patrones que hemos construido (lock alrededor de los cambios de estado, sleep con el mutex como interlock, wake en cada transición) son los mismos patrones que usa el resto del kernel. El vocabulario es el mismo, las primitivas son las mismas, la disciplina es la misma. Estás cerca de poder leer la mayoría de los drivers de dispositivos de caracteres del árbol sin ayuda.

Antes de continuar con los temas complementarios del capítulo y los laboratorios, tómate un momento para mirar tu propio código fuente. El driver de la etapa 4 debería tener aproximadamente 500 líneas de código (`myfirst.c`), más unas 110 líneas de `cbuf.c` y 20 líneas de `cbuf.h`. El total es pequeño, la separación en capas es limpia y casi cada línea hace algo concreto. Esa densidad es lo que parece el código de driver bien estructurado.

## Sección 8: Tres temas complementarios

Esta sección cubre tres temas que aparecen con frecuencia junto con la E/S con buffer en el mundo real. Cada uno es suficientemente amplio como para llenar un capítulo entero por sí solo; no vamos a hacer eso. En cambio, vamos a presentar cada uno al nivel que un lector de este libro necesita para reconocer el patrón, hablar de él con sensatez y saber dónde buscar cuando llegue el momento de usarlo. Los tratamientos más profundos llegan más adelante, en los capítulos donde cada tema es el asunto principal.

Los tres temas son: `d_mmap(9)` para que el espacio de usuario mapee un buffer del kernel; las consideraciones sobre zero-copy y lo que realmente significan; y los patrones de lectura anticipada y coalescencia de escrituras utilizados por drivers de alto rendimiento.

### Tema 1: `d_mmap(9)` y el mapeo de un buffer del kernel

`d_mmap(9)` es el callback de dispositivo de caracteres que el kernel invoca cuando un programa en espacio de usuario llama a `mmap(2)` sobre `/dev/myfirst`. La tarea del handler es traducir un *desplazamiento de archivo* a una *dirección física* que el sistema VM pueda mapear en el proceso del usuario. La firma es:

```c
typedef int d_mmap_t(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
                     int nprot, vm_memattr_t *memattr);
```

Por cada fragmento del tamaño de una página que el usuario quiere mapear, el kernel llama a `d_mmap` con `offset` establecido en el desplazamiento en bytes dentro del dispositivo. El handler calcula la dirección física de la página correspondiente y la almacena a través de `*paddr`. También puede ajustar los atributos de memoria mediante `*memattr` (caché, write-combining, etc.). Devolver un código de error distinto de cero le indica al kernel "este desplazamiento no se puede mapear"; devolver `0` indica éxito.

La razón por la que presentamos `d_mmap` aquí es que es el pariente ligero de la E/S con buffer. Con `read(2)` y `write(2)`, cada byte se copia a través de la frontera de confianza en cada llamada. Con `mmap(2)` seguido de acceso directo a memoria, los bytes son visibles para el espacio de usuario sin ninguna copia explícita. Un programa en espacio de usuario lee o escribe en la región mapeada exactamente como si fuera memoria ordinaria, y el buffer del kernel contiene los mismos bytes que ve el usuario.

Este patrón resulta atractivo para una clase pequeña pero importante de dispositivos. Un frame buffer, un buffer de dispositivo mapeado por DMA, una cola de eventos en memoria compartida: cada uno de estos se beneficia de ser mapeado directamente para que el código de usuario pueda manipular los bytes sin entrar nunca en el kernel. El ejemplo clásico en `/usr/src/sys/dev/mem/memdev.c` (con la función `memmmap` específica de la arquitectura bajo cada directorio `arch`) mapea `/dev/mem` para que los procesos de usuario con privilegios puedan leer o escribir páginas de memoria física.

Para un driver de aprendizaje como el nuestro, el objetivo es más modesto: permitir que `mmap(2)` vea el mismo buffer circular que utilizan `read(2)` y `write(2)`. El usuario puede entonces leer el buffer sin pasar por el camino de las syscalls. No extenderemos el driver para que admita escrituras a través de `mmap` (eso requeriría un manejo cuidadoso de la coherencia de caché y las actualizaciones concurrentes con el camino de las syscalls), pero un mapeo de solo lectura es una capacidad útil que añadir.

#### Una implementación mínima de `d_mmap`

La implementación es breve:

```c
static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct myfirst_softc *sc = dev->si_drv1;

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);
        if ((nprot & VM_PROT_WRITE) != 0)
                return (EACCES);
        if (offset >= sc->cb.cb_size)
                return (-1);
        *paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
        return (0);
}
```

Añade `.d_mmap = myfirst_mmap,` al `cdevsw`. El handler hace cuatro cosas en secuencia.

En primer lugar, comprueba que el dispositivo sigue conectado. Un usuario que mantenga un `mmap` sobre un driver desmontado debería ver `ENXIO`, no un panic del kernel.

En segundo lugar, rechaza los mapeos de escritura. Permitir `PROT_WRITE` dejaría que el espacio de usuario modificara el buffer de forma concurrente con los handlers de lectura y escritura, lo que entraría en condición de carrera con los invariantes del cbuf. Un mapeo de solo lectura es suficiente para nuestros propósitos de aprendizaje; un driver real que quiera mapeos con escritura tiene que hacer un trabajo considerablemente mayor para mantener el cbuf consistente.

En tercer lugar, acota el desplazamiento. El usuario podría solicitar `offset = 1 << 30`, muy por encima del final del buffer; el handler devuelve `-1` para rechazarlo. (Devolver `-1` le indica al kernel "no hay dirección válida para este desplazamiento"; el kernel lo trata como el final de la región mapeable.)

En cuarto lugar, calcula la dirección física con `vtophys(9)`. `vtophys` traduce una dirección virtual del kernel a la dirección física correspondiente para una sola página. El buffer se asignó con `malloc(9)`, que devuelve memoria *virtualmente* contigua; para una asignación que cabe en una página (nuestro `MYFIRST_BUFSIZE` de 4096 bytes en una máquina con páginas de 4 KB) esto también es trivialmente contiguo en términos físicos, y un solo `vtophys` es suficiente. Para buffers más grandes, cada página debe buscarse individualmente, porque `malloc(9)` no garantiza contigüidad física entre páginas. La expresión `(offset & ~PAGE_MASK)` redondea el desplazamiento del llamador hacia abajo hasta el límite de página para que `vtophys` se llame sobre la base de página correcta; el kernel se encarga entonces de aplicar el desplazamiento dentro de la página desde la llamada `mmap` del usuario. Un driver de producción cuyo buffer pueda abarcar más de una página debería recorrer la asignación página a página, o cambiar a `contigmalloc(9)` cuando se requiera contigüidad física real.

#### Advertencias y limitaciones

Algunas advertencias importantes se aplican a esta implementación mínima.

`vtophys` funciona para memoria asignada por `malloc(9)` solo cuando cada página de la asignación es contigua en memoria física. Las asignaciones pequeñas (por debajo de una página) son siempre contiguas. Las asignaciones más grandes realizadas con `malloc(9)` son *virtualmente* contiguas pero no necesariamente lo son en términos físicos; el handler necesitaría calcular la dirección física página a página en lugar de asumir linealidad. Para el buffer de 4 KB del capítulo 10 (que cabe en una sola página) la forma simple funciona.

Para buffers genuinamente grandes, la primitiva adecuada es `contigmalloc(9)` (memoria física contigua) o las funciones `dev_pager_*` para proporcionar un pager personalizado. Ambas corresponden a capítulos posteriores donde tratamos los detalles del VM en profundidad.

El mapeo es de solo lectura. Una solicitud `PROT_WRITE` fallará con `EACCES`. Permitir escrituras requeriría bien una forma de invalidar los mapeos del usuario cuando cambian los índices del cbuf (poco práctico para un buffer circular), bien un diseño fundamentalmente diferente en el que las escrituras del usuario manejen el buffer directamente. Ninguna de las dos opciones es apropiada para un capítulo de aprendizaje.

Por último, mapear el cbuf *no* permite que el espacio de usuario vea un flujo coherente de bytes como hace un `read`. El mapeo muestra la memoria subyacente *en bruto*, incluyendo bytes fuera de la región activa (que pueden ser obsoletos o cero) e ignorando los índices head/used. Un usuario que lee desde el mapeo necesita consultar `sysctl dev.myfirst.0.stats.cb_used` y `cb_used` para saber dónde empieza y dónde termina la región activa. Esto es intencional: `mmap` es un mecanismo de bajo nivel que expone memoria en bruto, y cualquier interpretación estructurada ha de añadirse por encima.

#### Un pequeño programa de prueba para `mmap`

Un programa en espacio de usuario que mapea el buffer y lo recorre tiene este aspecto:

```c
/* mmap_myfirst.c: map the myfirst buffer read-only and dump it. */
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"
#define BUFSIZE 4096

int
main(void)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0) { perror("open"); return (1); }

        char *map = mmap(NULL, BUFSIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) { perror("mmap"); close(fd); return (1); }

        printf("first 64 bytes:\n");
        for (int i = 0; i < 64; i++)
                printf(" %02x", (unsigned char)map[i]);
        putchar('\n');

        munmap(map, BUFSIZE);
        close(fd);
        return (0);
}
```

Ejecútalo después de escribir algunos bytes en el dispositivo:

```sh
$ printf 'ABCDEFGHIJKL' > /dev/myfirst
$ ./mmap_myfirst
first 64 bytes:
 41 42 43 44 45 46 47 48 49 4a 4b 4c 00 00 00 00 ...
```

Los primeros doce bytes son `A`, `B`, ..., `L`, exactamente lo que se escribió. Los bytes restantes son cero porque `cbuf_init` rellena con ceros la memoria de respaldo y no hemos escrito nada más allá del desplazamiento 12. Este es el mecanismo básico.

#### Cuándo usarías realmente `d_mmap`

La mayoría de los pseudo-dispositivos no necesitan `d_mmap`. El camino de las syscalls es rápido, sencillo y bien comprendido, y el coste de un `read(2)` extra por página es insignificante para datos de baja tasa. Usa `d_mmap` cuando se aplique alguna de las siguientes situaciones:

- Los datos se producen en el buffer a tasas muy altas (gigabytes por segundo en gráficos o E/S de alto rendimiento) y la sobrecarga de las syscalls por byte empieza a dominar.
- El espacio de usuario quiere inspeccionar o procesar posiciones específicas en un buffer grande sin copiar todo el contenido.
- El driver representa hardware cuyos registros o áreas de DMA son direccionables como memoria (por ejemplo, el FIFO de comandos de una GPU).

Para nuestro pseudo-dispositivo, `d_mmap` es principalmente un ejercicio de aprendizaje. Construirlo te enseña la firma de la llamada, la relación con el sistema VM y la distinción entre `vtophys` y `contigmalloc`. El uso real en producción llega cuando escribes un driver que exige ese rendimiento.

### Tema 2: Consideraciones sobre zero-copy

"Zero-copy" es una de las palabras más sobreutilizadas en los debates sobre rendimiento de sistemas. Interpretada de forma estricta, significa "no se copian datos entre ubicaciones de memoria durante la operación". Esa definición es demasiado estricta para ser útil: incluso DMA de un dispositivo a memoria es, técnicamente, una copia. En la práctica, "zero-copy" es una abreviatura de "los bytes no pasan por las cachés de la CPU como parte de una instrucción de copia explícita en el camino de E/S".

Para un dispositivo de caracteres como `myfirst`, la pregunta es si puedes evitar la copia de `uiomove(9)` en los handlers de lectura y escritura. La respuesta, para los patrones que hemos construido, es "no, e intentarlo suele ser un error". He aquí por qué.

`uiomove(9)` realiza una copia del kernel al usuario (o del usuario al kernel) por cada transferencia. Eso es un conjunto de movimientos de bytes por llamada a `read(2)` o `write(2)`. La CPU tira del origen hacia la caché, escribe el destino desde la caché y continúa con su trabajo. En hardware moderno, esta copia es rápida: una línea de caché L1 tiene 64 bytes, la CPU puede transferir decenas de gigabytes por segundo en copias de memoria, y el coste por byte está en los nanosegundos de un solo dígito.

Para eliminar esa copia, hay que encontrar otra forma de hacer los bytes visibles en el espacio de usuario. Los dos mecanismos principales son `mmap(2)` (que acabamos de comentar) y las primitivas de memoria compartida (`shm_open(3)`, sockets con `MSG_PEEK`, sendfile). Todos tienen sus propios costes: actualizaciones de la tabla de páginas, vaciados de TLB, tráfico IPI en sistemas multi-CPU, la imposibilidad de usar la memoria fuente para cualquier otra cosa mientras esté mapeada. Para transferencias pequeñas y medianas, `uiomove` es *más rápido* que las alternativas porque los costes de configuración de estas dominan.

Hay casos reales en los que la copia cero resulta beneficiosa. Un driver de red que usa DMA para traer los paquetes entrantes a mbufs y se los pasa a la pila de protocolos evita una copia que, de otro modo, costaría tanto como el propio DMA. Un driver de almacenamiento que utiliza `bus_dmamap_load(9)` para configurar una transferencia DMA desde un buffer en espacio de usuario (tras bloquearlo con `vslock`) evita dos copias que, de otro modo, dominarían el coste de la E/S. Un driver de gráficos de alto rendimiento puede mapear los buffers de comandos de la GPU directamente en el proceso de renderizado para evitar una copia por fotograma. Todas estas son ventajas reales.

Para un pseudodispositivo cuyos datos no provienen de hardware real, sin embargo, la ganancia es ilusoria. La copia «ahorrada» no es más que reorganizar dónde se almacenan los bytes; el coste aparece en otro lugar (la actualización de la tabla de páginas, el fallo de caché cuando el usuario toca una página que ha sido escrita directamente desde el kernel, la contención cuando dos CPUs tocan la misma página compartida). El driver de la Etapa 4 realiza un `uiomove` por cada trozo del tamaño del bounce buffer, lo que equivale aproximadamente a una copia cada 256 bytes, una cifra perfectamente asumible para lo que un solo núcleo puede sostener.

Si te encuentras optimizando la copia en un pseudodispositivo, vale la pena hacerse antes dos preguntas.

La primera es si la copia es realmente el cuello de botella. Ejecuta el driver con `dtrace` o `pmcstat` y mide dónde van los ciclos. Si `uiomove` no está entre los tres primeros, optimizarlo no producirá ninguna diferencia medible. Los cuellos de botella más habituales en este tipo de código son la contención de locks (una CPU está esperando a que otra libere el mutex), el coste de las syscalls (muchas syscalls pequeñas en lugar de unas pocas grandes) y el coste de despertar a los durmientes (cada `wakeup` implica recorrer la cola de espera). Todo eso ofrece ganancias mayores que la propia copia.

La segunda pregunta es si el *usuario* del driver quiere realmente la semántica de copia cero. Un usuario que llama a `read(2)` le está pidiendo al kernel una copia de los bytes, no un puntero a los bytes del kernel. Cambiar a un mapeo modifica el contrato: el usuario tiene que conocer el mapeo, gestionarlo de forma explícita y entender las reglas de coherencia de caché. Es una compensación en la que el usuario debe optar conscientemente, no una mejora transparente.

El enfoque correcto es este: la copia cero es una técnica con costes y beneficios específicos. Úsala cuando los beneficios superen claramente los costes, y no antes. Para la mayoría de los drivers, especialmente los pseudodispositivos, el camino de la syscall con `uiomove` es la elección correcta.

### Tema 3: Readahead y write coalescing

El tercer tema trata sobre el rendimiento. Cuando un driver soporta un flujo constante de bytes a alta velocidad, dos patrones se vuelven importantes: **readahead** en el lado de la lectura y **write coalescing** en el lado de la escritura. Ambos consisten en hacer más trabajo por syscall, y ambos reducen el coste por byte del camino de I/O.

#### Readahead

El readahead consiste en obtener más datos de los que el usuario ha pedido en ese momento, bajo la suposición de que los pedirá a continuación. Una lectura de archivo normal suele activar el readahead a nivel de VFS: cuando el kernel detecta que un proceso ha leído unos cuantos bloques secuenciales, comienza a leer los siguientes bloques en segundo plano para que el próximo `read(2)` los encuentre ya en memoria. El usuario percibe una latencia menor en las lecturas sucesivas.

Para un pseudodispositivo, el readahead a nivel de VFS no es directamente aplicable (no existe un archivo subyacente). Sin embargo, el *driver* puede implementar su propia variante del readahead pidiendo a la fuente de datos que produzca con antelación. Imagina un driver que envuelve una fuente de datos lenta (un sensor de hardware, un servicio remoto). Cuando el usuario lee, el driver extrae datos de la fuente. El usuario vuelve a leer; el driver extrae más. Con readahead, el driver podría extraer un *bloque* de datos de la fuente la primera vez que el usuario lee, almacenar los bytes extra en el cbuf y servir las lecturas posteriores directamente desde el cbuf sin volver a la fuente.

Esto es exactamente lo que el driver `myfirst` ya hace en esencia. El cbuf *es* el buffer de readahead. Las escrituras depositan datos, las lecturas los consumen, y el lector no tiene que esperar a que el escritor escriba cada byte individual. La lección más amplia es que disponer de un buffer en el driver es, estructuralmente, el mismo patrón que el readahead: permite al consumidor encontrar datos ya preparados.

Cuando construyas un driver contra una fuente real, la lógica de readahead vive típicamente en un thread del kernel o en un callout que vigila `cbuf_used` y dispara una extracción de la fuente cuando el contador cae por debajo de un umbral. Ese umbral es el *low-water mark* (marca de nivel bajo); la extracción se detiene cuando el contador alcanza el *high-water mark* (marca de nivel alto). El cbuf se convierte en un buffer entre la tasa de ráfaga de la fuente y la tasa de ráfaga del consumidor, y el thread del kernel lo mantiene adecuadamente lleno.

#### Write coalescing

El write coalescing es el patrón simétrico. Un driver que envía datos a un destino lento (un registro de hardware, un servicio remoto) puede recoger varias escrituras pequeñas en una sola escritura grande, reduciendo el coste por escritura en el destino. Las llamadas `write(2)` del usuario depositan bytes en el cbuf; un thread del kernel o un callout lee del cbuf y escribe en el destino en fragmentos más grandes.

El coalescing es especialmente útil cuando el destino tiene un coste alto por operación. Imagina un driver que se comunica con un chip cuya estructura de comandos espera una cabecera, una carga útil y un pie por cada escritura: una única escritura de 1024 bytes al chip puede ser veinte veces más rápida que mil escrituras de 1 byte, porque el coste por escritura domina a tamaños pequeños. El driver aplica el coalescing recogiendo bytes en el cbuf y enviándolos al destino en bloques más grandes.

La decisión de *cuándo* vaciar el buffer es la parte difícil. Existen dos políticas habituales: **flush at threshold** (vaciar cuando `cbuf_used` supera un high-water mark) y **flush at timeout** (vaciar tras un retardo fijo desde que llegó el primer byte). La mayoría de los drivers reales usan una combinación de ambas: vaciar en cuanto se cumpla cualquiera de las dos condiciones. Un `callout(9)` (el primitivo de ejecución diferida del kernel) es la forma natural de programar el timeout. El capítulo 13 cubre `callout` en detalle; por ahora, la clave conceptual es que el coalescing es un intercambio deliberado entre la latencia por byte (peor, porque el byte permanece en el buffer) y el rendimiento por operación (mejor, porque el destino recibe menos escrituras de mayor tamaño).

#### Cómo se aplican estos patrones a `myfirst`

El driver `myfirst` no necesita ninguno de los dos patrones de forma explícita porque no tiene una fuente ni un destino real. El cbuf ya proporciona el acoplamiento entre el escritor y el lector, y el único "vaciado" es el natural que ocurre cuando el lector llama a `read(2)`. Sin embargo, conocer estos patrones es útil por dos razones.

En primer lugar, cuando leas código de drivers en `/usr/src/sys/dev/`, verás estos patrones repetidamente. Los drivers de red aplican coalescing a las escrituras TX mediante colas. Los drivers de audio hacen readahead obteniendo bloques DMA por adelantado respecto al consumidor. Los drivers de dispositivo de bloques usan la capa BIO para agrupar peticiones de I/O por adyacencia de sectores. Reconocer el patrón te permite recorrer mil líneas de código de driver sin perder el hilo.

En segundo lugar, cuando empieces a escribir drivers para hardware real en la Parte 4 y más allá, tendrás que decidir si aplicas estos patrones a tu driver y cómo. El trabajo del capítulo 10 te ha dado el *sustrato* (un buffer circular con la semántica adecuada de locking y bloqueo). Añadir readahead significa arrancar un thread del kernel para llenarlo. Añadir coalescing significa vaciarlo en función de un temporizador o un umbral. El sustrato es el mismo; las políticas son distintas.

### Cierre de la sección 8

Estos tres temas (`d_mmap`, zero-copy, readahead/coalescing) son conversaciones habituales en el desarrollo de drivers. Ninguno de ellos es un tema propio del capítulo 10, pero cada uno se apoya en la abstracción del buffer y en la maquinaria de I/O que acabas de poner en marcha.

`d_mmap` añade un camino complementario al buffer: además de `read(2)` y `write(2)`, el espacio de usuario puede acceder ahora a los bytes directamente. El zero-copy es el marco conceptual que explica por qué `d_mmap` importa en algunos casos y resulta excesivo en otros. El readahead y el write coalescing son los patrones que convierten un driver con buffer en un driver de alto rendimiento.

Las siguientes secciones del capítulo vuelven a tu driver actual: laboratorios prácticos que consolidan las cuatro etapas, ejercicios de desafío que amplían tu comprensión, y una sección de resolución de problemas para los errores que este material tiene más probabilidades de producir.

## Laboratorios prácticos

Los laboratorios que siguen te guían por las cuatro etapas del capítulo, con puntos de verificación concretos entre ellas. Cada laboratorio corresponde a un hito que puedes comprobar con el kit de pruebas de la sección 6. Están diseñados para realizarse en orden; los laboratorios posteriores dan por hechos los anteriores.

Una nota general: al comienzo de cada sesión de laboratorio, ejecuta `kldunload myfirst` (si el módulo anterior sigue cargado) y luego `kldload ./myfirst.ko`. Observa `dmesg | tail` para ver el mensaje de attach. Si el attach falla, el resto del laboratorio fallará de formas confusas; soluciona el attach primero.

### Laboratorio 1: El buffer circular independiente

**Objetivo:** Construir y verificar la implementación de `cbuf` en userland. Este laboratorio es completamente en espacio de usuario; no interviene ningún módulo del kernel.

**Pasos:**

1. Crea el directorio `examples/part-02/ch10-handling-io-efficiently/cbuf-userland/` si no existe todavía.
2. Escribe `cbuf.h` y `cbuf.c` exactamente como se muestran en la sección 2. Resiste la tentación de copiar el código del libro a ojo; escribirlo te obliga a fijarte en cada línea.
3. Escribe `cb_test.c` de la sección 2.
4. Compila con `cc -Wall -Wextra -o cb_test cbuf.c cb_test.c`.
5. Ejecuta `./cb_test`. Deberías ver tres líneas "OK" y el mensaje final "all tests OK".

**Preguntas de verificación:**

- ¿Qué devuelve `cbuf_write(&cb, src, n)` cuando el buffer ya está lleno?
- ¿Qué devuelve `cbuf_read(&cb, dst, n)` cuando el buffer ya está vacío?
- Tras `cbuf_init(&cb, 4)` y `cbuf_write(&cb, "ABCDE", 5)`, ¿cuánto vale `cbuf_used(&cb)`? ¿Cuál es el contenido de `cb.cb_data` (posiciones 0..3)?

Si no puedes responder a estas preguntas a partir de tu propio código, vuelve a leer la sección 2 y sigue la ejecución paso a paso.

**Objetivo adicional:** añade una cuarta prueba, `test_alternation`, que escriba un byte, lo lea, escriba otro byte, lo lea, y así sucesivamente durante 100 iteraciones. Esto detecta errores de uno en uno (off-by-one) en `cbuf_read` que las pruebas existentes no capturan.

### Laboratorio 2: El driver de la etapa 2 (integración del buffer circular)

**Objetivo:** Mover el `cbuf` verificado al kernel y sustituir la FIFO lineal de la etapa 3 del capítulo 9.

**Pasos:**

1. Crea `examples/part-02/ch10-handling-io-efficiently/stage2-circular/`.
2. Copia `cbuf.h` de tu directorio de userland al nuevo directorio.
3. Escribe el `cbuf.c` para el kernel de la sección 3 (esta es la versión que usa `MALLOC_DEFINE`).
4. Copia `myfirst.c` de `examples/part-02/ch09-reading-and-writing/stage3-echo/` al nuevo directorio.
5. Modifica `myfirst.c` para usar la abstracción cbuf. Los cambios son:
   - Añade `#include "cbuf.h"` cerca del principio del archivo.
   - Sustituye `char *buf; size_t buflen, bufhead, bufused;` por `struct cbuf cb;` en el softc.
   - Actualiza `myfirst_attach` para llamar a `cbuf_init(&sc->cb, MYFIRST_BUFSIZE)`. Actualiza el camino de error para llamar a `cbuf_destroy`.
   - Actualiza `myfirst_detach` para llamar a `cbuf_destroy(&sc->cb)`.
   - Sustituye `myfirst_read` y `myfirst_write` por las versiones con bucle y bounce de la sección 3.
   - Actualiza los manejadores de sysctl como en la sección 3 (usa los helpers `myfirst_sysctl_cb_used` y `myfirst_sysctl_cb_free`).
6. Actualiza el `Makefile` para compilar ambos archivos fuente: `SRCS= myfirst.c cbuf.c device_if.h bus_if.h`.
7. Compila con `make`. Corrige cualquier error de compilación.
8. Carga con `kldload ./myfirst.ko` y verifica con `dmesg | tail`.

**Verificación:**

```sh
$ printf 'helloworld' > /dev/myfirst
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 10
$ cat /dev/myfirst
helloworld
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 0
```

**Objetivo adicional 1:** escribe suficientes bytes como para que el buffer dé la vuelta (escribe 3000 bytes, lee 2000, escribe 2000 de nuevo). Comprueba que `cb_head` es distinto de cero en `sysctl` y que los datos siguen llegando correctamente.

**Objetivo adicional 2:** añade un indicador de depuración controlado por sysctl (`myfirst_debug`) y una macro `MYFIRST_DBG` (la sección 3 muestra el patrón). Úsalo para registrar cada `cbuf_read` y `cbuf_write` exitosos en los manejadores de I/O. Activa el indicador con `sysctl dev.myfirst.debug=1` y observa `dmesg`.

### Laboratorio 3: El driver de la etapa 3 (bloqueo y no bloqueo)

**Objetivo:** Añadir bloqueo cuando el buffer está vacío, bloqueo cuando está lleno, y `EAGAIN` para llamadores no bloqueantes.

**Pasos:**

1. Crea `examples/part-02/ch10-handling-io-efficiently/stage3-blocking/` y copia el código fuente de la etapa 2 en ese directorio.
2. Modifica `myfirst_read` para añadir el bucle de sleep interno (sección 5). La nueva forma incluye la instantánea `nbefore = uio->uio_resid`, la llamada a `mtx_sleep` y el `wakeup(&sc->cb)` tras una lectura exitosa.
3. Modifica `myfirst_write` para añadir el bucle de sleep simétrico y el `wakeup(&sc->cb)` correspondiente.
4. Actualiza `myfirst_detach` para asignar `sc->is_attached = 0` *antes* de llamar a `wakeup(&sc->cb)`, todo ello bajo el mutex.
5. Compila, carga y verifica.

**Verificación:**

```sh
$ cat /dev/myfirst &
[1] 12345
$ ps -AxH -o pid,wchan,command | grep cat
12345 myfrd  cat /dev/myfirst
$ echo hi > /dev/myfirst
hi
[after the cat consumes "hi", it blocks again]
$ kill -INT %1
[1]    Interrupt: 2
```

**Verificación de `EAGAIN`:**

```sh
$ ./rw_myfirst_nb       # from the userland directory
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
...
```

Si el paso 1 sigue mostrando `read returned 0`, la comprobación de `IO_NDELAY` en `myfirst_read` está ausente o es incorrecta.

**Objetivo adicional 1:** abre dos procesos `cat` contra `/dev/myfirst` simultáneamente. Escribe 100 bytes desde un tercer terminal. Ambos `cat` deberían despertar; uno obtendrá los bytes (el que gane la carrera por el lock), el otro se bloqueará de nuevo. Puedes verificar la asignación etiquetando cada `cat` con un flujo de salida diferente: `cat /dev/myfirst > /tmp/a &` y `cat /dev/myfirst > /tmp/b &`, y luego `cmp /tmp/a /tmp/b` (uno estará vacío).

**Objetivo adicional 2:** mide cuánto tarda `cat /dev/myfirst` en despertar tras una escritura, usando `time(1)`. La latencia de despertar debería ser de pocos microsegundos; si es de milisegundos, algo está introduciendo buffering entre la escritura y el despertar (o tu máquina está muy cargada).

### Laboratorio 4: El driver de la etapa 4 (soporte de poll y refactorización)

**Objetivo:** Añadir `d_poll`, refactorizar el acceso al buffer en helpers y documentar la estrategia de locking.

1. Crea `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/` y copia el código fuente de la etapa 3.
2. Añade `struct selinfo rsel; struct selinfo wsel;` al softc.
3. Implementa `myfirst_poll` tal como se describe en la sección 5.
4. Añade `selwakeup(&sc->wsel)` después del `cbuf_read` exitoso en la lectura, y `selwakeup(&sc->rsel)` después del `cbuf_write` exitoso en la escritura.
5. Añade `seldrain(&sc->rsel); seldrain(&sc->wsel);` en detach.
6. Añade `.d_poll = myfirst_poll,` al `cdevsw`.
7. Refactoriza los manejadores de I/O para usar las cuatro funciones auxiliares de la sección 7 (`myfirst_buf_read`, `myfirst_buf_write`, `myfirst_wait_data`, `myfirst_wait_room`).
8. Añade el comentario sobre la estrategia de locking de la sección 7.
9. Compila, carga y verifica.

**Verificación:**

```sh
$ ./rw_myfirst_nb
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
step 3: wrote 12 bytes
step 4: poll(POLLIN, 0) = 1 revents=0x41
step 5: read 12 bytes: hello world
```

El cambio clave respecto al laboratorio 3 es que el paso 4 ahora debería devolver `1` (no `0`), con `revents=0x41` (POLLIN | POLLRDNORM). Si sigue devolviendo 0, la llamada a `selwakeup` falta en la ruta de escritura o el manejador `myfirst_poll` es incorrecto.

**Objetivo adicional 1:** ejecuta `producer_consumer` con `TOTAL_BYTES = 8 * 1024 * 1024` (8 MB) y verifica que la prueba termine sin discrepancias. El productor genera bytes más rápido de lo que el consumidor los lee, por lo que el buffer debería llenarse y activar la ruta de bloqueo repetidamente. Observa `sysctl dev.myfirst.0.stats.cb_used` en otro terminal; debería oscilar.

**Objetivo adicional 2:** ejecuta dos instancias de `producer_consumer` en paralelo contra el mismo dispositivo. Los dos escritores competirán por espacio en el buffer; los dos lectores competirán por los bytes. Cada par debería seguir viendo sumas de verificación consistentes, pero el *entrelazado* de bytes será impredecible. Esto muestra que el driver es de flujo único por dispositivo, no por descriptor; si necesitas flujos por descriptor, ese sería un diseño de driver diferente.

### Laboratorio 5: Memory mapping

**Objetivo:** Añadir `d_mmap` para que el espacio de usuario pueda mapear el cbuf en modo solo lectura.

**Pasos:**

1. Crea `examples/part-02/ch10-handling-io-efficiently/stage5-mmap/` y copia el código fuente de la Etapa 4.
2. Añade `myfirst_mmap` del Apartado 8 al código fuente.
3. Añade `.d_mmap = myfirst_mmap,` al `cdevsw`.
4. Compila, carga y verifica.

**Verificación:**

```sh
$ printf 'ABCDEFGHIJKL' > /dev/myfirst
$ ./mmap_myfirst       # from the userland directory
first 64 bytes:
 41 42 43 44 45 46 47 48 49 4a 4b 4c 00 00 00 ...
```

Los primeros doce bytes son los bytes que escribiste.

**Objetivo adicional 1:** escribe un programa pequeño que mapee el buffer y lea bytes desde `offset = sc->cb_size - 32` (es decir, los últimos 32 bytes). Comprueba que el programa no falla. Luego escribe suficientes bytes para llevar el head del buffer hasta la región de desbordamiento circular y lee desde el mismo offset. El contenido será diferente, porque los bytes *en bruto* en memoria no son los mismos que los bytes *activos* desde la perspectiva del cbuf.

**Objetivo adicional 2:** intenta mapear el buffer con `PROT_WRITE`. Tu programa debería ver que `mmap` falla con `EACCES`, porque el driver rechaza los mapeos con escritura.

### Laboratorio 6: Pruebas de estrés y larga duración

**Objetivo:** Ejecutar el driver bajo carga sostenida durante al menos una hora sin errores.

**Pasos:**

1. Configura cuatro procesos de prueba en paralelo:
   - `dd if=/dev/zero of=/dev/myfirst bs=4k 2>/dev/null &`
   - `dd if=/dev/myfirst of=/dev/null bs=4k 2>/dev/null &`
   - `./producer_consumer`
   - Un bucle que consulta `sysctl dev.myfirst.0.stats` cada 5 segundos.
2. Deja que la prueba se ejecute durante al menos una hora.
3. Comprueba `dmesg` por advertencias del kernel, pánicos o quejas de `WITNESS`. Comprueba `vmstat -m | grep cbuf` para confirmar que no hay fugas. Verifica que `producer_consumer` informe cero discrepancias.

**Verificación:** Sin advertencias del kernel. Sin crecimiento de memoria en `vmstat`. `producer_consumer` devuelve 0.

**Objetivo adicional:** ejecuta la misma prueba con un kernel de depuración que tenga `WITNESS` activado. El kernel será más lento, pero detectará cualquier violación de la disciplina de locking. Si tu driver es correcto, no debería aparecer ninguna advertencia.

### Laboratorio 7: Fallos deliberados

**Objetivo:** Romper el driver de tres maneras concretas y observar lo que ocurre. Este laboratorio te enseña a reconocer los modos de fallo que más conviene evitar.

**Pasos para el fallo 1: mantener el lock a través de `uiomove`.**

1. Edita tu driver de la Etapa 4. En `myfirst_read`, comenta el `mtx_unlock(&sc->mtx)` que aparece antes de `uiomove(bounce, got, uio)`.
2. Añade un `mtx_unlock` equivalente después del `uiomove` para que el código siga compilando.
3. Compila y carga en un kernel con `WITNESS` activado.
4. Ejecuta un solo `cat /dev/myfirst` y escribe algunos bytes desde otro terminal.

**Lo que deberías observar:** Una advertencia de `WITNESS` en `dmesg` que se queja de "sleeping with mutex held". El sistema puede seguir funcionando, pero la advertencia es el fallo.

**Limpieza:** restaura el código original.

**Pasos para el fallo 2: olvidar el `wakeup` tras una escritura.**

1. En `myfirst_write`, comenta `wakeup(&sc->cb)`.
2. Compila y carga.
3. Ejecuta `cat /dev/myfirst &` y `echo hi > /dev/myfirst`.

**Lo que deberías observar:** El `cat` no se despierta. Permanecerá en estado `myfrd` indefinidamente (o hasta que lo interrumpas con Ctrl-C).

**Limpieza:** restaura el wakeup. Comprueba que `cat` se despierta ahora de inmediato.

**Pasos para el fallo 3: `PCATCH` ausente.**

1. En `myfirst_wait_data`, cambia `PCATCH` a `0` en la llamada a `mtx_sleep`.
2. Compila y carga.
3. Ejecuta `cat /dev/myfirst &` e intenta `kill -INT %1`.

**Lo que deberías observar:** El `cat` no responde a Ctrl-C hasta que escribas algunos bytes para despertarlo. Con `PCATCH`, la señal interrumpiría el sleep de inmediato.

**Limpieza:** restaura `PCATCH`. Comprueba que `kill -INT` funciona como se espera.

Estos tres fallos son los errores de driver más comunes en el territorio de este capítulo. Provocarlos deliberadamente, una sola vez, es la mejor manera de reconocerlos cuando aparezcan por accidente.

### Laboratorio 8: Lectura de drivers reales de FreeBSD

**Objetivo:** Leer tres drivers de dispositivo de caracteres en `/usr/src/sys/dev/` e identificar cómo implementa cada uno sus patrones de buffer, sleep y poll.

**Pasos:**

1. Lee `/usr/src/sys/dev/evdev/cdev.c`. Identifica:
   - Dónde se asigna el ring buffer por cliente.
   - Dónde bloquea el manejador de lectura (busca `mtx_sleep`).
   - Cómo se implementa `EVDEV_CLIENT_EMPTYQ`.
   - Cómo se configura `kqueue` junto a `select/poll` (aún no hemos cubierto `kqueue`; simplemente observa las llamadas a `knlist_*`).
2. Lee `/usr/src/sys/dev/random/randomdev.c`. Identifica:
   - Dónde se define `randomdev_poll`.
   - Cómo gestiona un dispositivo aleatorio que aún no ha recibido semilla.
3. Lee `/usr/src/sys/dev/null/null.c`. Identifica:
   - Cómo `zero_read` itera sobre `uio_resid`.
   - Por qué no hay buffer, ni sleep, ni manejador de poll.

**Preguntas de control:**

- ¿Por qué el manejador de lectura de `evdev` usa `mtx_sleep` mientras que el de `null` no?
- ¿Qué devolvería el manejador de poll de `randomdev` si se le llama mientras el dispositivo no tiene semilla?
- ¿Cómo detecta `evdev` que un cliente ha sido desconectado (revocado)?

El objetivo de este laboratorio no es memorizar estos drivers. Es confirmar que los patrones que has construido en `myfirst` son los mismos que usa el kernel en otros lugares. Al terminar el laboratorio deberías sentir que el resto de `dev/` es en gran medida *legible* ahora, donde quizás te parecía impenetrable dos capítulos atrás.

## Ejercicios de desafío

Los laboratorios anteriores garantizan que tienes un driver funcional y un kit de pruebas operativo. Los desafíos siguientes son ejercicios de ampliación. Cada uno extiende el material del capítulo en una dirección útil y recompensa el trabajo cuidadoso. Tómate tu tiempo; algunos son más complejos de lo que parecen.

### Desafío 1: Añadir un tamaño de buffer configurable

La macro `MYFIRST_BUFSIZE` fija el tamaño del buffer en 4 KB. Hazlo configurable.

- Añade un `TUNABLE_INT("hw.myfirst.bufsize", &myfirst_bufsize)` y un `SYSCTL_INT(_hw_myfirst, OID_AUTO, bufsize, ...)` correspondiente para que el usuario pueda establecer el tamaño del buffer al cargar el módulo.
- Usa el valor en `myfirst_attach` para dimensionar el cbuf.
- Valida el valor (rechaza el cero, rechaza tamaños mayores de 1 MB y usa un valor predeterminado razonable si la entrada no es válida).
- Verifica con `kenv hw.myfirst.bufsize=8192; kldload ./myfirst.ko; sysctl dev.myfirst.0.stats.cb_size`.

**Reto adicional:** haz que el tamaño del buffer sea *configurable en tiempo de ejecución* mediante `sysctl`. Esto es más difícil que el configurable en tiempo de carga, porque requiere reasignar el cbuf de forma segura mientras el dispositivo puede estar en uso; tendrás que vaciar o copiar los bytes existentes, tomar y liberar el lock en los momentos correctos, y decidir qué hacer con los llamadores que están durmiendo. (Pista: puede ser más sencillo exigir que todos los descriptores estén cerrados antes de permitir el redimensionamiento en tiempo de ejecución.)

### Desafío 2: Implementar la semántica de sobreescritura como modo opcional

Añade un `ioctl(2)` (o, más sencillo por ahora, un `sysctl`) que cambie el buffer entre el modo «bloquear cuando está lleno» (el predeterminado) y el modo «sobreescribir el más antiguo cuando está lleno». En el modo de sobreescritura, `myfirst_write` siempre tiene éxito: cuando `cbuf_free` es cero, el driver avanza `cb_head` para hacer espacio y luego escribe los nuevos bytes.

- Añade una función `cbuf_overwrite` junto a `cbuf_write` que implemente la semántica de sobreescritura. No modifiques `cbuf_write`; las dos deben ser funciones hermanas.
- Añade un sysctl `dev.myfirst.0.overwrite_mode` (entero de lectura-escritura, 0 o 1).
- En `myfirst_write`, despacha a `cbuf_overwrite` si el flag está activado.
- Prueba con un escritor pequeño que produzca bytes más rápido de lo que el lector los consume; en modo sobreescritura, el lector debería ver solo los bytes más recientes, mientras que en el modo normal el escritor se bloquea.

**Reto adicional:** añade un contador para el número de bytes sobreescritos (perdidos). Expónlo como sysctl para que el usuario pueda ver cuántos datos se han descartado.

### Desafío 3: Posición por lector

El driver actual tiene una única posición de lectura compartida (`cb_head`). Cuando dos lectores consumen bytes, cada llamada `read(2)` extrae algunos bytes del buffer; los dos lectores se reparten el flujo entre ellos. Algunos drivers desean lo contrario: que cada lector vea *todos* los bytes, de modo que dos lectores obtengan el flujo completo de forma independiente.

Esto es una refactorización importante:

- Mantén una posición de lectura por descriptor en `myfirst_fh`.
- Sigue la pista del «byte activo más antiguo» global a través de todos los descriptores. El `head` efectivo del cbuf pasa a ser `min(per_fh_head)`.
- `myfirst_read` avanza únicamente la posición por descriptor; `cbuf_read` se sustituye por un equivalente por descriptor.
- Un descriptor abierto a mitad de flujo ve únicamente los bytes escritos después de su apertura.
- Que el buffer esté «lleno» depende del descriptor más lento; necesitas lógica de contrapresión que tenga en cuenta a los rezagados.

Este desafío es más difícil de lo que parece; es esencialmente construir una tubería multicast. Inténtalo solo si tienes tiempo para pensar detenidamente en el locking.

### Desafío 4: Implementar `d_kqfilter`

Añade soporte de `kqueue(2)` junto al `d_poll` que ya tienes.

- Implementa una función `myfirst_kqfilter` despachada desde `cdevsw->d_kqfilter`.
- Para `EVFILT_READ`, registra un filtro que se active cuando `cbuf_used > 0`.
- Para `EVFILT_WRITE`, registra un filtro que se active cuando `cbuf_free > 0`.
- Usa `knlist_add(9)` y `knlist_remove(9)` para gestionar la lista por filtro.
- Activa `KNOTE_LOCKED(...)` desde los manejadores de I/O cuando el buffer cambie de estado.
- Prueba con un pequeño programa de usuario con `kqueue(2)` que abra el dispositivo, registre `EVFILT_READ`, llame a `kevent(2)` e informe cuando el descriptor sea legible.

Este desafío es la extensión natural de la Etapa 4. También anticipa el material sobre `kqueue` que el Capítulo 11 tratará con más profundidad junto a la concurrencia.

### Desafío 5: Contadores por CPU

Los contadores `bytes_read` y `bytes_written` se actualizan bajo el mutex. Con una carga elevada en múltiples CPU, esto puede convertirse en un punto de contención. La API `counter(9)` de FreeBSD proporciona contadores por CPU que pueden incrementarse sin lock y sumarse para su lectura.

- Reemplaza `sc->bytes_read` y `sc->bytes_written` con instancias de `counter_u64_t`.
- Asígnalos con `counter_u64_alloc(M_WAITOK)` en attach; libéralos con `counter_u64_free` en detach.
- Usa `counter_u64_add(counter, n)` para incrementar.
- Usa `counter_u64_fetch(counter)` (con un manejador sysctl) para leer.

**Reto adicional:** mide la diferencia. Ejecuta `producer_consumer` contra las versiones antigua y nueva y compara el tiempo real transcurrido. Con una prueba pequeña la diferencia será imperceptible; con una prueba con muchos threads (múltiples productores y consumidores) la versión por CPU debería ser mediblemente más rápida.

### Desafío 6: Un simulador de interrupciones estilo hardware

Los buffers de los drivers reales suelen llenarse mediante un manejador de interrupciones, no mediante una syscall `write(2)`. Simula esto:

- Usa `callout(9)` (el Capítulo 13 lo cubre; puedes adelantarte) para ejecutar un callback cada 100 ms.
- El callback escribe un pequeño fragmento de datos en el cbuf (por ejemplo, la hora actual como cadena de texto).
- El usuario lee desde `/dev/myfirst` y ve un flujo de líneas con marca de tiempo.

Este desafío anticipa el material de ejecución diferida del Capítulo 13 y muestra cómo la misma abstracción de buffer admite tanto un productor dirigido por syscalls como uno dirigido por un thread del kernel.

### Desafío 7: Un buffer de registro con comportamiento estilo `dmesg`

Construye un segundo dispositivo de caracteres, `/dev/myfirst_log`, que use un cbuf en modo sobreescritura para mantener un registro circular de eventos recientes del driver. Cada llamada a la macro `MYFIRST_DBG` escribiría en este registro en lugar de (o además de) llamar a `device_printf`.

- Usa un `struct cbuf` separado en el softc.
- Proporciona una manera de que el lado del kernel introduzca líneas en el registro (`myfirst_log_printf(sc, fmt, ...)`).
- El usuario puede ejecutar `cat /dev/myfirst_log` para ver las últimas N líneas.
- Una nueva línea que desborde el buffer expulsa la línea más antigua, no solo el byte más antiguo (esto requiere lógica de desalojo consciente de líneas).

Este desafío introduce un patrón de driver bastante común (un registro de depuración privado) y te da práctica con un segundo caso de uso de buffer diseñado de forma independiente en el mismo módulo.

### Desafío 8: Medición de rendimiento

Construye un arnés de medición que mida el rendimiento del driver a lo largo de las cuatro etapas.

- Escribe un pequeño programa en C que abra el dispositivo, escriba 100 MB de datos y mida el tiempo de la operación.
- Complementa esto con un lector que consuma 100 MB y mida su propio tiempo.
- Ejecuta ambos contra las etapas 2, 3 y 4 del capítulo y produce una pequeña tabla con las cifras de rendimiento.
- Identifica qué etapa es la más lenta y explica por qué.

La respuesta esperada es "la etapa 3 es más lenta que la etapa 2 debido a las llamadas adicionales a `wakeup` y `selwakeup` por iteración; la etapa 4 es similar a la etapa 3 dentro del margen de ruido de medición". Sin embargo, los números reales son interesantes y pueden sorprenderte, dependiendo de tu CPU, el ancho de banda de memoria y la carga del sistema.

**Ampliación:** perfila el driver bajo carga con `pmcstat(8)` e identifica las tres funciones con mayor tiempo de CPU. Si `uiomove` aparece entre las tres primeras, habrás validado la discusión de la sección 8 sobre zero-copy. Si `mtx_lock` aparece entre las tres primeras, tienes un problema de contención que el material sobre locking del capítulo 11 abordará.

### Desafío 9: Lectura cruzada de drivers reales

Elige tres drivers en `/usr/src/sys/dev/` que no hayas leído antes. Para cada uno, identifica:

- Dónde se asigna y libera el buffer.
- Si es un buffer circular, una cola o tiene otra forma.
- Qué lo protege (mutex, sx, lock-free, ninguno).
- Cómo los manejadores `read` y `write` consumen de él o producen en él.
- Cómo se integran `select`/`poll`/`kqueue` con los cambios de estado del buffer.

Puntos de partida sugeridos: `/usr/src/sys/dev/iicbus/iiconf.c` (una categoría diferente, pero que usa algunas de las mismas primitivas) y `/usr/src/sys/fs/cuse/cuse.c` (un driver que expone su buffer al espacio de usuario). Verás variaciones sobre los mismos temas que acabas de construir.

### Desafío 10: Documenta tu driver

Escribe un README de una página en tu directorio `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/`. El README debería incluir:

- Qué hace el driver.
- Cómo construirlo (`make`).
- Cómo cargarlo y descargarlo (`kldload`, `kldunload`).
- La interfaz en espacio de usuario: ruta del dispositivo, modo, expectativas del lector/escritor, comportamiento de bloqueo.
- Qué exponen los sysctls.
- Cómo activar el registro de depuración.
- Una referencia al capítulo que lo generó.

La documentación es la parte del trabajo de desarrollo de drivers que se omite con más frecuencia. Un driver que solo comprende su autor supone un riesgo de mantenimiento. Incluso un README de una página que explique lo básico marca la diferencia entre código que sobrevive a un traspaso y código que no lo hace.

## Resolución de problemas y errores comunes

La mayor parte de los bugs que aparecen en el territorio de este capítulo se agrupan en un número reducido de categorías. La lista que sigue cataloga las categorías, los síntomas que produce cada una y la corrección. Léela una vez antes de trabajar en los laboratorios; vuelve a ella cuando algo salga mal.

### Síntoma: `cat /dev/myfirst` se bloquea indefinidamente, incluso después de que `echo` escribe datos

**Causa.** El manejador de escritura no llama a `wakeup(&sc->cb)` tras un `cbuf_write` exitoso. El lector está dormido en el canal `&sc->cb`; sin un `wakeup` correspondiente, nunca retornará.

**Corrección.** Añade `wakeup(&sc->cb)` después de cada operación que cambie el estado y que pueda desbloquear a un hilo en espera. En `myfirst_write`, eso significa después de la llamada a `cbuf_write`. En `myfirst_read`, después de la llamada a `cbuf_read` (que puede desbloquear a un escritor en espera).

**Cómo verificarlo.** Ejecuta `ps -AxH -o pid,wchan,command | grep cat`. Si la columna `wchan` muestra `myfrd` (o el wmesg que hayas usado), el lector está durmiendo. La dirección del canal en el que se durmió debe coincidir con la dirección del canal que se despierta.

### Síntoma: Corrupción de datos bajo carga elevada

**Causa.** Casi siempre es un bug de wrap-around o un lock ausente en el acceso al cbuf. O bien la aritmética interna del cbuf es incorrecta, o bien dos threads acceden a él concurrentemente sin sincronización.

**Corrección.** Vuelve a leer el código fuente del cbuf con atención. Ejecuta el `cb_test` de userland contra tu `cbuf.c` actual (compílalo directamente con `cc`). Si los tests de userland pasan, el problema está en el locking del driver, no en el cbuf. Comprueba que cada llamada `cbuf_*` esté delimitada por `mtx_lock` y `mtx_unlock`. Usa `INVARIANTS` y `WITNESS` en tu configuración del kernel para detectar violaciones.

**Cómo verificarlo.** Ejecuta `producer_consumer` con un checksum conocido. Si los checksums coinciden pero se reportan discrepancias, los datos están siendo reordenados (un bug de wrap-around). Si los checksums difieren, se están perdiendo o duplicando bytes (un bug de locking).

### Síntoma: Kernel panic con "sleeping with mutex held"

**Causa.** Llamaste a `uiomove(9)`, `copyin(9)`, `copyout(9)` u otra función que puede dormir mientras mantenías `sc->mtx`. La función intentó generar un fallo en memoria de usuario, y el manejador de page-fault intentó dormir, pero mantener un mutex no durmible durante un sleep está prohibido.

**Corrección.** Libera el mutex antes de cualquier llamada que pueda dormir. Los manejadores de la Etapa 4 hacen esto con cuidado: adquieren el lock para acceder al cbuf, lo liberan para llamar a `uiomove` y lo vuelven a adquirir para actualizar el estado.

**Cómo verificarlo.** Un kernel con `WITNESS` habilitado imprimirá una advertencia antes de entrar en pánico. La advertencia identifica el mutex y la función durmiente. La primera vez que ocurra, copia el mensaje en un log de depuración para poder localizar el punto de la llamada.

### Síntoma: Se devuelve `EAGAIN` incluso cuando hay datos disponibles

**Causa.** El manejador comprueba la flag incorrecta, o la comprueba en el lugar equivocado del bucle. Dos variantes frecuentes: comprobar `ioflag & O_RDONLY` en lugar de `ioflag & IO_NDELAY`, o devolver `EAGAIN` después de que ya se hayan transferido algunos bytes (que es la regla de la Sección 4 que no debes romper).

**Corrección.** Vuelve a leer con atención el código de los manejadores de la Sección 5. La ruta de `EAGAIN` está dentro del bucle interno `while (cbuf_used(&sc->cb) == 0)`, después de la comprobación de `nbefore`, y solo cuando `ioflag & IO_NDELAY` es distinto de cero.

**Cómo verificarlo.** Ejecuta `rw_myfirst_nb`. El paso 5 debería leer los bytes correctamente. Si muestra `EAGAIN`, el bug está en uno de los dos puntos indicados arriba.

### Síntoma: Una escritura tiene éxito pero una lectura posterior obtiene menos bytes

**Causa.** El contador de bytes se actualiza de forma incorrecta, o el cbuf se modifica fuera de los manejadores. Un modo de fallo concreto: contar `want` bytes como escritos cuando `cbuf_write` solo almacenó `put` bytes (una condición de carrera en la Etapa 2 entre la comprobación de `cbuf_free` y la llamada a `cbuf_write`, aunque no se manifieste en uso con un único escritor).

**Corrección.** Fíjate en la línea `bytes_written += put` de `myfirst_write`; debe utilizar el valor de retorno real de `cbuf_write`, no el tamaño solicitado. Compara `sc->bytes_written` y `sc->bytes_read` a lo largo del tiempo; deberían diferir como mucho en `cbuf_size`.

**Cómo verificarlo.** Añade una línea de log: `device_printf(dev, "wrote %zu of %zu\n", put, want);`. Si `put != want` aparece en algún momento en `dmesg`, habrás encontrado la discrepancia.

### Síntoma: `kldunload` devuelve `EBUSY`

**Causa.** Algún descriptor sigue abierto en el dispositivo. El detach se niega a continuar cuando `active_fhs > 0`.

**Corrección.** Localiza el proceso que mantiene el descriptor abierto y ciérralo. `fstat | grep myfirst` lista los procesos problemáticos. Mátalos con `kill` si es necesario.

**Cómo verificarlo.** Después de cerrar todos los descriptores (o matar los procesos problemáticos), `sysctl dev.myfirst.0.stats.active_fhs` debería bajar a cero. `kldunload myfirst` debería tener éxito entonces.

### Síntoma: Crecimiento de memoria en `vmstat -m | grep cbuf`

**Causa.** El driver está asignando memoria sin liberarla. O bien la ruta de fallo del attach olvidó llamar a `cbuf_destroy`, o la ruta del detach lo olvidó, o se está asignando más de un cbuf por attach.

**Corrección.** Audita cada ruta de código que llame a `cbuf_init`. Cada llamada debe tener exactamente una llamada correspondiente a `cbuf_destroy` antes de que desaparezca el contexto que la rodea. El patrón estándar es colocar `cbuf_init` cerca del inicio de `attach` y `cbuf_destroy` cerca del final de `detach`, con la cadena de `goto fail_*` de la ruta de fallo llamando a `cbuf_destroy` si el attach falla después de `cbuf_init`.

**Cómo verificarlo.** Carga y descarga el módulo varias veces con `kldload` y `kldunload`. `vmstat -m | grep cbuf` debería mostrar `0` después de cada `kldunload`.

### Síntoma: `select(2)` o `poll(2)` no se despiertan

**Causa.** Al driver le falta una llamada a `selwakeup` cuando cambia el estado. O la ruta de lectura olvidó llamar a `selwakeup(&sc->wsel)` después de consumir bytes, o la ruta de escritura olvidó llamar a `selwakeup(&sc->rsel)` después de añadir bytes.

**Corrección.** El patrón es el siguiente: cada cambio de estado que pueda convertir una condición previamente no lista en una condición lista debe ir acompañado de una llamada a `selwakeup`. Consumir bytes -> `selwakeup(&sc->wsel)`. Añadir bytes -> `selwakeup(&sc->rsel)`.

**Cómo verificarlo.** Ejecuta `rw_myfirst_nb`. El paso 4 debería mostrar `revents=0x41`. Si muestra `revents=0x0`, falta la llamada a `selwakeup` o el manejador `myfirst_poll` no está estableciendo `revents` correctamente.

### Síntoma: `truss` muestra `EINVAL` para lecturas de cero bytes

**Causa.** Tu manejador rechaza las lecturas de cero bytes con `EINVAL`. Como se explicó en la Sección 4, las lecturas y escrituras de cero bytes son legales y el manejador no debe producir un error en ellas.

**Corrección.** Elimina cualquier retorno anticipado `if (uio->uio_resid == 0) return (EINVAL);` al inicio de `myfirst_read` o `myfirst_write`.

**Cómo verificarlo.** Un programa que llame a `read(fd, NULL, 0)` debería ver que la llamada devuelve `0`, no `-1` con `EINVAL`.

### Síntoma: `ps` muestra al lector bloqueado en un estado de sleep con un nombre diferente al esperado

**Causa.** Tu `mtx_sleep` se llama con un `wmesg` diferente al esperado. Dos variantes frecuentes: una errata (`mfyrd` en lugar de `myfrd`), o el mismo manejador se llama desde una ruta de código donde el motivo de espera es en realidad diferente.

**Corrección.** Estandariza las cadenas `wmesg`. `myfrd` para "myfirst read" y `myfwr` para "myfirst write". Una cadena corta y única por punto de espera hace que `ps -AxH` sea inmediatamente informativo.

**Cómo verificarlo.** `ps -AxH` debería mostrar `myfrd` para los lectores dormidos y `myfwr` para los escritores dormidos.

### Síntoma: Una señal no interrumpe una lectura bloqueada

**Causa.** El `mtx_sleep` se llama sin `PCATCH`. Sin `PCATCH`, las señales se difieren hasta que el sleep termina por algún otro motivo.

**Corrección.** Pasa siempre `PCATCH` para sleeps desencadenados por el usuario. La excepción son los sleeps que deben ser no interrumpibles (lógica interna del kernel que no debe cancelarse mediante una señal). Para `myfirst_read` y `myfirst_write`, ambos están controlados por el usuario y ambos deben pasar `PCATCH`.

**Cómo verificarlo.** `cat /dev/myfirst &` seguido de `kill -INT %1` debería hacer que `cat` termine. Si `cat` no termina hasta que también escribes en el dispositivo (o envías `kill -9`), falta `PCATCH`.

### Síntoma: El compilador advierte sobre un prototipo ausente para `cbuf_read`

**Causa.** El código fuente del driver usa `cbuf_read` pero no incluye `cbuf.h`.

**Corrección.** Añade `#include "cbuf.h"` cerca del inicio de `myfirst.c`. La ruta del archivo es relativa al directorio fuente, por lo que mientras ambos archivos estén en el mismo directorio el include se resolverá correctamente.

**Cómo verificarlo.** Una compilación limpia sin advertencias.

### Síntoma: `make` se queja de que faltan `bus_if.h` o `device_if.h`

**Causa.** Al Makefile le falta la línea estándar `SRCS+= device_if.h bus_if.h` que incorpora las cabeceras kobj generadas automáticamente para Newbus.

**Corrección.** Usa el Makefile de la Sección 3.

**Cómo verificarlo.** `make clean && make` debería completarse sin errores de cabeceras ausentes.

### Síntoma: `kldload` falla con `Exec format error`

**Causa.** El .ko se compiló contra un kernel diferente al que está en ejecución actualmente. Esto ocurre habitualmente cuando reinicias con un kernel diferente sin recompilar, o cuando copias un .ko de una máquina a otra con fuentes de kernel distintas.

**Corrección.** Ejecuta `make clean && make` contra el `/usr/src` del kernel en ejecución.

**Cómo verificarlo.** `uname -a` debería coincidir con la versión del kernel con la que se compiló el .ko. Comprueba `dmesg` después del `kldload` fallido para obtener más detalles.

### Síntoma: El driver reporta datos correctos pero los pierde después de varias ejecuciones

**Causa.** El cbuf no se reinicia entre attaches, o el softc no se inicializa a cero. Con `M_ZERO` en la llamada a `malloc(9)` (y la llamada a `cbuf_init` inicializando a cero su propio estado), esto no debería ocurrir, pero una corrección parcial que omita uno de estos pasos puede dejar estado obsoleto.

**Corrección.** Audita `myfirst_attach` para asegurarte de que cada campo del softc se inicializa explícitamente. Usa `M_ZERO` en la llamada a `malloc(9)` que asigna el softc (Newbus lo hace automáticamente con `device_get_softc`, pero verifícalo). Usa `cbuf_init` para poner los índices del cbuf a cero.

**Cómo verificarlo.** Ejecuta `kldload`, escribe algunos datos, `kldunload` y de nuevo `kldload`. El nuevo attach debería reportar `cb_used = 0`.

### Síntoma: `producer_consumer` reporta un pequeño número de discrepancias bajo carga elevada

**Causa.** Un bug de locking sutil, frecuentemente relacionado con el orden de las llamadas a `wakeup` y la recomprobación del bucle de sleep interno. El síntoma clásico: bajo contención, ocasionalmente un thread se despierta y consume bytes que otro thread creía que seguían disponibles.

**Corrección.** Verifica que todo uso de `mtx_sleep` esté dentro de un bucle `while` (no `if`), y que el bucle vuelva a comprobar la condición tras despertar. La señal de despertar es una *pista*, no una garantía; un thread que se despierta puede encontrar la condición falsa de nuevo porque otro thread llegó antes.

**Cómo verificarlo.** `producer_consumer` debería notificar cero discrepancias en múltiples ejecuciones. Un número de discrepancias que varía entre ejecuciones sugiere una condición de carrera; un número de discrepancias que es siempre exactamente N sugiere un error de desfase en uno.

### Consejos generales para la depuración

Tres hábitos hacen que la depuración de drivers sea mucho más rápida.

El primero es tener un kernel compilado con `WITNESS` disponible. `WITNESS` detecta violaciones del orden de adquisición de locks y bugs del tipo "durmiendo con mutex adquirido" que un kernel de producción permitiría en silencio. El coste de rendimiento es significativo, así que ejecuta `WITNESS` en tu entorno de laboratorio, no en producción.

El segundo es añadir líneas de log con `device_printf` de forma generosa durante el desarrollo y, antes de hacer commit, eliminarlas o protegerlas tras una variable `myfirst_debug`. El buffer de log es finito, así que no registres por byte; una línea por llamada de I/O es la granularidad adecuada.

El tercero es compilar con `-Wall -Wextra` y tratar los avisos como bugs. El sistema de build del kernel ya pasa muchos flags de aviso por defecto; préstales atención. Casi todo aviso es el kernel advirtiéndote de un bug real o potencial.

Cuando todo lo demás falle, siéntate y traza el camino de ejecución en papel. Un driver de este tamaño es lo suficientemente pequeño como para caber en una sola hoja. El noventa por ciento de las veces, dibujar el grafo de llamadas y las adquisiciones de locks en orden te muestra el bug.

## Referencia rápida: patrones y primitivas

Esta referencia es el material del capítulo reducido a forma de consulta rápida. Úsala después de haber leído el capítulo; es un recordatorio, no un tutorial.

### La API del buffer circular

```c
struct cbuf {
        char    *cb_data;       /* backing storage */
        size_t   cb_size;       /* total capacity */
        size_t   cb_head;       /* next byte to read */
        size_t   cb_used;       /* live byte count */
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);
size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);
size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);
```

Reglas:
- El cbuf no hace locking; es responsabilidad del llamador.
- `cbuf_write` y `cbuf_read` limitan `n` al espacio disponible o a los datos activos y devuelven el recuento real.
- `cbuf_used` y `cbuf_free` devuelven el estado actual bajo la suposición de que el llamador mantiene el lock que protege el cbuf.

### Los helpers a nivel de driver

```c
size_t  myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n);
size_t  myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n);
int     myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
            struct uio *uio);
int     myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
            struct uio *uio);
```

Reglas:
- Los cuatro helpers comprueban mediante `mtx_assert(MA_OWNED)` que `sc->mtx` está adquirido.
- Los helpers de espera devuelven `-1` para indicar "rompe el bucle externo, devuelve 0 al espacio de usuario".
- Los helpers de espera devuelven `EAGAIN`, `EINTR`, `ERESTART` o `ENXIO` para las condiciones correspondientes.

### El esqueleto del handler de lectura

```c
nbefore = uio->uio_resid;
while (uio->uio_resid > 0) {
        mtx_lock(&sc->mtx);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error == -1 ? 0 : error);
        }
        take = MIN((size_t)uio->uio_resid, sizeof(bounce));
        got = myfirst_buf_read(sc, bounce, take);
        fh->reads += got;
        mtx_unlock(&sc->mtx);

        wakeup(&sc->cb);
        selwakeup(&sc->wsel);

        error = uiomove(bounce, got, uio);
        if (error != 0)
                return (error);
}
return (0);
```

### El esqueleto del handler de escritura

```c
nbefore = uio->uio_resid;
while (uio->uio_resid > 0) {
        mtx_lock(&sc->mtx);
        error = myfirst_wait_room(sc, ioflag, nbefore, uio);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error == -1 ? 0 : error);
        }
        room = cbuf_free(&sc->cb);
        mtx_unlock(&sc->mtx);

        want = MIN((size_t)uio->uio_resid, sizeof(bounce));
        want = MIN(want, room);
        error = uiomove(bounce, want, uio);
        if (error != 0)
                return (error);

        mtx_lock(&sc->mtx);
        put = myfirst_buf_write(sc, bounce, want);
        fh->writes += put;
        mtx_unlock(&sc->mtx);

        wakeup(&sc->cb);
        selwakeup(&sc->rsel);
}
return (0);
```

### El patrón de espera

```c
mtx_lock(&sc->mtx);
while (CONDITION) {
        if (uio->uio_resid != nbefore)
                break_with_zero;
        if (ioflag & IO_NDELAY)
                return (EAGAIN);
        error = mtx_sleep(CHANNEL, &sc->mtx, PCATCH, "wmesg", 0);
        if (error != 0)
                return (error);
        if (!sc->is_attached)
                return (ENXIO);
}
/* condition is false now; act on the buffer */
```

Reglas:
- Usa `while`, no `if`, alrededor de la condición.
- Pasa siempre `PCATCH` en las esperas iniciadas por el usuario.
- Comprueba siempre la condición de nuevo después de que `mtx_sleep` retorne.
- Comprueba siempre `is_attached` tras despertar, por si hay un detach pendiente.

### El patrón de despertar

```c
/* After a state change that might unblock a sleeper: */
wakeup(CHANNEL);
selwakeup(SELINFO);
```

Reglas:
- El canal debe coincidir con el canal pasado a `mtx_sleep`.
- El selinfo debe ser aquel contra el que `selrecord` registró la espera.
- Usa `wakeup` (despertar a todos) para esperadores compartidos; `wakeup_one` para patrones de transferencia a un único receptor.
- Los despertares espurios son seguros; los despertares perdidos son bugs.

### El handler `d_poll`

```c
static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int revents = 0;

        mtx_lock(&sc->mtx);
        if (events & (POLLIN | POLLRDNORM)) {
                if (cbuf_used(&sc->cb) > 0)
                        revents |= events & (POLLIN | POLLRDNORM);
                else
                        selrecord(td, &sc->rsel);
        }
        if (events & (POLLOUT | POLLWRNORM)) {
                if (cbuf_free(&sc->cb) > 0)
                        revents |= events & (POLLOUT | POLLWRNORM);
                else
                        selrecord(td, &sc->wsel);
        }
        mtx_unlock(&sc->mtx);
        return (revents);
}
```

### El handler `d_mmap`

```c
static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct myfirst_softc *sc = dev->si_drv1;

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);
        if ((nprot & VM_PROT_WRITE) != 0)
                return (EACCES);
        if (offset >= sc->cb.cb_size)
                return (-1);
        *paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
        return (0);
}
```

### La `cdevsw`

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       myfirst_open,
        .d_close =      myfirst_close,
        .d_read =       myfirst_read,
        .d_write =      myfirst_write,
        .d_poll =       myfirst_poll,
        .d_mmap =       myfirst_mmap,
        .d_name =       "myfirst",
};
```

### Valores de errno para I/O

| Errno     | Significado                                               |
|-----------|-----------------------------------------------------------|
| `0`       | Éxito                                                     |
| `EAGAIN`  | Bloquearía; reintenta más tarde                           |
| `EFAULT`  | Puntero de usuario incorrecto (de `uiomove`)              |
| `EINTR`   | Interrumpido por una señal                                |
| `ENXIO`   | Dispositivo no presente o ya desmontado                   |
| `EIO`     | Error de hardware                                         |
| `ENOSPC`  | Sin espacio de forma permanente (preferible bloquear en lleno) |
| `EACCES`  | Modo de acceso no permitido                               |
| `EBUSY`   | El dispositivo está abierto o bloqueado por otra razón    |

### Bits de `ioflag`

| Bit            | Flag de origen | Significado                                   |
|----------------|----------------|-----------------------------------------------|
| `IO_NDELAY`    | `O_NONBLOCK`   | El llamador es no bloqueante                  |
| `IO_DIRECT`    | `O_DIRECT`     | Evitar la caché donde sea posible             |
| `IO_SYNC`      | `O_FSYNC`      | (solo escritura) Semántica síncrona           |

`O_NONBLOCK == IO_NDELAY` según el CTASSERT del kernel.

### Eventos de `poll(2)`

| Evento       | Significado                                              |
|--------------|----------------------------------------------------------|
| `POLLIN`     | Legible: hay bytes disponibles                           |
| `POLLRDNORM` | Igual que POLLIN para dispositivos de caracteres         |
| `POLLOUT`    | Escribible: hay espacio disponible                       |
| `POLLWRNORM` | Igual que POLLOUT para dispositivos de caracteres        |
| `POLLERR`    | Condición de error                                       |
| `POLLHUP`    | Cuelgue (el extremo remoto cerró la conexión)            |
| `POLLNVAL`   | Descriptor de archivo inválido                           |

Un driver gestiona típicamente `POLLIN | POLLRDNORM` para indicar disponibilidad de lectura y `POLLOUT | POLLWRNORM` para disponibilidad de escritura. Los demás eventos suelen ser establecidos por el kernel, no por el driver.

### Referencia del asignador de memoria

| Llamada                                              | Cuándo usarla                           |
|------------------------------------------------------|-----------------------------------------|
| `malloc(n, M_DEVBUF, M_WAITOK \| M_ZERO)`            | Asignación normal, puede dormir         |
| `malloc(n, M_DEVBUF, M_NOWAIT \| M_ZERO)`            | No puede dormir (contexto de interrupción) |
| `free(p, M_DEVBUF)`                                  | Liberar memoria asignada con lo anterior |
| `MALLOC_DEFINE(M_TAG, "name", "desc")`               | Declarar una etiqueta de memoria propia |
| `contigmalloc(n, M_TAG, M_WAITOK, ...)`              | Asignación físicamente contigua         |

### Referencia de espera y despertar

| Llamada                                                           | Cuándo usarla                               |
|-------------------------------------------------------------------|---------------------------------------------|
| `mtx_sleep(chan, mtx, PCATCH, "msg", 0)`                          | Dormir con interbloqueo de mutex            |
| `tsleep(chan, PCATCH \| pri, "msg", timo)`                        | Dormir sin mutex (poco habitual en drivers) |
| `cv_wait(&cv, &mtx)`                                              | Dormir sobre una variable de condición      |
| `wakeup(chan)`                                                    | Despertar a todos los durmientes del canal  |
| `wakeup_one(chan)`                                                | Despertar a un solo durmiente (transferencia a receptor único) |

### Referencia de locks

| Llamada                                                  | Cuándo usarla                              |
|----------------------------------------------------------|--------------------------------------------|
| `mtx_init(&mtx, "name", "type", MTX_DEF)`                | Inicializar un mutex dormible              |
| `mtx_destroy(&mtx)`                                      | Destruir en el detach                      |
| `mtx_lock(&mtx)`, `mtx_unlock(&mtx)`                     | Adquirir / liberar                         |
| `mtx_assert(&mtx, MA_OWNED)`                             | Comprobar que el lock está adquirido (debug) |

### Referencia de herramientas de prueba

| Herramienta              | Uso                                                    |
|--------------------------|--------------------------------------------------------|
| `cat`, `echo`            | Pruebas rápidas de humo                                |
| `dd`                     | Pruebas de volumen, observación de transferencias parciales |
| `hexdump -C`             | Verificar el contenido en bytes                        |
| `truss -t read,write`    | Trazar los valores de retorno de las syscalls          |
| `ktrace`                 | Trazado detallado, incluidas señales                   |
| `sysctl dev.myfirst.0.stats` | Estado en vivo del driver                          |
| `vmstat -m`              | Contabilidad de etiquetas de memoria                   |
| `ps -AxH`                | Encontrar threads durmientes y su wmesg                |
| `dmesg | tail`           | Líneas de log emitidas por el driver y avisos del kernel |

### Resumen del ciclo de vida del driver

```text
kldload
    -> myfirst_identify   (optional in this driver: creates the child)
    -> myfirst_probe      (returns BUS_PROBE_DEFAULT)
    -> myfirst_attach     (allocates softc, cbuf, cdev, sysctl, mutex)

steady state
    -> myfirst_open       (allocates per-fh state)
    -> myfirst_read       (drains cbuf via bounce + uiomove)
    -> myfirst_write      (fills cbuf via uiomove + bounce)
    -> myfirst_poll       (reports POLLIN/POLLOUT readiness)
    -> myfirst_close      (per-fh dtor releases per-fh state)

kldunload
    -> myfirst_detach     (refuses if any descriptor open)
    -> wakeup releases sleepers
    -> destroy_dev
    -> cbuf_destroy
    -> sysctl_ctx_free
    -> mtx_destroy
```

### Resumen de la distribución de archivos

```text
examples/part-02/ch10-handling-io-efficiently/
    README.md
    cbuf-userland/
        cbuf.h
        cbuf.c
        cb_test.c
        Makefile
    stage2-circular/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage3-blocking/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage4-poll-refactor/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage5-mmap/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    userland/
        rw_myfirst_v2.c
        rw_myfirst_nb.c
        producer_consumer.c
        stress_rw.c
        mmap_myfirst.c
        Makefile
```

Cada directorio de etapa es independiente; puedes ejecutar `make` y `kldload` en cualquiera de ellos sin tocar los demás. Las herramientas de userland son compartidas por todas las etapas.

### Un modelo mental en un párrafo

El driver posee un buffer circular, protegido por un único mutex. Un lector adquiere el mutex mientras transfiere bytes del buffer hacia un bounce buffer en la pila, libera el mutex, copia el bounce hacia el espacio de usuario con `uiomove`, despierta a los escritores en espera y repite el ciclo hasta que la petición del usuario queda satisfecha o el buffer está vacío. Un escritor hace lo contrario: adquiere el mutex, copia bytes del usuario hacia el bounce, libera el mutex, copia el bounce hacia el buffer, despierta a los lectores en espera y repite. Cuando el buffer está vacío (en lecturas) o lleno (en escrituras), el handler duerme con el mutex como interbloqueo (modo por defecto) o devuelve `EAGAIN` (modo no bloqueante). La integración con `select(2)` y `poll(2)` se proporciona a través de `selrecord` (en `d_poll`) y `selwakeup` (en los handlers de I/O). La ruta de detach espera a que todos los descriptores se cierren y luego libera todo.

Ese párrafo cabe en tu cabeza. Todo lo demás en este capítulo es la elaboración cuidadosa de cómo hacer que cada pieza funcione.

## Apéndice: una lectura guiada de `evdev/cdev.c`

El capítulo ha señalado `/usr/src/sys/dev/evdev/cdev.c` en varias ocasiones como el ejemplo más limpio del árbol de un dispositivo de caracteres que hace lo que `myfirst` hace ahora: un buffer en anillo por cliente, lecturas bloqueantes, soporte no bloqueante e integración con `select`/`poll`/`kqueue`. Leer ese archivo una vez, con los patrones de este capítulo en la mano, es la forma más rápida de confirmar que el kernel realmente funciona como el capítulo ha venido describiendo. Este apéndice recorre las partes relevantes.

El objetivo *no* es enseñar `evdev`. Es usar `evdev` como ejemplo ilustrativo. Al final de este recorrido deberías sentir que lo que construiste en `myfirst` tiene la misma forma que lo que el kernel utiliza para dispositivos de entrada reales. Las diferencias están en los detalles (el protocolo, las estructuras, la pila de drivers por capas), no en los patrones subyacentes.

### Qué es `evdev`

`evdev` es el puerto de FreeBSD de la interfaz de dispositivos de eventos de Linux. Expone dispositivos de entrada (teclados, ratones, pantallas táctiles) a través de nodos `/dev/input/eventN` que los programas del espacio de usuario (servidores X, compositores Wayland, gestores de consola) leen para obtener un flujo de eventos de entrada. Cada evento es una estructura de tamaño fijo con una marca de tiempo, un tipo, un código y un valor.

La capa del driver que nos interesa es el cdev por cliente. Cuando un proceso abre `/dev/input/event0`, el kernel crea un `struct evdev_client` para ese descriptor, lo adjunta al dispositivo subyacente y lo usa como buffer por apertura. Las lecturas extraen eventos del buffer; las escrituras introducen eventos en él (para algunos dispositivos); `select`/`poll`/`kqueue` informan cuando hay eventos disponibles.

Esa descripción debería resultarte muy familiar a estas alturas. Es la misma arquitectura que `myfirst` en la Etapa 4, con tres diferencias: el buffer es por descriptor en lugar de por dispositivo; la unidad de transferencia es una estructura de tamaño fijo en lugar de un byte; y el driver participa en un framework más amplio de gestión de entrada.

### El estado por cliente

Abre `/usr/src/sys/dev/evdev/evdev_private.h` (el archivo es breve; puedes leer las partes relevantes en un par de minutos). La estructura clave es `struct evdev_client`:

```c
struct evdev_client {
        struct evdev_dev *      ec_evdev;
        struct mtx              ec_buffer_mtx;
        size_t                  ec_buffer_size;
        size_t                  ec_buffer_head;
        size_t                  ec_buffer_tail;
        size_t                  ec_buffer_ready;
        ...
        bool                    ec_blocked;
        bool                    ec_revoked;
        ...
        struct selinfo          ec_selp;
        struct sigio *          ec_sigio;
        ...
        struct input_event      ec_buffer[];
};
```

Compárala con tu softc de `myfirst`:

- `ec_evdev` es el análogo de `dev->si_drv1` en `evdev`: un puntero inverso desde el estado por cliente hacia el estado global del dispositivo.
- `ec_buffer_mtx` es el mutex por cliente; el `sc->mtx` de `myfirst` es por dispositivo.
- `ec_buffer_size`, `ec_buffer_head`, `ec_buffer_tail` y `ec_buffer_ready` son los índices del buffer circular. Fíjate en que `evdev` utiliza un `tail` explícito en lugar de uno derivado; el código es ligeramente distinto, pero la estructura es la misma.
- `ec_blocked` es un flag indicativo para la lógica de wakeup.
- `ec_revoked` marca a un cliente desconectado forzosamente; es el equivalente de `is_attached` en `myfirst`.
- `ec_selp` es el `selinfo` para el soporte de `select`/`poll`/`kqueue`, exactamente igual que `sc->rsel` y `sc->wsel` en tu driver (aquí combinados en uno porque evdev solo gestiona la disponibilidad de lectura; no existe el concepto de «la escritura bloquearía»).
- `ec_buffer[]` es el miembro de array flexible que contiene los eventos reales.

Los patrones son los mismos. Los nombres son distintos.

### El manejador de lectura

Abre `/usr/src/sys/dev/evdev/cdev.c` y busca `evdev_read`:

```c
static int
evdev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct evdev_dev *evdev = dev->si_drv1;
        struct evdev_client *client;
        ...
        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (ret);

        debugf(client, "read %zd bytes by thread %d", uio->uio_resid,
            uio->uio_td->td_tid);

        if (client->ec_revoked)
                return (ENODEV);

        ...
        if (uio->uio_resid != 0 && uio->uio_resid < evsize)
                return (EINVAL);

        remaining = uio->uio_resid / evsize;

        EVDEV_CLIENT_LOCKQ(client);

        if (EVDEV_CLIENT_EMPTYQ(client)) {
                if (ioflag & O_NONBLOCK)
                        ret = EWOULDBLOCK;
                else {
                        if (remaining != 0) {
                                client->ec_blocked = true;
                                ret = mtx_sleep(client, &client->ec_buffer_mtx,
                                    PCATCH, "evread", 0);
                                if (ret == 0 && client->ec_revoked)
                                        ret = ENODEV;
                        }
                }
        }

        while (ret == 0 && !EVDEV_CLIENT_EMPTYQ(client) && remaining > 0) {
                head = client->ec_buffer + client->ec_buffer_head;
                ...
                bcopy(head, &event.t, evsize);

                client->ec_buffer_head =
                    (client->ec_buffer_head + 1) % client->ec_buffer_size;
                remaining--;

                EVDEV_CLIENT_UNLOCKQ(client);
                ret = uiomove(&event, evsize, uio);
                EVDEV_CLIENT_LOCKQ(client);
        }

        EVDEV_CLIENT_UNLOCKQ(client);

        return (ret);
}
```

Recórrelo despacio.

El manejador recupera el estado por cliente con `devfs_get_cdevpriv`, exactamente igual que hace `myfirst_read`. La comprobación de `ec_revoked` es el equivalente en `evdev` de la comprobación `is_attached` de `myfirst`, con la diferencia de que `evdev` devuelve `ENODEV` en lugar de `ENXIO` (ambas son opciones válidas para indicar «el dispositivo ya no existe»).

El manejador valida a continuación que el tamaño de transferencia solicitado sea un múltiplo del tamaño de un registro de evento (porque entregar eventos parciales no tiene ningún sentido). Esto supone un nivel adicional respecto a lo que hace `myfirst`, y es específico de los dispositivos de flujo de eventos.

Luego, exactamente como se describía en la sección 5, el manejador entra en un bucle de *comprobar-esperar-recomprobar*. Si el buffer está vacío (`EVDEV_CLIENT_EMPTYQ(client)`), el manejador devuelve `EWOULDBLOCK` (el mismo valor que `EAGAIN`) para los llamadores en modo no bloqueante, o duerme con `mtx_sleep` y `PCATCH`. El canal de espera es el propio `client`; el mutex de enclavamiento es `&client->ec_buffer_mtx`. Cuando la espera finaliza, el manejador vuelve a comprobar `ec_revoked` y devuelve `ENODEV` si el cliente se ha desconectado mientras dormía.

Tras la espera, el manejador entra en el bucle de transferencia. Extrae un evento del buffer (con `bcopy` en una variable `event` residente en la pila), avanza `ec_buffer_head` módulo el tamaño del buffer, libera el mutex y llama a `uiomove` para enviar el evento al espacio de usuario. Luego vuelve a adquirir el mutex y continúa hasta que el buffer se vacía o se satisface la solicitud del usuario.

Este es el patrón de bounce buffer de la sección 3, donde `event` desempeña el papel de `bounce`. La operación sobre el cbuf es `bcopy(head, &event.t, evsize)` (copia de un único evento fuera del anillo) seguida de `uiomove(&event, evsize, uio)` (transferencia al usuario). El mutex se mantiene solo durante la operación sobre el cbuf, nunca durante `uiomove`. Esta es exactamente la regla que pusimos en práctica en `myfirst`.

### El wakeup

Busca `evdev_notify_event` (la función que se invoca cuando se entrega un nuevo evento en el buffer de un cliente):

```c
void
evdev_notify_event(struct evdev_client *client)
{

        EVDEV_CLIENT_LOCKQ_ASSERT(client);

        if (client->ec_blocked) {
                client->ec_blocked = false;
                wakeup(client);
        }
        if (client->ec_selected) {
                client->ec_selected = false;
                selwakeup(&client->ec_selp);
        }

        KNOTE_LOCKED(&client->ec_selp.si_note, 0);
}
```

Ahí está el wakeup. `wakeup(client)` corresponde al `mtx_sleep(client, ...)` de `evdev_read`. `selwakeup(&client->ec_selp)` corresponde al `selrecord` que veremos en un momento. `KNOTE_LOCKED` es el análogo de `selwakeup` para `kqueue`; aún no lo hemos construido (es territorio del capítulo 11), pero el patrón es el mismo.

El flag `ec_blocked` es una optimización: si ningún cliente está durmiendo en ese momento, se omite el wakeup. Es una optimización pequeña pero útil. `myfirst` no la tiene porque el coste es despreciable en nuestro caso de uso, pero podrías añadir la misma comprobación sin ningún esfuerzo.

### El poll

Busca `evdev_poll`:

```c
static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
        struct evdev_client *client;
        int revents = 0;
        int ret;

        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (POLLNVAL);

        if (events & (POLLIN | POLLRDNORM)) {
                EVDEV_CLIENT_LOCKQ(client);
                if (!EVDEV_CLIENT_EMPTYQ(client))
                        revents = events & (POLLIN | POLLRDNORM);
                else {
                        client->ec_selected = true;
                        selrecord(td, &client->ec_selp);
                }
                EVDEV_CLIENT_UNLOCKQ(client);
        }

        return (revents);
}
```

Esto es prácticamente idéntico al `myfirst_poll` de la sección 5, con dos diferencias. `evdev` solo gestiona `POLLIN`; no hay `POLLOUT` porque los eventos de entrada son unidireccionales. Y `evdev` devuelve `POLLNVAL` si `devfs_get_cdevpriv` falla, que es la respuesta convencional para «este descriptor no es válido» (frente al enfoque más sencillo de `myfirst`, que devuelve cero).

El patrón es el que introdujo la sección 5: comprobar la condición, devolver listo si es verdadera, registrar con `selrecord` si no lo es. El flag `ec_selected` es de nuevo una optimización para evitar wakeups innecesarios; puedes ignorarlo para comprender el funcionamiento general.

### El kqfilter

Busca `evdev_kqfilter`:

```c
static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
        struct evdev_client *client;
        int ret;

        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (ret);

        switch(kn->kn_filter) {
        case EVFILT_READ:
                kn->kn_fop = &evdev_cdev_filterops;
                break;
        default:
                return(EINVAL);
        }
        kn->kn_hook = (caddr_t)client;

        knlist_add(&client->ec_selp.si_note, kn, 0);

        return (0);
}
```

Este es el manejador de registro para `kqueue`. Es uno de los temas del capítulo 11; lo mostramos aquí únicamente para señalar que el campo `si_note` de `selinfo` es el gancho que usa `kqueue`. El mecanismo de `selrecord`/`selwakeup` para `select`/`poll`, y el mecanismo de `knlist_add`/`KNOTE_LOCKED` para `kqueue`, comparten la misma estructura `selinfo`. Ese uso compartido es lo que permite que un único conjunto de llamadas de cambio de estado (en `evdev_notify_event`) despierte las tres rutas de notificación de disponibilidad a la vez.

Cuando extendamos `myfirst` con soporte para `kqueue` en el capítulo 11, los cambios encajarán en prácticamente el mismo patrón: un manejador `myfirst_kqfilter` que registra contra `&sc->rsel.si_note`, y una llamada `KNOTE_LOCKED(&sc->rsel.si_note, 0)` junto a cada `selwakeup(&sc->rsel)`. El sustrato ya está aquí.

### Lo que confirma este recorrido

A estas alturas, tres cosas deberían estar claras.

La primera es que los *patrones* que has ido construyendo no son invención de este libro. Son los patrones que usa el kernel, en un driver real que se distribuye con FreeBSD y que ejecuta cada usuario de teclado y ratón cada día. Puedes leer este código, reconocer lo que hace cada sección y explicarlo. Eso es una habilidad real que da sus frutos en todos los capítulos que siguen.

La segunda es que los *detalles* difieren entre drivers. `evdev` usa un índice `tail` explícito. Usa registros de eventos de tamaño fijo en lugar de bytes. Tiene buffers por cliente en lugar de por dispositivo. Usa `bcopy` en lugar de una abstracción cbuf. Ninguna de estas diferencias invalida el patrón subyacente; son decisiones sobre cómo especializar el patrón para un caso de uso concreto.

La tercera es que *puedes leer más*. Con un par de horas y un café, puedes recorrer `/usr/src/sys/dev/uart/uart_core.c` o `/usr/src/sys/dev/snp/snp.c`. Cada uno tendrá un aspecto diferente a primera vista, pero el buffer, el locking, el sleep/wake, el poll: todo eso te resultará familiar. El capítulo te ha entregado un vocabulario; el código fuente del kernel es donde lo ejercitas.

### Un plan de lectura breve

Si quieres adquirir el hábito de leer código fuente del kernel como parte de tu flujo de trabajo de desarrollo de drivers, aquí tienes un plan breve. Dedica una hora a la semana, durante tres semanas, a lo siguiente.

Primera semana: vuelve a leer `/usr/src/sys/dev/null/null.c` y `/usr/src/sys/dev/evdev/cdev.c`. Compáralos. El primero es el dispositivo de caracteres más sencillo posible; el segundo es uno con buffer competente. Anota exactamente qué características tiene cada archivo y por qué.

Segunda semana: lee `/usr/src/sys/dev/random/randomdev.c`. Es más grande que `evdev`, pero usa los mismos patrones, con la adición de una capa de recolección de entropía por debajo. Anota en qué se diferencia `randomdev_read` de `evdev_read` y `myfirst_read`, y por qué.

Tercera semana: elige un driver en `/usr/src/sys/dev/` que te interese (un driver USB, un driver de red, un driver de almacenamiento). Lee la parte que gestiona las operaciones de I/O con el espacio de usuario. Para entonces, los patrones deberían serte lo suficientemente familiares como para que las partes desconocidas (el enlace al bus, el acceso a registros de hardware, la configuración de DMA) destaquen como las cosas *nuevas* que aprender, no como obstáculos para entender la ruta de I/O.

Después de tres semanas con este ritmo, habrás leído más código de drivers de lo que la mayoría de los desarrolladores de kernel profesionales leen en un mes típico. La inversión se multiplica.

## Resumen del capítulo

Este capítulo tomó el buffer en el kernel del capítulo 9, que era un FIFO lineal que desperdiciaba la mitad de su capacidad, y lo convirtió en un verdadero buffer circular con semántica adecuada de I/O parcial, modos bloqueante y no bloqueante, e integración con `poll(2)`. El driver con el que terminas el capítulo es notablemente mejor que el que tenías al empezarlo, en cuatro aspectos concretos.

Usa su capacidad completa. El buffer circular mantiene toda la asignación en uso. Un escritor constante y un lector equivalente pueden mantener el buffer a cualquier nivel de llenado indefinidamente; el desbordamiento circular es invisible para los manejadores de I/O porque vive dentro de la abstracción cbuf.

Respeta las transferencias parciales. Tanto `myfirst_read` como `myfirst_write` iteran hasta que no pueden avanzar, y luego devuelven cero con `uio_resid` reflejando la porción no consumida. Un llamador en espacio de usuario que itera sobre `read(2)` o `write(2)` verá la semántica UNIX correcta. Un llamador que no itera seguirá viendo conteos correctos; el driver no descarta bytes silenciosamente.

Bloquea correctamente. Un lector que encuentra el buffer vacío duerme en un canal limpio, con el mutex liberado de forma atómica; un escritor que añade bytes despierta al durmiente. El mismo patrón funciona en la dirección contraria. Las señales se respetan a través de `PCATCH`, de modo que el Ctrl-C del usuario interrumpe a un lector bloqueado en cuestión de microsegundos.

Admite el modo no bloqueante. Un descriptor abierto con `O_NONBLOCK` (o con el flag establecido posteriormente mediante `fcntl(2)`) recibe `EAGAIN` en lugar de un sleep. Un manejador `d_poll` informa de `POLLIN` y `POLLOUT` correctamente según el estado del buffer, y `selrecord(9)` junto con `selwakeup(9)` garantizan que los llamadores de `select(2)` y `poll(2)` se despierten cuando cambia la disponibilidad.

Cada una de esas capacidades se construye en una sección numerada, cada una con código que compila, se carga y se comporta de forma predecible. Las etapas del capítulo (un buffer en espacio de usuario, un empalme en el kernel, una versión con gestión de bloqueo, una refactorización con soporte para poll, una variante con mapeo de memoria) forman una progresión clara que sigue el orden en que un principiante se encuentra naturalmente con estas cuestiones.

Por el camino hemos tratado tres temas complementarios que suelen aparecer junto a las operaciones de I/O con buffer en los drivers reales: `d_mmap(9)`, los patrones y limitaciones del enfoque zero-copy, y los patrones de lectura anticipada y consolidación de escrituras usados por los drivers de alto rendimiento. Ninguno de ellos es un tema propio del capítulo 10, pero cada uno se apoya de forma natural en la abstracción de buffer que acabas de establecer.

Hemos ejercitado el driver con cinco nuevos programas de prueba en espacio de usuario (`rw_myfirst_v2`, `rw_myfirst_nb`, `producer_consumer`, `stress_rw`, `mmap_myfirst`) más las herramientas estándar del sistema base (`dd`, `cat`, `hexdump`, `truss`, `sysctl`, `vmstat`). La combinación es suficiente para detectar la mayoría de los errores que suele producir este material. La sección de resolución de problemas cataloga esos errores con sus síntomas y correcciones; tenla a mano.

Por último, refactorizamos el acceso al buffer en un pequeño conjunto de funciones auxiliares (`myfirst_buf_read`, `myfirst_buf_write`, `myfirst_wait_data`, `myfirst_wait_room`), escribimos un comentario de un párrafo sobre la estrategia de locking cerca del principio del código fuente, y colocamos el cbuf en su propio archivo con su propio `MALLOC_DEFINE`. El código fuente del driver tiene ahora la forma que el capítulo 11 necesita: disciplina de locking clara, abstracciones bien delimitadas, sin sorpresas.

## Cerrando

El driver con buffer que acabas de terminar es la base de todo lo que viene después en la parte 3. La forma de su ruta de I/O, la disciplina de su locking, la manera en que duerme y despierta, la manera en que se compone con `poll(2)`: estos no son los patrones del capítulo 10. Son *los* patrones que usa el kernel, y una vez que los reconozcas en tu propio código, los reconocerás en cada driver de dispositivo de caracteres de `/usr/src/sys/dev/`.

Vale la pena tomarse un momento para percibir ese cambio. Cuando empezaste el capítulo 7, el interior de un driver era probablemente un conjunto opaco de nombres y firmas. Al final del capítulo 8 tenías una idea del ciclo de vida. Al final del capítulo 9 tenías una idea de la ruta de datos. Ahora, al final del capítulo 10, tienes una idea de cómo se comporta un driver bajo carga: cómo se adapta a llamadores concurrentes, cómo gestiona un recurso finito (el buffer), cómo coopera con las primitivas de disponibilidad del kernel, cómo deja el camino libre para que los programas en espacio de usuario puedan hacer trabajo real con él.

La mayor parte de lo que has construido pasará al capítulo 11. El mutex, el buffer, las funciones auxiliares, la estrategia de locking, el kit de pruebas, la disciplina de laboratorio: todo ello permanece. Lo que cambia en el capítulo 11 es la *profundidad* de las preguntas que haces sobre cada pieza. ¿Por qué un mutex y no dos? ¿Por qué `wakeup` y no `wakeup_one`? ¿Por qué `mtx_sleep` y no `cv_wait`? ¿Qué garantías ofrece el kernel sobre cuándo se despierta un durmiente, y qué garantías no ofrece? ¿Cómo demuestras que un fragmento de código es correcto bajo concurrencia, en lugar de simplemente esperarlo?

El capítulo 11 toma esas preguntas en serio. Presenta `WITNESS` e `INVARIANTS` como las herramientas de verificación del kernel, recorre las clases de lock y analiza los patrones que convierten una concurrencia que simplemente funciona en una concurrencia verificablemente correcta. Será un capítulo sustancial, pero el sustrato es lo que acabas de construir.

Tres recordatorios finales antes de continuar.

Lo primero es *hacer commit de tu código*. Sea cual sea el sistema de control de versiones que uses, guarda los cuatro directorios de etapa como una instantánea. El primer laboratorio del siguiente capítulo copiará el código fuente de la Stage 4 y lo modificará; no querrás perder esa base funcional.

Lo segundo es *realizar los laboratorios*. Leer código de drivers te enseña los patrones; escribir código de drivers te enseña la disciplina. Los laboratorios de este capítulo son breves a propósito. Incluso los más largos se pueden completar en una sola sesión. La combinación de «lo construí yo» y «lo rompí a propósito para ver qué pasaba» es lo que el capítulo está diseñado para producir.

Lo tercero es *confiar en el camino lento*. El capítulo ha sido deliberadamente cuidadoso, deliberadamente paciente, deliberadamente repetitivo en algunos puntos. El trabajo con drivers recompensa ese estilo. Los errores que duelen son los que parecen que no podrían suceder nunca. La defensa contra ellos es ser lento, cuidadoso y metódico, incluso cuando el código parece sencillo. Quien va despacio en cada paso termina el Capítulo 11 preparado para el Capítulo 12; quien se precipita termina el Capítulo 11 con un kernel panic y una tarde perdida.

Lo estás haciendo bien. Sigue adelante.

## Punto de control de la Parte 2

Antes de adentrarte en la Parte 3, detente y comprueba que tienes los pies bien asentados. La Parte 2 te ha llevado desde «qué es un módulo» hasta «un pseudo-driver multietapa que atiende a lectores y escritores reales bajo carga». La próxima parte pondrá ese driver a prueba en una escala mucho más exigente, así que la base tiene que ser sólida.

A estas alturas deberías poder hacer cada una de las siguientes cosas sin necesidad de ir a buscar la respuesta:

- Escribir, compilar, cargar y descargar un módulo del kernel en un kernel en ejecución, y leer `dmesg` para confirmar el ciclo de vida.
- Construir un esqueleto Newbus con `device_probe`, `device_attach` y `device_detach`, respaldado por un softc por unidad asignado con `device_get_softc`.
- Exponer un nodo `/dev` a través de un `cdevsw` con manejadores funcionales `d_open`, `d_close`, `d_read`, `d_write` y `d_ioctl`, y verificar que `devfs` elimina el nodo al descargar el módulo.
- Gestionar un buffer circular cuyo estado está protegido por un mutex, cuyos lectores pueden bloquearse con `mtx_sleep` y ser despertados por `wakeup`, y cuya disponibilidad se anuncia mediante `selrecord` y `selwakeup`.
- Provocar un fallo deliberado en la ruta de attach y observar cómo cada asignación se deshace en orden inverso.

Si alguno de esos puntos te resulta inestable, los laboratorios que los fundamentan merecen una segunda vuelta. Una lista de repaso dirigida:

- Disciplina de construcción, carga y descarga: Laboratorio 7.2 (Construir, cargar y verificar el ciclo de vida) y Laboratorio 7.4 (Simular un fallo de attach y verificar el desenrollado).
- Higiene de `cdevsw` y nodos `devfs`: Laboratorio 8.1 (Nombre estructurado y permisos más estrictos) y Laboratorio 8.5 (Driver con dos nodos).
- Ruta de datos y comportamiento de ida y vuelta: Laboratorio 9.2 (Ejercitar la etapa 2 con escrituras y lecturas) y Laboratorio 9.3 (Comportamiento FIFO de la etapa 3).
- La secuencia central del Capítulo 10: Laboratorio 2 (Buffer circular de la etapa 2), Laboratorio 3 (Etapa 3: bloqueo y no bloqueo) y Laboratorio 4 (Etapa 4: soporte de poll y refactorización).

La Parte 3 dará por sentado que todo lo anterior es memoria muscular, no algo que hay que consultar. En concreto, el Capítulo 11 esperará:

- Un `myfirst` en etapa 4 funcional que se cargue, se descargue y sobreviva a lectores y escritores concurrentes sin corrupción.
- Familiaridad con `mtx_sleep`/`wakeup` y el par `selrecord`/`selwakeup` como primitivas básicas de bloqueo y disponibilidad del kernel, ya que la Parte 3 los comparará y contrastará con `cv(9)`, `sx(9)` y `sema(9)`.
- Un kernel construido con `INVARIANTS` y `WITNESS`, ya que todos los capítulos de la Parte 3 se apoyan en ambos desde la primera sección.

Si esos tres puntos son sólidos, estás listo para pasar página. Si alguno flaquea, corrígelo antes de seguir. Una hora tranquila ahora te ahorra una tarde desconcertante después.

## Mirando hacia adelante: puente al Capítulo 11

El Capítulo 11 se titula «Concurrencia en drivers». Su cometido es tomar el driver que acabas de terminar y examinarlo a través de la lente de la concurrencia: no en el sentido informal de «funciona bajo carga moderada» que hemos utilizado hasta ahora, sino en el sentido riguroso de «puedo demostrar que esto es correcto bajo cualquier intercalado de operaciones».

El puente se apoya en tres observaciones extraídas del trabajo del Capítulo 10.

En primer lugar, ya tienes un único mutex que protege todo el estado compartido. Ese es el diseño de concurrencia no trivial más sencillo que puede tener un driver, y es el punto de partida adecuado para entender las alternativas más elaboradas. El Capítulo 11 usará tu driver como caso de prueba para preguntarse cuándo es suficiente un único mutex, cuándo no lo es, y qué hacer cuando no lo es.

En segundo lugar, ya tienes un patrón de suspensión y despertar que utiliza el mutex como enclavamiento. `mtx_sleep` y `wakeup` son los bloques de construcción de toda primitiva de bloqueo en el kernel. El Capítulo 11 presentará las variables de condición (`cv_*`) como una alternativa más estructurada y explicará cuándo resulta adecuada cada una.

En tercer lugar, ya tienes una abstracción de buffer que intencionadamente no es thread-safe por sí sola. El cbuf delega en el llamante la responsabilidad de proporcionar el locking. El Capítulo 11 discutirá el espectro que va desde «la estructura de datos no proporciona locking» (tu cbuf) pasando por «la estructura de datos proporciona locking interno» (algunas primitivas del kernel) hasta «la estructura de datos es lock-free» (lectores basados en `buf_ring(9)` y `epoch(9)`). Cada extremo del espectro tiene sus usos; entender cuándo elegir cada uno forma parte de convertirse en un desarrollador de drivers.

Los temas concretos que cubrirá el Capítulo 11 son los siguientes:

- Las cinco clases de lock de FreeBSD (`mtx`, `sx`, `rw`, `rm`, `lockmgr`) y cuándo resulta adecuada cada una.
- El orden de adquisición de locks y cómo usar `WITNESS` para verificarlo.
- La interacción entre los locks y el contexto de interrupción.
- Las variables de condición y cuándo preferirlas sobre `mtx_sleep`.
- Los locks de lector/escritor y sus casos de uso.
- El framework `epoch(9)` para estructuras de datos con lecturas predominantes.
- Las operaciones atómicas (`atomic_*`) y cuándo eliminan la necesidad de usar locks.
- Un recorrido por los errores de concurrencia más habituales (despertares perdidos, inversiones del orden de locks, ABA, doble liberación bajo contención).

No necesitas adelantar lectura para comenzar el Capítulo 11. Todo lo visto en este capítulo es preparación suficiente. Trae tu driver en etapa 4, tu kit de pruebas y tu kernel con `WITNESS` habilitado; el próximo capítulo comienza donde este terminó.

Una pequeña despedida de este capítulo: acabas de convertir un driver de principiante en algo respetable. Los bytes que fluyen por `/dev/myfirst` lo hacen ahora de la misma manera que fluyen por cualquier otro dispositivo de caracteres del sistema. Los patrones son correctos, el locking es correcto, los contratos con el espacio de usuario se respetan. El driver es tuyo para extenderlo, especializarlo y usarlo como base para cualquier dispositivo real que venga después. Tómate un momento para disfrutarlo, y luego pasa página.
