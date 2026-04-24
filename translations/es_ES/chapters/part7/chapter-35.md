---
title: "E/S asíncrona y gestión de eventos"
description: "Implementación de operaciones asíncronas y arquitecturas orientadas a eventos"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 35
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 135
language: "es-ES"
---
# E/S asíncrona y gestión de eventos

## Introducción

Hasta este punto, casi todos los drivers que hemos escrito han funcionado siguiendo un esquema sencillo. Un proceso de usuario llama a `read(2)` y espera. Nuestro driver produce datos, el kernel los copia hacia fuera y la llamada retorna. Un proceso de usuario llama a `write(2)` y espera. Nuestro driver consume los datos, los almacena y la llamada retorna. El thread de usuario duerme mientras el driver trabaja, y se despierta cuando el trabajo está terminado. Este es el modelo síncrono, y es el punto de partida correcto para enseñar a escribir drivers porque encaja con la forma de una llamada a función ordinaria: pides algo, esperas y obtienes una respuesta.

La E/S síncrona funciona bien para muchos dispositivos, pero falla para otros. Un teclado no decide producir una pulsación de tecla porque un programa haya llamado a `read()`. Un puerto serie no sincroniza los bytes entrantes para que coincidan con el ritmo del lector. Un sensor puede producir datos a intervalos irregulares, o únicamente cuando ocurre algo interesante en el mundo físico. Si insistimos en que todo usuario de ese tipo de dispositivo debe bloquearse en `read()` hasta que llegue el siguiente evento, obligamos al programa en userland a enfrentarse a una elección muy difícil. Puede dedicar un thread a bloquearse en cada dispositivo, lo que hace el programa difícil de escribir y lento para responder a cualquier otra cosa, o puede ejecutar un bucle en userland llamando a `read()` con un timeout corto una y otra vez, lo que desperdicia ciclos de CPU y aun así pierde eventos que ocurren entre sondeos.

FreeBSD resuelve este problema proporcionando a los drivers un conjunto de mecanismos de notificación asíncrona, cada uno construido sobre la misma idea subyacente: un proceso no necesita bloquearse en `read()` para saber que hay datos disponibles. En su lugar, puede registrar interés en un dispositivo, hacer otro trabajo útil y dejar que el kernel lo despierte cuando el dispositivo tenga algo que comunicar. Los mecanismos difieren en sus detalles, sus perfiles de rendimiento y sus casos de uso previstos, pero comparten una forma común. Un proceso en espera declara qué está esperando, el driver registra ese interés, el driver descubre más tarde que la condición se ha satisfecho y el driver entrega una notificación que provoca que el proceso en espera sea despertado, planificado o señalizado.

Cuatro de estos mecanismos son relevantes para los autores de drivers. Las llamadas al sistema clásicas `poll(2)` y `select(2)` permiten que un programa en userland pregunte al kernel cuáles de un conjunto de descriptores de archivo están listos. El framework más reciente `kqueue(2)` ofrece una interfaz de eventos más eficiente y expresiva, y es la opción preferida para las aplicaciones modernas de alto rendimiento. El mecanismo de señal `SIGIO`, invocado a través de `FIOASYNC` y `fsetown()`, entrega señales a un proceso registrado cada vez que cambia el estado del dispositivo. Y los drivers que necesitan hacer un seguimiento de sus propios eventos internos suelen construir una pequeña cola de eventos dentro del softc, de modo que los lectores vean una secuencia coherente de registros legibles en lugar del estado bruto del hardware.

En este capítulo aprenderemos cómo funciona cada uno de estos mecanismos, cómo implementarlo correctamente en un driver de caracteres, cómo combinarlos para que un único driver pueda atender llamadas de `poll(2)`, `kqueue(2)` y `SIGIO` simultáneamente, y cómo auditar el código resultante en busca de las sutiles condiciones de carrera y los wakeups perdidos que son la pesadilla de la programación asíncrona. Basaremos cada parte en el código fuente real de FreeBSD 14.3, analizando cómo `if_tuntap.c`, `sys_pipe.c` y `evdev/cdev.c` resuelven los mismos problemas en producción.

Al final del capítulo serás capaz de tomar un driver bloqueante y dotarlo de soporte asíncrono completo sin romper su semántica síncrona. Sabrás cómo implementar correctamente los manejadores `d_poll()`, `d_kqfilter()` y `FIOASYNC`. Entenderás por qué `selrecord()` y `selwakeup()` deben llamarse en un orden específico con un bloqueo específico. Sabrás qué es un `knlist`, cómo se adjunta `knote` a él y por qué `KNOTE_LOCKED()` es la llamada que quieres utilizar en casi todos los drivers. Verás cómo `fsetown()` y `pgsigio()` se combinan para entregar señales exactamente al proceso correcto. Y sabrás cómo construir una cola de eventos interna que una todo el mecanismo, de modo que cada notificación asíncrona lleve al lector a un único registro coherente y bien definido dentro del driver.

A lo largo del capítulo desarrollaremos un driver complementario llamado `evdemo`. Es un pseudodispositivo que simula una fuente de eventos: marcas de tiempo, transiciones de estado y eventos «interesantes» ocasionales que un programa en userland quiere observar en tiempo real. Cada sección del capítulo añade una nueva capa a `evdemo`, de modo que al final tendrás un driver asíncrono pequeño pero completo que podrás cargar, inspeccionar y extender. Al igual que `bugdemo` en el capítulo anterior, `evdemo` no toca hardware real, por lo que todos los experimentos son seguros para ejecutarlos en una máquina virtual FreeBSD de desarrollo.

## Guía del lector: cómo usar este capítulo

Este capítulo se encuentra en la Parte 7 del libro, en la sección de Temas de Maestría, inmediatamente después del capítulo de Depuración Avanzada. Asume que ya has escrito al menos un driver de caracteres sencillo, sabes cómo cargar y descargar un módulo de forma segura, y has trabajado con los manejadores síncronos `read()`, `write()` e `ioctl()`. Si alguno de esos conceptos te resulta inseguro, repasar rápidamente los Capítulos 8 al 12 te compensará con creces en este.

No necesitas haber terminado todos los capítulos anteriores de maestría para seguir este. Un lector que haya dominado el patrón básico del driver de caracteres y haya visto `callout(9)` o `taskqueue(9)` de pasada podrá seguir el ritmo. Cuando el material de un capítulo anterior sea esencial, te daremos un breve recordatorio en la sección correspondiente.

El material es acumulativo dentro del capítulo. Cada sección añade un nuevo mecanismo asíncrono al driver `evdemo`, y el refactor final los une todos. Puedes adelantar para conocer un mecanismo concreto, pero los laboratorios se leen de forma más natural en orden, ya que los laboratorios posteriores asumen el código de los anteriores.

No necesitas ningún hardware especial. Una máquina virtual FreeBSD 14.3 modesta es suficiente para todos los laboratorios del capítulo. Una consola serie es útil pero no obligatoria. Querrás tener un segundo terminal abierto para poder ver `dmesg`, ejecutar los programas de prueba en espacio de usuario y monitorizar los canales de espera en `top(1)` mientras el driver está cargado.

Un plan de lectura razonable sería el siguiente. Lee las tres primeras secciones de una sola vez para construir el modelo mental de poll y select. Tómate un descanso. Lee las Secciones 4 y 5 otro día, porque `kqueue` y las señales introducen cada una un nuevo conjunto de ideas. Trabaja los laboratorios a tu propio ritmo. El capítulo es largo a propósito: la E/S asíncrona es donde reside gran parte de la complejidad de los drivers, y precipitarse con el material es la forma más segura de escribir un driver que funcione la mayor parte del tiempo pero que pierda wakeups en casos poco frecuentes.

Parte del código de este capítulo hace deliberadamente lo incorrecto para que podamos ver los síntomas de los errores más habituales. Esos ejemplos están claramente etiquetados. Los laboratorios terminados hacen lo correcto, y el driver refactorizado final es seguro de cargar.

## Cómo sacar el máximo provecho de este capítulo

El capítulo sigue un patrón que verás repetido en cada sección. Primero explicamos qué es un mecanismo y qué problema resuelve. Luego mostramos cómo espera userland que se comporte, para que entiendas el contrato que tu driver debe cumplir. Después examinamos el código fuente real del kernel de FreeBSD para ver cómo implementan el mecanismo los drivers existentes. Y finalmente lo aplicamos al driver `evdemo` en un laboratorio.

Varios hábitos te ayudarán a asimilar el material.

Mantén un terminal abierto en `/usr/src/` para poder consultar cualquier archivo fuente de FreeBSD al que el capítulo haga referencia. La E/S asíncrona es una de las áreas donde leer drivers reales aporta más beneficios, porque los patrones son lo suficientemente cortos para verlos de una sola lectura y las variaciones entre drivers te enseñan qué es esencial y qué es meramente estilístico. Cuando el capítulo mencione `if_tuntap.c` o `sys_pipe.c`, abre el archivo y míralo. Un minuto con el código fuente real construye más intuición que cualquier descripción de segunda mano.

Mantén un segundo terminal abierto en tu máquina virtual FreeBSD para poder cargar y descargar `evdemo` a medida que avanza el capítulo. Escribe el código tú mismo la primera vez que lo veas. Los archivos complementarios de `examples/part-07/ch35-async-io/` contienen los fuentes terminados, pero escribir el código genera memoria muscular que la lectura no proporciona. Cuando una sección introduzca un nuevo callback, añádelo al driver, reconstruye, recarga y prueba antes de continuar.

Presta mucha atención al bloqueo. La E/S asíncrona es el área donde una adquisición de lock descuidada puede convertir un driver limpio en un deadlock o en una corrupción silenciosa de datos. Cuando el capítulo muestre un mutex siendo adquirido antes de una llamada a `selrecord()` o `KNOTE_LOCKED()`, fíjate en el orden y pregúntate por qué debe ser así. Cuando una instrucción de laboratorio indique tomar el mutex del softc antes de modificar una cola de eventos, tómalo. La disciplina en el uso de locks es el único hábito que separa de forma más fiable los drivers asíncronos que funcionan de los que funcionan casi siempre.

Por último, recuerda que el código asíncrono tiende a revelar sus errores solo bajo presión. Un driver que supera una prueba de un solo thread puede seguir teniendo wakeups perdidos o condiciones de carrera que se manifiestan cuando dos o tres threads compiten por el mismo dispositivo. Varios laboratorios de este capítulo incluyen pruebas de estrés con varios lectores precisamente por ese motivo. No los saltes. Ejecutar tu código bajo contención es la mejor manera de demostrar que funciona de verdad.

Con esos hábitos en mente, comencemos con la diferencia entre E/S síncrona y asíncrona, y la cuestión de cuándo cada una es la elección correcta.

## 1. E/S síncrona frente a asíncrona en los drivers de dispositivo

La E/S síncrona es el modelo que hemos utilizado en casi todos los drivers hasta ahora. Un proceso de usuario llama a `read(2)`. El kernel delega en nuestro callback `d_read`. O devolvemos los datos que ya están disponibles, o dormimos el thread llamante en una variable de condición hasta que lleguen datos. Cuando los datos están listos, despertamos el thread, este copia los datos y `read(2)` retorna. El programa de usuario se bloquea durante la llamada y luego continúa.

Este patrón es fácil de razonar. Coincide con el funcionamiento de las funciones ordinarias: llamas, esperas y obtienes un resultado. También es una opción muy buena para los dispositivos donde la demanda del llamante impulsa el trabajo del dispositivo. Un lector de disco pide datos y se le ordena al controlador de disco que los obtenga. Un sensor con una operación `read_current_value` encaja naturalmente con una llamada síncrona. Para estos dispositivos, el proceso de usuario siempre sabe cuándo pedir, y el coste de la espera es el coste de la E/S real.

Pero para muchos dispositivos reales, el trabajo del driver no lo impulsa la demanda del llamante. Lo impulsa el mundo exterior.

### El mundo no espera a read()

Considera un teclado. El dispositivo no tiene opinión sobre quién llama a `read(2)` cuando se pulsa una tecla. El usuario pulsa la tecla, se dispara una interrupción, el driver extrae un código de exploración del hardware y los datos están ahora disponibles. Si un programa en userland está bloqueado en `read()`, se despierta y recibe la tecla. Si ningún programa está leyendo, la tecla permanece en un buffer. Si varios programas comparten interés en el teclado, solo uno de ellos recibe la tecla con la semántica de bloqueo clásica, lo cual casi nunca es lo que el programador quiere.

Considera un puerto serie. Los bytes llegan a la velocidad del cable, independientemente de la disposición de cualquier programa para recibirlos. Si el driver bloquea cada byte entrante detrás de un lector, en la práctica obliga al lector a mantener un thread siempre dormido en `read()`, por si acaso ocurre algo. Ese thread no puede hacer nada más. Un único proceso bien diseñado podría querer reaccionar a varios puertos serie, un socket de red, un temporizador y un teclado, todo al mismo tiempo. El modelo síncrono no puede expresar eso.

Imagina un sensor USB que solo informa un valor cuando la magnitud medida supera un umbral. Un sensor de temperatura puede generar un evento solo cuando la temperatura varía más de medio grado. Un sensor de movimiento puede dispararse solo cuando detecta movimiento. El ritmo propio del dispositivo, no el del userland, decide cuándo están listos los datos. Un proceso que bloquea en `read()` podría esperar milisegundos, segundos, minutos o, simplemente, no recibir nada nunca.

Todas estas situaciones comparten una propiedad: el evento es externo a la solicitud del programa. El driver sabe cuándo están listos los datos. El userland, no. Si el userland tiene que bloquear en `read()` cada vez para averiguar lo que el driver ya sabe, el programa queda a merced del ritmo del driver.

### Por qué el busy waiting no es la respuesta correcta

Una solución ingenua consiste en que el programa de espacio de usuario consulte al driver de forma continua. En lugar de llamar a `read()` una vez y bloquearse, lo llama en modo no bloqueante una y otra vez. `open(/dev/...)` con `O_NONBLOCK` devuelve el control inmediatamente si no hay datos disponibles. El programa puede girar en un bucle: llama a `read()`, hace otro trabajo, vuelve a llamar a `read()`, y así sucesivamente.

Este patrón se denomina busy waiting y casi siempre es incorrecto. Consume CPU aunque no ocurra nada, porque el programa no para de preguntar al driver si tiene trabajo. Se pierden eventos que suceden entre consultas. Añade latencia a cada evento: una tecla pulsada cien microsegundos después de la última consulta tiene que esperar hasta la siguiente para ser detectada. Y escala mal: un programa que vigila diez dispositivos de este tipo tiene que consultarlos a todos en cada iteración, agravando todos los problemas.

El busy waiting solo es adecuado en una situación concreta: cuando la frecuencia de consulta es conocida, la latencia del dispositivo se mide en microsegundos y el programa no tiene ningún otro trabajo que hacer. Incluso en ese caso, la solución correcta suele ser utilizar los mecanismos de temporización de alta precisión de la CPU y `usleep()` entre consultas, en lugar de girar en bucle. En cualquier otro caso, el busy waiting es la herramienta equivocada.

El modelo de bloqueo síncrono y el modelo de busy waiting son los dos extremos de un espectro. Ambos desperdician recursos. Lo que necesitamos es una tercera opción: el espacio de usuario le pide al kernel que le avise cuando el dispositivo esté listo y, mientras tanto, realiza otro trabajo hasta que el kernel levanta la mano. Esa tercera opción es lo que proporciona la I/O asíncrona.

### La I/O asíncrona no es simplemente una lectura no bloqueante

Un error frecuente entre los principiantes es pensar que la I/O asíncrona consiste en llamar a `read()` con `O_NONBLOCK`. No es así. Un `read()` no bloqueante devuelve el control inmediatamente si no hay datos disponibles; eso es una propiedad útil, pero no constituye I/O asíncrona por sí sola. Un `read()` no bloqueante sin un mecanismo de notificación no es más que busy waiting con mejor apariencia.

La I/O asíncrona, en el sentido que emplea este capítulo, es un protocolo de notificación entre el driver y el espacio de usuario. El espacio de usuario no necesita estar en una llamada de lectura para enterarse de que el driver tiene datos. El driver no tiene que adivinar quién está interesado. Cuando el estado del driver cambia de forma relevante, notifica a quienes esperan a través de un mecanismo bien definido: `poll`/`select`, `kqueue`, `SIGIO` o alguna combinación de ellos. El que espera se despierta, lee los datos y vuelve a esperar.

Esta distinción importa porque separa tres aspectos independientes en un driver:

El primero es el registro de la espera. Un programa de espacio de usuario declara su interés en un dispositivo llamando a `poll()`, `kevent()` o habilitando `FIOASYNC`. El driver almacena ese registro para poder encontrar al que espera más adelante.

El segundo es la entrega del aviso de activación. Cuando el estado del driver cambia, este llama a `selwakeup()`, `KNOTE_LOCKED()` o `pgsigio()` para entregar la notificación. Esta es una operación separada de la producción de datos. Un driver puede producir datos sin entregar una notificación (por ejemplo, durante un relleno inicial que ocurre antes de que nadie se haya registrado). Un driver puede entregar una notificación sin producir datos (por ejemplo, cuando un dispositivo cuelga la línea). Y un driver puede entregar varias notificaciones para una unidad de datos si hay varios mecanismos registrados.

El tercero es la propiedad del evento. Una señal `SIGIO` se entrega a un proceso o grupo de procesos concreto. Un `knote` pertenece a un `kqueue` concreto. Un proceso en espera de `select()` pertenece a un thread concreto. Si el driver no puede asociar el aviso de activación al propietario correcto, las notificaciones se pierden o se entregan a la parte equivocada. Cada mecanismo tiene sus propias reglas para asociar notificaciones a propietarios, y debemos aplicar esas reglas correctamente para cada uno de ellos por separado.

Mantener estos tres aspectos bien diferenciados es uno de los temas principales de este capítulo. Muchos de los bugs sutiles en los drivers asíncronos provienen de confundirlos. Si alguna vez te preguntas por qué existe una determinada llamada de activación o por qué se mantiene un lock concreto, nueve de cada diez veces la respuesta está en mantener separados el registro, la entrega y la propiedad.

### Patrones del mundo real: fuentes de eventos que necesitan I/O asíncrona

Conviene nombrar los patrones en los que la I/O asíncrona es la elección correcta, porque una vez que los reconoces los verás en todas partes.

Los dispositivos de entrada de caracteres son el caso clásico. Un teclado, un ratón, una pantalla táctil, un joystick: cada uno produce eventos cuando el usuario interactúa con él, a una cadencia que nadie puede predecir de antemano. El usuario puede pulsar una tecla ahora o dentro de cinco minutos. El driver sabe cuándo llega el evento. El espacio de usuario necesita una forma de enterarse.

Las interfaces serie y de red son otro caso. Los bytes llegan del cable al ritmo del cable. Un emulador de terminal no quiere bloquearse esperando el siguiente byte, porque también tiene que redibujar la pantalla, responder a la entrada del teclado y actualizar el cursor. Un programa de red no quiere bloquearse esperando el siguiente paquete, porque normalmente tiene que vigilar varios sockets a la vez.

Los sensores que informan según una condición son un tercer caso. Un botón que indica «pulsado» o «liberado». Un sensor de temperatura que se activa cuando el valor medido cruza un umbral. Un detector de movimiento. Un contacto de puerta. Todos ellos son dirigidos por eventos en el sentido estricto: no ocurre nada hasta que algo interesante sucede en el mundo.

Las líneas de control y las señales de módem son un cuarto caso. Las líneas `CARRIER`, `DSR` y `RTS` de un puerto serie cambian de estado independientemente del flujo de datos. Un programa que depende de ellas quiere que se le notifique cuando cambian, no tener que consultarlas continuamente.

Cualquier dispositivo que combina varios tipos de eventos en una sola corriente de datos es un quinto caso. Considera un dispositivo de entrada `evdev` que agrega pulsaciones de teclado, movimientos de ratón y eventos de pantalla táctil en una corriente de eventos unificada. El driver construye una cola interna de eventos, un registro por cada acontecimiento relevante, y los lectores extraen eventos de ella. Construiremos una versión reducida de exactamente este patrón más adelante en el capítulo, porque ilustra cómo una cola de eventos, la notificación asíncrona y la semántica síncrona de `read()` se combinan en un driver bien estructurado.

### Cuándo no usar I/O asíncrona

En aras del equilibrio, veamos algunos casos en los que la I/O asíncrona no es la respuesta correcta.

Un driver cuya única operación es una transferencia masiva a petición del llamante no tiene motivos para exponer `poll()` ni `kqueue()`. Si toda interacción es un viaje de ida y vuelta que inicia el usuario, el modelo de bloqueo síncrono es a la vez más sencillo y correcto. Añadir notificación asíncrona a un driver así solo aumenta la complejidad.

Un driver con una tasa de datos tan alta que cualquier sobrecarga de notificación sea relevante puede necesitar un enfoque completamente diferente. `netmap(4)` y otros frameworks de kernel-bypass existen precisamente para este caso, y están muy lejos del alcance de este capítulo. Un diseño ordinario basado en `kqueue()` funciona bien hasta millones de eventos por segundo, pero en algún punto el coste de cualquier mecanismo de notificación se convierte en un cuello de botella.

Un driver cuyo consumidor es otro subsistema del kernel, en lugar de un programa de espacio de usuario, generalmente no necesita en absoluto una notificación asíncrona orientada al espacio de usuario. Necesita sincronización dentro del kernel: mutexes, variables de condición, `callout(9)`, `taskqueue(9)`. Esos son los patrones que estudiamos en capítulos anteriores y siguen siendo la respuesta correcta cuando ambos lados del evento viven dentro del kernel.

Para todo lo que queda en medio, la I/O asíncrona es la herramienta adecuada, y aprenderla correctamente es una de las habilidades más duraderas que puede adquirir un autor de drivers. Las tres secciones siguientes construyen el modelo mental y el código: primero `poll()` y `select()`, luego `selrecord()` y `selwakeup()`, y después `kqueue()`. Las secciones posteriores añaden señales, colas de eventos y el diseño combinado.

### Un modelo mental para el resto del capítulo

Antes de continuar, fijemos un modelo mental que guíe el resto del capítulo. Todo driver asíncrono tiene tres tipos de rutas de código.

La primera es la ruta del productor. Aquí es donde el driver se entera de que algo ha ocurrido. En hardware, es el manejador de interrupciones. Para un pseudodispositivo como `evdemo`, es el código que simula el evento. La labor del productor es actualizar el estado interno del driver de modo que un lector que consultara en ese momento viera el nuevo evento.

La segunda es la ruta del que espera. Aquí es donde un llamante del espacio de usuario registra su interés. El thread del llamante entra en el kernel a través de una llamada al sistema (`poll`, `select`, `kevent` o `ioctl(FIOASYNC)`), el kernel despacha a nuestro callback `d_poll` o `d_kqfilter`, y registramos el interés del llamante de forma que el productor pueda encontrarlo más adelante.

La tercera es la ruta de entrega. Aquí es donde el productor notifica a quienes esperan. El productor acaba de actualizar el estado. Llama a `selwakeup()`, `KNOTE_LOCKED()`, `pgsigio()` o alguna combinación de ellos, y esas llamadas despiertan a los threads en espera, que luego típicamente llaman a `read()` para recoger los datos reales.

Este modelo de tres rutas es el marco desde el que abordaremos cada mecanismo. Cuando estudiemos `poll()`, nos preguntaremos: ¿qué hace el productor, qué registra el que espera y cómo es la entrega? Cuando estudiemos `kqueue()`, haremos las mismas tres preguntas. Cuando estudiemos `SIGIO`, las mismas tres preguntas. Los mecanismos difieren en sus detalles, pero todos encajan en la misma forma, y conocer esa forma hace que cada uno sea más fácil de aprender.

Con el modelo mental establecido, examinemos `poll(2)` y `select(2)`, los más antiguos y portables de los tres mecanismos.

## 2. Introducción a poll() y select()

Las llamadas al sistema `poll(2)` y `select(2)` son la respuesta original de UNIX a la pregunta «¿cómo espero en varios descriptores de archivo a la vez?». Llevan décadas en UNIX, funcionan en todas las plataformas relevantes y siguen siendo la forma más portable para que un programa de espacio de usuario vigile varios dispositivos, sockets o pipes en un único bucle.

Comparten la misma abstracción subyacente. Un programa pasa un conjunto de descriptores de archivo y una máscara de eventos que le interesan: legible, escribible o excepcional. El kernel examina cada descriptor, pregunta a su driver o subsistema si el evento está listo y, si ninguno lo está, pone el thread llamante a dormir hasta que alguno lo esté o expire un tiempo de espera. Cuando se despierta, el kernel devuelve qué descriptores están ahora activos y el programa puede atenderlos.

Desde el punto de vista del driver, tanto `poll` como `select` desembocan en el mismo callback `d_poll` de un `cdev`. Si el programa de espacio de usuario usó `poll(2)` o `select(2)` es invisible para el driver. Respondemos a una sola pregunta: dado este conjunto de eventos en los que el llamante está interesado, ¿cuáles están listos ahora mismo? Si ninguno está listo, también registramos al llamante para poder despertarlo cuando algo lo esté.

Ese doble papel (responder ahora, registrar para más tarde) es el núcleo del contrato de `d_poll`. El driver debe responder al estado actual inmediatamente y no debe olvidar al que espera si la respuesta fue «nada». Equivocarse en cualquiera de las dos mitades produce los dos bugs clásicos de poll. Si el driver informa de «no listo» cuando los datos en realidad sí lo están, el llamante se duerme y no despierta nunca, porque no ocurrirá ningún evento adicional que desencadene una activación. Si el driver no registra al que espera cuando no hay nada listo, el llamante tampoco despertará nunca, porque el driver nunca sabrá a quién despertar cuando los datos finalmente lleguen. Ambos bugs producen el mismo síntoma (un proceso colgado) y ambos son consecuencia de no implementar exactamente el patrón correcto.

### Qué espera el espacio de usuario de poll() y select()

Antes de implementar `d_poll`, conviene saber exactamente qué está haciendo el llamante del espacio de usuario. El código de usuario tiene un aspecto similar al siguiente:

```c
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

struct pollfd pfd[1];
int fd = open("/dev/evdemo", O_RDONLY);

pfd[0].fd = fd;
pfd[0].events = POLLIN;
pfd[0].revents = 0;

int r = poll(pfd, 1, 5000);   /* wait up to 5 seconds */
if (r > 0 && (pfd[0].revents & POLLIN)) {
    /* data is ready; do a read() now */
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf));
    /* ... */
}
```

El usuario pasa un array de `struct pollfd`, cada elemento con una máscara `events` que indica qué eventos le interesan. El kernel responde escribiendo en el campo `revents` los eventos que están realmente listos. El tercer argumento es un tiempo de espera en milisegundos: `-1` significa «esperar indefinidamente» y `0` significa «no bloquearse en absoluto, simplemente consultar el estado».

`select(2)` hace lo mismo con una API ligeramente diferente: tres mapas de bits `fd_set` para descriptores legibles, escribibles y excepcionales, y un tiempo de espera como `struct timeval`. Dentro del kernel, ambas llamadas se normalizan en la misma operación sobre cada descriptor implicado, que termina en nuestro callback `d_poll`.

El llamador espera la siguiente semántica:

Si alguno de los eventos solicitados está listo en ese momento, la llamada debe retornar de inmediato con los eventos listos indicados.

Si ninguno de los eventos solicitados está listo y el tiempo de espera no ha vencido, la llamada debe bloquearse hasta que alguno de los eventos esté listo o expire el tiempo de espera.

Si el descriptor se cierra o se vuelve inválido durante la llamada, el kernel devuelve `POLLNVAL`, `POLLHUP` o `POLLERR` según corresponda.

Los bits de la máscara de eventos con los que un driver trabaja habitualmente son los siguientes:

`POLLIN` y `POLLRDNORM`, ambos con el significado de «hay datos disponibles para leer». FreeBSD define `POLLRDNORM` como distinto de `POLLIN`, pero en la mayor parte del código de drivers los tratamos juntos, porque los programas suelen pedir uno u otro y esperan que cualquiera de los dos funcione.

`POLLOUT` y `POLLWRNORM`, ambos con el significado de «el dispositivo tiene espacio en el buffer para aceptar una escritura». FreeBSD define `POLLWRNORM` como idéntico a `POLLOUT`, por lo que en la práctica son el mismo bit.

`POLLPRI`, que indica que hay datos fuera de banda o de prioridad disponibles. La mayoría de los drivers de caracteres no tienen concepto de prioridad y no tocan este bit.

`POLLERR`, que indica que ha ocurrido un error en el dispositivo. El driver normalmente activa este bit cuando algo ha fallado y el dispositivo no puede recuperarse.

`POLLHUP`, que indica que el extremo remoto ha cerrado la conexión. El master de un pty lo ve cuando el slave cierra su extremo. El lector de una pipe lo ve cuando el escritor cierra el suyo. Un driver de dispositivo normalmente activa este bit durante el proceso de detach, o cuando un servicio en capas se ha desconectado.

`POLLNVAL`, que indica que la solicitud no es válida. El driver suele dejar este bit en manos del framework del kernel, que lo activa cuando el descriptor no es válido o cuando el driver no tiene `d_poll`.

La combinación de `POLLHUP` y `POLLIN` merece una mención especial: cuando un dispositivo se cierra y tenía datos en el buffer, los lectores deben ver `POLLHUP` junto con `POLLIN`, porque los datos almacenados en el buffer aún pueden leerse aunque no vayan a llegar más. Los programas de userland bien escritos gestionan este caso de forma explícita.

### El callback d_poll

Ahora podemos ver el propio callback `d_poll`. Su firma, definida en `/usr/src/sys/sys/conf.h`, es:

```c
typedef int d_poll_t(struct cdev *dev, int events, struct thread *td);
```

El argumento `dev` es nuestro `cdev`, del que recuperamos el softc mediante `dev->si_drv1`. El argumento `events` es la máscara de eventos en los que está interesado el llamador. El argumento `td` es el thread que realiza la llamada, que necesitamos pasar a `selrecord()` para que el kernel pueda asociar los futuros wakeups al waiter correcto. El valor de retorno es el subconjunto de `events` que están listos en este momento.

