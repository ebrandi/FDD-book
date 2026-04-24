---
title: "Buenas prácticas de seguridad"
description: "Implementación de medidas de seguridad en drivers de dispositivo de FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 31
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 240
language: "es-ES"
---
# Buenas prácticas de seguridad

## Introducción

Llegas al Capítulo 31 con una comprensión del entorno que pocos autores te han pedido que construyas. El Capítulo 29 te enseñó a escribir drivers que sobreviven a cambios de bus, arquitectura y tamaño de palabra. El Capítulo 30 te enseñó a escribir drivers que se comportan correctamente cuando la máquina sobre la que se ejecutan no es una máquina real, sino virtual, y cuando el proceso que los utiliza no está en el host sino dentro de un jail. Ambos capítulos han tratado sobre fronteras: la frontera entre hardware y kernel, la frontera entre host e invitado, la frontera entre host y contenedor. El Capítulo 31 te pide que examines una frontera diferente, una que está más cerca que cualquiera de las anteriores, y que es más fácil de olvidar precisamente porque atraviesa el centro de tu propio código.

La frontera de la que trata este capítulo es la que existe entre el kernel y todo aquel que se comunica con él. Programas del espacio de usuario, el propio hardware, otras partes del kernel, el bootloader pasándote un parámetro, un blob de firmware que llegó del sitio de soporte del fabricante la semana pasada, un dispositivo que empezó a comportarse de forma extraña tras una actualización. Todos ellos se encuentran al otro lado de un límite de confianza respecto al driver que escribiste. Cualquiera de ellos puede, de forma deliberada o accidental, pasar al driver algo que no coincide con sus expectativas. Un driver que respeta esta frontera es un driver en el que se puede confiar para defender el kernel. Un driver que no la respeta es un driver que, el día en que llegue algo hostil, permitirá que esa hostilidad alcance código que nunca debería haber alcanzado.

Este capítulo trata de construir ese hábito de respeto. No es un libro de texto de seguridad y no intentará convertirte en investigador de vulnerabilidades. Lo que sí hará es enseñarte a ver tu driver como lo vería un atacante o un programa descuidado, a reconocer las clases específicas de error que convierten un pequeño bug en un compromiso completo del sistema, y a utilizar los primitivos de FreeBSD correctos cuando quieras evitar que esos errores ocurran.

Unas palabras sobre lo que esto significa en la práctica. Un bug de seguridad en el kernel no es simplemente una versión peor de un bug de seguridad en el espacio de usuario. Un buffer overflow en un programa del espacio de usuario puede corromper la memoria de ese programa; un buffer overflow en un driver puede corromper el kernel, y el kernel da servicio a todos los programas del sistema. Un error off-by-one en un analizador del espacio de usuario puede hacer que este falle; el mismo off-by-one en un driver puede darle a un atacante un medio para leer memoria del kernel que contiene secretos de otro usuario, o para escribir bytes arbitrarios en las tablas de funciones del kernel. Las consecuencias no escalan linealmente con el tamaño del bug. Escalan con el nivel de privilegio del código donde vive el bug, y el kernel ocupa la cima de esa jerarquía.

Por eso la seguridad en los drivers no es un tema separado que se añade encima de las habilidades de programación que has ido construyendo. Son esas mismas habilidades de programación, aplicadas con una disciplina concreta en mente. Las llamadas `copyin()` y `copyout()` que viste en capítulos anteriores se convierten en herramientas para hacer cumplir un límite de confianza. Los flags de `malloc()` que aprendiste se convierten en formas de controlar el ciclo de vida de la memoria. La disciplina de locking que practicaste se convierte en una forma de prevenir condiciones de carrera que un atacante podría aprovechar. Las comprobaciones de privilegios que viste brevemente en el Capítulo 30 se convierten en una primera línea de defensa contra llamantes sin privilegios que intenten acceder a lugares a los que no deberían poder llegar. La seguridad es, en un sentido real, la disciplina de escribir código del kernel bien, mantenida a un estándar más alto y examinada a través de los ojos de alguien que desea que falle.

El capítulo construye esa visión en diez pasos. La sección 1 motiva el tema y explica por qué el modelo de seguridad de un driver es diferente al de una aplicación. La sección 2 trabaja la mecánica de los buffer overflows y la corrupción de memoria en código del kernel, un tema que es fácil de malentender porque el C ordinario que aprendiste en el Capítulo 4 tiene trampas que se vuelven peligrosas dentro del kernel. La sección 3 aborda la entrada del usuario, la mayor fuente individual de bugs explotables en los drivers, y recorre el uso seguro de `copyin(9)`, `copyout(9)` y `copyinstr(9)`. La sección 4 se ocupa de la asignación de memoria y su ciclo de vida, incluidos los flags específicos de FreeBSD en `malloc(9)` que son relevantes para la seguridad. La sección 5 examina las condiciones de carrera y los bugs de tipo time-of-check-to-time-of-use, una clase de problema que se encuentra en la intersección entre concurrencia y seguridad. La sección 6 aborda el control de acceso: cómo un driver debe comprobar si un llamante tiene permiso para hacer lo que solicita, utilizando `priv_check(9)`, `ucred(9)`, `jailed(9)` y el mecanismo securelevel. La sección 7 aborda las fugas de información, la sutil clase de bug en la que un driver revela datos que no pretendía revelar. La sección 8 examina el registro de eventos y la depuración, que pueden convertirse ellos mismos en problemas de seguridad si son descuidados con lo que imprimen. La sección 9 destila un conjunto de principios de diseño en torno a los valores predeterminados seguros y el comportamiento a prueba de fallos. La sección 10 completa el capítulo con pruebas, hardening y una introducción práctica a las herramientas que FreeBSD te proporciona para encontrar estos bugs antes que nadie: `INVARIANTS`, `WITNESS`, los sanitizadores del kernel, `Capsicum(4)` y fuzzing con syzkaller.

A lo largo de esas diez secciones, el capítulo también tratará el framework `mac(4)`, el papel de Capsicum en la restricción de llamantes de ioctl, los idiomas de seguridad para cadenas como `copyinstr(9)` y los problemas de compilación que vienen con las características modernas de hardening del kernel como ASLR, SSP y PIE para módulos cargables. Cada uno de estos temas aparece donde resulta más relevante, sin sacrificar nunca el hilo conductor central.

Una última cosa antes de empezar. Escribir código del kernel seguro no es cuestión de leer una lista de verificación y marcar elementos. Es una forma de leer tu propio código y un conjunto de reflejos que se vuelven automáticos con el tiempo. La primera vez que veas un driver que llama a `copyin` en un buffer de tamaño fijo en la pila sin comprobar la longitud, puede que te preguntes qué tiene de malo; la centésima vez que veas ese patrón, sentirás que se te eriza el vello de la nuca. El objetivo de este capítulo no es enseñarte cada variante de cada vulnerabilidad; eso requeriría toda una estantería de libros. El objetivo es ayudarte a desarrollar esos reflejos. Una vez que los tengas, escribirás drivers que son más seguros por defecto, y detectarás los patrones peligrosos en el código de otras personas antes de que causen problemas.

Comencemos.

## Guía para el lector: cómo usar este capítulo

El Capítulo 31 es conceptual de una forma que algunos de los capítulos anteriores no lo eran. Los ejemplos de código son breves y concretos; el valor está en el pensamiento que enseñan. Puedes recorrer todo el capítulo leyendo con atención, y saldrás de él siendo un mejor autor de drivers aunque no escribas ni una sola línea. Los laboratorios al final convierten el pensamiento en memoria muscular, y los desafíos llevan ese pensamiento a rincones incómodos donde viven los bugs reales, pero el texto en sí es la principal superficie de enseñanza del capítulo.

Si eliges el **camino solo de lectura**, planifica aproximadamente tres o cuatro horas de lectura concentrada. Al final serás capaz de reconocer las principales clases de bugs de seguridad en drivers, de explicar por qué los bugs a nivel de kernel cambian el modelo de confianza de todo el sistema, de describir los primitivos de FreeBSD que defienden contra cada clase y de esbozar cómo debería ser una versión segura de un patrón inseguro dado. Eso es una cantidad considerable de conocimiento, y para muchos lectores es donde el capítulo debería terminar en la primera lectura.

Si eliges el **camino de lectura más laboratorios**, planifica entre ocho y doce horas distribuidas en dos o tres sesiones. Los laboratorios se construyen sobre un pequeño driver pedagógico llamado `secdev` que escribirás a lo largo del capítulo. Cada laboratorio es un ejercicio breve y enfocado: en uno, corregirás un manejador de `ioctl` deliberadamente inseguro; en otro, añadirás `priv_check(9)` y observarás qué ocurre cuando procesos sin privilegios y en jail intentan usar un punto de entrada restringido; en otro, introducirás una condición de carrera, verás cómo se manifiesta bajo `WITNESS` y luego la corregirás; y en un laboratorio final, ejecutarás un fuzzer sencillo contra la superficie `ioctl` del driver y leerás los informes de fallo resultantes. Cada laboratorio te deja con un sistema funcionando y una entrada en tu cuaderno de laboratorio; ninguno es lo suficientemente largo como para agotar una tarde.

Si eliges el **camino de lectura más laboratorios más desafíos**, planifica un fin de semana largo o dos. Los desafíos llevan a `secdev` a territorio real: añadirás hooks de política MAC para que una política local del sitio pueda anular los valores predeterminados del driver, etiquetarás los ioctls del driver con derechos de capacidad para que un proceso confinado por Capsicum pueda seguir usando el subconjunto seguro, escribirás un archivo de descripción corto de syzkaller para los puntos de entrada del driver y ejecutarás las variantes sanitizadas del kernel (`KASAN`, `KMSAN`) para ver qué detectan que el build normal de `INVARIANTS` no detecta. Cada desafío es autocontenido; ninguno requiere leer capítulos adicionales para completarse.

Una nota sobre el entorno de laboratorio. Continuarás usando la máquina FreeBSD 14.3 desechable de los capítulos anteriores. Los laboratorios de este capítulo no necesitan una segunda máquina, no necesitan `bhyve(8)` y no necesitan modificar el host de formas que persistan tras un reinicio. Cargarás y descargarás módulos del kernel, escribirás en un dispositivo de caracteres de prueba, leerás `dmesg` con atención y editarás un pequeño árbol de archivos fuente. Si algo sale mal, un reinicio recuperará el host. Una instantánea o un entorno de boot sigue siendo una buena idea, y es barato crearla.

Un consejo especial para este capítulo: **lee despacio**. La prosa sobre seguridad es a veces engañosamente suave. Las ideas parecen obvias en la página, pero la razón por la que un bug determinado es un bug puede requerir un minuto de reflexión antes de que encaje. Resiste la tentación de leer por encima. Si un párrafo describe una condición de carrera que no entiendes del todo, detente y vuelve a leerlo. Si un fragmento de código demuestra una fuga de información, sigue mentalmente el camino de los bytes filtrados hasta que puedas identificar de dónde proviene cada uno. La recompensa por una lectura atenta aquí es un conjunto de reflejos que sobrevivirán a este libro.

### Requisitos previos

Debes sentirte cómodo con todo lo visto en los capítulos anteriores. En particular, este capítulo asume que ya entiendes cómo escribir un módulo del kernel cargable, cómo los puntos de entrada `open`, `read`, `write` e `ioctl` del driver se conectan a los nodos `/dev/`, cómo se asigna softc y se adjunta a un `device_t`, cómo funcionan los mutexes y los contadores de referencia al nivel que enseñaron el Capítulo 14 y el Capítulo 21, y cómo las interrupciones y los callouts interactúan con los caminos de espera. Si alguno de esos puntos no está claro, un breve repaso antes de empezar hará que los ejemplos sean más fáciles de asimilar.

También debes sentirte cómodo con la administración habitual del sistema FreeBSD: leer `dmesg`, cargar y descargar módulos, ejecutar comandos como usuario sin privilegios, crear un jail sencillo y usar `sysctl(8)` para observar y ajustar el sistema. El capítulo se referirá a estas herramientas sin explicarlas desde cero.

No se necesitan conocimientos previos de investigación de seguridad. El capítulo construye su vocabulario desde los fundamentos.

### Lo que este capítulo no cubre

Un capítulo responsable te dice qué deja fuera. Este capítulo no enseña desarrollo de exploits. No te enseña a escribir shellcode, a construir una cadena ROP ni a convertir un fallo en ejecución de código. Son temas legítimos, pero pertenecen a un tipo de libro diferente, y las habilidades que requieren no son las que te ayudan a escribir drivers más seguros.

Este capítulo no te convierte en auditor de seguridad. Auditar una base de código grande en busca de cada clase de vulnerabilidad es una disciplina distinta con sus propias herramientas y ritmos. Lo que el capítulo sí te da es la capacidad de auditar tu propio driver de forma competente, y de reconocer los patrones que merecen ser señalados en el de otra persona.

Este capítulo no reemplaza los avisos de seguridad de FreeBSD, el estándar CERT C de codificación, la guía del SEI sobre programación segura del kernel ni las páginas de manual de las API que trata. Te señala hacia esas fuentes y espera que las consultes cuando una pregunta concreta vaya más allá de lo que el capítulo puede abarcar. Cada sección principal del capítulo termina con una breve referencia a las páginas de manual pertinentes, para que tu primer punto de consulta tras el capítulo sea la propia documentación de FreeBSD.

Por último, el capítulo no pretende cubrir todas las clases de error posibles. Se centra en las que más importan en el contexto de los drivers y que pueden abordarse con las primitivas de FreeBSD que el lector ya conoce o puede aprender en unas pocas páginas. Algunas clases de error más exóticas, como los canales laterales de ejecución especulativa al estilo Spectre, se mencionan solo de pasada; pertenecen a trabajos especializados de endurecimiento que la mayoría de los autores de drivers no escribe ni debería escribir desde cero.

### Estructura y ritmo

La sección 1 construye el modelo mental: qué está en riesgo cuando un driver falla y en qué se diferencia el modelo de seguridad del driver del de una aplicación. La sección 2 aborda los desbordamientos de buffer y la corrupción de memoria en código del kernel, incluyendo las formas sutiles en que difieren de sus equivalentes en espacio de usuario. La sección 3 enseña el manejo seguro de la entrada proporcionada por el usuario mediante `copyin(9)`, `copyout(9)`, `copyinstr(9)` y primitivas relacionadas. La sección 4 cubre la asignación de memoria y su ciclo de vida: los flags de `malloc(9)`, la diferencia entre `free(9)` y `zfree(9)`, y los patrones de uso después de liberación (use-after-free) en los que caen los módulos del kernel. La sección 5 trata las condiciones de carrera y los bugs de tipo TOCTOU, incluyendo las formas relevantes para la seguridad en que se manifiestan. La sección 6 cubre el control de acceso y la aplicación de privilegios, desde `priv_check(9)` hasta `ucred(9)`, las jails y la maquinaria de securelevel. La sección 7 aborda las fugas de información y las formas sorprendentemente sutiles en que los datos se escapan. La sección 8 trata el registro y la depuración, que pueden convertirse ellos mismos en problemas de seguridad. La sección 9 recoge los principios de los valores predeterminados seguros y el diseño a prueba de fallos. La sección 10 cubre las pruebas y el endurecimiento del sistema: `INVARIANTS`, `WITNESS`, `KASAN`, `KMSAN`, `KCOV`, `Capsicum(4)`, el marco `mac(4)` y un recorrido práctico sobre cómo ejecutar syzkaller contra la superficie ioctl de un driver. Los laboratorios y ejercicios de desafío siguen a continuación, junto con un puente de cierre hacia el capítulo 32.

Lee las secciones en orden. Cada una asume la anterior, y las dos últimas secciones (secciones 9 y 10) sintetizan lo aprendido antes en consejos prácticos y un flujo de trabajo.

### Trabaja sección por sección

Cada sección de este capítulo cubre una idea central. No intentes mantener dos secciones en la cabeza al mismo tiempo. Si una sección termina y te sientes inseguro sobre alguno de sus puntos, haz una pausa antes de empezar la siguiente, vuelve a leer los párrafos finales y consulta las páginas de manual citadas. Una pausa de cinco minutos para consolidar lo aprendido es casi siempre más rápida que descubrir dos secciones después que la base no era del todo sólida.

### Mantén el driver de referencia a mano

El capítulo construye un pequeño driver pedagógico llamado `secdev` a lo largo de sus laboratorios. Lo encontrarás, junto con el código de partida, versiones intencionalmente rotas y variantes corregidas, en `examples/part-07/ch31-security/`. Cada directorio de laboratorio contiene el estado del driver en ese paso, junto con su `Makefile`, un breve `README.md` y cualquier script de soporte. Clona el directorio, escribe el código a medida que avanzas y carga cada versión después de cada cambio. Ejecutar código inseguro en tu máquina de laboratorio y observar qué ocurre es parte de la lección; no te saltes las pruebas en vivo.

### Abre el árbol de código fuente de FreeBSD

Varias secciones apuntan a archivos reales de FreeBSD. Los que vale la pena leer con detenimiento en este capítulo son `/usr/src/sys/sys/systm.h` (para las firmas exactas de `copyin`, `copyout`, `copyinstr`, `bzero` y `explicit_bzero`), `/usr/src/sys/sys/priv.h` (para las constantes de privilegio y los prototipos de `priv_check`), `/usr/src/sys/sys/ucred.h` (para la estructura de credenciales), `/usr/src/sys/sys/jail.h` (para la macro `jailed()` y la estructura `prison`), `/usr/src/sys/sys/malloc.h` (para los flags de asignación), `/usr/src/sys/sys/sbuf.h` (para el constructor de cadenas seguro), `/usr/src/sys/sys/capsicum.h` (para los derechos de capacidad), `/usr/src/sys/sys/sysctl.h` (para los flags `CTLFLAG_SECURE`, `CTLFLAG_PRISON`, `CTLFLAG_CAPRD` y `CTLFLAG_CAPWR`), y `/usr/src/sys/kern/kern_priv.c` (para la implementación de la comprobación de privilegios). Ábrelos cuando el capítulo te indique que lo hagas. El código fuente es la autoridad; el libro es una guía hacia él.

### Lleva un cuaderno de laboratorio

Continúa el cuaderno de laboratorio de los capítulos anteriores. Para este capítulo, anota una breve nota por cada laboratorio: qué comandos ejecutaste, qué módulos se cargaron, qué dijo `dmesg`, qué te sorprendió. El trabajo de seguridad, más que cualquier otro, se beneficia de un registro escrito, porque los bugs que te enseña a ver a menudo son invisibles hasta que los buscas de la forma correcta, y una entrada del cuaderno de la semana pasada puede ahorrarte una hora de redescubrimiento esta semana.

### Avanza a tu ritmo

Varias ideas de este capítulo asientan mejor la segunda vez que las encuentras que la primera. Los bits de características en virtio tenían más sentido en el capítulo 30 tras un día de descanso; lo mismo ocurre aquí con, por ejemplo, la distinción entre el manejo de errores de `copyin` y la nueva copia segura frente a TOCTOU. Si una subsección se vuelve confusa en la primera lectura, márcala, continúa y vuelve a ella. La lectura sobre seguridad premia la paciencia.

## Cómo sacar el máximo partido a este capítulo

El capítulo 31 recompensa un tipo particular de implicación. Las primitivas concretas que introduce, `priv_check(9)`, `copyin(9)`, `sbuf(9)`, `zfree(9)`, `ppsratecheck(9)`, no son decorativas; son los ladrillos del código de driver seguro. El hábito más valioso que puedes adquirir mientras lees este capítulo es el de hacerte dos preguntas en cada punto de llamada: ¿de dónde vienen estos datos, y quién tiene permitido provocar que estén aquí?

### Lee con mentalidad adversarial

La lectura orientada a la seguridad exige un cambio en la forma de mirar el código. Cuando el capítulo te muestra un driver que copia `len` bytes del espacio de usuario a un buffer, no leas el fragmento como si el valor de `len` fuera razonable. Léelo como si `len` fuera 0xFFFFFFFF. Léelo como si `len` fuera un valor cuidadosamente elegido que supera una comprobación obvia y falla en una más sutil. Lee el código de la misma manera que lo leería una persona aburrida, inteligente y malintencionada antes de irse a la cama. Esa es la lectura que encuentra bugs.

### Ejecuta lo que lees

Cuando el capítulo introduzca una primitiva, ejecuta un pequeño ejemplo de ella. Cuando muestre un patrón para `priv_check`, escribe un módulo del kernel de dos líneas que llame a `priv_check` con una constante específica y observa qué ocurre cuando invocas su ioctl desde un proceso sin privilegios de root. Cuando describa el efecto de `CTLFLAG_SECURE` sobre un sysctl, configura un sysctl ficticio en tu módulo de laboratorio, sube y baja el securelevel, y observa cómo cambia el comportamiento. El sistema en ejecución enseña lo que la prosa sola no puede.

### Escribe los laboratorios a mano

Cada línea de código en los laboratorios está ahí para enseñar algo. Escribirla tú mismo te hace ir más despacio, lo suficiente para notar la estructura. Copiar y pegar el código a menudo parece productivo y casi nunca lo es; la memoria muscular de escribir código del kernel es parte del proceso de aprendizaje. Incluso cuando un laboratorio te pide que corrijas un archivo deliberadamente inseguro, escribe tú mismo la corrección en lugar de pegar la respuesta sugerida.

### Trata `dmesg` como parte del texto

Varios de los bugs de este capítulo solo se manifiestan en la salida del registro del kernel. Un `KASSERT` que se dispara, una queja de `WITNESS` sobre una adquisición de lock fuera de orden, un aviso con limitación de tasa proveniente de tu propia llamada a `log(9)`: todos aparecen en `dmesg` y en ningún otro lugar. Vigila `dmesg` durante los laboratorios. Síguela en un segundo terminal. Copia las líneas relevantes en tu cuaderno cuando enseñen algo que no sea evidente.

### Rompe cosas deliberadamente

En varios puntos del capítulo, y explícitamente en algunos laboratorios, se te pedirá que ejecutes código inseguro para ver qué ocurre. Hazlo. Un kernel panic en tu máquina de laboratorio es una experiencia educativa económica. Descarga el módulo después de cada experimento, anota el síntoma en tu cuaderno y sigue adelante. Un panic en un sistema en producción es costoso; el objetivo de un entorno de laboratorio es precisamente darte la libertad de aprender estas lecciones donde son baratas.

### Trabaja en pareja cuando puedas

Si tienes un compañero de estudio, este es un buen capítulo para trabajar juntos. El trabajo de seguridad se beneficia enormemente de un segundo par de ojos. Uno de vosotros puede leer el código buscando bugs mientras el otro lee la prosa; luego podéis cambiar y comparar notas. Los dos modos de lectura encuentran cosas diferentes, y la conversación en sí misma es educativa.

### Confía en la iteración

No recordarás cada flag, cada constante, cada identificador de privilegio en la primera pasada. Eso está bien. Lo que importa es que recuerdes la forma del tema, los nombres de las primitivas y dónde buscar cuando surja una pregunta concreta. Los identificadores específicos se convierten en reflejo después de haber escrito dos o tres drivers con conciencia de seguridad; no son un ejercicio de memorización.

### Descansa

La lectura sobre seguridad es cognitivamente intensa de una manera diferente al trabajo de rendimiento o al trabajo de interconexión de buses. Te pide que mantengas en la cabeza un modelo de adversario mientras lees código diseñado para servir a un amigo. Dos horas de lectura concentrada seguidas de un descanso real son casi siempre más productivas que cuatro horas de esfuerzo continuo.

Con estos hábitos en su lugar, comencemos con la pregunta que enmarca todo lo demás: ¿por qué importa la seguridad en los drivers?

## Sección 1: Por qué importa la seguridad en los drivers

Es tentador pensar en la seguridad de los drivers como un subconjunto de la seguridad del software en general, con las mismas técnicas y las mismas consecuencias, solo aplicadas a una base de código diferente. Esa formulación no está del todo equivocada, pero pasa por alto lo que distingue a los drivers. La razón por la que los drivers merecen su propio capítulo sobre seguridad es que las consecuencias de un bug de seguridad en un driver son diferentes de las consecuencias del mismo bug en un programa del espacio de usuario, y las defensas también tienen un aspecto diferente. Esta sección construye el modelo mental sobre el que se apoya el resto del capítulo.

### En qué confía el kernel

El kernel es la única parte del sistema en la que se confía para hacer ciertas cosas. Es el único software que puede leer o escribir en cualquier dirección de memoria física. Es el único software que puede comunicarse directamente con el hardware. Es el único software que puede conceder o revocar privilegios a los procesos del espacio de usuario. Es el software que custodia los secretos de cada usuario y las credenciales de cada programa en ejecución. Cuando decide si una determinada solicitud debe tener éxito, nada por encima de él puede revocar esa decisión.

Ese privilegio es la razón de ser del kernel. Sin él, el kernel no podría imponer los límites que hacen posible un sistema multiusuario. Con él, el kernel asume una responsabilidad que ningún programa del espacio de usuario asume: cada línea de código del kernel se ejecuta con la autoridad de todo el sistema, y cada bug en el código del kernel puede, en principio, escalar hasta convertirse en la autoridad de todo el sistema.

Un driver es parte del kernel. Una vez cargado, el código de un driver se ejecuta con el mismo privilegio que el resto del kernel. No existe ningún límite más fino dentro del kernel que diga "este código es solo un driver, así que no puede tocar el planificador". Una desreferencia de puntero en tu driver, si cae en la dirección equivocada, puede corromper cualquier estructura de datos que el kernel utilice. Un desbordamiento de buffer en tu driver, si es lo suficientemente grande, puede sobreescribir cualquier puntero de función que el kernel utilice. Un valor no inicializado en tu driver, si fluye al lugar correcto, puede filtrar los secretos de un vecino. El kernel confía plenamente en el driver, porque no tiene ningún mecanismo para desconfiar de él.

Esa asimetría es lo primero que hay que interiorizar. Los programas del espacio de usuario se ejecutan bajo el kernel, y el kernel puede imponerles reglas. Los drivers se ejecutan dentro del kernel, y nadie les impone reglas excepto los propios autores del driver.

### Un bug en el kernel cambia el modelo de confianza

Un bug en un programa del espacio de usuario es un bug. Un bug en el kernel, y en particular en un driver al que puede acceder un proceso sin privilegios, a menudo es algo peor: es un cambio en el modelo de confianza de todo el sistema. Esta es la idea más importante de este capítulo, y vale la pena detenerse en ella.

Considera un pequeño bug en un editor de texto del espacio de usuario: un error off-by-one que escribe un byte de más en un buffer. En el peor caso, el editor se bloquea. Quizás el usuario pierde unos minutos de trabajo. Quizás el sandbox del editor captura el bloqueo y el impacto es aún menor. Las consecuencias están acotadas por lo que ese usuario ya podía hacer; el editor se estaba ejecutando con los privilegios del usuario, por lo que el daño no puede escapar de esos privilegios.

Ahora imagina el mismo error off-by-one en el manejador `ioctl` de un driver. Si el driver es accesible desde procesos sin privilegios, un usuario sin privilegios puede provocar el off-by-one. El byte de más aterriza en memoria del kernel. Dependiendo del lugar donde caiga, podría invertir un bit en una estructura que el kernel utiliza para decidir quién tiene permiso para hacer qué. Un atacante hábil puede maniobrar para que ese cambio de bit modifique qué proceso tiene privilegios de root. En ese momento, el off-by-one ya no es un crash, sino una escalada de privilegios. El usuario sin privilegios se convierte en root. El modelo de confianza del sistema, que asumía que solo los usuarios autorizados eran root, deja de ser válido.

Esto no es una hipótesis exagerada. Es la forma habitual en que los errores del kernel se convierten en exploits. Las estructuras de datos del kernel se encuentran próximas entre sí en memoria. Un atacante que pueda escribir un único byte en algún punto del kernel puede, con suficiente ingenio, dirigir ese byte hacia una estructura que importe. Unos pocos bytes fuera de lugar en la estructura de datos adecuada se convierten en un exploit funcional. Unos pocos bytes en el lugar correcto pueden marcar la diferencia entre "mi editor se ha colgado" y "el atacante ahora controla la máquina".

Por eso el enfoque mental sobre la seguridad debe cambiar cuando pasas del espacio de usuario al kernel. No te preguntas "¿qué es lo peor que puede ocurrir si este código falla?". Te preguntas "¿qué es lo peor que alguien podría hacerle al sistema si pudiera dirigir este código para que fallara exactamente como quisiera?". Son preguntas distintas, y la segunda es siempre la correcta dentro del kernel.

### Un catálogo parcial de lo que está en juego

Conviene hacer lo abstracto concreto. Si un driver tiene un bug que puede activarse desde el espacio de usuario, ¿qué está en riesgo exactamente? La lista es larga. A continuación se presentan las categorías principales, como forma de fijar las consecuencias en tu mente antes de que el capítulo pase a las clases específicas de bug.

**Escalada de privilegios.** Un usuario sin privilegios obtiene privilegios de root, o un usuario en una jaula obtiene privilegios a nivel de host, o un usuario dentro de un sandbox en modo capability obtiene privilegios fuera de ese sandbox.

**Lectura arbitraria de memoria del kernel.** Un atacante lee memoria del kernel que no debería ver. Esto incluye claves criptográficas, hashes de contraseñas, contenidos de archivos de otros usuarios que se encuentran en la caché de páginas, y las propias estructuras de datos del kernel que revelan dónde reside otra memoria de interés.

**Escritura arbitraria de memoria del kernel.** Un atacante escribe en memoria del kernel donde no debería poder escribir. Esto suele ser la base de la escalada de privilegios, porque puede utilizarse para modificar estructuras de credenciales, punteros a funciones u otro estado crítico para la seguridad.

**Denegación de servicio.** Un atacante provoca un panic en el kernel, hace que se cuelgue o lo lleva a consumir tantos recursos que el sistema deja de ser útil. Los drivers que pueden inducir un bucle indefinido, asignar memoria sin límite o alcanzar un `KASSERT` a partir de una entrada de usuario son fuentes de DoS.

**Fuga de información.** Un atacante obtiene información que no debería conocer: un puntero del kernel (lo que anula la efectividad de KASLR), el contenido de un buffer no inicializado (que puede contener datos de llamantes anteriores) o metadatos sobre otros procesos o dispositivos del sistema.

**Persistencia.** Un atacante instala código que sobrevive a los reinicios, generalmente escribiendo en un archivo que el kernel cargará en el próximo boot o corrompiendo una estructura de configuración.

**Escape de sandbox.** Un atacante confinado en una jaula, en un guest de VM o en un sandbox de Capsicum en modo capability escapa de su confinamiento aprovechando un bug en un driver.

Cada uno de estos es una consecuencia plausible de un único error plausible en un driver. El error es con frecuencia algo que parecía inofensivo al autor: una comprobación de longitud olvidada, una estructura que no se puso a cero antes de copiarse al espacio de usuario, una condición de carrera entre dos rutas que parecían mutuamente excluyentes. El objetivo de este capítulo es ayudarte a detectar esos errores antes de que se conviertan en cualquiera de los elementos de esa lista.

### Incidentes reales

Todos los kernels importantes tienen un historial de incidentes de seguridad relacionados con drivers. FreeBSD no es una excepción. Sin convertir esto en un ejercicio de arqueología de vulnerabilidades, vale la pena mencionar algunos tipos de incidente que resultan especialmente instructivos.

Existe el clásico **ioctl sin comprobación de privilegios**, en el que un driver expone un ioctl que realiza algo que un usuario sin privilegios de root no debería poder hacer, pero se olvida de llamar a `priv_check(9)` o equivalente antes de ejecutarlo. La corrección es añadir una sola línea; el bug puede permitir la ejecución de código arbitrario como root. Este patrón ha aparecido en múltiples kernels a lo largo de las décadas.

Existe la **fuga de información por memoria no inicializada**, en la que un driver asigna una estructura, rellena algunos campos y copia la estructura al espacio de usuario. Los campos que el driver no rellenó contienen lo que el asignador de memoria devolvió en ese momento, que puede incluir datos del último llamante. Con el tiempo, los atacantes han podido extraer punteros del kernel, contenidos de archivos y claves criptográficas de esta clase de bug.

Existe el **desbordamiento de buffer en una ruta aparentemente inocente**, en el que un driver analiza una estructura a partir de un blob de firmware o un descriptor USB sin comprobar los campos de longitud que el dato declara. Un atacante que pueda controlar el firmware (por ejemplo, conectando un dispositivo USB malicioso) puede provocar el desbordamiento. Esta clase de bug es especialmente perniciosa porque el atacante puede ser físico: conecta un USB y se marcha.

Existe la **condición de carrera entre `open` y `read`**, en la que dos threads abren y leen un dispositivo simultáneamente, y la máquina de estados del driver tiene un hueco en su sincronización. El segundo thread observa un estado a medio inicializar y provoca un crash o, peor aún, puede continuar y ve datos que deberían haber sido borrados.

Existe el **bug TOCTOU**, en el que un driver valida un valor en una estructura del espacio de usuario y luego confía en que ese valor sigue siendo el mismo. Entre la comprobación y el uso, el programa en espacio de usuario ha cambiado el valor, y el driver opera ahora sobre datos que nunca validó.

Cada uno de estos bugs es evitable. Cada uno tiene una primitiva bien conocida de FreeBSD que lo previene. El capítulo está estructurado en torno a enseñar esas primitivas en el orden adecuado.

### La mentalidad de seguridad

Un tema recurrente en este capítulo es que el código seguro proviene de una forma de pensar particular, no de un conjunto particular de técnicas. Las técnicas importan; necesitas conocerlas. Pero las técnicas sin la mentalidad producen código que es seguro frente a los bugs específicos que el autor tuvo en mente, e inseguro frente a todos los bugs que no tuvo. La mentalidad, aplicada de forma consistente, sigue produciendo código seguro incluso cuando las técnicas son imperfectas.

La mentalidad tiene tres hábitos. El primero es **asumir lo peor de cada entrada**. Cada byte que lees del espacio de usuario, de un dispositivo, de firmware, de un bus, de un sysctl o de un loader tunable podría ser el peor byte que un atacante habría podido elegir. No porque la mayoría de las entradas sean adversariales, sino porque el código seguro debe funcionar correctamente incluso cuando lo son. El segundo es **asumir lo mínimo sobre el entorno**. No asumir que el llamante es root solo porque la configuración de pruebas lo hizo root; compruébalo. No asumir que un campo en una estructura fue puesto a cero solo porque el último escritor dijo que sí; ponlo a cero tú mismo. No asumir que el sistema está en securelevel 0; compruébalo. El tercero es **fallar cerrando en lugar de abriendo**. Cuando algo está mal, devuelve un error. Cuando algo falta, niégate a continuar. Cuando una comprobación falla, detente. Un driver que tiende a no funcionar cuando las reglas no están claras es un driver difícil de explotar; un driver que tiende a funcionar de todas formas es un driver esperando ser atacado.

Esos tres hábitos no necesitan memorización. Necesitan ser interiorizados. Este capítulo es un taller para interiorizarlos.

### Ni siquiera root es de confianza

Un punto concreto que los principiantes a veces pasan por alto: incluso cuando el llamante es root, el driver debe seguir validando su entrada. Esto puede parecer contraintuitivo. Si el llamante es root, ya puede hacer cualquier cosa; ¿qué sentido tiene validar su entrada?

La cuestión es que «el llamante es root» es una afirmación sobre autorización, no sobre corrección. Un usuario root puede pedirle a tu driver que haga algo, y el kernel dirá que sí. Pero un usuario root también puede ser un programa con bugs que está pasando la longitud incorrecta por error. Un usuario root puede ser un programa comprometido del que un atacante se ha apoderado. Un usuario root puede estar ejecutando un script descuidado que trata un puntero como una longitud. En cada uno de estos casos, tu driver debe seguir comportándose de forma sensata.

En concreto, si root te pasa un `len` de 0xFFFFFFFF en un argumento de `ioctl`, el comportamiento correcto es devolver `EINVAL`, no hacer alegremente un `copyin` de cuatro gigabytes de memoria de usuario en un buffer del kernel. Root no quería realmente eso; root estaba ejecutando un programa que tenía un bug. El trabajo de tu driver es evitar que ese bug se convierta en un bug del kernel.

Por eso la validación de entradas es universal. No se trata de desconfiar del llamante; se trata de que el driver se proteja a sí mismo y al resto del kernel de errores, ya sean deliberados o accidentales, provenientes de quien sea.

### Dónde están los límites

Un driver vive entre varios límites. Vale la pena nombrarlos explícitamente, porque diferentes clases de bug habitan en diferentes límites y las defensas son distintas.

El **límite usuario-kernel** separa el userland del kernel. Los datos que cruzan del espacio de usuario al kernel deben ser validados; los datos que cruzan del kernel al espacio de usuario deben ser saneados. `copyin(9)` y `copyout(9)` son el mecanismo principal para cruzar este límite de forma segura. Las secciones 3, 4 y 7 de este capítulo tratan este límite.

El **límite driver-bus** separa el driver del hardware. Los datos leídos de un dispositivo no son siempre de confianza; un dispositivo malicioso o un bug de firmware puede presentar valores que el driver no esperaba. Los campos de longitud en los descriptores, por ejemplo, deben estar acotados por las propias expectativas del driver y no por los valores que el dispositivo declara. La sección 2 toca este punto.

El **límite de privilegios** separa diferentes niveles de autoridad: root de no-root, el host de una jaula, el kernel de un sandbox en modo capability. Las comprobaciones de privilegios refuerzan este límite. La sección 6 lo trata en profundidad.

El **límite módulo-módulo** separa tu driver de otros módulos del kernel. Es el menos defendido de los límites, porque el kernel confía completamente en sus propios módulos por defecto. Esta es una de las razones por las que la sección anterior habla del radio de daño de un bug en un driver: casi siempre es mayor que el propio driver.

### El lugar de este capítulo

Los capítulos 29 y 30 enseñaron el entorno en dos sentidos: arquitectónico y operativo. El capítulo 29 te enseñó a hacer el driver portable entre buses y arquitecturas. El capítulo 30 te enseñó a hacer que se comportara correctamente en entornos virtualizados y en contenedores. El capítulo 31 enseña un tercer tipo de entorno: la política. Es decir, las elecciones relevantes para la seguridad que toma el administrador y que el adversario intenta vulnerar. Tomados en conjunto, los tres capítulos describen el entorno alrededor de un driver de FreeBSD en tiempo de ejecución, y lo que el autor del driver debe hacer para ser un ciudadano responsable de ese entorno.

El hilo continúa. El capítulo 32 se ocupará de Device Tree y del desarrollo embebido, lo que puede parecer un cambio de tema pero es en realidad el mismo hilo llevado a nuevo hardware. Los hábitos de seguridad que aprendes aquí te acompañan a cada placa ARM, a cada sistema RISC-V, a cada objetivo embebido donde la disciplina de privilegios y recursos de un driver importa tanto como en un escritorio. Los capítulos posteriores profundizarán en la historia de la depuración, incluyendo algunas de las técnicas que este capítulo introduce a un nivel general. Los hábitos de seguridad que construyes ahora te servirán durante el resto del libro y durante el resto de tu carrera como autor de drivers.

### Cerrando la sección 1

Un driver es parte del kernel. Cada bug en un driver es un potencial bug del kernel, y cada bug del kernel es un potencial cambio en el modelo de confianza del sistema. Dado que el radio de daño es tan amplio, el listón para la corrección en un driver es más alto que el listón en un programa de espacio de usuario. Las secciones restantes del capítulo recorren las clases específicas de bug que más importan en los drivers y las primitivas de FreeBSD que defienden contra ellas.

La única frase que debes recordar de esta sección es esta: **en un driver, los bugs no son solo errores; son cambios en quién puede hacer qué en el sistema**. Mantén esa perspectiva en mente mientras lees el resto del capítulo, y cada una de las demás frases será más fácil de seguir.

Con las consecuencias a la vista, pasamos a la primera clase concreta de bug: los desbordamientos de buffer y la corrupción de memoria dentro del código del kernel.

## Sección 2: Cómo evitar los desbordamientos de buffer y la corrupción de memoria

Los desbordamientos de buffer y sus variantes, las lecturas y escrituras fuera de límites, son la clase de bug de seguridad más antigua y siguen siendo una de las más comunes. Aparecen en código de espacio de usuario, en código del kernel y en cualquier lenguaje que no imponga límites en el propio nivel del lenguaje. C es uno de esos lenguajes, y el C del kernel es ese mismo lenguaje con aristas más afiladas, de modo que los drivers son terreno fértil para los bugs de buffer.

Esta sección explica cómo aparecen estos bugs en el código del kernel, por qué son a menudo peores que sus equivalentes en espacio de usuario y cómo escribir código de driver que los evite por construcción. Parte de la base de que el lector recuerda el material de C del capítulo 4 y el material de C del kernel de los capítulos 5 y 14. Si alguno de esos puntos está flojo, una revisita breve antes de leer esta sección valdrá la pena.

### Un breve repaso sobre los buffers

Un buffer en C es una región de memoria con un tamaño específico. En un driver, los buffers proceden de varios lugares. Los buffers asignados en la pila se declaran como variables locales dentro de funciones; su vida útil es la duración de la llamada a la función, y su asignación y liberación son prácticamente gratuitas. Los buffers asignados en el heap provienen de `malloc(9)` o de `uma_zalloc(9)`; persisten mientras el driver conserve un puntero a ellos. Los buffers asignados de forma estática se declaran en el ámbito de archivo; persisten durante toda la vida útil del módulo. Cada uno de ellos tiene propiedades distintas y riesgos propios.

Lo que todos los buffers tienen en común es un tamaño. Escribir más allá del final del buffer, leer antes de su inicio o indexarlo con un valor que no cabe se denomina **buffer overflow** (o underflow). El overflow en sí es el mecanismo; lo que escribe y dónde aterriza esa escritura determinan la gravedad.

Un desbordamiento de pila en un driver es el tipo más peligroso, porque la pila contiene las direcciones de retorno, los registros guardados y las variables locales de toda la cadena de llamadas. Una escritura más allá del final de un buffer de pila puede alcanzar la dirección de retorno del llamador y, a partir de ahí, provocar la ejecución de código arbitrario. Un desbordamiento de heap es menos directamente explotable, pero los buffers de heap están con frecuencia adyacentes a otras estructuras de datos del kernel, y un desbordamiento de heap que alcanza la estructura adecuada constituye un camino claro hacia la compromisión del sistema. Un desbordamiento de buffer estático es el menos frecuente, pero puede conducir igualmente a la compromisión si el buffer estático está junto a otros datos modificables del módulo.

El vocabulario de desbordamiento de «stack» y «heap» resultará familiar a quienes hayan trabajado en espacio de usuario. El mecanismo es el mismo. Las consecuencias son peores, porque el código y los datos del kernel comparten un espacio de direcciones con todo lo demás que hace.

### Cómo se producen los desbordamientos en el código del kernel

Los desbordamientos no ocurren porque los autores escriban en memoria que no pretendían tocar. Ocurren porque el autor escribe en memoria que sí pretendía escribir, pero la longitud o el desplazamiento son incorrectos. Las formas más habituales de este error son:

**Confiar en una longitud proveniente del espacio de usuario.** El argumento `ioctl` del driver contiene un campo de longitud, y el driver usa esa longitud para decidir cuánto copiar con `copyin` o cuán grande debe ser el buffer a asignar. Si la longitud no está acotada, el usuario puede elegir un valor que provoque un comportamiento incorrecto en la copia.

**Error de uno en un bucle (off-by-one).** Un bucle que itera sobre un array usa `<=` donde se pretendía `<`, o `<` donde se pretendía `<=`. La iteración extra accede a memoria justo más allá del final del array.

**Tamaño de buffer incorrecto en una llamada.** Una llamada a `copyin`, `strlcpy`, `snprintf` u otras similares recibe un argumento de tamaño. El autor pasa `sizeof(buf)` cuando `buf` es un puntero, lo que devuelve el tamaño del puntero (cuatro u ocho bytes) en lugar del tamaño del buffer. La llamada escribe muchos más bytes de los previstos.

**Desbordamiento aritmético en el cálculo de una longitud.** El autor multiplica o suma longitudes para calcular el tamaño de un buffer, y la multiplicación desborda un entero de 32 bits. El «tamaño» resultante es pequeño, la asignación tiene éxito y la copia posterior escribe mucho más de lo que se asignó.

**Truncar una cadena sin añadir el terminador null.** El autor usa `strncpy` u otra función similar, pero `strncpy` no garantiza un terminador null; una operación de cadena posterior lee más allá del final del buffer.

**Omitir una comprobación de longitud porque el código «evidentemente» no puede llegar a un estado incorrecto.** El autor se convence de que un camino de ejecución determinado no puede producir una longitud superior a cierta cota, por lo que no hace falta ninguna comprobación. Ese camino sí puede producir dicha longitud, porque el autor pasó por alto un caso.

Cada uno de estos es una clase de bug con sus propias contramedidas. El resto de esta sección recorre esas contramedidas.

### Acota siempre las longitudes

La contramedida más sencilla y efectiva es acotar todas las longitudes. Antes de usar una longitud proveniente de una fuente no confiable, compárala con un máximo conocido. Antes de asignar un buffer cuyo tamaño provenga de una fuente no confiable, compara ese tamaño con un máximo conocido. Antes de copiar en un buffer, confirma que el tamaño de la copia cabe.

En concreto, si el manejador de `ioctl` recibe una estructura con un campo `u_int32_t len`, añade una comprobación como esta al inicio mismo del manejador:

```c
#define SECDEV_MAX_LEN    4096

static int
secdev_ioctl_set_name(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char *kbuf;
    int error;

    if (args->len > SECDEV_MAX_LEN)
        return (EINVAL);

    kbuf = malloc(args->len + 1, M_SECDEV, M_WAITOK | M_ZERO);
    error = copyin(args->data, kbuf, args->len);
    if (error != 0) {
        free(kbuf, M_SECDEV);
        return (error);
    }
    kbuf[args->len] = '\0';

    /* use kbuf */

    free(kbuf, M_SECDEV);
    return (0);
}
```

La primera línea de la función establece la cota. Independientemente de lo que pase el llamador, `args->len` será como mucho `SECDEV_MAX_LEN`. La asignación está acotada, la copia está acotada y el terminador null está dentro del buffer. Este patrón es el caballo de batalla del código de driver seguro.

¿Cuál debe ser la cota? Depende de la semántica del argumento. El nombre de un dispositivo puede acotarse razonablemente a unos pocos cientos de bytes. Un blob de configuración puede acotarse a unos pocos kilobytes. Un blob de firmware puede acotarse a unos pocos megabytes. Elige un número suficientemente generoso para cubrir el uso legítimo y suficientemente pequeño para que sus consecuencias, en caso de alcanzarse, sean asumibles. Si la cota es demasiado pequeña, los usuarios se quejarán de fallos legítimos; si es demasiado grande, un atacante puede usarla como amplificador para una denegación de servicio. Una cota generosa es casi siempre la elección correcta.

Algunos drivers derivan la cota de la estructura del hardware. Un driver para un banco de registros de tamaño fijo puede acotar las lecturas y escrituras al tamaño del banco. Un driver para un anillo de 256 entradas puede acotar el índice a 255. Las cotas derivadas de la estructura del hardware son especialmente robustas, porque corresponden a una restricción física y no a una elección arbitraria.

### La trampa de `sizeof(buf)`

Uno de los bugs de tamaño de buffer más comunes en código C es la confusión entre `sizeof(buf)` y `sizeof(*buf)`, o entre `sizeof(buf)` y la longitud de la memoria a la que apunta `buf`. La trampa aparece con mayor frecuencia cuando un buffer se pasa a una función.

Considera esta función insegura:

```c
static void
bad_copy(char *dst, const char *src)
{
    strlcpy(dst, src, sizeof(dst));    /* WRONG */
}
```

Aquí, `dst` es un `char *`, por lo que `sizeof(dst)` es el tamaño de un puntero: 4 en sistemas de 32 bits, 8 en sistemas de 64 bits. La llamada a `strlcpy` le indica que el destino mide 8 bytes, independientemente del tamaño real del buffer. En un sistema de 64 bits, la función escribe hasta 8 bytes y termina, y el buffer de 4096 bytes del llamador contiene ahora una cadena corta, lo que probablemente no es lo que nadie pretendía. En cualquier sistema, si el buffer del llamador tiene menos de 8 bytes, la llamada lo desborda.

La solución es pasar el tamaño del buffer explícitamente:

```c
static void
good_copy(char *dst, size_t dstlen, const char *src)
{
    strlcpy(dst, src, dstlen);
}
```

Los llamadores usan entonces `sizeof(their_buf)` en el punto de llamada, donde `their_buf` es el array conocido:

```c
char name[64];
good_copy(name, sizeof(name), user_input);
```

Este patrón es tan habitual en FreeBSD que muchas funciones internas lo siguen: reciben un par `(buf, bufsize)` en lugar de un simple `buf`. Cuando escribas funciones que escriban en un buffer, haz lo mismo. Tu yo del futuro, al leer el código seis meses después, te lo agradecerá.

### Funciones de cadenas con límite

Las funciones de cadenas tradicionales de C, `strcpy`, `strcat`, `sprintf`, se diseñaron en una época en la que nadie tomaba en serio los desbordamientos de buffer. No aceptan un argumento de tamaño; escriben hasta que encuentran un terminador null. En código del kernel son problemáticas, porque el terminador null puede estar muy lejos o no existir en absoluto.

FreeBSD ofrece alternativas con límite:

- `strlcpy(dst, src, dstsize)`: copia como mucho `dstsize - 1` bytes más un terminador null. Devuelve la longitud de la cadena fuente. Segura de usar cuando conoces correctamente `dstsize`.
- `strlcat(dst, src, dstsize)`: añade `src` a `dst`, garantizando que el resultado tenga como mucho `dstsize - 1` bytes más un terminador null. Al igual que `strlcpy`, es segura cuando `dstsize` es correcto.
- `snprintf(dst, dstsize, fmt, ...)`: da formato a `dst`, escribiendo como mucho `dstsize` bytes incluido el terminador. Devuelve el número de bytes que se habrían escrito, que puede ser mayor que `dstsize`. Comprueba el valor de retorno si necesitas saber si se produjo truncamiento.

`strncpy` y `strncat` también existen, pero tienen una semántica sorprendente. `strncpy` rellena con nulls si la fuente es más corta que el tamaño del destino y, lo que es más peligroso, no añade el terminador null si la fuente es más larga. `strncat` resulta confusa de otra manera. Prefiere `strlcpy` y `strlcat` en código nuevo.

Para salida formateada más extensa, la API `sbuf(9)` es aún más segura. Gestiona un buffer de crecimiento automático con una interfaz limpia para añadir cadenas, imprimir salida formateada y acotar el tamaño final. Es excesiva para copias pequeñas de tamaño fijo, pero excelente para cualquier cosa que construya un mensaje más largo. La sección 8 retoma `sbuf` en el contexto del registro de mensajes.

### Aritmética y desbordamiento

Una clase de bug de buffer más sutil proviene de la aritmética sobre tamaños. El ejemplo clásico es:

```c
uint32_t total = count * elem_size;          /* may overflow */
buf = malloc(total, M_SECDEV, M_WAITOK);
copyin(user_buf, buf, total);
```

Si `count * elem_size` desborda un `uint32_t` de 32 bits, `total` da la vuelta y toma un valor pequeño. El `malloc` tiene éxito con ese número pequeño. Se le pide al `copyin` el mismo número pequeño de bytes, lo que hace que el par asignación-copia sea en sí mismo seguro. Pero una parte posterior del driver puede tratar `count * elem_size` como si hubiera producido la cantidad completa, y escribir más allá del final del buffer.

La solución es comprobar el desbordamiento explícitamente:

```c
#include <sys/limits.h>

if (count == 0 || elem_size == 0)
    return (EINVAL);
if (count > SIZE_MAX / elem_size)
    return (EINVAL);
size_t total = count * elem_size;
```

La división es exacta (sin redondeo) para tipos enteros, y la comprobación `count > SIZE_MAX / elem_size` equivale a preguntar «¿desbordará la multiplicación un `size_t`?». Este patrón merece la pena memorizarlo. Es uno de esos modismos que parecen innecesarios en el caso habitual y resultan esenciales en el excepcional.

En compiladores modernos, FreeBSD también dispone de `__builtin_mul_overflow` y sus funciones hermanas, que realizan la aritmética e informan del desbordamiento en una sola operación. Son algo más cómodas cuando se dispone de ellas, pero la comprobación explícita mediante división funciona en cualquier entorno.

### La importancia de los tipos enteros

Estrechamente relacionada con lo anterior está la elección de los tipos enteros para longitudes y desplazamientos. Si una longitud se almacena como `int`, puede ser negativa, y un valor negativo que se cuele en una llamada que espera una longitud sin signo puede provocar un comportamiento catastrófico. Si una longitud se almacena como `short`, solo puede representar valores hasta 32767, y un llamador que pase un valor cercano a ese límite puede provocar truncamiento.

Los tipos seguros para longitudes en FreeBSD son `size_t` (sin signo, al menos 32 bits, habitualmente 64 en plataformas de 64 bits) y `ssize_t` (`size_t` con signo, generalmente para valores de retorno que pueden ser negativos para indicar error). Úsalos de forma coherente. Cuando recibas una longitud como entrada, conviértela a `size_t` en el momento más temprano posible. Cuando almacenes una longitud, hazlo como `size_t`. Cuando pases una longitud a una primitiva de FreeBSD, pásala como `size_t`.

Si la longitud proviene del espacio de usuario y la estructura visible al usuario usa un `uint32_t`, la conversión en un kernel de 64 bits es segura (sin truncamiento), pero aun así debes validar el valor antes de usarlo. Si la estructura visible al usuario usa `int64_t` y el kernel necesita un `size_t`, comprueba si hay valores negativos y desbordamiento antes de la conversión.

### Los buffers de pila son baratos pero limitados

Un buffer de pila es un array local:

```c
static int
secdev_read_name(struct secdev_softc *sc, struct uio *uio)
{
    char name[64];
    int error;

    mtx_lock(&sc->sc_mtx);
    strlcpy(name, sc->sc_name, sizeof(name));
    mtx_unlock(&sc->sc_mtx);

    error = uiomove(name, strlen(name), uio);
    return (error);
}
```

Los buffers de pila se asignan automáticamente, se liberan automáticamente cuando la función retorna y su uso es prácticamente gratuito. Son ideales para datos pequeños y de corta vida que no necesitan sobrevivir a la llamada a la función.

El límite de los buffers de pila es el tamaño de la propia pila del kernel. La pila del kernel de FreeBSD es pequeña, típicamente 16 KiB o 32 KiB según la arquitectura, y esa pila debe alojar toda la cadena de llamadas, incluidas las llamadas anidadas al VFS, al planificador, a los manejadores de interrupción y demás. Una función de driver que declara un buffer local de 4 KiB ya está usando una cuarta parte de la pila. Una función de driver que declara un buffer local de 32 KiB casi con certeza ha agotado la pila, y el kernel entrará en pánico o corromperá la memoria cuando eso ocurra.

Una regla práctica segura: mantén los buffers locales por debajo de 512 bytes, y a ser posible por debajo de 256 bytes. Para cualquier cosa más grande, asigna en el heap. El compilador no te advertirá cuando declares un buffer de pila demasiado grande; es responsabilidad del autor mantener el uso de la pila dentro de límites.

### Los buffers de heap y sus ciclos de vida

Un buffer de heap se asigna dinámicamente:

```c
char *buf;

buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
/* use buf */
free(buf, M_SECDEV);
```

Los buffers de heap pueden ser arbitrariamente grandes (hasta el límite de la memoria disponible), pueden sobrevivir a la función que los asigna y ofrecen al autor un control explícito sobre cuándo se liberan. Sin embargo, requieren atención deliberada: cada asignación debe ir emparejada con una liberación, cada liberación debe producirse después del último uso, y cada liberación debe ocurrir una única vez.

Las reglas para los buffers de heap son:

1. Comprueba siempre el resultado de la asignación si usas `M_NOWAIT`. Con `M_WAITOK`, la asignación no puede fallar; con `M_NOWAIT`, puede devolver `NULL` y tu código debe contemplar esa posibilidad.
2. Empareja cada `malloc` con exactamente un `free`. Ni cero, ni dos.
3. Tras llamar a `free`, no accedas al buffer. Si el puntero puede reutilizarse, ponlo a `NULL` inmediatamente después de la liberación, de modo que un uso accidental provoque un panic por puntero nulo en lugar de una corrupción silenciosa.
4. Si el buffer contenía datos sensibles, ponlo a cero con `explicit_bzero` o usa `zfree` antes de liberarlo.

La sección 4 aborda estas reglas con mayor profundidad, incluidos los flags específicos de FreeBSD en `malloc(9)`.

### Un ejemplo práctico: rutinas de copia seguras e inseguras

Para ilustrar los patrones con un ejemplo concreto, aquí tienes una rutina de copia insegura que podrías encontrar en un driver en primera versión, seguida de una reescritura segura. Lee la versión insegura con cuidado e intenta identificar todos los errores antes de leer el comentario.

```c
/* UNSAFE: do not use */
static int
secdev_bad_copy(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char buf[256];

    copyin(args->data, buf, args->len);
    buf[args->len] = '\0';
    strlcpy(sc->sc_name, buf, sizeof(sc->sc_name));
    return (0);
}
```

Hay al menos cuatro errores en esas cuatro líneas.

Primero, el valor de retorno de `copyin` se ignora. Si el usuario proporcionó un puntero incorrecto, `copyin` devuelve `EFAULT`, pero la función continúa como si la copia hubiera tenido éxito. Las operaciones posteriores sobre `buf` operan con la basura que hubiera en la pila en ese momento.

Segundo, `args->len` no está acotado. Si el usuario proporciona un `len` de 1000, `copyin` escribe 1000 bytes en un buffer de pila de 256 bytes. La pila queda corrompida. El driver acaba de convertirse en un vehículo para la escalada de privilegios.

Tercero, `buf[args->len] = '\0'` escribe más allá del final del buffer incluso en el caso no malicioso. Si `args->len == sizeof(buf)`, la asignación es a `buf[256]`, que es una posición más allá del final del array de 256 bytes.

Cuarto, la función devuelve 0 independientemente de si algo salió mal. El llamador recibe un código de éxito y no tiene manera de saber que el driver descartó silenciosamente su entrada.

A continuación se muestra una reescritura segura:

```c
/* SAFE */
static int
secdev_copy_name(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char buf[256];
    int error;

    if (args->len == 0 || args->len >= sizeof(buf))
        return (EINVAL);

    error = copyin(args->data, buf, args->len);
    if (error != 0)
        return (error);

    buf[args->len] = '\0';

    mtx_lock(&sc->sc_mtx);
    strlcpy(sc->sc_name, buf, sizeof(sc->sc_name));
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

El límite es ahora `args->len >= sizeof(buf)`, lo que garantiza que el terminador en `buf[args->len]` cabe. El valor de retorno de `copyin` se comprueba y se propaga. La escritura en `sc->sc_name` ocurre bajo el mutex que la protege, lo que garantiza que otro thread que lea el campo al mismo tiempo vea un valor coherente. La función devuelve el código de error que el llamador necesita para entender qué ocurrió.

La versión insegura tiene ocho líneas; la segura, trece. Las cinco líneas adicionales marcan la diferencia entre un driver que funciona y un incidente de seguridad.

### Un segundo ejemplo práctico: la longitud del descriptor

He aquí una clase diferente de error que aparece en drivers para dispositivos que presentan datos con estructura de descriptor (USB, virtio, configuración PCIe):

```c
/* UNSAFE */
static void
parse_descriptor(struct secdev_softc *sc, const uint8_t *buf, size_t buflen)
{
    size_t len = buf[0];
    const uint8_t *payload = &buf[1];

    /* copy the payload */
    memcpy(sc->sc_descriptor, payload, len);
}
```

La longitud se toma del primer byte del buffer, un valor que el dispositivo (o un atacante que lo suplante) puede establecer de forma arbitraria. Si `buf[0]` vale 200, `memcpy` copia 200 bytes, independientemente de si `buf` contiene realmente 200 bytes de datos válidos o de si `sc->sc_descriptor` es tan grande. Si `buflen` es menor que `buf[0] + 1`, `memcpy` lee más allá del final del buffer del llamador. Si `sizeof(sc->sc_descriptor)` es menor que `buf[0]`, `memcpy` escribe más allá del final del destino.

La versión segura valida ambos lados de la copia:

```c
/* SAFE */
static int
parse_descriptor(struct secdev_softc *sc, const uint8_t *buf, size_t buflen)
{
    if (buflen < 1)
        return (EINVAL);

    size_t len = buf[0];

    if (len + 1 > buflen)
        return (EINVAL);
    if (len > sizeof(sc->sc_descriptor))
        return (EINVAL);

    memcpy(sc->sc_descriptor, &buf[1], len);
    return (0);
}
```

Tres comprobaciones, cada una guardando un invariante diferente: el buffer tiene al menos un byte, la longitud indicada cabe en el buffer y la longitud indicada cabe en el destino. Cada comprobación protege contra un tipo diferente de entrada adversarial o accidental.

Un lector atento puede notar que `len + 1 > buflen` puede desbordarse si `len` vale `SIZE_MAX`. Para un `size_t` tomado de un byte, `len` es como máximo 255, por lo que el desbordamiento no puede ocurrir aquí; pero si escribes el mismo código para un campo de longitud de 32 bits, la comprobación debe reorganizarse como `len > buflen - 1` con una comprobación explícita `buflen >= 1`. El hábito de vigilar el desbordamiento aritmético es el mismo, aplicado a diferentes escalas.

### El desbordamiento de buffer como clase de error

Apartándonos de los ejemplos concretos: los desbordamientos de buffer no son un único error. Son una familia de errores cuyos miembros comparten una estructura: el código escribe en un buffer o lee de él con un tamaño u offset incorrecto. Los ejemplos anteriores muestran varios miembros de la familia, pero el patrón subyacente es el mismo: una longitud provenía de un lugar menos fiable de lo que el código creía, y el código no estaba preparado.

Las contramedidas también comparten una estructura. Todas se reducen a: no confíes en la longitud; compruébala contra un límite conocido antes de usarla; mantén el límite ajustado; propaga los errores cuando la comprobación falle; usa primitivas acotadas (`strlcpy`, `snprintf`, `sbuf(9)`) cuando tengas la opción; vigila el desbordamiento aritmético en los cálculos de longitud; y mantén los buffers de pila pequeños. Esa breve lista, aplicada de forma coherente, elimina la mayoría de los errores de desbordamiento de buffer antes de que se escriban.

### Corrupción de memoria más allá de los desbordamientos

No todo error de corrupción de memoria es un desbordamiento de buffer. Los drivers pueden corromper la memoria de otras maneras, y un tratamiento completo de la seguridad debe mencionarlas.

**Use-after-free** consiste en escribir en un buffer, o leer de él, después de que haya sido liberado. El asignador casi con certeza habrá entregado esa memoria a otra parte del kernel para entonces, por lo que la escritura corrompe lo que esté haciendo esa parte del kernel. La sección 4 trata el use-after-free en profundidad.

**Double-free** consiste en llamar a `free` dos veces sobre el mismo puntero. Dependiendo del asignador, esto puede corromper las propias estructuras de datos del asignador, lo que genera panics difíciles de diagnosticar minutos u horas después. La sección 4 trata la prevención.

**Out-of-bounds read** es el primo en lectura del desbordamiento de buffer. No corrompe la memoria directamente, pero puede filtrar información (véase la sección 7) y puede hacer que el kernel lea desde una página no mapeada, lo que provoca un panic. Merece las mismas contramedidas que el desbordamiento.

**Type confusion** consiste en tratar un bloque de memoria como si tuviera un tipo diferente al que realmente tiene. Por ejemplo, convertir un puntero al tipo de estructura incorrecto y acceder a sus campos. En el C del kernel, la confusión de tipos la detecta normalmente el compilador, pero puede ocurrir cuando un driver trabaja con punteros void o con estructuras compartidas entre versiones.

**Uninitialised memory use** consiste en leer una variable antes de asignarle un valor. El valor leído es el que hubiera en memoria en esa posición, que puede ser datos de llamadores anteriores. La sección 7 trata esto desde la perspectiva de la filtración de información.

Cada uno tiene sus propias contramedidas, pero la herramienta más eficaz para todos ellos es el conjunto de sanitizers del kernel que proporciona FreeBSD: `INVARIANTS`, `WITNESS`, `KASAN`, `KMSAN` y `KCOV`. La sección 10 trata estas herramientas en profundidad. La versión corta: compila siempre tu driver contra un kernel con `INVARIANTS` y `WITNESS`. Compílalo contra un kernel con `KASAN` habilitado durante el desarrollo. Ejecuta tus pruebas bajo el kernel con sanitizers. Los sanitizers encontrarán errores que de otro modo no encontrarías hasta que lo hiciera un cliente.

### Cómo ayudan las protecciones del compilador y dónde se detienen

Los kernels de FreeBSD se compilan habitualmente con varias características de mitigación de exploits habilitadas en el compilador. Entender qué hacen es parte de entender por qué ciertos hábitos defensivos importan más que otros.

**Stack-smashing protection (SSP)** inserta un valor canario en la pila entre las variables locales y la dirección de retorno guardada. Cuando la función retorna, el canario se comprueba contra un valor de referencia; si ha sido modificado (porque un desbordamiento del buffer de pila lo sobrescribió), el kernel produce un panic. SSP no impide que ocurra el desbordamiento, pero evita que muchos desbordamientos consigan el control de la ejecución. Sin SSP, sobrescribir la dirección de retorno redireccionaría la ejecución hacia código controlado por el atacante en el retorno. Con SSP, la sobrescritura se detecta y el kernel se detiene.

SSP es heurístico. No todas las funciones reciben un canario: las funciones sin buffers asignados en la pila, por ejemplo, no necesitan protección. El compilador aplica SSP a las funciones que parecen arriesgadas. El autor de un driver no debe asumir que SSP detectará cualquier error concreto; SSP detecta algunos desbordamientos de pila, no todos, y los detecta únicamente en el retorno de la función, no en el momento del desbordamiento.

**kASLR** es ortogonal a SSP. Aleatoriza la dirección base del kernel, los módulos cargables y la pila. Un atacante que quiera saltar a una función específica del kernel (por ejemplo, para eludir una comprobación) debe saber primero dónde está esa función. kASLR hace esto difícil. Una filtración de información que exponga cualquier puntero del kernel puede deshabilitar kASLR para todo el kernel: una vez que conoces la dirección de una función, conoces los offsets a todas las demás y puedes calcular cualquier otra dirección.

**W^X enforcement** garantiza que la memoria sea escribible o ejecutable, nunca ambas cosas a la vez. Históricamente, los atacantes desbordaban un buffer, escribían shellcode en la región desbordada y saltaban a él. W^X rompe esto al negarse a ejecutar desde memoria escribible. Los ataques modernos utilizan por ello la programación orientada a retorno (ROP), que encadena pequeños fragmentos de código existente en lugar de introducir código nuevo. ROP sigue siendo posible bajo W^X, pero es más difícil, y kASLR lo contrarresta (ROP necesita saber dónde están los fragmentos).

**Guard pages** rodean las pilas del kernel con páginas no mapeadas. Una escritura más allá del final de la pila alcanza una página no mapeada, lo que provoca un fallo de página que el kernel captura y convierte en un panic. Esto evita que ciertos ataques de stack-smashing corrompan silenciosamente la memoria adyacente a la pila. El coste es una página inutilizable por pila del kernel, lo cual es barato.

**Shadow stacks y CFI (control-flow integrity)** están en debate y despliegue parcial en los kernels modernos. Su objetivo es evitar que los atacantes redireccionen la ejecución verificando que cada salto indirecto llegue a un destino legítimo. Todavía no son estándar en FreeBSD, pero la dirección de la industria es clara: más restricciones impuestas por el compilador sobre lo que pueden hacer los creadores de exploits.

La lección para los autores de drivers: estas protecciones son reales y elevan el coste de la explotación. Pero no previenen los errores. Un desbordamiento de buffer sigue siendo un error, aunque SSP lo detecte. Una filtración de información sigue siendo un error, aunque kASLR la haga menos útil. Las protecciones del compilador son la última línea de defensa; la primera sigue siendo tu código cuidadoso.

Cuando la primera línea falla, las protecciones compran tiempo: tiempo para encontrar y corregir el error antes de que un atacante lo encadene en un exploit completo. Una filtración de información que, combinada con un desbordamiento de buffer, habría sido trivialmente explotable en 1995, ahora requiere que ambos errores existan en el mismo driver y que caigan varias mitigaciones más. El efecto es que los informes de errores que antes significaban "esto es un exploit de root" ahora suelen significar "esto es una condición previa para un exploit de root". Eso es progreso. Pero es un progreso comprado por el compilador, no por el código.

### Cerrando la sección 2

Los desbordamientos de buffer y la corrupción de memoria son los errores de seguridad más antiguos en código C, y siguen siendo la forma más común en que el código de driver falla. Las contramedidas son bien conocidas: acota toda longitud, usa primitivas acotadas cuando sea posible, vigila el desbordamiento aritmético, mantén los buffers de pila pequeños y ejecuta bajo los sanitizers del kernel durante el desarrollo. Ninguna de ellas es costosa. Todas ellas son innegociables para el código que vive en el kernel.

Los errores en esta sección provenían todos de datos del tamaño incorrecto que llegaban al buffer equivocado. La siguiente sección aborda un problema estrechamente relacionado: datos de la forma incorrecta que llegan a la función del kernel equivocada. Ese es el problema de la entrada del usuario, y es la mayor fuente individual de errores en drivers en el mundo real.

## Sección 3: Manejo seguro de la entrada del usuario

Todo driver que exporte un punto de entrada `ioctl`, `read`, `write` o `mmap` es un driver que recibe entrada del usuario. La forma de la entrada varía, pero el principio no: los datos del espacio de usuario deben cruzar el límite usuario-kernel, y ese cruce es donde ocurre la mayoría de los errores de seguridad en los drivers.

FreeBSD proporciona a los drivers un conjunto pequeño y bien diseñado de primitivas para cruzar el límite de forma segura. Las primitivas son `copyin(9)`, `copyout(9)`, `copyinstr(9)` y `uiomove(9)`. Usadas correctamente, hacen que sea casi imposible manejar mal la entrada del usuario. Usadas incorrectamente, convierten el límite en un agujero abierto. Esta sección enseña el uso correcto.

### El límite usuario-kernel

Antes de las primitivas, conviene hacer vivido el límite en sí mismo.

Un programa en espacio de usuario tiene su propio espacio de direcciones. Los punteros del programa hacen referencia a direcciones que solo tienen sentido dentro de ese espacio de direcciones. Un puntero que apunta al byte `0x7fff_1234_5678` en la memoria del programa no tiene ningún significado dentro del kernel; la visión que tiene el kernel de la memoria de usuario es indirecta, mediada por el subsistema de memoria virtual.

Cuando el programa realiza una llamada `ioctl` que incluye un puntero (por ejemplo, un puntero a una estructura que el driver debe rellenar), el kernel no recibe acceso en espacio del kernel a esa memoria. El kernel recibe una dirección de espacio de usuario. Desreferenciarla directamente desde código del kernel no es seguro: la dirección puede ser inválida (el usuario envió un puntero basura), puede apuntar a memoria que no está actualmente residente (paginada fuera), puede que no esté mapeada en absoluto en el espacio de direcciones actual, o puede encontrarse en una región que el kernel no tiene ningún motivo para leer.

Los primeros kernels de UNIX eran a veces descuidados en este aspecto y desreferenciaban los punteros de usuario directamente. El resultado era una clase de vulnerabilidad conocida como ataques de tipo "ptrace-style", en los que un programa de usuario podía inducir al kernel a leer o escribir direcciones arbitrarias pasando punteros cuidadosamente diseñados. Los kernels modernos, incluido FreeBSD, nunca desreferencian un puntero de usuario directamente desde código del kernel. Siempre recurren a una primitiva dedicada que valida y gestiona el acceso de forma segura.

Las propias primitivas son sencillas. Antes de analizarlas, una nota sobre vocabulario: cuando las páginas del manual y el kernel dicen "dirección del kernel", se refieren a una dirección que tiene sentido en el espacio de direcciones del kernel. Cuando dicen "dirección de usuario", se refieren a una dirección proporcionada por un llamador en espacio de usuario, que solo tiene sentido en el espacio de direcciones de ese llamador. Las primitivas traducen entre los dos, con las comprobaciones de seguridad adecuadas.

### `copyin(9)` y `copyout(9)`

Las dos primitivas en el corazón del límite usuario-kernel son `copyin(9)` y `copyout(9)`:

```c
int copyin(const void *udaddr, void *kaddr, size_t len);
int copyout(const void *kaddr, void *udaddr, size_t len);
```

`copyin` copia `len` bytes desde la dirección de usuario `udaddr` hasta la dirección del kernel `kaddr`. `copyout` copia `len` bytes desde la dirección del kernel `kaddr` hasta la dirección de usuario `udaddr`. Ambas devuelven 0 en caso de éxito y `EFAULT` si alguna parte de la copia falló, típicamente porque la dirección de usuario era inválida, la memoria no estaba residente o el acceso cruzó hacia memoria a la que el llamador no tenía derechos.

Las signaturas están declaradas en `/usr/src/sys/sys/systm.h`. Como la mayoría de las primitivas del kernel, son concisas y hacen una sola cosa. Sin embargo, esa cosa que hacen es esencial. Si un driver lee o escribe en memoria de usuario por cualquier otro medio, el driver es casi con toda certeza incorrecto.

**Comprueba siempre el valor de retorno.** Esta es la fuente más común de errores con copyin/copyout: un driver llama a `copyin` y continúa como si hubiera tenido éxito, cuando en realidad podría haber devuelto `EFAULT`. Si la copia falló, el buffer de destino contiene lo que hubiera antes (posiblemente sin inicializar), y operar sobre él es una receta para un crash o una filtración de información. Cada llamada a `copyin` o `copyout` debe comprobar el valor de retorno y, o bien continuar con el éxito, o bien propagar el error:

```c
error = copyin(args->data, kbuf, args->len);
if (error != 0) {
    free(kbuf, M_SECDEV);
    return (error);
}
```

Ese patrón aparece cientos de veces en el kernel de FreeBSD. Apréndelo y úsalo en cada punto de llamada.

**Nunca reutilices un puntero tras una copia fallida.** Si `copyin` devolvió `EFAULT`, el buffer puede haber sido escrito parcialmente. No intentes "recuperar" un resultado parcial; no asumas que los primeros bytes son válidos. Descarta el buffer, ponlo a cero si el contenido residual puede ser sensible, y devuelve el error.

**Valida siempre las longitudes antes de llamar.** Ya lo hemos visto en la Sección 2, pero merece repetirse aquí. El `len` que pasas a `copyin` viene de algún sitio; si viene de la estructura del llamador, debe estar acotado antes de la llamada. Un `len` sin acotar en un `copyin` es uno de los patrones más peligrosos en un driver.

**`copyin` y `copyout` pueden dormir.** Estas primitivas pueden hacer que el thread llamador duerma mientras espera que una página de usuario esté residente. Esto significa que no se pueden llamar desde contextos donde dormir está prohibido: manejadores de interrupciones, secciones críticas con spinlock y similares. Si necesitas transferir datos hacia o desde espacio de usuario desde ese tipo de contexto, difiere el trabajo a un contexto diferente (normalmente un taskqueue o un contexto de proceso regular) y deja que ese contexto realice la copia.

### `copyinstr(9)` para cadenas

Una cadena procedente del espacio de usuario es un caso especial. No sabes cuánto mide, solo que está terminada en nulo. Quieres copiarla, pero no quieres copiar más allá del buffer que has preparado, y necesitas manejar el caso en que la cadena proporcionada por el usuario no tenga terminador dentro del rango esperado.

La primitiva para esto es `copyinstr(9)`:

```c
int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *lencopied);
```

`copyinstr` copia bytes desde `udaddr` hasta `kaddr` hasta que encuentra un byte nulo o hasta que se han copiado `len` bytes, lo que ocurra primero. Si `lencopied` no es NULL, `*lencopied` se establece al número de bytes copiados (incluyendo el terminador, si se encontró uno). El valor de retorno es 0 en caso de éxito, `EFAULT` ante un fallo de acceso y `ENAMETOOLONG` si no se encontró ningún terminador dentro de `len` bytes.

La regla de seguridad clave es: **pasa siempre un `len` acotado**. Un `copyinstr` sin cota (o con una cota enorme) puede provocar que se escriban grandes cantidades de memoria del kernel, y en kernels más antiguos podía hacer que el kernel examinara cantidades ingentes de memoria de usuario antes de rendirse. En FreeBSD moderno el escaneo en sí está acotado por `len`, pero aun así debes pasar un límite ajustado y apropiado al tamaño esperado de la cadena. Un nombre de ruta puede estar razonablemente acotado a `MAXPATHLEN` (que es `PATH_MAX`, actualmente 1024 en FreeBSD). Un nombre de dispositivo puede estar acotado a 64. Un nombre de comando puede estar acotado a 32. Elige un límite que se ajuste al uso y pásalo.

Una segunda regla de seguridad es: **comprueba siempre el valor de retorno**, y trata `ENAMETOOLONG` como una condición distinta de `EFAULT`. La primera significa que el usuario intentó pasar una cadena más larga de lo que estabas dispuesto a aceptar, lo cual puede ser un error perfectamente legítimo. La segunda significa que el puntero del usuario era inválido, lo cual puede o no ser un error legítimo. Tu driver puede querer devolver un error diferente al espacio de usuario dependiendo de qué condición se produjo.

Una tercera regla de seguridad es: **comprueba la longitud copiada si la necesitas**. El parámetro `lencopied` te indica cuántos bytes se escribieron realmente, incluyendo el terminador. Si tu código depende de conocer la longitud exacta, compruébala. Si tu buffer tiene exactamente `len` bytes y `copyinstr` devolvió 0, el terminador está en `kbuf[lencopied - 1]` y la cadena tiene `lencopied - 1` bytes de longitud.

Un uso seguro de `copyinstr`:

```c
static int
secdev_ioctl_set_name(struct secdev_softc *sc,
    struct secdev_ioctl_name *args)
{
    char name[SECDEV_NAME_MAX];
    size_t namelen;
    int error;

    error = copyinstr(args->name, name, sizeof(name), &namelen);
    if (error == ENAMETOOLONG)
        return (EINVAL);
    if (error != 0)
        return (error);

    /* namelen includes the terminator; the string is namelen - 1 bytes */
    KASSERT(namelen > 0, ("copyinstr returned zero-length success"));
    KASSERT(name[namelen - 1] == '\0', ("copyinstr missed terminator"));

    mtx_lock(&sc->sc_mtx);
    strlcpy(sc->sc_name, name, sizeof(sc->sc_name));
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

La función toma un buffer de tamaño fijo en la pila, llama a `copyinstr` con un límite ajustado, gestiona los dos casos de error de forma diferenciada, comprueba los invariantes que `copyinstr` garantiza (`namelen > 0`, terminador en `name[namelen - 1]`), y copia en el softc bajo el lock. Este es el patrón canónico.

### `uiomove(9)` para flujos

Los puntos de entrada `read` y `write` no usan `copyin`/`copyout` directamente; usan `uiomove(9)`, que es un envoltorio que gestiona la iteración sobre un descriptor `struct uio`. Un `uio` describe una operación de I/O con potencialmente múltiples buffers (scatter-gather) y lleva la cuenta de cuánto se ha transferido hasta el momento.

```c
int uiomove(void *cp, int n, struct uio *uio);
```

`uiomove` copia hasta `n` bytes entre el buffer del kernel `cp` y lo que describe `uio`. Si `uio->uio_rw == UIO_READ`, la copia va del kernel al usuario; si `UIO_WRITE`, del usuario al kernel. La función actualiza `uio->uio_offset`, `uio->uio_resid` y `uio->uio_iov` para reflejar los bytes transferidos.

Al igual que `copyin`, `uiomove` devuelve 0 en caso de éxito y un código de error en caso de fallo. Al igual que `copyin`, puede dormir. Al igual que `copyin`, el llamador debe comprobar el valor de retorno.

Una implementación típica de `read`:

```c
static int
secdev_read(struct cdev *dev, struct uio *uio, int flag)
{
    struct secdev_softc *sc = dev->si_drv1;
    char buf[128];
    size_t len;
    int error;

    mtx_lock(&sc->sc_mtx);
    len = strlcpy(buf, sc->sc_name, sizeof(buf));
    mtx_unlock(&sc->sc_mtx);

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    if (uio->uio_offset >= len)
        return (0);   /* EOF */

    error = uiomove(buf + uio->uio_offset, len - uio->uio_offset, uio);
    return (error);
}
```

Esto gestiona el caso en que el usuario lee más allá del final de los datos (devolviendo 0 para indicar EOF), acota la copia al tamaño del buffer del kernel y propaga cualquier error de `uiomove`. Es un patrón seguro para datos cortos y fijos; los datos más largos suelen usar `sbuf(9)` internamente y copian al espacio de usuario con `sbuf_finish`/`sbuf_len`/`uiomove` al final.

### Valida cada campo de cada estructura

Cuando un `ioctl` recibe una estructura, el driver debe validar cada campo antes de confiar en ninguno de ellos. Un error habitual es validar los campos que el driver usa de inmediato e ignorar los que usará más adelante. La estructura vive durante toda la llamada al `ioctl`, y el driver puede acabar usando campos que no comprobó.

Concretamente, si tu `ioctl` recibe esta estructura:

```c
struct secdev_config {
    uint32_t version;       /* protocol version */
    uint32_t flags;         /* configuration flags */
    uint32_t len;           /* length of data */
    uint64_t data;          /* user pointer to data blob */
    char name[64];          /* human-readable name */
};
```

valida cada campo al inicio del manejador:

```c
static int
secdev_ioctl_config(struct secdev_softc *sc, struct secdev_config *cfg)
{
    if (cfg->version != SECDEV_CONFIG_VERSION_1)
        return (ENOTSUP);

    if ((cfg->flags & ~SECDEV_CONFIG_FLAGS_MASK) != 0)
        return (EINVAL);

    if (cfg->len > SECDEV_CONFIG_MAX_LEN)
        return (EINVAL);

    /* Name must be null-terminated within the field. */
    if (memchr(cfg->name, '\0', sizeof(cfg->name)) == NULL)
        return (EINVAL);

    /* ... proceed to use the structure ... */
    return (0);
}
```

Cuatro invariantes, cada uno comprobado y aplicado. El driver ahora sabe que `version`, `flags`, `len` y `name` están todos dentro del rango esperado. Puede usarlos sin validación adicional. Sin estas comprobaciones, cada uso posterior en la función se convierte en otra fuente potencial de errores.

Una sutileza importante: cuando una estructura incluye campos reservados o relleno (padding), el driver debe decidir qué hacer cuando esos campos son distintos de cero. La opción segura suele ser exigir que sean cero:

```c
if (cfg->reserved1 != 0 || cfg->reserved2 != 0)
    return (EINVAL);
```

Esto preserva la posibilidad de usar esos campos en una versión futura del protocolo sin romper la compatibilidad: si todos los llamadores actuales pasan cero, cualquier valor distinto de cero en el futuro proviene necesariamente de un llamador que conoce la nueva versión. Sin la comprobación, el driver no puede distinguir posteriormente entre llamadores antiguos (que casualmente dejaron basura en los campos reservados) y llamadores nuevos (que utilizan el campo para un nuevo propósito).

### Valida las estructuras que llegan en varias partes

Algunos `ioctl`s reciben una estructura que contiene un puntero a otro bloque de datos. La estructura exterior se copia primero; el puntero que contiene debe seguirse después con un segundo `copyin`. Todos los campos de ambas estructuras deben ser validados.

```c
struct secdev_ioctl_args {
    uint32_t version;
    uint32_t len;
    uint64_t data;    /* user pointer to a blob of `len` bytes */
};

static int
secdev_ioctl_something(struct secdev_softc *sc,
    struct secdev_ioctl_args *args)
{
    char *blob;
    int error;

    /* Validate the outer structure. */
    if (args->version != SECDEV_IOCTL_VERSION_1)
        return (ENOTSUP);
    if (args->len > SECDEV_MAX_BLOB)
        return (EINVAL);
    if (args->len == 0)
        return (EINVAL);

    blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

    /* Copy the inner blob. */
    error = copyin((const void *)(uintptr_t)args->data, blob, args->len);
    if (error != 0) {
        free(blob, M_SECDEV);
        return (error);
    }

    /* ... now validate the inner blob, whose shape depends on the version ... */

    free(blob, M_SECDEV);
    return (0);
}
```

Vale la pena comentar el cast a `uintptr_t`. El puntero de usuario llega como un `uint64_t` en la estructura, para evitar problemas de portabilidad entre userlands de 32 y 64 bits. El cast a `uintptr_t` y luego a `const void *` convierte la representación entera de vuelta en un puntero. En un kernel de 64 bits, esto no tiene efecto; en un kernel de 32 bits, los bits altos del `uint64_t` deben ser validados o descartados. FreeBSD funciona en ambos, y un userland de 32 bits sobre un kernel de 64 bits (a través de `COMPAT_FREEBSD32`) es un caso real. Sé explícito con el cast y documenta la suposición.

### El problema del "freezed"

Algunos drivers tienen campos en las estructuras del espacio de usuario que son punteros, y la convención del driver es que la memoria del espacio de usuario permanece válida hasta que se completa una operación concreta. Este patrón es habitual en drivers que hacen DMA directamente desde memoria de usuario.

El patrón es delicado porque el usuario puede, en principio, modificar la memoria entre la validación del driver y su uso. El DMA basado en punteros es también una mala idea en drivers modernos; las alternativas más seguras incluyen:

- `mmap`, en el que el driver mapea memoria del kernel en el espacio de usuario para acceso directo, con el kernel conservando la propiedad de la memoria y su validez.
- Una aproximación de copia a través del kernel, en la que el driver siempre copia hacia adentro, valida y opera sobre la copia del kernel.
- El framework `busdma(9)`, que gestiona correctamente los buffers del espacio de usuario cuando deben transferirse al hardware mediante DMA.

Si te encuentras escribiendo código que conserva un puntero al espacio de usuario y lo usa en un momento posterior, detente y reflexiona. Casi siempre es un diseño incorrecto. La Sección 5 vuelve sobre este problema como una cuestión de TOCTOU.

### Las direcciones del kernel no deben filtrarse en punteros de usuario

Una clase recurrente de error se produce cuando un driver, al intentar comunicar un puntero al espacio de usuario, copia una dirección del kernel. El usuario recibe un puntero a memoria del kernel, lo cual es una filtración de información espectacular (revela la disposición del kernel, anulando KASLR) y, si el usuario puede de alguna manera convencer al kernel de tratar el puntero copiado como un puntero de usuario, puede convertirse en un acceso arbitrario a la memoria del kernel.

El error suele ser inadvertido. Un caso habitual es una estructura compartida entre el kernel y el espacio de usuario, donde uno de sus campos es un puntero. Si el driver rellena el campo con un puntero del kernel y luego copia la estructura al espacio de usuario, la filtración ya ha ocurrido.

La solución es estructural: no compartas estructuras entre el kernel y el espacio de usuario que contengan campos de puntero pensados para usarse en cualquiera de los dos espacios. Si existe un campo de puntero, hazlo `uint64_t` y trátalo como un entero opaco. Cuando el kernel rellena un campo de tipo puntero visible desde el usuario, debe escoger un valor significativo para el espacio de usuario, no revelar su propio puntero interno.

Una segunda clase de filtración se produce cuando un driver copia hacia el espacio de usuario una estructura que contiene campos sin inicializar, y uno de esos campos resulta contener un puntero del kernel (por ejemplo, porque el asignador devolvió memoria que antes se usó para algo que almacenaba un puntero del kernel). La Sección 7 cubre esto en profundidad.

### `compat32` y los tamaños de estructuras

FreeBSD soporta la ejecución de programas de espacio de usuario de 32 bits sobre un kernel de 64 bits a través de la maquinaria `COMPAT_FREEBSD32`. Para un driver, esto significa que la estructura que pasa el llamador puede ser una estructura de 32 bits, con una disposición y un tamaño diferentes a los de la versión de 64 bits. Si el driver espera la estructura de 64 bits y el llamador pasó la de 32 bits, los campos que el driver lee estarán en los desplazamientos incorrectos y el driver leerá basura.

Gestionar esto queda fuera del alcance de un driver típico; el framework ayuda ofreciendo puntos de entrada `ioctl32` y traducción automática para muchos casos comunes. Si tu driver se usa desde espacio de usuario de 32 bits y utiliza estructuras personalizadas, consulta la página del manual `freebsd32(9)` y el subsistema `sys/compat/freebsd32` para orientarte. Ten presente este problema y prueba tu driver desde un userland de 32 bits en el entorno de laboratorio.

### Un ejemplo más completo: un handler `ioctl` completo

Combinando los patrones de esta sección, así es como luce un handler `ioctl` completo y seguro para una operación hipotética:

```c
static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    struct secdev_ioctl_args *args;
    char *blob;
    int error;

    switch (cmd) {
    case SECDEV_IOCTL_DO_THING:
        args = (struct secdev_ioctl_args *)data;

        /* 1. Validate every field of the outer structure. */
        if (args->version != SECDEV_IOCTL_VERSION_1)
            return (ENOTSUP);
        if ((args->flags & ~SECDEV_FLAGS_MASK) != 0)
            return (EINVAL);
        if (args->len == 0 || args->len > SECDEV_MAX_BLOB)
            return (EINVAL);

        /* 2. Check that the caller has permission, if required. */
        if ((args->flags & SECDEV_FLAG_PRIVILEGED) != 0) {
            error = priv_check(td, PRIV_DRIVER);
            if (error != 0)
                return (error);
        }

        /* 3. Allocate the kernel-side buffer. */
        blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

        /* 4. Copy in the user-space blob. */
        error = copyin((const void *)(uintptr_t)args->data, blob,
            args->len);
        if (error != 0) {
            free(blob, M_SECDEV);
            return (error);
        }

        /* 5. Do the work under the softc lock. */
        mtx_lock(&sc->sc_mtx);
        error = secdev_do_thing(sc, blob, args->len);
        mtx_unlock(&sc->sc_mtx);

        /* 6. Zero and free the kernel buffer (it held user data
         * that might be sensitive). */
        explicit_bzero(blob, args->len);
        free(blob, M_SECDEV);

        return (error);

    default:
        return (ENOTTY);
    }
}
```

Cada paso numerado corresponde a una preocupación concreta. Cada paso gestiona los errores localmente y los propaga. La asignación está acotada por la longitud validada; la copia está acotada por esa misma longitud; la comprobación de permisos es explícita; la limpieza es simétrica con la asignación; el código de retorno final comunica el éxito o el fallo concreto. Así es como luce un handler ioctl seguro. No es corto, pero cada línea está ahí por una razón.

### Errores comunes en la gestión de la entrada del usuario

Una breve lista de comprobación con los patrones que debes vigilar, como referencia a la que puedes volver cuando revises tu propio código:

- `copyin` con una longitud procedente del usuario, sin comprobación previa de límites.
- `copyinstr` sin un límite explícito.
- Valor de retorno de `copyin`, `copyout` o `copyinstr` ignorado.
- Campos de la estructura usados antes de ser validados.
- Campo puntero convertido de `uint64_t` a `void *` sin tener en cuenta la compatibilidad con userland de 32 bits.
- Campo de cadena asumido como terminado en nulo sin una comprobación con `memchr`.
- Longitud usada en aritmética antes de ser acotada.
- Puntero de espacio de usuario conservado y usado más adelante (territorio TOCTOU).
- Estructura de datos del kernel (con campos puntero) copiada directamente hacia espacio de usuario.
- Campos sin inicializar copiados hacia espacio de usuario.

Si durante una revisión de código encuentras alguno de estos patrones, detén la revisión, corrige el patrón y luego continúa.

### Un recorrido detallado: diseñar un ioctl seguro desde cero

Las técnicas acumuladas a lo largo de esta sección pueden parecer una larga lista de comprobación. Para mostrar cómo se combinan en la práctica, vamos a diseñar un ioctl concreto con cuidado, desde la interfaz de espacio de usuario hasta la implementación en el kernel.

**El problema.** Nuestro driver necesita un ioctl que permita al usuario establecer un parámetro de configuración formado por una cadena de nombre (longitud acotada), un modo (enum) y un blob de datos opaco (longitud variable). También debe devolver la interpretación del driver sobre la configuración (por ejemplo, la forma canónica del nombre).

**Definición de la interfaz.** La estructura visible para el usuario, definida en un archivo de cabecera que se distribuirá junto con el driver, tiene este aspecto:

```c
#define SECDEV_NAME_MAX   64
#define SECDEV_BLOB_MAX   (16 * 1024)

enum secdev_mode {
    SECDEV_MODE_OFF = 0,
    SECDEV_MODE_ON = 1,
    SECDEV_MODE_AUTO = 2,
};

struct secdev_config {
    char              sc_name[SECDEV_NAME_MAX];
    uint32_t          sc_mode;
    uint32_t          sc_bloblen;
    void             *sc_blob;
    /* output */
    char              sc_canonical[SECDEV_NAME_MAX];
};
```

Notas sobre el diseño:

El nombre es un buffer inline de tamaño fijo, no un puntero. Esto es deliberado: evita un `copyin` adicional para el nombre y simplifica la interfaz. La contrapartida es que el buffer se copia siempre aunque el nombre sea corto, pero con 64 bytes eso es despreciable.

El modo es `uint32_t` y no `enum secdev_mode` directamente, porque los miembros de una estructura que cruzan la frontera usuario/kernel deben tener anchos explícitos. El kernel valida que el valor sea uno de los valores enum conocidos.

El blob utiliza un puntero separado (`sc_blob`) y una longitud (`sc_bloblen`). El usuario establece ambos, y el kernel usa un segundo `copyin` para recuperar los datos. La longitud está acotada por `SECDEV_BLOB_MAX`, un valor que nosotros (los autores del driver) elegimos en función de lo que el driver vaya a hacer con los datos.

La salida canónica es otro buffer inline fijo. Al llamador en espacio de usuario puede o no importarle esta salida, pero el kernel siempre la rellena.

**El handler del kernel.** Vamos a recorrer la implementación paso a paso. El framework de ioctl copiará la estructura al kernel por nosotros, de modo que cuando nuestro handler entre en ejecución, `cfg` apunta a memoria del kernel. El campo `sc_blob`, sin embargo, sigue siendo un puntero de espacio de usuario que debemos gestionar nosotros mismos.

```c
static int
secdev_ioctl_config(struct secdev_softc *sc, struct secdev_config *cfg,
    struct thread *td)
{
    char kname[SECDEV_NAME_MAX];
    char canonical[SECDEV_NAME_MAX];
    void *kblob = NULL;
    size_t bloblen;
    uint32_t mode;
    int error;

    /* Step 1: Privilege check. */
    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* Step 2: Jail check. */
    if (jailed(td->td_ucred))
        return (EPERM);

    /* Step 3: Copy and validate the name. */
    bcopy(cfg->sc_name, kname, sizeof(kname));
    kname[sizeof(kname) - 1] = '\0';  /* ensure NUL termination */
    if (strnlen(kname, sizeof(kname)) == 0)
        return (EINVAL);
    if (!secdev_is_valid_name(kname))
        return (EINVAL);

    /* Step 4: Validate the mode. */
    mode = cfg->sc_mode;
    if (mode != SECDEV_MODE_OFF && mode != SECDEV_MODE_ON &&
        mode != SECDEV_MODE_AUTO)
        return (EINVAL);

    /* Step 5: Validate the blob length. */
    bloblen = cfg->sc_bloblen;
    if (bloblen > SECDEV_BLOB_MAX)
        return (EINVAL);

    /* Step 6: Copy in the blob. */
    if (bloblen > 0) {
        kblob = malloc(bloblen, M_SECDEV, M_WAITOK | M_ZERO);
        error = copyin(cfg->sc_blob, kblob, bloblen);
        if (error != 0)
            goto out;
    }

    /* Step 7: Apply the configuration under the lock. */
    mtx_lock(&sc->sc_mtx);
    if (sc->sc_blob != NULL) {
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
    }
    sc->sc_blob = kblob;
    sc->sc_bloblen = bloblen;
    kblob = NULL;  /* ownership transferred */

    strlcpy(sc->sc_name, kname, sizeof(sc->sc_name));
    sc->sc_mode = mode;

    /* Produce the canonical form while still under the lock. */
    secdev_canonicalize(sc->sc_name, canonical, sizeof(canonical));
    mtx_unlock(&sc->sc_mtx);

    /* Step 8: Fill the output fields. */
    bzero(cfg->sc_canonical, sizeof(cfg->sc_canonical));
    strlcpy(cfg->sc_canonical, canonical, sizeof(cfg->sc_canonical));
    /* (The ioctl framework handles copyout of cfg itself.) */

out:
    if (kblob != NULL) {
        explicit_bzero(kblob, bloblen);
        free(kblob, M_SECDEV);
    }
    return (error);
}
```

Ahora revisemos esto frente a los patrones que hemos comentado.

Comprobación de privilegios. `priv_check(PRIV_DRIVER)` es lo primero que se hace. Ningún llamador sin privilegios llega al resto.

Comprobación de jail. `jailed()` antes de cualquier operación que afecte al host.

Validación del nombre. El nombre se lee del `cfg` ya copiado al kernel, se fuerza la terminación NUL (por defensa, por si el usuario no la incluyó) y se filtra mediante `secdev_is_valid_name` (que presumiblemente rechaza caracteres no alfanuméricos).

Validación del modo. Una lista blanca explícita de valores de modo conocidos. Un valor desconocido devuelve `EINVAL` inmediatamente.

Validación de la longitud. La longitud del blob se comprueba frente a un máximo definido antes de usarla para la asignación. Sin esta comprobación, un usuario podría solicitar una asignación de varios gigabytes.

Asignación con `M_ZERO`. El buffer del blob se pone a cero para que, incluso si `copyin` falla a mitad, el contenido sea determinista.

Limpieza en la ruta de error. La etiqueta `out:` libera `kblob` si no transferimos la propiedad. El `kblob = NULL` tras la transferencia evita un double-free. Toda ruta a través de la función llega a `out:` con `kblob` en un estado coherente.

Puesta a cero explícita antes de liberar. El blob antiguo (si existe) se pone a cero antes de ser reemplazado, bajo la suposición de que puede contener datos sensibles. El nuevo blob en la ruta de error también se pone a cero por la misma razón.

Locking. El softc se actualiza bajo `sc_mtx`. La forma canónica se calcula bajo el lock para que el nombre y la forma canónica sean coherentes.

Puesta a cero de la salida. `cfg->sc_canonical` se pone a cero antes de rellenarse, de modo que el relleno y cualquier campo que el canonicalizador no haya establecido queden garantizados a cero.

Esta función tiene unas cuarenta líneas de código real y aproximadamente una docena de decisiones relevantes para la seguridad. Cada decisión por separado es pequeña; el efecto compuesto es una función que puede defenderse frente a casi todos los patrones tratados en este capítulo. Así es como luce el código de driver seguro en la práctica: sin artificios, sin trucos, solo cuidado.

El punto clave es que el código cuidadoso es el más fácil de revisar, el más fácil de mantener y el que tiende a seguir funcionando conforme el driver evoluciona. Los trucos ingeniosos, en cambio, son donde se esconden los bugs.

### Cierre de la sección 3

La entrada del usuario es la mayor fuente individual de bugs de seguridad en drivers en la práctica. Las primitivas que proporciona FreeBSD (copyin, copyout, copyinstr, uiomove) están bien diseñadas y son seguras, pero deben usarse correctamente: longitudes acotadas, valores de retorno comprobados, campos validados, buffers puestos a cero y destinos del tamaño adecuado. Un driver que aplica estas reglas de forma sistemática en cada cruce de la frontera usuario-kernel es un driver difícil de atacar desde el espacio de usuario.

La siguiente sección aborda un tema estrechamente relacionado: la asignación de memoria. Los patrones de las secciones 2 y 3 daban por supuesto que `malloc` y `free` se usan de forma segura. La sección 4 hace explícita esa suposición y muestra qué significa "de forma segura" para el asignador de FreeBSD en particular.

## Sección 4: uso seguro de la asignación de memoria

Un driver que valida sus entradas con cuidado pero asigna memoria de forma descuidada solo ha hecho la mitad del trabajo. La asignación y la liberación de memoria son donde el comportamiento del driver ante condiciones adversas (denegación de servicio, agotamiento, entradas hostiles) resulta más visible, y donde un puñado de bugs sutiles (use-after-free, double-free, fugas) puede convertirse en un compromiso total del sistema. Esta sección cubre el modelo de seguridad del asignador de FreeBSD y los patrones que evitan que un driver se convierta en un criadero de bugs del asignador.

### `malloc(9)` en el kernel

El asignador del kernel de propósito general principal es `malloc(9)`. Su declaración en `/usr/src/sys/sys/malloc.h`:

```c
void *malloc(size_t size, struct malloc_type *type, int flags);
void free(void *addr, struct malloc_type *type);
void zfree(void *addr, struct malloc_type *type);
```

A diferencia del `malloc` de espacio de usuario, la versión del kernel recibe dos argumentos adicionales. El primero, `type`, es una etiqueta `struct malloc_type` que identifica qué parte del kernel está usando la memoria. Esto permite que `vmstat -m` informe, por subsistema, cuánta memoria utiliza cada parte del kernel. Todo driver debería declarar su propio `malloc_type` con `MALLOC_DECLARE` y `MALLOC_DEFINE`, de modo que sus asignaciones sean visibles en la contabilidad.

```c
#include <sys/malloc.h>

MALLOC_DECLARE(M_SECDEV);
MALLOC_DEFINE(M_SECDEV, "secdev", "Secure example driver");
```

El primer argumento, `M_SECDEV`, es el identificador; el segundo, `"secdev"`, es el nombre corto que aparece en `vmstat -m`; el tercero es una descripción más larga. Usa un esquema de nombres que facilite encontrar las asignaciones del driver en la salida del sistema, especialmente cuando estás diagnosticando una fuga.

El argumento `flags` controla el comportamiento de la asignación. Tres flags son esenciales:

- `M_WAITOK`: el asignador puede bloquearse para satisfacer la asignación. La llamada no puede fallar; devuelve un puntero válido o el kernel entra en pánico (lo que ocurre solo en circunstancias muy inusuales).
- `M_NOWAIT`: el asignador no debe bloquearse. Si la memoria no está disponible de inmediato, la llamada devuelve `NULL`. El llamador debe comprobar y gestionar el caso `NULL`.
- `M_ZERO`: la memoria devuelta se pone a cero antes de devolverse. Úsalo siempre que el llamador vaya a rellenar solo parte de la memoria, para evitar filtrar datos basura.

Existen otros (`M_USE_RESERVE`, `M_NODUMP`, `M_NOWAIT`, `M_EXEC`), pero estos tres son los que un driver usa a diario.

### Cuándo usar `M_WAITOK` y cuándo usar `M_NOWAIT`

La elección entre `M_WAITOK` y `M_NOWAIT` la dicta el contexto, no la preferencia.

Usa `M_WAITOK` cuando el driver se encuentre en un contexto que puede bloquearse. Esto ocurre en la mayoría de los puntos de entrada del driver: `open`, `close`, `read`, `write`, `ioctl`, `attach`, `detach`. En estos contextos está permitido bloquearse, y la capacidad del asignador de esperar hasta que haya memoria disponible es una simplificación significativa.

Usa `M_NOWAIT` cuando el driver se encuentre en un contexto que no puede bloquearse. Esto ocurre en los handlers de interrupción, dentro de secciones críticas con spin-mutex y dentro de ciertas rutas de callback que el kernel especifica como no durmientes. En estos contextos, `M_WAITOK` dispararía una aserción de `WITNESS` y un pánico. Aunque `WITNESS` no esté habilitado, bloquearse en un contexto no durmiente puede provocar un deadlock en el sistema.

La regla general: si puedes usar `M_WAITOK`, úsalo. Elimina toda una clase de gestión de errores (la comprobación de NULL) y hace que el comportamiento del driver sea más predecible bajo presión de memoria. Recurre a `M_NOWAIT` solo cuando el contexto lo exija.

Con `M_NOWAIT`, debes comprobar el valor de retorno:

```c
buf = malloc(size, M_SECDEV, M_NOWAIT);
if (buf == NULL)
    return (ENOMEM);
```

No comprobar el valor de retorno equivale a un pánico por puntero nulo que tarde o temprano se producirá. El compilador no te avisará de ello.

### `M_ZERO` es tu aliado

Una de las clases de bug de driver más sutiles es aquella en la que el driver asigna memoria, rellena algunos campos y luego usa o expone el resto. El "resto" es lo que el asignador devolvió, que en FreeBSD es lo que la lista libre del asignador tenía allí en ese momento. Si esa memoria contenía datos de otro subsistema antes de ser liberada, un driver que no la limpie puede exponer esos datos accidentalmente (una fuga de información) o comportarse de forma incorrecta porque un campo que no estableció tiene un valor distinto de cero.

`M_ZERO` previene ambos problemas:

```c
struct secdev_state *st;

st = malloc(sizeof(*st), M_SECDEV, M_WAITOK | M_ZERO);
```

Tras esta llamada, cada byte de `*st` es cero. El driver puede entonces rellenar campos concretos y confiar en que todo lo demás es cero o ha sido establecido explícitamente. Esto es tan importante para la seguridad que muchos autores de drivers FreeBSD tratan `M_ZERO` como el valor por defecto, añadiéndolo salvo que haya una razón concreta para no hacerlo.

La excepción son las asignaciones grandes en las que tienes la certeza de que sobrescribirás cada byte antes de usarlo (por ejemplo, un buffer que se rellena inmediatamente con `copyin`). En ese caso, `M_ZERO` es un pequeño desperdicio y puedes omitirlo. En todos los demás casos, prefiere `M_ZERO` y acepta el pequeño coste.

Un caso especialmente importante: **cualquier estructura que vaya a copiarse al espacio de usuario debe haberse puesto a cero con `M_ZERO` en el momento de la asignación o haber tenido cada byte establecido explícitamente antes de la copia**. De lo contrario, la estructura puede incluir datos del kernel que estaban allí antes. La sección 7 vuelve sobre esto.

### `uma_zone` para asignaciones de alta frecuencia

Para asignaciones que ocurren muchas veces por segundo con un tamaño fijo, FreeBSD ofrece el asignador de zonas UMA:

```c
uma_zone_t uma_zcreate(const char *name, size_t size, ...);
void *uma_zalloc(uma_zone_t zone, int flags);
void uma_zfree(uma_zone_t zone, void *item);
```

Las zonas UMA son significativamente más rápidas que `malloc` para asignaciones pequeñas repetidas, porque mantienen cachés por CPU y evitan el lock global del asignador en la mayoría de las operaciones. Los drivers que gestionan paquetes de red, peticiones de I/O u otros eventos de alta frecuencia suelen usar zonas UMA en lugar de `malloc`.

Las propiedades de seguridad de las zonas UMA son similares a las de `malloc`. Sigues pasando `M_WAITOK` o `M_NOWAIT`. Puedes seguir pasando `M_ZERO` (o puedes usar los argumentos `uminit`/`ctor`/`dtor` de `uma_zcreate_arg` para gestionar el estado inicial). Debes seguir comprobando NULL cuando usas `M_NOWAIT`.

UMA tiene una consideración de seguridad adicional que conviene conocer: **los elementos devueltos a una zona no se ponen a cero por defecto**. Un elemento liberado con `uma_zfree` puede conservar su contenido anterior y ser entregado a una llamada posterior a `uma_zalloc` con ese mismo contenido. Si el elemento contenía datos sensibles, el driver debe ponerlo a cero antes de liberarlo, o pasar `M_ZERO` en cada asignación, o usar la maquinaria de constructores de `uminit` para inicializarlo a cero en el momento de la asignación. La opción más segura por defecto es usar `explicit_bzero` sobre el elemento antes de llamar a `uma_zfree`.

### Use-after-free: qué es y por qué importa

Un bug de use-after-free ocurre cuando un driver libera un puntero y luego lo utiliza. El asignador, en ese momento, casi con total seguridad ya ha cedido esa memoria a otra parte del kernel. Las escrituras en el puntero liberado corrompen esa otra parte del kernel; las lecturas devuelven lo que ahora esté almacenado allí.

El patrón clásico:

```c
/* UNSAFE */
static void
secdev_cleanup(struct secdev_softc *sc)
{
    free(sc->sc_buf, M_SECDEV);
    /* sc->sc_buf is now dangling */

    /* later, elsewhere, something calls: */
    secdev_use_buf(sc);   /* crash or silent corruption */
}
```

La solución tiene dos partes. Primero, establece el puntero a NULL inmediatamente después de liberarlo, de modo que cualquier uso posterior sea una desreferencia de puntero nulo (un fallo inmediato y diagnosticable) en lugar de un acceso a un puntero colgante (corrupción silenciosa):

```c
free(sc->sc_buf, M_SECDEV);
sc->sc_buf = NULL;
```

Segundo, audita los caminos de código que podrían seguir manteniendo referencias a la memoria liberada. La asignación a NULL evita fallos en los accesos a `sc->sc_buf`, pero una variable local o el parámetro de una función que todavía conserve el puntero antiguo no está protegida. La disciplina consiste en liberar la memoria únicamente cuando estás seguro de que nadie más tiene un puntero a ella. Los contadores de referencias (`refcount(9)`) son el primitivo de FreeBSD para esto.

Una variante del bug es el patrón **use-after-detach**, en el que un driver libera su softc durante `detach` pero un manejador de interrupciones o un callback sigue ejecutándose y accede al softc ya liberado. La solución consiste en drenar toda la actividad asíncrona antes de liberar en `detach`: cancela los callouts pendientes con `callout_drain`, drena las taskqueues con `taskqueue_drain`, desinstala los manejadores de interrupciones con `bus_teardown_intr`, y así sucesivamente. Una vez que todos los caminos asíncronos están en reposo, la liberación es segura.

### Double-free: qué es y por qué importa

Un double-free ocurre cuando un driver llama a `free` dos veces sobre el mismo puntero. La primera llamada devuelve la memoria al asignador. La segunda corrompe la contabilidad interna del asignador, porque intenta insertar la misma memoria en la lista libre dos veces.

El asignador de FreeBSD detecta muchos double-frees y entra en pánico de inmediato (especialmente con `INVARIANTS` activado). Pero algunos double-frees escapan a la detección, y las consecuencias son sutiles: una asignación posterior puede devolver memoria que en apariencia está disponible pero que en realidad sigue en uso en algún lugar.

La prevención es el mismo patrón de asignación a NULL:

```c
free(sc->sc_buf, M_SECDEV);
sc->sc_buf = NULL;
```

`free(NULL, ...)` está definido como una operación sin efecto en FreeBSD (como en la mayoría de los asignadores), por lo que una segunda llamada con `sc->sc_buf == NULL` no hace nada. La asignación a NULL convierte el double-free en una operación inocua.

Un patrón relacionado es el **double-free en el camino de error**, en el que la lógica de limpieza de una función libera un puntero y, a continuación, una función externa también libera el mismo puntero. La defensa consiste en decidir explícitamente qué función es propietaria de cada asignación y en transferir esa propiedad en momentos bien definidos. "¿Quién libera esto?" es una pregunta que debe tener una respuesta clara en cada línea del código.

### Las fugas de memoria son un problema de seguridad

Una fuga de memoria es una porción de memoria que se asigna y nunca se libera. En un driver de larga ejecución, las fugas se acumulan. Con el tiempo el kernel se queda sin memoria, ya sea para el subsistema del driver o para el sistema en su conjunto, y las consecuencias son graves.

¿Por qué es una fuga un problema de seguridad? Por dos razones. Primera, una fuga es un vector de denegación de servicio: un atacante que pueda provocar una asignación sin su correspondiente liberación puede agotar la memoria. Si el atacante no tiene privilegios pero el `ioctl` del driver asigna memoria en cada llamada, el atacante puede iterar sobre `ioctl` hasta que el kernel elimine algo importante por OOM. Segunda, una fuga a menudo oculta otros bugs: la presión acumulada por la fuga modifica el comportamiento de las asignaciones posteriores (más fallos frecuentes de `M_NOWAIT`, un comportamiento más impredecible de la caché de páginas), lo que puede hacer que bugs relacionados con condiciones de carrera o dependencias de asignación afloren en producción.

La prevención radica en la disciplina de propiedad de las asignaciones: por cada `malloc`, debe haber exactamente un `free`, alcanzable en todos los caminos de código. La herramienta `vmstat -m` de FreeBSD facilita en la práctica el seguimiento de fugas: `vmstat -m | grep secdev` muestra, por tipo, cuántas asignaciones están activas. Un driver con una fuga mostrará un número que crece de forma sostenida bajo carga; un driver sin fugas mostrará un número estable.

Para los drivers nuevos, merece la pena hacer pruebas de estrés en el laboratorio buscando fugas: abre y cierra el dispositivo un millón de veces en un bucle, ejecuta la matriz completa de `ioctl` repetidamente, observa `vmstat -m` para el tipo del driver y busca crecimiento. Cualquier crecimiento sostenido es una fuga. Las fugas detectadas en el laboratorio son mil veces más baratas de corregir que las detectadas en producción.

### `explicit_bzero` y `zfree` para datos sensibles

Algunos datos no deberían poder persistir en memoria después de que el driver haya terminado con ellos. Las claves criptográficas, las contraseñas de usuario, los secretos del dispositivo y cualquier información cuya exposición en una instantánea de memoria resultara perjudicial deben borrarse antes de liberar la memoria.

El enfoque ingenuo consiste en usar `bzero` o `memset(buf, 0, len)` antes del `free`. Esto funciona, pero tiene un defecto sutil: el optimizador puede eliminar el `bzero` si puede demostrar que la memoria no se lee después. El razonamiento del optimizador es correcto desde el punto de vista de la semántica del lenguaje, pero destruye la intención de seguridad.

El primitivo correcto es `explicit_bzero(9)`:

```c
void explicit_bzero(void *buf, size_t len);
```

`explicit_bzero` está declarado en `/usr/src/sys/sys/systm.h`. Realiza el borrado y el compilador garantiza que no será eliminado por la optimización, incluso si la memoria no se lee después. Úsalo para cualquier buffer que contenga datos sensibles:

```c
explicit_bzero(key_buf, sizeof(key_buf));
free(key_buf, M_SECDEV);
```

FreeBSD también proporciona `zfree(9)`, que borra la memoria antes de liberarla:

```c
void zfree(void *addr, struct malloc_type *type);
```

`zfree` es conveniente: combina el borrado y la liberación en una sola llamada. Primero borra la memoria usando `explicit_bzero` y luego la libera. Usa `zfree` cuando estés a punto de liberar un buffer que contenía datos sensibles. Usa `explicit_bzero` seguido de `free` si necesitas borrar el buffer sin liberarlo, o si estás trabajando con memoria procedente de una fuente distinta a `malloc`.

Una pregunta habitual: ¿qué son "datos sensibles"? La respuesta conservadora es que cualquier dato que provenga del espacio de usuario debe tratarse como sensible, porque no puedes saber qué representa para el usuario. Una respuesta más pragmática es que los datos que son claramente un secreto (una clave, un hash de contraseña, un nonce, material de autenticación) deben borrarse, y los datos que podrían revelar información sobre las actividades del usuario (contenidos de archivos, cargas útiles de red, texto de comandos) deberían borrarse cuando el driver haya terminado con ellos. Ante la duda, borra. El coste es pequeño.

### Las etiquetas `malloc_type` y la trazabilidad

La etiqueta `malloc_type` en cada asignación tiene varios propósitos. Hace visibles las asignaciones en `vmstat -m`. Ayuda en la depuración de pánicos, porque la etiqueta queda registrada en los metadatos del asignador. Ayuda en la contabilidad propia del asignador y, en algunas configuraciones, permite establecer límites de memoria por tipo.

Un driver que utiliza una única etiqueta `malloc_type` para todas sus asignaciones es más fácil de auditar que uno que usa muchas. Crea una etiqueta por subsistema lógico dentro del driver, no una por punto de asignación. Para drivers pequeños, una única etiqueta suele ser suficiente.

El patrón de declaración:

```c
/* At the top of the driver source file: */
MALLOC_DECLARE(M_SECDEV);
MALLOC_DEFINE(M_SECDEV, "secdev", "Secure example driver");

/* Allocations throughout the driver use M_SECDEV: */
buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
```

`MALLOC_DECLARE` declara la etiqueta para visibilidad externa; `MALLOC_DEFINE` es quien la registra realmente (e incorpora al sistema de contabilidad). Ambas son necesarias. No pongas `MALLOC_DEFINE` en un archivo de cabecera, porque el enlazador del kernel se quejará de definiciones duplicadas si varios archivos objeto incluyen la cabecera.

### El ciclo de vida del softc

El softc es el estado por instancia del driver. Habitualmente se asigna durante `attach` y se libera durante `detach`. El ciclo de vida del softc es uno de los aspectos más importantes que hay que gestionar correctamente en un driver.

La asignación suele realizarse mediante `device_get_softc`, que devuelve un puntero a una estructura cuyo tamaño se declaró en el momento del registro del driver. Esto significa que la memoria del softc es propiedad del bus, no del driver; el driver no llama a `malloc` para ella y no llama a `free`. El bus asigna el softc cuando el driver se vincula al dispositivo y lo libera cuando el driver se desvincula.

Pero el softc contiene a menudo punteros a otra memoria que el driver sí asignó. Esos punteros deben liberarse en `detach`, en el orden inverso al de su asignación. Un patrón típico:

```c
static int
secdev_detach(device_t dev)
{
    struct secdev_softc *sc = device_get_softc(dev);

    /* Reverse order of allocation. */

    /* 1. Stop taking new work. */
    destroy_dev(sc->sc_cdev);

    /* 2. Drain async activity. */
    callout_drain(&sc->sc_callout);
    taskqueue_drain(sc->sc_taskqueue, &sc->sc_task);

    /* 3. Free allocated resources. */
    if (sc->sc_blob != NULL) {
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
        sc->sc_blob = NULL;
    }

    /* 4. Destroy synchronization primitives. */
    mtx_destroy(&sc->sc_mtx);

    /* 5. Release bus resources. */
    bus_release_resources(dev, secdev_spec, sc->sc_res);

    return (0);
}
```

Cada paso aborda una preocupación específica. El orden importa: destruye el nodo del dispositivo antes de liberar los recursos de los que dependen sus callbacks; drena la actividad asíncrona antes de liberar los datos que dichos caminos asíncronos podrían tocar; destruye los primitivos de sincronización al final.

Un error en cualquiera de estos órdenes es un bug. El orden incorrecto puede producir patrones de use-after-free o double-free. El laboratorio posterior en el capítulo recorre una función `detach` que tiene bugs de ordenación sutiles y te pide que los corrijas.

### Un patrón completo de asignación y liberación

Reuniendo todos los patrones, aquí tienes una secuencia segura de asignación y uso:

```c
static int
secdev_load_blob(struct secdev_softc *sc, struct secdev_blob_args *args)
{
    char *blob = NULL;
    int error;

    if (args->len == 0 || args->len > SECDEV_MAX_BLOB)
        return (EINVAL);

    blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

    error = copyin((const void *)(uintptr_t)args->data, blob, args->len);
    if (error != 0)
        goto done;

    error = secdev_validate_blob(blob, args->len);
    if (error != 0)
        goto done;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_blob != NULL) {
        /* replace existing */
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
    }
    sc->sc_blob = blob;
    sc->sc_bloblen = args->len;
    blob = NULL;  /* ownership transferred */
    mtx_unlock(&sc->sc_mtx);

done:
    if (blob != NULL) {
        explicit_bzero(blob, args->len);
        free(blob, M_SECDEV);
    }
    return (error);
}
```

La función tiene un único punto de salida a través de la etiqueta `done`. La asignación `blob = NULL` tras la transferencia de propiedad garantiza que la limpieza en `done` detecte la transferencia y no libere de nuevo. El `explicit_bzero` antes de cada `free` borra el buffer por si contenía datos sensibles. El `sc->sc_blob` existente, si lo hay, se borra y libera antes de ser reemplazado, para evitar filtrar el contenido del antiguo blob.

Este patrón (punto de salida único, transferencia de propiedad, borrado explícito, asignación comprobada, `copyin` comprobado) aparece en variaciones a lo largo del kernel de FreeBSD. Apréndelo bien.

### Una mirada más detallada a las zonas UMA

`malloc(9)` es un asignador de propósito general adecuado para tamaños variables. Para objetos de tamaño fijo que se asignan y liberan con frecuencia, el asignador de zonas UMA suele ser la mejor opción. UMA son las siglas de Universal Memory Allocator y está declarado en `/usr/src/sys/vm/uma.h`.

Una zona UMA se crea una sola vez, al cargar el módulo, y contiene un pool de objetos de tamaño fijo. `uma_zalloc(9)` devuelve un objeto del pool (asignando uno nuevo si es necesario). `uma_zfree(9)` devuelve un objeto al pool (o lo libera de vuelta al kernel si el pool está lleno). Dado que las asignaciones provienen de un pool preconfigurado, son más rápidas que el `malloc` general y tienen mejor localidad de caché.

Creación de una zona:

```c
static uma_zone_t secdev_packet_zone;

static int
secdev_modevent(module_t mod, int event, void *arg)
{
    switch (event) {
    case MOD_LOAD:
        secdev_packet_zone = uma_zcreate("secdev_packet",
            sizeof(struct secdev_packet),
            NULL,   /* ctor */
            NULL,   /* dtor */
            NULL,   /* init */
            NULL,   /* fini */
            UMA_ALIGN_PTR, 0);
        return (0);

    case MOD_UNLOAD:
        uma_zdestroy(secdev_packet_zone);
        return (0);
    }
    return (EOPNOTSUPP);
}
```

Uso de una zona:

```c
struct secdev_packet *pkt;

pkt = uma_zalloc(secdev_packet_zone, M_WAITOK | M_ZERO);
/* ... use pkt ... */
uma_zfree(secdev_packet_zone, pkt);
```

Las ventajas de seguridad de una zona UMA frente a `malloc`:

Una zona puede tener un constructor y un destructor que inicialicen o finalicen los objetos. Esto puede garantizar que cada objeto devuelto al llamador se encuentre en un estado conocido.

Una zona tiene nombre, por lo que `vmstat -z` atribuye las asignaciones a ella. Esto ayuda a detectar fugas y patrones de memoria inusuales en subsistemas específicos.

El pool de objetos puede vaciarse bajo presión de memoria. Una asignación de malloc se mantiene durante su ciclo de vida; un objeto de una zona UMA puede devolverse al kernel al liberarse si el pool está por encima de su marca de nivel alto.

Los riesgos de seguridad:

Un objeto devuelto a la zona no se borra automáticamente. Si la zona contiene objetos que pueden incluir datos sensibles, añade un destructor que los borre, o borra explícitamente antes de liberar:

```c
explicit_bzero(pkt, sizeof(*pkt));
uma_zfree(secdev_packet_zone, pkt);
```

Dado que UMA reutiliza objetos rápidamente, un objeto que acabas de liberar puede entregarse a otro llamador casi de inmediato. Si el otro llamador es un thread diferente en otro subsistema, los datos residuales podrían fluir entre ellos. La solución, de nuevo, es el borrado explícito.

Una función destructora pasada a `uma_zcreate` se llama cuando un objeto está a punto de liberarse de vuelta al kernel (no cuando vuelve al pool). Para borrar en cada liberación, usa `M_ZERO` en `uma_zalloc` (que borra al asignar, equivalente a un `bzero` inmediato después) o borra explícitamente.

Las zonas UMA no son apropiadas para todas las asignaciones de un driver. Para asignaciones puntuales o irregulares, `malloc(9)` es más sencillo. Para objetos de tamaño fijo de alta frecuencia, UMA gana en rendimiento y facilita la contabilidad de la memoria. Elige en función del patrón de acceso.

### Conteo de referencias para objetos compartidos

Cuando un objeto de tu driver puede ser retenido por múltiples contextos (un softc referenciado tanto por un callout como por descriptores de archivo en espacio de usuario, por ejemplo), el recuento de referencias es la herramienta canónica para gestionar el ciclo de vida. La familia `refcount(9)` en `/usr/src/sys/sys/refcount.h` proporciona sencillos helpers atómicos:

```c
refcount_init(&obj->refcnt, 1);  /* initial reference */
refcount_acquire(&obj->refcnt);  /* acquire an additional reference */
if (refcount_release(&obj->refcnt)) {
    /* last reference dropped; caller frees */
    free(obj, M_SECDEV);
}
```

El invariante es sencillo: cada contexto que mantiene un puntero al objeto también mantiene una referencia. Cuando termina, la libera. El contexto que la libera en último lugar es el responsable de liberar la memoria.

Utilizados correctamente, los refcounts evitan la clásica ambigüedad de «¿quién lo libera?». Utilizados incorrectamente (adquisiciones y liberaciones desbalanceadas), producen fugas de memoria o errores de use-after-free. La disciplina es la siguiente:

Todo camino de código que obtiene un puntero al objeto adquiere una referencia.

Todo camino de código que libera el puntero llama a `refcount_release` y comprueba el valor de retorno.

Una única referencia «propietaria» la mantiene quien creó el objeto; ese propietario es el responsable de liberar por defecto.

Incluso un uso sencillo de refcounts detecta una amplia clase de errores de ciclo de vida. Para drivers complejos con múltiples contextos concurrentes, los refcounts son indispensables.

### Cerrando la sección 4

El asignador de FreeBSD es seguro si se usa correctamente. Las reglas son sencillas: comprueba el retorno de `M_NOWAIT`, prefiere `M_ZERO`, pone a cero los datos sensibles antes de liberar, empareja cada `malloc` con exactamente un `free` en cada camino de ejecución, establece los punteros a NULL tras liberar, drena la actividad asíncrona antes de liberar las estructuras que esa actividad toca, y usa un `malloc_type` específico por driver para llevar una contabilidad clara del uso de memoria. Un driver que siga estas reglas no tendrá fugas de memoria, accesos tras liberación ni liberaciones dobles.

La siguiente sección aborda una clase de bug relacionada pero diferente: las condiciones de carrera y los bugs TOCTOU. Son situaciones en las que dos threads o dos momentos en el tiempo interactúan de forma errónea, y donde suelen ocultarse las consecuencias de seguridad.

## Sección 5: Prevención de condiciones de carrera y bugs TOCTOU

Una condición de carrera se produce cuando la corrección de un driver depende de la sincronización relativa de eventos que no controla. Un bug TOCTOU (Time of Check to Time of Use, tiempo de comprobación a tiempo de uso) es un tipo especial de condición de carrera en el que el driver comprueba una condición en un momento dado y actúa sobre los mismos datos en un momento posterior, asumiendo que la condición sigue siendo cierta. Entre ambos momentos, algo cambia. La comprobación es válida. La acción es válida. La combinación es un bug. Desde el punto de vista de la seguridad, las condiciones de carrera y los bugs TOCTOU son algunos de los fallos más peligrosos que puede tener un driver, porque a menudo permiten a un atacante eludir comprobaciones que parecen correctas cuando se leen de forma aislada.

El capítulo 19 ya cubrió la concurrencia, los locks y los primitivos de sincronización. El objetivo allí era la corrección. Esta sección vuelve sobre las mismas herramientas desde una perspectiva de seguridad. No nos preguntamos «¿se bloqueará mi driver?». Nos preguntamos «¿puede un atacante organizar los tiempos de manera que una comprobación que escribí no sirva para nada?».

### Cómo surgen las condiciones de carrera en los drivers

Un driver de FreeBSD opera en un entorno multi-thread. Varias cosas pueden estar ocurriendo al mismo tiempo:

Dos procesos de usuario distintos pueden llamar a `read(2)`, `write(2)` o `ioctl(2)` sobre el mismo archivo de dispositivo. Si el driver tiene un único softc, ambas llamadas operan sobre el mismo estado.

Un thread puede estar ejecutando tu manejador de ioctl mientras un manejador de interrupción del mismo dispositivo se dispara en otra CPU.

Un thread de usuario puede estar en mitad de tu driver mientras también se ejecuta un callout o una entrada de taskqueue programada con anterioridad.

El dispositivo puede desconectarse, haciendo que detach se ejecute mientras cualquiera de los anteriores aún está en curso.

Cualquier dato compartido al que accedan más de uno de estos contextos sin sincronización adecuada es una condición de carrera potencial. La condición de carrera se convierte en un problema de seguridad cuando los datos compartidos controlan el acceso, validan la entrada, registran tamaños de buffer o almacenan información de ciclo de vida.

### El patrón TOCTOU

El patrón TOCTOU más simple en un driver tiene este aspecto:

```c
if (sc->sc_initialized) {
    use(sc->sc_buffer);
}
```

Léelo con atención. Nada en él es obviamente incorrecto. El driver comprueba que el buffer está inicializado y luego lo usa. Pero si otro thread puede establecer `sc->sc_initialized` a `false` y liberar `sc->sc_buffer` entre la comprobación y el uso, el uso accede a memoria liberada. El atacante no necesita corromper el indicador ni el puntero. Solo necesita elegir el momento oportuno.

Un TOCTOU más sutil ocurre con la memoria de usuario:

```c
if (args->len > MAX_LEN)
    return (EINVAL);
error = copyin(args->data, kbuf, args->len);
```

Fíjate en `args`. Si ya se ha copiado al espacio del kernel, esto es seguro. Pero si `args` aún apunta al espacio de usuario, un segundo thread de usuario puede cambiar `args->len` entre la comprobación y el `copyin`. La comprobación valida la longitud antigua. La copia usa la longitud nueva. Si la nueva longitud supera `MAX_LEN`, el `copyin` desborda `kbuf`.

La solución es copiar primero y comprobar después, algo que ya cubrimos en la sección 3. Esta técnica existe precisamente porque el TOCTOU sobre memoria de usuario es un vector de ataque real. Copia siempre los datos del usuario al espacio del kernel primero, luego valida, luego usa.

### Ejemplo real: ioctl con una ruta

Imagina un ioctl que recibe una ruta y hace algo con el archivo:

```c
/* UNSAFE */
static int
secdev_open_path(struct secdev_softc *sc, struct secdev_path_arg *args)
{
    struct nameidata nd;
    int error;

    /* Check path length */
    if (strnlen(args->path, sizeof(args->path)) >= sizeof(args->path))
        return (ENAMETOOLONG);

    NDINIT(&nd, LOOKUP, 0, UIO_USERSPACE, args->path);
    error = namei(&nd);
    /* ... */
}
```

Este código tiene dos condiciones de carrera. En primer lugar, `args->path` sigue estando en el espacio de usuario si `args` no se copió al kernel; un thread de usuario puede cambiarlo entre la comprobación con `strnlen` y la llamada a `namei`. En segundo lugar, aunque `args` se hubiera copiado, usar `UIO_USERSPACE` le indica a la capa VFS que lea la ruta desde el espacio de usuario, momento en el que el proceso puede modificarla de nuevo antes de que VFS la lea. La solución es copiar la ruta al espacio del kernel con `copyinstr(9)`, validarla como una cadena del kernel y pasarla al VFS con `UIO_SYSSPACE`.

```c
/* SAFE */
static int
secdev_open_path(struct secdev_softc *sc, struct secdev_path_arg *args)
{
    struct nameidata nd;
    char kpath[MAXPATHLEN];
    size_t done;
    int error;

    error = copyinstr(args->path, kpath, sizeof(kpath), &done);
    if (error != 0)
        return (error);

    NDINIT(&nd, LOOKUP, 0, UIO_SYSSPACE, kpath);
    error = namei(&nd);
    /* ... */
}
```

La versión corregida copia la ruta al kernel exactamente una vez, la valida (gracias a que `copyinstr` limita la longitud y garantiza un terminador NUL) y entrega una cadena del kernel estable a la capa VFS. El proceso de usuario puede cambiar `args->path` tantas veces como quiera; nosotros ya no leemos de allí.

### Estado compartido y locking

Para las condiciones de carrera entre contextos concurrentes dentro del kernel, la herramienta es un lock. FreeBSD ofrece varios. Los más habituales en drivers son:

`mtx_t`, un mutex, creado con `mtx_init(9)`. Los mutex son rápidos, de corta duración y no deben mantenerse durante períodos de suspensión. Úsalos para proteger una sección crítica pequeña.

`sx_t`, un lock compartido-exclusivo, creado con `sx_init(9)`. Los locks compartidos-exclusivos pueden mantenerse durante períodos de suspensión. Úsalos cuando la sección crítica incluye algo como `malloc(M_WAITOK)` o una llamada al VFS.

`struct rwlock`, un lock de lectura-escritura, para el caso en que predominan las lecturas. Múltiples lectores pueden mantener el lock en modo compartido; un escritor exclusivo excluye a todos los lectores.

`struct mtx` combinado con variables de condición (`cv_init(9)`, `cv_wait(9)`, `cv_signal(9)`) para patrones productor-consumidor.

Las reglas para un locking seguro son simples y absolutas:

Define exactamente qué datos protege cada lock. Escríbelo en un comentario junto al campo del softc.

Adquiere el lock antes de leer o escribir los datos protegidos. Libéralo después.

No mantengas los locks más tiempo del necesario. Las secciones críticas largas perjudican el rendimiento y aumentan el riesgo de deadlock.

Adquiere múltiples locks en un orden coherente en todos los caminos de ejecución. Un orden inconsistente lleva al deadlock.

No realices una suspensión mientras mantienes un mutex. Convierte a un sx lock o libera el mutex primero.

No llames al espacio de usuario (`copyin`, `copyout`) mientras mantienes un mutex. Copia primero, luego adquiere el lock. Libéralo, luego copia de vuelta.

### Más de cerca: corregir un driver con condición de carrera

Considera el siguiente manejador mínimo:

```c
/* UNSAFE: races on sc_open */
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    if (sc->sc_open)
        return (EBUSY);
    sc->sc_open = true;
    return (0);
}

static int
secdev_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    sc->sc_open = false;
    return (0);
}
```

La intención es que solo un proceso pueda tener el dispositivo abierto al mismo tiempo. El bug es que `sc_open` se comprueba y establece sin un lock. Dos llamadas concurrentes a `open(2)` pueden ambas leer `sc_open == false`, ambas decidir que son las primeras y ambas establecerlo a `true`. Ambas tienen éxito. Ahora dos procesos comparten un dispositivo que debía ser exclusivo. Esta es una clase de bug del mundo real que ha afectado a drivers reales. La corrección:

```c
/* SAFE */
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_open)
        error = EBUSY;
    else
        sc->sc_open = true;
    mtx_unlock(&sc->sc_mtx);
    return (error);
}

static int
secdev_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    sc->sc_open = false;
    mtx_unlock(&sc->sc_mtx);
    return (0);
}
```

Ahora la lectura y la escritura ocurren dentro de una única sección crítica. Solo un llamador a la vez puede estar dentro de esa sección, por lo que la secuencia comprobar-y-establecer es atómica desde la perspectiva de cualquier otro llamador.

### Condiciones de carrera de ciclo de vida en detach

Las condiciones de carrera más difíciles en los drivers son las de ciclo de vida en torno a detach. El dispositivo desaparece, pero un thread de usuario sigue dentro de tu manejador de ioctl, o hay una interrupción en vuelo, o hay un callout pendiente. Si detach libera el softc mientras uno de estos lo referencia, tienes un acceso tras liberación.

FreeBSD te proporciona herramientas para gestionar esto:

`callout_drain(9)` espera a que cualquier callout programado termine antes de retornar. Llámalo en detach antes de liberar cualquier cosa que el callout toque.

`taskqueue_drain(9)` y `taskqueue_drain_all(9)` esperan a que las tareas pendientes se completen.

`destroy_dev(9)` marca un dispositivo de caracteres como desaparecido y espera a que todos los threads en vuelo abandonen los métodos d_* del dispositivo antes de retornar. Tras `destroy_dev`, ningún thread nuevo puede entrar y ningún thread antiguo permanece.

`bus_teardown_intr(9)` elimina un manejador de interrupción y espera a que cualquier instancia en vuelo de ese manejador se complete.

Una función detach correcta en un driver que tiene todos estos recursos tiene aproximadamente este aspecto:

```c
static int
secdev_detach(device_t dev)
{
    struct secdev_softc *sc = device_get_softc(dev);

    /* 1. Prevent new user-space entries. */
    if (sc->sc_cdev != NULL)
        destroy_dev(sc->sc_cdev);

    /* 2. Drain asynchronous activity. */
    callout_drain(&sc->sc_callout);
    taskqueue_drain_all(sc->sc_taskqueue);

    /* 3. Tear down interrupts (if any). */
    if (sc->sc_intr_cookie != NULL)
        bus_teardown_intr(dev, sc->sc_irq, sc->sc_intr_cookie);

    /* 4. Free resources. */
    /* ... */

    /* 5. Destroy lock last. */
    mtx_destroy(&sc->sc_mtx);
    return (0);
}
```

El orden importa. Primero dejamos de aceptar nuevo trabajo, luego drenamos todo el trabajo en vuelo y por último liberamos los recursos que ese trabajo en vuelo estaba usando. Si liberásemos los recursos primero y drenáramos después, un callout que aún estuviera en ejecución podría acceder a memoria liberada. Eso es un acceso tras liberación clásico en tiempo de detach, y es un bug de seguridad, no solo un fallo del sistema.

### Operaciones atómicas y código lock-free

FreeBSD proporciona operaciones atómicas (`atomic_add_int`, `atomic_cmpset_int` y similares) en `/usr/src/sys/sys/atomic_common.h` y en cabeceras específicas de arquitectura. Los atómicos son útiles para contadores, recuentos de referencias e indicadores simples. No son un sustituto de los locks cuando varios campos relacionados deben cambiar juntos.

Un error habitual de los principiantes es decir «usaré un atómico para evitar un lock». A veces esto es correcto. Con frecuencia lleva a una estructura de datos sutilmente rota, porque la operación atómica solo hace seguro un campo, mientras que el código realmente necesitaba actualizar dos campos juntos.

La regla segura es: si puedes expresar el invariante con una única lectura o escritura atómica, un atómico puede ser adecuado. Si el invariante abarca varios campos o una condición compuesta, usa un lock.

### Los refcounts como herramienta de ciclo de vida

Cuando un objeto puede ser referenciado desde múltiples contextos, un refcount ayuda a gestionar su ciclo de vida. `refcount_init`, `refcount_acquire` y `refcount_release` (declarados en `/usr/src/sys/sys/refcount.h`) te proporcionan un refcount atómico sencillo. La última liberación retorna true, momento en el que el llamador es responsable de liberar el objeto.

Los refcounts resuelven el problema clásico en el que el contexto A y el contexto B mantienen ambos un puntero a un objeto. Cualquiera de los dos puede terminar con él primero. El que termina último lo libera. Ninguno de los dos necesita saber si el otro ha terminado, porque el refcount lo registra por ellos.

Un driver que usa un refcount sobre su softc, o sobre el estado por apertura, puede liberar ese estado de forma segura incluso bajo acceso concurrente. El coste es prestar atención en cada punto de entrada y salida para equilibrar las adquisiciones y las liberaciones.

### Orden de acceso y barreras de memoria

Las CPU modernas reordenan los accesos a memoria. Una escritura en tu código puede hacerse visible para otras CPU en un orden distinto al que se emitió. Esto suele ser invisible porque los locks de FreeBSD incluyen las barreras necesarias. Al escribir código lock-free, puede que necesites barreras explícitas (`atomic_thread_fence_acq`, `atomic_thread_fence_rel` y variantes). Para casi todo el código de driver, usar un lock elimina la necesidad de pensar en barreras. Esa es otra razón para preferir los locks sobre las construcciones lock-free escritas a mano cuando aún estás aprendiendo.

### Seguridad con señales y suspensión

Si tu driver se suspende esperando un evento, usando `msleep(9)`, `cv_wait(9)` o `sx_sleep(9)`, usa la variante interrumpible (`msleep(..., PCATCH)`) cuando la espera la inicia el espacio de usuario. De lo contrario, un dispositivo bloqueado puede mantener un proceso en un estado no interrumpible indefinidamente, y un atacante suficientemente paciente puede aprovechar eso para agotar las ranuras de proceso. La variante interrumpible permite que el proceso reciba señales.

Comprueba siempre el valor de retorno de una suspensión. Si retorna un valor distinto de cero, la suspensión fue interrumpida (ya sea por una señal o por otra condición), y el driver debe normalmente deshacer la operación y retornar al espacio de usuario. No asumas que la condición es cierta solo porque la suspensión retornó.

### Limitación de tasa y agotamiento de recursos

Una última preocupación de seguridad relacionada con las condiciones de carrera es el agotamiento de recursos. Si un atacante puede llamar a tu ioctl un millón de veces por segundo, y cada llamada asigna un kilobyte de memoria del kernel que no se libera hasta el cierre, puede llevar al sistema a quedarse sin memoria. Esto es un ataque de denegación de servicio, y un driver cuidadoso se defiende contra ello.

Las defensas son: limitar el uso de recursos por apertura, limitar el uso de recursos de forma global y aplicar limitación de tasa a las operaciones costosas. FreeBSD proporciona `eventratecheck(9)` y `ppsratecheck(9)` en `/usr/src/sys/sys/time.h` para la limitación de tasa, y puedes construir tus propios contadores donde sea necesario. El principio es que el coste de llamar a tu driver no debería ser excesivamente asimétrico: si una sola llamada asigna megabytes de estado, o el llamador necesita una comprobación de privilegios o el driver necesita un límite estricto.

### Recuperación basada en épocas: un patrón de lectores sin lock

Para estructuras de datos que se leen con mucha frecuencia, en las que los lectores nunca deben bloquearse y los escritores son infrecuentes, FreeBSD ofrece un marco de recuperación basado en épocas en `/usr/src/sys/sys/epoch.h`. Los lectores entran en una época, acceden a los datos compartidos sin tomar un lock y salen de la época. Los escritores actualizan los datos (habitualmente reemplazando un puntero) y luego esperan a que todos los lectores que se encuentren en ese momento dentro de una época salgan, antes de liberar los datos antiguos.

Este patrón resulta útil en código de driver que realiza lecturas frecuentes en un hot path y quiere evitar el coste de los locks en ese punto. Por ejemplo, un driver de red que busca una regla en una estructura similar a una tabla de enrutamiento en cada paquete puede querer que los lectores funcionen sin lock.

```c
epoch_enter(secdev_epoch);
rule = atomic_load_ptr(&sc->sc_rules);
/* use rule; must not outlive the epoch */
do_stuff(rule);
epoch_exit(secdev_epoch);
```

Un escritor que reemplaza el conjunto de reglas:

```c
new_rules = build_new_rules();
old_rules = atomic_load_ptr(&sc->sc_rules);
atomic_store_ptr(&sc->sc_rules, new_rules);
epoch_wait(secdev_epoch);
free(old_rules, M_SECDEV);
```

`epoch_wait` bloquea al escritor hasta que todos los lectores que entraron antes del almacenamiento hayan salido. Una vez que retorna, ningún lector puede seguir usando `old_rules`, por lo que es seguro liberarlo.

Las consideraciones de seguridad relacionadas con las épocas son sutiles:

Un lector dentro de una época puede mantener un puntero a algo que está a punto de ser reemplazado. El lector debe terminar de usar ese puntero antes de salir de la época; cualquier uso posterior a la salida constituye un use-after-free.

Un lector dentro de una época no puede dormir. La época es un lock asimétrico: los escritores esperan a los lectores, de modo que un lector que duerma puede dejar a los escritores sin servicio indefinidamente.

El escritor debe garantizar que la operación de reemplazo es atómica desde la perspectiva del lector. Para un único puntero, un almacenamiento atómico basta. Para actualizaciones más complejas puede ser necesario usar dos épocas o una secuencia de tipo read-copy-update.

Usadas correctamente, las épocas ofrecen un rendimiento muy alto en cargas de trabajo intensivas en lecturas. Usadas incorrectamente (un lector que duerme o un escritor que no espera) producen condiciones de carrera difíciles de reproducir y de diagnosticar. Los principiantes deberían preferir los locks hasta que el perfil de rendimiento justifique la complejidad del código basado en épocas.

### Cerrando la sección 5

Las condiciones de carrera y los bugs de tipo TOCTOU son bugs basados en el tiempo. Ocurren cuando dos contextos acceden a datos compartidos sin coordinación, o cuando un driver comprueba una condición y actúa sobre ella en dos momentos distintos. Las herramientas para prevenirlos son claras: copiar los datos del usuario al kernel una sola vez y trabajar a partir de esa copia; usar un lock alrededor de cada acceso a estado mutable compartido; definir qué protege cada lock y mantenerlo durante toda la secuencia de comprobación y acción; drenar el trabajo asíncrono antes de liberar las estructuras que utiliza; y usar refcounts para gestionar el ciclo de vida en múltiples contextos.

Nada de esto es nuevo en la programación concurrente. Lo que sí es nueva es la mentalidad: una condición de carrera en un driver no es únicamente un problema de corrección. Es un problema de seguridad, porque un atacante puede a menudo organizar el tiempo que necesita para explotarla. La siguiente sección se aleja del tiempo y examina un tipo diferente de defensa: comprobaciones de privilegios, credenciales y control de acceso.

## Sección 6: Control de acceso y aplicación de privilegios

No todas las operaciones que expone un driver deben estar disponibles para cualquier usuario. Leer un sensor de temperatura puede ser adecuado para cualquiera. Reprogramar el firmware del dispositivo debería requerir privilegio. Escribir bytes en bruto en un controlador de almacenamiento probablemente debería requerir incluso más. Esta sección trata sobre cómo un driver de FreeBSD decide si el llamante tiene permiso para hacer lo que solicita, utilizando el mecanismo de credenciales y privilegios del kernel.

Las herramientas son `struct ucred`, `priv_check(9)` y `priv_check_cred(9)`, comprobaciones con conocimiento de jail, comprobaciones de securelevel y los marcos más amplios de MAC y Capsicum.

### La credencial del llamante: struct ucred

Cada thread que se ejecuta en el kernel de FreeBSD lleva una credencial, un puntero a una `struct ucred`. La credencial registra con qué identidad se ejecuta el thread, a qué jail está confinado, a qué grupos pertenece y otros atributos de seguridad. Desde dentro de un driver, se accede a la credencial casi siempre a través de `td->td_ucred`, donde `td` es el `struct thread *` que se pasa a tu punto de entrada.

La estructura está declarada en `/usr/src/sys/sys/ucred.h`. Los campos más relevantes para los drivers son:

`cr_uid`, el identificador de usuario efectivo. Normalmente es lo que se comprueba para responder a "¿es este root?".

`cr_ruid`, el identificador de usuario real.

`cr_gid`, el identificador de grupo efectivo.

`cr_prison`, un puntero al jail en el que está el proceso. Todos los procesos tienen uno. Los procesos que no están confinados en un jail pertenecen a `prison0`.

`cr_flags`, un pequeño conjunto de flags que incluye `CRED_FLAG_CAPMODE`, el cual indica el modo de capacidades (Capsicum).

No uses `cr_uid == 0` como puerta de privilegio. Es un error común y casi siempre incorrecto. La puerta correcta es `priv_check(9)`, que maneja los jails, el securelevel y las políticas MAC de forma adecuada. Comprobar `cr_uid` manualmente omite todo eso y otorga a root dentro de un jail el mismo poder que root en el sistema anfitrión, que no es la finalidad de los jails.

### priv_check y priv_check_cred

La primitiva canónica para la pregunta "¿puede el llamante realizar esta operación privilegiada?" es `priv_check(9)`. Su prototipo, tomado de `/usr/src/sys/sys/priv.h`:

```c
int priv_check(struct thread *td, int priv);
int priv_check_cred(struct ucred *cred, int priv);
```

`priv_check` opera sobre el thread actual. `priv_check_cred` opera sobre una credencial arbitraria; se usa cuando la credencial a comprobar no es la del thread en ejecución, por ejemplo al validar una operación en nombre de un archivo que se abrió anteriormente.

Ambas devuelven 0 si el privilegio se concede y un errno (típicamente `EPERM`) si no. El patrón en el driver es casi siempre:

```c
error = priv_check(td, PRIV_DRIVER);
if (error != 0)
    return (error);
```

El argumento `priv` selecciona uno de varias decenas de privilegios con nombre. La lista completa se encuentra en `/usr/src/sys/sys/priv.h` y abarca áreas como sistemas de archivos, redes, virtualización y drivers. Para la mayoría de los drivers de dispositivo, los nombres relevantes son:

`PRIV_DRIVER`, el privilegio genérico de driver. Concede acceso a operaciones restringidas a administradores.

`PRIV_IO`, I/O en bruto al hardware. Más restrictivo que `PRIV_DRIVER`, apropiado para operaciones que omiten las abstracciones habituales del driver y se comunican directamente con el hardware.

`PRIV_KLD_LOAD`, utilizado por el cargador de módulos. Normalmente no lo usarás desde un driver.

`PRIV_NET_*`, utilizado por operaciones relacionadas con la red.

Y muchos más. Lee la lista en `priv.h` y elige la coincidencia más específica para la operación que estás protegiendo. `PRIV_DRIVER` es un valor predeterminado razonable cuando ningún otro encaja mejor.

Un ejemplo real del kernel: en `/usr/src/sys/dev/mmc/mmcsd.c`, el driver comprueba `priv_check(td, PRIV_DRIVER)` antes de permitir ciertos ioctls que permitirían a un usuario reprogramar el controlador de almacenamiento. En `/usr/src/sys/dev/syscons/syscons.c`, el driver de consola comprueba `priv_check(td, PRIV_IO)` antes de permitir operaciones que manipulan el hardware directamente, ya que estas omiten la abstracción tty normal.

### Conocimiento de los jails

Los jails de FreeBSD (jail(8) y jail(9)) dividen el sistema en compartimentos. Los procesos dentro de un jail comparten el kernel del sistema anfitrión pero tienen una visión restringida del sistema: su propio nombre de host, su propia visibilidad de red, su propia raíz del sistema de archivos y privilegios reducidos. Dentro de un jail, `priv_check` rechaza muchos privilegios que de otro modo se concederían a root. Esta es una de las principales razones para usar `priv_check` en lugar de comprobar `cr_uid == 0`.

Algunas operaciones, sin embargo, no tienen ningún sentido dentro de un jail. Reprogramar el firmware de un dispositivo, por ejemplo, es una operación del sistema anfitrión. Un usuario root confinado en un jail nunca debería poder hacerlo. Para estos casos, añade una comprobación explícita de jail:

```c
if (jailed(td->td_ucred))
    return (EPERM);
error = priv_check(td, PRIV_DRIVER);
if (error != 0)
    return (error);
```

La macro `jailed()`, definida en `/usr/src/sys/sys/jail.h`, devuelve verdadero si la prisión de la credencial es cualquier cosa distinta de `prison0`. Para operaciones que nunca deberían realizarse desde dentro de un jail, comprueba esto primero.

Para operaciones que deberían estar permitidas dentro de un jail pero con restricciones, utiliza los propios campos del jail. `cred->cr_prison->pr_flags` contiene flags por jail; el marco de trabajo de jails también dispone de funciones auxiliares para comprobar si ciertas capacidades están permitidas en el jail. En la mayor parte del trabajo con drivers no irás más allá de la sencilla comprobación con `jailed()`.

### Securelevel

FreeBSD admite una configuración de ámbito global denominada securelevel. Con securelevel 0, el sistema se comporta con normalidad. Con securelevels más altos, ciertas operaciones quedan restringidas incluso para root: las escrituras en disco en bruto pueden desactivarse, el reloj del sistema no puede retrasarse, los módulos del kernel no pueden descargarse, y así sucesivamente. La razón de ser de esto es que en un servidor bien asegurado, elevar el securelevel durante el boot significa que un atacante que posteriormente obtenga acceso root no podrá deshabilitar el registro, instalar un módulo rootkit ni sobrescribir archivos fundamentales del sistema.

Para los drivers, los helpers relevantes están declarados en `/usr/src/sys/sys/priv.h`:

```c
int securelevel_gt(struct ucred *cr, int level);
int securelevel_ge(struct ucred *cr, int level);
```

Sus valores de retorno son contraintuitivos y merece la pena estudiarlos con detenimiento. Devuelven 0 cuando el securelevel **no** está por encima del umbral o en él (es decir, la operación está permitida), y `EPERM` cuando el securelevel **sí** está por encima del umbral o en él (la operación debe denegarse). En otras palabras, el valor de retorno está listo para usarse directamente como código de error.

El patrón de uso para un driver que debe rechazar modificaciones al hardware con securelevel 1 o superior es:

```c
error = securelevel_gt(td->td_ucred, 0);
if (error != 0)
    return (error);
```

Lee con atención: esto dice "devuelve un error si el securelevel es mayor que 0". Cuando el securelevel es 0 (normal), `securelevel_gt(cred, 0)` devuelve 0 y la comprobación pasa. Cuando el securelevel es 1 o superior, devuelve `EPERM` y la operación se rechaza.

La mayoría de los drivers no necesitan comprobaciones de securelevel. Tienen sentido para operaciones potencialmente desestabilizadoras del sistema: reprogramar firmware, escribir en sectores de disco en bruto, retrasar el reloj del sistema, etcétera.

### Comprobaciones en capas

Un driver que quiera aplicar defensa en profundidad puede combinar estas comprobaciones en capas:

```c
static int
secdev_reset_hardware(struct secdev_softc *sc, struct thread *td)
{
    int error;

    /* Not inside a jail. */
    if (jailed(td->td_ucred))
        return (EPERM);

    /* Not at elevated securelevel. */
    error = securelevel_gt(td->td_ucred, 0);
    if (error != 0)
        return (error);

    /* Must have driver privilege. */
    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* Okay, do the dangerous thing. */
    return (secdev_do_reset(sc));
}
```

Cada comprobación responde a una pregunta diferente. `jailed()` pregunta si estamos en el dominio de seguridad correcto. `securelevel_gt` pregunta si el administrador del sistema ha indicado al kernel que rechace este tipo de operación. `priv_check` pregunta si este thread concreto tiene el privilegio adecuado.

En muchos drivers, solo `priv_check` es estrictamente necesario, porque gestiona los jails y el securelevel a través del marco MAC y las propias definiciones de privilegio. Las llamadas explícitas a `jailed()` y `securelevel_gt` son adecuadas para operaciones con consecuencias conocidas en todo el sistema anfitrión. Ante la duda, empieza con `priv_check(td, PRIV_DRIVER)` y añade más capas solo cuando puedas explicar qué aporta cada comprobación adicional.

### La credencial en open, ioctl y otros caminos

Al diseñar las comprobaciones de privilegio, piensa en qué parte del ciclo de vida del driver residen. Hay dos lugares principales:

En el momento de apertura. Si solo los usuarios privilegiados deben poder abrir el dispositivo, comprueba los privilegios en `d_open`. Es la opción más sencilla y aplica la restricción en cada apertura: una vez que un usuario ha abierto el dispositivo, puede realizar lo que ese dispositivo permite. Este es el modelo que usa, por ejemplo, `/dev/mem`, que solo puede abrirse con el privilegio adecuado.

En el momento de la operación. Si el dispositivo admite múltiples operaciones con diferentes requisitos de privilegio, comprueba cada operación de forma independiente. Un controlador de almacenamiento podría permitir leer el estado del dispositivo a cualquier usuario, leer los datos SMART al propietario del archivo de dispositivo, y activar la actualización de firmware solo a usuarios con `PRIV_DRIVER`. Cada operación tiene su propia puerta.

Un driver puede combinar ambos enfoques: una comprobación de privilegio en la apertura para excluir completamente a los usuarios sin privilegios, y comprobaciones adicionales en ioctls específicos para operaciones que requieren más.

Una comprobación en el momento de apertura es fácil de implementar:

```c
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    int error;

    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* ... rest of open logic ... */
    return (0);
}
```

Una comprobación en el momento del ioctl sigue el mismo patrón; el argumento `struct thread *td` está disponible en todos los puntos de entrada.

### Permisos del archivo de dispositivo

Con independencia de las comprobaciones de privilegio dentro del driver, FreeBSD también aplica el modelo de permisos UNIX habitual a los propios archivos de dispositivo. Cuando tu driver llama a `make_dev_s` o `make_dev_credf` para crear un nodo de dispositivo, eliges propietario, grupo y modo. Estos se aplican a nivel del sistema de archivos: un usuario que no supere la comprobación de permisos sobre el nodo de dispositivo nunca llega a tu `d_open`.

La estructura `make_dev_args`, declarada en `/usr/src/sys/sys/conf.h`, incluye los campos `mda_uid`, `mda_gid` y `mda_mode`. El patrón es:

```c
struct make_dev_args args;

make_dev_args_init(&args);
args.mda_devsw = &secdev_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_OPERATOR;
args.mda_mode = 0640;
args.mda_si_drv1 = sc;
error = make_dev_s(&args, &sc->sc_cdev, "secdev");
```

`UID_ROOT` y `GID_OPERATOR` son nombres simbólicos convencionales. El modo `0640` significa que el propietario puede leer y escribir, el grupo puede leer, y los demás no tienen ningún acceso. Elige estos valores con cuidado. Un dispositivo que pudiera exponer datos sensibles o causar daños al hardware no debería ser legible ni escribible por cualquier usuario.

El patrón habitual para un dispositivo con privilegios es el modo `0600` (solo root) o `0660` (root y un grupo específico, habitualmente `operator` o `wheel`). El modo `0640` es común en dispositivos que un grupo de confianza puede leer con fines de monitorización. Los modos como `0666` (escritura universal) son casi nunca apropiados, ni siquiera para pseudodispositivos sencillos, a menos que el dispositivo realmente no haga nada que deba restringirse.

### Reglas de Devfs

Aunque tu driver cree el nodo de dispositivo con un modo restrictivo, el administrador del sistema puede modificarlo mediante reglas de devfs. Una regla de devfs puede relajar o restringir permisos en función del nombre del dispositivo, la jail y otros criterios. Tu driver no debe asumir que el modo establecido en la creación es el modo que tendrá el dispositivo en tiempo de ejecución; debe seguir aplicando sus comprobaciones en el kernel independientemente de ello. El modo del sistema de archivos y el `priv_check` en el kernel defienden frente a atacantes distintos; utiliza ambos.

### El MAC Framework

El framework de Control de Acceso Obligatorio (MAC) de FreeBSD, declarado en `/usr/src/sys/security/mac/`, permite que los módulos de política se conecten al kernel y tomen decisiones de acceso basadas en etiquetas más ricas que los permisos UNIX. Una política MAC puede, por ejemplo, restringir qué usuarios pueden acceder a qué dispositivos aunque los permisos UNIX lo permitan, o registrar cada uso de una operación sensible.

Para los autores de drivers, la cuestión es la siguiente: `priv_check` ya consulta el MAC framework. Cuando utilizas `priv_check`, te adhieres a cualquier política MAC que el administrador haya configurado. Si evitas `priv_check` y creas tu propia comprobación de privilegios usando `cr_uid`, también evitas MAC. Esa es una razón más para usar siempre `priv_check`.

Escribir tu propio módulo de política MAC queda fuera del alcance de este capítulo; el MAC framework es un tema extenso y tiene su propia documentación. La conclusión principal es simplemente que MAC existe, `priv_check` lo respeta, y no debes ignorarlo.

**Una breve nota sobre las políticas MAC incluidas en FreeBSD.** El sistema base incluye varias políticas MAC como módulos cargables: `mac_bsdextended(4)` para listas de reglas del sistema de archivos, `mac_portacl(4)` para el control de acceso a puertos de red, `mac_biba(4)` para la política de integridad Biba, `mac_mls(4)` para etiquetas de Seguridad Multinivel, y `mac_partition(4)` para particionar procesos en grupos aislados. Un autor de drivers no necesita entender ninguna de estas en detalle; el punto clave es que tu driver, al usar `priv_check`, obtiene sus decisiones de política de forma gratuita. Un administrador que habilite `mac_bsdextended` obtiene restricciones adicionales a nivel del sistema de archivos; tu driver no necesita saberlo.

**MAC y el nodo de dispositivo.** Cuando creas un dispositivo con `make_dev_s`, el MAC framework puede asignar una etiqueta al nodo de dispositivo. Las políticas consultan esa etiqueta cuando se intenta el acceso. Un driver no interactúa directamente con las etiquetas; el framework se encarga de ello. Pero saber que existe una etiqueta explica por qué, en un sistema con MAC habilitado, el acceso a tu dispositivo puede ser denegado aunque los permisos UNIX lo permitan. Eso no es un error; es MAC haciendo su trabajo.

### Capsicum y el modo de capacidad

Capsicum, declarado en `/usr/src/sys/sys/capsicum.h`, es un sistema de capacidades integrado en FreeBSD. Un proceso en modo de capacidad ha perdido el acceso a la mayoría de los espacios de nombres globales (no puede abrir nuevos archivos, no puede usar la red con efectos secundarios, no puede llamar a ioctl de forma arbitraria, etc.). Solo puede operar sobre los descriptores de archivo que ya posee, y esos descriptores de archivo pueden tener a su vez derechos limitados (solo lectura, solo escritura, ciertos ioctls únicamente, etc.).

Capsicum se introdujo en FreeBSD gracias al trabajo de Robert Watson y sus colaboradores. Se sitúa junto al modelo de permisos UNIX tradicional y añade una segunda capa más granular. Mientras que los permisos UNIX preguntan «¿puede este usuario acceder a este recurso por su nombre?», Capsicum pregunta «¿tiene este proceso una capacidad para este objeto concreto?». Las dos capas trabajan conjuntamente: el usuario debe tener permiso UNIX para abrir el archivo en primer lugar, pero una vez que el descriptor de archivo existe, Capsicum puede restringir aún más lo que el titular del descriptor puede hacer con él.

Para un driver, la principal preocupación respecto a Capsicum es que algunos de tus ioctls pueden ser inapropiados para un proceso en modo de capacidad. La función auxiliar `IN_CAPABILITY_MODE(td)`, definida en `capsicum.h`, te indica si el thread que realiza la llamada está en modo de capacidad. Un driver puede comprobarlo y rechazar las operaciones que no sean seguras:

```c
if (IN_CAPABILITY_MODE(td))
    return (ECAPMODE);
```

Esto es adecuado para operaciones con efectos secundarios globales a los que un proceso en modo de capacidad no debería tener acceso. Entre los ejemplos se podría incluir un ioctl que reconfigura el estado global del driver, un ioctl que afecta a otros procesos o a otros descriptores de archivo, o un ioctl que realiza una operación que requiere consultar el espacio de nombres del sistema de archivos global. Si el ioctl de tu driver necesita acceder a algo que no está ya nombrado por el descriptor de archivo sobre el que fue llamado, una comprobación del modo de capacidad es adecuada.

Sin embargo, para la mayoría de las operaciones de un driver, la situación con Capsicum es más sencilla: el proceso que posee el descriptor de archivo recibió los derechos que necesitaba cuando se le entregó el descriptor. El driver no necesita volver a comprobar esos derechos; la capa de descriptores de archivo ya lo hizo. Asegúrate de que tu driver admite el flujo normal de cap-rights (casi con toda seguridad lo hace por defecto) y considera qué ioctls individuales deben marcarse con derechos `CAP_IOCTL_*` en la capa VFS.

**Derechos de capacidad con granularidad de ioctl.** FreeBSD permite restringir un descriptor de archivo a un subconjunto específico de ioctls mediante `cap_ioctls_limit(2)`. Por ejemplo, un proceso puede poseer un descriptor de archivo que permita `FIOASYNC` y `FIONBIO` pero ningún otro ioctl. La restricción la aplica la capa VFS, no tu driver, pero el conjunto de ioctls que expones es el que define qué puede seleccionarse para ser restringido. Un driver que implementa únicamente ioctls con sentido y bien documentados facilita a sus usuarios la aplicación de restricciones cap-ioctl razonables.

**Examinando el uso de Capsicum en el árbol de código fuente.** Para ver ejemplos reales de código compatible con Capsicum, consulta `/usr/src/sys/net/if_tuntap.c` junto con los archivos centrales de capacidades en `/usr/src/sys/kern/sys_capability.c`. La mayoría de los drivers individuales se apoyan en la capa VFS para hacer cumplir `caprights`, y solo añaden una comprobación explícita de `IN_CAPABILITY_MODE(td)` en las pocas operaciones con efectos secundarios globales. El patrón es coherente: preserva el comportamiento normal, añade una comprobación `IN_CAPABILITY_MODE` donde las operaciones no serían seguras, y documenta qué ioctls son seguros para sandbox.

### Sysctls con indicadores de seguridad

Muchos drivers exponen ajustes configurables y estadísticas a través de sysctls. Un sysctl que expone información sensible, o que puede configurarse para cambiar el comportamiento del driver, debe usar los indicadores adecuados. De `/usr/src/sys/sys/sysctl.h`:

`CTLFLAG_SECURE` (valor `0x08000000`) solicita al framework de sysctl que consulte `priv_check(PRIV_SYSCTL_SECURE)` antes de permitir la operación. Es útil para sysctls que no deben modificarse con un securelevel elevado.

`CTLFLAG_PRISON` permite que el sysctl sea visible y modificable desde dentro de una jail (raramente deseado para drivers).

`CTLFLAG_CAPRD` y `CTLFLAG_CAPWR` permiten que el sysctl se lea o escriba desde el modo de capacidad. Por defecto, los sysctls son inaccesibles en modo de capacidad.

`CTLFLAG_TUN` permite que el sysctl se configure como un parámetro del loader (desde `/boot/loader.conf`).

`CTLFLAG_RD` frente a `CTLFLAG_RW` determina el acceso de solo lectura frente al de lectura y escritura; prefiere `CTLFLAG_RD` para todo lo que expone estado, y sé deliberado con lo que haces modificable.

Un sysctl que expone un buffer interno del driver con fines de depuración debería ser como mínimo `CTLFLAG_RD | CTLFLAG_SECURE`, y posiblemente no existir en absoluto en builds de producción.

### Un ioctl completo con control de privilegios

Juntando todas las piezas, así es como se ve un ioctl con control de privilegios, de principio a fin:

```c
static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    int error;

    switch (cmd) {
    case SECDEV_GET_STATUS:
        /* Anyone with the device open can do this. */
        error = secdev_get_status(sc, (struct secdev_status *)data);
        break;

    case SECDEV_RESET:
        /* Resetting is privileged, jail-restricted, and securelevel-sensitive. */
        if (jailed(td->td_ucred)) {
            error = EPERM;
            break;
        }
        error = securelevel_gt(td->td_ucred, 0);
        if (error != 0)
            break;
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
            break;
        error = secdev_do_reset(sc);
        break;

    default:
        error = ENOTTY;
        break;
    }

    return (error);
}
```

Diferentes comandos tienen diferentes comprobaciones. El comando de estado no requiere privilegios, ya que simplemente lee el estado. El comando de reinicio es el caso peligroso, y pasa por la comprobación completa en capas.

### Cerrando la sección 6

El control de acceso en un driver de FreeBSD es una colaboración entre varias capas. Los permisos del sistema de archivos en el nodo de dispositivo deciden quién puede abrirlo. La familia de funciones `priv_check(9)` decide si un thread puede realizar una operación privilegiada concreta. Las comprobaciones de jail deciden si la operación tiene sentido en el dominio de seguridad del invocador. Las comprobaciones de securelevel deciden si el administrador del sistema ha permitido esta clase de operación. El MAC framework permite que los módulos de política añadan sus propias decisiones por encima. Los derechos de Capsicum limitan lo que puede hacer un proceso confinado en modo de capacidad.

El uso correcto de estas herramientas se reduce a una breve lista de reglas: comprueba las credenciales del invocador en los puntos adecuados, prefiere `priv_check` sobre comprobaciones de UID ad hoc, añade `jailed()` y `securelevel_gt` cuando la operación tenga consecuencias que afecten a todo el sistema anfitrión, elige la constante `PRIV_*` más específica que se ajuste a la operación, y establece modos conservadores para el archivo de dispositivo en `make_dev_s`.

La siguiente sección examina un tipo diferente de fuga: no una escalada de privilegios, sino una fuga de información. Incluso las operaciones correctamente protegidas pueden revelar inadvertidamente el contenido de la memoria del kernel si no se escriben con cuidado.

## Sección 7: Protección contra fugas de información

Una fuga de información ocurre cuando memoria del kernel que no debería ser visible para el espacio de usuario se copia igualmente al espacio de usuario. La forma clásica consiste en devolver al usuario el contenido de una estructura sin haberla inicializado antes. Los bytes de relleno entre campos, o los bytes finales tras el último campo, contienen lo que hubiera en la pila del kernel o en una página recién asignada la última vez que se usó esa memoria. Podría ser una contraseña, un puntero que ayudaría a eludir ASLR, una clave de cifrado, o cualquier otra cosa.

Las fugas de información a veces se minimizan como «no tan graves». Lo son. En las cadenas de ataque modernas, una fuga de información suele ser el primer paso: elimina la aleatorización del espacio de direcciones del kernel (kASLR) y hace que otros exploits sean fiables. Una clase de error que comienza con «solo filtra unos pocos bytes» a menudo termina con «el atacante obtiene ejecución de código en el kernel».

### Cómo se producen las fugas de información

Hay tres formas principales en las que un driver filtra información:

**Campos de estructura no inicializados copiados al espacio de usuario.** Una estructura tiene N campos definidos más ranuras de relleno y alineación. El código rellena los N campos y llama a `copyout`. El relleno va incluido, llevando consigo cualquier memoria de pila no inicializada que hubiera allí.

**Buffers parcialmente inicializados.** El driver asigna un buffer, rellena una parte de él y copia todo el buffer al espacio de usuario. La parte no inicializada del final lleva contenido del heap.

**Respuestas sobredimensionadas.** Se piden al driver `N` bytes, pero devuelve un buffer de tamaño `M > N`. Los `M - N` bytes adicionales contienen lo que hubiera en la cola del buffer de origen.

**Lectura más allá de un NUL.** Para datos de cadena, el driver copia un buffer hasta su tamaño asignado en lugar de hasta el terminador NUL. Los bytes tras el NUL pueden contener cualquier dato que hubiera en ese buffer anteriormente.

Cada uno de estos errores es fácil de cometer por descuido y fácil de prevenir una vez que conoces el patrón.

### El problema del relleno

Considera esta estructura:

```c
struct secdev_info {
    uint32_t version;
    uint64_t flags;
    uint16_t id;
    char name[32];
};
```

En un sistema de 64 bits, el compilador inserta relleno para alinear `flags` a 8 bytes. Entre `version` (4 bytes) y `flags` (8 bytes), hay 4 bytes de relleno. Después de `id` (2 bytes) y antes de `name` (alineación de 1 byte), hay 6 bytes más de relleno al final si el tamaño de la estructura se redondea al múltiplo de 8 más cercano.

Si tu código hace:

```c
struct secdev_info info;

info.version = 1;
info.flags = 0x12345678;
info.id = 42;
strncpy(info.name, "secdev0", sizeof(info.name));

error = copyout(&info, args->buf, sizeof(info));
```

entonces los bytes de relleno, que nunca estableciste, salen al espacio de usuario. Contienen la memoria de pila que hubiera en esas posiciones cuando se entró en la función. Eso es una fuga de información.

La solución es universal y económica: pon la estructura a cero primero.

```c
struct secdev_info info;

bzero(&info, sizeof(info));      /* or memset(&info, 0, sizeof(info)) */
info.version = 1;
info.flags = 0x12345678;
info.id = 42;
strncpy(info.name, "secdev0", sizeof(info.name));

error = copyout(&info, args->buf, sizeof(info));
```

Ahora el relleno está a cero, igual que cualquier campo que hayas olvidado establecer. El coste es una llamada a `bzero`; el beneficio es que tu driver no puede filtrar memoria del kernel a través de esta estructura, sin importar qué campos se añadan más adelante. Siempre pon las estructuras a cero antes de hacer copyout.

Un patrón equivalente usando inicializadores designados funciona cuando declaras e inicializas en un solo paso:

```c
struct secdev_info info = { 0 };  /* or { } in some standards */
info.version = 1;
/* ... */
```

El `= { 0 }` pone a cero todos los bytes, incluido el relleno. Combina esto con la asignación de los campos específicos a continuación, y tendrás un patrón limpio.

### El caso de la asignación en el heap

Cuando asignas un buffer con `malloc(9)` y lo rellenas antes de devolver el control al espacio de usuario, el problema es el mismo. Usa siempre `M_ZERO` para inicializarlo a cero, o pon explícitamente el buffer a cero antes de escribir en él:

```c
buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
```

Aunque tengas intención de rellenar cada byte, usar `M_ZERO` es un seguro barato: si un bug provoca un relleno parcial, los bytes no rellenados serán cero en lugar de contenido obsoleto del heap.

### Respuestas de tamaño excesivo

Una forma sutil de fuga se produce cuando el driver devuelve más datos de los que el usuario solicitó. Imagina un ioctl que devuelve una lista de elementos:

```c
/* User asks for up to user_len bytes of list data. */
if (user_len > sc->sc_list_bytes)
    user_len = sc->sc_list_bytes;

error = copyout(sc->sc_list, args->buf, sc->sc_list_bytes);  /* BUG: wrong length */
```

El driver copia `sc_list_bytes` bytes independientemente de lo que el usuario haya solicitado. Si `sc_list_bytes > user_len`, el driver escribe más allá de `args->buf`, lo cual es un bug distinto (desbordamiento de buffer en espacio de usuario). Si el driver escribe primero en un buffer local y luego realiza la copia, un error similar escribiría más allá del buffer local.

El patrón correcto consiste en limitar la longitud y usar esa longitud limitada para la copia:

```c
size_t to_copy = MIN(user_len, sc->sc_list_bytes);
error = copyout(sc->sc_list, args->buf, to_copy);
```

Las fugas de información a través de respuestas de tamaño excesivo son frecuentes cuando el código del driver evoluciona: el autor original escribió una verificación y copia emparejadas; un cambio posterior modificó un lado pero no el otro. Cada copyout debe usar la longitud ya validada en el lado del kernel, y esa longitud debe estar acotada por el tamaño del buffer del usuario.

### Cadenas y el terminador NUL

Las cadenas son una fuente especialmente prolífica de fugas de información porque tienen dos longitudes naturales distintas: la longitud de la cadena (hasta el NUL) y el tamaño del buffer que la contiene. Supón lo siguiente:

```c
char name[32];
strncpy(name, "secdev0", sizeof(name));  /* copies 8 bytes, NUL-padded */

/* ... later, maybe in a different function ... */
strncpy(name, "xdev", sizeof(name));     /* copies 5 bytes, NUL-padded */

copyout(name, args->buf, sizeof(name));  /* copies all 32 bytes */
```

El segundo `strncpy` sobrescribe los primeros cinco bytes con "xdev\0" y luego rellena el resto del buffer con NULs. Esto resulta seguro porque `strncpy` rellena con NULs cuando la fuente es más corta que el destino. Pero si el buffer proviene de `malloc(9)` sin `M_ZERO`, o de un buffer en la pila que fue escrito por código anterior, los bytes después del NUL pueden contener datos residuales. Copiar el buffer completo los filtra.

El patrón seguro consiste en copiar solo hasta el NUL, o bien en poner el buffer a cero antes de escribir:

```c
bzero(name, sizeof(name));
snprintf(name, sizeof(name), "%s", "secdev0");
copyout(name, args->buf, strlen(name) + 1);
```

`snprintf` garantiza la terminación con NUL. Poner el buffer a cero primero garantiza que los bytes después del NUL sean cero. El `+ 1` en la longitud de copia incluye el propio NUL.

Alternativamente, copia solo la cadena y deja que el espacio de usuario gestione su propio relleno:

```c
copyout(name, args->buf, strlen(name) + 1);
```

El patrón más limpio es poner el buffer a cero primero y copiar exactamente la longitud válida.

### Datos sensibles: puesta a cero explícita antes de liberar

Cuando un driver asigna memoria para almacenar datos sensibles (claves criptográficas, credenciales de usuario, secretos propietarios), la memoria debe ponerse a cero de forma explícita antes de liberarse. De lo contrario, la memoria liberada vuelve al pool libre del asignador del kernel con los datos todavía visibles, y las asignaciones posteriores de ese pool pueden exponerlos.

FreeBSD proporciona `explicit_bzero(9)`, declarado en `/usr/src/sys/sys/systm.h`, que pone a cero la memoria de un modo que el compilador no puede optimizar y eliminar:

```c
explicit_bzero(sc->sc_secret, sc->sc_secret_len);
free(sc->sc_secret, M_SECDEV);
sc->sc_secret = NULL;
```

El `bzero` ordinario puede ser eliminado por el compilador si los datos no se leen tras la puesta a cero, que es exactamente la situación antes de un free. `explicit_bzero` garantiza que la puesta a cero se realiza. Úsalo siempre que los datos sensibles estén a punto de liberarse o salir del ámbito.

También existe `zfree(9)`, declarado en `/usr/src/sys/sys/malloc.h`, que pone a cero y libera en una sola llamada:

```c
zfree(sc->sc_secret, M_SECDEV);
sc->sc_secret = NULL;
```

`zfree` conoce el tamaño de la asignación a partir de los metadatos del asignador y pone a cero ese número de bytes antes de liberar. Este suele ser el patrón más limpio para material criptográfico.

Para las zonas UMA, el equivalente es que la propia zona puede configurarse para poner a cero al liberar, o bien puedes aplicar `explicit_bzero` al objeto antes de llamar a `uma_zfree`. Para los buffers en la pila con contenido sensible, `explicit_bzero` al final de la función es la herramienta adecuada.

### Nunca filtres punteros del kernel

Una forma específica de fuga de información es devolver un puntero del kernel al espacio de usuario. La dirección en el kernel de un softc, o de un buffer interno, es información útil para un atacante que intenta explotar otro bug. `printf("%p")` en los mensajes de log también puede filtrar direcciones. La regla general es no incluir direcciones del kernel en la salida visible por el usuario.

Para los sysctls y los ioctls, la regla más sencilla es que ningún campo de una estructura orientada al usuario debe ser un puntero del kernel en bruto. Si el driver quiere exponer un identificador para un objeto del kernel, usa un ID entero pequeño (un índice en una tabla, por ejemplo), no la dirección del objeto. Convierte de uno a otro dentro del driver; nunca expongas el puntero en bruto.

El `printf(9)` de FreeBSD admite el formato `%p`, que efectivamente imprime un puntero, pero los mensajes de log en drivers de producción deben evitar `%p` para cualquier situación en la que el puntero pueda facilitar la explotación. Para depuración, `%p` es adecuado durante el desarrollo; antes de distribuir el driver, audita las llamadas a `printf` y `log` para asegurarte de que no quede ningún `%p` en rutas accesibles desde el espacio de usuario.

### Salida de sysctl

Los sysctls que exponen estructuras tienen las mismas reglas que los ioctls. Pon la estructura a cero antes de rellenarla, limita la longitud de salida al buffer del llamante y evita las fugas de punteros. El helper `sysctl_handle_opaque` se usa con frecuencia para estructuras en bruto; asegúrate de que la estructura está completamente inicializada antes de que el handle retorne.

Un patrón más seguro consiste en exponer cada campo como su propio sysctl, usando `sysctl_handle_int`, `sysctl_handle_string` y similares. Esto evita por completo el problema del relleno porque cada valor se copia como un tipo primitivo. También resulta más ergonómico para los usuarios: `sysctl secdev.stats.packets` es más útil que un blob opaco que tendrían que decodificar.

### Errores de copyout

`copyout` puede fallar. Si el buffer del usuario queda sin mapear entre la validación y la copia, `copyout` devuelve `EFAULT`. Tu driver debe gestionar esto de forma limpia: por lo general, devuelve el error al usuario y asegúrate de que cualquier éxito parcial se deshaga.

Una secuencia del tipo «asignar estado, rellenar el buffer de salida, copyout, confirmar estado» es más segura que «confirmar estado, copyout». Si el copyout falla en el segundo patrón, el estado queda confirmado pero el usuario nunca supo qué ocurrió. Si falla en el primer patrón, nada queda confirmado y el usuario recibe un error limpio.

### Divulgación deliberada

Algunos sysctls e ioctls están diseñados explícitamente para revelar información que de otro modo sería privada. Estos requieren un modelo de amenazas especialmente cuidadoso. Pregúntate: ¿quién tiene permitido hacer esta llamada? ¿Qué aprenden? ¿Podría un atacante con menos privilegios que obtenga esa información usarla para algo peor?

Un sysctl al estilo de dmesg que expone mensajes recientes del kernel es perfectamente válido, pero solo porque su alcance ha sido delimitado y filtrado; exponer buffers de log del kernel en bruto sin delimitar su alcance es algo muy diferente.

Ante la duda, un sysctl que revela datos sensibles debe estar protegido con `CTLFLAG_SECURE`, restringido a usuarios con privilegios y expuesto solo a través de rutas a las que los usuarios deben optar explícitamente. Opta por revelar menos de forma predeterminada, no más.

### Hash de punteros del kernel

A veces un driver necesita legítimamente exponer algo que identifique un objeto del kernel, ya sea para depuración o para correlacionar eventos. La dirección del puntero en bruto es la respuesta equivocada por las razones ya expuestas. Una respuesta mejor es una representación hasheada o enmascarada que identifique el objeto sin revelar su dirección.

FreeBSD proporciona `%p` en `printf(9)`, que imprime un puntero. También proporciona un mecanismo relacionado mediante el cual los punteros pueden «ofuscarse» en la salida visible por el usuario usando un secreto específico de cada arranque, de modo que dos punteros en la misma salida sean consistentemente distinguibles pero sus valores absolutos no se filtren. El soporte para esto varía según el subsistema; al diseñar tu propia salida, considera si un ID entero compacto (un índice en una tabla) es suficiente. Con frecuencia lo es.

Para los logs, `%p` es adecuado durante el desarrollo cuando los logs son privados. Antes de distribuir, reemplaza cualquier `%p` en rutas alcanzables desde el espacio de usuario con una guardia exclusiva de depuración (para que el formato esté presente únicamente en compilaciones de depuración) o con un identificador que no sea un puntero.

### Cerrando la sección 7

Las fugas de información son la versión más silenciosa de los desbordamientos de buffer: no provocan caídas, no corrompen datos, simplemente envían al espacio de usuario datos que deberían haber permanecido en el kernel. Las herramientas para prevenirlas son sencillas y económicas. Pon las estructuras a cero antes de rellenarlas. Usa `M_ZERO` en las asignaciones del heap que se copiarán al espacio de usuario. Limita las longitudes de copia al menor entre el buffer del llamante y el buffer fuente del kernel. Usa `explicit_bzero` o `zfree` para los datos sensibles antes de liberarlos. Mantén los punteros del kernel fuera de la salida visible por el usuario. Acota las cadenas a su longitud real, no al tamaño de su buffer.

Un driver que aplica estos hábitos de forma consistente no filtrará información a través de sus interfaces. La siguiente sección aborda el lado de la depuración y los diagnósticos: cómo registrar eventos sin filtrar información, cómo depurar sin dejar código hostil para producción y cómo mantener informado al operador sin poner un mapa en manos del atacante.

## Sección 8: registro seguro y depuración

Todo driver genera logs. `printf(9)` y `log(9)` se encuentran entre las primeras herramientas a las que recurre el autor de un driver, y con buena razón: un mensaje de log bien ubicado convierte un fallo misterioso en una narrativa legible. Pero los logs no son gratuitos. Consumen espacio en disco, pueden verse inundados y pueden filtrar datos sensibles. Un driver consciente de la seguridad trata el registro de mensajes como una preocupación de diseño de primer orden, no como un mero añadido de última hora al depurar.

Esta sección trata sobre cómo escribir mensajes de log que ayuden a los operadores sin comprometer la seguridad.

### Las primitivas de registro

Los drivers de FreeBSD tienen dos formas principales de emitir mensajes.

`printf(9)`, con el mismo nombre que la función de la biblioteca de C pero con semántica específica del kernel, escribe en el buffer de mensajes del kernel y, si la consola está activa, también en la consola. Es incondicional: cada llamada a `printf` produce un mensaje.

`log(9)`, declarado en `/usr/src/sys/sys/syslog.h`, escribe en el anillo de log del kernel con una prioridad compatible con syslog. Los mensajes van al buffer de log del kernel (legible mediante `dmesg(8)`) y, a través de `syslogd(8)`, a los destinos de log configurados. La prioridad sigue la escala syslog habitual: `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`.

Usa `log(9)` cuando quieras que el mensaje sea filtrado o enrutado por syslog. Usa `printf(9)` cuando quieras una emisión incondicional, generalmente para eventos muy importantes o para salida que siempre debe aparecer en la consola.

`device_printf(9)` es un pequeño wrapper sobre `printf` que prefija el mensaje con el nombre del dispositivo (`secdev0: ...`). Prefiérelo dentro del código del driver para que los mensajes sean fácilmente atribuibles.

### Qué registrar y qué no registrar

Un driver consciente de la seguridad registra:

**Transiciones de estado relevantes.** Attach, detach, reset, actualización de firmware, link up, link down. Estas permiten a un operador correlacionar el comportamiento del driver con los eventos del sistema.

**Errores del hardware o de las solicitudes de usuario.** Un argumento de ioctl incorrecto, un error de DMA, un timeout, un error de CRC. Estos permiten al operador diagnosticar problemas.

**Resúmenes de eventos anómalos con limitación de frecuencia.** Si se recibe un ioctl malformado un millón de veces por segundo, registra el primero y resume el resto.

Un driver consciente de la seguridad no registra:

**Datos del usuario.** El contenido de los buffers que el usuario proporcionó. Nunca sabes qué contienen.

**Material criptográfico.** Claves, IVs, texto en claro, texto cifrado. Nunca.

**Estado sensible del hardware.** En un dispositivo de seguridad, el contenido de algunos registros es en sí mismo un secreto.

**Direcciones del kernel.** `%p` es adecuado en las primeras fases del desarrollo; no tiene cabida en los logs de producción.

**Detalles de fallos de autenticación.** Un mensaje de log que dijera «el usuario jane falló la comprobación X porque el registro valía 0x5d» le indica a un atacante qué comprobación debe superar. Un log que dijera «autenticación fallida» informa al operador de que hubo un fallo sin instruir al atacante.

Piensa en quién lee los logs. En un servidor multiinquilino, otros usuarios pueden tener privilegios de lectura de logs. En un appliance distribuido, el log puede exportarse para soporte remoto. Trata los mensajes de log como información que podría terminar en cualquier superficie que el sistema toque.

### Limitación de frecuencia

Un driver ruidoso es un problema de seguridad. Si un atacante puede provocar un mensaje de log, puede provocar un millón de ellos. La inundación de logs consume espacio en disco, ralentiza el sistema y entierra los mensajes legítimos. FreeBSD proporciona `eventratecheck(9)` y `ppsratecheck(9)` en `/usr/src/sys/sys/time.h`:

```c
int eventratecheck(struct timeval *lasttime, int *cur_pps, int max_pps);
int ppsratecheck(struct timeval *lasttime, int *cur_pps, int max_pps);
```

Ambas devuelven 1 si el evento tiene paso libre y 0 si ha sido limitado en tasa. `lasttime` y `cur_pps` son estado por llamada que conservas en tu softc. `max_pps` es el límite en eventos por segundo.

Patrón:

```c
static struct timeval secdev_last_log;
static int secdev_cur_pps;

if (ppsratecheck(&secdev_last_log, &secdev_cur_pps, 5)) {
    device_printf(dev, "malformed ioctl from uid %u\n",
        td->td_ucred->cr_uid);
}
```

Ahora, independientemente de cuántos ioctls malformados envíe el atacante, el driver emite como máximo 5 mensajes de log por segundo. Eso es suficiente para que el operador note que algo está ocurriendo sin inundar el sistema.

La limitación de tasa por evento (un par `lasttime`/`cur_pps` por tipo de evento) es preferible a un límite global único, porque impide que una avalancha de un tipo de evento enmascare otros eventos.

### Niveles de log en la práctica

Una buena regla general es la siguiente:

`LOG_ERR` para fallos inesperados del driver que requieren atención del operador. "DMA mapping failed", "device returned CRC error", "firmware update aborted".

`LOG_WARNING` para situaciones inusuales pero no necesariamente críticas. "Received oversized buffer, truncating", "falling back to polled mode".

`LOG_NOTICE` para eventos normales pero que vale la pena registrar. "Firmware version 2.1 loaded", "device attached".

`LOG_INFO` para información de estado de alto volumen que los operadores pueden filtrar.

`LOG_DEBUG` para salida de depuración. Un driver de producción normalmente no emite `LOG_DEBUG` a menos que el operador haya habilitado el log de depuración mediante un sysctl.

`LOG_EMERG` y `LOG_ALERT` están reservados para condiciones que amenazan el sistema y, por lo general, los drivers de dispositivo no los emiten.

Elegir el nivel adecuado importa porque los operadores configuran syslog para filtrar por nivel. Un driver que registra cada paquete recibido con `LOG_ERR` hace que los logs sean inútiles.

### Log de depuración y producción

Durante el desarrollo, querrás un log detallado: cada transición de estado, cada entrada y salida, cada asignación de buffer. Está bien. La cuestión es cómo desactivarlo en producción sin perder la capacidad de volver a habilitarlo cuando haya un bug que diagnosticar.

Dos patrones son habituales:

**Un nivel de depuración controlado por sysctl.** El driver lee un sysctl al inicio de cada evento que merece registrarse y emite o suprime el mensaje según el nivel. Esto permite el control en tiempo de ejecución sin necesidad de recompilar.

```c
static int secdev_debug = 0;
SYSCTL_INT(_hw_secdev, OID_AUTO, debug, CTLFLAG_RW,
    &secdev_debug, 0, "debug level");

#define SECDEV_DBG(level, fmt, ...) do {                    \
    if (secdev_debug >= (level))                            \
        device_printf(sc->sc_dev, fmt, ##__VA_ARGS__);      \
} while (0)
```

**Control en tiempo de compilación.** Un driver puede usar `#ifdef SECDEV_DEBUG` para incluir o excluir bloques de depuración. Esto es más rápido (sin comprobación en tiempo de ejecución) pero requiere una recompilación para cambiar. Con frecuencia se combinan los dos enfoques: `#ifdef SECDEV_DEBUG` envuelve la infraestructura, y el sysctl controla la verbosidad dentro de ella.

En cualquier caso, evita las llamadas a `printf` en rutas de ejecución crítica que no estén protegidas por algún tipo de condicional. Un `printf` sin comentar en un manejador de interrupciones o en una ruta por paquete es un desastre de rendimiento esperando activarse.

### Sin dejar nada atrás

Antes de confirmar los cambios en el driver, busca con grep:

Llamadas a `printf` sin el prefijo `device_printf`. Estas dificultan la atribución de los mensajes de log.

Especificadores de formato `%p`. Si aparecen en rutas accesibles desde el espacio de usuario, sustitúyelos por formatos menos sensibles (un número de secuencia, un hash, nada).

`LOG_ERR` en eventos que pueden desencadenarse desde el espacio de usuario sin limitación de tasa. Los atacantes pueden aprovecharlos como arma.

`TODO`, `XXX`, `FIXME`, `HACK` cerca de código relacionado con la seguridad. Dejarlos para los revisores está bien; publicarlos en producción, no.

Equivalentes a fprintf usados solo en pruebas que debían eliminarse.

### dmesg y el buffer de mensajes del kernel

El buffer de mensajes del kernel es un buffer circular de tamaño fijo compartido por todos los drivers y por el propio kernel. En un sistema muy activo, los mensajes antiguos van desapareciendo a medida que llegan los nuevos. Un driver que inunda el buffer desplaza los mensajes útiles de otros drivers.

`dmesg(8)` muestra el contenido actual del buffer. Los operadores dependen de él. Ser un buen ciudadano en el buffer significa: registrar lo importante, no registrar en rutas de ejecución crítica, limitar la tasa de todo lo que los usuarios puedan desencadenar y no inundar el buffer.

El tamaño del buffer es configurable (sysctl `kern.msgbufsize`), pero no puedes contar con un tamaño concreto. Escribe como si cada mensaje fuera valioso y tuviera que competir con los demás por el espacio disponible.

### KTR y tracing

Para un tracing detallado sin el coste de `printf`, FreeBSD proporciona KTR (Kernel Tracing), declarado en `/usr/src/sys/sys/ktr.h`. Los macros de KTR, cuando están habilitados, registran eventos en un anillo compacto dentro del kernel que es independiente del buffer de mensajes. Un kernel compilado con `options KTR` puede consultarse con `sysctl debug.ktr.buf` y con `ktrdump(8)`.

Los eventos de KTR son ideales para el tracing por operación donde un `printf` sería demasiado costoso. Tienen un coste casi nulo en tiempo de ejecución cuando están deshabilitados. Para un driver con requisitos de seguridad estrictos, KTR ofrece una forma de dejar la infraestructura de tracing en el código sin pagar por ella en producción.

Otros frameworks de tracing (dtrace(1) mediante sondas SDT) merecen aprenderse para una inspección profunda. Están fuera del alcance de este capítulo, pero conviene saber que existen.

### Registro de operaciones privilegiadas

Hay un caso concreto que merece atención: cuando tu driver realiza con éxito una operación privilegiada, regístrala. Esto crea una pista de auditoría. Si se produce una actualización de firmware, registra quién la desencadenó. Si se emite un reset de hardware, regístralo. Si se reconfigura un dispositivo, registra el cambio.

```c
log(LOG_NOTICE, "secdev: firmware update initiated by uid %u (euid %u)\n",
    td->td_ucred->cr_ruid, td->td_ucred->cr_uid);
```

El operador podrá ver más tarde quién hizo qué. Si alguna vez se produce un incidente de seguridad, este log es la primera prueba. Hazlo preciso y difícil de falsificar.

No registres en exceso el uso legítimo de privilegios; una actualización de firmware desencadenada por `freebsd-update` una vez al mes es un mensaje, no mil. Pero ese único mensaje debe contener suficiente detalle para reconstruir lo que ocurrió: quién, cuándo, qué, con qué argumentos.

### El framework audit(4)

Para pistas de auditoría más detalladas de las que ofrece `log(9)`, FreeBSD incluye un subsistema de auditoría (`audit(4)`) basado en el formato de auditoría BSM (Basic Security Module), originario de Solaris. Cuando se habilita mediante `auditd(8)`, el kernel emite registros de auditoría estructurados para muchos eventos relevantes para la seguridad: inicios de sesión, cambios de privilegio, acceso a archivos y, cada vez más, eventos específicos del driver cuando los drivers se instrumentan a sí mismos.

Un driver que gestiona operaciones altamente sensibles puede emitir registros de auditoría personalizados utilizando los macros `AUDIT_KERNEL_*` declarados en `/usr/src/sys/security/audit/audit.h`. Esto es más complejo que una llamada a `log(9)`, pero produce registros que encajan en el flujo de trabajo de auditoría existente del operador, son estructurados (legibles por máquina) y pueden enviarse a recopiladores de auditoría remotos para cumplimiento normativo.

Para la mayoría de los drivers, `log(9)` con `LOG_NOTICE` y un mensaje claro es suficiente. Para drivers que deben cumplir requisitos normativos específicos (gubernamentales, financieros, médicos), considera invertir en la integración con el sistema de auditoría. La infraestructura ya está en el kernel; solo tienes que llamarla.

### Uso de dtrace con tu driver

Junto al logging, `dtrace(1)` permite a un operador observar el comportamiento del driver sin recompilar. Un driver que declara sondas de tracing estático (SDT) mediante `sys/sdt.h` expone puntos de enganche bien definidos a los que los scripts de dtrace pueden conectarse.

```c
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(secdev);
SDT_PROBE_DEFINE2(secdev, , , ioctl_called, "u_long", "int");

static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    SDT_PROBE2(secdev, , , ioctl_called, cmd, td->td_ucred->cr_uid);
    /* ... */
}
```

Un operador puede entonces escribir un script de dtrace que se active con `secdev:::ioctl_called` y cuente o registre cada evento. La ventaja frente a `log(9)` es que las sondas de dtrace tienen un coste prácticamente nulo cuando están deshabilitadas, y permiten que sea el operador quien decida qué observar, en lugar de obligar al autor del driver a anticipar cada pregunta útil.

Para un driver orientado a la seguridad, las sondas SDT en la entrada y salida de operaciones privilegiadas permiten a las herramientas de monitorización de seguridad observar patrones de uso sin que el driver tenga que registrar cada llamada. Esto resulta útil para la detección de anomalías: un pico repentino de llamadas ioctl desde un UID inesperado, por ejemplo, puede ser marcado por un monitor basado en dtrace.

### Cerrando la sección 8

El logging es la forma en que un driver se comunica con su operador. Como cualquier comunicación, puede ser clara o confusa, honesta o engañosa, útil o perjudicial. Un driver consciente de la seguridad registra los eventos importantes con los niveles adecuados, evita registrar datos sensibles, limita la tasa de todo lo que un atacante pueda desencadenar y utiliza una infraestructura de depuración que puede activarse y desactivarse sin recompilar. Prefiere `device_printf(9)` para la atribución, usa `log(9)` con prioridades bien pensadas y nunca deja `%p` ni sentencias `printf` sin protección en rutas de producción.

La siguiente sección adopta una perspectiva más amplia. Más allá de las técnicas concretas (comprobación de límites, verificación de privilegios, logging seguro), existe una pregunta a nivel de diseño: ¿qué debe hacer un driver por defecto cuando algo sale mal? ¿Qué comportamiento a prueba de fallos debe exhibir? Ese es el tema de los valores por defecto seguros.

## Sección 9: Valores por defecto seguros y diseño a prueba de fallos

Las decisiones de diseño de un driver determinan su seguridad mucho antes de que se escriba ninguna línea de código concreta. Dos drivers pueden usar las mismas APIs, el mismo asignador de memoria, las mismas primitivas de locking y terminar con posturas de seguridad muy distintas, porque uno fue diseñado para ser abierto y el otro para ser cerrado. Esta sección trata sobre las decisiones de diseño que hacen que un driver sea seguro por defecto.

La idea central se resume en un único principio: ante la duda, rechaza. Un driver que falla de forma abierta tiene que ser correcto en cada rama para ser seguro. Un driver que falla de forma cerrada solo tiene que ser correcto en las rutas limitadas donde decide permitir algo.

### Fallo cerrado

La primera y más importante decisión de diseño es qué ocurre cuando tu código alcanza un estado que no esperaba. Considera una sentencia switch:

```c
switch (op) {
case OP_FOO:
    return (do_foo());
case OP_BAR:
    return (do_bar());
}
return (0);   /* fall-through: everything else succeeds! */
```

Este es un diseño de fallo abierto. Cualquier código de operación que no sea `OP_FOO` o `OP_BAR` tiene éxito silenciosamente, devolviendo 0. Eso casi nunca es lo que quieres. Un nuevo código de operación añadido a la API pero no gestionado en el driver se convierte en un no-op silencioso. Un atacante que lo descubra puede usarlo para eludir las comprobaciones.

La versión de fallo cerrado:

```c
switch (op) {
case OP_FOO:
    return (do_foo());
case OP_BAR:
    return (do_bar());
default:
    return (EINVAL);
}
```

Las operaciones desconocidas devuelven explícitamente un error. Si se añade una nueva operación a la API, el compilador o las pruebas te avisarán en cuanto intentes gestionarla sin actualizar el switch, porque el nuevo caso es necesario para silenciar el `EINVAL`.

El mismo principio se aplica en cada punto de decisión. Cuando una función comprueba una precondición:

```c
/* Fail open: if the check is inconclusive, allow the operation. */
if (bad_condition == true)
    return (EPERM);
return (0);

/* Fail closed: if the check is inconclusive, refuse. */
if (good_condition != true)
    return (EPERM);
return (0);
```

La segunda forma falla de forma cerrada: si la precondición no puede verificarse como buena, la operación se rechaza. Esto es más seguro cuando `good_condition` tiene alguna posibilidad de ser falsa debido a un error en la configuración, una condición de carrera o un bug.

### Lista blanca, no lista negra

Íntimamente relacionado: cuando decidas qué está permitido, incluye en la lista blanca lo que se sabe que es bueno en lugar de añadir a la lista negra lo que se sabe que es malo. Las listas negras siempre son incompletas, porque no puedes enumerar todas las entradas malas. Las listas blancas son finitas por construcción.

```c
/* Bad: blacklist */
if (c == '\n' || c == '\r' || c == '\0')
    return (EINVAL);

/* Good: whitelist */
if (!isalnum(c) && c != '-' && c != '_')
    return (EINVAL);
```

La lista negra pasó por alto `\t`, `\x7f`, todos los caracteres de bit alto, etcétera. La lista blanca hizo explícito el conjunto permitido y rechazó todo lo demás.

Esto se aplica a la validación de entradas en general. Un driver que acepta un conjunto de nombres de configuración debe listarlos explícitamente. Un driver que acepta un conjunto de códigos de operación debe enumerarlos. Si un usuario envía algo que no está en la lista, recházalo.

### La interfaz mínima útil

Un driver expone funcionalidad al espacio de usuario mediante nodos de dispositivo, ioctls, sysctls y, en ocasiones, protocolos de red. Cada entrada expuesta es una superficie de ataque potencial. Un driver seguro expone solo lo que los usuarios realmente necesitan.

Antes de publicar un ioctl, pregúntate: ¿lo usa alguien realmente? Si un ioctl de depuración fue útil durante el desarrollo pero no tiene ningún papel en producción, elimínalo o compílalo de forma condicional detrás de un flag de depuración. Si un sysctl expone estado interno que solo importa para ingeniería, ocúltalo detrás de `CTLFLAG_SECURE` y considera eliminarlo.

El coste de eliminar una interfaz ahora es pequeño: unas pocas líneas de código. El coste más adelante, cuando la interfaz ya se ha publicado y tiene usuarios, es mucho mayor. Las interfaces más pequeñas son más fáciles de revisar, más fáciles de probar y tienen menos oportunidades para que aparezcan bugs.

### Mínimo privilegio al abrir

Un nodo de dispositivo puede crearse con modos restrictivos o permisivos. Empieza de forma restrictiva. Un modo de `0600` o `0640` es casi siempre un valor por defecto mejor que `0666`. Si los usuarios se quejan de que no pueden acceder al dispositivo, esa es una conversación que te interesa tener; siempre puedes relajar el modo, y el operador puede usar las reglas de devfs para hacerlo de forma específica para cada sitio. Si los usuarios obtienen silenciosamente acceso que no deberían tener, no tendrás esa conversación hasta que algo falle.

Del mismo modo, un driver que da soporte a jails debe ser inaccesible en jails por defecto, a menos que haya una razón específica. El razonamiento es el mismo: es más fácil abrirlo después que adaptar una política cerrada a algo que ya está abierto.

### Valores por defecto conservadores

Todo parámetro configurable tiene un valor por defecto. Elige valores conservadores.

Un driver que disponga de un tunable configurable del tipo «permitir al usuario X hacer Y» debe tener como valor predeterminado X = ninguno. Si un operador quiere conceder acceso, puede cambiar el tunable. Si el valor predeterminado concediera acceso, cualquier despliegue en el que se pasara por alto ese tunable quedaría expuesto.

Un driver que tenga un timeout debe tener como valor predeterminado un timeout corto. Si la operación suele completarse rápidamente, un valor predeterminado corto es suficiente. Si en ocasiones tarda más, el operador puede aumentar el timeout. Un valor predeterminado largo es una oportunidad para un ataque de denegación de servicio.

Un driver que tenga un límite de tamaño de buffer debe tener como valor predeterminado un límite pequeño. De nuevo, los operadores pueden aumentarlo; los atacantes, no.

### Defensa en profundidad

Ningún mecanismo de seguridad es perfecto por sí solo. Un driver diseñado con defensa en profundidad asume que cualquier capa puede fallar y construye múltiples capas.

Ejemplo: supón que un driver acepta un ioctl que requiere privilegios. Las capas de defensa son:

El modo del nodo de dispositivo impide que usuarios sin privilegios abran el dispositivo.

Un `priv_check` en el momento de apertura bloquea a usuarios sin privilegios incluso si el modo está mal configurado.

Un `priv_check` sobre el ioctl específico captura el caso en que un usuario sin privilegios haya llegado de algún modo al manejador del ioctl.

Un `jailed()` en el ioctl bloquea a los usuarios dentro de una jaula (jailed).

La validación de entrada sobre los argumentos del ioctl rechaza solicitudes malformadas.

Un registro con limitación de tasa anota las solicitudes malformadas repetidas.

Si las cinco están presentes, un fallo en cualquiera de ellas queda contenido por las demás. Si solo hay una y falla, el driver queda comprometido. La defensa en profundidad cuesta algo más de código y algo más de CPU, y a cambio proporciona una resiliencia real.

### Timeouts y watchdogs

Un driver que espera eventos externos debería tener timeouts. El hardware puede no responder. El espacio de usuario puede dejar de leer. Las redes pueden bloquearse. Sin un timeout, un driver en espera puede retener recursos indefinidamente, y un atacante que controle el evento externo puede denegar el servicio con solo no responder.

`msleep(9)` acepta un argumento de timeout en ticks. Úsalo. Un sleep sin timeout raramente es la respuesta correcta en el código de un driver.

Para operaciones de mayor duración, un temporizador watchdog puede detectar que una operación se ha bloqueado y tomar acciones de recuperación: abortar, reintentar o restablecer. El framework `callout(9)` es el mecanismo habitual.

### Uso acotado de recursos

Todo recurso que un driver pueda asignar en nombre de un llamador debería tener un límite máximo. Los tamaños de buffer tienen valores máximos. Los recuentos de recursos por apertura tienen valores máximos. Los recuentos de recursos globales tienen valores máximos. Cuando se alcanza el límite, el driver devuelve un error, no intenta un «mejor esfuerzo».

Sin límites, un proceso que se comporta mal o es hostil puede agotar los recursos. El agotamiento puede ser de memoria, de estado similar a descriptores de archivo, de eventos que requieren interrupción, o simplemente de tiempo de CPU. Los límites garantizan que ningún llamador individual pueda acaparar los recursos.

Una estructura predeterminada razonable:

```c
#define SECDEV_MAX_BUFLEN     (1 << 20)   /* per buffer */
#define SECDEV_MAX_OPEN_BUFS  16          /* per open */
#define SECDEV_MAX_GLOBAL     256         /* driver-wide */
```

Comprueba cada límite de forma explícita antes de asignar. Devuelve `EINVAL`, `ENOMEM` o `ENOBUFS` según corresponda cuando se alcance el límite.

### Carga y descarga segura del módulo

Un driver que admite ser descargado debe gestionar la limpieza correctamente. Una descarga no segura es un bug de seguridad. Si la descarga deja un callback registrado, un mapeo activo o un DMA en vuelo, volver a cargar el módulo (o descargarlo y reanudarlo) puede acceder a memoria que ya no es propiedad del driver. Eso es un use-after-free esperando ocurrir.

La regla es: si alguna parte de `detach` o `unload` falla, o bien propaga el error (y mantén el driver cargado) o lleva la limpieza hasta su finalización. Un desmontaje parcial es peor que ningún desmontaje.

Una estrategia razonable consiste en hacer que la ruta de descarga sea paranoica. Comprueba cada recurso y desmonta todos los que fueron asignados, en orden inverso al de asignación. Usa los helpers `callout_drain` y `taskqueue_drain` para esperar a que finalice el trabajo asíncrono. Solo después de que todos esos recursos estén inactivos se libera el softc.

Si algún paso falla, devuelve `EBUSY` desde `detach` y documenta que el driver no puede descargarse en este momento. Eso es mejor que liberar a medias y colapsar después.

### Entradas concurrentes seguras

Los puntos de entrada de un driver (open, close, read, write, ioctl) pueden invocarse de forma concurrente. El driver debe escribirse como si cualquier punto de entrada pudiera invocarse desde cualquier contexto en cualquier momento. Cualquier otra aproximación es una condición de carrera esperando a desencadenarse.

La implicación práctica es que cada punto de entrada que accede a estado compartido adquiere primero el lock del softc. Cada operación que usa recursos del softc lo hace bajo el lock. Si la operación tiene que dormir o realizar trabajo en espacio de usuario, el código suelta el lock, realiza el trabajo y lo readquiere con cuidado, comprobando que el estado no haya cambiado mientras tanto.

La concurrencia no es un añadido posterior. Es parte de la interfaz.

### Las rutas de error son rutas normales

Un aspecto sutil del diseño seguro es que las rutas de error reciben el mismo cuidado que las rutas de éxito. En un driver, las rutas de error suelen liberar recursos, soltar locks y restaurar el estado. Un bug en una ruta de error es igual de explotable que un bug en una ruta de éxito, y a menudo más, porque las rutas de error están menos probadas.

Escribe cada ruta de error como si fuera el camino habitual de un usuario que busca bugs. Cada `goto cleanup` o etiqueta `out:` es candidata a un double-free, a un unlock olvidado o a un mapeo no liberado. Recorre mentalmente cada ruta de error y confirma que:

Cada recurso asignado en la ruta de éxito se libera en la ruta de error.

Ningún recurso se libera dos veces.

Cada lock que se retiene se libera exactamente una vez.

Ninguna ruta de error deja estado parcialmente inicializado visible para otros contextos.

Un patrón sistemático ayuda. El patrón «single cleanup path» (una sola etiqueta, la limpieza procede en orden inverso al de asignación) captura la mayoría de estos bugs por construcción:

```c
static int
secdev_do_something(struct secdev_softc *sc, struct secdev_arg *arg)
{
    void *kbuf = NULL;
    struct secdev_item *item = NULL;
    int error;

    kbuf = malloc(arg->len, M_SECDEV, M_WAITOK | M_ZERO);

    error = copyin(arg->data, kbuf, arg->len);
    if (error != 0)
        goto done;

    item = uma_zalloc(sc->sc_zone, M_WAITOK | M_ZERO);

    error = secdev_process(sc, kbuf, arg->len, item);
    if (error != 0)
        goto done;

    mtx_lock(&sc->sc_mtx);
    LIST_INSERT_HEAD(&sc->sc_items, item, link);
    mtx_unlock(&sc->sc_mtx);
    item = NULL;  /* ownership transferred */

done:
    if (item != NULL)
        uma_zfree(sc->sc_zone, item);
    free(kbuf, M_SECDEV);
    return (error);
}
```

Cada asignación está emparejada con una limpieza en `done`. La limpieza usa comprobaciones de `NULL` para que los recursos liberados antes (o nunca asignados) no provoquen double-frees. Las transferencias de propiedad establecen el puntero a `NULL`, lo que suprime la limpieza.

El uso consistente de este patrón elimina la mayoría de los bugs en las rutas de limpieza. El código es más largo que un estilo de retorno anticipado, pero es enormemente más seguro.

### Tampoco te fíes de tu propio código

Un último aspecto del diseño a prueba de fallos es asumir que incluso tu propio código tiene bugs. Incluye comprobaciones `KASSERT(9)` para los invariantes. `KASSERT` no hace nada cuando `INVARIANTS` no está configurado (habitual en builds de producción), pero en kernels de desarrollo comprueba cada aserción y provoca un panic en caso de fallo. Eso convierte un bug de corrupción sutil en un panic visible y depurable.

```c
KASSERT(sc != NULL, ("secdev: NULL softc"));
KASSERT(len <= SECDEV_MAX_BUFLEN, ("secdev: len %zu too large", len));
```

Los invariantes documentados como `KASSERT` ayudan a los lectores (tú en el futuro, tus compañeros futuros) a entender qué espera el código. También detectan regresiones que de otro modo corromperían el estado de forma silenciosa.

### Degradación elegante frente a rechazo total

Una elección de diseño que surge con frecuencia en el trabajo a prueba de fallos: cuando falla una parte no crítica de una operación, ¿debe el driver continuar con un resultado degradado o debe rechazar la operación por completo?

No hay una respuesta universal. Cada caso depende de lo que el llamador probablemente hará con un éxito parcial. Un driver que devuelve un paquete con algunos campos sin inicializar (porque falló una llamada a un subsistema) está invitando al llamador a confiar en los bytes a cero como si fueran significativos. Un driver que falla toda la operación es más disruptivo pero menos sorprendente.

Para operaciones relevantes para la seguridad, prefiere el rechazo total. Un control de privilegios que falla no debe resultar en «la mayor parte de la operación se ejecutó, pero omitimos el paso privilegiado»; debe resultar en el rechazo completo. Un éxito parcial que dependía del paso omitido es un bug esperando ser encontrado.

Para operaciones no relacionadas con la seguridad, la degradación elegante suele ser la decisión correcta. Si falla una actualización de estadísticas opcional, la operación principal debería completarse igualmente. Documenta el aspecto de la degradación para que los llamadores puedan anticiparla.

### Caso práctico: valores predeterminados seguros reales en /dev/null

El driver `null` de FreeBSD, en `/usr/src/sys/dev/null/null.c`, merece estudiarse como ejemplo de diseño seguro por defecto. Es uno de los drivers más simples del árbol de código fuente, pero su construcción encarna la mayoría de los principios de este capítulo.

Crea dos nodos de dispositivo, `/dev/null` y `/dev/zero`, ambos con permisos accesibles para todos (`0666`). Esto es intencionado: están pensados para ser usados por cualquier proceso, privilegiado o no, y ninguno de los dos puede filtrar información ni corromper el estado del kernel. La decisión sobre los permisos es deliberada y está documentada.

Los manejadores de lectura, escritura e ioctl son mínimos. `null_read` devuelve 0 (fin de archivo). `null_write` consume la entrada sin tocar el estado del kernel. `zero_read` rellena el buffer del usuario con ceros usando `uiomove_frombuf` sobre un buffer estático relleno de ceros.

El manejador de ioctl devuelve `ENOIOCTL` para comandos desconocidos, de modo que las capas superiores puedan traducirlo al error apropiado. Se gestiona un pequeño conjunto de comandos `FIO*` específicos para el comportamiento no bloqueante y asíncrono, cada uno realizando únicamente la contabilidad mínima que tiene sentido para un flujo null o zero.

El driver no tiene locking porque no tiene estado mutable que proteger: el buffer de ceros es constante y las operaciones de lectura/escritura no modifican ningún dato compartido. La ausencia de locking no es descuido, sino la consecuencia de un diseño que minimiza lo que se comparte desde el principio.

El `detach` del driver es sencillo: destruye los nodos de dispositivo. Como no hay estado asíncrono, ni callouts, ni interrupciones, ni taskqueues, la limpieza es correspondientemente simple.

Lo que hace de este un buen ejemplo de valores predeterminados seguros es la disciplina de no hacer más de lo necesario. El driver no añade funcionalidades de forma especulativa, no expone estado interno ni admite ioctls que no hayan sido requeridos por usuarios concretos. Su interfaz es mínima, lo que mantiene mínima su superficie de ataque. Su comportamiento es predecible y ha sido exactamente el mismo durante décadas.

Los drivers reales no siempre pueden ser tan simples; la mayoría tienen estado que gestionar, hardware con el que comunicarse y operaciones que realizar. Pero el principio de diseño se generaliza: cuanto más simple sea el driver, menos modos de fallo tendrá. Ante la elección entre añadir funcionalidad y prescindir de ella, la opción más segura suele ser prescindir.

### Cerrando la sección 9

Los valores seguros por defecto se reducen a una predisposición al rechazo. Por defecto, `EINVAL` para entradas desconocidas. Por defecto, modos restrictivos en los nodos de dispositivo. Por defecto, límites conservadores en los recursos. Por defecto, timeouts cortos. Por defecto, requisitos de privilegio estrictos. Usa listas blancas, no listas negras. Falla en estado cerrado, no en estado abierto.

Nada de esto es exótico. Son hábitos de diseño que se acumulan. Un driver construido sobre ellos no es simplemente un driver que puede hacerse seguro; es un driver que es seguro por defecto, y que tiene que romperse activamente para volverse inseguro.

La siguiente sección cierra el capítulo examinando el otro extremo del ciclo de desarrollo: las pruebas. ¿Cómo sabes que tu driver es tan seguro como crees? ¿Cómo cazas los bugs que la revisión pasó por alto?

## Sección 10: pruebas y endurecimiento de tu driver

Un driver no es seguro porque hayas escrito código seguro. Es seguro porque lo has probado exhaustivamente, incluso en condiciones para las que no fue diseñado. Esta sección trata sobre las herramientas que FreeBSD te ofrece para encontrar bugs antes que los atacantes, y los hábitos que hacen que un driver se mantenga seguro a medida que evoluciona.

### Un recorrido práctico: encontrar un bug con KASAN

Antes de la orientación general, considera un escenario concreto. Tienes un driver que supera todas tus pruebas funcionales, pero del que sospechas que tiene un bug de seguridad de memoria. Construyes un kernel con `options KASAN`, lo arrancas, cargas tu driver y ejecutas una prueba de estrés. La prueba provoca un crash del kernel con una salida que tiene un aspecto parecido a este:

```text
==================================================================
ERROR: KASan: use-after-free on address 0xfffffe003c180008
Read of size 8 at 0xfffffe003c180008 by thread 100123

Call stack:
 kasan_report
 secdev_callout_fn
 softclock_call_cc
 ...

Buffer of size 4096 at 0xfffffe003c180000 was allocated by thread 100089:
 kasan_alloc_mark
 malloc
 secdev_attach
 ...

The buffer was freed by thread 100089:
 kasan_free_mark
 free
 secdev_detach
 ...
==================================================================
```

Lee la salida con atención. KASAN te indica la instrucción exacta que accedió a memoria liberada (`secdev_callout_fn`), la asignación exacta que fue liberada (en `secdev_attach`) y la liberación exacta (en `secdev_detach`). Ahora el bug es evidente: el callout se programó en el attach, pero el detach liberó el buffer antes de drenar el callout. Cuando el callout se dispara después de la liberación, accede al buffer ya liberado.

La solución: añadir `callout_drain` al detach antes del `free`. KASAN te ayudó a encontrar, en treinta segundos, un bug que podría haber tardado horas o semanas en encontrarse mediante inspección, y que quizás nunca habría sido encontrado en producción hasta que un cliente reportase un crash aleatorio.

KASAN no es gratuito en términos de rendimiento. La sobrecarga en tiempo de ejecución es considerable, tanto en CPU (puede que de 2 a 3 veces más lento) como en memoria (cada byte de memoria asignada tiene un byte sombra asociado). No lo usarías en producción. Pero para las pruebas de desarrollo, y especialmente para los autores de drivers, es una de las herramientas más eficaces disponibles.

KMSAN funciona de forma análoga para las lecturas de memoria no inicializada, y KCOV impulsa el fuzzing guiado por cobertura. Juntos abordan las principales clases de errores de seguridad de memoria: use-after-free (KASAN), memoria no inicializada (KMSAN) y errores que tus pruebas no alcanzan (KCOV junto con un fuzzer).

### Compilar con los sanitizadores del kernel

Un kernel FreeBSD estándar está optimizado para producción. Un kernel de desarrollo para pruebas de drivers debería estar optimizado para encontrar errores. Las opciones que añades al archivo de configuración del kernel activan comprobaciones adicionales.

**`options INVARIANTS`** activa `KASSERT(9)`. Cada aserción se comprueba en tiempo de ejecución. Una aserción fallida hace que el kernel entre en pánico con una traza de pila que apunta a la aserción. Esto detecta violaciones de invariantes que de otro modo corromperían datos de forma silenciosa.

**`options INVARIANT_SUPPORT`** está implícito en `INVARIANTS`, pero a veces es necesario como opción separada para los módulos construidos contra un kernel con `INVARIANTS`.

**`options WITNESS`** activa el comprobador de orden de locks WITNESS. Cada adquisición de lock queda registrada, y el kernel entra en pánico si se detecta un ciclo (A adquirido, luego B; más tarde, B adquirido, luego A). Esto detecta errores de deadlock antes de que se produzca el deadlock.

**`options WITNESS_SKIPSPIN`** deshabilita WITNESS para los spin mutexes, lo que puede reducir la sobrecarga a costa de perder algunas comprobaciones.

**`options DIAGNOSTIC`** habilita comprobaciones adicionales en tiempo de ejecución en varios subsistemas. Es menos estricto que `INVARIANTS` y detecta algunos casos adicionales.

**`options KASAN`** habilita el Kernel Address Sanitizer, que detecta usos tras liberación (use-after-free), accesos fuera de límites y algunos usos de memoria no inicializada. Requiere soporte del compilador y una sobrecarga de memoria considerable, pero es excelente para encontrar errores de seguridad de memoria.

**`options KMSAN`** habilita el Kernel Memory Sanitizer, que detecta usos de memoria no inicializada. Esto detecta directamente los errores de fuga de información descritos en la Sección 7.

**`options KCOV`** habilita el seguimiento de cobertura del kernel, que es lo que hace posible el fuzzing guiado por cobertura.

Un kernel de desarrollo de drivers podría incluir:

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options DIAGNOSTIC
```

y, para pruebas más profundas de seguridad de memoria, `KASAN` o `KMSAN` en las arquitecturas compatibles. Construye ese kernel, arráncalo y ejecuta tu driver contra él. Muchos errores aparecen de inmediato.

Los builds de producción no suelen incluir estas opciones (ralentizan el kernel de forma considerable). Úsalas como red de seguridad durante el desarrollo.

### Pruebas de estrés

Un driver que supera las pruebas funcionales puede fallar igualmente bajo carga. Las pruebas de estrés ejercitan la concurrencia del driver, sus patrones de asignación de memoria y sus rutas de error a volúmenes que amplifican las condiciones de carrera.

Un arnés de estrés sencillo para un dispositivo de caracteres podría:

Abrir el dispositivo desde N procesos de forma concurrente.

Cada proceso emite M ioctls con argumentos válidos e inválidos en orden aleatorio.

Un proceso separado desconecta y reconecta el dispositivo periódicamente (o ejecuta kldunload/kldload).

Esto expone rápidamente las condiciones de carrera entre las operaciones en espacio de usuario y el detach, que se encuentran entre las categorías de carrera más difíciles de detectar mediante inspección.

El framework de pruebas `stress2` de FreeBSD, disponible en `https://github.com/pho/stress2`, tiene una larga historia de encontrar errores en el kernel. Incluye escenarios para VFS, redes y varios subsistemas. El autor de un driver puede aprender mucho leyendo esos escenarios y adaptándolos a la interfaz del driver.

### Fuzzing

El fuzzing es la técnica de generar grandes cantidades de entradas aleatorias o semialeatorias y observar si el programa falla, produce aserciones o se comporta de forma incorrecta. Los fuzzers modernos están guiados por cobertura: observan qué rutas de código se ejercitan y evolucionan las entradas para explorar nuevas rutas. Esto es mucho más efectivo que la entrada puramente aleatoria.

Para las pruebas de drivers, el fuzzer principal es **syzkaller**, un proyecto externo que entiende la semántica de las syscalls y produce entradas estructuradas. Syzkaller no forma parte del sistema base de FreeBSD; es una herramienta externa que se ejecuta sobre un kernel FreeBSD construido con instrumentación de cobertura `KCOV`. Syzkaller ha encontrado muchos errores en el kernel de FreeBSD a lo largo de los años, y un driver que quiera ser ejercitado a fondo se beneficia de estar descrito en una descripción de syscall de syzkaller (archivo `.txt` bajo `sys/freebsd/` de syzkaller).

Si tu driver expone una interfaz ioctl o sysctl considerable, considera escribir una descripción de syzkaller para él. El formato es sencillo, y la inversión vale la pena la primera vez que syzkaller encuentre un error que ningún revisor humano habría detectado.

Los enfoques de fuzzing más sencillos también funcionan. Un script de shell que emita ioctls aleatorios con argumentos aleatorios y busque panics en `dmesg` es mejor que no hacer fuzzing en absoluto. El objetivo es generar entradas que tu diseño no haya anticipado.

### ASLR, PIE y protección de pila

Los kernels modernos de FreeBSD utilizan varias técnicas de mitigación de exploits. Entenderlas forma parte de comprender por qué los errores que hemos tratado son importantes.

**kASLR**, la aleatorización del espacio de direcciones del kernel (kernel Address Space Layout Randomization), coloca el código, los datos y las pilas del kernel en direcciones aleatorias durante el boot. Un atacante que quiera saltar al código del kernel, o sobreescribir un puntero a función específico, no sabe dónde se encuentran ese código o ese puntero. kASLR es fundamental para hacer que muchos errores de seguridad de memoria sean inexplotables en la práctica.

Las fugas de información (Sección 7) son especialmente peligrosas porque pueden anular kASLR. Un único puntero del kernel filtrado proporciona al atacante la dirección base y lo desbloquea todo lo demás.

**SSP**, el Stack-Smashing Protector, coloca un valor canario en la pila entre las variables locales y la dirección de retorno. Cuando una función retorna, se comprueba el canario; si ha sido sobreescrito (porque un desbordamiento de buffer lo ha machacado de camino hacia la dirección de retorno), el kernel entra en pánico. SSP no previene los desbordamientos, pero sí impide que muchos de ellos tomen el control de la ejecución.

No todas las funciones están protegidas. El compilador aplica SSP en función de heurísticas: funciones con buffers locales, funciones que toman la dirección de variables locales, etc. Entender esto significa entender por qué ciertos patrones de desbordamiento de buffer son más explotables que otros.

**PIE**, los ejecutables independientes de posición (Position-Independent Executables), permite que el kernel (y los módulos) se reubiquen en direcciones aleatorias. Combinado con kASLR, esto es lo que hace efectiva la aleatorización.

**Los protectores de pila y las páginas de guarda** rodean las pilas del kernel con páginas no mapeadas. Un intento de escribir más allá de la pila choca con una página no mapeada y provoca un pánico en lugar de corromper silenciosamente la memoria adyacente.

**W^X**, escritura-o-ejecución (write-xor-execute), mantiene la memoria del kernel como escribible o ejecutable, nunca ambas a la vez. Esto impide muchos exploits clásicos que dependían de escribir shellcode en memoria y luego saltar a él.

El autor de un driver no implementa ninguna de estas protecciones; son protecciones que afectan a todo el kernel. Pero los errores de un driver pueden socavarlas. Una fuga de información anula kASLR. Un desbordamiento de buffer que golpea de forma fiable un puntero a función o una vtable anula SSP. Un use-after-free que compite con una asignación nueva entrega al atacante memoria controlada en una dirección del kernel.

En resumen: el objetivo de un código de driver cuidadoso no es solo evitar los cuelgues. Es mantener intactas las defensas del kernel. Cuando tu driver filtra un puntero, no solo has expuesto información; has degradado la postura de mitigación de exploits de todo el sistema.

### Lectura de los diffs

Cada vez que modifiques el driver, lee el diff con atención. Busca:

Nuevas llamadas a `copyin` o `copyout`: ¿están las longitudes acotadas? ¿Se ponen los buffers a cero antes?

Nuevas operaciones sensibles a privilegios: ¿tienen `priv_check` o equivalente?

Nuevos locks: ¿es el orden de adquisición coherente con el resto del código?

Nuevas asignaciones de memoria: ¿están emparejadas con liberaciones en cada ruta, incluidas las rutas de error?

Nuevos mensajes de log: ¿están limitados en frecuencia? ¿Filtran datos sensibles?

Nuevos campos visibles para el usuario en estructuras: ¿están inicializados? ¿Se pone la estructura a cero antes del copyout?

El hábito de revisar los diffs detecta muchas regresiones. Si tu proyecto utiliza revisión de código (debería hacerlo), incluye estas preguntas en la lista de comprobación.

### Análisis estático

El código del kernel de FreeBSD puede analizarse con varias herramientas de análisis estático, entre ellas `cppcheck`, `clang-analyzer` (scan-build) y, cada vez más, herramientas al estilo de Coverity y GitHub CodeQL. Estas herramientas a menudo reportan advertencias que un revisor humano pasaría por alto: una condición que nunca puede ser verdadera, un puntero usado tras una ruta en la que fue liberado, una comprobación de null ausente.

Toma en serio las advertencias del análisis estático. La mayoría son falsos positivos; algunas son errores reales. Silenciar una advertencia debe ser una decisión, no un reflejo. Cuando la herramienta se equivoca, añade un comentario explicando el motivo. Cuando la herramienta tiene razón, corrige el código.

La comprobación de sintaxis con `bmake` sobre el árbol de código fuente del kernel es un primer paso rápido. Ejecutar `clang --analyze` o `scan-build` sobre tu driver es un paso más profundo. Ninguno de los dos reemplaza la revisión o las pruebas, pero ambos detectan errores a bajo coste.

### Revisión de código

Ninguna herramienta reemplaza a otro par de ojos. La revisión es especialmente importante para el código relevante para la seguridad. Cuando propongas un cambio en una ruta sensible a la seguridad, busca a alguien más que lo revise. Describe en qué consiste el cambio, qué invariantes preserva y qué has comprobado. Agradece que encuentren un problema que tú no hayas detectado.

Para los proyectos de código abierto, el sistema de revisión de FreeBSD (`reviews.freebsd.org`) proporciona una forma cómoda de obtener revisión externa. Úsalo. La comunidad tiene una larga tradición de revisión reflexiva y consciente de la seguridad, y los revisores a menudo detectan cosas que tú no verías.

### Pruebas tras un cambio

Cuando se encuentra y corrige un error, añade una prueba que lo habría detectado. Esto es importante porque:

La misma clase de error suele reaparecer en otros lugares. Una prueba que detecta la instancia concreta puede detectar futuros errores similares.

Sin una prueba, no tienes forma de saber que la corrección ha funcionado.

Sin una prueba, una futura refactorización podría reintroducir el error.

Las pruebas pueden ser pruebas unitarias (en espacio de usuario, ejercitando funciones individuales), pruebas de integración (cargando el driver en una VM y ejercitándolo) o casos de fuzz (entradas que antes provocaban un fallo y que ya no deberían hacerlo). Todas tienen su lugar.

### Integración continua

Las pruebas automatizadas en cada cambio detectan las regresiones de forma temprana. Una configuración de CI que construya el driver contra un kernel de desarrollo con `INVARIANTS`, `WITNESS` y posiblemente `KASAN`, ejecute el arnés de estrés y compruebe el resultado, es la columna vertebral de un driver que se mantiene seguro.

Para un driver en el árbol de FreeBSD, esto ya lo proporciona el CI del proyecto. Para los drivers fuera del árbol, configurar un CI requiere algo de esfuerzo, pero la inversión se recupera rápidamente.

### Tomar en serio los informes de errores

Cuando alguien reporta un cuelgue o una posible vulnerabilidad en tu driver, trátalo como real hasta que tengas pruebas de lo contrario. Incluso un error "inofensivo" puede ser explotable de maneras que el reportador no vio. "Puedo hacer que el kernel entre en pánico con este ioctl" no es un problema menor; es, como mínimo, un error de denegación de servicio, y muy a menudo un error de seguridad de memoria que podría convertirse en ejecución de código arbitrario.

El equipo de seguridad de FreeBSD (`secteam@freebsd.org`) es el destinatario adecuado para los informes de vulnerabilidades en el sistema base. Para los drivers fuera del árbol, dispón de un canal similar. Responde con rapidez, corrige con cuidado y da crédito al reportador cuando corresponda.

### Hardening a lo largo del tiempo

La postura de seguridad de un driver no es estática. Surgen nuevas clases de errores. Aparecen nuevas mitigaciones. Las nuevas técnicas de ataque hacen que los errores antiguos sean más explotables. Dedica tiempo en cada ciclo de versión a:

Releer las rutas relevantes para la seguridad del driver.

Comprobar si hay nuevas advertencias del compilador o hallazgos del análisis estático.

Probar las últimas herramientas (KASAN, KMSAN, syzkaller) contra el driver.

Actualizar el modelo de privilegios si FreeBSD ha añadido nuevos códigos `PRIV_*` o comprobaciones más específicas.

Eliminar las interfaces que ningún usuario necesita realmente.

La disciplina de la reexaminación periódica es lo que distingue a un driver que es seguro el día en que se publica de uno que permanece seguro durante toda su vida útil.

### Post-incidente: qué hacer cuando un error se convierte en CVE

Un capítulo realista sobre seguridad debe contemplar la posibilidad de que, a pesar de todas las precauciones, un error en tu driver sea reportado externamente como una vulnerabilidad. El proceso suele ser el siguiente:

Un investigador o usuario descubre un comportamiento inesperado en tu driver.

Lo investigan y determinan que el comportamiento es un error de seguridad: fuga de información, escalada de privilegios, fallo al procesar entrada no confiable o similar.

Lo notifican mediante un canal de divulgación responsable (para los drivers del sistema base de FreeBSD, este canal es la dirección `secteam@freebsd.org`).

Recibes el informe.

La primera respuesta importa. Aunque el bug resulte ser menos grave de lo que parece a primera vista, trata al notificador como un colaborador, no como un adversario. Acusa recibo con prontitud. Haz preguntas de aclaración si es necesario. No lo desestimes sin investigar. No intentes silenciar al notificador. La mayoría de los investigadores de vulnerabilidades quieren que el bug se corrija; si colaboras, obtienes una solución más rápido y habitualmente recibes crédito público que beneficia la imagen del proyecto.

Clasifica el informe técnicamente. ¿Puedes reproducir el bug? ¿Es un crash, una fuga de información, una escalada de privilegios o algo distinto? ¿Cuál es el modelo de atacante: quién necesita tener acceso y qué obtiene? ¿Es explotable en combinación con otros bugs conocidos?

Si se confirma, coordina una solución. Ten en cuenta que, para los drivers del sistema base de FreeBSD, la corrección debe pasar por el proceso de revisión habitual del proyecto y, cuando corresponda, por el proceso de avisos de seguridad. Para drivers externos al árbol, tienes más flexibilidad, pero aun así debes escribir la corrección con cuidado y probarla a fondo.

Prepara la divulgación. La práctica habitual de divulgación concede al proyecto tiempo para corregir el bug antes de que los detalles sean públicos. Las normas del sector suelen establecer 90 días. Dentro de ese plazo se prepara el aviso, se publica una versión parcheada y la divulgación pública se produce de forma simultánea con la publicación. No filtres detalles antes de tiempo ni retrases la divulgación más allá de la fecha acordada.

Redacta el mensaje del commit con cuidado. Los commits de correcciones de seguridad deben mencionar la vulnerabilidad sin proporcionar una hoja de ruta a los atacantes. "Fix incorrect bounds check in secdev_write that could allow kernel memory disclosure" es mejor que "tweak write" (demasiado vago, los revisores no lo detectan) o que "Fix CVE-2026-12345, where an attacker can read arbitrary kernel memory by issuing a write of X bytes followed by a read, bypassing Y check" (demasiado específico: los atacantes leen el historial de commits antes de que los usuarios puedan actualizar).

Tras la publicación, si los detalles se hacen públicos, prepárate para responder preguntas. Los usuarios querrán saber: ¿soy vulnerable?, ¿cómo actualizo? y ¿cómo puedo saber si fui atacado? Ten respuestas claras y serenas preparadas de antemano.

Realiza una revisión retrospectiva del bug. No para buscar culpables, sino para aprender. ¿Por qué existía el bug? ¿Hubo algún patrón que la revisión no detectó? ¿Podría haberlo detectado alguna herramienta? ¿Debería cambiar el proceso del equipo? Escribe las conclusiones y aplícalas en el trabajo futuro.

La seguridad es una práctica continua, y el aprendizaje tras un incidente es una de sus partes más importantes. Un proyecto que corrige el bug y sigue adelante no ha aprendido nada; un proyecto que reflexiona sobre por qué ocurrió el bug hace que el siguiente sea menos probable.

### En resumen: sección 10

Las pruebas y el hardening son la forma en que un diseño cuidadoso se convierte en uno seguro. Construye tu kernel de desarrollo con `INVARIANTS`, `WITNESS` y, cuando sea posible, `KASAN` o `KMSAN`. Realiza pruebas de estrés bajo carga concurrente. Aplica fuzzing con syzkaller o, como mínimo, con un harness de entrada aleatoria. Utiliza análisis estático. Revisa los diffs. Responde con seriedad a los informes de errores. Vuelve a probar tras cada corrección. Endurece el código con el tiempo.

Un driver no se vuelve seguro por casualidad. Se vuelve seguro porque el autor asumió que existían errores, los buscó con todas las herramientas disponibles y los corrigió uno a uno.

## Laboratorios prácticos

Los laboratorios de este capítulo construyen un pequeño dispositivo de caracteres llamado `secdev` y te guían para hacerlo progresivamente más seguro. Cada laboratorio parte de un archivo inicial proporcionado, te pide realizar cambios concretos y ofrece una referencia «corregida» con la que comparar. Trabájalos en orden.

Los laboratorios están diseñados para ejecutarse en una máquina virtual FreeBSD 14.3 o en un host de pruebas donde los kernel panics sean aceptables. No los ejecutes en una máquina con servicios importantes en ejecución; un error en el driver inseguro puede hacer que el kernel se cuelgue.

Si estás ejecutando estos laboratorios dentro de una VM, asegúrate de que la VM está configurada para escribir los volcados de memoria en una ubicación que puedas recuperar tras el reinicio. Activa `dumpon(8)` y configura `/etc/fstab` adecuadamente para que los core dumps acaben en `/var/crash` después de un panic. Consulta `/usr/src/sbin/savecore/savecore.8` para más detalles. Esta infraestructura es la que utilizarás para diagnosticar cualquier panic que provoquen los laboratorios.

Los archivos complementarios de estos laboratorios se encuentran en `examples/part-07/ch31-security/`. Cada laboratorio tiene su propia subcarpeta que contiene un archivo fuente `secdev.c`, un `Makefile`, un `README.md` que describe el laboratorio y, cuando corresponde, una subcarpeta `test/` con pequeños programas de prueba en espacio de usuario.

A medida que avances en los laboratorios, mantén un registro en tu cuaderno de laboratorio: qué archivos modificaste, qué observaste al cargar la versión defectuosa, qué observaste con la corrección aplicada y cualquier comportamiento inesperado. El cuaderno es una herramienta de aprendizaje; te obliga a articular lo que ves, y así es como se consolida el aprendizaje.

### Laboratorio 1: el secdev inseguro

**Objetivo.** Construir, cargar y probar la versión intencionalmente insegura de `secdev`, confirmar que funciona y, a continuación, identificar al menos tres problemas de seguridad leyendo el código con una mentalidad de seguridad.

**Requisitos previos.**

Este laboratorio presupone que dispones de una máquina virtual FreeBSD 14.3 o un sistema de pruebas donde puedas cargar y descargar módulos del kernel. Deberías haber completado ya los capítulos de construcción de módulos (Parte 2 en adelante) para que `make`, `kldload`, `kldunload` y el acceso a los nodos de dispositivo te resulten familiares. Si no es así, detente y vuelve a repasar esos capítulos; el resto del Capítulo 31 presupone que te sientes cómodo con la compilación de módulos.

**Pasos.**

1. Copia `examples/part-07/ch31-security/lab01-unsafe/` en un directorio de trabajo en tu máquina de pruebas FreeBSD. Puedes clonar el repositorio complementario del libro o copiar los archivos manualmente si los extrajiste localmente.

2. Lee `secdev.c` detenidamente. Fíjate en lo que hace: proporciona un dispositivo de caracteres `/dev/secdev` con operaciones `read`, `write` e `ioctl`. `read` devuelve el contenido de un buffer interno. `write` copia los datos del usuario en el buffer. Un ioctl (`SECDEV_GET_INFO`) devuelve una estructura que describe el dispositivo.

3. Lee el `Makefile`. Debe ser un makefile estándar de módulo del kernel de FreeBSD que utilice `bsd.kmod.mk`.

4. Construye el módulo con `make`. Resuelve cualquier error de compilación consultando los capítulos anteriores sobre construcción de módulos. Una compilación correcta produce `secdev.ko`.

5. Carga el módulo con `kldload ./secdev.ko`. Verifica con:
   ```
   kldstat | grep secdev
   ls -l /dev/secdev
   ```
   Deberías ver el módulo listado y el nodo de dispositivo presente con los permisos que haya creado el driver inseguro.

6. Prueba el dispositivo como una prueba funcional normal:
   ```
   echo "hello" > /dev/secdev
   cat /dev/secdev
   ```
   Deberías ver `hello` impreso de vuelta. Si no es así, revisa `dmesg` para ver los mensajes de error.

7. Ahora, revisa el código con la mentalidad de seguridad de este capítulo. Para cada una de las siguientes categorías, encuentra al menos un problema en el código inseguro:
   - Oportunidad de desbordamiento de buffer.
   - Oportunidad de filtración de información.
   - Comprobación de privilegios ausente.
   - Entrada de usuario no verificada.
   Anota cada hallazgo en tu cuaderno de laboratorio, incluyendo el número de línea y la preocupación concreta.

8. Descarga el módulo con `kldunload secdev` cuando hayas terminado. Verifica con `kldstat` que ya no está.

**Observaciones.**

El `secdev` inseguro tiene varios problemas por diseño. En `secdev_write`, el código llama a `uiomove(sc->sc_buf, uio->uio_resid, uio)`, que copia `uio_resid` bytes independientemente de `sizeof(sc->sc_buf)`. Una escritura de 8192 bytes en un buffer de 4096 bytes desborda el buffer interno. Dependiendo de lo que haya junto a `sc_buf` en memoria, esto puede producir un panic inmediato o no, pero siempre corrompe la memoria adyacente del kernel.

`SECDEV_GET_INFO` devuelve una `struct secdev_info` que se rellena campo a campo sin haberse puesto a cero previamente. Cualquier byte de relleno entre campos transporta contenido de la pila al espacio de usuario. La estructura probablemente tiene relleno alrededor de los miembros `uint64_t` por razones de alineación.

El dispositivo se crea con `args.mda_mode = 0666` (o equivalente), lo que permite a cualquier usuario del sistema leer y escribir. Un usuario sin privilegios especiales puede corromper el buffer del kernel o filtrar información a través del ioctl.

El manejador de ioctl no comprueba `priv_check` ni nada similar. Cualquier usuario que pueda abrir el dispositivo puede emitir cualquier ioctl.

`secdev_read` copia `sc->sc_buflen` bytes independientemente del tamaño del buffer del llamador, pudiendo leer más allá de los datos válidos si `sc_buflen` fue alguna vez mayor que el contenido válido actual.

**Exploración adicional.**

Como usuario no root, prueba las operaciones que deberían ser privilegiadas y confirma que tienen éxito cuando no deberían. Escribe un pequeño programa en C que emita `SECDEV_GET_INFO` e imprima la estructura devuelta como un volcado hexadecimal. Busca bytes distintos de cero en campos que no se establecieron explícitamente; esos son datos filtrados del kernel.

**En resumen.**

El objetivo de este laboratorio es el reconocimiento de patrones. Un driver real tendría versiones más sutiles de todos estos errores, enterradas dentro de cientos de líneas de código. Entrenarte para verlos en un driver simple los hace más fáciles de detectar en cualquier otro sitio. Guarda `lab01-unsafe/secdev.c` como referencia de lo que no hay que hacer.

### Laboratorio 2: comprobar los límites del buffer

**Objetivo.** Corregir el desbordamiento de buffer en `write` y añadir la correspondiente comprobación de longitud en `read`. Observa la diferencia en el comportamiento del driver cuando se somete a pruebas de estrés.

**Pasos.**

1. Parte de `lab02-bounds/secdev.c`. Este es el código de `lab01` más algunos comentarios `TODO` que marcan dónde añadirás las comprobaciones.

2. En `secdev_write`, calcula cuántos datos pueden escribirse de forma segura en el buffer interno. Recuerda que `uiomove` escribe como máximo la longitud que le pases. Limita `uio->uio_resid` al espacio disponible restante antes de llamar a `uiomove`.

3. En `secdev_read`, asegúrate de copiar solo los datos que sean realmente válidos en el buffer, no su tamaño asignado completo.

4. Vuelve a compilar y a probar. Con las correcciones aplicadas, una escritura de 10 KB en un buffer de 4 KB debería simplemente llenarlo, no desbordarlo.

5. Somete el driver corregido a pruebas de estrés:
   ```
   dd if=/dev/zero of=/dev/secdev bs=8192 count=100
   dd if=/dev/secdev of=/dev/null bs=8192 count=100
   ```
   Ninguno de los comandos debería hacer que el kernel se cuelgue ni producir advertencias en `dmesg`. Si lo hacen, tu comprobación de límites está incompleta.

6. Compara tu corrección con `lab02-fixed/secdev.c`. Si tu corrección es diferente pero correcta, está bien; pueden ser válidas varias soluciones. Si la tuya es incorrecta, estudia la corrección de referencia y comprende dónde te equivocaste.

**Ganando confianza.**

Escribe un pequeño programa en C que realice escrituras de distintos tamaños (0 bytes, 1 byte, el tamaño del buffer, el tamaño del buffer + 1, mucho más grande que el tamaño del buffer) y verifique que cada una devuelve el número esperado de bytes escritos o un error sensato. Este tipo de prueba de límites es la forma que tienen los tests reales de un driver.

**En resumen.**

La comprobación de límites es la corrección de seguridad más simple y captura una gran fracción de los errores reales en drivers. Interioriza el patrón: cada `uiomove`, `copyin`, `copyout` y memcpy limita la longitud respecto a los tamaños tanto del origen como del destino. El compilador no puede detectar esto por ti; es responsabilidad exclusiva del autor.

### Laboratorio 3: poner a cero antes de copyout

**Objetivo.** Corregir la filtración de información en el ioctl `SECDEV_GET_INFO`. Observa, mediante un programa de prueba en espacio de usuario, la diferencia entre la versión defectuosa y la corregida.

**Pasos.**

1. Parte de `lab03-info-leak/secdev.c`. Contiene el ioctl tal como estaba en el código inseguro original.

2. Observa la definición de la estructura. Fíjate en el relleno entre campos:
   ```c
   struct secdev_info {
       uint32_t version;
       /* 4 bytes of padding here on 64-bit systems */
       uint64_t flags;
       uint16_t id;
       /* 6 bytes of padding to align name to 8 bytes */
       char name[32];
   };
   ```
   Comprueba el tamaño con `pahole` o con un pequeño programa en C que imprima `sizeof(struct secdev_info)`.

3. Antes de corregirlo, construye y carga la versión defectuosa. Ejecuta el programa de prueba que se proporciona en `lab03-info-leak/test/leak_check.c`. Este emite el ioctl repetidamente y vuelca la estructura devuelta como un volcado hexadecimal. Fíjate en los bytes de relleno. Deberías ver valores distintos de cero que varían entre ejecuciones; esos son bytes filtrados de la pila del kernel.

4. En `secdev_ioctl`, antes de rellenar la `struct secdev_info`, pon la estructura a cero con `bzero` (o utiliza la inicialización `= { 0 }`).

5. Corrige también el campo de nombre: usa `snprintf` en lugar de `strncpy` para garantizar un terminador NUL, y copia solo hasta el NUL en lugar de todo el tamaño del buffer.

6. Vuelve a compilar y a probar con el mismo programa `leak_check`. Los bytes de relleno deberían ser cero en cada ejecución. El comportamiento visible desde el espacio de usuario no cambia; el cambio interno es que los bytes de relleno ya no transportan contenido de la pila.

7. Compara con `lab03-fixed/secdev.c`.

**Una exploración más profunda.**

Si tienes `KMSAN` compilado en tu kernel, carga la versión defectuosa del driver y ejecuta `leak_check`. KMSAN debería informar de una lectura no inicializada cuando se copia la estructura hacia fuera. Esto demuestra por qué KMSAN es valioso: detecta filtraciones de información que son invisibles sin él.

**En resumen.**

Esta corrección cuesta una sola llamada a `bzero`. El beneficio es que el ioctl no puede filtrar información jamás, independientemente de los campos que añadan o eliminen los cambios futuros. Haz de `bzero` (o la declaración con inicialización a cero) parte de tu reflejo para cualquier estructura que vaya a tocar `copyout`, `sysctl` o límites similares.

### Laboratorio 4: añadir comprobaciones de privilegios

**Objetivo.** Restringir el dispositivo a usuarios con privilegios y verificar que el acceso sin privilegios es denegado.

**Pasos.**

1. Parte de `lab04-privilege/secdev.c`.

2. Modifica el código de creación del nodo de dispositivo en `secdev_modevent` (o en `secdev_attach`, según la estructura) para que use un modo restrictivo (`0600`) y el usuario y grupo root:
   ```c
   args.mda_uid = UID_ROOT;
   args.mda_gid = GID_WHEEL;
   args.mda_mode = 0600;
   ```

3. En `secdev_open`, añade una llamada a `priv_check(td, PRIV_DRIVER)` al principio:
   ```c
   error = priv_check(td, PRIV_DRIVER);
   if (error != 0)
       return (error);
   ```
   Devuelve el error si la comprobación falla.

4. Vuelve a compilar y recarga el módulo.

5. Prueba desde un shell sin privilegios de root:
   ```
   % cat /dev/secdev
   cat: /dev/secdev: Permission denied
   ```
   La apertura debería fallar con `EPERM` (informado como "Permission denied"). El modo del sistema de archivos bloquea el acceso antes de que se llegue siquiera a `d_open`.

6. Cambia temporalmente el modo del nodo de dispositivo (como root) con `chmod 0666 /dev/secdev`. Intenta de nuevo como usuario sin privilegios. Esta vez el sistema de archivos permite la apertura, pero `priv_check` en `d_open` la deniega:
   ```
   % cat /dev/secdev
   cat: /dev/secdev: Operation not permitted
   ```
   Esto demuestra la capa de defensa dentro del kernel.

7. Restablece los permisos con `chmod 0600 /dev/secdev` o recarga el módulo para restaurar el valor predeterminado.

8. Como root, el dispositivo debería seguir funcionando con normalidad. Verifica:
   ```
   # echo "hello" > /dev/secdev
   # cat /dev/secdev
   hello
   ```

9. Compara con `lab04-fixed/secdev.c`.

**Profundizando más.**

Intenta crear un entorno enjaulado y ejecutar un shell como root dentro del jail:
```console
# jail -c path=/ name=testjail persist
# jexec testjail sh
# cat /dev/secdev
```
Dependiendo de si tu driver ha añadido una comprobación de `jailed()`, el comportamiento varía. Si el driver no comprueba `jailed`, el root enjaulado puede seguir accediendo al dispositivo. Añade `if (jailed(td->td_ucred)) return (EPERM);` al principio de `secdev_open` y verifica que el acceso desde el jail ahora es denegado.

**En resumen.**

Restringir los permisos del nodo de dispositivo es una defensa de dos capas: la capa del sistema de archivos y el `priv_check` en el kernel. Ambas juntas hacen que el driver sea robusto frente a errores de configuración. Añadir `jailed()` encima bloquea incluso al root dentro de una jaula para operaciones sensibles. Cada capa defiende frente a un modo de fallo diferente; no te apoyes en una sola.

### Lab 5: Registro con tasa limitada

**Objetivo.** Añade un mensaje de registro con tasa limitada para ioctls malformados y verifica que una avalancha de peticiones malformadas no sature el registro.

**Pasos.**

1. Parte del archivo `lab05-ratelimit/secdev.c`.

2. Añade un `struct timeval` estático y un `int` estático para almacenar el estado de la limitación de tasa. Estos son globales por driver, no por softc, a menos que quieras límites específicos por dispositivo:
   ```c
   static struct timeval secdev_log_last;
   static int secdev_log_pps;
   ```

3. En `secdev_ioctl`, en la rama `default` (el caso que gestiona los ioctls desconocidos), usa `ppsratecheck` para decidir si registrar el mensaje:
   ```c
   default:
       if (ppsratecheck(&secdev_log_last, &secdev_log_pps, 5)) {
           device_printf(sc->sc_dev,
               "unknown ioctl 0x%lx from uid %u\n",
               cmd, td->td_ucred->cr_uid);
       }
       return (ENOTTY);
   ```
   El tercer argumento, `5`, es el número máximo de mensajes por segundo.

4. Vuelve a compilar y recarga el módulo.

5. Escribe un pequeño programa de prueba que emita un millón de ioctls incorrectos en un bucle cerrado:
   ```c
   #include <sys/ioctl.h>
   #include <fcntl.h>
   int main(void) {
       int fd = open("/dev/secdev", O_RDWR);
       for (int i = 0; i < 1000000; i++)
           ioctl(fd, 0xdeadbeef, NULL);
       return (0);
   }
   ```

6. Mientras se ejecuta (como root), monitoriza `dmesg -f`. Deberías ver mensajes llegando, pero a no más de unas 5 por segundo. Sin limitación de tasa, tendrías un millón de mensajes.

7. Cuenta los mensajes con algo como `dmesg | grep "unknown ioctl" | wc -l`. Compara con un millón (el número de intentos).

8. Compara con `lab05-fixed/secdev.c`.

**Variaciones para experimentar.**

Sustituye `ppsratecheck` por `eventratecheck` y observa la diferencia (basado en eventos frente a por segundo). Experimenta con diferentes tasas máximas. Añade un resumen del recuento de mensajes suprimidos que se emita periódicamente (por ejemplo, «se han suprimido N mensajes en los últimos M segundos») para dar visibilidad al operador.

**En resumen.**

El registro con tasa limitada te da visibilidad sobre actividad sospechosa sin convertir el driver en un vector de denegación de servicio. Aplica este patrón a cualquier mensaje de registro que pueda desencadenarse desde acciones del usuario. El coste son unas pocas líneas adicionales por cada instrucción de registro; el beneficio es que tu driver deja de ser una herramienta que los atacantes puedan usar para saturar el sistema.

### Lab 6: Detach seguro

**Objetivo.** Haz que `secdev_detach` sea seguro bajo actividad concurrente. Observa, provocando deliberadamente una condición de carrera entre la descarga y el uso activo, cómo la corrección previene los pánicos por use-after-free.

**Pasos.**

1. Parte del archivo `lab06-detach/secdev.c`. Esta versión introduce un pequeño callout que actualiza periódicamente un contador interno, y un ioctl que duerme brevemente para simular una operación de larga duración.

2. Revisa la función `detach` actual. Fíjate en qué libera y en qué orden. El archivo de partida tiene intencionalmente un detach defectuoso que libera el softc sin drenar.

3. Prueba primero la versión defectuosa (compila con `INVARIANTS` y `WITNESS` en el kernel):
   - Inicia un programa de prueba que mantenga `/dev/secdev` abierto y emita el ioctl lento en un bucle.
   - Mientras se ejecuta, emite `kldunload secdev`.
   - Observa el resultado. Puedes ver un pánico del kernel, un `kldunload` bloqueado o, si tienes suerte, nada visible (la condición de carrera puede no activarse en cada ejecución). `WITNESS` puede quejarse sobre el estado de los locks.
   - Vuelve a compilar e inténtalo de nuevo hasta que veas el problema. Las condiciones de carrera concurrentes pueden ser intermitentes.

4. Ahora corrige el detach:
   - Usa `destroy_dev` sobre el cdev antes de cualquier otra limpieza, de modo que ningún thread del espacio de usuario pueda entrar al driver y cualquier thread en vuelo termine antes de que `destroy_dev` retorne.
   - Añade una llamada a `callout_drain` antes de liberar el softc. Esto garantiza que cualquier callout en vuelo haya terminado.
   - Si el driver usa un taskqueue, añade `taskqueue_drain_all`.
   - Solo después de todo el drenado, libera los recursos.

5. Vuelve a compilar y reprueba la misma condición de carrera:
   - El programa de usuario continúa ejecutándose sin interrupciones durante `kldunload`.
   - Tras `kldunload`, el siguiente ioctl del programa de usuario recibe un error (normalmente `ENXIO`) porque el cdev fue destruido.
   - El kernel permanece estable. Sin pánicos, sin quejas de `WITNESS`.

6. Compara con `lab06-fixed/secdev.c`. Confirma que la versión corregida gestiona la actividad en vuelo de forma segura.

**Comprendiendo lo que ocurrió.**

La versión defectuosa genera una condición de carrera porque:
- `destroy_dev` no se llama, o no se llama lo suficientemente pronto. Las llamadas `d_*` en vuelo continúan.
- El callout está programado para el futuro y aún no se ha disparado.
- El softc se libera mientras algo todavía lo referencia.
- El softc liberado es reutilizado por el asignador para otro fin.
- El callout se dispara, accede a lo que cree que es su softc y corrompe la memoria que ahora ocupa ese espacio.

La corrección ordena la limpieza en secuencia: primero deja de aceptar nuevas entradas (`destroy_dev`), luego detiene las entradas en vuelo esperando a que salgan (parte del contrato de `destroy_dev`), después detiene el trabajo programado (`callout_drain`) y solo entonces libera el estado. Cada paso cierra una puerta; nada más allá de una puerta cerrada puede alcanzar la memoria que se está liberando.

**En resumen.**

Las condiciones de carrera durante el detach se encuentran entre los bugs de driver más difíciles de detectar mediante inspección, porque el bug solo ocurre cuando la sincronización se alinea. Usar `destroy_dev`, `callout_drain` y `taskqueue_drain_all` de forma defensiva en cada función `detach` es uno de los hábitos de mayor valor que puedes adoptar. Hazlo de manera mecánica, aunque no creas que tu driver tiene actividad asíncrona. El próximo autor que añada un callout puede olvidarlo; tu detach defensivo lo detectará.

### Lab 7: Valores por defecto seguros en todas partes

**Objetivo.** Aplica todas las lecciones hasta ahora a un único driver: el «secure secdev». Constrúyelo a partir de un esqueleto y revisa el resultado final como si estuvieras realizando una auditoría de seguridad.

**Pasos.**

1. Parte del archivo `lab07-secure/secdev.c`. Es un esqueleto con marcadores `TODO` en muchos lugares.

2. Rellena cada `TODO` aplicando las lecciones de los Labs 1 al 6, más cualquier defensa adicional que consideres apropiada. Adiciones sugeridas:
   - Un `MALLOC_DEFINE` para la memoria del driver.
   - Un mutex en el softc que proteja todos los campos compartidos.
   - `priv_check(td, PRIV_DRIVER)` en `d_open` y en cada ioctl con privilegios.
   - Comprobaciones `jailed()` para operaciones que no deben estar disponibles para usuarios dentro de una jaula.
   - `securelevel_gt` para operaciones que deben rechazarse con un securelevel elevado.
   - `bzero` en cada estructura antes de rellenarla para el `copyout`.
   - `M_ZERO` en cada asignación que vaya a copiarse al espacio de usuario.
   - `explicit_bzero` en los buffers sensibles antes de `free`.
   - `device_printf` con tasa limitada en cada mensaje de registro que pueda desencadenarse desde el espacio de usuario.
   - `destroy_dev`, `callout_drain` y otros drenajes en `detach` antes de cualquier liberación.
   - Una bandera `secdev_debug` controlada por sysctl que condicione el registro detallado.
   - Validación de entrada que use una lista blanca de códigos de operación permitidos.
   - Copias con límites en ambas direcciones.
   - Sentencias `KASSERT` que documenten los invariantes internos.

3. Vuelve a compilar y carga el módulo.

4. Ejecuta una prueba funcional exhaustiva para confirmar que todo sigue funcionando:
   - Como root, abre el dispositivo, lee, escribe y usa ioctl.
   - Como usuario sin privilegios, confirma que `/dev/secdev` es inaccesible.
   - Dentro de una jaula, confirma que las operaciones sensibles son rechazadas.

5. Ejecuta una prueba de estrés de seguridad:
   - Casos límite (lecturas de 0 bytes, escrituras del tamaño exacto del buffer, escrituras de un byte por encima).
   - ioctls malformados.
   - open/read/write/close concurrentes desde múltiples procesos.
   - `kldunload` durante el uso activo.

6. Compara tu trabajo con `lab07-fixed/secdev.c`. Observa las diferencias. Allí donde tu versión sea más defensiva, pregúntate si la defensa adicional vale la complejidad. Allí donde la referencia sea más defensiva, pregúntate si has pasado por alto alguna defensa.

**Una autoevaluación.**

Una vez que tu driver del lab 7 compile y pase las pruebas, ponte el sombrero del revisor. Repasa la sección de la Lista de comprobación de seguridad de este capítulo y confirma cada elemento. Los elementos que no puedas confirmar son lagunas en tu driver. Corrígelas ahora, mientras el código está fresco; más adelante, encontrar y corregir esas lagunas es más lento y propenso a errores.

**En resumen.**

Este lab es la consolidación del capítulo. Tu driver terminado sigue siendo un dispositivo de caracteres simple, pero es uno del que no te avergonzarías si apareciera en un árbol real de FreeBSD. Las prácticas que has aplicado aquí son las mismas que separan los drivers aficionados de los profesionales. Guarda tu driver del lab 7 como referencia: cuando escribas tu primer driver real, este será el esqueleto del que partirás.

## Ejercicios de desafío

Estos desafíos van más allá de los laboratorios. Son más largos, más abiertos y asumen que has terminado el Lab 7. Tómate tu tiempo. Ninguno de ellos requiere nuevas APIs de FreeBSD; requieren una aplicación más profunda de lo que has aprendido.

Estos desafíos están pensados para afrontarse durante días o semanas, no en una sola sesión. Ejercitan el juicio tanto como la codificación: la pregunta «¿es esto seguro?» es a menudo «¿seguro contra qué modelo de amenaza?». Ser explícito sobre el modelo de amenaza es parte del ejercicio.

### Desafío 1: Añadir un ioctl multietapa

Diseña e implementa un ioctl que realice una operación multietapa sobre `secdev`: primero, el usuario sube un blob; segundo, el usuario solicita el procesamiento; tercero, el usuario descarga el resultado. Cada paso es una llamada ioctl independiente.

El desafío consiste en gestionar correctamente el estado por apertura: el blob subido en el paso 1 debe estar asociado al descriptor de archivo del llamador, no globalmente. Dos usuarios concurrentes no deben ver los blobs del otro. El estado debe limpiarse cuando se cierre el descriptor de archivo, aunque el usuario no haya llegado al paso 3.

Consideraciones de seguridad: limita el tamaño del blob, valida cada paso de la máquina de estados (no se puede solicitar el procesamiento sin un blob; no se puede descargar sin completar el procesamiento), asegúrate de que el estado parcial en los caminos de error se limpia correctamente y comprueba que un identificador visible para el usuario (si expones uno) no sea un puntero del kernel.

### Desafío 2: Escribir una descripción para syzkaller

Escribe una descripción de syscall para syzkaller de la interfaz ioctl de `secdev`. El formato está documentado en el repositorio de syzkaller. Instala syzkaller y aliméntalo con tu driver; ejecútalo durante al menos una hora (idealmente más) y observa qué encuentra.

Si encuentra bugs, corrígelos. Escribe una nota sobre qué era cada bug y cómo funciona la corrección. Si no encuentra bugs en varias horas, plantéate si tu descripción de syzkaller realmente ejercita el driver a fondo. A menudo, una descripción que no encuentra bugs no está explorando la interfaz de forma exhaustiva.

### Desafío 3: Detectar una liberación doble en tu propio código

Introduce intencionalmente un bug de liberación doble (double-free) en una copia de tu `secdev` seguro. Compila el módulo contra un kernel con `INVARIANTS` y `WITNESS`. Carga y ejercita el módulo de forma que se dispare la liberación doble. Observa qué ocurre.

Ahora vuelve a compilar el kernel con `KASAN`. Carga y ejercita el mismo módulo defectuoso. Observa la diferencia en cómo se detecta el bug.

Anota qué capturó cada sanitizador y qué legible era la salida. Este ejercicio desarrolla la intuición sobre qué sanitizador usar primero en cada situación.

### Desafío 4: Modelar las amenazas de un driver existente

Elige un driver del árbol de FreeBSD que no hayas examinado antes (algo pequeño, idealmente con menos de 2000 líneas). Léelo con atención. Escribe un modelo de amenaza: ¿quiénes son los llamadores?, ¿qué privilegios necesitan?, ¿qué podría salir mal?, ¿qué mitigaciones están en vigor?, ¿qué podría añadirse?

El objetivo no es encontrar bugs específicos. Es practicar la mentalidad de seguridad en código real. Un buen modelo de amenaza son unas pocas páginas de prosa que permitirían a otro ingeniero revisar el mismo driver de forma eficiente.

### Desafío 5: Comparar `/dev/null` y `/dev/mem`

Abre `/usr/src/sys/dev/null/null.c` y `/usr/src/sys/dev/mem/memdev.c` (o sus equivalentes por arquitectura). Léelos ambos.

Escribe un breve ensayo (una o dos páginas) sobre las diferencias de seguridad. `/dev/null` es uno de los drivers más simples de FreeBSD; ¿qué hace y por qué es seguro? `/dev/mem` es uno de los más peligrosos; ¿qué hace y cómo lo mantiene seguro FreeBSD? ¿Qué puedes aprender sobre la forma del código de driver seguro a partir de ese contraste?

## Resolución de problemas y errores comunes

Un breve catálogo de errores que he visto repetidamente en código de driver real, con el síntoma, la causa y la solución.

### «A veces funciona, a veces no»

**Síntoma.** Una prueba pasa la mayor parte del tiempo pero falla ocasionalmente. Ejecutarla bajo carga amplifica la tasa de fallos.

**Causa.** Casi siempre una condición de carrera. Algo se está leyendo y escribiendo concurrentemente sin un lock.

**Solución.** Identifica el estado compartido. Añade un lock. Adquiere el lock durante toda la secuencia de comprobación y acción. No confíes en las operaciones `atomic_*` para resolver un problema de invariante de múltiples campos.

### "El driver se bloquea al descargarse"

**Síntoma.** `kldunload` provoca un panic o deja el kernel bloqueado.

**Causa.** Un callout, una tarea de taskqueue o un kernel thread sigue en ejecución cuando `detach` libera la estructura que utiliza. O bien una operación cdev en vuelo sigue dentro del código del driver cuando se omite `destroy_dev`.

**Solución.** En `detach`, llama a `destroy_dev` antes que nada. Después aplica `callout_drain` a cada callout, `taskqueue_drain_all` a cada taskqueue y espera a que salga cada kernel thread. Solo entonces libera el estado. Estructura el detach como la secuencia inversa exacta del attach.

### "El ioctl funciona como root pero no desde mi cuenta de servicio"

**Síntoma.** El usuario informa de que root puede usar el dispositivo, pero una cuenta sin privilegios no puede.

**Causa.** Los permisos del nodo de dispositivo son demasiado restrictivos, o una llamada a `priv_check` rechaza la operación.

**Solución.** Si la operación realmente debe requerir privilegios, el comportamiento es el esperado; documéntalo. Si no es así, reconsidera: ¿se añadió la comprobación de privilegio por error? ¿Es demasiado restrictivo el modo del nodo de dispositivo? La respuesta correcta depende de la operación; en la mayoría de los casos reales la respuesta es "sí, debe requerir privilegios, actualiza la documentación".

### "dmesg se inunda de mensajes"

**Síntoma.** `dmesg` muestra miles de mensajes idénticos del driver. Los mensajes legítimos quedan desplazados.

**Causa.** Una instrucción de log en una ruta alcanzable desde el espacio de usuario, sin limitación de tasa.

**Solución.** Envuelve el log con `ppsratecheck` o `eventratecheck`. Limita a unos pocos mensajes por segundo. Si el mensaje indica un error, incluye un recuento de mensajes suprimidos cuando la tasa vuelva a la normalidad (las funciones auxiliares de tasa lo permiten).

### "La estructura vuelve con bytes basura"

**Síntoma.** Una herramienta del espacio de usuario informa de que ve datos aparentemente aleatorios en un campo que no esperaba encontrar inicializado.

**Causa.** El driver no puso a cero la estructura antes de rellenarla y copiarla hacia fuera. Los datos "aleatorios" son en realidad contenido previo de la pila o del heap.

**Solución.** Añade un `bzero` al principio de la función, o inicializa la estructura con `= { 0 }` en la declaración. Nunca hagas `copyout` de una estructura no inicializada.

### "Tenemos una fuga de memoria pero no veo dónde"

**Síntoma.** `vmstat -m` muestra que el tipo malloc del driver crece con el tiempo. Al final el sistema se queda sin memoria.

**Causa.** Una ruta de asignación que no tiene su correspondiente ruta de liberación, o una ruta de error que retorna sin liberar.

**Solución.** Utiliza un tipo malloc con nombre (`MALLOC_DEFINE`). Audita cada asignación. Recorre cada ruta de error. Considera el patrón de etiqueta única de limpieza. Compila con `INVARIANTS` y vigila las advertencias del asignador al descargar.

### "kldload tiene éxito pero mi dispositivo no aparece en /dev"

**Síntoma.** `kldstat` muestra el módulo cargado, pero no hay ninguna entrada `/dev/secdev`.

**Causa.** Normalmente un error en la secuencia de `attach` antes de que se llame a `make_dev_s`, o bien `make_dev_s` ha fallado en silencio.

**Solución.** Comprueba el valor de retorno de `make_dev_s`. Añade un `device_printf` que informe de cualquier error. Verifica que se alcanza `attach` añadiendo un `device_printf` al principio.

### "Una prueba C sencilla pasa, pero un script de shell que hace lo mismo en un bucle falla"

**Síntoma.** Las pruebas de un solo disparo funcionan. Las pruebas repetidas rápidas fallan.

**Causa.** Probablemente una condición de carrera entre operaciones repetidas, o un recurso que no se limpia entre llamadas. En ocasiones un bug de tipo TOCTOU sensible a la temporización.

**Solución.** Aplica más carga en las pruebas de estrés. Usa `dtrace` o `ktrace` para ver qué ocurre. Busca estado que persiste entre llamadas y no debería hacerlo.

### "KASAN indica use-after-free pero mis malloc/free están equilibrados"

**Síntoma.** `KASAN` informa de acceso a memoria liberada, pero la inspección visual del driver muestra que cada asignación se libera exactamente una vez.

**Causa.** Un caso sutil frecuente: un callout o una tarea de taskqueue todavía mantiene un puntero al objeto liberado. El callout se dispara después de la liberación.

**Solución.** Traza el ciclo de vida del callout. Asegúrate de que `callout_drain` (o equivalente) se ejecuta antes de cualquier liberación. Un caso relacionado es un callback de finalización asíncrona; asegúrate de que la operación se completa o se cancela antes de que se libere la estructura propietaria.

### "WITNESS se queja del orden de los locks"

**Síntoma.** `WITNESS` informa de "lock order reversal" y señala dos locks que se adquirieron en orden inconsistente.

**Causa.** En un punto el código adquirió el lock A y luego el lock B; en otro punto adquirió el lock B y luego el lock A. Esto puede producir un deadlock.

**Solución.** Establece un orden canónico para tus locks. Documéntalo. Adquiérelos en ese orden en todos los casos. Si una ruta de código necesita legítimamente el orden inverso, utiliza `mtx_trylock` con un patrón de espera y reintento.

### "vmstat -m muestra un recuento de liberaciones negativo"

**Síntoma.** `vmstat -m` muestra el tipo malloc del driver con un número negativo de asignaciones, o con un recuento de uso que aumenta sin límite con el tiempo.

**Causa.** Un tipo `malloc`/`free` que no coincide, o una fuga en la que las asignaciones se producen sin sus correspondientes liberaciones.

**Solución.** Un recuento de liberaciones negativo casi siempre significa que una llamada a `free` pasó la etiqueta de tipo incorrecta. Audita cada `free(ptr, M_TYPE)` y confirma que el tipo coincide con el `malloc`. Un recuento de uso en continuo aumento es una fuga; audita cada ruta que asigna y confirma que tiene una liberación correspondiente en cada salida.

### "El driver funciona en amd64 pero provoca un panic en arm64"

**Síntoma.** Las pruebas funcionales en amd64 pasan; el mismo driver provoca un panic en arm64.

**Causa.** Con frecuencia una discrepancia en el relleno o la alineación de estructuras. arm64 tiene reglas de relleno diferentes a las de amd64 para ciertas estructuras. Un acceso alineado en amd64 puede estar desalineado en arm64 y provocar un panic.

**Solución.** Usa `__packed` con cuidado (cambia la alineación), usa `__aligned(N)` donde la alineación importe, y evita asumir que el tamaño o la distribución de una estructura coincide entre arquitecturas. Para los campos que cruzan la frontera usuario/kernel, usa anchos explícitos (`uint32_t` en lugar de `int`, `uint64_t` en lugar de `long`).

### "El driver compila sin errores pero dmesg muestra advertencias del build del kernel"

**Síntoma.** El módulo se construye, pero al cargarlo aparecen advertencias sobre símbolos sin resolver o incompatibilidades de ABI.

**Causa.** El módulo se construyó contra un kernel diferente al que se está cargando. El ABI del kernel no está garantizado como estable entre versiones, por lo que un módulo construido contra 14.2 puede no cargarse correctamente en 14.3.

**Solución.** Reconstruye el módulo contra el árbol de código fuente del kernel en ejecución. `uname -r` muestra la versión del kernel en ejecución; verifica que `/usr/src` coincide. Si no coinciden, instala el código fuente correspondiente (mediante `freebsd-update`, `svn` o `git`, según tu distribución de fuentes).

### "El driver es intermitentemente más lento de lo esperado"

**Síntoma.** Los benchmarks muestran picos de latencia ocasionales y grandes incluso bajo carga moderada.

**Causa.** Con frecuencia un problema de contención de lock: múltiples threads se encolan en un único mutex. En ocasiones una parada del asignador: `malloc(M_WAITOK)` en una ruta caliente espera a que haya memoria disponible.

**Solución.** Usa `dtrace` para perfilar la contención de locks (proveedor `lockstat`) e identificar qué lock está caliente. Reestructura para reducir la sección crítica, divide el lock o usa un enfoque sin locks. Para las paradas del asignador, preasigna o usa una zona UMA con una marca de agua alta.

## Lista de verificación de seguridad para la revisión del código del driver

Esta sección es una lista de verificación de referencia que puedes tener a mano mientras revisas un driver, el tuyo o el de otra persona. No es exhaustiva, pero si se ha considerado conscientemente cada elemento de la lista, el driver estará en mucho mejor estado que la media.

### Comprobaciones estructurales

Las rutas de carga y descarga del módulo del driver son simétricas. Cada recurso asignado en la carga se libera en la descarga, y el orden de liberación es el inverso del orden de asignación.

El driver usa `make_dev_s` o `make_dev_credf` (no el antiguo `make_dev` solo) para que los errores durante la creación del nodo de dispositivo se notifiquen y se gestionen.

El nodo de dispositivo se crea con permisos conservadores. El modo `0600` o `0640` es el predeterminado; cualquier permiso más permisivo tiene una razón explícita registrada en comentarios o mensajes de commit.

El driver declara un `malloc_type` con nombre mediante `MALLOC_DECLARE` y `MALLOC_DEFINE`. Todas las asignaciones usan este tipo.

Cada lock del driver tiene un comentario junto a su declaración que indica qué protege. El comentario es preciso.

### Comprobaciones de entrada y límites

Cada llamada a `copyin` va acompañada de un argumento de tamaño que no puede superar el tamaño del buffer de destino.

Cada llamada a `copyout` usa una longitud que es el mínimo entre el tamaño del buffer del llamador y el tamaño de la fuente del kernel.

`copyinstr` se usa para cadenas que deben terminarse en NUL. Se comprueba el valor de retorno (incluido `done`).

Cada estructura de argumento de ioctl se copia al espacio del kernel antes de leer cualquiera de sus campos.

Las llamadas a `uiomove` pasan una longitud limitada al tamaño del buffer del que se lee o al que se escribe, no solo `uio->uio_resid`.

Cada campo de longitud proporcionado por el usuario se valida: distinto de cero cuando se requiere, acotado por debajo del máximo apropiado, comprobado frente al espacio de buffer restante.

### Gestión de memoria

Cada llamada a `malloc` comprueba el valor de retorno si se usa `M_NOWAIT`. `M_WAITOK` sin `M_NULLOK` nunca se comprueba inútilmente para NULL; el código confía en la garantía del asignador.

Cada `malloc` tiene exactamente un `free` en cada ruta de código. Las rutas de éxito y las rutas de error se auditan ambas.

Los datos sensibles (claves, contraseñas, credenciales, secretos propietarios) se ponen a cero con `explicit_bzero` o `zfree` antes de liberar la memoria.

Las estructuras que se copiarán al espacio de usuario se ponen a cero antes de rellenarse.

Los buffers asignados para la salida al usuario usan `M_ZERO` en el momento de la asignación para evitar fugas de datos obsoletos a través del extremo final.

Tras liberar un puntero, se establece a NULL o el ámbito termina inmediatamente.

### Privilegios y control de acceso

Las operaciones que requieren privilegio administrativo llaman a `priv_check(td, PRIV_DRIVER)` o a una constante `PRIV_*` más específica.

Las operaciones que no deben permitirse dentro de un jail comprueban explícitamente `jailed(td->td_ucred)` y devuelven `EPERM` si hay jail.

Las operaciones que dependen del securelevel del sistema llaman a `securelevel_gt` o `securelevel_ge` y gestionan el valor de retorno correctamente (nota la semántica invertida: un valor distinto de cero significa rechazar).

Ninguna operación usa `cr_uid == 0` como barrera de privilegio. Se usa `priv_check` en su lugar.

Los sysctls que exponen datos sensibles usan `CTLFLAG_SECURE` o se restringen a usuarios con privilegios mediante comprobaciones de permisos.

### Concurrencia

Cada campo del softc al que accede más de un contexto está protegido por un lock.

La secuencia completa de comprobación y actuación (incluidas las búsquedas que determinan si una operación es legal) se realiza bajo el lock apropiado.

Ninguna llamada a `copyin`, `copyout` o `uiomove` se realiza mientras se mantiene un mutex. Si se necesita I/O al espacio de usuario, el código suelta el lock, realiza el I/O y lo readquiere comprobando los invariantes.

`detach` llama primero a `destroy_dev` (o equivalente), luego vacía los callouts, taskqueues e interrupciones, y por último libera el estado.

Los callouts, taskqueues y kernel threads se rastrean para que todos puedan vaciarse durante la descarga.

### Higiene de la información

Ningún puntero del kernel (`%p` o equivalente) se devuelve al espacio de usuario a través de un ioctl, sysctl o mensaje de log en una ruta activable por el usuario.

Ningún mensaje de log activable por el usuario está sin límite; `ppsratecheck` o similar envuelve cada uno de esos mensajes.

Los logs no incluyen datos proporcionados por el usuario que puedan contener caracteres de control o información sensible.

El logging de depuración está envuelto en un condicional (sysctl o en tiempo de compilación) para que los builds de producción no lo emitan por defecto.

### Modos de fallo

Cada sentencia switch tiene una rama `default:` que devuelve un error razonable.

Cada analizador o validador incluye en lista blanca lo que está permitido, en lugar de incluir en lista negra lo que no lo está.

Cada operación con uso de recursos tiene un límite máximo. El límite está documentado.

Cada sleep tiene un tiempo de espera finito, a menos que una razón genuina requiera una espera sin límite (e incluso en ese caso, se usa `PCATCH` para permitir señales).

Cada ruta de error libera los recursos que su ruta de éxito habría conservado.

La respuesta del driver ante una entrada inesperada es rechazar la operación, no intentar adivinar.

### Pruebas

El driver se ha cargado y probado contra un kernel compilado con `INVARIANTS` y `WITNESS`. No se dispara ninguna aserción y no se notifica ninguna violación del orden de locks.

El driver se ha probado bajo carga concurrente (múltiples procesos, múltiples descriptores de archivo abiertos, operaciones intercaladas).

El driver se ha probado bajo concurrencia en el momento del detach (un usuario se encuentra dentro del driver mientras se intenta la descarga).

Se ha ejecutado alguna forma de fuzzing contra el driver (idealmente syzkaller, como mínimo una prueba de shell aleatoria).

El driver ha sido revisado por alguien distinto de su autor. La revisión fue específicamente para consideraciones de seguridad, no solo de funcionalidad.

### Evolución

La postura de seguridad del driver se reexamina a intervalos regulares. Los nuevos avisos del compilador y los nuevos hallazgos de los sanitizadores se evalúan con rigor. Se consideran los nuevos códigos de privilegio de FreeBSD. Las interfaces no utilizadas se eliminan.

Los informes de bugs sobre el driver se tratan como posiblemente explotables hasta que se demuestre lo contrario.

El historial de commits muestra que los cambios relevantes para la seguridad reciben mensajes de commit cuidadosos que explican qué estaba mal y qué hace la corrección.

## Una mirada más profunda a los patrones de vulnerabilidad del mundo real

Los principios de este capítulo son abstracciones sobre bugs reales que ocurrieron en kernels reales. Esta sección estudia algunos patrones que han aparecido a lo largo de los años en FreeBSD, Linux y otros sistemas operativos de código abierto. El objetivo no es catalogar CVEs (para eso existen bases de datos enteras) sino entrenar el reconocimiento de patrones.

### La copia incompleta

Un patrón clásico: un driver recibe un buffer de usuario de longitud variable. Copia una cabecera fija, extrae un campo de longitud de la cabecera y, a continuación, copia la porción variable según esa longitud.

```c
error = copyin(uaddr, &hdr, sizeof(hdr));
if (error != 0)
    return (error);

if (hdr.body_len > MAX_BODY)
    return (EINVAL);

error = copyin(uaddr + sizeof(hdr), body, hdr.body_len);
```

El bug es que la comprobación de longitud compara `body_len` con `MAX_BODY`, pero `body` puede ser un buffer de tamaño fijo con un tamaño diferente. Si `MAX_BODY` se define de forma descuidada, o si en su momento era el tamaño de `body` pero `body` ha encogido desde entonces, la copia desborda `body`.

Cada vez que veas un patrón del tipo "valida la cabecera y luego copia el cuerpo basándote en ella", comprueba que el límite de longitud coincide realmente con el tamaño del buffer de destino. Usa `sizeof(body)` directamente si puedes, en lugar de una macro que podría desviarse.

### La confusión de signo

Una longitud se almacena como `int` pero debería ser no negativa. Un llamador pasa `-1`. Tu código:

```c
if (len > MAX_LEN)
    return (EINVAL);

buf = malloc(len, M_FOO, M_WAITOK);
copyin(uaddr, buf, len);
```

¿Pasa la primera comprobación? Sí, porque `-1` es menor que `MAX_LEN` cuando se compara como `int` con signo. ¿Qué ocurre en `malloc(len, ...)` con `len = -1`? En muchas plataformas, `-1` se convierte silenciosamente en un `size_t` positivo muy grande. La asignación falla (o, peor aún, tiene éxito con un tamaño enorme), o `copyin` intenta copiar un buffer enorme.

La solución es usar tipos sin signo para los tamaños (preferiblemente `size_t`), o comprobar los valores negativos explícitamente:

```c
if (len < 0 || len > MAX_LEN)
    return (EINVAL);
```

O, mejor aún, cambiar el tipo para que los valores negativos no puedan existir:

```c
size_t len = arg->len;     /* copied from user, already size_t */
if (len > MAX_LEN)
    return (EINVAL);
```

La confusión de signo es una de las causas raíz más comunes de los desbordamientos de buffer en el código del kernel. Usa `size_t` para los tamaños. Usa `ssize_t` solo cuando los valores negativos tengan sentido. Nunca mezcles tipos con signo y sin signo en una comprobación de tamaño.

### La validación incompleta

Un driver acepta una estructura compleja con muchos campos. La función de validación comprueba algunos campos pero olvida otros:

```c
if (args->type > TYPE_MAX)
    return (EINVAL);
if (args->count > COUNT_MAX)
    return (EINVAL);
/* forgot to validate args->offset */

use(args->offset);  /* attacker-controlled */
```

El bug es que `args->offset` se usa como índice en un array sin que se comprueben sus límites. Un atacante suministra un desplazamiento enorme y lee o escribe en la memoria del kernel.

La solución es tratar la validación como una lista de verificación. Por cada campo de la estructura de entrada, pregúntate: ¿qué valores son válidos? Aplícalos todos. Una función auxiliar `is_valid_arg` que centralice la validación y que se llame de forma temprana es mejor que las comprobaciones dispersas.

### La comprobación omitida en el camino de error

Un driver valida cuidadosamente la entrada en el camino de éxito, pero el camino de error limpia los recursos en función de un campo que nunca se validó:

```c
if (args->count > COUNT_MAX)
    return (EINVAL);
buf = malloc(args->count * sizeof(*buf), M_FOO, M_WAITOK);
error = copyin(args->data, buf, args->count * sizeof(*buf));
if (error != 0) {
    /* error cleanup */
    if (args->free_flag)          /* untrusted field */
        some_free(args->ptr);     /* attacker-controlled */
    free(buf, M_FOO);
    return (error);
}
```

El camino de error usa `args->free_flag` y `args->ptr`, ninguno de los cuales fue validado. Si el atacante consigue que `copyin` falle (por ejemplo, desmapeando la memoria), el camino de error libera un puntero controlado por el atacante, corrompiendo el heap del kernel.

La lección: la validación debe cubrir todos los campos que cualquier camino de código lee. Es tentador pensar "el camino de error es inusual; no hay problema". Los atacantes apuntan específicamente a los caminos de error precisamente porque están menos probados.

### La doble búsqueda

Un driver busca un objeto en una tabla por nombre o ID y, a continuación, realiza una operación. Entre la búsqueda y la operación, otro thread elimina el objeto. La operación actúa entonces sobre memoria liberada.

```c
obj = lookup(id);
if (obj == NULL)
    return (ENOENT);
do_operation(obj);   /* obj may have been freed in between */
```

La solución es tomar una referencia sobre el objeto (usando un refcount) dentro de la búsqueda, mantener la referencia durante toda la operación y liberarla al final. La función de búsqueda toma el lock, incrementa el refcount y libera el lock. La operación trabaja entonces con un puntero cuyo refcount está activo y que no puede ser liberado por debajo. La liberación decrementa el refcount; cuando cae a cero, el último poseedor libera el objeto.

Los contadores de referencias son la respuesta canónica de FreeBSD al problema de la doble búsqueda. Consulta `/usr/src/sys/sys/refcount.h`.

### El buffer que creció

En su momento, un buffer era de 256 bytes. Se definió una constante `BUF_SIZE = 256`. El código comprobaba `len <= BUF_SIZE` y copiaba `len` bytes en el buffer. Más adelante, alguien aumentó el buffer a 1024 bytes pero olvidó actualizar la constante. O bien se actualizó la constante, pero un `sizeof(buf)` en una de las llamadas no se actualizó porque no usaba la constante.

Esta clase de bug se previene usando siempre `sizeof` directamente sobre el buffer de destino, en lugar de una constante que puede desviarse:

```c
char buf[BUF_SIZE];
if (len > sizeof(buf))     /* always matches the actual buf size */
    return (EINVAL);
```

Las constantes son útiles cuando varios lugares necesitan el mismo límite. Si usas una constante, mantén la definición y el array adyacentes en el código fuente y considera añadir un `_Static_assert(sizeof(buf) == BUF_SIZE, ...)` para detectar desviaciones.

### El puntero no verificado de una estructura

Un driver recibe una estructura del espacio de usuario que contiene punteros. El driver usa los punteros directamente:

```c
error = copyin(uaddr, &cmd, sizeof(cmd));
/* cmd.data_ptr is user-space pointer */
use(cmd.data_ptr);   /* treating user pointer as kernel pointer */
```

Se trata de un bug catastrófico: el puntero es una dirección del espacio de usuario, pero el código lo desreferencia como si fuera memoria del kernel. En algunas arquitecturas, esto puede acceder a cualquier memoria que resulte encontrarse en esa dirección en el espacio del kernel, que normalmente es basura o memoria inválida. En otras, genera un fallo. En algunos casos patológicos específicos, accede a datos sensibles del kernel.

La solución: nunca desreferenciar un puntero obtenido del espacio de usuario. Los punteros en estructuras suministradas por el usuario deben pasarse a `copyin` o `copyout`, que traducen correctamente las direcciones de usuario. Nunca los trates como direcciones del kernel.

### El copyout olvidado

Un driver lee una estructura del espacio de usuario, la modifica, pero olvida copiar la versión modificada de vuelta:

```c
error = copyin(uaddr, &cmd, sizeof(cmd));
if (error != 0)
    return (error);

cmd.status = STATUS_OK;
/* forgot to copyout */
return (0);
```

Se trata de un bug funcional, no estrictamente de seguridad, pero su imagen especular es: olvidar `copyin` y asumir que un campo ya estaba establecido. "Establezco `cmd.status` en `copyin` y lo leo después" es incorrecto si el campo fue establecido en realidad por el espacio de usuario; el valor del usuario es lo que el código lee.

Cada estructura que fluye del usuario al kernel y de vuelta necesita una convención clara sobre cuándo ocurren `copyin` y `copyout`, y qué campos son autoritativos en qué dirección. Documéntalo y síguelo.

### La condición de carrera accidental

Un driver toma un lock, lee un campo, libera el lock y luego usa el valor:

```c
mtx_lock(&sc->sc_mtx);
val = sc->sc_val;
mtx_unlock(&sc->sc_mtx);

/* ... some unrelated work ... */

mtx_lock(&sc->sc_mtx);
if (val == sc->sc_val) {
    /* act on val */
}
mtx_unlock(&sc->sc_mtx);
```

El driver asume que `val` sigue siendo válido porque lo vuelve a comprobar. Pero "actuar sobre val" usa la copia obsoleta, no el campo actual. Si `sc_val` es un puntero, la acción puede operar sobre un objeto liberado. Si `sc_val` es un índice, la acción puede usar un índice obsoleto.

La lección: una vez que liberas un lock, cualquier valor que hayas leído bajo ese lock es obsoleto. Si necesitas actuar de nuevo bajo el lock, vuelve a leer el estado dentro de la nueva adquisición. La comprobación `if (val == sc->sc_val)` protege contra cambios; la acción necesita usar el valor actual, no el almacenado.

### El truncamiento silencioso

Un driver recibe una cadena de hasta 256 bytes y la almacena en un buffer de 128 bytes. El código usa `strncpy`:

```c
strncpy(sc->sc_name, user_name, sizeof(sc->sc_name));
```

`strncpy` se detiene al llegar al tamaño del destino. Pero `strncpy` no garantiza un terminador NUL si la fuente era más larga. El código posterior hace:

```c
printf("name: %s\n", sc->sc_name);
```

`printf("%s", ...)` lee hasta encontrar un NUL. Si `sc_name` no tiene terminador NUL, printf lee más allá del final del array hacia la memoria adyacente, filtrando potencialmente esa memoria en el log o provocando un crash.

Las alternativas más seguras son `strlcpy` (garantiza la terminación NUL y trunca si es necesario) y `snprintf` (la misma garantía con formato). `strncpy` es una mina; está en la biblioteca estándar solo por razones históricas.

### El evento registrado en exceso

Un driver registra un mensaje cada vez que se dispara un evento. El evento puede ser activado por el usuario. Un usuario envía un millón de eventos en un bucle. El buffer de mensajes del kernel se llena y desborda; los mensajes legítimos se pierden. El usuario ha conseguido una denegación de servicio sobre el propio subsistema de logging.

La solución, como se comentó en la sección 8, es la limitación de tasa (rate limiting). Cada mensaje de log que pueda ser activado por el usuario debe ir envuelto en una comprobación de límite de tasa. Se puede emitir periódicamente un resumen del recuento suprimido ("[secdev] 1234 suppressed messages in last 5 seconds") para informar al operador sobre un posible flooding en curso.

### El bug invisible

Un driver funciona bien durante años. Luego, una actualización del compilador cambia la forma en que gestiona determinada construcción de código, o una API del kernel cambia su semántica en una nueva versión de FreeBSD, y el comportamiento del driver cambia. Una comprobación que funcionaba correctamente deja de funcionar en silencio. Los usuarios no se dan cuenta hasta que aparece un exploit.

Los bugs invisibles son el argumento más sólido a favor de `KASSERT`, los sanitizadores y los tests. Un `KASSERT(p != NULL)` al comienzo de cada función documenta lo que esa función espera. Un kernel con `INVARIANTS` detecta el momento en que un invariante se rompe. Una buena suite de tests nota cuándo cambia el comportamiento.

Cuanto más simple sea la función y más claro su contrato, menos lugares habrá donde los bugs invisibles puedan esconderse. Esta es una de las razones por las que el estilo de código del kernel de FreeBSD descrito en `style(9)` valora las funciones cortas con responsabilidades bien definidas: son más fáciles de razonar, lo que facilita evitar los bugs invisibles desde el principio.

### Cerrando el catálogo de patrones

Cada uno de los patrones anteriores se ha observado en código de kernel real. Muchos han dado lugar a CVEs. Las defensas son:

- Usa `size_t` para los tamaños; evita la confusión de signo.
- Validación por lista blanca; no olvides ningún campo.
- Trata los caminos de error con el mismo rigor que los caminos de éxito.
- Usa refcounts para gestionar el ciclo de vida de los objetos bajo concurrencia.
- Usa `sizeof` directamente sobre el buffer en lugar de una constante propensa a desviarse.
- No desreferencies punteros de usuario.
- Mantén la gestión de `copyin` / `copyout` explícita por campo.
- Recuerda que un valor leído bajo un lock es obsoleto una vez que el lock se libera.
- Usa `strlcpy` o `snprintf`, nunca `strncpy`.
- Limita la tasa de todos los logs que puedan activarse desde el usuario.
- Escribe los invariantes como `KASSERT` para que las regresiones se detecten.

Memoriza estos patrones. Aplícalos como una lista de verificación mental en cada función que escribas o revises.

## Apéndice: cabeceras y APIs utilizadas en este capítulo

Una referencia breve a las cabeceras de FreeBSD mencionadas a lo largo de este capítulo, agrupadas por tema. Cada cabecera se encuentra en `/usr/src/sys/` seguida de la ruta indicada.

### Memoria y operaciones de copia

- `sys/systm.h`: declaraciones de `copyin`, `copyout`, `copyinstr`, `bzero`, `explicit_bzero`, `printf`, `log` y muchos otros primitivos del núcleo del kernel.
- `sys/malloc.h`: `malloc(9)`, `free(9)`, `zfree(9)`, `MALLOC_DECLARE`, `MALLOC_DEFINE`, flags M_*.
- `sys/uio.h`: `struct uio`, `uiomove(9)`, constantes UIO_READ / UIO_WRITE.
- `vm/uma.h`: asignador de zonas UMA (`uma_zcreate`, `uma_zalloc`, `uma_zfree`, `uma_zdestroy`).
- `sys/refcount.h`: primitivos de conteo de referencias (`refcount_init`, `refcount_acquire`, `refcount_release`).

### Privilegios y control de acceso

- `sys/priv.h`: `priv_check(9)`, `priv_check_cred(9)`, constantes `PRIV_*`, `securelevel_gt`, `securelevel_ge`.
- `sys/ucred.h`: `struct ucred` y sus campos.
- `sys/jail.h`: `struct prison`, macro `jailed(9)`, funciones auxiliares relacionadas con la jail.
- `sys/capsicum.h`: capacidades de Capsicum, `cap_rights_t`, `IN_CAPABILITY_MODE(td)`.
- `security/mac/mac_framework.h`: hooks del framework MAC (principalmente para escritores de políticas de seguridad, pero útil como referencia).

### Locks y concurrencia

- `sys/mutex.h`: `struct mtx`, `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`.
- `sys/sx.h`: locks compartidos/exclusivos.
- `sys/rwlock.h`: locks de lectura/escritura.
- `sys/condvar.h`: variables de condición (`cv_init`, `cv_wait`, `cv_signal`).
- `sys/lock.h`: infraestructura común de locks.
- `sys/atomic_common.h`: operaciones atómicas (y cabeceras específicas de arquitectura).

### Archivos de dispositivo e infraestructura de dev

- `sys/conf.h`: `struct cdev`, `struct cdevsw`, `struct make_dev_args`, `make_dev_s`, `make_dev_credf`, `destroy_dev`.
- `sys/module.h`: `DRIVER_MODULE`, `MODULE_VERSION`, declaraciones de módulos del kernel.
- `sys/kernel.h`: SYSINIT, SYSUNINIT y macros relacionadas de hooks del kernel.
- `sys/bus.h`: `device_t`, métodos de dispositivo, `bus_alloc_resource`, `bus_teardown_intr`.

### Temporización, limitación de tasa y callouts

- `sys/time.h`: `eventratecheck(9)`, `ppsratecheck(9)`, `struct timeval`.
- `sys/callout.h`: `struct callout`, `callout_init_mtx`, `callout_reset`, `callout_drain`.
- `sys/taskqueue.h`: primitivas de cola de tareas (`taskqueue_create`, `taskqueue_enqueue`, `taskqueue_drain`).

### Registro y diagnóstico

- `sys/syslog.h`: constantes de prioridad `LOG_*` para `log(9)`.
- `sys/kassert.h`: `KASSERT`, `MPASS`, macros de aserción.
- `sys/ktr.h`: macros de trazado KTR.
- `sys/sdt.h`: sondas de Statically Defined Tracing para dtrace(1).

### Sysctls

- `sys/sysctl.h`: macros `SYSCTL_*`, flags `CTLFLAG_*` incluyendo `CTLFLAG_SECURE`, `CTLFLAG_PRISON`, `CTLFLAG_CAPRD`, `CTLFLAG_CAPWR`.

### Red (cuando aplique)

- `sys/mbuf.h`: `struct mbuf`, asignación y manipulación de mbufs.
- `net/if.h`: `struct ifnet`, primitivas de interfaz de red.

### Epoch y sin bloqueo

- `sys/epoch.h`: primitivas de reclamación basada en epoch (`epoch_enter`, `epoch_exit`, `epoch_wait`).
- `sys/atomic_common.h` y cabeceras atómicas específicas de arquitectura: barreras de memoria, lecturas y escrituras atómicas.

### Trazado y observabilidad

- `security/audit/audit.h`: framework de auditoría del kernel (cuando está compilado).
- `sys/sdt.h`: Statically Defined Tracing para integración con dtrace.
- `sys/ktr.h`: trazado en el kernel con KTR.

Este apéndice no es exhaustivo; el conjunto completo de cabeceras que un driver puede necesitar es mucho mayor. Cubre las que se mencionan en este capítulo. Cuando escribas tu propio driver, usa `grep` en `/usr/src/sys/sys/` para encontrar la primitiva que necesitas y lee la cabecera para entender qué está disponible. Muchas de estas cabeceras están bien comentadas y recompensan una lectura atenta.

Leer las cabeceras es en sí mismo una práctica de seguridad. Cada primitiva tiene un contrato: qué argumentos acepta, qué restricciones impone, qué garantiza en caso de éxito, qué devuelve en caso de fallo. Un driver que usa una primitiva sin leer su contrato confía en suposiciones que quizá no se cumplan. Un driver que lee el contrato y se atiene a él es un driver que se beneficia de la propia disciplina del kernel.

Muchas de las cabeceras listadas arriba merecen estudiarse como ejemplos de buen diseño del kernel. `sys/refcount.h` es pequeña, está cuidadosamente comentada y muestra cómo se construye una primitiva simple a partir de operaciones atómicas. `sys/kassert.h` muestra cómo se usa la compilación condicional para construir una funcionalidad que no cuesta nada en producción pero que detecta errores en kernels de desarrollo. `sys/priv.h` muestra cómo una larga lista de constantes con nombre puede organizarse por subsistema y usarse como gramática de una política. Cuando te quedes sin ideas sobre cómo estructurar los internos de tu driver, estas cabeceras son un buen lugar donde buscar inspiración.

## Apéndice: lecturas adicionales

Una breve lista de recursos que profundizan en la seguridad de FreeBSD más allá de lo que este capítulo puede cubrir:

**FreeBSD Architecture Handbook**, en particular los capítulos sobre el subsistema jail, Capsicum y el framework MAC. Disponible en línea en `https://docs.freebsd.org/en/books/arch-handbook/`.

**Capítulo de seguridad del FreeBSD Handbook**, orientado a administradores pero con contexto útil sobre cómo las funcionalidades de nivel de sistema (jails, securelevel, MAC) interactúan entre sí.

**Capsicum: Practical Capabilities for UNIX**, el artículo original de Robert Watson, Jonathan Anderson, Ben Laurie y Kris Kennaway. Explica el razonamiento de diseño detrás de Capsicum, lo que ayuda a decidir cómo debe comportarse tu driver en modo de capacidades.

**"The Design and Implementation of the FreeBSD Operating System"**, de Marshall Kirk McKusick, George V. Neville-Neil y Robert N. M. Watson. La segunda edición cubre FreeBSD 11; muchos capítulos relevantes para la seguridad siguen siendo aplicables en versiones posteriores.

**`style(9)`**, la guía de estilo de código del kernel de FreeBSD, disponible como página de manual: `man 9 style`. El código del kernel legible es código más seguro; las convenciones de `style(9)` son parte de cómo el árbol se mantiene revisable a escala.

**Documentación de KASAN, KMSAN y KCOV** en `/usr/src/share/man/` y secciones relacionadas. Leerla te ayuda a configurar e interpretar la salida de los sanitizadores.

**Documentación de syzkaller**, en `https://github.com/google/syzkaller`. El directorio `sys/freebsd/` contiene descripciones de syscalls que ilustran cómo describir la interfaz de tu propio driver.

**Bases de datos de CVE** como `https://nvd.nist.gov/vuln/search` o `https://cve.mitre.org/`. Buscar "FreeBSD" o nombres de drivers específicos muestra errores reales que se han encontrado y corregido. Leer unos pocos informes CVE al mes enseña mucho sobre qué tipos de errores se producen en la práctica.

**Avisos de seguridad de FreeBSD**, en `https://www.freebsd.org/security/advisories/`. Son informes oficiales sobre vulnerabilidades corregidas. Muchos afectan al kernel y son relevantes para los autores de drivers.

**El árbol de código fuente de FreeBSD** es la referencia más extensa y más autoritativa. Dedica tiempo a leer drivers similares al tuyo. Observa cómo validan la entrada, comprueban privilegios, gestionan el bloqueo y manejan el detach. Imitar los patrones que ves en código bien revisado es una de las formas más rápidas de aprender.

**Listas de correo de seguridad**, como `freebsd-security@` y la lista más amplia `oss-security`, tienen actividad diaria sobre problemas del kernel y de drivers en proyectos de código abierto. Suscribirse en modo pasivo y hojear algunos mensajes a la semana ayuda a tomar conciencia de las tendencias de amenazas sin exigir mucho esfuerzo.

**Literatura sobre verificación formal**, aunque especializada, ha empezado a tocar el código del kernel. Proyectos como seL4 demuestran cómo es un microkernel completamente verificado. FreeBSD no lo es, pero leer sobre verificación formal moldea la manera en que piensas sobre invariantes y contratos en tu propio código.

**Libros sobre prácticas de codificación segura en C**, como `Secure Coding in C and C++` de Robert Seacord, se aplican bien al trabajo en el kernel, dado que el C del kernel es un dialecto del mismo lenguaje con los mismos problemas, más algunos adicionales. Capítulo a capítulo, proporcionan el catálogo mental de errores que este capítulo solo pudo esbozar.

**Libros específicos de FreeBSD**, en particular el de McKusick, Neville-Neil y Watson mencionado antes, pero también volúmenes más antiguos que cubren la evolución de subsistemas específicos. Leer sobre cómo evolucionaron las jails, cómo se diseñó Capsicum o cómo llegó a existir MAC ayuda a entender el razonamiento detrás de las primitivas en lugar de solo su mecánica.

**Charlas de conferencias** de BSDCan, EuroBSDCon y AsiaBSDCon tratan a menudo temas de seguridad. Los archivos de vídeo te permiten ver años de charlas pasadas a tu propio ritmo. Muchas son impartidas por desarrolladores activos de FreeBSD y reflejan el pensamiento actual.

**Artículos académicos sobre seguridad en sistemas operativos** de foros como USENIX Security, IEEE S&P y CCS ofrecen una visión a más largo plazo. No todos los artículos son relevantes para drivers, pero los que sí lo son profundizan tu comprensión de los modelos de amenaza, las capacidades de los atacantes y la base teórica de las mitigaciones.

**El feed de CVE**, especialmente cuando se filtra por problemas del kernel, es un goteo continuo de ejemplos del mundo real. Leer unos pocos a la semana desarrolla la intuición sobre qué aspecto tienen los errores en la práctica y qué clases reaparecen con más frecuencia.

**Tu propio código, seis meses después**. Releer tu trabajo anterior con la perspectiva que da la distancia es una herramienta de aprendizaje muy valiosa. Los errores que notarás son los que has aprendido a ver desde que lo escribiste. Adquiere este hábito; reserva tiempo para ello.

Los recursos anteriores, incluso un pequeño subconjunto de ellos, te mantendrán en crecimiento durante años. La seguridad es un campo de aprendizaje continuo. Este capítulo es un paso en ese aprendizaje; el siguiente paso es tuyo.

Todo autor de drivers con mentalidad de seguridad debería haber leído al menos unos cuantos de estos. El campo avanza, y mantenerse al día es parte del oficio.

## Cerrando

La seguridad en los drivers de dispositivo no es una técnica única. Es una forma de trabajar. Cada línea de código lleva un poco de responsabilidad sobre la seguridad del kernel. El capítulo ha cubierto los pilares principales:

**El kernel confía plenamente en cada driver.** Una vez que el código se ejecuta en el kernel, no hay sandbox, no hay aislamiento, no hay segunda oportunidad. La disciplina del autor del driver es la última línea de defensa del sistema.

**Los desbordamientos de buffer y la corrupción de memoria** son la vulnerabilidad clásica del kernel. Se previenen acotando cada copia, prefiriendo funciones de cadena con límites y tratando la aritmética de punteros con desconfianza.

**La entrada del usuario cruza un límite de confianza.** Cada byte proveniente del espacio de usuario debe copiarse al kernel con `copyin(9)`, `copyinstr(9)` o `uiomove(9)` antes de usarlo. Cada byte que regresa debe copiarse con `copyout(9)` o `uiomove(9)`. La memoria en espacio de usuario no es de confianza; la memoria del kernel sí lo es. Mantenlas claramente separadas.

**La asignación de memoria** debe comprobarse, equilibrarse y controlarse. Comprueba siempre los retornos de `M_NOWAIT`. Usa `M_ZERO` por defecto. Empareja cada `malloc` con exactamente un `free`. Usa un `malloc_type` por driver para tener trazabilidad. Usa `explicit_bzero` o `zfree` para datos sensibles.

**Las condiciones de carrera y los errores TOCTOU** se producen por un bloqueo inconsistente o por tratar los datos del espacio de usuario como estables. Se corrigen con locks alrededor del estado compartido y copiando los datos del usuario antes de validarlos.

**Las comprobaciones de privilegio** usan `priv_check(9)` como primitiva canónica. Combínalas con conciencia de jail y securelevel donde corresponda. Establece permisos conservadores en los nodos de dispositivo. Deja que los frameworks MAC y Capsicum trabajen en paralelo.

**Las fugas de información** se evitan poniendo a cero las estructuras antes de rellenarlas, acotando las longitudes de copia en ambos extremos y manteniendo los punteros del kernel fuera de las salidas visibles para el usuario.

**El registro** es parte de la interfaz del driver. Úsalo para ayudar al operador sin ayudar al atacante. Limita la tasa de todo lo que pueda desencadenarse desde el espacio de usuario. No registres datos sensibles.

**Los valores predeterminados seguros** implican fallar de forma cerrada, usar listas blancas en lugar de listas negras, establecer valores predeterminados conservadores y tratar los caminos de error con el mismo cuidado que los de éxito.

**Las pruebas y el endurecimiento** convierten el código cuidadoso en código de confianza. Compila con `INVARIANTS`, `WITNESS` y los sanitizadores del kernel. Realiza pruebas de estrés. Haz fuzzing. Revisa. Vuelve a probar.

Nada de esto es un esfuerzo puntual. Un driver se mantiene seguro porque su autor sigue aplicando estos hábitos en cada commit, en cada versión, durante toda la vida del código.

La disciplina no es glamurosa. Es trabajo tedioso: poner a cero la estructura, comprobar la longitud, adquirir el lock, usar `priv_check`. Pero este trabajo tedioso es exactamente lo que mantiene los sistemas seguros. Un kernel comprometido es un evento catastrófico para los usuarios. Un driver comprometido es una vía de acceso al kernel. La persona al teclado de ese driver, decidiendo si añadir la comprobación de límites o saltársela, está tomando una decisión de seguridad que puede ser invisible durante años y que de repente puede importar muchísimo.

Sé el autor que añade la comprobación de límites.

### Una reflexión más: la seguridad como identidad profesional

Vale la pena decirlo explícitamente: los hábitos de este capítulo no son meras técnicas. Son lo que distingue a un autor de kernel experimentado de un aprendiz. Todo ingeniero de kernel maduro lleva esta lista mental no porque la haya memorizado, sino porque ha interiorizado, con los años, un escepticismo hacia su propio código. El escepticismo no es ansiedad. Es disciplina.

Escribe código y luego léelo como si lo hubiera escrito un desconocido. Pregúntate qué ocurre si el llamador es hostil. Pregúntate qué ocurre si el valor es cero, negativo o imposiblemente grande. Pregúntate qué ocurre si el otro thread llega entre estas dos instrucciones. Pregúntate qué ocurre en el camino de error que no pensabas probar. Escribe la comprobación. Escribe la aserción. Sigue adelante.

Esto es lo que hacen los ingenieros de kernel profesionales. No es glamuroso, rara vez recibe aplausos, y es lo que mantiene en pie el sistema operativo del que todos dependemos. El kernel no es magia; son millones de líneas de código cuidadosamente revisado, escrito y reescrito por personas que tratan cada línea como una pequeña responsabilidad. Unirse a esa profesión significa asumir esa disciplina.

Ahora tienes las herramientas. El resto es práctica.

## Mirando hacia adelante: Device Tree y desarrollo embebido

Este capítulo te ha entrenado para mirar tu driver desde fuera, con los ojos de quien podría intentar hacer un uso indebido de él. Los límites que has aprendido a vigilar eran invisibles para el compilador, pero muy reales para el kernel: el espacio de usuario a un lado, la memoria del kernel al otro; un thread con privilegios, otro sin ellos; un campo de longitud que el llamante declaraba, una longitud que el driver tenía que verificar. El capítulo 31 trató sobre *quién tiene permiso para pedirle al driver que haga algo* y *qué debe comprobar el driver antes de aceptar*.

El capítulo 32 cambia completamente la perspectiva. La pregunta deja de ser *quién quiere que este driver se ejecute* y pasa a ser *cómo encuentra este driver su hardware en absoluto*. En las máquinas de tipo PC en las que nos hemos apoyado hasta ahora, esa pregunta tenía una respuesta cómoda. Los dispositivos PCI se anunciaban a través de registros de configuración estándar. Los periféricos descritos por ACPI aparecían en una tabla que el firmware entregaba al kernel. El bus hacía el trabajo de búsqueda, el kernel interrogaba a cada candidato, y la función `probe()` de tu driver solo tenía que mirar un identificador y decir sí o no. El descubrimiento era, en gran medida, problema de otro.

En plataformas embebidas, esa suposición falla. Una pequeña placa ARM no habla PCI, no lleva un BIOS con ACPI y no entrega al kernel una tabla ordenada de dispositivos. El SoC tiene un controlador I2C en una dirección física fija, tres UARTs en otras tres direcciones fijas, un banco GPIO en una cuarta, un temporizador, un watchdog, un árbol de relojes y una docena de periféricos más soldados a la placa en una disposición concreta. Nada en el silicio se anuncia a sí mismo. Si el kernel va a conectar drivers a esos periféricos, algo tiene que decirle dónde están, qué son y cómo se relacionan entre sí.

Ese algo es el **Device Tree**, y el capítulo 32 es donde aprendes a trabajar con él. Verás cómo los archivos fuente `.dts` describen el hardware, cómo el Device Tree Compiler (`dtc`) los convierte en los blobs `.dtb` que el bootloader entrega al kernel, y cómo el soporte FDT de FreeBSD recorre esos blobs para decidir qué drivers conectar. Conocerás las interfaces `ofw_bus`, el enumerador `simplebus` y los helpers de Open Firmware (`ofw_bus_search_compatible`, `ofw_bus_get_node`, las llamadas de lectura de propiedades) que convierten un nodo de Device Tree en un driver correctamente conectado. Compilarás un pequeño overlay, lo cargarás y verás cómo un driver pedagógico aparece en `dmesg`.

Los hábitos de seguridad que has desarrollado en este capítulo te acompañarán en ese territorio. Un driver para una placa embebida sigue siendo un driver: sigue ejecutándose en el espacio del kernel, sigue copiando datos a través de los límites del espacio de usuario, sigue necesitando comprobaciones de límites, sigue tomando locks y sigue limpiando en detach. Una placa ARM no relaja ninguno de esos requisitos. De hecho, los sistemas embebidos elevan el nivel de riesgo, porque la misma imagen de placa puede desplegarse en miles de dispositivos en el campo, cada uno más difícil de parchear que un servidor en un centro de datos. La disposición que acabas de aprender, escéptica con las entradas, cuidadosa con la memoria y conservadora respecto a los privilegios, es exactamente la disposición que necesita el autor de un driver embebido.

Lo que cambia en el capítulo 32 es el conjunto de helpers que llamas para descubrir tu hardware y los archivos que lees para saber hacia dónde apuntarlos. La forma probe-attach-detach se mantiene. El softc se mantiene. El ciclo de vida se mantiene. Un puñado de nuevas llamadas y una nueva forma de pensar sobre la descripción del hardware son lo que añades. El capítulo los desarrolla con calma, desde la forma de un archivo `.dts` hasta un driver funcional que parpadea un LED en una placa real o emulada.

Nos vemos allí.

## Una nota final sobre los hábitos

Este capítulo ha sido más largo que algunos. La extensión es deliberada. La seguridad no es un tema que pueda resumirse en una sola regla contundente; es una forma de pensar que requiere ejemplos, práctica y repetición. Un lector que termina este capítulo por primera vez habrá estado expuesto a los patrones. Un lector que vuelve a este capítulo al comenzar un nuevo driver encontrará nuevo significado en pasajes que en la primera lectura le parecían meramente informativos.

Aquí están los hábitos más importantes, condensados en una sola lista para que los lleves contigo. Son los reflejos que más importan en el trabajo diario con drivers:

Cualquier valor del espacio de usuario es hostil hasta que se copia, se acota y se valida.

Cualquier longitud tiene un máximo. El máximo se aplica antes de que nada utilice la longitud.

Cualquier estructura copiada al espacio de usuario se pone a cero primero.

Cualquier asignación de memoria va emparejada con una liberación en todos los caminos de código.

Cualquier sección crítica se mantiene durante toda la secuencia de verificación y acción que protege.

Cualquier operación sensible a privilegios llama a `priv_check` antes de actuar.

Cualquier ruta de detach drena el trabajo asíncrono antes de liberar el estado.

Cualquier mensaje de log que pueda ser activado desde el espacio de usuario está limitado en frecuencia.

Cualquier entrada desconocida devuelve un error, nunca un éxito silencioso.

Cualquier suposición que valga la pena hacer vale la pena escribirla como `KASSERT`.

Nueve líneas. Si estas se vuelven automáticas, tienes el núcleo de lo que este capítulo enseña.

El oficio crece desde aquí. Hay más patrones, más matices, más herramientas; los irás encontrando a medida que leas más código fuente de FreeBSD, revises más código y escribas más drivers. Lo que permanece igual es la disposición: escéptico con las entradas hostiles, cuidadoso con la memoria, claro sobre los límites de los locks, conservador respecto a lo que se expone. Esa disposición es la que comparten los ingenieros del kernel a lo largo de décadas. Ahora la tienes tú. Úsala bien.

## Una nota sobre la evolución de las amenazas

Un pensamiento más antes de las palabras de cierre. Las amenazas contra las que nos defendemos hoy no son las amenazas contra las que nos defenderemos dentro de diez años. Los atacantes evolucionan. Las mitigaciones evolucionan. Se descubren nuevas clases de bugs y las antiguas caen en desuso. Un driver que era de vanguardia en sus defensas en 2020 puede necesitar actualización para considerarse seguro en 2030.

Esto no es motivo de desesperación. Es un motivo para el aprendizaje continuo. Cada año, un autor de drivers responsable debería leer algunos artículos de seguridad nuevos, probar algunos sanitizadores nuevos y revisar los CVEs recientes que afectan a kernels similares al suyo. No para memorizar vulnerabilidades específicas, sino para mantener una idea de dónde se están encontrando los bugs hoy.

Los patrones que enseña este capítulo son estables. Los desbordamientos de buffer han sido bugs desde antes de UNIX. El use-after-free ha sido un bug desde que C tuvo malloc. Las condiciones de carrera han sido bugs desde que los kernels tuvieron múltiples threads. Las encarnaciones concretas cambian, pero las defensas subyacentes perduran. Un driver escrito con la disposición que fomenta este capítulo estará en gran parte bien en cualquier década; cuando los detalles cambien, el autor que construyó esa disposición se adaptará más rápido que quien simplemente memorizó una lista de verificación.

## Palabras de cierre

Un driver es pequeño. La influencia de un driver es grande. El código que escribes se ejecuta en la parte más privilegiada del sistema, toca memoria de la que dependen todos los demás procesos, y es depositario de los secretos de usuarios que nunca verán tu nombre. Esa confianza no es automática; se gana, una línea cuidadosa a la vez, por autores que asumieron que el atacante estaba mirando y construyeron en consecuencia.

Los autores de FreeBSD llevan décadas escribiendo ese tipo de código. El kernel de FreeBSD no es perfecto; ningún kernel de su escala puede serlo. Pero tiene una cultura de cuidado, un conjunto de primitivas que recompensan la diligencia y una comunidad que trata los bugs de seguridad como oportunidades de aprendizaje y no como vergüenzas. Cuando escribes un driver para FreeBSD, escribes dentro de esa cultura. Tu código lo leerán personas que conocen la diferencia entre un desbordamiento de buffer y un buffer que por casualidad es suficientemente grande; que conocen la diferencia entre una comprobación de privilegios que intercepta a root fuera de jail y una que intercepta a root dentro de jail; que saben que una condición de carrera no es un raro fallo de temporización sino una vulnerabilidad a la espera del atacante adecuado.

Escribe para esos lectores. Escribe para el usuario cuyo portátil ejecuta tu código sin saber que está ahí. Escribe para el mantenedor que heredará tu trabajo dentro de diez años. Escribe para el revisor que verá la comprobación defensiva que añadiste y sentirá una silenciosa satisfacción al comprobar que alguien lo tuvo en cuenta.

De eso ha tratado el capítulo 31. De eso tratará el resto de tu carrera como autor de código del kernel. Gracias por tomarte el tiempo de trabajarlo con cuidado. El capítulo termina aquí; la práctica comienza mañana.