Una implementación esquelética tiene este aspecto:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);

    if (events & (POLLIN | POLLRDNORM)) {
        if (evdemo_event_ready(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }

    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);

    mtx_unlock(&sc->sc_mtx);
    return (revents);
}
```

Este es el patrón clásico. Vamos a recorrerlo línea a línea.

Tomamos el mutex del softc porque estamos a punto de examinar el estado interno del driver, y ningún otro thread debería estar modificándolo mientras decidimos si un evento está listo. Mantener el lock mientras llamamos a `selrecord()` es también lo que cierra la condición de carrera entre la respuesta y el registro, como veremos en un momento.

Examinamos cada tipo de evento que le interesa al llamador. Para los eventos de lectura, preguntamos al driver si hay datos listos. Si los hay, añadimos los bits correspondientes a `revents`. Si no los hay, llamamos a `selrecord()` para registrar este thread como waiter en el selinfo `sc_rsel`. Ese selinfo vive en el softc, es compartido por todos los potenciales waiters y es lo que pasaremos más adelante a `selwakeup()` cuando lleguen datos.

Para los eventos de escritura, en este ejemplo no disponemos de un buffer interno que pueda llenarse, así que siempre informamos del dispositivo como listo para escribir. Muchos drivers pertenecen a esta categoría: las escrituras siempre caben. Los drivers con buffers de tamaño fijo deben comprobar el estado del buffer de la misma forma que comprueban el estado de lectura, y solo informar `POLLOUT` cuando haya espacio disponible.

Liberamos el lock y devolvemos la máscara de eventos listos.

Hay tres aspectos de este patrón que merecen especial atención.

En primer lugar, devolvemos inmediatamente `revents` en todos los casos. El callback `d_poll` no duerme. Si nada está listo, registramos un waiter y devolvemos cero. El framework genérico de poll del kernel se encarga del bloqueo real: una vez que `d_poll` devuelve el control, el kernel duerme el thread de forma atómica si ningún descriptor de archivo ha devuelto eventos. El autor del driver no ve este sueño; es gestionado íntegramente por la lógica de despacho de poll del kernel.

En segundo lugar, debemos llamar a `selrecord()` solo para los tipos de evento que no están listos en este momento. Si un evento está listo y también llamamos a `selrecord()`, no rompemos nada (el framework lo gestiona), pero resulta ineficiente: el thread no va a dormirse, así que registrarlo carece de sentido. El patrón "comprueba, y si no está listo regístralo" mantiene el trabajo proporcional a las necesidades.

En tercer lugar, el lock que mantenemos durante la comprobación y la llamada a `selrecord()` es el mismo lock que tomaremos en la ruta del productor cuando llamemos a `selwakeup()`. Esto es lo que previene la condición de carrera del wakeup perdido: si el productor actúa después de que comprobemos el estado pero antes de que registremos el waiter, el productor no puede entregar el wakeup hasta que `selrecord()` haya concluido, de modo que el wakeup nos encontrará. Veremos esto con detalle en la sección 3.

### Registro del método d_poll en el cdevsw

Para que nuestro driver responda a las llamadas `poll()`, rellenamos el campo `d_poll` del `struct cdevsw` que pasamos a `make_dev()` o `make_dev_s()`:

```c
static struct cdevsw evdemo_cdevsw = {
    .d_version = D_VERSION,
    .d_name    = "evdemo",
    .d_open    = evdemo_open,
    .d_close   = evdemo_close,
    .d_read    = evdemo_read,
    .d_write   = evdemo_write,
    .d_ioctl   = evdemo_ioctl,
    .d_poll    = evdemo_poll,
};
```

Si no establecemos `d_poll`, el kernel proporciona un valor por defecto. En `/usr/src/sys/kern/kern_conf.c`, el valor por defecto es `no_poll`, que llama a `poll_no_poll()`. Ese valor por defecto devuelve los bits estándar de lectura y escritura salvo que el llamador haya pedido algo inusual, en cuyo caso devuelve `POLLNVAL`. Este comportamiento tiene sentido para dispositivos que siempre están listos, como `/dev/null` y `/dev/zero`, pero casi nunca es lo que se desea para un dispositivo orientado a eventos. Para cualquier driver con semántica asíncrona real, querrás implementar `d_poll` tú mismo.

### Aspecto de los drivers reales

Veamos dos implementaciones reales, porque el patrón quedará más claro cuando lo veas en código de producción.

Abre `/usr/src/sys/net/if_tuntap.c` y busca la función `tunpoll`. Es lo suficientemente corta como para citarla aquí:

```c
static int
tunpoll(struct cdev *dev, int events, struct thread *td)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet    *ifp = TUN2IFP(tp);
    int     revents = 0;

    if (events & (POLLIN | POLLRDNORM)) {
        IFQ_LOCK(&ifp->if_snd);
        if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
            revents |= events & (POLLIN | POLLRDNORM);
        } else {
            selrecord(td, &tp->tun_rsel);
        }
        IFQ_UNLOCK(&ifp->if_snd);
    }
    revents |= events & (POLLOUT | POLLWRNORM);
    return (revents);
}
```

Esto es prácticamente nuestro esqueleto al pie de la letra, con la cola de paquetes salientes del driver `tun` como fuente de datos y el selinfo `tun_rsel` como punto de espera. El lock aquí es `IFQ_LOCK`, el lock de la cola, que el productor también toma antes de modificar la cola y llamar a `selwakeuppri()`. Ese emparejamiento de locks es lo que hace que el diseño sea correcto.

Ahora abre `/usr/src/sys/dev/evdev/cdev.c` y busca `evdev_poll`. Este es un ejemplo ligeramente más largo y más instructivo porque gestiona explícitamente un dispositivo revocado:

```c
static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdev_client *client;
    int ret;
    int revents = 0;

    ret = devfs_get_cdevpriv((void **)&client);
    if (ret != 0)
        return (POLLNVAL);

    if (client->ec_revoked)
        return (POLLHUP);

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

Observa dos comportamientos adicionales que no teníamos en el esqueleto.

Cuando el cliente ha sido revocado (lo que ocurre cuando el dispositivo se está desconectando mientras el cliente todavía tiene abierto el descriptor de archivo), la función devuelve `POLLHUP` para que el programa en espacio de usuario sepa que debe rendirse. Este es el manejo correcto del caso de detach. Nuestro esqueleto todavía no hace esto, pero el `evdemo` final refactorizado sí lo hará.

El driver establece un flag, `ec_selected`, para recordar que se ha registrado un waiter. Esto permite al productor evitar llamar a `selwakeup()` para clientes que nunca han realizado un poll, lo que constituye una pequeña optimización. La mayoría de los drivers omiten esta optimización y simplemente llaman a `selwakeup()` cada vez, lo cual es más sencillo y sigue siendo correcto.

### Lo que ve el usuario

En el lado de userland, al llamador no le importa qué implementación hemos elegido. Llama a `poll()` con un tiempo de espera y ve el resultado. La primera llamada devuelve cero si nada está listo y el tiempo de espera expira, o un número positivo de descriptores listos en caso contrario. La segunda llamada examina la máscara de bits `revents` y despacha al manejo adecuado.

Esta es la separación limpia que logra el I/O asíncrono. El programa de usuario no sabe ni le importa nada sobre `selinfo` o `knlist`. Solo sabe que preguntó al kernel "¿está esto listo ya?" y obtuvo una respuesta. La tarea del driver es hacer que esa respuesta sea veraz y garantizar que el próximo evento relevante despierte al waiter.

### Cerrando la sección 2

Ya tenemos la visión de userland sobre poll y select, la firma del kernel de `d_poll` y una primera implementación esquelética que registra waiters e informa sobre eventos de lectura. Pero el esqueleto todavía está incompleto. Hemos usado `selrecord()` sin explicar qué hace realmente con el `struct selinfo`, y aún no hemos visto la llamada correspondiente a `selwakeup()` que produce la notificación. Ese es el tema de la siguiente sección, y es donde residen los sutiles problemas de corrección del I/O asíncrono basado en poll.

## 3. Uso de selwakeup() y selrecord()

`selrecord()` y `selwakeup()` son las dos mitades del protocolo clásico de poll-wait. Están presentes en los kernels BSD desde la introducción original de `select(2)` en 4.2BSD y siguen siendo la forma canónica de implementar wait/wakeup para `poll(2)` y `select(2)` en los drivers de FreeBSD. El par es simple en esquema pero sutil en los detalles, y la mayoría de los bugs interesantes en drivers basados en poll provienen de equivocarse en esa sutileza.

Esta sección te guía a través de la maquinaria de selinfo paso a paso. Primero vemos qué contiene realmente `struct selinfo`. Luego examinamos exactamente qué hace `selrecord()` y qué no hace. A continuación vemos `selwakeup()` y sus funciones asociadas. Finalmente examinamos la condición de carrera clásica del wakeup perdido, la disciplina de locking que la previene y las técnicas de diagnóstico que puedes usar para confirmar que tu driver está haciendo lo correcto.

### struct selinfo

Abre `/usr/src/sys/sys/selinfo.h` y examina la definición:

```c
struct selinfo {
    struct selfdlist    si_tdlist;  /* List of sleeping threads. */
    struct knlist       si_note;    /* kernel note list */
    struct mtx          *si_mtx;    /* Lock for tdlist. */
};

#define SEL_WAITING(si)    (!TAILQ_EMPTY(&(si)->si_tdlist))
```

Solo tres campos. `si_tdlist` es una lista de threads que están durmiendo actualmente en este selinfo porque llamaron a `selrecord()` y su llamada a `poll()` o `select()` decidió bloquearse. `si_note` es un `knlist`, que encontraremos en la sección 4 cuando implementemos el soporte para `kqueue`; permite que el mismo selinfo sirva tanto a los waiters de `poll()` como a los de `kqueue()`. `si_mtx` es el lock que protege la lista.

La macro `SEL_WAITING()` te indica si hay algún thread actualmente aparcado en este selinfo. Los drivers la usan ocasionalmente para decidir si vale la pena llamar a `selwakeup()`, aunque la propia rutina de wakeup es lo suficientemente barata como para que la comprobación resulte habitualmente innecesaria.

Dos hábitos importantes para `struct selinfo`:

En primer lugar, el driver debe inicializar a cero el selinfo antes del primer uso. La forma habitual es incrustarlo en un softc que se inicializa a cero con `malloc(..., M_ZERO)`, pero si asignas un selinfo por separado debes ponerlo a cero con `bzero()` o equivalente. Un selinfo no inicializado hará que el kernel entre en pánico la primera vez que se llame a `selrecord()` sobre él.

En segundo lugar, el driver debe vaciar los waiters del selinfo antes de destruirlo. La secuencia canónica en el momento del detach es `seldrain(&sc->sc_rsel)` seguido de `knlist_destroy(&sc->sc_rsel.si_note)`. La llamada a `seldrain()` despierta a todos los waiters actualmente aparcados para que vean que el descriptor se ha vuelto inválido en lugar de bloquearse para siempre. La llamada a `knlist_destroy()` limpia la lista de knotes para los waiters de kqueue, que implementaremos en la siguiente sección.

### Qué hace selrecord()

`selrecord()` se llama desde `d_poll` cuando el driver decide que el evento actual no está listo y el thread necesitará esperar. Su firma:

```c
void selrecord(struct thread *selector, struct selinfo *sip);
```

La implementación vive en `/usr/src/sys/kern/sys_generic.c`. La esencia es suficientemente concisa como para resumirla:

1. La función comprueba que el thread se encuentre en un contexto de poll válido.
2. Toma uno de los descriptores `selfd` preasignados por thread que están adjuntos a la estructura `seltd` del thread.
3. Enlaza ese descriptor en la lista de esperas activas del thread y en el `si_tdlist` del `selinfo`.
4. Recuerda el mutex del selinfo en el descriptor, de modo que la ruta del wakeup sepa qué lock tomar.

Lo más importante que hay que entender es qué no hace `selrecord()`. No duerme el thread. No bloquea. No transiciona el thread a ningún estado bloqueado. Simplemente registra el hecho de que este thread tiene interés en este selinfo, de modo que más tarde, cuando el código de despacho de poll del kernel decida bloquear el thread (si ningún descriptor ha devuelto eventos), sepa en qué selinfos está aparcado el thread.

Una vez que todos los callbacks `d_poll` de un thread han devuelto el control, el código de despacho de poll examina los resultados. Si algún descriptor de archivo devolvió eventos, la llamada retorna inmediatamente sin bloquearse. Si ninguno lo hizo, el thread se duerme. El sueño se produce sobre una variable de condición por thread dentro de `struct seltd`, y el wakeup se entrega a través de esa variable de condición. El papel del selinfo es enlazar el `seltd` del thread con todos los drivers relevantes para que cada driver pueda encontrar el thread más adelante.

Esta separación entre "registrar" y "dormir" es lo que permite que una única llamada a `poll()` monitorice múltiples descriptores de archivo. El thread se registra con cada selinfo de cada driver que le interesa. Cuando cualquiera de ellos actúa, el wakeup encuentra el thread a través de su `seltd` y sale de nuevo hacia el despacho de poll, que entonces examina todos los descriptores de archivo registrados para ver cuáles están listos.

### Qué hace selwakeup()

`selwakeup()` se llama desde la ruta del productor cuando el estado del driver cambia de una forma que podría satisfacer a un waiter. Su firma:

```c
void selwakeup(struct selinfo *sip);
```

Existe también una variante llamada `selwakeuppri()` que toma un argumento de prioridad, útil cuando el driver quiere controlar la prioridad con la que el thread despertado reanuda su ejecución. En la práctica, `selwakeup()` es adecuado para casi cualquier driver; `selwakeuppri()` se usa en algunos subsistemas que quieren priorizar la latencia a costa de la equidad.

La implementación recorre el `si_tdlist` del selinfo y señala la variable de condición de cada thread aparcado. También recorre la lista `si_note` del selinfo y entrega notificaciones al estilo kqueue a todos los knotes adjuntos, de modo que una única llamada a `selwakeup()` sirve tanto a los waiters de poll como a los de kqueue.

Es fundamental que `selwakeup()` se llame solo después de que el estado interno del driver haya sido actualizado para reflejar el nuevo evento. Si llamas a `selwakeup()` antes de que los datos sean visibles, el thread despertado recorre de nuevo `d_poll`, comprueba que no hay nada preparado (porque el productor todavía no lo ha hecho visible), vuelve a registrarse y se duerme. Cuando el productor actualiza finalmente el estado, nadie es despertado, ya que el nuevo registro ocurrió después del wakeup. El driver tiene entonces que esperar al siguiente evento para liberar al proceso en espera, y puede que ese evento nunca llegue.

El orden correcto es siempre: actualizar el estado y luego despertar. Nunca al revés.

### La condición de carrera del wakeup perdido

El bug más famoso en los drivers basados en poll es el wakeup perdido. Tiene el siguiente aspecto:

```c
/* Producer thread */
append_event(sc, ev);              /* update state */
selwakeup(&sc->sc_rsel);           /* wake waiters */

/* Consumer thread, in d_poll */
if (events & POLLIN) {
    if (event_ready(sc))
        revents |= POLLIN;
    else
        selrecord(td, &sc->sc_rsel);
}
return (revents);
```

Si el productor se ejecuta entre la comprobación de `event_ready()` del consumidor y su llamada a `selrecord()`, el wakeup se pierde. El consumidor no vio ningún evento, el productor publicó un evento y llamó a `selwakeup()` sobre una lista de espera vacía, y el consumidor se registró después. Nadie volverá a llamar a `selwakeup()` hasta que llegue el siguiente evento. El consumidor duerme hasta ese siguiente evento, aunque ya haya uno listo.

Esta es la clásica condición de carrera TOCTOU entre la comprobación y el registro. La corrección estándar consiste en usar un único mutex para serializar la comprobación, el registro y el wakeup:

```c
/* Producer thread */
mtx_lock(&sc->sc_mtx);
append_event(sc, ev);
mtx_unlock(&sc->sc_mtx);
selwakeup(&sc->sc_rsel);

/* Consumer thread, in d_poll */
mtx_lock(&sc->sc_mtx);
if (events & POLLIN) {
    if (event_ready(sc))
        revents |= POLLIN;
    else
        selrecord(td, &sc->sc_rsel);
}
mtx_unlock(&sc->sc_mtx);
return (revents);
```

Ahora la comprobación y el registro son atómicos con respecto al productor. Si el productor actualiza el estado antes de que el consumidor compruebe, el consumidor ve el evento y devuelve `POLLIN` sin registrarse. Si el productor está a punto de actualizar el estado mientras el consumidor se encuentra en la sección crítica, el productor tiene que esperar a que el consumidor termine. En ambos casos, el wakeup llega al consumidor.

La sutileza importante es que `selwakeup()` se llama fuera del mutex del softc. Este es el patrón estándar en el kernel de FreeBSD: actualiza el estado bajo el lock, suelta el lock y entrega la notificación. `selwakeup()` es segura de llamar desde muchos contextos, pero toma el mutex interno del selinfo, y no queremos anidar ese lock dentro de un lock de driver arbitrario. En la práctica, la regla es: mantén el lock del softc durante la actualización de estado, suéltalo y llama a `selwakeup()`.

Verás este patrón en los drivers de FreeBSD. En `if_tuntap.c` la ruta del productor llama a `selwakeuppri()` desde fuera de cualquier lock de driver. En `evdev/cdev.c` ocurre lo mismo. El productor actualiza el estado bajo su lock interno, lo suelta y luego emite el wakeup. El consumidor, en `d_poll`, toma el mismo lock durante la comprobación y el `selrecord()`. Esa disciplina elimina la condición de carrera del wakeup perdido.

### Reflexionando sobre el lock

¿Por qué funciona esto? Porque el lock serializa dos operaciones concretas: la actualización de estado del productor y la comprobación más el registro del consumidor. La llamada a `selwakeup()` y el posterior sueño del thread están fuera del lock, pero eso está bien, porque la semántica de las variables de condición del mecanismo subyacente se encarga de esa condición de carrera por separado.

El argumento con más detalle es el siguiente. Supón que el consumidor adquiere el lock primero. Comprueba el estado, no ve nada, llama a `selrecord()` para registrarse y suelta el lock. Más tarde el productor adquiere el lock, actualiza el estado, lo suelta y llama a `selwakeup()`. El consumidor ya está registrado, por lo que el wakeup lo encuentra. Correcto.

Ahora supón que el productor adquiere el lock primero. Actualiza el estado, suelta el lock y llama a `selwakeup()`. El consumidor no estaba registrado todavía, así que el wakeup no encuentra ningún waiter. Eso está bien porque el consumidor aún no ha llegado al punto en el que habría dormido; el consumidor está a punto de adquirir el lock. Cuando el consumidor adquiere el lock, comprueba el estado, ve el evento (porque el productor ya lo ha actualizado) y devuelve `POLLIN` sin llamar a `selrecord()`. El consumidor recibe la notificación correctamente.

El tercer caso es el delicado. El consumidor acaba de comprobar el estado (bajo el lock) y está a punto de llamar a `selrecord()`, pero en realidad, como el lock está tomado durante todo ese tiempo, este caso no puede producirse. El productor no puede actualizar el estado hasta que el consumidor suelte el lock, momento en el que el consumidor ya se ha registrado.

Así que la disciplina de lock es: mantén siempre el lock durante la comprobación y el registro del consumidor, y mantén siempre el lock durante la actualización de estado del productor. La llamada a `selwakeup()` ocurre fuera del lock porque tiene su propia sincronización interna.

### Errores comunes

Merece la pena señalar explícitamente algunos errores habituales.

Llamar a `selwakeup()` dentro del lock de actualización de estado es incorrecto en la mayoría de los casos, porque `selwakeup()` puede necesitar tomar otros locks (el mutex del selinfo, el lock de la cola selinfo del thread). Hacerlo desde dentro del mutex del softc crea una oportunidad de ordenación de locks fácil de cometer mal. La regla general es: actualiza bajo el lock, suéltalo y llama a `selwakeup()`.

Olvidarse de despertar a todos los selinfos interesados es el otro error común. Si el driver tiene selinfos separados para lectura y escritura (uno para los waiters de `POLLIN` y otro para los de `POLLOUT`), debe despertar el correcto cuando cambia el estado. Despertar el incorrecto significa que el waiter real duerme indefinidamente.

Llamar a `selrecord()` sin mantener ningún lock produce una ventana temporal en la que el evento puede llegar sin que se entregue el wakeup. Esta es la condición de carrera que acabamos de analizar, y la corrección es siempre la misma: mantén el lock.

Llamar a `selrecord()` siempre, incluso cuando los datos están listos, no es un bug de corrección, pero supone una carga inútil sobre el pool `selfd` por thread. Si los datos están listos, el thread no va a dormir, así que registrarlo es trabajo desperdiciado. El patrón "comprueba; si está listo, devuelve; si no, regístrate" es el correcto.

Llamar a `selwakeup()` sobre un selinfo destruido es un crash esperando a ocurrir. La ruta de detach debe llamar a `seldrain()` antes de liberar el selinfo o el softc que lo contiene.

### Técnicas de diagnóstico

Cuando el soporte de poll de un driver no funciona, hay algunas herramientas que te ayudan a aislar el problema.

La primera herramienta es `top(1)`. Carga el driver, abre un descriptor en un programa de userland y haz que el programa llame a `poll()` con un timeout largo. Busca el programa en `top -H` y comprueba la columna WCHAN. Si el poll funciona correctamente, el canal de espera del thread será `select` o algo similar. Si el thread está en otro estado (ejecutando, listo para ejecutar, sueño corto), es posible que la llamada a poll haya retornado prematuramente o que el programa esté haciendo spinning.

La segunda herramienta son contadores en el driver. Añade un contador para cada llamada a `selrecord()`, uno para cada llamada a `selwakeup()` y uno para cada vez que `d_poll` devuelve una máscara de listo. Después de una prueba, imprime estos contadores a través de `sysctl`. Si `selrecord()` se dispara pero `selwakeup()` nunca lo hace, la ruta del productor nunca se activa. Si `selwakeup()` se dispara pero el programa sigue dormido, probablemente tengas un wakeup perdido porque la actualización de estado y el registro ocurren fuera del lock.

La tercera herramienta es `ktrace(1)` y `kdump(1)`. Ejecuta el programa de prueba bajo `ktrace`, y el volcado mostrará cada llamada al sistema y su temporización. Un programa que llama a `poll()` y se bloquea aparecerá como una entrada `RET poll` después del wakeup, y la marca de tiempo te dirá cuándo llegó realmente el wakeup. Si el evento del productor ocurrió en el instante T y el wakeup llegó segundos después, tienes un bug.

La cuarta herramienta es DTrace, que puede instrumentar el propio `selwakeup`. Un script que sondea `fbt:kernel:selwakeup:entry` e imprime el puntero al softc del driver que lo llama muestra cada wakeup del sistema. Si el wakeup de tu driver nunca se dispara, DTrace te lo dirá en cuestión de milisegundos.

### Cerrando el ciclo: evdemo con soporte para poll

Juntando todas las piezas, aquí está el código adicional mínimo que nuestro driver `evdemo` necesita para soportar `poll()` correctamente:

```c
/* In the softc */
struct evdemo_softc {
    /* ... existing fields ... */
    struct selinfo sc_rsel;  /* read selectors */
};

/* At attach */
knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);

/* In d_poll */
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (sc->sc_nevents > 0)
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}

/* In the producer path (for evdemo, this is the event injection
 * routine triggered from a callout or ioctl) */
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);
}

/* At detach */
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

Observa que llamamos a `knlist_init_mtx()` en el knlist embebido `si_note` del selinfo aunque todavía no estemos implementando kqueue. Esto nos cuesta casi nada y hace que el selinfo sea compatible con el soporte de kqueue que añadiremos en la Sección 4. Si no inicializas previamente `si_note`, la primera llamada a `selwakeup()` que intente recorrer el knlist provocará un crash. Muchos drivers inicializan el knlist durante el attach por costumbre.

Observa también que el helper `evdemo_post_event` mantiene el mutex del softc mientras actualiza el contador de eventos, suelta el mutex y luego llama a `selwakeup()`. Ese es el patrón estándar del productor, y es el que reutilizaremos a lo largo del resto del capítulo.

### Cerrando la Sección 3

En este punto tienes todas las piezas conceptuales y prácticas de la E/S asíncrona basada en poll. Conoces el contrato, las estructuras del kernel, la disciplina de locking correcta y los modos de fallo habituales. Puedes tomar un driver bloqueante existente, añadir soporte para `d_poll` y hacer que se comporte correctamente bajo `poll(2)` y `select(2)`.

El problema es que `poll(2)` y `select(2)` tienen limitaciones de escalabilidad bien conocidas. Cada llamada vuelve a declarar el conjunto completo de descriptores en los que el llamador está interesado, lo que es O(N) por llamada. Para programas que observan miles de descriptores, esta sobrecarga domina. FreeBSD ha ofrecido un mecanismo mejor desde finales de los años noventa, concretamente `kqueue(2)`, y ese es el tema de la siguiente sección.

## 4. Soporte para kqueue y EVFILT_READ/EVFILT_WRITE

`kqueue(2)` es la facilidad de notificación de eventos escalable de FreeBSD. A diferencia de `poll(2)` y `select(2)`, que requieren que el programa de userland vuelva a declarar sus intereses en cada llamada, `kqueue(2)` permite al programa registrar los intereses una vez y luego preguntar solo por los eventos que realmente se han producido. Para un programa que observa diez mil descriptores de archivo donde solo unos pocos están activos, esta es la diferencia entre un programa rápido e interactivo y uno lento y sobrecargado.

`kqueue` también es más expresivo que `poll`. Más allá de los filtros básicos de legible y escribible, ofrece filtros para señales, temporizadores, eventos del sistema de archivos, eventos del ciclo de vida de procesos, eventos definidos por el usuario y varias otras categorías. Un driver que solo quiere participar en las notificaciones clásicas de legible y escribible encaja limpiamente en el framework; las características más amplias están disponibles si se necesitan.

Desde el punto de vista del driver, el soporte de kqueue añade un callback a `cdevsw`, `d_kqfilter`, y un conjunto de operaciones de filtro, un `struct filterops`, que proporciona las funciones de ciclo de vida y entrega de eventos para cada tipo de filtro. Todo el mecanismo reutiliza el `struct selinfo` que conocimos en la Sección 3, por lo que los drivers que ya soportan `poll()` pueden añadir soporte para kqueue escribiendo unas cien líneas de código adicional y llamando a un puñado de nuevas APIs.

### Qué aspecto tiene kqueue para el userland

Antes de implementar el lado del driver, veamos cómo es el programa de usuario. Un llamador abre un `kqueue`, registra interés en un descriptor de archivo y luego recoge eventos:

```c
#include <sys/event.h>

int kq = kqueue();
int fd = open("/dev/evdemo", O_RDONLY);

struct kevent change;
EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
kevent(kq, &change, 1, NULL, 0, NULL);

for (;;) {
    struct kevent ev;
    int n = kevent(kq, NULL, 0, &ev, 1, NULL);
    if (n > 0 && ev.filter == EVFILT_READ) {
        char buf[256];
        ssize_t r = read(fd, buf, sizeof(buf));
        /* ... */
    }
}
```

La macro `EV_SET` construye un `struct kevent` que describe el interés: "observa el descriptor de archivo `fd` para eventos `EVFILT_READ`, usando semántica de disparo por flanco (`EV_CLEAR`), y mantenlo activo (`EV_ADD`)." La primera llamada a `kevent()` registra ese interés. El bucle llama a `kevent()` en modo bloqueante, pide el siguiente evento y lo gestiona cuando llega.

El driver nunca ve el descriptor de archivo `kqueue` ni la estructura `kevent` directamente. Solo ve el `struct knote` por interés y su `struct filterops` asociado. El registro fluye a través del framework hasta nuestro callback `d_kqfilter`, que elige las operaciones de filtro correctas y adjunta el knote a nuestro softc. La entrega fluye a través de las llamadas a `KNOTE_LOCKED()` en la ruta del productor, que recorre nuestra lista de knotes y notifica a cada kqueue adjunto del evento listo.

### Las estructuras de datos

Dos estructuras importan en el lado del driver: `struct filterops` y `struct knlist`.

`struct filterops`, definida en `/usr/src/sys/sys/event.h`, contiene las funciones de ciclo de vida por filtro:

```c
struct filterops {
    int     f_isfd;
    int     (*f_attach)(struct knote *kn);
    void    (*f_detach)(struct knote *kn);
    int     (*f_event)(struct knote *kn, long hint);
    void    (*f_touch)(struct knote *kn, struct kevent *kev, u_long type);
    int     (*f_userdump)(struct proc *p, struct knote *kn,
                          struct kinfo_knote *kin);
};
```

Los campos que nos interesan para un driver son:

`f_isfd` es 1 si el filtro está adjunto a un descriptor de archivo. Casi todos los filtros de driver tienen este valor a 1. Un filtro que observa algo no ligado a un fd (como `EVFILT_TIMER`) lo pondría a 0.

`f_attach` se llama cuando se está adjuntando un knote a un interés recién registrado. Muchos drivers dejan esto como `NULL` porque todo el trabajo de adjunción ocurre en el propio `d_kqfilter`.

`f_detach` se llama cuando se está eliminando un knote. El driver lo usa para dar de baja el knote de su lista interna de knotes.

`f_event` se invoca para evaluar si la condición del filtro está actualmente satisfecha. Devuelve un valor distinto de cero si es así, y cero si no lo es. Es el equivalente en kqueue de la comprobación de estado que realiza `d_poll`.

`f_touch` se utiliza cuando el filtro admite actualizaciones de tipo `EV_ADD`/`EV_DELETE` que no deben tratarse como un registro completo nuevo. La mayoría de los drivers lo dejan como `NULL` y aceptan el comportamiento predeterminado.

`f_userdump` se usa para la introspección del kernel y puede dejarse como `NULL` en el código del driver.

`struct knlist`, definida en la misma cabecera, mantiene una lista de knotes asociados a un objeto concreto. Contiene punteros a las operaciones de lock del objeto para que el framework de kqueue pueda adquirir y liberar el lock adecuado al entregar eventos:

```c
struct knlist {
    struct  klist   kl_list;
    void    (*kl_lock)(void *);
    void    (*kl_unlock)(void *);
    void    (*kl_assert_lock)(void *, int);
    void    *kl_lockarg;
    int     kl_autodestroy;
};
```

Los drivers raramente manipulan esta estructura directamente. El framework proporciona funciones auxiliares, empezando por `knlist_init_mtx()` para el caso habitual de un knlist protegido por un único mutex.

### Inicialización de una knlist

La forma más sencilla de inicializar una knlist es:

```c
knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
```

El primer argumento es la knlist que se va a inicializar. El segundo es el mutex del driver. El framework almacena el mutex y lo toma cuando es necesario para proteger la lista de knotes. La lista de knotes suele estar embebida en un `struct selinfo`, como vimos en la sección anterior; reutilizar el mismo selinfo tanto para los waiters de poll como para los de kqueue permite que una sola llamada a `selwakeup()` cubra ambos mecanismos.

Para un driver que ya pone a cero el softc mediante `M_ZERO`, la inicialización se reduce a esta única llamada durante el attach.

### El callback d_kqfilter

El callback `d_kqfilter` es el punto de entrada para el registro en kqueue. Su firma, en `/usr/src/sys/sys/conf.h`, es:

```c
typedef int d_kqfilter_t(struct cdev *dev, struct knote *kn);
```

El argumento `dev` es nuestro `cdev`. El argumento `kn` es el knote que se está registrando. El callback decide qué operaciones de filtro son aplicables, adjunta el knote a nuestra lista de knotes y devuelve cero en caso de éxito.

Una implementación mínima para un driver que soporta `EVFILT_READ`:

```c
static int
evdemo_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdemo_softc *sc = dev->si_drv1;

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdemo_read_filterops;
        kn->kn_hook = sc;
        knlist_add(&sc->sc_rsel.si_note, kn, 0);
        return (0);
    default:
        return (EINVAL);
    }
}
```

Repasemos esto paso a paso.

El `switch` sobre `kn->kn_filter` determina con qué tipo de filtro estamos tratando. Un driver que soporta únicamente `EVFILT_READ` devuelve `EINVAL` para cualquier otra cosa. Un driver que también soporta `EVFILT_WRITE` tiene un segundo caso que apunta a una estructura de operaciones de filtro diferente.

Asignamos `kn->kn_fop` a las operaciones de filtro correspondientes a este tipo de filtro. El framework de kqueue invoca estas operaciones a medida que avanza el ciclo de vida del knote.

Asignamos `kn->kn_hook` al softc. El knote dispone de este puntero genérico para uso exclusivo del driver. Nuestras funciones de filtro recuperarán el softc de `kn->kn_hook` cuando sean invocadas.

Llamamos a `knlist_add()` para enlazar el knote en nuestra lista de knotes. El tercer argumento, `islocked`, es cero aquí porque en este punto no tenemos el lock de la knlist. Si lo tuviéramos, pasaríamos 1.

Devolvemos cero para indicar éxito.

### La implementación de filterops

Las operaciones de filtro son donde reside el comportamiento específico de cada filtro. Para `EVFILT_READ` en `evdemo`, tienen este aspecto:

```c
static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;
    int ready;

    mtx_assert(&sc->sc_mtx, MA_OWNED);

    kn->kn_data = sc->sc_nevents;
    ready = (sc->sc_nevents > 0);

    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        ready = 1;
    }

    return (ready);
}

static void
evdemo_kqdetach(struct knote *kn)
{
    struct evdemo_softc *sc = kn->kn_hook;

    knlist_remove(&sc->sc_rsel.si_note, kn, 0);
}

static const struct filterops evdemo_read_filterops = {
    .f_isfd   = 1,
    .f_attach = NULL,
    .f_detach = evdemo_kqdetach,
    .f_event  = evdemo_kqread,
};
```

La función `f_event`, `evdemo_kqread`, se invoca cada vez que el framework quiere saber si el filtro está listo. Consulta el softc, informa del número de eventos disponibles en `kn->kn_data` (una convención de la que dependen los usuarios de kqueue para saber cuántos datos hay disponibles) y devuelve un valor distinto de cero si hay al menos un evento pendiente. También activa el flag `EV_EOF` cuando el dispositivo está en proceso de detach, lo que permite al espacio de usuario saber que no van a llegar más eventos.

Nótese la aserción de que el mutex del softc está tomado. El framework toma el lock de nuestra knlist, que le indicamos que es el mutex del softc mediante `knlist_init_mtx`. Dado que el callback f_event se invoca dentro de ese lock, podemos consultar `sc_nevents` y `sc_detaching` de forma segura.

La función `f_detach` elimina el knote de nuestra knlist cuando el espacio de usuario ya no tiene interés en este registro.

La constante `evdemo_read_filterops` es a lo que apuntaba `d_kqfilter` en la subsección anterior. `f_isfd = 1` le indica al framework que este filtro está vinculado a un descriptor de archivo, que es el valor correcto para cualquier filtro a nivel de driver.

### Entrega de eventos mediante KNOTE_LOCKED

En el lado productor, necesitamos notificar a los knotes registrados cuando cambia el estado del driver. La macro es `KNOTE_LOCKED()`, definida en `/usr/src/sys/sys/event.h`:

```c
#define KNOTE_LOCKED(list, hint)    knote(list, hint, KNF_LISTLOCKED)
```

Recibe un puntero a knlist y un hint. El hint se propaga al callback `f_event` de cada knote, dándole al productor una forma de pasar contexto (por ejemplo, un tipo de evento concreto) al filtro. La mayoría de los drivers pasan cero.

La variante `KNOTE_LOCKED` es la que debes usar cuando ya tienes el lock de la knlist. La variante `KNOTE_UNLOCKED` se usa cuando no lo tienes. Como el lock de la knlist suele ser el mutex del softc, y como el resto del camino del productor tiene ese lock de todas formas, `KNOTE_LOCKED` es la opción habitual.

Al añadirlo a nuestro camino del productor:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);
}
```

Ahora notificamos tanto a los waiters de kqueue como a los de poll desde el mismo productor. `KNOTE_LOCKED` dentro del mutex del softc recorre la lista de knotes y evalúa el `f_event` de cada uno, encolando notificaciones a los kqueues que tienen waiters activos. `selwakeup` fuera del lock despierta a los waiters de `poll()` y `select()`. Los dos mecanismos son independientes y ninguno interfiere con el otro.

### Detach: limpieza de la knlist

En el momento del detach, el driver debe vaciar la knlist antes de destruirla. La secuencia correcta es:

```c
knlist_clear(&sc->sc_rsel.si_note, 0);
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

`knlist_clear()` elimina todos los knotes que siguen enlazados. Tras esta llamada, cualquier programa del espacio de usuario que todavía tenga un registro de kqueue verá desaparecer el knote en su próxima recolección de eventos. `seldrain()` despierta a los waiters de `poll()` que estén esperando para que puedan retornar. `knlist_destroy()` verifica que la lista esté vacía y libera los recursos internos.

El orden importa. Si destruyes la knlist sin haberla vaciado antes, la destrucción generará un panic en la aserción de que la lista está vacía. Si vacías la knlist pero dejas a los waiters de poll en espera, dormirán hasta que algo los despierte, lo cual es un desperdicio de recursos. Sigue la secuencia anterior y el camino de detach quedará limpio.

### Un ejemplo más completo: pipes

Abre `/usr/src/sys/kern/sys_pipe.c` y observa la implementación del kqfilter para pipes. Es uno de los ejemplos más completos del kernel y merece una lectura íntegra, ya que los pipes soportan tanto filtros de lectura como de escritura con un manejo adecuado del EOF. Las piezas clave son las dos estructuras filterops:

```c
static const struct filterops pipe_rfiltops = {
    .f_isfd   = 1,
    .f_detach = filt_pipedetach,
    .f_event  = filt_piperead,
    .f_userdump = filt_pipedump,
};

static const struct filterops pipe_wfiltops = {
    .f_isfd   = 1,
    .f_detach = filt_pipedetach,
    .f_event  = filt_pipewrite,
    .f_userdump = filt_pipedump,
};
```

Y la función de evento del filtro de lectura:

```c
static int
filt_piperead(struct knote *kn, long hint)
{
    struct file *fp = kn->kn_fp;
    struct pipe *rpipe = kn->kn_hook;

    PIPE_LOCK_ASSERT(rpipe, MA_OWNED);
    kn->kn_data = rpipe->pipe_buffer.cnt;
    if (kn->kn_data == 0)
        kn->kn_data = rpipe->pipe_pages.cnt;

    if ((rpipe->pipe_state & PIPE_EOF) != 0 &&
        ((rpipe->pipe_type & PIPE_TYPE_NAMED) == 0 ||
        fp->f_pipegen != rpipe->pipe_wgen)) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    kn->kn_flags &= ~EV_EOF;
    return (kn->kn_data > 0);
}
```

Nótese el manejo del EOF, la limpieza explícita de `EV_EOF` cuando el pipe ya no está en EOF (lo que tiene importancia si el named pipe tiene un nuevo escritor) y el uso de `kn->kn_data` para informar de la cantidad de datos disponibles. Estos son los detalles que un driver terminado resuelve correctamente.

### Anatomía de struct knote

Hemos estado pasando un puntero a `struct knote` sin examinarlo con detenimiento, pero la vida del driver es más sencilla cuando sabemos qué contiene. `struct knote`, definida en `/usr/src/sys/sys/event.h`, es la estructura del kernel que representa cada inscripción de interés individual. Cada llamada a `kevent(2)` que registra un interés crea exactamente un knote, que persiste hasta que se elimina el registro. Para un driver, el knote es la unidad de intercambio: toda operación sobre knlist recibe un knote, todo callback de filtro recibe un knote y toda entrega de evento recorre una lista de ellos. Conocer qué hay dentro de la estructura transforma los contratos de callback que hemos estado siguiendo en algo sobre lo que podemos razonar, en lugar de simplemente memorizar.

Los campos que interesan al driver son un pequeño subconjunto de toda la estructura, pero cada uno merece atención.

`kn_filter` identifica qué tipo de filtro solicitó el espacio de usuario. Dentro de `d_kqfilter`, es sobre este campo sobre el que hacemos el switch: `EVFILT_READ`, `EVFILT_WRITE`, `EVFILT_EXCEPT`, etcétera. El valor proviene del campo `filter` del `struct kevent` que envió el espacio de usuario. Un driver que soporta un único tipo de filtro comprueba este campo y rechaza cualquier discrepancia con `EINVAL`.

`kn_fop` es el puntero a la tabla `struct filterops` que dará servicio a este knote durante el resto de su vida. El driver lo establece dentro de `d_kqfilter`. A partir de ese momento, el framework utiliza este puntero para invocar nuestros callbacks de attach, detach, event y touch. La tabla filterops es siempre `static const` en los drivers que examinamos, porque el framework no toma una referencia sobre ella y se espera que el driver mantenga el puntero válido durante toda la vida del knote.

`kn_hook` es un puntero genérico de uso exclusivo del driver. El driver normalmente lo apunta al softc, a un registro de estado por cliente o al objeto al que debe reaccionar el filtro. El framework nunca lo lee ni lo escribe. Cuando se ejecutan los callbacks del filtro, obtienen el estado del driver de `kn_hook` en lugar de realizar una búsqueda global, lo que evita tanto el coste de esa búsqueda como una clase de problemas de ordenación de locks que las búsquedas globales pueden introducir.

`kn_hookid` es un entero complementario a `kn_hook`, disponible para etiquetado específico del driver. La mayoría de los drivers no lo utilizan.

`kn_data` es el mecanismo mediante el cual el callback `f_event` del filtro comunica al espacio de usuario «cuánto está listo». Para filtros de lectura, los drivers almacenan convencionalmente el número de bytes o registros disponibles. Para filtros de escritura, almacenan el espacio disponible. El espacio de usuario lo lee a través del campo `data` del `struct kevent` devuelto, y herramientas como `libevent` dependen de esa convención. El driver `/dev/klog` almacena aquí un recuento de bytes en bruto, mientras que el driver evdev almacena la profundidad de la cola en bytes multiplicando el número de registros por `sizeof(struct input_event)`, porque los clientes evdev leen registros `struct input_event` en lugar de bytes en bruto.

`kn_sfflags` y `kn_sdata` contienen los flags y los datos por registro que el espacio de usuario solicitó mediante los campos `fflags` y `data` del `struct kevent`. Los filtros que soportan control preciso, como `EVFILT_TIMER` con su período o `EVFILT_VNODE` con su máscara de notas, consultan estos campos para decidir cómo comportarse. Los filtros simples de driver normalmente los ignoran.

`kn_flags` contiene los flags de entrega que el framework propaga al espacio de usuario en la siguiente recolección de eventos. El que usa todo driver es `EV_EOF`, que señaliza «no llegarán más datos de esta fuente». Los drivers activan `EV_EOF` en `f_event` cuando el dispositivo está siendo desconectado (detach), cuando el par de un pseudo-terminal ha cerrado, cuando un pipe ha perdido a su escritor o siempre que la señal de disponibilidad se haya vuelto permanente.

`kn_status` es el estado interno propiedad del framework: `KN_ACTIVE`, `KN_QUEUED`, `KN_DISABLED`, `KN_DETACHED` y algunos más. Los drivers no deben modificarlo. La tarea del driver es simplemente informar de la disponibilidad a través de `f_event`; el framework actualiza `kn_status` en consecuencia.

`kn_link`, `kn_selnext` y `kn_tqe` son los campos de enlace de lista usados por las diversas listas del framework de kqueue. Los helpers de knlist los manipulan en nuestro nombre. Los drivers nunca deben tocarlos directamente.

En conjunto, estos campos cuentan una historia sencilla. El driver crea la asociación del knote con sus operaciones de filtro dentro de `d_kqfilter`, establece `kn_hook` y opcionalmente `kn_hookid` para que los callbacks del filtro puedan recuperar su contexto, y luego deja que el framework gestione el enlace y el estado. Al driver le corresponde únicamente el informe de disponibilidad a través de `f_event`. La transición entre driver y framework es limpia, y la mayoría de los bugs de drivers en este ámbito provienen de intentar cruzar esa frontera, ya sea modificando flags de estado que pertenecen al framework o conservando punteros obsoletos al knote una vez que `f_detach` ha sido invocado.

Un punto que merece énfasis: el knote sobrevive a cualquier llamada individual a `f_event`, pero no sobrevive a `f_detach`. Una vez que el framework invoca `f_detach`, el knote está siendo destruido; el driver debe desengancharlo de cualquier estructura interna a la que esté unido y no debe conservar el puntero. El puntero `kn_hook`, que es propiedad del driver, debe tratarse de la misma manera. Si el driver mantenía un puntero inverso desde un campo del softc al knote por alguna razón (poco habitual, pero a veces útil para el detach iniciado por el driver), debe limpiar ese puntero inverso durante `f_detach` antes de que el framework libere el knote.

### Dentro de struct knlist: cómo funciona la sala de espera del driver

`struct knlist`, declarada en `/usr/src/sys/sys/event.h`, es donde un driver acumula los knotes que están interesados en ese momento en una de sus fuentes de notificación. Todo objeto de driver que pueda despertar a waiters de kqueue posee al menos una knlist. El objeto pipe tiene dos, una para lectores y otra para escritores. El tty también tiene dos, `t_inpoll` y `t_outpoll`, cada una con su propia knlist. El objeto de cliente evdev tiene una por cliente. En nuestro driver `evdemo`, nos apoyamos en el `struct selinfo.si_note` que ya tenemos para poll, de modo que la misma knlist es la que despierta tanto a los consumidores de poll como a los de kqueue.

La estructura en sí es pequeña:

```c
struct knlist {
    struct  klist   kl_list;
    void    (*kl_lock)(void *);
    void    (*kl_unlock)(void *);
    void    (*kl_assert_lock)(void *, int);
    void    *kl_lockarg;
    int     kl_autodestroy;
};
```

`kl_list` es la cabeza de lista simplemente enlazada de entradas `struct knote`, con el enlace a través del campo `kn_selnext` de cada knote. El framework manipula la cabeza de lista; el driver nunca lo hace directamente.

`kl_lock`, `kl_unlock` y `kl_assert_lock` son punteros de función que el framework utiliza cuando necesita tomar el lock del objeto. El knlist no posee un lock propio, sino que toma prestado el régimen de locking del driver. Por eso un `struct selinfo` puede albergar un knlist sin crear un lock adicional: el lock es el que el driver ya ha declarado.

`kl_lockarg` es el argumento que se pasa a esas funciones de lock. Cuando inicializamos un knlist con `knlist_init_mtx(&knl, &sc->sc_mtx)`, el framework almacena `&sc->sc_mtx` en `kl_lockarg` y organiza los callbacks de locking para envolver `mtx_lock` y `mtx_unlock`. El driver nunca ve esta conexión y nunca necesita conocerla.

`kl_autodestroy` es un indicador utilizado por algunos subsistemas específicos, principalmente AIO, donde el knlist vive dentro del `struct kaiocb` y debe destruirse automáticamente cuando la solicitud se completa. El código de un driver casi nunca lo establece. La ruta `aio_filtops` en `/usr/src/sys/kern/vfs_aio.c` es el uso canónico, y vale la pena recordar que el indicador existe para que leer ese archivo más adelante no te sorprenda.

El contrato de locking merece especial atención porque es la fuente más común de bugs en drivers que usan kqueue. Cuando el framework llama a nuestro `f_event`, ya sostiene el lock del knlist, que es nuestro mutex de softc. Nuestro `f_event` puede leer el estado del softc, pero no debe tomar el mutex del softc de nuevo (ya es nuestro), no debe dormir y no debe bloquearse en ningún otro lock que pueda estar sostenido durante una invocación de `f_event`. Cuando invocamos `KNOTE_LOCKED`, estamos indicando que ya tenemos el lock, por lo que el framework omite el bloqueo al recorrer la lista. Cuando invocamos `KNOTE_UNLOCKED`, el framework toma y libera el lock en nuestro nombre. Mezclar ambos estilos dentro de una misma ruta del productor es una fuente clásica de pánicos sutiles de doble lock bajo carga.

La unificación con `struct selinfo` merece atención. En la Sección 3 tratamos `struct selinfo` como un concepto exclusivo de poll, pero en realidad incrusta un `struct knlist` en su miembro `si_note`. Esta es la razón por la que un driver que ya soporta `poll()` tiene la infraestructura necesaria para `kqueue()` en su softc: añadir kqueue es en gran medida cuestión de inicializar el knlist con `knlist_init_mtx` y conectar las operaciones de filtro. La ruta del productor ya llama a `selwakeup()`, que a su vez recorre `si_note` bajo el lock apropiado y notifica a cualquier knote enlazado. Hacer la notificación de forma explícita con `KNOTE_LOCKED(&sc->sc_rsel.si_note, 0)` es más claro y nos permite elegir exactamente cuándo se produce la distribución kqueue en relación con cualquier otro trabajo del productor. En los drivers que leeremos más adelante, aparecen ambos estilos; cualquiera de los dos es correcto siempre que el locking sea coherente.

### El ciclo de vida de knlist en detalle

El ciclo de vida de una knlist sigue el ciclo de vida del objeto driver que la posee. Una knlist nace durante el attach (ya sea en el punto de entrada attach del driver para un driver de hardware real, o en el SYSINIT para un pseudo-dispositivo), vive a lo largo de los ciclos open-read-close de los consumidores en espacio de usuario, y se destruye en el detach. Las funciones que necesitamos, todas declaradas en `/usr/src/sys/sys/event.h` e implementadas en `/usr/src/sys/kern/kern_event.c`, son `knlist_init`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_clear` y `knlist_destroy`.

`knlist_init_mtx` es la que llama casi cualquier driver. Inicializa la cabeza de la lista, configura la knlist para usar `mtx_lock`/`mtx_unlock` con el mutex del driver como argumento, y marca la knlist como activa. El llamante pasa un puntero a la knlist (normalmente `&sc->sc_rsel.si_note` o, para drivers con notificación por dirección, también `&sc->sc_wsel.si_note`) y un puntero a un mutex que ya existe en el driver.

`knlist_init` es la forma general, que se usa cuando el régimen de lock del driver no es un mutex simple. Acepta tres punteros a función (lock, unlock, assert), un puntero de argumento que se pasa a esas funciones, y la cabeza de lista subyacente. Los pipes usan la forma `_mtx` con su mutex de par de pipe; los buffers de socket usan un `knlist_init` personalizado porque tienen su propia disciplina de locking. La mayoría de los drivers no necesita la forma general.

`knlist_add` se llama desde `d_kqfilter` para enlazar un knote recién registrado en la lista. Su prototipo es `void knlist_add(struct knlist *knl, struct knote *kn, int islocked)`. El argumento `islocked` indica a la función si el llamante ya tiene el lock de la knlist. Si es cero, la función toma el lock por nosotros. Si es uno, estamos afirmando que ya lo tenemos. Los drivers que no realizan ningún locking adicional dentro de `d_kqfilter` pasan cero; los drivers como `/dev/klog` que tomaron el lock de msgbuf en la entrada pasan uno. Cualquier patrón es correcto; la elección depende de lo que el driver quiera proteger alrededor de la llamada a `knlist_add`.

`knlist_remove` es la operación inversa, que se llama normalmente desde el callback `f_detach`. Su prototipo es `void knlist_remove(struct knlist *knl, struct knote *kn, int islocked)`. El framework invoca `f_detach` con el lock de la knlist ya tomado, por lo que `islocked` es uno en ese contexto. Si por alguna razón el driver necesita eliminar un knote concreto desde fuera de `f_detach` (lo cual es inusual y pocas veces correcto), debe gestionar su propio locking.

`knlist_clear` es la función de eliminación masiva que se usa en el momento del detach del driver. Recorre la lista, elimina todos los knotes y marca cada uno con `EV_EOF | EV_ONESHOT` para que el espacio de usuario vea un evento final y se descarte el registro. La firma `void knlist_clear(struct knlist *knl, int islocked)` es en realidad un envoltorio alrededor de `knlist_cleardel` en `/usr/src/sys/kern/kern_event.c` con un `struct thread *` NULL y el flag de kill activado, lo que significa 'eliminar todo'. Los drivers la llaman desde `detach` justo antes de destruir la knlist.

`knlist_destroy` libera la maquinaria interna de la knlist. Antes de llamarla, la knlist debe estar vacía. Si destruyes una knlist con knotes activos, el kernel genera una aserción y entra en pánico. Por eso la secuencia de detach que vimos antes es rígida:

```c
knlist_clear(&sc->sc_rsel.si_note, 0);
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

`knlist_clear` vacía la lista. `seldrain` despierta a cualquier hilo bloqueado en `poll()` que siga esperando en el mismo selinfo, para que esos threads en espera regresen del kernel. `knlist_destroy` desmonta las estructuras internas y valida que la lista esté vacía. Si se omite cualquiera de estos pasos, el detach se vuelve inseguro: los knotes activos que intentaran llamar a `f_event` de un driver ya descargado provocarían un crash del kernel; un hilo en espera de poll cuyo selinfo haya sido liberado despertaría ante un puntero colgante.

Hay dos aspectos más que vale la pena destacar en la implementación de `knlist_remove` en `/usr/src/sys/kern/kern_event.c`. Entra en el helper interno `knlist_remove_kq`, que también adquiere el lock de kq para que la eliminación sea coherente con cualquier despacho de eventos en curso. Además, activa `KN_DETACHED` en `kn_status` para señalar al resto del framework que ese knote ha desaparecido. Los drivers nunca observan `KN_DETACHED` directamente, pero saber que existe explica por qué el detach concurrente y la entrega de eventos pueden competir de forma segura: la máquina de estados interna del framework los mantiene consistentes.

### El contrato del callback kqfilter

`d_kqfilter` se llama desde la ruta de registro de kqueue en `/usr/src/sys/kern/kern_event.c`, concretamente desde `kqueue_register` a través del método `fo_kqfilter` del descriptor de archivo. Cuando el callback se ejecuta, el framework ya ha validado el descriptor de archivo, asignado la `struct knote` y rellenado la solicitud del espacio de usuario. Nuestra tarea es concreta: elegir las filterops correctas, vincularse a la knlist adecuada y devolver cero.

Lo que `d_kqfilter` debe hacer. Debe inspeccionar `kn->kn_filter` para decidir qué tipo de filtro solicita el espacio de usuario. Debe asignar a `kn->kn_fop` una `struct filterops` válida para ese tipo. Debe adjuntar el knote a una knlist que pertenezca a nuestro driver, normalmente llamando a `knlist_add`. Y debe devolver cero en caso de éxito o un errno razonable en caso de error. Si el driver no puede atender el filtro solicitado, `EINVAL` es la respuesta correcta.

Lo que `d_kqfilter` no debe hacer. No debe dormir, porque la ruta de registro de kqueue mantiene locks bajo los que no es seguro hacerlo. No debe asignar memoria con `M_WAITOK`, por la misma razón. No debe llamar a ninguna función que pueda bloquearse esperando a otro proceso. Si el driver necesita más que una búsqueda rápida y una inserción en la knlist, algo está haciendo mal. El callback es esencialmente una operación de conexión en ruta rápida.

El estado del lock en la entrada merece atención. El framework no mantiene el lock de la knlist cuando llama a `d_kqfilter`. Por tanto, podemos pasar `islocked = 0` a `knlist_add` si no hemos tomado nosotros mismos el lock de la knlist. Si nuestro driver necesita examinar el estado del softc como parte de la lógica de selección de filtro (por ejemplo, para devolver `ENODEV` en un cdev revocado como hace el driver evdev), podemos tomar nosotros el mutex del softc, comprobar el estado, llamar a `knlist_add` con `islocked = 1` y liberar el mutex antes de retornar. El ejemplo de evdev que sigue muestra exactamente ese patrón.

Devolver un valor distinto de cero desde `d_kqfilter` significa que el espacio de usuario recibirá ese errno de vuelta desde `kevent(2)`. No significa 'inténtalo de nuevo'. Un driver que devuelva `EAGAIN` confundirá al espacio de usuario porque `kevent` no interpreta ese valor como lo hace `read`. Usa `EINVAL` para filtros no soportados y `ENODEV` para dispositivos revocados o destruidos, y evita devolver errores rebuscados.

Un matiz sobre cuándo se invoca `d_kqfilter`: una llamada a `kevent(2)` que registra un nuevo interés con `EV_ADD` entra en el framework, comprueba que aún no existe ningún knote para este par (archivo, filtro), asigna uno y luego llama a `fo_kqfilter` en el objeto fileops del descriptor de archivo. Ahí es donde se alcanza nuestro `d_kqfilter`, a través de la tabla fileops del cdev. Si el llamante en cambio está actualizando un registro existente (por ejemplo, alternando entre habilitado y deshabilitado con `EV_ENABLE`/`EV_DISABLE`), nuestro callback no interviene; el framework lo gestiona internamente a través de `f_touch` o manipulación directa del estado.

### Ejemplo práctico: el driver /dev/klog

La implementación de `kqfilter` del lado del driver más sencilla del árbol de código fuente real es el dispositivo de registro del kernel, `/dev/klog`, en `/usr/src/sys/kern/subr_log.c`. Todo su soporte de kqueue cabe en unas cuarenta líneas y usa exactamente el patrón que hemos estado describiendo. Leámoslo.

La tabla filterops es mínima, con solo los callbacks de detach y evento:

```c
static const struct filterops log_read_filterops = {
    .f_isfd   = 1,
    .f_attach = NULL,
    .f_detach = logkqdetach,
    .f_event  = logkqread,
};
```

El hook de attach es NULL porque todo el trabajo del lado del driver ocurre dentro del propio `logkqfilter`. No hace falta un callback `f_attach` separado; el punto de entrada `d_kqfilter` hace todo lo que necesita. Los drivers que necesitan realizar configuración por knote más allá de lo que hace `d_kqfilter` pueden usar `f_attach`, pero eso es poco frecuente.

`logkqfilter` es el callback `d_kqfilter`:

```c
static int
logkqfilter(struct cdev *dev __unused, struct knote *kn)
{

    if (kn->kn_filter != EVFILT_READ)
        return (EINVAL);

    kn->kn_fop = &log_read_filterops;
    knlist_add(&logsoftc.sc_selp.si_note, kn, 1);

    return (0);
}
```

El driver `/dev/klog` solo admite eventos de lectura; una solicitud de cualquier otro tipo de filtro recibe `EINVAL`. El callback asigna a `kn_fop` la tabla filterops estática y luego adjunta el knote a la knlist del selinfo del softc. El tercer argumento de `knlist_add` es `1` aquí, lo que significa que el llamante ya tiene el lock de la knlist. El driver toma el lock del buffer de mensajes antes de entrar en el callback por sus propias razones, por lo que pasar `1` es correcto.

La función de evento es igual de breve:

```c
static int
logkqread(struct knote *kn, long hint __unused)
{

    mtx_assert(&msgbuf_lock, MA_OWNED);

    kn->kn_data = msgbuf_getcount(msgbufp);
    return (kn->kn_data != 0);
}
```

Verifica el lock del buffer de mensajes (que es el que usa la knlist), lee el número de bytes en cola y devuelve un valor distinto de cero si hay algo disponible. El espacio de usuario ve el número de bytes en `kn->kn_data` en la siguiente recogida.

La función de detach es de una sola línea:

```c
static void
logkqdetach(struct knote *kn)
{

    knlist_remove(&logsoftc.sc_selp.si_note, kn, 1);
}
```

Elimina el knote de la knlist, pasando de nuevo `1` porque el framework ha tomado el lock antes de entrar en `f_detach`.

La última pieza es el productor. Cuando el temporizador del log se dispara y hay nuevos datos para notificar a los hilos en espera, `/dev/klog` llama a `KNOTE_LOCKED(&logsoftc.sc_selp.si_note, 0)` bajo el lock del buffer de mensajes. Esto recorre la knlist, llama a `f_event` de cada knote registrado y encola notificaciones para todos los kqueues que tengan hilos en espera. El hint de cero es ignorado por `logkqread`, que es el caso habitual.

Toda la integración con kqueue se inicializa una sola vez al arranque del subsistema mediante `knlist_init_mtx(&logsoftc.sc_selp.si_note, &msgbuf_lock)`. `/dev/klog` nunca se descarga en la práctica, por lo que no hay ninguna secuencia de destrucción que estudiar aquí. Eso llega más adelante, en el ejemplo de evdev.

Lo que hay que destacar es lo pequeño que es este código. Una integración completa, funcional y de nivel productivo de `kqfilter` para un driver real en FreeBSD 14.3 ocupa menos de cuarenta líneas. La complejidad de kqueue está en el framework, no en la contribución del driver.

### Ejemplo práctico: filtros de lectura y escritura en TTY

El subsistema de terminal en `/usr/src/sys/kern/tty.c` nos presenta el siguiente nivel: un driver que admite tanto filtros de lectura como de escritura, y que usa `EV_EOF` para señalar que el dispositivo ha desaparecido. El patrón es el que usamos en cualquier driver que quiera exponer dos caras independientes del mismo dispositivo.

Las dos tablas filterops en `/usr/src/sys/kern/tty.c` son:

```c
static const struct filterops tty_kqops_read = {
    .f_isfd   = 1,
    .f_detach = tty_kqops_read_detach,
    .f_event  = tty_kqops_read_event,
};

static const struct filterops tty_kqops_write = {
    .f_isfd   = 1,
    .f_detach = tty_kqops_write_detach,
    .f_event  = tty_kqops_write_event,
};
```

El punto de entrada `d_kqfilter`, `ttydev_kqfilter`, conmuta según el filtro solicitado y se adjunta a una de las dos knlists:

```c
static int
ttydev_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct tty *tp = dev->si_drv1;
    int error;

    error = ttydev_enter(tp);
    if (error != 0)
        return (error);

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_hook = tp;
        kn->kn_fop = &tty_kqops_read;
        knlist_add(&tp->t_inpoll.si_note, kn, 1);
        break;
    case EVFILT_WRITE:
        kn->kn_hook = tp;
        kn->kn_fop = &tty_kqops_write;
        knlist_add(&tp->t_outpoll.si_note, kn, 1);
        break;
    default:
        error = EINVAL;
        break;
    }

    tty_unlock(tp);
    return (error);
}
```

Hay tres aspectos que merece la pena destacar aquí.

Primero, cada dirección tiene su propio selinfo (`t_inpoll`, `t_outpoll`) y, por tanto, su propia knlist. Un knote de lectura va a una lista y un knote de escritura va a la otra. Esto permite al productor notificar solo el lado que cambió: cuando llegan caracteres entrantes, solo despiertan los hilos en espera de lectura; cuando se vacía el buffer de salida, solo despiertan los hilos en espera de escritura. Los drivers que unan ambos lados en una sola knlist tendrían que desperdiciar ciclos despertando a todos ante cada cambio de estado.

Segundo, el tercer argumento de `knlist_add` es `1`, porque `ttydev_enter` ya ha tomado el lock de tty antes de que se ejecute el switch. El subsistema tty mantiene ese lock tomado de entrada a salida en la mayoría de los puntos de entrada, por lo que toda operación knlist dentro ya está bloqueada.

Tercero, el callback de evento de lectura muestra la disciplina de `EV_EOF` que describimos antes:

```c
static int
tty_kqops_read_event(struct knote *kn, long hint __unused)
{
    struct tty *tp = kn->kn_hook;

    tty_lock_assert(tp, MA_OWNED);

    if (tty_gone(tp) || (tp->t_flags & TF_ZOMBIE) != 0) {
        kn->kn_flags |= EV_EOF;
        return (1);
    } else {
        kn->kn_data = ttydisc_read_poll(tp);
        return (kn->kn_data > 0);
    }
}
```

Si el tty ha desaparecido o es un zombie, se activa `EV_EOF` y el filtro informa que está listo para que el espacio de usuario despierte, lea, no obtenga nada, y aprenda por el flag EOF que el dispositivo ha terminado. En caso contrario, el filtro informa del número de bytes legibles y si ese recuento es positivo. El callback del lado de escritura `tty_kqops_write_event` refleja este patrón, informando sobre `ttydisc_write_poll` para el espacio libre del buffer de salida. Los callbacks de detach simplemente eliminan el knote de la lista en la que estaba, con `islocked = 1` de nuevo.

Lo que el ejemplo de tty enseña es que un driver con dos direcciones necesita dos knlists, dos tablas filterops, dos funciones de evento y un `d_kqfilter` que encamine el registro hacia la correcta. El lado productor es simétrico: los caracteres entrantes disparan `KNOTE_LOCKED` sobre `t_inpoll.si_note`; el espacio libre en el buffer de salida dispara lo mismo sobre `t_outpoll.si_note`. La separación es limpia y predecible, y encaja con la forma en que los programas en espacio de usuario razonan sobre la E/S de terminal.

### Ejemplo práctico: disciplina de detach en evdev

Para el último ejemplo práctico nos centramos en el subsistema de eventos de entrada en `/usr/src/sys/dev/evdev/cdev.c`. Su kqfilter es estructuralmente similar al de `/dev/klog`, pero el driver evdev demuestra algo que los dos ejemplos anteriores pasaron por alto: una secuencia de detach completa que desmonta el knlist de forma segura incluso cuando puede haber procesos activos en el espacio de usuario con registros de kqueue pendientes.

Las rutas de filterops y attach resultan familiares. La tabla de filterops de evdev es:

```c
static const struct filterops evdev_cdev_filterops = {
    .f_isfd   = 1,
    .f_detach = evdev_kqdetach,
    .f_event  = evdev_kqread,
};
```

La implementación de `d_kqfilter` añade una comprobación adicional importante sobre la revocación, lo que hace que evdev sea algo más completo que `/dev/klog`:

```c
static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdev_client *client;
    int ret;

    ret = devfs_get_cdevpriv((void **)&client);
    if (ret != 0)
        return (ret);

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdev_cdev_filterops;
        kn->kn_hook = client;
        EVDEV_CLIENT_LOCKQ(client);
        if (client->ec_revoked)
            ret = ENODEV;
        else
            knlist_add(&client->ec_selp.si_note, kn, 1);
        EVDEV_CLIENT_UNLOCKQ(client);
        break;
    default:
        ret = EINVAL;
    }

    return (ret);
}
```

Si el cliente ha sido revocado, porque el dispositivo va a desaparecer o porque un proceso controlador ha revocado el acceso de forma explícita, el driver devuelve `ENODEV` en lugar de registrar un knote. Fíjate en que el driver toma su propio lock por cliente en torno a la comprobación de `ec_revoked` y a `knlist_add`, de modo que las dos operaciones son atómicas con respecto a la revocación. Este es el contrato que describimos antes, aplicado de forma limpia: búsquedas baratas, una retención breve del lock, sin dormir ni asignar memoria en la ruta caliente.

La función de evento informa de la disponibilidad desde la cola de eventos por cliente:

```c
static int
evdev_kqread(struct knote *kn, long hint __unused)
{
    struct evdev_client *client = kn->kn_hook;

    EVDEV_CLIENT_LOCKQ_ASSERT(client);

    kn->kn_data = EVDEV_CLIENT_SIZEQ(client) *
                  sizeof(struct input_event);
    if (client->ec_revoked) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (kn->kn_data != 0);
}
```

Fíjate en la convención de `kn->kn_data`: no es solo el número de elementos, sino el número de elementos en bytes, porque el espacio de usuario lee valores brutos de `struct input_event` y espera conteos en bytes tal como los devuelve `read()`. Este tipo de detalle es importante para las bibliotecas del espacio de usuario que usan `kn->kn_data` para dimensionar buffers.

La ruta del productor en `evdev_notify_event` combina todos los mecanismos de notificación asíncrona que el subsistema admite:

```c
if (client->ec_blocked) {
    client->ec_blocked = false;
    wakeup(client);
}
if (client->ec_selected) {
    client->ec_selected = false;
    selwakeup(&client->ec_selp);
}
KNOTE_LOCKED(&client->ec_selp.si_note, 0);

if (client->ec_sigio != NULL)
    pgsigio(&client->ec_sigio, SIGIO, 0);
```

Este es un productor asíncrono completo: los hilos bloqueados en `read()` reciben la señal a través de `wakeup()`, los hilos bloqueados en `poll()` y `select()` la reciben a través de `selwakeup()`, los que esperan en kqueue la reciben a través de `KNOTE_LOCKED`, y los consumidores SIGIO registrados la reciben a través de `pgsigio`. Cualquier consumidor concreto percibe exactamente uno de estos mecanismos, pero el productor no necesita saber cuál; los invoca todos de forma incondicional y deja que cada mecanismo se filtre a sí mismo. Nuestro driver `evdemo` adoptará el mismo productor en capas a medida que terminemos el capítulo.

La secuencia de detach es la parte más instructiva. Cuando un cliente de evdev desaparece, el driver ejecuta:

```c
knlist_clear(&client->ec_selp.si_note, 0);
seldrain(&client->ec_selp);
knlist_destroy(&client->ec_selp.si_note);
```

Esto es exactamente la disciplina en tres pasos que describimos. El resultado es que cualquier proceso en el espacio de usuario que todavía tenga un registro de kqueue para este cliente recibe un evento `EV_EOF` final y luego ve desaparecer el registro; cualquier hilo bloqueado en `poll()` todavía detenido en el selinfo se activa y retorna; cualquier entrega de kqueue en vuelo que estaba a punto de hacer una llamada de vuelta a nuestro filterops se completa de forma segura antes de que se libere la memoria del knlist.

Equivocarse en el orden convierte este proceso de desmontaje limpio en un panic. Llamar a `knlist_destroy` antes que a `knlist_clear` activa una aserción sobre una lista no vacía. Usar `knlist_clear` sin `seldrain` deja a los hilos bloqueados en poll colgados. Usar `seldrain` sin un `knlist_clear` previo funcionará, pero dejará registros de kqueue apuntando a un driver que está a punto de desaparecer, y el primer intento de entrega de eventos provocará un crash. Sigue la secuencia.

El ejemplo de evdev reúne todo lo que hemos tratado en esta sección: un attach consciente de la revocación, un informe de eventos con el conteo de bytes correcto, una ruta de productor combinada y un desmontaje que respeta las reglas de vida útil. Un driver que imite este patrón se comportará bien en producción.

### El parámetro hint: qué es y por qué existe

Todo callback `f_event` recibe un argumento `long hint` que hemos estado asignando silenciosamente a cero. Vale la pena entender para qué sirve ese parámetro, porque no es siempre cero en el kernel.

El hint es un cookie que el productor pasa al filtro. Cuando un productor llama a `KNOTE_LOCKED(list, hint)`, el framework pasa ese mismo valor `hint` al `f_event` de cada filtro. Depende completamente del productor y del filtro acordar qué significa ese valor. El framework no lo interpreta.

Para drivers simples que tienen un único significado de "listo", cero es la elección natural y el filtro ignora el argumento. Para drivers con más de una ruta de productor, el hint puede distinguirlas. El filtro de vnode usa hints no nulos para codificar `NOTE_DELETE`, `NOTE_RENAME` y eventos relacionados a nivel de vnode, y la función `f_event` comprueba los bits del hint para decidir qué bits de `kn->kn_fflags` establecer en el evento entregado. Esto va más allá de lo que necesita un driver de caracteres ordinario, pero explica la generalidad de la firma.

El lado del productor es donde se origina el valor del hint. Un driver puede llamar a `KNOTE_LOCKED(&sc->sc_rsel.si_note, MY_HINT_NEW_DATA)` y el filtro puede hacer un switch sobre el valor para tomar caminos distintos. En la práctica, los drivers ordinarios pasan cero y mantienen el filtro simple.

### Entrega de eventos: KNOTE_LOCKED frente a KNOTE_UNLOCKED, en profundidad

Las dos macros de entrega en `/usr/src/sys/sys/event.h` son:

```c
#define KNOTE_LOCKED(list, hint)    knote(list, hint, KNF_LISTLOCKED)
#define KNOTE_UNLOCKED(list, hint)  knote(list, hint, 0)
```

Ambas llaman a la misma función subyacente `knote()` en `/usr/src/sys/kern/kern_event.c`, que recorre el knlist e invoca `f_event` en cada knote. La diferencia está en el tercer argumento: `KNF_LISTLOCKED` indica que quien llama ya tiene el lock del knlist, mientras que cero indica que lo tome por él.

Elegir entre ellas es cuestión de que la elección coincida con la ruta de bloqueo del productor. Si el productor es invocado con el mutex del driver ya tomado (porque se llama desde un gestor de ISR bloqueado, o desde dentro de una función de productor que necesitaba el lock para su propio trabajo), `KNOTE_LOCKED` es la opción correcta. Si el productor es invocado sin lock (porque se ejecuta en contexto de thread y el lock se tomaría específicamente para la notificación), `KNOTE_UNLOCKED` es la opción correcta. El error que hay que evitar es llamar a `KNOTE_LOCKED` sin tener realmente el lock, lo que genera condiciones de carrera terribles bajo carga, o llamar a `KNOTE_UNLOCKED` mientras se tiene el lock, lo que genera recursión y provoca un panic.

Un ejemplo en contexto de ISR ayuda a entenderlo: si un gestor de interrupción de dispositivo llama a una función de la mitad inferior que adquiere el mutex del softc, hace algún trabajo y necesita notificar a los hilos que esperan en kqueue, el patrón más limpio es hacer el trabajo y la llamada a `KNOTE_LOCKED` dentro del mutex tomado y soltar el lock después. El mutex es el lock del knlist, por lo que `KNOTE_LOCKED` es lo que se debe usar. Si la notificación proviene en cambio de un thread que todavía no ha tomado el lock, el thread toma el lock, hace el trabajo, llama a `KNOTE_LOCKED` y luego suelta el lock; o bien usa `KNOTE_UNLOCKED` y deja que el framework tome brevemente el lock al recorrer la lista.

Una segunda sutileza es el comportamiento de `knote` cuando la lista está vacía. Recorrer una lista vacía es barato, pero no es gratuito; sigue tomando el lock. Los drivers que entregan notificaciones a muy alta frecuencia pueden comprobar primero `KNLIST_EMPTY(list)` y omitir la llamada a `KNOTE_LOCKED` si no hay hilos esperando. La macro `KNLIST_EMPTY`, definida en `/usr/src/sys/sys/event.h` como `SLIST_EMPTY(&(list)->kl_list)`, es segura de leer sin el lock a efectos de orientación, porque el peor caso de una lectura obsoleta es un wakeup perdido en un knote que se añadió hace un microsegundo, y ese knote percibirá la siguiente entrega. En la práctica, esta optimización rara vez justifica la complejidad añadida, pero conviene conocerla.

### Errores comunes en implementaciones de kqfilter en drivers

A lo largo de la lectura de drivers con soporte para kqueue en el árbol de código fuente, aparece un puñado de patrones de bugs recurrentes. Conocer los escollos de antemano ayuda a evitarlos.

Olvidar destruir el knlist. Un driver que llama a `knlist_init_mtx` en attach pero no llama a `knlist_destroy` en detach filtra el estado interno del knlist y, lo que es peor, puede dejar knotes activos colgando. La solución es incluir la secuencia clear-drain-destroy en cada ruta de detach.

Llamar a `knlist_destroy` antes que a `knlist_clear`. `knlist_destroy` comprueba mediante una aserción que la lista esté vacía. Si hay algún knote todavía registrado, la aserción falla y el kernel entra en panic. Limpia siempre primero.

Usar `KNOTE_LOCKED` sin el lock. Esto es sutil porque la mayoría de las veces funciona. Bajo carga, dos productores pueden generar una condición de carrera al recorrer los knotes, y la suposición del framework de que la lista es estable durante el recorrido se rompe. El síntoma suele ser una corrupción de puntero de knote o un use-after-free en `f_event`.

Dormir en `f_event`. El framework tiene el lock del knlist, que es nuestro mutex del softc, cuando nos llama. Dormir bajo un mutex es un error del kernel. Si `f_event` necesita un estado que no es accesible bajo el mutex del softc, el diseño es incorrecto; mueve el estado al softc o precalcúlalo antes de la notificación.

Devolver `kn_data` obsoleto. El campo `kn->kn_data` debe reflejar el estado en el momento en que se evaluó el filtro. Un driver que calcula `kn_data` una sola vez en `d_kqfilter` y olvida actualizarlo en `f_event` entregará conteos de bytes obsoletos al espacio de usuario. Recalcúlalo siempre en `f_event`.

Mantener `kn_hook` apuntando a memoria liberada. Si `kn_hook` apunta a un softc y el softc se libera antes de que se desregistre el knote, la siguiente llamada a `f_event` desreferenciará memoria liberada. Esto es lo que `knlist_clear` y `seldrain` se supone que previenen, pero solo si se llaman en el orden correcto y antes de liberar el softc. El orden de detach en el punto de entrada de detach del driver importa.

Establecer `EV_EOF` solo una vez. `EV_EOF` es pegajoso en el sentido de que, una vez establecido, el espacio de usuario lo verá, pero `f_event` se llama múltiples veces durante la vida de un knote. Si la condición que causó `EV_EOF` puede volver a ser falsa (por ejemplo, una named pipe que obtiene un nuevo escritor), el filtro debe limpiar `EV_EOF` de forma explícita. El filtro de pipe en `/usr/src/sys/kern/sys_pipe.c` lo demuestra: `filt_piperead` tanto establece como limpia `EV_EOF` dependiendo del estado del pipe.

Confundir `f_isfd` con `f_attach`. `f_isfd = 1` significa que el filtro está vinculado a un descriptor de archivo; casi todos los filtros de drivers necesitan esto. `f_attach = NULL` significa que la ruta de registro no necesita un callback de attach por knote más allá de lo que `d_kqfilter` ya hizo. Son independientes. Un driver puede establecer `f_isfd = 1` y `f_attach = NULL` al mismo tiempo, que es el caso habitual.

Devolver errores desde `f_event`. `f_event` devuelve un int, pero es un booleano: cero significa "no listo" y cualquier valor no nulo significa "listo". No es un errno. Devolver `EINVAL` desde `f_event` significa "listo", que casi con toda seguridad no es lo que pretendía el driver.

### Un modelo mental para el framework kqueue

Vale la pena detenerse para construir un modelo mental del framework kqueue que encaje con lo que hemos aprendido. A distintos lectores les resultarán útiles distintos modelos; uno que funciona bien para los autores de drivers es el siguiente.

Imagina cada objeto de driver (un cdev, un registro de estado por cliente, un pipe, un tty) como una pequeña oficina. La oficina tiene bandejas de entrada y de salida, que son los knlists. Cuando un visitante (un programa del espacio de usuario) quiere que le avisen cuando hay correo nuevo en la bandeja de entrada, deja una nota adhesiva en la oficina: su descriptor de archivo kqueue más el tipo de filtro que le interesa. Los empleados de la oficina (nuestro callback `d_kqfilter`) toman la nota adhesiva, comprueban en qué bandeja de entrada corresponde colocarla (la bandeja `EVFILT_READ` o la bandeja de salida `EVFILT_WRITE`) y la fijan allí. La nota adhesiva registra a quién notificar (el kqueue) y cómo (los callbacks de `struct filterops`).

Cuando llega correo de verdad (la ruta del productor inserta un registro y quiere notificarlo), los empleados de la oficina recorren las notas adhesivas de la bandeja de entrada y, para cada una, comprueban si la condición se cumple en ese momento (el callback `f_event`). Si se cumple, el empleado descuelga el teléfono y llama al kqueue del visitante, entregando la notificación. El visitante lee la notificación en su próximo reap de `kevent(2)`.

Cuando el visitante cambia de opinión y ya no quiere notificaciones (elimina el registro), los empleados de la oficina retiran la nota adhesiva (el callback `f_detach`). Cuando la oficina cierra definitivamente (el driver se desvincula del sistema), los empleados retiran todas las notas adhesivas de golpe (`knlist_clear`), despiertan a los visitantes que están sentados físicamente en la sala de espera (`seldrain`), y luego desmontan el tablón de notas adhesivas (`knlist_destroy`).

El lock del tablón es el mutex de softc del driver. Los empleados lo mantienen adquirido mientras recorren las notas, fijan una nota o retiran una nota. Por eso `f_event` no puede dormir: los empleados no pueden soltar el lock mientras trabajan con la lista, porque otros empleados podrían llegar con actualizaciones. También por eso `KNOTE_LOCKED` es la llamada correcta cuando el productor ya mantiene el lock: el empleado que dice "ya lo tengo adquirido" permite al framework omitir una readquisición innecesaria.

El modelo está simplificado y omite complicaciones como la semántica de flanco de `EV_CLEAR` y las actualizaciones de registro de `f_touch`, pero captura la arquitectura esencial. El driver posee el tablón; el framework posee las notas adhesivas. El driver notifica la disponibilidad; el framework gestiona la entrega. El driver desmonta el tablón en el detach; las estructuras de notas adhesivas del framework se liberan como parte de ese desmontaje.

Ten este cuadro en mente cuando leas el código de otros subsistemas que usan kqueue, y los nombres desconocidos encajarán con roles familiares. `kqueue_register` es el visitante que entra a entregar una nota adhesiva. `knote` es el empleado que recorre el tablón. `f_event` es la comprobación de disponibilidad individual de cada nota. `selwakeup` es la alarma general que también alcanza el tablón. Los nombres son distintos; las formas son las mismas.

### Lectura de kern_event.c: una guía para el curioso

Para los lectores que quieran ir más allá de los callbacks, el propio framework kqueue merece una visita detallada. `/usr/src/sys/kern/kern_event.c` tiene alrededor de tres mil líneas, lo que parece intimidante, pero la estructura del archivo es predecible una vez que sabemos qué buscar.

Cerca del principio del archivo se declaran las tablas filterops estáticas para los filtros integrados. `file_filtops` gestiona los filtros de lectura y escritura genéricos para los descriptores de archivo que no proporcionan su propio kqfilter; `timer_filtops` gestiona `EVFILT_TIMER`; `user_filtops` gestiona `EVFILT_USER`; y varios más los siguen. Estas son las filterops que el framework instala durante el boot, y leerlas da una buena idea de cómo debe verse una tabla filterops en código de producción.

Tras las declaraciones estáticas vienen los puntos de entrada de las llamadas al sistema: `kqueue`, `kevent` y las variantes heredadas. Estas realizan la validación de argumentos y despachan al núcleo del mecanismo. Un lector que quiera trazar una llamada desde el espacio de usuario a través del kernel empieza aquí.

El núcleo del mecanismo es un conjunto de funciones cuyos nombres comienzan por `kqueue_`. `kqueue_register` gestiona `EV_ADD`, `EV_DELETE`, `EV_ENABLE`, `EV_DISABLE` y `EV_RECEIPT`; es donde el framework llama a nuestro `d_kqfilter` a través de `fo_kqfilter`. `kqueue_scan` recoge los eventos listos y los devuelve al espacio de usuario. `kqueue_acquire` y `kqueue_release` llevan la cuenta de referencias del kqueue para un acceso concurrente seguro. `kqueue_close` desmonta el kqueue cuando se cierra el último descriptor de archivo que lo referencia. Trazar desde el principio de `kqueue_register` pasando por `kqueue_expand`, `knote_attach` y la llamada a `fo_kqfilter` revela el camino completo del registro.

La función `knote` en sí, aproximadamente a dos tercios del archivo, es la que alcanzamos a través de `KNOTE_LOCKED` y `KNOTE_UNLOCKED`. Recorre la knlist, invoca `f_event` en cada knote y encola notificaciones para los que informen de estar listos. Leerla muestra exactamente por qué son necesarias las aserciones de lock en nuestro `f_event` y cómo el framework intercala el recorrido de la lista con la notificación de kqueue. El recorrido utiliza `SLIST_FOREACH_SAFE` con un puntero temporal, de modo que un `f_detach` durante el recorrido no corrompe la iteración. Ese sutil detalle es lo que hace que el detach concurrente y la entrega sean seguros.

Más adelante está el mecanismo de la knlist: `knlist_init`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_cleardel`, `knlist_destroy` y las distintas funciones auxiliares. Estas son las funciones que hemos estado llamando. Leerlas confirma la semántica de lock en la que nos hemos apoyado y muestra cómo se consumen los argumentos `islocked` dentro de esas funciones auxiliares.

Cerca del final del archivo están las implementaciones de los filtros integrados, con nombres como `filt_timerattach`, `filt_user` y `filt_fileattach`. Vale la pena leerlas porque son lo más parecido a una implementación de referencia sobre cómo debe estructurarse un filtro. El filtro de pipe en `/usr/src/sys/kern/sys_pipe.c` es otra buena referencia; el soporte de kqueue para sockets en `/usr/src/sys/kern/uipc_socket.c` es una tercera.

Un lector que recorra `kqueue_register`, `knote` y `knlist_remove` en ese orden comprenderá la mayor parte del framework en una tarde. El mecanismo restante (destrucción automática, implementación del temporizador, filtros de proceso y señales, máscaras de vnode-note) es lo suficientemente especializado como para que el autor de un driver pueda omitirlo a menos que surja una necesidad concreta. El resto de este capítulo no requiere ninguno de esos elementos.

### Patrones de driver que aún no hemos utilizado

En el árbol de código fuente aparecen dos patrones que no hemos utilizado en `evdemo` porque no son necesarios, pero conviene reconocerlos para que los lectores que los encuentren en otros lugares sepan de qué se trata.

El primero es el uso de `f_attach` para la configuración por knote más allá de lo que hace `d_kqfilter`. El filtro `EVFILT_TIMER` usa `f_attach` para arrancar un temporizador de un solo disparo o repetitivo cuando el knote se registra por primera vez, y `f_detach` para detenerlo. El filtro `EVFILT_USER` en `/usr/src/sys/kern/kern_event.c` utiliza `filt_userattach` como una operación vacía (no-op) porque el knote no está asociado a ningún objeto del kernel; el mecanismo `NOTE_TRIGGER` activado por el usuario gestiona la entrega íntegramente a través de `f_touch`. Un driver que necesite su propio estado por knote podría asignarlo en `f_attach` y liberarlo en `f_detach`, usando `kn_hook` o `kn_hookid` para recordar el puntero. Casi ningún driver necesita realmente esto, porque el estado por registro suele encajar de forma natural en el softc.

El segundo es `f_touch`, que intercepta las operaciones `EV_ADD`, `EV_DELETE` y `EV_ENABLE`/`EV_DISABLE`. La función `filt_usertouch` en `/usr/src/sys/kern/kern_event.c` es una buena referencia sobre cómo se estructura `f_touch`: inspecciona el argumento `type` (uno de `EVENT_REGISTER`, `EVENT_PROCESS` o `EVENT_CLEAR`) para decidir qué está pidiendo el espacio de usuario y actualiza los campos del knote en consecuencia. La mayoría de los filtros de driver dejan `f_touch` como NULL y aceptan el comportamiento por defecto del framework, que consiste en almacenar `sfflags`, `sdata` y los flags del evento directamente en el knote durante `EV_ADD`. El comportamiento por defecto es correcto para los filtros que no necesitan lógica adicional en las actualizaciones de registro.

Un tercer patrón que usa el árbol de código fuente pero que nuestro driver no usa es la variante "kill" del desmontaje de la knlist. `knlist_cleardel` en `/usr/src/sys/kern/kern_event.c` acepta un flag `killkn` que, cuando está activo, fuerza la extracción de cada knote de la lista independientemente de si sigue en uso. `knlist_clear` es el envoltorio habitual con este flag activado. Un driver que quiera conservar los knotes tras un evento (por ejemplo, para reasociarlos a un nuevo objeto) podría llamar a `knlist_cleardel` con `killkn` en false y los knotes quedarían desconectados pero vivos. Esto casi nunca es lo que quiere un driver. El caso habitual es `knlist_clear`, que los elimina y libera.

### Una nota sobre EV_CLEAR, EV_ONESHOT y el comportamiento disparado por flanco

El framework kqueue admite varios modos de entrega mediante flags en `struct kevent`:

`EV_CLEAR` hace que el filtro sea disparado por flanco: una vez que un knote se activa, no volverá a activarse hasta que la condición subyacente pase de falso a verdadero de nuevo. Es la opción habitual para los filtros de lectura y escritura en descriptores de alto rendimiento, porque evita inundar el espacio de usuario con notificaciones repetidas para los mismos datos.

`EV_ONESHOT` hace que el filtro se active exactamente una vez y luego se elimine automáticamente. Es útil para eventos que ocurren una sola vez.

`EV_DISPATCH` hace que el filtro se active como máximo una vez por cada recolección de `kevent()`, deshabilitándose automáticamente tras cada activación. El espacio de usuario lo vuelve a habilitar registrándose de nuevo con `EV_ENABLE`. Este es el modo preferido por los programas del espacio de usuario con múltiples threads que quieren asegurarse de que solo un thread reaccione a cada evento.

Las funciones de filtro del driver no necesitan conocer estos flags; el framework los gestiona. El driver simplemente informa de si la condición subyacente se cumple, y el framework decide qué hacer con el knote resultante.

### Cerrando la sección 4

Nuestro driver ya cuenta con soporte de `kqueue`. El código total que hemos añadido no es enorme: un callback `d_kqfilter`, una `struct filterops`, dos funciones de filtro cortas y una llamada a `KNOTE_LOCKED()` en el productor. La complejidad radica más en entender el framework que en escribir mucho código.

Pero solo hemos cubierto los dos filtros más comunes, `EVFILT_READ` y `EVFILT_WRITE`. El alcance del capítulo excluye deliberadamente temas de kqueue más avanzados, como los filtros definidos por el usuario (`EVFILT_USER`), implementaciones personalizadas de `f_touch` e interacciones con el subsistema AIO. Son lo suficientemente especializados como para que raramente aparezcan en drivers ordinarios, y desplazarían material que la mayoría de los lectores necesita. Si los necesitas, el material de esta sección te prepara para leer las partes correspondientes de `/usr/src/sys/kern/kern_event.c` y comprender lo que encuentres.

Repasando lo que ha cubierto esta sección, el lector debería sentirse cómodo con las distintas capas que tienden a confundirse en las discusiones informales sobre kqueue. La capa más externa es la API del espacio de usuario: `kqueue(2)`, `kevent(2)` y los valores de `struct kevent` que los programas envían y recogen. La capa intermedia es el framework: `kqueue_register`, `knote`, `kqueue_scan` y el mecanismo que relaciona los registros con las entregas. La capa interna es el contrato del driver: `d_kqfilter`, `struct filterops`, `struct knote`, `struct knlist` y el pequeño conjunto de funciones auxiliares como `knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_clear` y `knlist_destroy`. Las tres capas se comunican a través de límites bien definidos, y entender cuál es cuál es la diferencia entre adivinar cómo funciona kqueue y comprenderlo de verdad.

También hemos recorrido tres implementaciones reales de drivers: `/dev/klog`, el subsistema tty y la pila de entrada evdev. Cada una ilustra un aspecto diferente del contrato kqfilter. El driver klog muestra el mínimo que necesita un driver con soporte de kqueue. El subsistema tty muestra cómo gestionar dos direcciones con dos knlists separadas. El driver evdev muestra el attach con conocimiento de revocación, el informe de eventos con recuento de bytes correcto, el camino del productor combinado que se ramifica hacia múltiples mecanismos asíncronos y la secuencia estricta de clear-drain-destroy en el detach. Un driver que combine las piezas apropiadas de estos tres patrones se comportará bien en producción, y un lector que haya seguido la discusión debería ser capaz de reconocer esos patrones cuando aparezcan en otros subsistemas del árbol.

En la siguiente sección pasamos al tercer mecanismo asíncrono, `SIGIO`. A diferencia de `poll()` y `kqueue()`, que son notificaciones de tipo pull (el espacio de usuario pregunta, el kernel responde), `SIGIO` es de tipo push: el kernel envía una señal al proceso registrado cada vez que cambia el estado del dispositivo. Es más antiguo, más sencillo y tiene algunos problemas sutiles en programas con múltiples threads, pero sigue siendo útil en situaciones concretas y forma parte del conjunto estándar de herramientas para drivers.

## 5. Señales asíncronas con SIGIO y FIOASYNC

El tercer mecanismo asíncrono clásico es la E/S dirigida por señales, también llamada notificación `SIGIO` por la señal que normalmente utiliza. El usuario la habilita mediante el ioctl `FIOASYNC` sobre un descriptor de archivo abierto, establece un propietario con `FIOSETOWN` e instala un manejador para `SIGIO`. El driver, cada vez que se produce un cambio de estado relevante, envía `SIGIO` al propietario registrado. Esa señal puede interrumpir casi cualquier llamada al sistema en el propietario, que entonces suele atender el dispositivo y volver a su trabajo habitual.

La E/S dirigida por señales es anterior a `kqueue`, menos escalable que `poll` y tiene algunos problemas sutiles en programas con múltiples threads. Sigue siendo el mecanismo adecuado en un conjunto pequeño pero real de casos: programas con un solo thread que quieren la notificación asíncrona más sencilla posible, scripts de shell que usan `trap` y código heredado que lleva décadas usando `SIGIO` y no va a cambiar. FreeBSD sigue ofreciendo soporte completo para él, y se espera que la mayoría de los drivers de caracteres ordinarios respeten el mecanismo.

### Cómo funciona la E/S dirigida por señales desde el espacio de usuario

Un programa de usuario que utilice `SIGIO` hace tres cosas. Instala un manejador de señal para `SIGIO`. Le dice al kernel qué proceso debe ser el propietario de la señal para este descriptor. Habilita la notificación asíncrona.

El código tiene un aspecto similar a este:

```c
#include <signal.h>
#include <sys/filio.h>
#include <fcntl.h>
#include <unistd.h>

static volatile sig_atomic_t got_sigio;

static void
on_sigio(int sig)
{
    got_sigio = 1;
}

int
main(void)
{
    int fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);

    struct sigaction sa;
    sa.sa_handler = on_sigio;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGIO, &sa, NULL);

    int pid = getpid();
    ioctl(fd, FIOSETOWN, &pid);

    int one = 1;
    ioctl(fd, FIOASYNC, &one);

    for (;;) {
        pause();
        if (got_sigio) {
            got_sigio = 0;
            char buf[256];
            ssize_t n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                /* process data */
            }
        }
    }
}
```

El orden de los ioctls es importante. El programa instala primero el manejador de señal para que `SIGIO` no sea ignorada cuando llegue. A continuación llama a `FIOSETOWN` con su propio PID (los valores positivos indican un proceso, los valores negativos indican un grupo de procesos) para que el driver sepa dónde entregar la señal. Por último, llama a `FIOASYNC` con un valor distinto de cero para habilitar la notificación.

Una vez habilitada la notificación asíncrona, cada cambio de estado en el driver que habría satisfecho un `POLLIN` provoca una señal `SIGIO` al propietario. El manejador del programa se ejecuta de forma asíncrona, establece un flag y retorna; el bucle principal atiende entonces el dispositivo. Vacía el dispositivo con lecturas no bloqueantes hasta agotarlo, porque entre el momento en que se envió la señal y el momento en que se ejecutó el manejador pueden haberse acumulado múltiples eventos.

### Los ioctls FIOASYNC, FIOSETOWN y FIOGETOWN

Abre `/usr/src/sys/sys/filio.h` para ver las definiciones de los ioctls:

```c
#define FIOASYNC    _IOW('f', 125, int)   /* set/clear async i/o */
#define FIOSETOWN   _IOW('f', 124, int)   /* set owner */
#define FIOGETOWN   _IOR('f', 123, int)   /* get owner */
```

Estos son ioctls estándar que la mayor parte de la capa de gestión de descriptores de archivo ya entiende. Para un descriptor de archivo ordinario (un socket, una tubería, un pty), el kernel los gestiona sin implicar al driver. Para un `cdev`, sin embargo, el driver es responsable de implementarlos, porque el driver posee el estado que los ioctls manipulan.

El enfoque convencional en un driver de caracteres de FreeBSD es:

`FIOASYNC` toma un argumento de tipo `int *`. Un valor distinto de cero habilita la notificación asíncrona. El cero la deshabilita. El driver almacena el indicador en el softc y lo usa para decidir si generar señales.

`FIOSETOWN` toma un argumento de tipo `int *`. Un valor positivo es un PID, un valor negativo es un ID de grupo de procesos, y el cero borra el propietario. El driver usa `fsetown()` para registrar el propietario.

`FIOGETOWN` toma un argumento de tipo `int *` que debe rellenarse. El driver usa `fgetown()` para obtener el propietario actual.

### fsetown, fgetown y funsetown

El mecanismo de seguimiento del propietario utiliza un `struct sigio` en el kernel. No tenemos que asignar ni gestionar esa estructura directamente; los helpers `fsetown()` y `funsetown()` lo hacen por nosotros. La API pública, en `/usr/src/sys/sys/sigio.h` y `/usr/src/sys/kern/kern_descrip.c`, consta de cuatro funciones:

```c
int   fsetown(pid_t pgid, struct sigio **sigiop);
void  funsetown(struct sigio **sigiop);
pid_t fgetown(struct sigio **sigiop);
void  pgsigio(struct sigio **sigiop, int sig, int checkctty);
```

El driver almacena un único `struct sigio *` en el softc. Los cuatro helpers toman un puntero a este puntero, porque pueden reemplazar la estructura completa como parte de su trabajo. Los helpers se encargan del conteo de referencias, el locking y la eliminación segura durante la salida del proceso mediante `eventhandler(9)`.

`fsetown()` instala un nuevo propietario. Espera ser llamado con las credenciales del llamador disponibles (lo cual siempre ocurre dentro de un manejador de ioctl). Si el PID objetivo es cero, borra el propietario. Si el objetivo es un número positivo, busca el proceso. Si es un número negativo, busca el grupo de procesos. Devuelve cero en caso de éxito o un errno en caso de error.

`funsetown()` borra el propietario y libera la estructura asociada. Los drivers lo llaman durante el cierre y durante el detach para asegurarse de que no queden referencias obsoletas.

`fgetown()` devuelve el propietario actual como un PID (positivo) o un ID de grupo de procesos (negativo), o cero si no hay ningún propietario establecido.

`pgsigio()` entrega una señal al propietario. El tercer argumento, `checkctty`, debe ser cero para un driver que no sea un terminal controlador. Esto es lo que el driver llama desde la ruta del productor siempre que la notificación asíncrona esté habilitada.

### Implementación de SIGIO en evdemo

Juntando las piezas, esto es lo que añadimos a nuestro driver para dar soporte a `SIGIO`:

En el softc:

```c
struct evdemo_softc {
    /* ... existing fields ... */
    struct sigio    *sc_sigio;
    bool             sc_async;
};
```

En `d_ioctl`:

```c
static int
evdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int error = 0;

    switch (cmd) {
    case FIOASYNC:
        mtx_lock(&sc->sc_mtx);
        sc->sc_async = (*(int *)data != 0);
        mtx_unlock(&sc->sc_mtx);
        break;

    case FIOSETOWN:
        error = fsetown(*(int *)data, &sc->sc_sigio);
        break;

    case FIOGETOWN:
        *(int *)data = fgetown(&sc->sc_sigio);
        break;

    default:
        error = ENOTTY;
        break;
    }
    return (error);
}
```

En la ruta del productor:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    async = sc->sc_async;
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

En `d_close` o durante el detach:

```c
static int
evdemo_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;

    funsetown(&sc->sc_sigio);
    /* ... other close handling ... */
    return (0);
}
```

Repasemos las partes.

El softc gana dos nuevos campos: `sc_sigio`, el puntero que pasamos a `fsetown()` y sus compañeros, y `sc_async`, un indicador que le dice al productor si las señales están habilitadas. El indicador es redundante con «sc_sigio no es NULL» en cierto sentido, pero mantenerlo explícito hace que el código del productor sea más claro y rápido.

El manejador `d_ioctl` implementa los tres ioctls. Tomamos el mutex del softc para `FIOASYNC` porque actualizamos `sc_async`. No tomamos el mutex para `FIOSETOWN` y `FIOGETOWN` porque los helpers `fsetown()` y `fgetown()` tienen su propio locking interno y no deben llamarse con un lock de driver en posesión.

En el productor, copiamos `sc_async` en una variable local bajo el lock para que el valor que usamos fuera del lock sea coherente. Si hubiéramos leído simplemente `sc->sc_async` después de soltar el lock, otro thread podría haberlo cambiado en ese intervalo, lo que constituiría una condición de carrera. Tomar una instantánea bajo el lock evita la condición de carrera.

Llamamos a `pgsigio()` fuera del lock del softc porque `pgsigio()` toma sus propios locks y podría crear problemas de ordenamiento si se anidaran. El patrón es el mismo que con `selwakeup()`: actualizar bajo el lock, soltarlo y luego entregar la notificación.

En `d_close`, llamamos a `funsetown()` para borrar el propietario. Esto también gestiona el caso en que el proceso que estableció el propietario haya terminado desde entonces, de modo que el driver no pierde asignaciones de `struct sigio`. Si el proceso ya ha terminado, `funsetown()` es esencialmente una no-operación; si no lo ha hecho, la llamada limpia el registro.

### Advertencias: semántica de señales en programas multi-thread

El I/O dirigido por señales tiene debilidades bien conocidas en programas multi-thread. El problema principal es que las señales en POSIX se envían a un proceso, no a un thread específico. Cuando el kernel entrega `SIGIO`, cualquiera de los threads del proceso cuya máscara permita la señal puede ser el que la reciba. Para un programa que desea que un thread específico atienda la notificación, esto resulta incómodo.

Existen soluciones alternativas. `pthread_sigmask()` puede usarse para bloquear `SIGIO` en todos los threads excepto en el que debe atenderlo. Si quieres convertir señales en eventos legibles sobre un descriptor de archivo, FreeBSD ofrece `EVFILT_SIGNAL` mediante `kqueue(2)`, que permite que un kqueue informe de que una señal determinada ha sido entregada al proceso. FreeBSD no proporciona la llamada al sistema `signalfd(2)`, propia de Linux. La solución alternativa más sencilla, y a menudo la correcta, es usar `kqueue` directamente para los eventos del driver subyacente: cada thread puede poseer un kqueue separado y esperar exactamente los eventos que le interesan, sin lidiar en absoluto con las reglas de entrega de señales.

Una segunda debilidad es que las señales interrumpen las llamadas al sistema. Con los SA flags predeterminados, una llamada al sistema interrumpida devuelve `EINTR`, y el programa debe comprobarlo y reintentar. Esto es lo suficientemente inusual como para producir con frecuencia errores en programas escritos sin tener en cuenta `SIGIO`. La solución es establecer `SA_RESTART` en `sa_flags`, lo que hace que el kernel reinicie automáticamente las llamadas al sistema interrumpidas.

Una tercera debilidad es que la entrega de señales es asíncrona respecto a la ejecución del programa. Una señal que llega mientras el programa está actualizando una estructura de datos puede llevar a un estado inconsistente si el manejador de señales toca la misma estructura. La solución es mantener los manejadores de señales muy simples (establecer un indicador y devolver) y realizar el trabajo real en el bucle principal.

Para los programas modernos, `kqueue` evita los tres problemas. Para programas heredados y aplicaciones simples de un solo thread, `SIGIO` funciona perfectamente, e implementarlo en un driver requiere una pequeña cantidad de código.

### Cómo son los drivers reales

El driver `if_tuntap.c` proporciona un ejemplo representativo del manejo de SIGIO. En el softc:

```c
struct tuntap_softc {
    /* ... */
    struct sigio        *tun_sigio;
    /* ... */
};
```

En el manejador de ioctls, el driver llama a `fsetown()` y `fgetown()` para `FIOSETOWN` y `FIOGETOWN` respectivamente, y almacena el indicador `FIOASYNC`. En la ruta del productor (cuando un paquete está listo para ser leído desde la interfaz), el driver llama a `pgsigio()`:

```c
if (tp->tun_flags & TUN_ASYNC && tp->tun_sigio)
    pgsigio(&tp->tun_sigio, SIGIO, 0);
```

En la ruta de cierre, llama a `funsetown()`.

El driver `evdev/cdev.c` tiene una estructura similar. Estos son los patrones que reutilizarás en tus propios drivers.

### Prueba de SIGIO desde la shell

Una propiedad interesante de `SIGIO` es que puedes demostrarlo desde la shell sin escribir ningún código. Las shells de estilo Bourne (sh, bash) tienen un comando `trap` incorporado que ejecuta una acción cuando llega una señal. Combinado con el ioctl `FIOASYNC`, podemos configurar una prueba en pocas líneas:

```sh
trap 'echo signal received' SIGIO
exec 3< /dev/evdemo
# (mechanism to enable FIOASYNC on fd 3 goes here)
# Trigger events in another terminal and watch for "signal received"
```

El inconveniente es que no existe una forma directa a nivel de shell de emitir un `ioctl`. Necesitas un pequeño helper en C, o una herramienta como el comando `ioctl(1)` que algunas versiones de BSD incluyen, o `truss` en un proceso hijo trazado. Para el laboratorio de este capítulo proporcionamos un pequeño programa `evdemo_sigio` que llama a los ioctls correctos y luego simplemente se detiene, dejando que el manejador `trap` de la shell muestre las entregas de señales.

### Una nota sobre POSIX AIO

FreeBSD también admite las APIs POSIX `aio_read(2)` y `aio_write(2)`. Estas quedan fuera del alcance normal de un driver de caracteres, y los drivers `cdev` ordinarios casi nunca necesitan implementar nada especial para participar en AIO. Las subsecciones restantes de esta sección explican por qué es así, cómo AIO despacha realmente las solicitudes dentro del kernel y cuándo (si alguna vez) un driver debería pensar en AIO. La intención es evitar una fuente habitual de confusión: cuando los lectores ven «I/O asíncrono sobre archivos» en la documentación de FreeBSD, están leyendo sobre POSIX AIO, y es fácil suponer que un driver necesita su propia maquinaria AIO para ser un ciudadano de primera clase. No es así.

### Cómo despacha AIO: fo_aio_queue y aio_queue_file

Cuando un programa en espacio de usuario llama a `aio_read(2)` o `aio_write(2)`, la solicitud entra en el kernel, se valida y se convierte en un `struct kaiocb` (bloque de control AIO del kernel). Vale la pena seguir el camino de código desde ese punto, porque explica por qué un driver de caracteres casi nunca necesita hacer nada respecto a POSIX AIO.

En `/usr/src/sys/kern/vfs_aio.c`, el despacho se realiza en la capa de operaciones de archivo. La decisión relevante, dentro de `aio_aqueue`, tiene este aspecto:

```c
if (fp->f_ops->fo_aio_queue == NULL)
    error = aio_queue_file(fp, job);
else
    error = fo_aio_queue(fp, job);
```

La decisión se toma en la capa de operaciones de archivo, no en la capa cdev. Si el `struct fileops` del archivo tiene su propio puntero de función `fo_aio_queue`, AIO delega en él. Las operaciones de archivo de vnode establecen `fo_aio_queue = vn_aio_queue_vnops`, que enruta las solicitudes de archivo regular a través de una ruta que sabe cómo comunicarse con el sistema de archivos subyacente. El fileops de un archivo cdev, por el contrario, deja `fo_aio_queue` como NULL, por lo que AIO cae en la ruta genérica `aio_queue_file`.

`aio_queue_file` en `/usr/src/sys/kern/vfs_aio.c` intenta dos cosas. Primero, intenta `aio_qbio` (la ruta basada en bio, descrita en la siguiente subsección) si el objeto subyacente parece un dispositivo de bloques. En segundo lugar, si la ruta bio no es aplicable, programa `aio_process_rw` en uno de los threads trabajadores de AIO. `aio_process_rw` es una ruta basada en daemon que simplemente llama a `fo_read` o `fo_write` de forma síncrona desde el thread trabajador de AIO. En otras palabras, para un cdev genérico, el «I/O asíncrono» se implementa pidiendo a un thread del kernel que realice una `read()` o `write()` síncrona en nombre de la aplicación.

Por eso los drivers de caracteres ordinarios no necesitan implementar sus propios hooks de AIO. El subsistema AIO no llama al driver a través de un nuevo punto de entrada; llama a `fo_read` y `fo_write`, que a su vez llaman a los `d_read` y `d_write` existentes del driver. Si nuestro driver ya admite lecturas bloqueantes y no bloqueantes correctamente, ya admite AIO, simplemente a través de un thread trabajador. No se requiere código adicional en el lado del driver.

### La ruta del dispositivo de bloques: aio_qbio y callbacks de bio

Para los dispositivos de bloques (disco, CD y similares), la ruta del thread trabajador es ineficiente porque la capa de I/O de bloques ya tiene su propio mecanismo de finalización asíncrona. FreeBSD aprovecha esto mediante `aio_qbio` en `/usr/src/sys/kern/vfs_aio.c`, que envía la solicitud como un `struct bio` directamente a la rutina de estrategia del dispositivo subyacente y organiza que `aio_biowakeup` sea llamado al completarse. El bio lleva un puntero de retorno al `struct kaiocb` para que la finalización pueda encontrar el camino de vuelta al marco AIO.

`aio_biowakeup` en `/usr/src/sys/kern/vfs_aio.c` recupera el `struct kaiocb` que transporta el bio, calcula el recuento de bytes residuales y llama a `aio_complete` con el resultado. `aio_complete` establece los campos de estado y error en el kaiocb, lo marca como terminado y luego llama a `aio_bio_done_notify`, que se distribuye hacia cualquier registro de kqueue en el kaiocb, cualquier bloqueante en `aio_suspend` y cualquier registro de señal que el espacio de usuario solicitó a través del campo `aiocb.aio_sigevent`.

`aio_biocleanup` es el helper complementario que libera las asignaciones del buffer del bio y devuelve el bio a su pool. Todo bio utilizado en la ruta AIO pasa por él, ya sea en la ruta de despertar o en el bucle de limpieza cuando el envío falla a mitad de una solicitud multi-bio.

Este recorrido es completamente interno a la capa de E/S de bloques. Un driver de caracteres que no sea un dispositivo de bloques nunca lo verá. Un driver de dispositivo de bloques ve exactamente los mismos bios que vería desde cualquier otra fuente: el driver no puede distinguir si ese bio concreto provino de `aio_read` o de `read` sobre una página del buffer cache. Ese es precisamente el punto. AIO encaja en la capa de bloques reutilizando el contrato de strategy existente, sin añadir un camino paralelo. Un driver de bloques que implemente correctamente su rutina strategy obtiene AIO de forma gratuita.

### La ruta del worker thread: aio_process_rw

Cuando `aio_qbio` no es aplicable, como ocurre con casi todos los drivers de caracteres, `aio_queue_file` cae en `aio_schedule(job, aio_process_rw)`. Esto coloca el trabajo en la cola de trabajo AIO. Uno de los threads del daemon AIO creados previamente (el tamaño del pool es ajustable mediante el sysctl `vfs.aio.max_aio_procs`) lo recoge, ejecuta `aio_process_rw` y realiza la E/S real.

`aio_process_rw` en `/usr/src/sys/kern/vfs_aio.c` es el núcleo de la ruta del worker. Prepara un `struct uio` a partir de los campos del kaiocb, llama a `fo_read` o `fo_write` sobre el archivo y pasa el valor de retorno a `aio_complete`. Desde el punto de vista del driver, la E/S llega a través de una llamada de lectura o escritura completamente ordinaria, con una diferencia sutil: el thread que llama es un daemon AIO, no el proceso que envió la solicitud. Las credenciales del usuario son correctas porque el framework AIO las conservó, pero el contexto del proceso es el del daemon AIO. Los drivers que dependen de `curthread` o `curproc` para su propia contabilidad interna pueden ver valores inesperados; los que no lo hacen, que son casi todos, se comportan de forma idéntica independientemente de si el que llama es el propio thread del usuario o un daemon AIO.

La ruta del worker thread no es "asíncrona" en el sentido del hardware. Es "asíncrona" en el sentido de la API: el userland no bloqueó. La sustitución ocurre en el límite del thread, no en el límite de la E/S, por lo que un dispositivo lento sigue ocupando un worker AIO mientras atiende la solicitud. Para la mayoría de los drivers cdev, este es exactamente el equilibrio correcto. El userland obtiene el modelo de programación que desea; el kernel usa un worker thread para hacer el trabajo; el driver no hace nada especial. Si el driver ya respeta `O_NONBLOCK` correctamente, el worker thread incluso puede enviarle solicitudes no bloqueantes y devolver `EAGAIN` al userland a través de la ruta normal.

### Finalización: aio_complete, aio_cancel y aio_return

Una vez que se ha llamado a `aio_complete`, el kaiocb entra en su estado de finalización. El programa en userland acabará llamando a `aio_return(2)` para obtener el recuento de bytes, o a `aio_error(2)` para comprobar el código de error, o esperará en un kqueue o una señal para que se le informe de que el trabajo ha terminado. Esas llamadas buscan el kaiocb por su puntero en userland y devuelven los campos que estableció `aio_complete`.

Desde el punto de vista del driver, no hay nada que hacer en la ruta de retorno. El driver no es propietario del kaiocb, no lo libera y no señaliza la finalización directamente. La finalización la anuncia `aio_complete`; `aio_return` es una cuestión del userland que gestiona íntegramente la capa AIO del kernel. El trabajo del driver terminó cuando satisfizo la llamada a `fo_read` o `fo_write`, o cuando la rutina strategy llamó a `biodone` sobre el bio.

Para la cancelación, `aio_cancel` en `/usr/src/sys/kern/vfs_aio.c` acaba llamando a `aio_complete(job, -1, ECANCELED)`. Eso es todo. El trabajo se marca como completado con un error y se activan las rutas de activación habituales. El driver no necesita saber nada sobre la cancelación a menos que implemente su propia cola de retención de solicitudes de larga duración, lo cual es excepcional.

Vale la pena dejar clara una distinción. `aio_cancel` es la función de cancelación del lado del kernel utilizada internamente por AIO; no es el syscall de userland. `aio_cancel(2)`, orientado al userland, toma un descriptor de archivo y un puntero a un `aiocb` y pide al kernel que cancele una o todas las solicitudes pendientes. Internamente, esto termina llamando al `aio_cancel` del kernel sobre cada kaiocb coincidente. El nombre es un poco desafortunado; leer el código fuente deja claro cuál es cuál.

### EVFILT_AIO: cómo usa AIO kqueue

Vale la pena saber, aunque no actuar sobre ello, que `EVFILT_AIO` existe. Declarado en `/usr/src/sys/sys/event.h` e implementado en `/usr/src/sys/kern/vfs_aio.c` como la tabla `aio_filtops`, permite que los programas en userland esperen las finalizaciones AIO en un kqueue. Los filterops se registran una sola vez al cargar el módulo AIO mediante `kqueue_add_filteropts(EVFILT_AIO, &aio_filtops)`. Las callbacks por kaiocb son:

```c
static const struct filterops aio_filtops = {
    .f_isfd   = 0,
    .f_attach = filt_aioattach,
    .f_detach = filt_aiodetach,
    .f_event  = filt_aio,
};
```

`f_isfd` es cero aquí porque un registro AIO tiene como clave el kaiocb, no un descriptor de archivo. `filt_aioattach` vincula el knote a la propia knlist del kaiocb. `filt_aio` informa del estado de finalización comprobando si el kaiocb ha sido marcado como terminado. El campo `kl_autodestroy` de la knlist del kaiocb está activo, por lo que la knlist puede desmontarse automáticamente cuando el kaiocb se libera. Este es uno de los pocos lugares en el árbol donde `kl_autodestroy` se ejerce realmente, lo que hace que `vfs_aio.c` sea una lectura útil si alguna vez necesitas entender cómo se usa ese indicador.

Nada de esto es asunto del driver. El módulo AIO registra `EVFILT_AIO` una vez en el boot y, a partir de entonces, el userland puede esperar las finalizaciones a través de kqueue sin ninguna implicación adicional del driver. Un driver que quiera que el userland pueda esperar eventos originados por el driver a través de kqueue lo hace mediante `EVFILT_READ` o `EVFILT_WRITE`, no mediante `EVFILT_AIO`.

### Por qué kqueue es la respuesta correcta para la mayoría de drivers

Reuniendo todo esto, la orientación para los autores de drivers es clara.

Si el driver es un dispositivo de bloques, el kernel ya conecta AIO a la ruta bio a través de `aio_qbio`. No se necesita trabajo adicional. Un driver de bloques que sirve correctamente su rutina strategy también sirve AIO correctamente.

Si el driver es un dispositivo de caracteres que emite eventos y quiere que el userland espere a ellos sin bloquear un thread, el mecanismo correcto es `kqueue`. El userland registra `EVFILT_READ` o `EVFILT_WRITE` en el descriptor de archivo del driver, y el driver notifica a los esperadores mediante `KNOTE_LOCKED`. Esto es lo que hemos venido construyendo a lo largo de este capítulo, y lo que hacen todos los drivers que hemos leído.

Si el driver es un dispositivo de caracteres al que los programadores de userland querrían llamar con `aio_read(2)` por razones de portabilidad, no se requiere ningún trabajo del lado del driver. AIO atenderá la solicitud mediante un worker thread que llama al `d_read` existente del driver. El userland obtiene la portabilidad que quiere; el driver puede mantenerse simple.

La única ocasión en que un driver podría plantearse implementar `d_aio_read` o `d_aio_write` es cuando dispone de una ruta de hardware de alto rendimiento, genuinamente asíncrona, que puede completar el trabajo sin bloquear un worker thread, y cuando el coste del fallback al worker thread sería prohibitivo. Esto es excepcionalmente raro en drivers ordinarios, y los drivers que sí tienen dicha ruta (principalmente los de almacenamiento) normalmente la exponen a través de la capa de bloques y no como un cdev.

En resumen: para los drivers cdev, "implementar AIO" casi siempre significa "implementar kqueue". El resto de la maquinaria AIO pertenece al kernel, no a nosotros. Y esa es la nota con la que queríamos terminar esta parte del capítulo, porque cierra el círculo: de los cuatro mecanismos asíncronos (poll, kqueue, SIGIO, AIO), el que más código de driver necesita es kqueue, y el que menos necesita es AIO. El capítulo ha dedicado su tiempo, por tanto, al mecanismo que importa.

### Lectura de vfs_aio.c: una guía

Para los lectores que quieran trazar la ruta AIO a través del kernel, `/usr/src/sys/kern/vfs_aio.c` está organizado de la siguiente manera.

Cerca de la parte superior del archivo se tratan `struct kaiocb` y `struct kaioinfo` (a través de comentarios en el código circundante, ya que las propias estructuras están declaradas en `/usr/src/sys/sys/aio.h`). A continuación aparecen el conjunto de funciones estáticas `filt_aioattach`/`filt_aiodetach`/`filt_aio` y la tabla `aio_filtops`. Estas son la integración de kqueue para `EVFILT_AIO`.

A continuación vienen el SYSINIT y el registro del módulo, con `aio_onceonly` realizando la configuración única que incluye `kqueue_add_filteropts(EVFILT_AIO, &aio_filtops)`. Aquí es donde se instala el filtro `EVFILT_AIO` a nivel de sistema. Ningún driver participa; el módulo AIO lo hace por sí solo.

La parte central del archivo es el núcleo de AIO: `aio_aqueue` (el punto de entrada de la capa syscall), `aio_queue_file` (el dispatcher genérico), `aio_qbio` (la ruta basada en bio), `aio_process_rw` (la ruta del worker thread), `aio_complete` (el anuncio de finalización) y `aio_bio_done_notify` (el fan-out de activación). Seguir la traza desde `aio_aqueue` a través de cada uno de ellos en orden mapea la vida de una solicitud AIO desde el envío hasta la finalización.

Las funciones de señalización de finalización incluyen `aio_bio_done_notify`, que recorre la knlist del kaiocb y dispara `KNOTE_UNLOCKED` sobre cualquier knote `EVFILT_AIO` registrado, activa cualquier thread bloqueado en `aio_suspend` y entrega cualquier señal registrada a través de `pgsigio`. Este es el análogo AIO de la ruta de productor combinada que vimos en el driver evdev.

La cancelación reside en `aio_cancel` y en `kern_aio_cancel` de la capa syscall. `aio_cancel` sobre un kaiocb simplemente llama a `aio_complete(job, -1, ECANCELED)`, que empuja el trabajo a través de la misma ruta de finalización que uno exitoso. El userland ve `ECANCELED` en lugar de un recuento de bytes.

El archivo termina con las implementaciones de syscall para `aio_read`, `aio_write`, `aio_suspend`, `aio_cancel`, `aio_return`, `aio_error` y otras relacionadas, además del envío por lotes `lio_listio`. Todas ellas acaban llamando a los dispatchers centrales de la parte central del archivo.

Un lector que siga la traza de un `aio_read` de userland a través de `aio_aqueue` hasta `aio_queue_file`, ya sea por `aio_qbio` o por `aio_process_rw`, y luego a través de `aio_complete` de vuelta a `aio_bio_done_notify`, habrá visto toda la ruta AIO de principio a fin. El archivo es largo, pero la estructura es regular, y las partes que conciernen a los drivers son una pequeña fracción del total.

### Lista de comprobación para el driver

Ahora que hemos analizado qué pide y qué no pide AIO a los drivers, aquí tienes una breve lista de comprobación que los autores de drivers pueden usar como referencia rápida.

Para un driver cdev que solo necesita notificación básica de eventos de lectura, no hay nada que hacer para AIO. Implementa `d_read`, implementa `d_poll` o `d_kqfilter` para la notificación no bloqueante, y el userland puede usar `aio_read(2)` a través del worker thread AIO sin código adicional en el driver.

Para un driver cdev que quiera ser amigable con los programas de userland que usan AIO por razones de portabilidad, la respuesta es la misma: no se necesita nada adicional. El worker thread AIO lo gestiona.

Para un driver de dispositivo de bloques, la capa bio gestiona AIO a través de `aio_qbio` y `aio_biowakeup`. Un driver de bloques que atiende correctamente su rutina strategy también atiende AIO correctamente. De nuevo, no se necesita nada adicional.

Para un driver que tiene una ruta de hardware genuinamente asíncrona y quiere exponerla a través de AIO sin pasar por un worker thread, los hooks `d_aio_read` y `d_aio_write` de `cdevsw` existen, pero son lo suficientemente raros como para que su implementación quede fuera del alcance de este capítulo. Ese driver debería estudiar el mecanismo `fo_aio_queue` de file-ops en `/usr/src/sys/kern/vfs_aio.c` y los pocos subsistemas que lo usan.

Para cualquier otro driver, la respuesta es aún más sencilla: implementa kqueue, deja que el userland espere los eventos de manera eficiente y trata AIO como una comodidad del userland que el kernel gestiona sin la implicación del driver.

### Cerrando la sección 5

Ahora tenemos tres mecanismos de notificación asíncrona independientes en nuestro driver: `poll()`, `kqueue()` y `SIGIO`. Cada uno es relativamente pequeño por sí solo, y cada uno puede implementarse sin interferir con los demás. El patrón, en todos los casos, es el mismo: registrar el interés en la ruta de espera, entregar la notificación en la ruta de producción y tener cuidado con el locking y la limpieza.

Pero estos tres mecanismos asumen que el driver tiene una noción bien definida de "un evento está listo". Hasta ahora nuestra discusión ha sido abstracta sobre lo que es realmente un evento. En la próxima sección veremos cómo un driver organiza sus eventos internamente, de modo que una sola llamada a `read()` pueda producir un registro limpio y bien tipado en lugar del estado bruto del hardware. La cola de eventos interna es la pieza que une todo el diseño asíncrono.

## 6. Colas de eventos internas y paso de mensajes

Hasta ahora hemos tratado «hay un evento listo» como una condición imprecisa. En drivers reales, la condición suele ser concreta: hay un registro en una cola interna. El productor inserta registros, el consumidor los lee, y los mecanismos de notificación asíncrona informan al consumidor cuando la cola ha ganado o perdido registros. Diseñar bien la cola es lo que simplifica el resto del driver.

Una cola de eventos tiene varios atributos que la distinguen de un buffer de bytes en bruto. Cada entrada es un registro estructurado, no un flujo de bytes: un evento tipado con una carga útil. Las entradas se entregan completas, nunca de forma parcial: el lector recibe un registro completo o no recibe ninguno. La cola tiene un tamaño limitado, por lo que los productores deben definir una política para lo que ocurre cuando la cola se llena: descartar el registro más antiguo, descartar el más reciente, devolver un error o esperar a que haya espacio. Y la cola se consume en orden: los eventos se entregan en el mismo orden en que se insertaron, salvo que el diseño permita explícitamente otra cosa.

Diseñar la cola con cuidado tiene su recompensa en todo el driver. Un lector que recibe un flujo de registros bien tipados puede escribir código userland sencillo y robusto. Un productor que conoce la política de desbordamiento de la cola puede tomar decisiones sensatas cuando los eventos llegan más rápido de lo que pueden consumirse. Los mecanismos de notificación asíncrona (`poll`, `kqueue`, `SIGIO`) resultan todos más claros, porque cada uno puede expresar su condición en términos de si la cola está vacía o no, en lugar de en términos de un estado arbitrario propio de cada dispositivo.

### Diseño del registro de evento

La primera decisión es qué aspecto tiene un registro de evento individual. Un registro mínimo para nuestro driver `evdemo`:

```c
struct evdemo_event {
    struct timespec ev_time;    /* timestamp */
    uint32_t        ev_type;    /* event type */
    uint32_t        ev_code;    /* event code */
    int64_t         ev_value;   /* event value */
};
```

Este diseño refleja el de interfaces de eventos reales como `evdev`, y no es casualidad: una marca de tiempo más una triple combinación (tipo, código, valor) es suficiente para describir la mayoría de los flujos de eventos, desde pulsaciones de teclado hasta lecturas de sensor o eventos de botón en un mando de juego. La marca de tiempo permite que el userland reconstruya cuándo ocurrió el evento, independientemente de cuándo fue consumido, lo que importa para las aplicaciones sensibles a la latencia.

Un driver que necesite más estructura puede añadir campos, pero merece la pena defender la disciplina de mantener el registro de tamaño fijo. Un registro de tamaño fijo simplifica la gestión de memoria de la cola, convierte la ruta de lectura en una simple copia y evita los problemas de ABI que surgen cuando los registros tienen longitud variable.

### El ring buffer

La cola en sí puede ser un ring buffer sencillo de capacidad fija:

```c
#define EVDEMO_QUEUE_SIZE 64

struct evdemo_softc {
    /* ... */
    struct evdemo_event sc_queue[EVDEMO_QUEUE_SIZE];
    u_int               sc_qhead;  /* next read position */
    u_int               sc_qtail;  /* next write position */
    u_int               sc_nevents;/* count of queued events */
    u_int               sc_dropped;/* overflow count */
    /* ... */
};

static inline bool
evdemo_queue_empty(const struct evdemo_softc *sc)
{
    return (sc->sc_nevents == 0);
}

static inline bool
evdemo_queue_full(const struct evdemo_softc *sc)
{
    return (sc->sc_nevents == EVDEMO_QUEUE_SIZE);
}

static void
evdemo_enqueue(struct evdemo_softc *sc, const struct evdemo_event *ev)
{
    mtx_assert(&sc->sc_mtx, MA_OWNED);

    if (evdemo_queue_full(sc)) {
        /* Overflow policy: drop oldest. */
        sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
        sc->sc_nevents--;
        sc->sc_dropped++;
    }

    sc->sc_queue[sc->sc_qtail] = *ev;
    sc->sc_qtail = (sc->sc_qtail + 1) % EVDEMO_QUEUE_SIZE;
    sc->sc_nevents++;
}

static int
evdemo_dequeue(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_assert(&sc->sc_mtx, MA_OWNED);

    if (evdemo_queue_empty(sc))
        return (-1);

    *ev = sc->sc_queue[sc->sc_qhead];
    sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
    sc->sc_nevents--;
    return (0);
}
```

Hay varias cosas en este código que merecen destacarse.

Usamos un ring de aritmética modular simple en lugar de una lista enlazada. Esto mantiene la huella de memoria fija, evita asignaciones en el momento del evento y hace que la cola sea eficiente desde el punto de vista de las líneas de caché (dos lecturas y una escritura por operación). La mayoría de los drivers con este patrón utilizan un ring.

Llevamos la cuenta de `sc_nevents` de forma separada a los punteros de cabeza y cola. Usar solo cabeza y cola, sin un contador, lleva a la clásica ambigüedad entre "vacía" y "llena": cuando la cabeza es igual a la cola, la cola podría estar en cualquiera de los dos estados. El campo de contador resuelve la ambigüedad y abarata las rutas rápidas.

Tenemos una política de desbordamiento incorporada en `evdemo_enqueue`. Cuando la cola está llena, descartamos el evento más antiguo. Esta es la política correcta para un flujo de eventos en el que los eventos recientes son más valiosos que los obsoletos; un log de seguridad o un flujo de métricas podría preferir la contraria. También incrementamos `sc_dropped` para que el userland pueda saber cuántos eventos se perdieron.

Tanto `evdemo_enqueue` como `evdemo_dequeue` aseguran que el mutex del softc está adquirido. Esto es una red de seguridad estructural: si quien llama olvida tomar el lock, la aserción se dispara en un kernel de depuración y señala exactamente el punto de llamada incorrecto. Sin la aserción, el bug podría manifestarse solo en condiciones de temporización poco frecuentes como una corrupción silenciosa de la cola.

### La ruta de lectura

Con la cola en su lugar, el manejador `read()` síncrono se vuelve breve:

```c
static int
evdemo_read(struct cdev *dev, struct uio *uio, int flag)
{
    struct evdemo_softc *sc = dev->si_drv1;
    struct evdemo_event ev;
    int error = 0;

    while (uio->uio_resid >= sizeof(ev)) {
        mtx_lock(&sc->sc_mtx);
        while (evdemo_queue_empty(sc) && !sc->sc_detaching) {
            if (flag & O_NONBLOCK) {
                mtx_unlock(&sc->sc_mtx);
                return (error ? error : EAGAIN);
            }
            error = cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
            if (error != 0) {
                mtx_unlock(&sc->sc_mtx);
                return (error);
            }
        }
        if (sc->sc_detaching) {
            mtx_unlock(&sc->sc_mtx);
            return (0);
        }
        evdemo_dequeue(sc, &ev);
        mtx_unlock(&sc->sc_mtx);

        error = uiomove(&ev, sizeof(ev), uio);
        if (error != 0)
            return (error);
    }
    return (0);
}
```

El patrón es estándar: iterar mientras al llamador le quede espacio en el buffer, esperar un registro si la cola está vacía, sacarlo de la cola bajo el lock, liberar el lock, copiar mediante `uiomove(9)`. Gestionamos `O_NONBLOCK` devolviendo `EAGAIN` cuando la cola está vacía, y gestionamos el detach devolviendo cero (fin de archivo) para que los lectores puedan terminar limpiamente.

La llamada a `cv_wait_sig()` es una espera en variable de condición que también retorna al recibir una señal, de modo que un lector bloqueado en `read()` puede ser interrumpido por `SIGINT` u otras señales. Este es el patrón de espera interrumpible que quizá recuerdes de capítulos anteriores sobre sincronización. La variable de condición se señaliza desde la ruta del productor, que vemos a continuación.

### Integración de la ruta del productor

El productor ahora tiene tres cosas que hacer: encolar el evento, señalizar a los lectores bloqueados mediante la variable de condición, y entregar notificaciones asíncronas a través de los tres mecanismos que hemos estudiado:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    async = sc->sc_async;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

Esta es la forma canónica del productor. Todas las actualizaciones de estado y todas las notificaciones con el lock adquirido ocurren dentro del mutex del softc; las notificaciones sin el lock ocurren fuera. El orden importa: `cv_broadcast` y `KNOTE_LOCKED` con el lock adquirido ocurren antes de soltar el lock, y `selwakeup` y `pgsigio` sin el lock ocurren después.

Un detalle es el uso de `cv_broadcast()` en lugar de `cv_signal()`. Si hay varios lectores bloqueados en `read()`, normalmente queremos despertar a todos para que cada uno intente reclamar un registro. Con `cv_signal()` despertamos solo a uno, y los demás permanecen dormidos hasta que llegue otro evento. En un diseño con un único lector, `cv_signal()` sería suficiente; en el caso general, `cv_broadcast()` es más seguro.

### La integración con poll y kqueue

La ventaja de la cola de eventos interna es que `d_poll` y `d_kqfilter` se reducen a una sola expresión en términos del estado de la cola:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (!evdemo_queue_empty(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}

static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;

    mtx_assert(&sc->sc_mtx, MA_OWNED);

    kn->kn_data = sc->sc_nevents;
    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (!evdemo_queue_empty(sc));
}
```

El filtro de lectura informa `kn->kn_data` con el número de eventos encolados y devuelve verdadero siempre que la cola no esté vacía. El programa de userland ve `kn_data` y puede saber cuántos eventos están disponibles sin necesidad de llamar a `read()` todavía. Esta es una característica pequeña pero útil de la API de kqueue, y no nos cuesta nada implementarla.

### Exposición de métricas de la cola mediante sysctl

Un driver orientado al diagnóstico expone el estado de su cola a través de `sysctl(9)`. Para `evdemo` añadimos algunos contadores:

```c
SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0, "evdemo driver");

SYSCTL_UINT(_dev_evdemo, OID_AUTO, qsize, CTLFLAG_RD,
    &evdemo_qsize, 0, "queue capacity");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, qlen, CTLFLAG_RD,
    &evdemo_qlen, 0, "current queue length");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, dropped, CTLFLAG_RD,
    &evdemo_dropped, 0, "events dropped due to overflow");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, posted, CTLFLAG_RD,
    &evdemo_posted, 0, "events posted since attach");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, consumed, CTLFLAG_RD,
    &evdemo_consumed, 0, "events consumed by read(2)");
```

Estos pueden convertirse en contadores `counter(9)` para mayor eficiencia en la caché en sistemas multinúcleo, pero un simple `uint32_t` es suficiente para fines didácticos. Con estos contadores, una invocación de `sysctl dev.evdemo` muestra el estado en tiempo de ejecución de la cola de un vistazo, lo que resulta invaluable al depurar un driver que parece estar perdiendo o descartando eventos.

### Políticas de desbordamiento: una discusión de diseño

Nuestro código descarta el evento más antiguo cuando la cola se llena. Reflexionemos sobre cuándo esa es la elección correcta y cuándo no.

Descartar el más antiguo es la elección correcta cuando los eventos recientes son más valiosos que los antiguos. Una cola de eventos de interfaz de usuario es un buen ejemplo: un programa que se despertara y encontrara cien pulsaciones de teclado encoladas normalmente se interesa por las más recientes, no por las de hace cinco minutos. Un flujo de telemetría en el que cada registro tiene marca de tiempo es similar: los registros antiguos están obsoletos.

Descartar el más nuevo es la elección correcta cuando la cola representa un registro que no debe tener huecos. Un log de seguridad nunca debería perder un evento por desbordamiento; debería mejor rechazar el registro del evento más nuevo (e incrementar un contador de "descartados") antes que reescribir silenciosamente la historia.

Bloquear al productor es la elección correcta cuando el productor realmente puede esperar. Un driver cuyo productor es un manejador de interrupciones no puede bloquearse; un driver cuyo productor es una llamada `write` desde el espacio de usuario sí puede. Si el productor puede esperar, entonces una cola llena se convierte en contrapresión (back-pressure) que ralentiza al productor para que se ajuste al ritmo del consumidor, lo cual es con frecuencia exactamente lo que se quiere.

Devolver un error es la elección correcta para un protocolo de petición-respuesta en el que quien llama necesita saber de inmediato si el comando tuvo éxito. Esto es más habitual en las rutas de ioctl que en las colas de eventos, pero es una política válida.

El error más común es elegir una política sin pensar en cuál se adapta al dispositivo. Un driver que descarta eventos antiguos cuando los datos subyacentes son un log de seguridad perderá evidencias. Un driver que descarta eventos nuevos cuando la interfaz de usuario necesita capacidad de respuesta se sentirá lento. Elegir la política correcta es una decisión de diseño, y merece la pena documentarla en los comentarios del driver para que los futuros responsables del mantenimiento entiendan por qué se tomó esa decisión.

### Cómo evitar lecturas parciales

Un detalle pequeño pero importante: la ruta de lectura debe entregar un evento completo o ninguno. No debe copiar la mitad de un evento y devolver un conteo de lectura corto, porque el llamador en el userland tendría que reconstruir el evento a través de varias llamadas, lo cual es frágil y propenso a errores.

La forma más sencilla de garantizar esto es la comprobación al inicio del bucle:

```c
while (uio->uio_resid >= sizeof(ev)) {
    /* ... */
}
```

Si el buffer del usuario tiene menos bytes restantes que un evento, simplemente paramos. El llamador recibe exactamente tantos eventos completos como quepan. Si el llamador pasó un buffer de longitud cero, retornamos inmediatamente con cero bytes, que es la convención para una lectura vacía.

### Gestión de la coalescencia de eventos

Algunos drivers tienen razones legítimas para coalescer eventos. Si un teclado produce "tecla pulsada" seguido inmediatamente de "tecla liberada" para la misma tecla, el driver podría verse tentado a colapsar ambos en un único evento "tecla presionada brevemente" para ahorrar espacio en la cola. Nuestro consejo es resistir esa tentación en la mayoría de los casos. La coalescencia cambia la semántica de los eventos y puede confundir a los programas de userland escritos para esperar eventos en bruto.

Cuando la coalescencia está justificada (por ejemplo, la coalescencia de movimientos de ratón de forma que se preserve la posición final), impleméntala con cuidado y documéntala. La lógica de coalescencia debe residir en la ruta de encolado, no en la ruta del consumidor, para que todos los consumidores vean un comportamiento coherente.

### Cerrando la sección 6

La cola de eventos interna es lo que une todos los mecanismos asíncronos. Cada notificación, cada comprobación de lectura disponible, cada filtro de kqueue, cada entrega de SIGIO: todos se reducen a "¿está la cola vacía o no?" Una vez que la cola está en su lugar, el resto del driver se convierte en un asunto de conexiones, no de diseño.

En la siguiente sección veremos los patrones de diseño para combinar `poll`, `kqueue` y `SIGIO` en un único driver, y la auditoría de locking que garantiza que la combinación es correcta. Añadir cada mecanismo por separado fue la parte fácil. Conseguir que todos funcionen juntos, con un productor y muchos waiters simultáneos de distintos tipos, es donde ocurre la verdadera ingeniería de drivers.

## 7. Combinación de técnicas asíncronas

Hasta ahora hemos visto `poll`, `kqueue` y `SIGIO` de uno en uno, cada uno en su propia sección, cada uno con su propia disciplina de lock y patrón de wakeup. En un driver real, los tres mecanismos coexisten. Una única ruta de productor tiene que despertar a los durmientes en variables de condición, a los waiters de poll, a los knotes de kqueue y a los propietarios de señales, en un orden específico, bajo locks específicos, sin perder ningún wakeup y sin provocar un deadlock.

Esta sección trata de acertar con esa combinación. Es en gran medida una revisión y una consolidación: hemos visto cada mecanismo individualmente, y ahora los vemos juntos. La revisión merece hacerse porque las interacciones entre mecanismos son exactamente el lugar donde les gusta esconderse a los bugs de los drivers. Pequeñas diferencias en el orden de adquisición de locks o en el momento de las notificaciones, que no causarían un problema visible con un único mecanismo, pueden llevar a wakeups perdidos o deadlocks cuando se apilan varios mecanismos.

### Cuándo usar cada mecanismo

Un driver que admite los tres mecanismos deja que sus clientes en el userland elijan la herramienta adecuada para cada caso. Los tres mecanismos tienen diferentes puntos fuertes:

`poll` y `select` son los más portables. Un programa de userland que necesite ejecutarse sin cambios en un amplio abanico de sistemas UNIX usará `poll`. Los drivers deben implementar `poll` porque es el mínimo común denominador, y hacerlo es barato.

`kqueue` es el más eficiente y el más flexible. Los programas de userland que vigilan miles de descriptores deben usar `kqueue`. Los drivers deben implementar `kqueue` porque es el mecanismo preferido para el nuevo código FreeBSD y porque la mayoría de las aplicaciones que se preocupan por el rendimiento lo elegirán.

`SIGIO` es el más sencillo para una clase específica de programas: scripts de shell que usan `trap`, pequeños programas de un solo thread que quieren la notificación más simple posible, y código heredado. Los drivers deben implementar `SIGIO` porque el trabajo es mínimo y los casos de uso que admite son reales.

En la práctica, casi todos los drivers de caracteres para un dispositivo orientado a eventos deben implementar los tres. El código es pequeño, el mantenimiento es reducido, y la flexibilidad en el userland es alta.

### La plantilla de la ruta del productor

La ruta canónica del productor para un driver que admite los tres mecanismos es:

```c
static void
driver_post_event(struct driver_softc *sc, struct event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    enqueue_event(sc, ev);
    async = sc->sc_async;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

Cada parte de esta plantilla tiene una razón para estar donde está.

`mtx_lock` adquiere el mutex del softc. Este es el único lock que serializa todas las transiciones de estado en el driver, y todos los lectores y escritores lo respetan.

`enqueue_event` está dentro del lock. La cola es el estado compartido, y cualquier actualización sobre ella debe ser atómica respecto a otras actualizaciones y a las lecturas de estado.

`async = sc->sc_async` está dentro del lock. Esto captura una instantánea coherente del flag asíncrono para poder usarla fuera del lock sin que haya una condición de carrera.

`cv_broadcast` se llama dentro del lock. Las variables de condición exigen que el mutex asociado esté tomado en el momento de la señalización. La señal se entrega de inmediato, pero el despertar efectivo de un thread bloqueado ocurre cuando se libera el mutex.

`KNOTE_LOCKED` se llama dentro del lock. Recorre la lista de knotes y entrega notificaciones de kqueue, y espera que el lock del knlist (que es nuestro mutex del softc) esté tomado.

`mtx_unlock` libera el mutex del softc. A partir de este punto estamos fuera de la sección crítica.

`selwakeup` se llama fuera del lock. Este es el orden canónico para `selwakeup`: no debe llamarse dentro de locks arbitrarios del driver, porque adquiere sus propios locks internos.

`pgsigio` se llama fuera del lock por la misma razón.

Este orden es la disposición menos propensa a errores. Existen muchas variaciones posibles, pero cualquier desviación de este patrón debe estar justificada por una razón concreta.

### Orden de los locks

Con cuatro llamadas de notificación distintas y una actualización de estado, el orden de los locks importa. Repasemos qué locks están en juego.

El mutex del softc se adquiere primero y se mantiene a lo largo de la actualización de estado y las notificaciones dentro del lock.

`cv_broadcast` no adquiere ningún lock adicional más allá del que ya tenemos.

`KNOTE_LOCKED` evalúa el callback `f_event` de cada knote. Los callbacks se ejecutan con el lock de la knlist (nuestro mutex del softc) adquirido. Esos callbacks no deben intentar adquirir ningún lock adicional, porque hacerlo crearía una adquisición anidada que otras rutas (por ejemplo, el consumidor en `d_poll`) podrían tomar en el orden contrario. En la práctica, los callbacks `f_event` solo leen estado, que es exactamente lo que diseñamos.

`selwakeup` adquiere el mutex interno del selinfo y recorre la lista de threads aparcados, despertándolos. Esto se hace fuera del mutex del softc. Internamente, `selwakeup` también recorre la lista de knotes del selinfo, pero eso ya fue gestionado por nuestra llamada anterior a `KNOTE_LOCKED`; hacerlo dos veces es inofensivo pero innecesario, así que hacemos el `KNOTE_LOCKED` mientras tenemos el lock y dejamos que `selwakeup` se encargue solo de la lista de threads.

`pgsigio` adquiere los locks relacionados con señales y entrega la señal al proceso propietario o al grupo de procesos. Esto se hace fuera del mutex del softc.

La regla de orden de los locks es: el mutex del softc primero, nunca anidado dentro de los locks de selinfo o de señales. Mientras sigamos este orden, no podremos llegar a un deadlock.

### Los caminos del consumidor

Cada uno de los tres caminos del consumidor usa el mutex del softc de forma consistente:

```c
/* Condition-variable consumer: d_read */
mtx_lock(&sc->sc_mtx);
while (queue_empty(sc))
    cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
dequeue(sc, ev);
mtx_unlock(&sc->sc_mtx);

/* Poll consumer: d_poll */
mtx_lock(&sc->sc_mtx);
if (queue_empty(sc))
    selrecord(td, &sc->sc_rsel);
else
    revents |= POLLIN;
mtx_unlock(&sc->sc_mtx);

/* Kqueue consumer: f_event */
/* Called with softc mutex already held by the kqueue framework */
return (!queue_empty(sc));

/* SIGIO consumer: handled entirely in userland; the driver
 * only sends the signal, never consumes it */
```

Los tres consumidores comprueban la cola bajo el mutex del softc. Esto es lo que cierra la condición de carrera entre la actualización de estado del productor y la comprobación del consumidor: si el productor tiene el lock, el consumidor espera y ve el estado posterior a la actualización; si el consumidor tiene el lock, el productor espera y notifica después de que el consumidor se haya registrado.

### Errores frecuentes

Hay varios bugs concretos que aparecen con suficiente frecuencia como para nombrarlos explícitamente.

**Olvidar una de las llamadas de notificación en el productor.** La secuencia canónica parece un bloque de código de plantilla, y es fácil omitir una de las cuatro llamadas. Los tests que solo ejercitan un mecanismo pasarán, pero los demás mecanismos quedarán rotos. Las revisiones de código y los tests automatizados ayudan en este punto.

**Mantener el lock durante `selwakeup` o `pgsigio`.** El consejo del capítulo es soltar el lock antes de estas llamadas. Algunos drivers mantienen el lock por error (por ejemplo, porque el productor está en medio de un patrón de lock/unlock/lock difícil de refactorizar). El resultado es un deadlock latente que solo se manifiesta cuando un camino distinto ya tiene ese lock.

**Llamar a `cv_signal` en lugar de `cv_broadcast`.** Un driver de un único lector puede usar `cv_signal`. Un driver que permite múltiples lectores debe usar `cv_broadcast`, porque solo uno de los waiters señalados conseguirá desencolar un evento y los demás deben ver el estado actualizado para volver a dormirse. Si eliges `cv_signal` y luego permites múltiples lectores, habrás introducido un wakeup perdido latente que solo aparece bajo contención.

**Olvidar `knlist_init_mtx` en el attach.** Un driver que nunca inicializa su knlist se bloqueará en la primera llamada a `KNOTE_LOCKED`, porque los punteros a funciones de lock de la knlist son nulos. El síntoma es una desreferencia de puntero nulo dentro de `knote()`, y puede resultar confuso si olvidaste la llamada de inicialización en una refactorización.

**Olvidar `funsetown` en el close.** Un proceso que habilitó `FIOASYNC` y luego terminó sin cerrar el fd deja un `struct sigio` obsoleto. El kernel gestiona la salida del proceso a través de un `eventhandler(9)` que llama a `funsetown` por nosotros, así que normalmente esto es seguro, pero filtrar la estructura durante el close sigue siendo un bug.

**Olvidar `seldrain` y `knlist_destroy` en el detach.** Los waiters aparcados en el selinfo deben ser despertados cuando el dispositivo desaparece. Olvidar esto deja a los waiters dormidos para siempre y puede provocar un panic del kernel cuando se libera el selinfo.

### Probar el diseño combinado

La mejor manera de probar un driver que soporta los tres mecanismos es ejecutar tres programas en espacio de usuario en paralelo:

Un lector basado en `poll` que observe los eventos y los imprima.

Un lector basado en `kqueue` que haga lo mismo con `EVFILT_READ`.

Un lector basado en `SIGIO` que habilite `FIOASYNC` e imprima en cada señal.

Genera eventos en el driver a una frecuencia conocida y verifica que los tres lectores los ven todos. Si algún lector se retrasa o pierde eventos, hay un bug en el cableado de ese mecanismo. Los contadores en el lado del driver ayudan aquí: si el driver informa de 1000 eventos publicados pero un lector informa de 900 eventos vistos, una de cada diez notificaciones se está perdiendo.

Ejecutar los tres lectores a la vez contra el mismo dispositivo pone a prueba al productor de una manera que los tests de un solo mecanismo no hacen. Cualquier bug de orden de locks que solo se manifieste cuando los tres están activos aparecerá con esta carga de trabajo.

### Compatibilidad con aplicaciones

Un driver bien diseñado puede esperar funcionar con código en espacio de usuario tanto antiguo como moderno, con programas de un solo thread y con programas multihilo, con código que elige un mecanismo y con código que elige otro. La manera de conseguirlo es soportar los tres mecanismos y respetar el contrato documentado de cada uno.

El código heredado basado en `select` debería funcionar a través de nuestra implementación de `poll`, porque `select` se traduce a `poll` en el kernel.

El código moderno basado en `kqueue` debería funcionar a través de nuestro `d_kqfilter`, porque `kqueue` es el mecanismo nativo para el espacio de usuario orientado a eventos en FreeBSD.

Los programas de un solo thread que usan `SIGIO` deberían funcionar a través de nuestro manejo de `FIOASYNC`/`FIOSETOWN`.

Los programas que mezclan mecanismos (por ejemplo, observar algunos descriptores con `kqueue` y usar `SIGIO` para eventos urgentes) también deberían funcionar, porque el camino del productor del driver notifica a todos los waiters en cada evento.

Esto es lo que significa "compatibilidad con aplicaciones" para un driver. Respeta los contratos, notifica a todos los waiters, gestiona la limpieza correctamente, y el código en espacio de usuario de cualquier época funcionará.

### Cerrando la sección 7

Ahora tenemos una visión completa. Tres mecanismos asíncronos, un productor, una cola, un conjunto de locks, una secuencia de detach. El diseño combinado no requiere mucho más código que cualquier mecanismo individual por separado; el arte está en conseguir los locks y el orden correctos, y en probar la combinación para que los bugs latentes se descubran antes de que lleguen a producción.

La siguiente sección aplica este diseño combinado como una refactorización de nuestro driver `evdemo` en evolución. Auditaremos el código final, veremos qué cambió, y publicaremos el driver como la versión v2.5-async. La refactorización es donde el consejo abstracto se convierte en código fuente concreto y funcional.

## 8. Refactorización final para soporte asíncrono

Las secciones anteriores construyeron `evdemo` un mecanismo a la vez, por lo que el código que tenemos ahora es una acumulación funcional pero algo desordenada. En esta sección refactorizamos el driver como un todo coherente, con una disciplina de locking consistente, un camino de detach completo, y un conjunto de contadores expuestos que nos permiten observar su comportamiento. El resultado es el driver de ejemplo en `examples/part-07/ch35-async-io/lab06-v25-async/`, que sirve como implementación de referencia para los ejercicios de este capítulo.

Llamar a esto la refactorización "final" es ligeramente aspiracional: un driver real nunca está verdaderamente terminado. Pero refactorizar después de construir una funcionalidad es un hábito útil, porque es cuando la estructura del código se hace visible como un todo en lugar de como una serie de adiciones. Los bugs que se ocultaron durante el desarrollo incremental a menudo se vuelven obvios una vez que el código se presenta como un flujo único.

### Revisión de thread safety

Nuestra revisión comienza con el locking. Cada elemento de estado en el softc está ahora protegido por `sc_mtx`, con las siguientes excepciones:

`sc_sigio` está protegido internamente por el lock global `SIGIO_LOCK`, no por nuestro mutex del softc. Esto es correcto, porque las APIs `fsetown`, `fgetown`, `funsetown` y `pgsigio` toman el lock global ellas mismas. No debemos tomar `sc_mtx` antes de llamar a esas APIs, o invertiríamos el orden de los locks con el resto del código de señales del kernel.

`sc_rsel` está protegido internamente por su propio mutex de selinfo. No tocamos la lista interna directamente; solo llamamos a `selrecord` y `selwakeup`. Esas funciones toman el lock interno ellas mismas.

Todo lo demás (la cola, los contadores, el flag async, el flag de detaching, la cola de espera de la condition variable) está protegido por `sc_mtx`.

La auditoría consiste en verificar que cada camino del código que lee o escribe uno de estos campos toma `sc_mtx` antes del acceso y lo suelta después. Repasemos cada camino.

Attach: `sc_mtx` se inicializa antes de cualquier acceso. Todo lo demás se pone a cero. No es posible el acceso concurrente en el momento del attach porque aún no existe ningún handle al driver.

Detach: se toma `sc_mtx` para establecer `sc_detaching = true`, se emiten `cv_broadcast` y `KNOTE_LOCKED`, se suelta el lock, se llama a `selwakeup`, y se invoca `destroy_dev_drain`. Después de que `destroy_dev_drain` regrese, no pueden iniciarse más llamadas a nuestros callbacks. Entonces podemos llamar a `seldrain`, `knlist_destroy`, `funsetown`, `mtx_destroy`, `cv_destroy`, y liberar el softc.

Open: `sc_mtx` no es estrictamente necesario porque el open está serializado por el kernel, pero tomarlo para las actualizaciones de estado interno es barato y clarifica el código.

Close: `funsetown` se llama fuera de `sc_mtx`.

Read: `sc_mtx` se mantiene alrededor de la comprobación de la cola, la llamada a `cv_wait_sig`, y el dequeue. El `uiomove` se hace fuera del lock, porque `uiomove` podría provocar un page fault y no queremos mantener locks del driver durante los fallos de página.

Write: no aplica en `evdemo`, pero en un driver que acepta escrituras el patrón es simétrico.

Ioctl: `FIOASYNC` toma `sc_mtx`; `FIOSETOWN` y `FIOGETOWN` no, porque usan `fsetown`/`fgetown` que tienen su propio locking.

Poll: `sc_mtx` se mantiene durante la comprobación y la llamada a `selrecord`.

Kqfilter: el framework kqueue toma `sc_mtx` antes de llamar a nuestro callback `f_event`. Nuestro `d_kqfilter` lo toma para la llamada a `knlist_add`.

Productor (`evdemo_post_event` desde el callout): `sc_mtx` se mantiene durante el enqueue, el `cv_broadcast`, y la llamada a `KNOTE_LOCKED`; se suelta antes de `selwakeup` y `pgsigio`.

Cada lectura y escritura de cada campo del softc está asignada a `sc_mtx` o al lock externo apropiado. Esta es la auditoría que debes realizar en cada driver asíncrono, porque es la que descubre los bugs de concurrencia latentes antes de que lleguen a producción.

### La secuencia de attach completa

Juntando el camino de attach, en el orden en que deben ocurrir las llamadas:

```c
static int
evdemo_modevent(module_t mod, int event, void *arg)
{
    struct evdemo_softc *sc;
    int error = 0;

    switch (event) {
    case MOD_LOAD:
        sc = malloc(sizeof(*sc), M_EVDEMO, M_WAITOK | M_ZERO);
        mtx_init(&sc->sc_mtx, "evdemo", NULL, MTX_DEF);
        cv_init(&sc->sc_cv, "evdemo");
        knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
        callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

        sc->sc_dev = make_dev(&evdemo_cdevsw, 0, UID_ROOT, GID_WHEEL,
            0600, "evdemo");
        sc->sc_dev->si_drv1 = sc;
        evdemo_sc_global = sc;
        break;
    /* ... */
    }
    return (error);
}
```

El orden es deliberado: primero inicializar todos los primitivos de sincronización, luego registrar los callbacks (que pueden empezar a llegar en cualquier momento después de la llamada a `make_dev`), y luego publicar el softc a través de `si_drv1` y el puntero global.

Una sutileza es `M_WAITOK`. Queremos una asignación bloqueante en el momento del attach porque estamos en un contexto de carga de módulo, que siempre puede dormir. `M_ZERO` es esencial porque un selinfo, knlist o condition variable sin inicializar provocará un crash del kernel. Con estos flags, la asignación o bien tiene éxito con una estructura a cero o bien la carga del módulo falla limpiamente.

### La secuencia de detach completa

El camino de detach es más delicado, porque tenemos que coordinarnos con los llamantes en curso y los waiters activos:

```c
case MOD_UNLOAD:
    sc = evdemo_sc_global;
    if (sc == NULL)
        break;

    mtx_lock(&sc->sc_mtx);
    sc->sc_detaching = true;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);

    callout_drain(&sc->sc_callout);
    destroy_dev_drain(sc->sc_dev);

    seldrain(&sc->sc_rsel);
    knlist_destroy(&sc->sc_rsel.si_note);
    funsetown(&sc->sc_sigio);

    cv_destroy(&sc->sc_cv);
    mtx_destroy(&sc->sc_mtx);

    free(sc, M_EVDEMO);
    evdemo_sc_global = NULL;
    break;
```

La secuencia merece estudiarse porque contiene varios pasos sensibles al orden.

Establecer `sc_detaching` bajo el lock y hacer el broadcast es lo que permite que los lectores bloqueados se despierten y vean el flag. Sin esto, un lector atascado en `cv_wait_sig` dormiría para siempre porque estamos a punto de destruir la condition variable.

La llamada a `KNOTE_LOCKED` (con el camino `EV_EOF` en `f_event`) permite que cualquier waiter de kqueue vea el fin de archivo.

El `selwakeup` fuera del lock despierta a los waiters de poll. Estos regresan al espacio de usuario y ven sus descriptores de archivo volviéndose inválidos.

El `callout_drain` detiene la fuente de eventos simulada. Cualquier callout que esté a punto de dispararse se completa primero; no se inician nuevos.

`destroy_dev_drain` espera a que todos los callbacks en curso hayan retornado. Tras esto, se garantiza que `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll` y `d_kqfilter` han retornado.

`seldrain` limpia cualquier estado de selinfo que haya quedado pendiente.

`knlist_destroy` verifica que la lista de knotes esté vacía (debería estarlo, porque el `f_detach` de cada knote fue invocado al cerrar el descriptor de archivo) y libera el estado interno del lock.

`funsetown` limpia el propietario de la señal.

Por último, destruimos la variable de condición y el mutex, liberamos el softc y limpiamos el puntero global.

Este orden cuidadoso es la diferencia entre un driver que se descarga limpiamente y un driver que provoca un panic en la segunda carga. El plan de pruebas de cualquier driver serio incluye un ejercicio de «cargar y descargar cien veces en un bucle», porque las ventanas de carrera en la ruta de detach suelen ser demasiado estrechas para activarse en un solo intento.

### Exposición de métricas de eventos

El driver definitivo expone sus métricas de eventos a través de `sysctl`:

```c
SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0, "evdemo driver");

static SYSCTL_NODE(_dev_evdemo, OID_AUTO, stats,
    CTLFLAG_RW, 0, "Runtime statistics");

SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, posted, CTLFLAG_RD,
    &evdemo_posted, 0, "Events posted since attach");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, consumed, CTLFLAG_RD,
    &evdemo_consumed, 0, "Events consumed by read(2)");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, dropped, CTLFLAG_RD,
    &evdemo_dropped, 0, "Events dropped due to overflow");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, qlen, CTLFLAG_RD,
    &evdemo_qlen, 0, "Current queue length");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, selwakeups, CTLFLAG_RD,
    &evdemo_selwakeups, 0, "selwakeup calls");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, knotes_delivered, CTLFLAG_RD,
    &evdemo_knotes_delivered, 0, "knote deliveries");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, sigio_sent, CTLFLAG_RD,
    &evdemo_sigio_sent, 0, "SIGIO signals sent");
```

Cada contador se incrementa bajo el lock del softc desde el productor. Los contadores no son necesarios para el funcionamiento correcto, pero sí son imprescindibles para que el driver sea observable. Un driver que reporta cero eventos consumidos mientras la cola está llena nos indica que el lector no está vaciando la cola. Un driver que reporta más selwakeups que knotes entregados nos dice algo sobre la mezcla de waiters. Un driver que reporta muchos `sigio_sent` pero sin efecto visible en el espacio de usuario nos indica que debemos revisar el manejador de señales del propietario.

La observabilidad cuesta casi nada añadir y se amortiza con creces en la depuración en producción. Incluirla en la refactorización final es parte de lo que hace que el driver esté listo para uso real.

### Versionado del driver

Etiquetamos esta versión como `v2.5-async` en el código y en el directorio de ejemplos del libro. La convención es una declaración simple de `MODULE_VERSION`:

```c
MODULE_VERSION(evdemo, 25);
```

El número es la forma entera de la versión: 25 para la 2.5. La infraestructura de carga de módulos de FreeBSD utiliza este número para imponer restricciones de dependencia entre módulos. Un módulo que depende de `evdemo` en una versión específica puede declararlo con `MODULE_DEPEND(9)`. Para nuestro driver independiente la versión es principalmente informativa, pero actualizarla con cada nueva funcionalidad es un buen hábito.

### Cerrando la sección 8

El driver `evdemo` definitivo soporta `read()` bloqueante y no bloqueante, `poll()` con `selrecord`/`selwakeup`, `kqueue()` con `EVFILT_READ`, y `SIGIO` a través de `FIOASYNC`/`FIOSETOWN`. Dispone de una cola de eventos interna acotada con política de desbordamiento de descarte del evento más antiguo. Expone contadores a través de `sysctl` para la observabilidad. Sus secuencias de attach y detach están auditadas para la seguridad de threads. Es un driver pequeño, de unas cuatrocientas líneas de C, pero demuestra cada uno de los patrones que este capítulo ha enseñado.

Más importante aún, es una plantilla. Los patrones que has visto aquí se generalizan a cualquier driver que necesite I/O asíncrono. Un dispositivo de entrada USB sustituye el callout simulado por un callback URB real. Un driver GPIO sustituye el callout por un manejador de interrupciones real. Un pseudodispositivo de red sustituye la cola de eventos por una cadena de mbuf. El marco de notificación asíncrona (poll, kqueue, SIGIO) es el mismo en todos estos casos. Una vez que conoces el patrón, añadir soporte asíncrono a un nuevo driver es una cuestión de conexión, no de diseño.

Ya hemos cubierto el material principal del capítulo. La siguiente parte del capítulo es práctica: una secuencia de laboratorios que te guía a través de la construcción de `evdemo` tú mismo, añadiendo cada mecanismo uno por uno, y verificando el comportamiento con programas reales de espacio de usuario. Si has estado leyendo sin ejecutar código, ahora es el momento de abrir un terminal en tu máquina virtual FreeBSD y empezar a escribir.

## Laboratorios prácticos

Los laboratorios de esta sección construyen `evdemo` de forma incremental. Cada laboratorio corresponde a una carpeta dentro de `examples/part-07/ch35-async-io/` en el código fuente complementario de este libro. Puedes escribir cada laboratorio desde cero (lo cual es más lento pero construye una intuición más sólida), o partir de los fuentes proporcionados y centrarte en el código que el laboratorio enseña. Cualquiera de los dos enfoques es válido; elige el que mejor se adapte a tu estilo de aprendizaje.

Algunas notas generales antes de empezar.

Todos los laboratorios utilizan el mismo patrón de `Makefile`. Una línea `KMOD` nombra el módulo, una línea `SRCS` lista los fuentes y `bsd.kmod.mk` hace el resto. Ejecuta `make` en el directorio del laboratorio para generar `evdemo.ko`, y `sudo kldload ./evdemo.ko` para cargarlo. `make test` compila los programas de prueba de espacio de usuario en el mismo directorio.

Todos los laboratorios exponen un nodo de dispositivo en `/dev/evdemo`. Si olvidas descargar una versión anterior del driver antes de compilar una nueva, la carga fallará con el mensaje "device already exists". Ejecuta `sudo kldunload evdemo` para limpiar y, a continuación, vuelve a cargar.

Todos los laboratorios incluyen un pequeño programa de prueba que ejercita el mecanismo que el laboratorio enseña. Ejecutar el programa de prueba junto al driver verifica que el mecanismo funciona de extremo a extremo. Si un programa de prueba se queda bloqueado o reporta un error, hay algo roto en el driver, y las notas de resolución de problemas del laboratorio normalmente te ayudarán a encontrarlo.

### Laboratorio 1: Línea de base síncrona

El primer laboratorio establece una línea de base síncrona sobre la que se construyen los laboratorios posteriores. Nuestro objetivo aquí es un driver `evdemo` mínimo que soporte `read()` bloqueante sobre una cola de eventos interna. Todavía sin mecanismos asíncronos. Este laboratorio enseña las estructuras de datos de la cola y el patrón de variable de condición sobre el que se apoyará todo lo demás.

**Archivos:**

- `evdemo.c` - código fuente del driver
- `evdemo.h` - cabecera compartida con la definición del registro de eventos
- `evdemo_test.c` - lector de espacio de usuario
- `Makefile` - build del módulo y objetivo de prueba

**Pasos:**

1. Lee el contenido del directorio del laboratorio. Familiarízate con la estructura de `evdemo_softc`, especialmente los campos de la cola y la variable de condición.

2. Compila el driver: `make`.

3. Compila el programa de prueba: `make test`.

4. Carga el driver: `sudo kldload ./evdemo.ko`.

5. En un terminal, ejecuta el programa de prueba: `sudo ./evdemo_test`. El programa abre `/dev/evdemo` y llama a `read()`, que se bloqueará porque no se ha publicado ningún evento.

6. En un segundo terminal, dispara eventos: `sudo sysctl dev.evdemo.trigger=1`. El sysctl está conectado en el driver para llamar a `evdemo_post_event` con un evento sintético. El programa de prueba debería desbloquearse, imprimir el evento y volver a llamar a `read()`.

7. Dispara algunos eventos más. Observa cómo el programa de prueba imprime cada uno a medida que llega.

8. Descarga el driver: `sudo kldunload evdemo`.

**Qué observar:** La llamada `read()` en el programa de prueba se bloquea mientras la cola está vacía y devuelve exactamente un evento cada vez. El programa de prueba no hace spin en la CPU mientras espera; puedes confirmarlo observando `top -H` en un tercer terminal y comprobando que el proceso de prueba está en estado `S` (dormido) en un canal de espera llamado algo así como `evdemo` o el genérico `cv`.

**Errores comunes a verificar:** Si el programa de prueba devuelve inmediatamente con cero bytes, puede que la cola se reporte como vacía pero el camino de `read()` no esté esperando en la variable de condición. Comprueba que el bucle while en `evdemo_read` esté realmente llamando a `cv_wait_sig`. Si el programa de prueba se queda bloqueado y nunca se desbloquea incluso después de disparar un evento, comprueba que el productor esté realmente llamando a `cv_broadcast` dentro del mutex.

**Conclusión:** `read()` bloqueante con una variable de condición es la línea de base síncrona. Funciona, pero no es suficiente para los programas que necesitan vigilar múltiples descriptores o reaccionar a eventos sin tener un thread bloqueado en `read()` todo el tiempo. Los siguientes laboratorios añaden soporte asíncrono.

### Laboratorio 2: Añadir soporte para poll()

El segundo laboratorio añade `d_poll` al driver para que los programas en espacio de usuario puedan esperar sobre múltiples descriptores o integrar `evdemo` en un bucle de eventos. Este laboratorio enseña el patrón `selrecord`/`selwakeup`.

**Archivos:**

- `evdemo.c` - código fuente del driver (extendido desde el Laboratorio 1)
- `evdemo.h` - cabecera compartida
- `evdemo_test_poll.c` - programa de prueba basado en poll
- `Makefile` - build del módulo y objetivo de prueba

**Cambios en el driver respecto al Laboratorio 1:**

Añade un `struct selinfo sc_rsel` al softc.

Inicialízalo con `knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx)` durante el attach. Aunque todavía no usamos kqueue, preinicializar la knlist `si_note` es económico y hace que el selinfo sea compatible con el soporte de kqueue más adelante.

Añade un callback `d_poll`:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (!evdemo_queue_empty(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}
```

Conéctalo a la `cdevsw`:

```c
.d_poll = evdemo_poll,
```

Llama a `selwakeup(&sc->sc_rsel)` desde `evdemo_post_event` después de soltar el mutex.

Llama a `seldrain(&sc->sc_rsel)` y `knlist_destroy(&sc->sc_rsel.si_note)` durante el detach.

**Pasos:**

1. Copia el fuente del Laboratorio 1 como punto de partida.
2. Aplica los cambios anteriores.
3. Compila: `make`.
4. Compila el programa de prueba: `make test`.
5. Carga: `sudo kldload ./evdemo.ko`.
6. Ejecuta la prueba basada en poll: `sudo ./evdemo_test_poll`. Debería llamar a `poll()` con un tiempo de espera de 5 segundos e imprimir el resultado. Sin eventos publicados, `poll()` devuelve cero tras el tiempo de espera.
7. Dispara un evento mientras la prueba está en ejecución: `sudo sysctl dev.evdemo.trigger=1`. La llamada `poll()` debería devolver inmediatamente con `POLLIN` activado, y el programa debería leer el evento.
8. Prueba `poll()` con varios descriptores: el modo extendido del programa de prueba abre `/dev/evdemo` dos veces y hace poll sobre ambos descriptores. Dispara eventos y observa cuál se activa.

**Qué observar:** `poll()` se bloquea hasta que llega un evento, no hasta que expira el tiempo de espera, cuando se dispara un evento. El programa no hace spin en la CPU; está genuinamente dormido en el kernel. Puedes verificarlo con `top -H` y mirando WCHAN, que debería mostrar `select` o un canal de espera similar.

**Errores comunes a verificar:** Si el poll devuelve inmediatamente con `POLLIN` incluso cuando la cola está vacía, comprueba que tu verificación de cola vacía sea correcta. Si el poll devuelve con el tiempo de espera incluso después de disparar eventos, el productor no está llamando a `selwakeup`, o lo está llamando antes de actualizar la cola. Si el kernel entra en pánico cuando disparas un evento, el selinfo no fue inicializado correctamente; comprueba que se usó `M_ZERO` en la asignación del softc y que se llamó a `knlist_init_mtx`.

**Conclusión:** El soporte para `poll()` son unas cien líneas de código adicional y proporciona a todos los programas de espacio de usuario basados en poll la capacidad de integrar `evdemo`. La clave es la disciplina de lock: el mutex del softc serializa la comprobación y el registro en `d_poll` frente a la actualización de la cola en el productor. Sin el lock, la condición de carrera que analizamos en la Sección 3 causaría wakeups perdidos ocasionales.

### Laboratorio 3: Añadir soporte para kqueue

El tercer laboratorio añade `d_kqfilter` para que los programas que usan `kqueue(2)` puedan integrar `evdemo`. Este laboratorio enseña la estructura de operaciones de filtro y el patrón de entrega `KNOTE_LOCKED`.

**Archivos:**

- `evdemo.c` - código fuente del driver (extendido desde el Laboratorio 2)
- `evdemo.h` - cabecera compartida
- `evdemo_test_kqueue.c` - programa de prueba basado en kqueue
- `Makefile`

**Cambios en el driver respecto al Laboratorio 2:**

Añade las operaciones de filtro:

```c
static int evdemo_kqread(struct knote *, long);
static void evdemo_kqdetach(struct knote *);

static const struct filterops evdemo_read_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = evdemo_kqdetach,
    .f_event = evdemo_kqread,
};

static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;

    mtx_assert(&sc->sc_mtx, MA_OWNED);
    kn->kn_data = sc->sc_nevents;
    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (sc->sc_nevents > 0);
}

static void
evdemo_kqdetach(struct knote *kn)
{
    struct evdemo_softc *sc = kn->kn_hook;

    knlist_remove(&sc->sc_rsel.si_note, kn, 0);
}
```

Añade el callback `d_kqfilter`:

```c
static int
evdemo_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdemo_softc *sc = dev->si_drv1;

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdemo_read_filterops;
        kn->kn_hook = sc;
        knlist_add(&sc->sc_rsel.si_note, kn, 0);
        return (0);
    default:
        return (EINVAL);
    }
}
```

Conéctalo a la `cdevsw`:

```c
.d_kqfilter = evdemo_kqfilter,
```

Añade una llamada a `KNOTE_LOCKED(&sc->sc_rsel.si_note, 0)` dentro de la sección crítica del productor. Entre el `cv_broadcast` y el `mtx_unlock`.

Añade `knlist_clear(&sc->sc_rsel.si_note, 0)` al inicio del detach, antes de `seldrain`, para eliminar los knotes aún conectados que no recibieron su llamada `f_detach` (por ejemplo, porque se cerró un kqueue con el knote del dispositivo todavía conectado).

**Pasos:**

1. Copia el fuente del Laboratorio 2.
2. Aplica los cambios anteriores.
3. Compila y carga.
4. Ejecuta la prueba basada en kqueue: `sudo ./evdemo_test_kqueue`. El programa abre `/dev/evdemo`, crea un kqueue, registra `EVFILT_READ` para el dispositivo y llama a `kevent()` en modo bloqueante.
5. Dispara eventos y observa cómo el lector de kqueue los imprime.

**Qué observar:** El lector de kqueue reporta eventos a través de la API `kevent()` en lugar de a través de `poll()`. Obtiene el valor `kn_data` en `ev.data`, que le indica cuántos eventos hay en cola.

**Errores comunes a verificar:** Si el lector de kqueue devuelve inmediatamente con un error, `d_kqfilter` puede estar devolviendo `EINVAL` por un case incorrecto. Revisa la sentencia switch. Si el lector de kqueue se queda bloqueado incluso después de disparar eventos, probablemente `KNOTE_LOCKED` no esté siendo llamado, o se esté llamando fuera del lock. Si el kernel entra en pánico al descargar el módulo con quejas sobre una lista de knotes no vacía, falta `knlist_clear`.

**Conclusión:** El soporte para `kqueue` son otras cien líneas de código. La estructura es similar a `poll`: una comprobación en el callback de evento, una entrega en el productor y un paso de detach. El framework se encarga del trabajo pesado.

### Laboratorio 4: Añadir soporte para SIGIO

El cuarto laboratorio añade entrega asíncrona de señales. Este laboratorio enseña `FIOASYNC`, `fsetown` y `pgsigio`.

**Archivos:**

- `evdemo.c` - código fuente del driver (ampliado desde el Lab 3)
- `evdemo.h`
- `evdemo_test_sigio.c` - programa de prueba basado en SIGIO
- `Makefile`

**Cambios en el driver respecto al Lab 3:**

Añade el soporte asíncrono al softc:

```c
bool              sc_async;
struct sigio     *sc_sigio;
```

Añade los tres ioctls al manejador de ioctl:

```c
case FIOASYNC:
    mtx_lock(&sc->sc_mtx);
    sc->sc_async = (*(int *)data != 0);
    mtx_unlock(&sc->sc_mtx);
    break;

case FIOSETOWN:
    error = fsetown(*(int *)data, &sc->sc_sigio);
    break;

case FIOGETOWN:
    *(int *)data = fgetown(&sc->sc_sigio);
    break;
```

Añade la entrega de `pgsigio` al productor, fuera del lock:

```c
if (async)
    pgsigio(&sc->sc_sigio, SIGIO, 0);
```

Añade `funsetown(&sc->sc_sigio)` al camino de cierre y al de detach.

**Pasos:**

1. Copia el Lab 3.
2. Aplica los cambios anteriores.
3. Compila y carga.
4. Ejecuta la prueba basada en SIGIO:
   `sudo ./evdemo_test_sigio`. El programa instala un manejador de SIGIO, llama a `FIOSETOWN` con su PID, llama a `FIOASYNC` para activarlo, y luego se queda en un bucle, vaciando el driver con lecturas no bloqueantes cada vez que el manejador establece el flag.
5. Genera eventos y observa cómo el programa imprime cada uno.

**Qué observar:** Cada evento llega a través de una señal, no mediante un `read()` bloqueante ni un `poll()`. El propio manejador de señal no lee del dispositivo; establece un flag, y es el bucle principal quien lee. Este es el patrón estándar para los manejadores de SIGIO.

**Errores comunes que conviene comprobar:** Si el programa de prueba no recibe señales, es posible que `FIOASYNC` no esté activando `sc_async`, o que el productor no esté comprobando `sc_async`. Comprueba también que se haya llamado a `fsetown` antes de que el productor dispare.

Si el programa de prueba aborta con un error relacionado con SIGIO, es posible que el manejador de señal no esté instalado o que la señal esté enmascarada. Usa `sigprocmask` o `sigaction` con `SA_RESTART` si quieres que las llamadas al sistema se reinicien automáticamente tras la entrega de la señal.

**Conclusión:** SIGIO es más sencillo que poll o kqueue desde el punto de vista del driver: un manejador de ioctl, una llamada a `fsetown`, una llamada a `pgsigio`. El lado del userland es más complejo porque las señales tienen una semántica intrínsecamente delicada.

### Laboratorio 5: La cola de eventos

El quinto laboratorio se centra en la cola de eventos interna. Reorganizamos el driver para que la cola sea la única fuente de verdad para todos los mecanismos asíncronos, y añadimos introspección basada en sysctl para poder observar el comportamiento de la cola en tiempo de ejecución.

**Archivos:**

- `evdemo.c` - código fuente del driver con la implementación pulida de la cola
- `evdemo.h` - cabecera compartida con el registro de eventos
- `evdemo_watch.c` - herramienta de diagnóstico que muestra las métricas de la cola
- `Makefile`

**Qué cambia:**

Las funciones de la cola pasan a ser independientes y bien documentadas. Cada operación adquiere el mutex del softc, lo verifica con `mtx_assert` y utiliza una convención de nombres coherente.

Un subárbol `sysctl` bajo `dev.evdemo.stats` expone la longitud de la cola, el total de eventos publicados, el total de eventos consumidos y el total de eventos descartados por desbordamiento.

Un sysctl `trigger` permite al espacio de usuario publicar un evento sintético de un tipo determinado, lo que simplifica las pruebas sin necesidad de escribir y cargar un programa de prueba personalizado.

Un sysctl `burst` publica un lote de eventos de golpe, lo que ejercita el comportamiento de desbordamiento de la cola.

**Pasos:**

1. Copia el Laboratorio 4.
2. Aplica el pulido de la cola: extrae las operaciones de encolado y desencolado en funciones auxiliares con nombres claros, añade los contadores y añade las entradas sysctl.
3. Construye y carga.
4. Ejecuta `sysctl dev.evdemo.stats` en un bucle para observar el estado de la cola: `while :; do sysctl dev.evdemo.stats; sleep 1; done`.
5. Dispara ráfagas: `sudo sysctl dev.evdemo.burst=100`. Observa cómo la cola se llena y luego descarta eventos por desbordamiento cuando está llena.
6. Ejecuta cualquiera de los programas de prueba de lectura (poll, kqueue o SIGIO) mientras disparas ráfagas. Observa cómo el lector vacía la cola.

**Qué observar:** La longitud de la cola que informa sysctl registra el número de eventos que se han publicado pero que aún no se han consumido. El contador de descartados crece cuando se publican eventos mientras la cola está llena. Los contadores de publicados y consumidos divergen cuando el lector es más lento que el productor, y convergen cuando el lector lo alcanza.

**Errores comunes que hay que verificar:** Si el contador de descartados crece sin que se active la política de desbordamiento, la comprobación de llenado de la cola es incorrecta. Si el contador de publicados crece pero el de consumidos no, el productor está encolando pero el lector no está desencolando (lo cual puede ser correcto si no hay ningún lector en ejecución, pero habitualmente indica un error en la ruta de lectura).

**Conclusión:** La cola de eventos es el eje sobre el que giran los tres mecanismos asíncronos. Con la observabilidad de sysctl podemos ver directamente el comportamiento de la cola y verificar que hace lo que esperamos bajo distintas cargas.

### Laboratorio 6: El driver v2.5-async consolidado

El último laboratorio es el driver `evdemo` consolidado, con los tres mecanismos asíncronos, la disciplina de locking auditada, las métricas expuestas y la ruta de detach limpia. Esta es la implementación de referencia sobre la que se pueden modelar futuros drivers.

**Archivos:**

- `evdemo.c` - driver de referencia completo
- `evdemo.h` - cabecera compartida
- `evdemo_test_poll.c` - prueba basada en poll
- `evdemo_test_kqueue.c` - prueba basada en kqueue
- `evdemo_test_sigio.c` - prueba basada en SIGIO
- `evdemo_test_combined.c` - prueba que ejecuta los tres a la vez
- `Makefile`

**Qué demuestra este laboratorio:**

El programa de prueba combinado bifurca tres procesos hijos. Uno usa `poll`, otro usa `kqueue` y otro usa `SIGIO`. Cada hijo abre su propio descriptor de archivo hacia `/dev/evdemo` y espera eventos. El padre dispara eventos a una velocidad conocida e informa transcurrido un tiempo fijo.

**Pasos:**

1. Construye y carga.
2. Ejecuta la prueba combinada: `sudo ./evdemo_test_combined`. Bifurca los tres procesos hijos, dispara 1000 eventos a unos pocos cientos por segundo y muestra un resumen al final.
3. Verifica que los tres lectores reciben todos los eventos.

**Qué observar:** El contador de publicados en sysctl equivale a la suma de los eventos recibidos por los tres lectores. Ninguno de los mecanismos descarta eventos. Los lectores terminan con pocos milisegundos de diferencia entre sí, lo que demuestra que el driver responde a los tres simultáneamente.

**Errores comunes que hay que verificar:** Si un lector está sistemáticamente rezagado, comprueba que la notificación de su mecanismo se emite en cada evento. Si los tres lectores producen recuentos de eventos distintos, algún mecanismo está descartando notificaciones, lo que apunta a un wakeup perdido en el productor.

**Conclusión:** Un driver que implementa correctamente los tres mecanismos asíncronos atiende a cualquier llamador del espacio de usuario. Este es el objetivo al que aspirar cuando construyes un driver de producción para un dispositivo orientado a eventos. Una vez que conoces el patrón, el trabajo es mecánico.

### Laboratorio 7: Prueba de estrés de la descarga

El último laboratorio es una prueba de estrés de la ruta de detach, porque el detach es donde suelen ocultarse los errores sutiles en los drivers asíncronos.

**Archivos:**

- `evdemo.c` del Laboratorio 6
- `evdemo_stress.sh` - script de shell que carga, ejercita y descarga el driver en un bucle

**Pasos:**

1. Carga el driver.
2. En una terminal, ejecuta la prueba combinada de forma continua en un bucle.
3. En otra terminal, ejecuta el script de estrés: `sudo ./evdemo_stress.sh 100`. Este script carga, ejercita, descarga y recarga el driver cien veces seguidas, ejercitando las secuencias de attach y detach bajo lectores concurrentes.
4. Verifica que no se producen pánicos, que todos los lectores terminan limpiamente en cada ciclo de descarga y recarga, y que los contadores sysctl se reinician a cero en cada attach.

**Qué observar:** Un driver con la lógica de detach correcta puede soportar cien o mil ciclos de carga y descarga sin provocar pánico, pérdidas de memoria ni bloqueos. Un driver con un detach incorrecto generalmente entra en pánico en diez o veinte ciclos.

**Errores comunes que hay que verificar:** El error de detach más habitual es olvidarse de drenar a los llamadores en vuelo antes de liberar el softc. `destroy_dev_drain` es la herramienta canónica para esto; sin ella, una llamada `read()` o `ioctl()` en vuelo puede acceder a un softc ya liberado.

El segundo error más frecuente es un desajuste entre el orden de inicialización del attach y el detach. `knlist_init_mtx` debe ejecutarse antes de que el dispositivo sea publicado, porque una llamada `kqfilter` puede llegar inmediatamente después. De forma simétrica, `knlist_destroy` debe ejecutarse después de que el dispositivo haya sido drenado.

**Conclusión:** La prueba de estrés de la ruta de descarga es la prueba más eficaz para un driver asíncrono. Si tu driver sobrevive a 100 ciclos de carga y descarga bajo carga concurrente, probablemente sea sólido.

## Ejercicios de desafío

Estos ejercicios son opcionales. Se apoyan en los laboratorios para agudizar tus habilidades en áreas concretas. Tómate tu tiempo; no hay prisa.

### Desafío 1: Duelo de mecanismos

Modifica `evdemo_test_combined` para medir la latencia por evento de cada mecanismo: el tiempo transcurrido entre la llamada del productor a `evdemo_post_event` y el retorno de `read()` en el lector del espacio de usuario. Usa el reloj `CLOCK_MONOTONIC` y registra el tiempo en el propio registro del evento.

Genera una pequeña tabla que muestre la latencia media, mediana y percentil 99 para cada uno de `poll`, `kqueue` y `SIGIO`. Pruébalo sin contención (un lector por mecanismo) y con contención (tres lectores por mecanismo). ¿Qué mecanismo tiene la latencia más baja sin contención? ¿Y con contención?

La respuesta esperada es que `kqueue` sea el más bajo, `poll` el segundo y `SIGIO` variable (porque la latencia de entrega de señales depende del estado de ejecución actual del lector). Pero los detalles dependen de tu hardware, y el objetivo del ejercicio es medir, no predecir.

### Desafío 2: Estrés con múltiples lectores

Abre veinte descriptores de archivo hacia `/dev/evdemo` y haz polling de todos ellos a la vez desde un solo thread usando `kqueue`. Dispara 10000 eventos y verifica que cada evento se entrega a los veinte descriptores exactamente una vez.

Esto verifica que la lista de knotes del driver gestiona correctamente múltiples knotes y que `KNOTE_LOCKED` recorre la lista completa en cada evento.

### Desafío 3: Observar la condición de carrera del wakeup perdido

El tercer desafío te pide que rompas deliberadamente el driver para que puedas observar un wakeup perdido. Modifica `evdemo_post_event` para que actualice la cola y llame a las notificaciones fuera del mutex del softc en lugar de dentro:

```c
/* BROKEN: race with d_poll */
mtx_lock(&sc->sc_mtx);
evdemo_enqueue(sc, ev);
mtx_unlock(&sc->sc_mtx);
selwakeup(&sc->sc_rsel);
/* ... */
```

Esto desacopla el encolado del productor de la comprobación y el registro del consumidor. Con una tasa de eventos suficientemente alta y un consumidor ocupado, deberías ver ocasionalmente llamadas a `poll()` que retornan tras un retraso prolongado pese a haberse publicado eventos.

Intenta reproducir la condición de carrera. Mide el tiempo de las llamadas a `poll()`. Registra con qué frecuencia se activa la carrera en función de la tasa de eventos. Luego restaura el locking correcto y verifica que la carrera desaparece.

El objetivo de este ejercicio no es escribir código defectuoso. Es ver, con tus propios ojos, que la disciplina de locking que describimos en la Sección 3 no es un detalle teórico sino una propiedad real de corrección. Experimentar la condición de carrera una vez vale más que leer cien descripciones de ella.

### Desafío 4: Fusión de eventos

Añade una función de fusión de eventos a `evdemo`. Cuando el productor publique un evento de un tipo que coincida con el evento más reciente de la cola, fusiónalo con ese evento en una sola entrada con un contador incrementado en lugar de añadir una nueva entrada. Esto es similar a como algunos drivers fusionan eventos de interrupción.

Pruébalo con una ráfaga de cien eventos del mismo tipo. La longitud de la cola debería mantenerse en uno. Ahora pruébalo con cien eventos de tipos alternos: la cola debería llenarse con entradas alternadas.

El desafío radica tanto en diseñar el contrato con el espacio de usuario como en implementar la funcionalidad. ¿Qué ve el lector cuando se produce la fusión? ¿Cómo sabe que un evento fue fusionado? ¿Qué informa el campo `kn_data` de kqueue cuando la cola tiene una sola entrada que representa muchos eventos?

No hay una única respuesta correcta. Documenta tus decisiones de diseño en el código fuente y prepárate para defenderlas.

### Desafío 5: POLLHUP y POLLERR

Añade gestión elegante de `POLLHUP` y `POLLERR` al driver. Cuando el dispositivo sea desconectado mientras un programa del espacio de usuario aún lo tiene abierto, ese programa debería ver `POLLHUP` en su próxima llamada a `poll()` (junto con `POLLIN` si aún hay eventos en la cola). Cuando el driver tenga un error interno que impida operaciones futuras, debería establecer un indicador de error e informar de `POLLERR` en las llamadas a `poll()` subsiguientes.

Pruébalo provocando que el driver sea desconectado mientras un lector está haciendo polling. El lector debería despertar con `POLLHUP` y terminar limpiamente.

Esto enseña el contrato completo de `poll()` y las sutilezas del bitmask `revents`. También se solapa con la lógica de detach, que es el lugar adecuado para establecer la condición HUP.

### Desafío 6: Compatibilidad al estilo evdev

Añade una capa de compatibilidad a `evdemo` que implemente el conjunto de ioctls de evdev, de modo que tu driver sea visible para los programas del espacio de usuario que ya conocen evdev. Los ioctls clave son `EVIOCGVERSION`, `EVIOCGID`, `EVIOCGNAME` y algunos otros documentados en `/usr/src/sys/dev/evdev/input.h`.

Este es un ejercicio más extenso y genuinamente útil para entender cómo los dispositivos de entrada reales se exponen al espacio de usuario. Requiere leer el código fuente de evdev con detenimiento y elegir un subconjunto razonable para implementar.

### Desafío 7: Trazar un registro kqueue de principio a fin

Usando `dtrace(1)` o `ktrace(1)`, traza una única llamada a `kevent(2)` que registre un `EVFILT_READ` en un descriptor de archivo de `evdemo`. Tu traza debería cubrir:

- La entrada en la syscall `kevent`.
- La llamada a `kqueue_register` en el framework de kqueue.
- La invocación de `fo_kqfilter` sobre las fileops del cdev.
- La entrada en `evdemo_kqfilter` (el `d_kqfilter` de nuestro driver).
- La llamada a `knlist_add`.
- El retorno a través del framework hasta el espacio de usuario.

Captura el stack trace en cada punto. Luego dispara un evento del productor en el driver y traza la ruta de entrega:

- La llamada a `KNOTE_LOCKED` en el productor.
- La entrada en `knote` en el framework.
- La llamada a `evdemo_kqread` (nuestro `f_event`).
- El encolado de la notificación en el kqueue.

Por último, el espacio de usuario recoge el evento con otra llamada a `kevent`. Traza también esa ruta:

- La segunda entrada en la syscall `kevent`.
- La llamada a `kqueue_scan`.
- El recorrido de los knotes encolados.
- La entrega al espacio de usuario.

Envía tus trazas, anotadas con unas pocas frases que expliquen qué hace cada parte. Este ejercicio te obliga a enfrentarte directamente al código fuente del framework kqueue y es la forma más segura de pasar de «entiende los callbacks» a «entiende el framework». Un lector que complete este desafío tendrá la confianza necesaria para leer cualquier subsistema del árbol que utilice kqueue.

Consejo: `dtrace -n 'fbt::kqueue_register:entry { stack(); }'` es un punto de partida razonable. A partir de ahí, amplía la búsqueda añadiendo sondas sobre `knote`, `knlist_add`, `knlist_remove` y los puntos de entrada de tu driver, a medida que los vayas identificando en el código fuente.

### Desafío 8: Observar la disciplina de lock del knlist

Escribe un pequeño programa de prueba que abra un dispositivo `evdemo` dos veces desde dos procesos distintos, registre un `EVFILT_READ` en cada uno y luego dispare un evento productor. Usa `dtrace` para medir cuántas veces se adquiere y se libera el lock del knlist durante esa entrega única. Predice el número de antemano basándote en lo que el capítulo ha enseñado sobre `KNOTE_LOCKED` y el recorrido del knlist; luego verifica el resultado contra la traza.

A continuación, modifica `evdemo` para que el productor use `KNOTE_UNLOCKED` en lugar de `KNOTE_LOCKED` (ajustando el locking circundante para que la llamada sea segura). Repite la medición. El número de adquisiciones debería cambiar, y ese cambio debería coincidir con lo que el framework hace de forma diferente en los dos caminos de código.

Consejo: `dtrace -n 'mutex_enter:entry /arg0 == (uintptr_t)&sc->sc_mtx/ { @ = count(); }'` contará las adquisiciones del mutex sobre un mutex concreto si conoces su dirección. Puedes encontrar la dirección a través de `kldstat -v` más un poco de inspección simbólica.

## Solución de problemas frecuentes

Los errores en la E/S asíncrona tienden a caer en categorías reconocibles. Esta sección recopila los modos de fallo más comunes, sus síntomas y sus causas habituales, para que cuando te encuentres con uno puedas diagnosticarlo rápidamente.

### Síntoma: poll() nunca retorna

Una llamada a `poll()` bloquea indefinidamente aunque se estén disparando eventos.

**Causa 1:** El productor no está llamando a `selwakeup`. Añade un contador a `evdemo_post_event` y verifica que realmente se incrementa cuando se disparan eventos.

**Causa 2:** El productor está llamando a `selwakeup` antes de que se actualice el estado de la cola. Verifica que `selwakeup` se llame después de `mtx_unlock`, no antes.

**Causa 3:** El `d_poll` del consumidor no está llamando a `selrecord` correctamente. Comprueba que la llamada se realice bajo el mutex del softc y que el selinfo pasado sea el mismo que despierta el productor.

**Causa 4:** El consumidor está comprobando el estado incorrecto. Verifica que la comprobación de vaciado de cola en `d_poll` esté mirando el mismo campo que el productor actualiza.

### Síntoma: el evento de kqueue se dispara pero read() no devuelve datos

Un lector de kqueue recibe un evento `EVFILT_READ` pero una `read()` posterior devuelve `EAGAIN` o cero bytes.

**Causa 1:** La cola fue vaciada por otro lector entre la entrega del evento de kqueue y la lectura. Esto es un síntoma benigno de la contención con múltiples lectores, no un error. El lector debería iterar en `EAGAIN` y esperar al siguiente evento.

**Causa 2:** El callback `f_event` devuelve true cuando la cola está realmente vacía. Comprueba la lógica de `evdemo_kqread`.

**Causa 3:** El evento fue combinado o reclasificado después de la entrega al kqueue. Comprueba si hay alguna manipulación de la cola que pueda eliminar el evento después de que se llamara a `KNOTE_LOCKED`.

### Síntoma: SIGIO se entrega pero el manejador no se llama

El driver llama a `pgsigio`, pero el programa en espacio de usuario nunca recibe la señal.

**Causa 1:** El programa no ha instalado un manejador para `SIGIO`. Por defecto, `SIGIO` se ignora y no se entrega.

**Causa 2:** El programa ha bloqueado `SIGIO` con `pthread_sigmask` o `sigprocmask`. Comprueba la máscara de señales.

**Causa 3:** El programa llamó a `FIOSETOWN` con un PID incorrecto, por lo que la señal se envía a otro proceso. Verifica que el argumento sea el PID del proceso actual.

**Causa 4:** El driver llama a `pgsigio` solo cuando `sc_async` es true, pero el programa en espacio de usuario nunca habilitó `FIOASYNC`. Comprueba que el manejador de ioctl esté actualizando `sc_async` correctamente.

### Síntoma: pánico del kernel al descargar el módulo

El kernel entra en pánico durante `kldunload evdemo`.

**Causa 1:** Se está llamando a `knlist_destroy` sobre un knlist que todavía tiene knotes adjuntos. Añade `knlist_clear` antes de `knlist_destroy` para eliminar forzosamente los knotes restantes.

**Causa 2:** Se llama a `seldrain` antes de que los llamadores en curso hayan retornado. Llama primero a `destroy_dev_drain` y luego a `seldrain`.

**Causa 3:** La variable de condición se está destruyendo mientras un thread todavía espera en ella. Establece `sc_detaching = true` y llama a `cv_broadcast` antes de `cv_destroy`.

**Causa 4:** El softc se está liberando mientras otro thread todavía mantiene un puntero a él. Asegúrate de que el puntero global al softc se borra después de que retorne `destroy_dev_drain`, no antes.

### Síntoma: pérdida de memoria al cargar y descargar repetidamente

Tras muchos ciclos de carga y descarga, `vmstat -m` muestra asignaciones crecientes para el tipo `MALLOC_DEFINE` del driver.

**Causa 1:** El softc no se está liberando en el detach. Comprueba que se llame a `free(sc, M_EVDEMO)`.

**Causa 2:** No se está llamando a `funsetown`. Cada llamada a `fsetown` asigna una `struct sigio` que debe liberarse.

**Causa 3:** Alguna asignación interna (por ejemplo, una estructura por lector) no se está liberando al cerrar el dispositivo. Audita cada camino de asignación y confirma que cada `malloc` tiene su correspondiente `free`.

### Síntoma: poll() tarda en despertar bajo carga

Un lector basado en `poll()` normalmente despierta rápido, pero ocasionalmente tarda un tiempo significativo en ver un evento.

**Causa:** La latencia de entrega de wakeup del planificador en un sistema ocupado está en el rango de los milisegundos. Esto no es un error del driver; es una propiedad general del planificador del kernel.

Si esta latencia es inaceptable para tu caso de uso, considera `kqueue` con `EV_CLEAR`, que tiene un overhead de wakeup ligeramente menor, o usa un thread del kernel dedicado para el consumidor en lugar de un proceso en espacio de usuario.

### Síntoma: se pierden eventos bajo carga

El contador sysctl `dropped` del driver crece durante una ráfaga de eventos.

**Causa:** La cola es más pequeña que el tamaño de la ráfaga, y la política de desbordamiento (descartar el más antiguo) está entrando en acción.

Esto funciona según lo diseñado para la política predeterminada. Si tu aplicación no puede tolerar pérdidas, aumenta el tamaño de la cola o cambia la política de desbordamiento para bloquear al productor.

### Síntoma: solo un lector despierta aunque haya varios esperando

Varios lectores están bloqueados en `read()` o `poll()`, pero cuando se publica un evento solo uno de ellos despierta.

**Causa:** El productor está llamando a `cv_signal` en lugar de `cv_broadcast`. `cv_signal` despierta exactamente a un proceso en espera; `cv_broadcast` los despierta a todos.

Para un driver con múltiples lectores concurrentes, `cv_broadcast` es la elección correcta, porque cada lector puede competir por el evento y todos ellos necesitan ver el wakeup para decidir si volver a dormirse.

### Síntoma: el dispositivo se cuelga durante el detach

`kldunload` no retorna, y el kernel muestra el thread bloqueado en algún lugar de nuestro código de detach.

**Causa 1:** Una llamada está bloqueada en `d_read` y no la despertamos antes de esperar a `destroy_dev_drain`. Establece `sc_detaching`, haz broadcast y despierta el selinfo antes de llamar a `destroy_dev_drain`.

**Causa 2:** Un callout está en curso y no lo hemos drenado. Llama a `callout_drain` antes de `destroy_dev_drain`, o el callout podría volver a entrar en el driver después de que creamos haber terminado.

**Causa 3:** Un thread está detenido en `cv_wait_sig` en una condición que nunca volverá a recibir un broadcast. Asegúrate de que cada bucle de espera compruebe `sc_detaching` como condición de salida independiente.

### Síntoma: el lector despierta pero no encuentra nada que hacer

Un lector es despertado por `poll`, `kqueue` o una `read` bloqueada, pero al volver a comprobar la cola la encuentra vacía y tiene que volver a dormirse. Esto ocurre ocasionalmente incluso en un driver correcto.

**Causa:** Los wakeups espurios son una parte normal de la vida del kernel. El planificador puede entregar un wakeup destinado a otro proceso en espera, una fuente de eventos diferente que comparte el mismo `selinfo` puede haberse disparado, o una condición de carrera entre el productor y otro consumidor puede haber vaciado la cola antes de que este lector tuviera oportunidad de mirar. Ninguna de estas situaciones indica un error.

La respuesta correcta en el driver y en el lector es la misma: comprueba siempre la condición de nuevo tras despertar, y trata un wakeup como una pista de que algo puede haber ocurrido, no como una garantía de que el evento concreto que esperabas está disponible. Cada bucle de espera en el driver debería parecerse al patrón que establecimos en la Sección 3, con `cv_wait_sig` dentro de un `while` que comprueba la condición real. Cada lector en espacio de usuario debería esperar ver `EAGAIN` o una lectura de longitud cero después de un wakeup y volver a iterar para hacer poll de nuevo.

Si los wakeups sin trabajo ocurren con suficiente frecuencia como para desperdiciar CPU de forma significativa, considera si el productor está llamando a `selwakeup` más veces de las necesarias, por ejemplo en cada cambio de estado intermedio en lugar de solo cuando un evento visible para el lector está listo. Combinar los wakeups en el productor es la solución; deshabilitar el bucle de re-comprobación en el consumidor no lo es.

### Síntoma: pánico al descargar el módulo con "knlist not empty"

El camino de descarga del módulo entra en pánico con un fallo de aserción en `knlist_destroy` que muestra algo como "knlist not empty" o imprime un contador distinto de cero en el encabezado de lista del knlist.

**Causa 1:** Se llamó a `knlist_destroy` sin un previo `knlist_clear`. `knlist_destroy` verifica que la lista esté vacía; los knotes activos en la lista provocan el pánico. Inspecciona el camino de detach y confirma que `knlist_clear(&sc->sc_rsel.si_note, 0)` se ejecuta antes de `knlist_destroy`.

**Causa 2:** Un proceso en espacio de usuario todavía tiene un registro de kqueue abierto y el driver intentó desmontarse sin forzar la eliminación de los knotes. La llamada a `knlist_clear` está diseñada para manejar exactamente este caso: marca cada knote restante con `EV_EOF | EV_ONESHOT` para que el espacio de usuario vea un evento final y el registro se disuelva. Si el driver omite `knlist_clear` para dejar que el espacio de usuario se desconecte de forma natural, la aserción falla. La solución es llamar a `knlist_clear` de forma incondicional en el detach.

**Causa 3:** El camino de detach se está ejecutando mientras una entrega de eventos está en curso. El framework de kqueue usa su propio locking interno para mantener coherentes la entrega y el detach, pero un driver que desmonta su softc mientras `f_event` todavía se ejecuta en otro thread corromperá el ciclo de vida. Asegúrate de que todos los caminos del productor hayan parado (por ejemplo, estableciendo un indicador `sc_detaching` y drenando las colas de trabajo) antes de entrar en la secuencia clear-drain-destroy.

### Síntoma: pánico en f_event con un kn_hook obsoleto

El kernel entra en pánico dentro de la función `f_event` del driver con un backtrace que muestra un desreferenciado de memoria liberada o basura a través de `kn->kn_hook`.

**Causa 1:** El softc fue liberado antes de desmontar el knlist. El camino de detach del driver debe limpiar y destruir el knlist antes de liberar el softc, en ese orden. Invertir el orden deja knotes activos apuntando a memoria liberada.

**Causa 2:** Un objeto de estado por cliente (por ejemplo, un `evdev_client`) fue liberado mientras un knote todavía lo referencia. La lógica de limpieza para el estado por cliente debe ejecutar la secuencia `knlist_clear`/`seldrain`/`knlist_destroy` sobre el selinfo del cliente antes de liberar la estructura del cliente, no después.

**Causa 3:** Un camino de código diferente llamó accidentalmente a `free()` sobre el softc o el estado del cliente. Los depuradores de memoria (`KASAN` en plataformas que lo soporten, o patrones de envenenamiento instrumentados manualmente en las que no) confirmarán que la memoria está liberada cuando `f_event` la lee. Este es un ejercicio general de depuración de corrupción de memoria; el knote es la víctima, no la causa.

### Síntoma: KNOTE_LOCKED entra en pánico con una aserción de lock no adquirido

Un camino de productor que llama a `KNOTE_LOCKED` entra en pánico con una aserción como "mutex not owned" dentro de la comprobación de lock del knlist.

`KNOTE_LOCKED` es la variante que le dice al framework que omita el locking porque el llamador ya lo tiene; si el llamador no lo tiene, las aserciones del framework lo detectan. La solución es bien tomar el lock (normalmente el mutex del softc) alrededor de la llamada a `KNOTE_LOCKED`, bien usar `KNOTE_UNLOCKED` en su lugar y dejar que el framework tome el lock él mismo.

Lee el camino del productor con atención. Un error común es soltar el lock del softc a mitad de una función productora por alguna otra razón (por ejemplo, para llamar a una función que no puede invocarse bajo el lock) y luego olvidarse de volver a adquirirlo antes de la llamada a `KNOTE_LOCKED`. La solución es volver a tomar el lock o llamar a `KNOTE_UNLOCKED` en su lugar.

### Síntoma: los eventos de kqueue llegan pero kn_data es siempre cero

Un waiter de kqueue se despierta y lee un `struct kevent` cuyo campo `data` es cero, aunque el driver tiene eventos pendientes.

**Causa 1:** La función `f_event` asigna `kn->kn_data` solo bajo ciertas condiciones y, en caso contrario, lo deja sin modificar. El framework conserva el último valor escrito, por lo que un cero obsoleto de una invocación anterior persiste en la siguiente entrega. La solución consiste en calcular y asignar `kn->kn_data` incondicionalmente al comienzo de `f_event`.

**Causa 2:** La función `f_event` devuelve un valor distinto de cero basándose en una condición que no es la profundidad de la cola, y el campo `kn_data` no se actualizó para reflejar el conteo real. Comprueba que `kn_data` recibe la profundidad real, no un booleano, y que la comparación que determina el valor de retorno es coherente con dicho valor.

### Síntoma: poll() funciona pero kqueue nunca se activa

Un waiter basado en poll ve los eventos correctamente, pero un waiter de kqueue sobre el mismo descriptor de archivo nunca se despierta.

**Causa 1:** El punto de entrada `d_kqfilter` del driver no está en el cdevsw. Comprueba el inicializador de `cdevsw` y confirma que `.d_kqfilter = evdemo_kqfilter` está presente. Sin él, el framework de kqueue no tiene forma de registrar una knote sobre el descriptor.

**Causa 2:** El productor llama a `selwakeup` pero no a `KNOTE_LOCKED`. `selwakeup` sí recorre la knlist asociada al selinfo, pero solo bajo condiciones específicas; los drivers que quieren despertar a los waiters de kqueue de forma fiable deben llamar a `KNOTE_LOCKED` (o `KNOTE_UNLOCKED`) explícitamente en el camino del productor.

**Causa 3:** La función `f_event` siempre devuelve cero. Comprueba si la condición de disponibilidad se evalúa correctamente. Añade un `printf` para confirmar que se está llamando a `f_event`; si se llama pero devuelve cero, el error está en la comprobación de disponibilidad, no en el framework.

### Consejos generales

Al depurar un driver asíncrono, añade contadores con generosidad. Cada `selrecord`, cada `selwakeup`, cada `KNOTE_LOCKED`, cada `pgsigio` debería tener su propio contador. Cuando el comportamiento parece incorrecto, imprimir los contadores es la forma más rápida de saber qué mecanismo está fallando.

Usa `ktrace` en el lado del espacio de usuario para ver exactamente cuándo retornan las llamadas al sistema. Si el driver cree que entregó un wakeup en el tiempo T y el espacio de usuario cree que retornó en T+5 segundos, el wakeup fue encolado pero no entregado, lo que a menudo significa que un lock se mantuvo demasiado tiempo en algún punto.

Usa probes de DTrace en el driver y sobre el propio `selwakeup`. La probe `fbt:kernel:selwakeup:entry` muestra cada selwakeup en todo el sistema. La probe `fbt:kernel:pgsigio:entry` hace lo mismo para las entregas de señales. Una llamada ausente aparece como un hueco en la salida de la probe.

No desconfíes del framework. La infraestructura de I/O asíncrona del kernel está ampliamente probada en producción y casi nunca tiene errores a este nivel. Desconfía primero de tu propio driver, en particular del orden de los locks y de la secuencia de attach/detach.

## Cerrando

La I/O asíncrona es uno de los lugares donde la corrección de un driver se pone a prueba con más rigor. Un driver síncrono puede ocultar muchos pequeños errores de locking tras un flujo de un solo thread que, por casualidad, se ejecuta en serie. Un driver asíncrono expone cada rincón de su disciplina de locking, cada condición de carrera entre el productor y el consumidor, y cada restricción de orden sutil en el camino de detach. Conseguir que un driver asíncrono funcione correctamente es más difícil que escribir la versión síncrona, pero las recompensas son significativas: el driver sirve a muchos usuarios a la vez, se integra limpiamente con los bucles de eventos del espacio de usuario, se lleva bien con los frameworks modernos y evita las patologías de rendimiento del bloqueo y la espera activa.

Los mecanismos que hemos estudiado en este capítulo son los clásicos. `poll()` y `select()` son portables a todo sistema UNIX, e implementarlos en un driver es cuestión de un callback y un `selinfo`. `kqueue()` es el mecanismo preferido para las aplicaciones modernas de FreeBSD, y añade un callback más y un conjunto de operaciones de filtro. `SIGIO` es el mecanismo más antiguo y tiene algunos bordes problemáticos en código multi-thread, pero sigue siendo útil para scripts de shell y programas heredados.

Cada mecanismo tiene la misma forma subyacente: un waiter registra su interés, un productor detecta una condición y el kernel entrega una notificación al waiter. Los detalles difieren, pero la forma no. Comprender la forma hace que cada mecanismo específico sea más fácil de aprender. La cola de eventos interna que construimos en la Sección 6 es lo que une esa forma: cada mecanismo expresa su condición en términos del estado de la cola, y cada productor actualiza la cola antes de notificar.

La disciplina de locking es el único hábito que distingue con más consistencia un driver asíncrono funcional de uno defectuoso. Toma el mutex del softc antes de comprobar el estado. Tómalo antes de actualizar el estado. Tómalo antes de registrar un waiter. Tómalo antes de llamar a las notificaciones bajo lock (`cv_broadcast`, `KNOTE_LOCKED`). Suéltalo antes de llamar a las notificaciones fuera del lock (`selwakeup`, `pgsigio`). Este patrón no es una elección estética; es el patrón que previene los wakeups perdidos y los deadlocks. Cuando veas que el patrón se viola en un driver, pregúntate por qué, porque nueve de cada diez veces la desviación es un error.

La secuencia de detach es el segundo hábito que merece disciplina. Establece el flag de detaching bajo el lock. Lanza un broadcast para despertar a todos los waiters. Entrega un `EV_EOF` a los waiters de kqueue. Llama a `selwakeup` para liberar a los waiters de poll. Llama a `callout_drain` para detener el productor. Llama a `destroy_dev_drain` para esperar a los llamantes en curso. Solo después de todo eso puedes llamar de forma segura a `seldrain`, `knlist_destroy`, `funsetown`, `cv_destroy`, `mtx_destroy` y liberar el softc. Saltarse cualquier paso es una receta para un panic en el momento de descarga, y esos panics son especialmente dolorosos de diagnosticar porque se producen después de que el código que estabas probando ha terminado de ejecutarse.

El hábito de la observabilidad es el tercero. Cada contador que añades en tiempo de desarrollo ahorra horas de diagnóstico cuando el driver está en producción. Cada entrada sysctl que expones da a los operadores y a los depuradores una ventana al estado del driver sin necesidad de reconstruir el kernel. Cada probe de DTrace que declaras permite a un ingeniero remoto con un incidente en producción ver dentro de tu código sin necesidad de distribuir software nuevo. La observabilidad no es un lujo; es una funcionalidad, y escribir un driver sin ella es escribir un driver que no puedes depurar.

Ahora tienes todas las piezas del kit de herramientas de I/O asíncrona que un autor de drivers de FreeBSD necesita para el trabajo habitual. Puedes tomar un driver de caracteres bloqueante, auditar sus transiciones de estado, identificar los caminos del productor y del consumidor, añadir soporte para `poll`, `kqueue` y `SIGIO`, y verificar todo bajo estrés. Los patrones se generalizan más allá de los drivers de caracteres: los mismos mecanismos se aplican a pseudo-dispositivos, dispositivos de red con canales de control, sistemas de archivos con eventos de archivo y cualquier otro subsistema que exponga un flujo de eventos al espacio de usuario.

Dos notas finales antes de continuar.

Primero, la I/O asíncrona no es una lección que se aprende de una sola vez. Conforme leas más código fuente de FreeBSD, encontrarás variaciones de estos patrones en todas partes: en drivers de red que usan `grouptaskqueue`, en sistemas de archivos que usan `kqueue` para eventos de archivo, en el subsistema de auditoría que usa un buffer en anillo compartido con el espacio de usuario. Cada variación es una instancia de las mismas ideas subyacentes. Ser capaz de reconocer el patrón cuando lo ves vale más que memorizar cualquier API concreta.

Segundo, cuando escribas tu propio driver, resiste la tentación de inventar tu propio mecanismo asíncrono. Los mecanismos que proporciona el kernel cubren prácticamente todos los casos de uso, y los programas del espacio de usuario saben cómo utilizarlos. Un mecanismo personalizado es trabajo para ti, trabajo para tus usuarios y trabajo para quien mantenga el driver después. Reutiliza los patrones estándar. Existen por una razón.

## Puente hacia el Capítulo 36: cómo crear drivers sin documentación

El siguiente capítulo cambia el tipo de desafío al que nos enfrentamos. Hasta ahora, cada capítulo ha dado por sentado que el dispositivo para el que escribimos está documentado. Conocíamos sus registros, su conjunto de comandos, sus códigos de error, sus requisitos de temporización. El libro ha mostrado cómo convertir esa documentación en código del kernel funcional, y cómo probar, depurar y optimizar el resultado.

Pero no todo dispositivo está documentado. Un autor de drivers a veces se encuentra con hardware para el que no hay ninguna hoja de datos disponible, ya sea porque el fabricante se niega a publicarla, porque el hardware es tan antiguo que la documentación se ha perdido, o porque el dispositivo es una variante de algo documentado pero con cambios sin documentar. En esos casos, el oficio de escribir drivers se desplaza hacia la ingeniería inversa: observar el comportamiento del dispositivo, deducir su interfaz y producir un driver funcional a partir de evidencia indirecta en lugar de especificaciones.

El Capítulo 36 trata de ese oficio. Veremos cómo los autores con experiencia abordan un dispositivo sin documentar. Estudiaremos las herramientas para observar el comportamiento del dispositivo, desde analizadores de bus y sniffers de protocolo hasta las propias instalaciones de rastreo integradas en el kernel. Aprenderemos a construir un mapa de registros mediante experimentación, a reconocer patrones de comandos comunes entre fabricantes y a escribir un driver correcto a pesar de tener información incompleta sobre el hardware.

Los mecanismos asíncronos de este capítulo reaparecerán allí, porque el hardware orientado a eventos es exactamente el tipo de hardware que más recompensa una ingeniería inversa cuidadosa. Un dispositivo cuya documentación falta sigue comunicándose con el mundo mediante eventos, y hacer esos eventos visibles a través de `poll`, `kqueue` y `SIGIO` es a menudo el primer paso para descubrir qué está haciendo realmente el dispositivo.

Las habilidades de depuración del Capítulo 34 también serán importantes, porque un dispositivo sin documentar produce muchos más comportamientos inesperados que uno documentado, y `KASSERT`, `WITNESS` y `DTrace` son las herramientas para detectar esas sorpresas a tiempo. Los cimientos que hemos construido en las Partes 2 a 7 son exactamente lo que el capítulo de ingeniería inversa necesita.

Si llevas leyendo este libro desde el principio, tómate un momento para valorar lo lejos que has llegado. Empezaste con un árbol de código fuente vacío y sin ningún conocimiento del kernel. Ahora sabes cómo escribir un driver que admite I/O síncrona y asíncrona, gestiona la concurrencia correctamente, observa su propio comportamiento mediante contadores y probes de DTrace, y puede depurarse en un sistema en producción. A estas alturas ya has escrito suficientes drivers para que el kernel ya no sea un entorno ajeno. Es un lugar en el que sabes trabajar.

El siguiente capítulo toma ese conocimiento y se pregunta: ¿qué ocurre si falta la documentación del dispositivo? ¿Qué aspecto tiene el mismo oficio cuando trabajas a partir de evidencia en lugar de especificaciones? La respuesta es que, en realidad, el oficio cambia menos de lo que podrías pensar. Las herramientas son las mismas, las disciplinas son las mismas, y los hábitos que has construido te llevan la mayor parte del camino.

Veamos cómo funciona eso.
