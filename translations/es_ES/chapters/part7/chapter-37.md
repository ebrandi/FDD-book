---
title: "Enviar tu driver al FreeBSD Project"
description: "Proceso y directrices para contribuir drivers a FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 37
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 150
language: "es-ES"
---
# Cómo enviar tu driver al proyecto FreeBSD

## Introducción

Si has seguido este libro desde el principio, has recorrido un largo camino. Empezaste sin ningún conocimiento del kernel, aprendiste UNIX y C, recorriste la anatomía de un driver de FreeBSD, construiste drivers de caracteres y de red a mano, exploraste el framework Newbus y trabajaste a fondo en una parte completa sobre temas avanzados: portabilidad, virtualización, seguridad, FDT, rendimiento, depuración avanzada, E/S asíncrona e ingeniería inversa. Has llegado al punto en que puedes sentarte frente a una máquina de laboratorio, abrir un editor de texto y escribir un driver para un dispositivo que FreeBSD aún no soporta. Esa es una habilidad de ingeniería seria, y no llegó sin esfuerzo.

Este capítulo es donde el trabajo se vuelve hacia el exterior. Hasta ahora, los drivers que has construido han vivido en tus propios sistemas. Los cargaste con `kldload`, los probaste, los depuraste y los descargaste cuando terminaste. Te fueron útiles, y quizás también a algunos amigos o colegas que los copiaron de tu repositorio. Eso ya es un trabajo valioso. Pero un driver que vive únicamente en tu máquina solo sirve a quienes, por azar, encuentran tu máquina. Un driver que vive dentro del árbol de código fuente de FreeBSD sirve a todos los usuarios de FreeBSD, en cada versión, en cada arquitectura que el driver soporte, mientras el código se mantenga. La amplificación de valor es enorme, y las responsabilidades que conlleva son el tema de este capítulo.

El proyecto FreeBSD lleva aceptando contribuciones de desarrolladores externos desde principios de los años noventa. Miles de personas han enviado parches; cientos han llegado a convertirse en committers. El proceso por el que el código nuevo entra en el árbol no es un laberinto burocrático. Es un flujo de trabajo de revisión diseñado para preservar las cualidades que hacen que FreeBSD sea de confianza: consistencia del código, mantenibilidad a largo plazo, portabilidad entre arquitecturas, limpieza legal, documentación cuidadosa y continuidad en el mantenimiento. Cada una de esas cualidades es algo que los revisores protegen en nombre de todos los que ejecutan FreeBSD. Cuando envías un driver, le estás pidiendo al proyecto que asuma una responsabilidad a largo plazo sobre él. El proceso de revisión es la forma en que el proyecto confirma que el driver merece esa responsabilidad y, también, la forma en que el proyecto te ayuda a dar al driver la forma adecuada para que la respuesta sea sí.

Ese enfoque importa. Los nuevos contribuidores suelen llegar al proceso de revisión esperando una experiencia adversarial, en la que los revisores busquen razones para rechazar el trabajo. La realidad es la contraria. Los revisores, en su gran mayoría, están intentando ayudar. Quieren que tu driver sea integrado. Quieren que se integre de una forma que siga funcionando cinco versiones más adelante. Quieren que no suponga una carga de mantenimiento para quien tenga que tocar el código circundante el año que viene. Los comentarios que dejan en un parche de primera ronda no son una puntuación; son una lista de cosas que, cuando se resuelven, permitirán que el parche sea integrado. Un contribuidor que interioriza ese enfoque encuentra el proceso de revisión cooperativo en lugar de estresante.

Sin embargo, hay una distinción que debe quedar clara desde el principio. Un driver que funciona no es lo mismo que un driver listo para upstream. Un driver que se carga en tu portátil, maneja tu hardware y no provoca un kernel panic al descargarlo ha superado solo los primeros checkpoints. Para estar listo para upstream, también necesita pasar las guías de estilo del proyecto, llevar una licencia adecuada, venir acompañado de una página de manual que explique cómo interactúa un usuario con él, compilar en cada arquitectura que el proyecto soporta, integrarse limpiamente en el diseño del árbol de código fuente existente y ir acompañado de un mensaje de commit que otro revisor pueda leer cinco años después sin necesidad de reconstruir el contexto. Ninguno de estos requisitos es trabajo innecesario. Cada uno existe porque la experiencia ha demostrado qué ocurre en las bases de código donde se omiten.

El capítulo está organizado en torno a un flujo de trabajo natural. Empezaremos viendo cómo está organizado el proyecto FreeBSD desde el punto de vista de un contribuidor, y qué significa en la práctica la distinción entre contribuidor y committer. A continuación, recorreremos la preparación mecánica de un driver para su envío: qué distribución de archivos usar, qué estilo de código seguir, cómo nombrar las cosas y cómo escribir un mensaje de commit que un revisor pueda leer con agradecimiento. Veremos los aspectos de licencias y compatibilidad legal, porque incluso el código excelente no puede ser aceptado si su procedencia no está clara. Dedicaremos un tiempo considerable a las páginas de manual, porque la página de manual es la mitad del driver orientada al lector y merece el mismo cuidado que el código. Revisaremos las expectativas de pruebas, desde builds locales hasta `make universe`, y veremos cómo generar un parche de una forma que los revisores encuentren conveniente. Hablaremos sobre el lado humano del proceso de revisión: cómo trabajar con un mentor, cómo responder al feedback, cómo iterar en las rondas de revisión sin perder el impulso. Y terminaremos con el compromiso más duradero de todos: el mantenimiento una vez que el driver ha sido integrado.

El código complementario de este capítulo, en `examples/part-07/ch37-upstream/`, incluye varios artefactos prácticos: un diseño de árbol de driver de referencia que refleja la forma que tomaría un driver pequeño en `/usr/src/sys/dev/`; una página de manual de ejemplo que puedes adaptar a tu propio driver; una lista de verificación para el envío que puedes usar como revisión final antes de enviar un parche; un borrador de carta de presentación para un correo a la lista de correo del proyecto; un script auxiliar que genera un parche con las convenciones que el proyecto espera; y un script de validación previa al envío que ejecuta las comprobaciones de lint, estilo y build en el orden correcto. Ninguno de estos es un sustituto para comprender el material subyacente, pero te salvarán de los errores comunes que cuestan a los contribuidores primerizos una o dos rondas de revisión.

Una nota más antes de empezar. Este capítulo no va a enseñarte la historia política o de gobernanza del proyecto FreeBSD. Mencionaremos el Core Team y los roles de los distintos comités solo en la medida en que sea necesario para que un contribuidor navegue por el proyecto. Si tienes curiosidad por la gobernanza de FreeBSD después de leer este capítulo, la documentación propia del proyecto es el siguiente paso adecuado, y te indicaremos dónde encontrarla. El alcance de este capítulo es el trabajo práctico de convertir un driver que has escrito en un driver que puede ser integrado en upstream.

Al final del capítulo, tendrás una imagen clara del flujo de trabajo de envío, una comprensión práctica de las convenciones de estilo y documentación, un ensayo del ciclo de revisión y una visión realista de qué ocurre cuando tu driver ya está en el árbol. No serás un committer de FreeBSD al final de este capítulo; el proyecto otorga derechos de commit solo después de un historial sostenido de contribuciones de calidad, y eso es intencionado. Pero sabrás cómo hacer la primera contribución, cómo hacerla bien y cómo construir la reputación que podría, con el tiempo, llevar a derechos de commit si esa es la dirección que decides seguir.

## Orientación para el lector: cómo usar este capítulo

Este capítulo se encuentra en la Parte 7 del libro, inmediatamente después del capítulo sobre ingeniería inversa e inmediatamente antes del capítulo de cierre. A diferencia de muchos capítulos anteriores, el tema aquí es más sobre flujo de trabajo y disciplina que sobre los internos del kernel. No necesitarás escribir código nuevo de driver para seguir el capítulo, aunque te beneficiará enormemente si aplicas lo que aprendes a un driver que ya hayas escrito.

El tiempo de lectura es moderado. Si lees de corrido sin detenerte a probar nada, la prosa llevará alrededor de dos o tres horas. Si trabajas con los laboratorios y ejercicios de desafío, reserva un fin de semana completo o varias tardes. Los laboratorios están estructurados como ejercicios cortos y enfocados que puedes realizar con cualquier driver pequeño que tengas a mano, incluyendo uno de los drivers de capítulos anteriores, uno de los drivers simulados del Capítulo 36 o un driver nuevo que escribas para este capítulo.

No necesitas hardware especial. Una máquina virtual de desarrollo con FreeBSD 14.3, o un sistema FreeBSD en hardware real donde te sientas cómodo ejecutando comandos de build y prueba, es suficiente. Los laboratorios te pedirán que apliques comprobaciones de estilo a código real, construyas un driver real como módulo cargable, valides una página de manual con `mandoc(1)` y ensayes el flujo de trabajo de generación de parches contra una rama git desechable. Nada tocará el Phabricator real de FreeBSD ni GitHub, por lo que no hay riesgo de enviar accidentalmente trabajo a medias al proyecto.

Un calendario de lectura razonable tiene este aspecto. Lee las secciones 1 y 2 en una sola sesión; establecen el marco conceptual de cómo funciona el desarrollo de FreeBSD y cómo debe estar organizado tu driver. Haz una pausa. Lee las secciones 3 y 4 en una segunda sesión; cubren las licencias y las páginas de manual, que juntas constituyen la mayor parte del trabajo administrativo de un envío. Lee las secciones 5 y 6 en una tercera sesión; cubren las pruebas y la generación real de parches, que es donde el capítulo pasa de la preparación a la acción. Lee las secciones 7 y 8 en una cuarta sesión; cubren el lado humano y a largo plazo de la contribución. Los laboratorios se hacen mejor durante un fin de semana, con tiempo suficiente para repetirlos más de una vez si la primera pasada revela algo que quieras mejorar.

Si ya eres un usuario de FreeBSD seguro y un desarrollador del kernel con confianza, el material de este capítulo te resultará familiar en líneas generales, pero puede que aún te sorprenda en los detalles. Los detalles importan. Un revisor que conoce bien el árbol notará en segundos si la distribución de archivos coincide con las convenciones, si la cabecera de copyright está en la forma recomendada actual, si la página de manual utiliza las construcciones modernas de mdoc y si el mensaje de commit sigue el estilo de línea de asunto esperado. Hacer bien estos pequeños detalles desde el principio marca la diferencia entre una revisión que dura una ronda y una que dura cinco.

Si eres principiante, no dejes que los detalles te intimiden. Todo committer del proyecto fue, en algún momento, alguien cuyo primer parche rebotó cinco rondas de revisión antes de ser integrado. La capacidad de escribir buen código es algo que ya has desarrollado a lo largo del libro. La capacidad de enviarlo bien es lo que añade este capítulo. No lo conseguirás perfecto en el primer intento. Eso es normal. Lo que importa es que entiendas la forma del proceso y que abordes cada revisión con la intención de mejorar el envío en lugar de defenderlo.

Varias de las directrices de este capítulo, especialmente en torno a licencias, páginas de manual y el flujo de trabajo de revisión, reflejan el estado del proyecto FreeBSD en FreeBSD 14.3. El proyecto evoluciona y algunas de las convenciones específicas pueden cambiar con el tiempo. Cuando sepamos que una convención está cambiando, lo indicaremos. Cuando citemos un archivo concreto del árbol, nombraremos el archivo para que puedas abrirlo tú mismo y verificar el estado actual. El lector que confía pero también verifica es el que más beneficia al proyecto.

Una última nota sobre el ritmo. Este capítulo enseña deliberadamente disciplina tanto como enseña proceso. Algunas de las secciones parecerán casi repetitivas en su insistencia en pequeños detalles: espacios en blanco al final de línea, comentarios de cabecera correctos, uso exacto de las macros de la página de manual. Esa insistencia es parte de la lección. FreeBSD es una base de código amplia con una larga memoria institucional, y los pequeños detalles son lo que la mantiene manejable. Si te sientes tentado a saltarte a la ligera una sección de estilo, frena en cambio. La lentitud es el oficio.

## Cómo sacar el máximo provecho de este capítulo

El capítulo está organizado para leerse de forma lineal, pero cada sección se sostiene bien por sí sola de modo que puedes volver a una sección concreta cuando la necesites. Varios hábitos te ayudarán a asimilar el material.

Primero, lee cada sección con el árbol de código fuente de FreeBSD abierto en pantalla. Cada vez que el capítulo mencione un archivo de referencia como `/usr/src/share/man/man9/style.9`, ábrelo y échale un vistazo. El capítulo te da la estructura y la motivación; los archivos de referencia te dan el detalle autorizado. El hábito de contrastar lo que lees en el capítulo con lo que el árbol dice en realidad te será útil durante toda tu carrera como colaborador de FreeBSD.

> **Una nota sobre los números de línea.** Cuando el capítulo te señale alguna parte de la infraestructura del árbol por su nombre, como `make_dev_s`, `DRIVER_MODULE` o las propias reglas de `style(9)`, la referencia está anclada en ese nombre. Las transcripciones del verificador de estilo del tipo `mydev.c:23` que verás más adelante se refieren a líneas de tu propio driver en desarrollo y cambiarán a medida que lo edites. En cualquier caso, la referencia duradera es el símbolo, no el número: busca el nombre con grep en lugar de confiar en una línea concreta.

Segundo, mantén un pequeño archivo de notas mientras lees. Cada vez que una sección mencione una convención, una sección obligatoria o un comando, anótalo. Al final del capítulo tendrás una lista de verificación para la contribución totalmente personalizada. El directorio `examples/part-07/ch37-upstream/` incluye una plantilla de lista de verificación con la que puedes empezar, pero una lista que hayas escrito tú mismo, con tus propias palabras, te resultará más útil que cualquier plantilla.

Tercero, ten en mente un pequeño driver propio mientras lees. Puede ser el driver LED de capítulos anteriores, el dispositivo simulado del Capítulo 36 o un driver de caracteres que hayas escrito como práctica. El capítulo te pedirá que imagines que preparas ese driver concreto para enviarlo al proyecto. Trabajar con un driver real hace que las instrucciones calen mucho mejor que intentar absorberlas en abstracto.

Cuarto, no te saltes los laboratorios. Los laboratorios de este capítulo son breves y prácticos. La mayoría llevan menos de una hora. Existen porque hay partes del proceso de contribución que solo se entienden de verdad cuando las pruebas contra código real. Un lector que trabaje los laboratorios saldrá con una verdadera memoria muscular; uno que los salte volverá a releer el capítulo seis meses después y descubrirá que la mayor parte no se le quedó.

Quinto, trata los primeros errores como parte del aprendizaje. La primera vez que ejecutes `tools/build/checkstyle9.pl` contra tu código, verás avisos. La primera vez que ejecutes `mandoc -Tlint` contra tu página de manual, verás avisos. La primera vez que ejecutes `make universe` contra tu árbol, verás errores en al menos una arquitectura. Cada uno de esos avisos te está enseñando algo. Los revisores del proyecto ven esos mismos avisos a diario; el arte de preparar una contribución es, en gran medida, el arte de detectarlos y corregirlos antes de que nadie más tenga que hacerlo.

Por último, sé paciente con el ritmo del capítulo. Algunas de las secciones posteriores dedican tiempo a lo que podría parecer material social o interpersonal: cómo gestionar el feedback, cómo responder a un revisor que ha malinterpretado tu patch, cómo construir las relaciones que conducen al patrocinio. Ese material no es opcional. La ingeniería de software a la altura de un proyecto de kernel de código abierto es un oficio colaborativo, y la colaboración es el oficio en sí. Leer esas secciones a la ligera te costará más en la práctica que leer las secciones de estilo a la ligera.

Ya tienes el mapa. Pasemos a la primera sección y veamos cómo está organizado el proyecto FreeBSD desde el punto de vista de un colaborador.

## Sección 1: Comprender el proceso de contribución a FreeBSD

### Qué es realmente el proyecto FreeBSD

Antes de hablar sobre cómo contribuir al proyecto FreeBSD, necesitamos tener una imagen clara de qué es ese proyecto. El proyecto FreeBSD es una comunidad de voluntarios y colaboradores remunerados que juntos desarrollan, prueban, documentan, publican y dan soporte al sistema operativo FreeBSD. El proyecto lleva en funcionamiento de forma continua desde 1993. Se organiza en torno a un conjunto de árboles de código fuente compartidos, una cultura de revisión de código, una disciplina de ingeniería de releases y un acervo de conocimiento institucional acumulado sobre cómo deben construirse los kernels, los userlands, los ports y la documentación.

El proyecto se resume a menudo en tres palabras: source, ports y documentation. Estas corresponden a tres repositorios o subproyectos principales, cada uno con sus propios mantenedores, revisores y convenciones. Source, habitualmente escrito como `src`, es el sistema base: el kernel, las bibliotecas, las utilidades de userland, todo lo que incluye una instalación de FreeBSD. Ports es la colección de software de terceros que puede construirse sobre FreeBSD, como lenguajes de programación, entornos de escritorio y servidores de aplicaciones. Documentation es el Handbook, los artículos, los libros como el Porter's Handbook y el Developer's Handbook, el sitio web de FreeBSD y la infraestructura de traducción.

Los drivers de dispositivo viven principalmente en el árbol `src`, porque forman parte del kernel del sistema base y del soporte del sistema base para el hardware. Cuando este capítulo habla de enviar un driver, se refiere a enviarlo al árbol `src`. Ports y Documentation tienen sus propios procesos de contribución, que siguen principios similares pero difieren en los detalles. Este capítulo se centra exclusivamente en `src`.

El árbol `src` es extenso. Puedes ver su estructura de alto nivel explorando `/usr/src/`. La página de manual `/usr/src/share/man/man7/development.7` ofrece una introducción breve y legible al proceso de desarrollo, y el archivo `/usr/src/CONTRIBUTING.md` es la guía oficial del proyecto para los colaboradores. Si solo vas a leer dos archivos antes de tu primera contribución, lee esos dos. Los citaremos en repetidas ocasiones a lo largo de este capítulo.

### La estructura de toma de decisiones del proyecto

FreeBSD usa una estructura de toma de decisiones relativamente plana en comparación con otros proyectos grandes. El núcleo de esa estructura es el grupo de committers, que son desarrolladores con acceso de escritura a los repositorios de código fuente. Los committers son elegidos por los committers existentes tras una historia sostenida de contribuciones de calidad. Un organismo electo de nueve personas llamado Core Team se ocupa de ciertos tipos de decisiones y disputas a nivel de proyecto. Equipos más reducidos, como el Release Engineering Team (re@), el Security Officer Team (so@), el Ports Management Team (portmgr@) y el Documentation Engineering Team (doceng@), se encargan de áreas específicas.

A efectos de enviar un driver, la mayor parte de esa estructura no importa demasiado en la práctica cotidiana. Las personas que revisarán tu driver son committers individuales que conocen el subsistema en el que encaja tu driver. Si tu driver es un driver de red, los revisores serán probablemente personas activas en el subsistema de red. Si es un driver USB, los revisores serán personas activas en USB. El Core Team no interviene en el envío de drivers individuales; tampoco lo hace el Release Engineering Team, aunque serán ellos quienes decidan en qué release aparece tu driver por primera vez una vez integrado.

El modelo mental práctico es el siguiente. El proyecto FreeBSD es una gran comunidad de ingenieros. Algunos de ellos pueden hacer commit directamente en el árbol. Un número mucho mayor contribuye a través de procesos de revisión. Cuando envías un driver, pasas a formar parte de ese grupo más amplio, y el proceso de revisión es la forma en que la comunidad de committers evalúa si el driver está listo para entrar en el árbol bajo su responsabilidad compartida.

### Colaborador frente a committer

La distinción entre colaborador y committer es fundamental para entender cómo funciona el proyecto, y con frecuencia los recién llegados la malinterpretan.

Un colaborador es cualquier persona que envía cambios al proyecto. Te conviertes en colaborador la primera vez que abres una revisión en Phabricator o un pull request en GitHub contra el árbol de código fuente de FreeBSD. No existe un proceso formal para convertirse en colaborador. Simplemente envías trabajo. Si el trabajo es bueno, será revisado, corregido y finalmente incorporado al árbol por un committer en tu nombre. El commit lleva tu nombre y correo electrónico en el campo `Author:`, de modo que recibes el crédito por el código aunque no lo hayas publicado tú directamente.

Un committer es un colaborador al que se le ha concedido acceso de escritura directo a uno de los repositorios. Los derechos de commit se conceden tras una historia sostenida de contribuciones de calidad, normalmente a lo largo de varios años, y solo después de una nominación por parte de un committer existente y una votación del grupo de committers correspondiente. Los derechos de commit conllevan responsabilidades: se espera que revises los parches de otras personas, participes en las discusiones del proyecto y te hagas responsable del código que has incorporado a largo plazo.

Los dos roles no forman una jerarquía de prestigio. Son una división del trabajo. Los colaboradores se centran en escribir y enviar buenos parches. Los committers se centran en revisar, integrar y mantener el árbol. Un colaborador con un único parche de alto valor es más valioso para el proyecto que un committer que no participa activamente. El proyecto depende de ambos.

En este capítulo, debes verte a ti mismo como un colaborador. Tu objetivo es producir una contribución que un committer pueda revisar, aceptar e integrar. Si, años después, te encuentras con una larga historia de contribuciones y una relación sostenida con el proyecto, la cuestión de los derechos de commit puede surgir de forma natural. Pero esa es una pregunta para más adelante. El objetivo aquí es que tus primeras contribuciones cuenten.

### Cómo se organiza el trabajo en src

El repositorio `src` es un único árbol git. La rama principal, llamada `main` en git aunque también se la conoce como CURRENT en el lenguaje de ingeniería de releases, es donde ocurre todo el desarrollo activo. Los cambios se integran primero en `main`. Después, si el cambio es una corrección de errores o una pequeña funcionalidad que encaja en una release estable, puede aplicarse mediante cherry-pick a una de las ramas `stable/`, que corresponden a las versiones principales de FreeBSD, como la 14 y la 15. Las releases en sí son puntos etiquetados en las ramas `stable/`.

Como colaborador de drivers, tu objetivo por defecto es `main`. Tu parche debe aplicarse al `main` actual, debe compilar contra el `main` actual y debe probarse contra el `main` actual. Si el driver es algo que los usuarios de FreeBSD 14 también querrían, un committer puede decidir aplicar el commit mediante cherry-pick a la rama `stable/` correspondiente después de que haya estado un tiempo en `main`, pero esa es una decisión del committer, no tuya al hacer la contribución.

El repositorio git es visible en `https://cgit.freebsd.org/src/` y también tiene un espejo en `https://github.com/freebsd/freebsd-src`. Puedes hacer clone desde cualquiera de los dos. La URL de push oficial, para quienes tienen acceso de commit, es `ssh://git@gitrepo.FreeBSD.org/src.git`, pero como colaborador no harás push directamente. Generarás parches y los enviarás a través del proceso de revisión.

### Dónde viven los drivers de dispositivo en el árbol de código fuente

La mayoría de los drivers de dispositivo viven bajo `/usr/src/sys/dev/`. Este directorio contiene cientos de subdirectorios, uno por driver o familia de dispositivos. Si lo exploras, verás una panorámica del hardware que FreeBSD soporta: chips Ethernet, adaptadores SCSI, dispositivos USB, tarjetas de sonido, controladores I/O y una larga lista de otras categorías.

Una pequeña selección de subdirectorios de drivers existentes que conviene conocer:

- `/usr/src/sys/dev/null/` para el dispositivo de caracteres `/dev/null`.
- `/usr/src/sys/dev/led/` para el framework genérico de LED.
- `/usr/src/sys/dev/uart/` para los drivers UART.
- `/usr/src/sys/dev/virtio/` para la familia VirtIO.
- `/usr/src/sys/dev/usb/` para el subsistema USB y los drivers de dispositivo del lado USB.
- `/usr/src/sys/dev/re/` para el driver Ethernet PCI/PCIe de RealTek.
- `/usr/src/sys/dev/e1000/` para la familia de drivers Ethernet Gigabit de Intel.
- `/usr/src/sys/dev/random/` para el subsistema del kernel de números aleatorios.

Algunas categorías de driver viven en otros lugares. Los drivers de red cuya función tiene más que ver con la pila de red que con el propio dispositivo viven a veces bajo `/usr/src/sys/net/`. Los dispositivos similares a sistemas de archivos y los pseudodispositivos a veces viven bajo `/usr/src/sys/fs/`. Los drivers específicos de arquitectura a veces viven bajo `/usr/src/sys/<arch>/`. Para la mayoría de las contribuciones de principiantes, sin embargo, la pregunta será qué subdirectorio bajo `/usr/src/sys/dev/` es el hogar adecuado, y la respuesta casi siempre es obvia. Si tu driver es para un nuevo chip de red, probablemente le corresponda su propio subdirectorio bajo `/usr/src/sys/dev/`, posiblemente dentro de un subdirectorio de familia existente si extiende una familia ya existente. Si es para un dispositivo USB, puede que viva bajo `/usr/src/sys/dev/usb/` en su lugar. Si no estás seguro, una búsqueda en el árbol existente de un driver similar te indicará habitualmente dónde debe estar el tuyo.

### La otra mitad del driver: integración en el sistema de build del kernel

Además de los propios archivos de código fuente del driver, un driver integrado en FreeBSD tiene un segundo hogar bajo `/usr/src/sys/modules/`. Este directorio contiene los Makefiles del módulo del kernel que permiten construir el driver como módulo del kernel cargable. Para cada driver en `/usr/src/sys/dev/<driverdir>/`, existe habitualmente un directorio correspondiente en `/usr/src/sys/modules/<moduledir>/` que contiene un pequeño Makefile que indica al sistema de build cómo ensamblar el módulo. Analizaremos ese Makefile en detalle en la Sección 2.

Algunos drivers tienen puntos de integración adicionales. Los drivers que se incluyen como parte del kernel predeterminado aparecen en los archivos de configuración de arquitectura bajo `/usr/src/sys/<arch>/conf/GENERIC`. Los drivers que vienen con bindings de device tree pueden tener entradas bajo `/usr/src/sys/dts/`. Los drivers que exponen sysctls configurables o variables del loader necesitan entradas en la documentación correspondiente.

Como colaborador por primera vez, no necesitas preocuparte por todos estos puntos de integración a la vez. El conjunto mínimo para una contribución de driver típica son los archivos bajo `/usr/src/sys/dev/<driver>/`, el Makefile bajo `/usr/src/sys/modules/<driver>/` y la página de manual bajo `/usr/src/share/man/man4/<driver>.4`. Todo lo demás es incremental.

### Las plataformas de revisión

FreeBSD acepta actualmente contribuciones de código fuente a través de varios canales. El archivo `/usr/src/CONTRIBUTING.md` los enumera explícitamente:

- Un pull request en GitHub contra `https://github.com/freebsd/freebsd-src`.
- Una revisión de código en Phabricator en `https://reviews.freebsd.org/`.
- Un adjunto en un ticket de Bugzilla en `https://bugs.freebsd.org/`.
- Acceso directo al repositorio git, solo para committers.

Cada uno de estos canales tiene sus propias convenciones y sus casos de uso preferidos.

Phabricator es la plataforma tradicional de revisión de código del proyecto. Gestiona flujos de revisión completos: retroalimentación en múltiples rondas, historial de revisiones, comentarios en línea, asignación de revisores y parches listos para commit. La mayoría de los parches significativos, incluida la mayoría de las contribuciones de drivers, pasan por Phabricator. Lo verás referenciado como «review D12345» o similar, donde `D12345` es el identificador de revisión diferencial de Phabricator.

Los pull requests de GitHub son una vía de contribución cada vez más aceptada, en particular para parches pequeños, autocontenidos y sin controversia. El archivo `CONTRIBUTING.md` señala explícitamente que los PRs de GitHub funcionan bien cuando el cambio se limita a menos de una decena de archivos y menos de unas doscientas líneas, cuando pasa los trabajos de CI de GitHub y tiene un alcance limitado. Un driver pequeño típico encaja dentro de esos límites; un driver más grande con muchos archivos y puntos de integración puede manejarse mejor a través de Phabricator.

Bugzilla es el rastreador de errores del proyecto. Si tu driver corrige un error concreto que ha sido notificado, el lugar adecuado para el parche es la entrada correspondiente en Bugzilla. Si el driver es trabajo nuevo y no una corrección de errores, Bugzilla no suele ser el punto de partida adecuado, aunque un revisor puede pedirte que abras un ticket en Bugzilla para que el trabajo tenga un número de seguimiento.

Para una primera entrega de un driver, tanto Phabricator como una pull request en GitHub son opciones válidas. Muchos colaboradores empiezan con una PR en GitHub porque el flujo de trabajo les resulta familiar, y cambian a Phabricator si la revisión crece más allá de lo que GitHub gestiona bien. Recorreremos ambas vías en la sección 6.

El panorama de las plataformas de revisión cambia con el tiempo, y las URLs concretas, los límites de alcance y las vías preferidas descritas en este capítulo pueden quedar superadas por cambios en `/usr/src/CONTRIBUTING.md` o en las páginas de contribución del proyecto. Los detalles del proceso descritos arriba se verificaron por última vez frente al archivo `CONTRIBUTING.md` incluido en el árbol de código fuente el 2026-04-20. Antes de preparar tu primera entrega, vuelve a leer el `CONTRIBUTING.md` actual y la guía del committer enlazada desde el sitio de documentación de FreeBSD; si difieren de lo que dice este capítulo, confía en los archivos del proyecto, no en el libro.

### Ejercicio: explora el árbol de código fuente e identifica drivers similares

Antes de pasar a la Sección 2, dedica media hora a explorar `/usr/src/sys/dev/` y a construir una intuición sobre el aspecto exterior de un driver de FreeBSD.

Elige tres o cuatro drivers que sean aproximadamente similares en alcance al que piensas enviar, o a cualquier driver que hayas construido durante este libro. Para cada uno, observa:

- El contenido del directorio. ¿Cuántos archivos fuente? ¿Cuántos headers? ¿Cómo se llaman los archivos?
- El Makefile correspondiente en `/usr/src/sys/modules/`. ¿Qué aparece en `KMOD=` y en `SRCS=`?
- La página del manual en `/usr/src/share/man/man4/`. Ábrela y observa la estructura de secciones.
- La cabecera de copyright en el archivo `.c` principal. Fíjate en su formato.

No se trata de memorizar nada en este ejercicio. Estás construyendo una intuición de base. Cuando hayas examinado tres o cuatro drivers reales, las convenciones del árbol te parecerán menos abstractas. Cuando la Sección 2 hable de dónde van los archivos y cómo se nombran, las recomendaciones se asentarán sobre una imagen mental que ya habrás construido. Esa es la forma correcta de asimilar este material.

### Cerrando la Sección 1

El FreeBSD Project es una comunidad longeva organizada en torno a tres subproyectos principales: src, ports y documentation. Los drivers de dispositivo viven en el árbol src, principalmente en `/usr/src/sys/dev/`, con los Makefiles de módulo correspondientes en `/usr/src/sys/modules/` y las páginas del manual en `/usr/src/share/man/man4/`. Las contribuciones entran al árbol a través de un proceso de revisión gestionado por committers activos en el subsistema correspondiente. La distinción entre contributor y committer es una división del trabajo, no una jerarquía de prestigio. Tu objetivo como contributor por primera vez es producir una contribución que un committer pueda revisar, aceptar e integrar en el árbol.

Con ese marco establecido, podemos pasar ahora a la cuestión práctica de en qué estado debe estar tu driver antes de enviarlo. La Sección 2 recorre la preparación paso a paso.

## Sección 2: preparar tu driver para la contribución

### La diferencia entre un driver que funciona y un driver listo para contribuir

Un driver que se carga, ejecuta y descarga correctamente en tu máquina de pruebas es un driver que funciona. Un driver que un committer de FreeBSD puede revisar, integrar y mantener es un driver listo para contribuir. La diferencia entre ambos es casi siempre mayor de lo que los contributors noveles esperan, y cerrar esa brecha es el trabajo de esta sección.

La diferencia tiene tres partes. La primera es la organización: dónde van los archivos, cómo se nombran y cómo se integran con el sistema de build existente. La segunda es el estilo: cómo se formatea, nombra y comenta el código, y en qué medida se ajusta a las pautas de `style(9)` del proyecto. La tercera es la presentación: cómo se empaqueta el commit, qué dice el mensaje del commit y cómo se estructura el parche para su revisión. Ninguno de estos aspectos resulta difícil una vez que sabes qué buscar, pero cada uno implica una docena o dos de pequeñas convenciones que, en conjunto, determinan si la primera impresión de un revisor es positiva o difícil.

Antes de empezar, detente un momento a entender por qué existen estas convenciones. FreeBSD acumula treinta años de código. Miles de drivers han entrado en el árbol a lo largo de ese tiempo. Las convenciones que al principio parecen arbitrarias son, en casi todos los casos, el resultado de una experiencia dolorosa anterior que la comunidad decidió no repetir. Una convención que evita un error, o que reduce una fuente recurrente de fricción en la revisión, se amortiza con creces. Cuando sigues las convenciones, te beneficias de treinta años de memoria institucional. Cuando las ignoras, te ofreces voluntario para aprender de nuevo esas lecciones por tu cuenta y para hacer pasar a tus revisores por ellas otra vez.

### Dónde van los archivos

Para un driver independiente en el árbol, la organización típica tiene este aspecto. Supongamos que tu driver se llama `mydev` y que maneja una placa de sensores conectada por PCI.

- `/usr/src/sys/dev/mydev/mydev.c` es el archivo fuente principal del driver. Para un driver pequeño, puede ser el único archivo fuente.
- `/usr/src/sys/dev/mydev/mydev.h` es el header del driver. Si solo tienes un archivo `.c` y sus declaraciones internas no necesitan exponerse, puede que no necesites este header.
- `/usr/src/sys/dev/mydev/mydevreg.h` es un nombre habitual para un header que define los registros de hardware y los campos de bits. Esta convención, que usa el sufijo `reg`, está muy extendida en el árbol, y separar las definiciones de registros de las declaraciones internas del driver es una buena práctica.
- `/usr/src/sys/modules/mydev/Makefile` es el Makefile para compilar el driver como módulo del kernel cargable.
- `/usr/src/share/man/man4/mydev.4` es la página del manual.

Es posible que encuentres drivers existentes que no sigan exactamente esta organización. Los drivers más antiguos, anteriores a que se establecieran las convenciones actuales, a veces ponen todo en un mismo lugar o usan nombres de archivo diferentes. Las convenciones siguen evolucionando. Para un driver nuevo, seguir la organización moderna te ahorrará fricción en la revisión.

### Qué contiene `mydev.c`

El archivo fuente principal contiene normalmente, en este orden:

1. La cabecera de copyright y licencia, en el formato que cubriremos en la Sección 3.
2. Las directivas `#include`, que comienzan normalmente con `<sys/cdefs.h>` y `<sys/param.h>`, seguidas de los demás headers del kernel que necesite tu driver.
3. Declaraciones anticipadas y variables estáticas.
4. Los métodos del driver: `probe`, `attach`, `detach`, y cualquier otro que referencie tu tabla `device_method_t`.
5. Las funciones auxiliares que sean necesarias.
6. La tabla `device_method_t`, la estructura `driver_t`, el registro `DRIVER_MODULE` y la declaración `MODULE_VERSION`. Los drivers modernos de FreeBSD ya no declaran una variable `static devclass_t`; la firma actual de `DRIVER_MODULE` toma cinco argumentos (name, bus, driver, event handler, event handler argument) y el código del bus gestiona la clase de dispositivo por ti.

Un archivo de driver bien organizado tiene un ritmo visible que los lectores con experiencia en FreeBSD captan de inmediato. Los métodos aparecen antes que las tablas que los referencian. Las funciones auxiliares estáticas están cerca de los métodos que las utilizan. Las macros de registro van al final, de modo que el archivo se lee como una historia lineal que va desde las dependencias, pasando por las funciones, hasta el registro.

### Un archivo de driver mínimo

A modo de orientación, aquí tienes la estructura mínima de `mydev.c`. No está completo, pero muestra los elementos estructurales que un revisor esperará ver. Ya has visto el funcionamiento de cada una de estas macros en capítulos anteriores; aquí nos centramos en cómo se organizan en la página.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mydev/mydev.h>

static int	mydev_probe(device_t dev);
static int	mydev_attach(device_t dev);
static int	mydev_detach(device_t dev);

static int
mydev_probe(device_t dev)
{
	/* match your PCI vendor/device ID here */
	return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
	/* allocate resources, initialise the device */
	return (0);
}

static int
mydev_detach(device_t dev)
{
	/* release resources, quiesce the device */
	return (0);
}

static device_method_t mydev_methods[] = {
	DEVMETHOD(device_probe,		mydev_probe),
	DEVMETHOD(device_attach,	mydev_attach),
	DEVMETHOD(device_detach,	mydev_detach),
	DEVMETHOD_END
};

static driver_t mydev_driver = {
	"mydev",
	mydev_methods,
	sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, 0, 0);
MODULE_VERSION(mydev, 1);
MODULE_DEPEND(mydev, pci, 1, 1, 1);
```

Hay varios aspectos que merece la pena destacar. La cabecera de copyright usa el marcador de apertura `/*-`, que el script automático de recopilación de licencias reconoce. La línea SPDX nombra la licencia explícitamente. La indentación usa tabuladores, no espacios, como exige `style(9)`. Las declaraciones de función están separadas con tabuladores, también según `style(9)`. Las macros `DRIVER_MODULE` y relacionadas aparecen al final, en el orden que espera el sistema de build. Esta es la estructura que un revisor esperará encontrar.

### El Makefile del módulo

El Makefile del módulo suele ser muy pequeño. A continuación tienes un ejemplo realista, basado en el de `/usr/src/sys/modules/et/Makefile`:

```makefile
.PATH: ${SRCTOP}/sys/dev/mydev

KMOD=	mydev
SRCS=	mydev.c
SRCS+=	bus_if.h device_if.h pci_if.h

.include <bsd.kmod.mk>
```

En este breve archivo se codifican varias convenciones.

`SRCTOP` es una variable del sistema de build que apunta a la cima del árbol de código fuente. Su uso garantiza que el Makefile funcione independientemente de desde dónde se invoque el build en el árbol. No escribas `/usr/src` directamente en el código.

`KMOD` nombra el módulo. Es el nombre que usa `kldload`. Hazlo coincidir con el nombre del driver.

`SRCS` lista los archivos fuente. Los archivos `.c` son tus fuentes del driver. Los archivos `.h` con nombres como `bus_if.h` y `pci_if.h` no son headers convencionales; los genera automáticamente el sistema de build a partir de las definiciones de métodos en los archivos `.m` correspondientes. Los incluyes en la lista para que el sistema de build sepa que debe generarlos antes de compilar tu driver. Incluye `device_if.h` porque todo driver usa `device_method_t`; incluye `bus_if.h` si tu driver usa métodos `bus_*`; incluye `pci_if.h` si es un driver PCI; y así sucesivamente.

`bsd.kmod.mk` es la infraestructura de build estándar para módulos del kernel. Incluirlo al final te proporciona todas las reglas de build que necesitas.

Se aplican algunas convenciones adicionales:

- No añadas cabeceras de copyright a Makefiles triviales. La convención del árbol es que los Makefiles pequeños como este se tratan como archivos mecánicos y no llevan licencia. Los Makefiles con lógica sustancial sí llevan cabeceras de copyright.
- No uses características de GNU `make`. El sistema de build base de FreeBSD usa el BSD make incluido en el árbol, no GNU make.
- Usa tabuladores, no espacios, para indentar los cuerpos de las reglas.

### El archivo header

Si tu driver tiene un archivo header para declaraciones internas, colócalo en el mismo directorio que el archivo `.c`. La convención es nombrar el header interno `<driver>.h` y cualquier definición de registros de hardware `<driver>reg.h`. Mantén el ámbito del header reducido. Debe declarar estructuras y constantes que se usen en varios archivos `.c` del driver o que se necesiten para la interoperación con subsistemas estrechamente relacionados. No debe filtrar detalles internos del driver hacia el espacio de nombres general del kernel.

El header comienza con la misma cabecera de copyright que el archivo `.c`, seguida del include guard estándar:

```c
#ifndef _DEV_MYDEV_MYDEV_H_
#define _DEV_MYDEV_MYDEV_H_

/* header contents */

#endif /* _DEV_MYDEV_MYDEV_H_ */
```

El nombre del include guard sigue la convención de la ruta completa, en mayúsculas, con las barras y los puntos sustituidos por guiones bajos y con un guión bajo inicial y final. La convención es coherente en todo el árbol y un revisor notará cualquier desviación.

### Seguir `style(9)`: el resumen breve

El estilo de codificación completo de FreeBSD está documentado en `/usr/src/share/man/man9/style.9`. Deberías leer esa página del manual antes de enviar un driver, y al menos hojearla periódicamente a medida que tu propio estilo madura. Aquí extraeremos los puntos que con mayor frecuencia hacen tropezar a los contributors por primera vez.

#### Indentación y anchura de línea

La indentación usa tabuladores reales, con una tabulación de 8 columnas. Los niveles de indentación segundo y posteriores que no estén alineados con un tabulador usan 4 espacios adicionales de indentación. La anchura de línea es de 80 columnas; se permiten algunas excepciones cuando partir una línea la haría menos legible o rompería algo que se busca con grep, como un mensaje de pánico.

#### Forma de la cabecera de copyright

La cabecera de copyright usa el marcador de apertura `/*-`. Este marcador es especial. Una herramienta automatizada recopila licencias del árbol buscando comentarios multilínea que comiencen en la columna 1 con `/*-`. Usar el marcador identifica el bloque como una licencia; usar un `/*` convencional no lo hace. Inmediatamente después de `/*-`, la siguiente línea significativa debe ser `SPDX-License-Identifier:` seguida del código de licencia SPDX, como `BSD-2-Clause`. A continuación vienen una o más líneas de `Copyright`. Después, el texto de la licencia.

#### Declaraciones y definiciones de funciones

El tipo de retorno y la clase de almacenamiento de la función van en la línea anterior al nombre de la función. El nombre de la función comienza en la columna 1. Los argumentos caben en la misma línea que el nombre, a menos que eso supere las 80 columnas, en cuyo caso los argumentos siguientes se alinean con el paréntesis de apertura.

Correcto:

```c
static int
mydev_attach(device_t dev)
{
	struct mydev_softc *sc;

	sc = device_get_softc(dev);
	return (0);
}
```

Incorrecto, como señalaría de inmediato un revisor:

```c
static int mydev_attach(device_t dev) {
    struct mydev_softc *sc = device_get_softc(dev);
    return 0;
}
```

Las diferencias parecen pequeñas: la posición del tipo de retorno, la posición de la llave de apertura, el uso de espacios en lugar de tabuladores, la declaración con inicialización en una sola línea y la ausencia de paréntesis alrededor del valor de retorno. Cada una de esas diferencias infringe `style(9)`. En conjunto, hacen que la función parezca fuera de lugar en el árbol. Un revisor te pedirá que los corrijas, y corregirlos a posteriori supone más trabajo que escribirlos correctamente desde el principio.

#### Nombres de variables y convenciones de identificadores

Usa identificadores en minúsculas con guiones bajos en lugar de camelCase. `mydev_softc`, no `MydevSoftc` ni `mydevSoftc`. Las funciones siguen la misma convención.

Las constantes y macros se escriben en mayúsculas con guiones bajos:
`MYDEV_REG_CONTROL`, `MYDEV_FLAG_INITIALIZED`.

Las variables globales son poco habituales en los drivers; es preferible guardar el estado por dispositivo en el softc. Cuando una variable global sea inevitable, dale un nombre con el prefijo del driver para evitar colisiones con el resto del kernel.

#### Paréntesis en los valores de retorno

El estilo de FreeBSD exige que las expresiones `return` vayan entre paréntesis:
`return (0);` en lugar de `return 0;`. Esta es una convención que se remonta al kernel BSD original y se aplica con bastante rigor.

#### Comentarios

Los comentarios multilínea utilizan la siguiente forma:

```c
/*
 * This is the opening of a multi-line comment.  Make it real
 * sentences.  Fill the lines to the column 80 mark so the
 * comment reads like a paragraph.
 */
```

Los comentarios de una sola línea pueden usar la forma tradicional `/* ... */` o la forma `// ...`. Sé coherente dentro de un archivo; no mezcles estilos.

Los comentarios deben explicar el porqué, no el qué. `/* iterate over the array */` no aporta nada si el lector puede ver el bucle. `/* the hardware requires a read-back to flush the write before we proceed */` sí resulta útil porque explica una restricción no evidente.

#### Mensajes de error

Utiliza `device_printf(dev, "message\n")` para la salida de registro específica del dispositivo. No uses `printf` directamente desde un driver si tienes un `device_t` disponible; `device_printf` antepone al mensaje el nombre del driver y el número de unidad, que es lo que cualquier persona que lea los logs del kernel espera ver.

Los mensajes de error que se buscan con grep deben permanecer en una sola línea aunque superen los 80 caracteres. La página de manual `style(9)` es explícita al respecto.

#### Números mágicos

No uses números mágicos en el cuerpo del código. Los desplazamientos de registros de hardware, las máscaras de bits y los códigos de estado deben ser constantes con nombre en la cabecera `<driver>reg.h`. Esto hace que el código sea legible y también facilita corregir las definiciones de registros cuando, inevitablemente, descubres que algo no era del todo correcto.

### Uso de `tools/build/checkstyle9.pl`

El proyecto incluye un verificador de estilo automatizado en
`/usr/src/tools/build/checkstyle9.pl`. Es un script de Perl que
lee los archivos fuente y advierte sobre las infracciones de estilo
más habituales. No es perfecto, y algunas advertencias serán falsos
positivos o reflejarán convenciones que el script no interpreta del
todo bien, pero detecta una gran proporción de los errores sencillos.

Ejecútalo contra tu driver antes de enviarlo:

```sh
/usr/src/tools/build/checkstyle9.pl sys/dev/mydev/mydev.c
```

Verás una salida similar a esta:

```text
mydev.c:23: missing blank line after variable declarations
mydev.c:57: spaces not tabs at start of line
mydev.c:91: return value not parenthesised
```

Corrige cada advertencia. Vuelve a ejecutarlo. Repite hasta que la
salida esté limpia.

El archivo `CONTRIBUTING.md` es explícito al respecto: «Ejecuta
`tools/build/checkstyle9.pl` en tu rama de Git y elimina todos los
errores.» Los revisores no quieren hacer de verificadores de estilo
por ti. Enviar código que no ha pasado por `checkstyle9.pl` les hace
perder el tiempo.

### Uso cuidadoso de `indent(1)`

FreeBSD también incluye `indent(1)`, un reformateador de código fuente
en C. Puede reformatear un archivo para que se ajuste a partes de
`style(9)` de forma automática. Es útil, pero no es mágico.
`indent(1)` gestiona bien algunas reglas de estilo, como la
indentación basada en tabulaciones y la colocación de llaves, pero
maneja otras reglas deficientemente o no las trata en absoluto, y en
algunos casos empeora las cosas al reformatear comentarios o firmas
de función de formas que violan `style(9)` aunque la entrada fuera
correcta.

Trata `indent(1)` como un primer paso aproximado, no como un
formateador canónico. Ejecútalo sobre un archivo para aproximarte
al cumplimiento, luego lee el resultado con atención y corrige lo
que haya quedado mal. No lo ejecutes sobre archivos ya existentes
en el árbol como parte de un parche no relacionado; mezclar cambios
de estilo con cambios funcionales es un antipatrón en el proceso de
revisión.

### Mensajes de commit

Un buen mensaje de commit hace dos cosas. Primero, le dice al lector
de un vistazo qué hace el commit. Segundo, le explica con más detalle
por qué lo hace. El asunto es lo primero; el cuerpo es lo segundo.

Las convenciones del asunto en el árbol de FreeBSD tienen esta forma:

```text
subsystem: Short description of the change
```

El prefijo `subsystem` le indica al lector qué parte del árbol está
afectada. Para el envío de un driver, el prefijo es normalmente el
nombre del driver:

```text
mydev: Add driver for MyDevice FC100
```

La primera palabra tras los dos puntos va en mayúscula, y la
descripción es una frase fragmentaria, no una oración completa. El
asunto se limita a unos 50 caracteres, con 72 como límite estricto.
Observa los commits recientes del árbol con `git log --oneline` para
ver el patrón:

```text
rge: add disable_aspm tunable for PCIe power management
asmc: add automatic voltage/current/power/ambient sensor detection
tcp: use RFC 6191 for connection recycling in TIME-WAIT
pf: include all elements when hashing rules
```

El cuerpo del mensaje de commit viene después de una línea en blanco.
Explica el cambio con más detalle: qué hace el cambio, por qué es
necesario, a qué hardware o escenario afecta, y cualquier
consideración que pueda necesitar un lector futuro. Ajusta el cuerpo
a 72 columnas.

Un buen mensaje de commit para el envío de un driver podría tener
este aspecto:

```text
mydev: Add driver for FooCorp FC100 sensor board

This driver supports the FooCorp FC100 series of PCI-attached
environmental sensor boards, which expose a simple command and
status interface over a single BAR.  The driver implements
probe/attach/detach following the Newbus conventions, exposes a
character device for userland communication, and supports
sysctl-driven sampling configuration.

The FC100 is documented in the FooCorp Programmer's Reference
Manual version 1.4, which the maintainer has on file.  Tested on
amd64 and arm64 against a hardware sample; no errata were
observed during the test period.

Reviewed by:	someone@FreeBSD.org
MFC after:	2 weeks
```

Varias partes de ese mensaje son estándar. `Reviewed by:` indica el
committer que dio su visto bueno en la revisión. `MFC after:` sugiere
un período antes de que el commit pueda fusionarse de CURRENT a STABLE
(MFC son las siglas de Merge From Current). Como colaborador, no
rellenas estas líneas tú mismo; las añadirá el committer que incorpore
tu parche.

Lo que sí escribes tú es el cuerpo: los párrafos descriptivos que
explican el cambio. Escríbelos como si los estuvieras redactando para
un lector futuro que verá el commit en `git log` dentro de cinco años
y querrá saber de qué se trataba. Ese lector puedes ser tú, o alguien
que mantenga tu driver cuando tú ya hayas pasado a otra cosa. Sé
amable con ese lector en el mensaje de commit.

### Signed-off-by y el Developer Certificate of Origin

En particular para los pull requests de GitHub, el archivo
`CONTRIBUTING.md` pide que los commits incluyan una línea
`Signed-off-by:`. Esta línea certifica el Developer Certificate of
Origin en `https://developercertificate.org/`, que en términos
sencillos es una declaración de que tienes el derecho de contribuir
el código bajo la licencia del proyecto.

Añadir un `Signed-off-by:` es sencillo:

```sh
git commit -s
```

El indicador `-s` añade una línea de la forma:

```text
Signed-off-by: Your Name <you@example.com>
```

al mensaje de commit. Usa el mismo nombre y correo electrónico que
empleas en la línea de autor del commit.

### Cómo debe verse un árbol listo para enviar

Después de todo esto, el árbol de tu driver dentro del árbol de
código fuente de FreeBSD debería tener un aspecto similar a este:

```text
/usr/src/sys/dev/mydev/
	mydev.c
	mydev.h              (optional)
	mydevreg.h           (optional but recommended)

/usr/src/sys/modules/mydev/
	Makefile

/usr/src/share/man/man4/
	mydev.4
```

Y deberías poder construir el módulo con:

```sh
cd /usr/src/sys/modules/mydev
make obj
make depend
make
```

Y validar la página de manual con:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

Y ejecutar el verificador de estilo con:

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

Si los tres completan sin errores, tu driver está mecánicamente listo
para enviarse. Todavía quedan por tratar la licencia, el contenido de
la página de manual, las pruebas y la generación del parche, que
abordaremos en las siguientes secciones. Pero la estructura básica ya
está en su lugar, y un revisor que abra el parche encontrará que los
nombres de archivo, las disposiciones de archivos, el estilo y la
integración de build coinciden con lo que espera ver en el árbol.

### Errores habituales en la preparación de la sección 2

Antes de cerrar esta sección, recojamos los errores de preparación
más comunes que cometen los colaboradores por primera vez. Trátalo
como una rápida comprobación propia antes de pasar a la sección 3.

- Archivos en la ubicación incorrecta. El driver vive bajo
  `/usr/src/sys/dev/<driver>/`, no en la raíz de `/usr/src/sys/`.
  El Makefile del módulo vive bajo `/usr/src/sys/modules/<driver>/`.
  La página de manual vive bajo `/usr/src/share/man/man4/`.
- Nombres de archivo que no coinciden con el driver. Si el driver es
  `mydev`, el archivo fuente principal es `mydev.c`, no `main.c` ni
  `driver.c`.
- Cabecera de copyright ausente o incorrecta. La cabecera usa `/*-`
  como marcador de apertura, el identificador SPDX va primero, y el
  texto de licencia corresponde a una de las licencias aceptadas por
  el proyecto.
- Espacios en lugar de tabulaciones. `style(9)` es explícito respecto
  a las tabulaciones, y el verificador de estilo marcará la
  indentación con espacios de inmediato.
- Paréntesis ausentes en las expresiones `return`. Un error pequeño
  pero recurrente que el verificador de estilo detectará.
- Línea en blanco ausente entre las declaraciones de variables y el
  código. Otra convención menor que el verificador de estilo detecta.
- Mensaje de commit que no sigue la forma `subsystem: Short
  description`. El revisor te pedirá que lo reescribas.
- Espacios en blanco al final de línea. El archivo `CONTRIBUTING.md`
  señala explícitamente los espacios en blanco al final de línea como
  algo que los revisores rechazan.
- Makefile que tiene `/usr/src` codificado directamente en lugar de
  usar `${SRCTOP}`.

Cada uno de estos errores es fácil de corregir cuando sabes dónde
buscarlos. Cada uno de ellos añade una ronda más de ida y vuelta en
la revisión cuando no lo haces. El objetivo de esta sección era
darte el conocimiento para detectarlos todos antes de enviar.

### Cerrando la sección 2

Preparar un driver para enviarlo tiene más que ver con la atención
al detalle que con la brillantez. La disposición de archivos, el
estilo, la cabecera de copyright, el Makefile, el mensaje de commit:
cada uno de ellos tiene una forma convencional, y un driver cuyos
archivos se ajustan a esas convenciones es un driver cuya primera
impresión en el revisor es «esto tiene buen aspecto». Esa primera
impresión vale más que cualquier otro factor individual a la hora de
determinar cuántas rondas de revisión necesitará el parche.

Todavía no hemos hablado en detalle sobre la licencia en sí, ni sobre
la página de manual, ni sobre las pruebas. Esos son los temas de las
tres secciones siguientes. Pero la preparación mecánica del árbol de
código fuente, que es donde los colaboradores noveles tropiezan con
más frecuencia, está ya cubierta.

Pasemos ahora a la licencia y las consideraciones legales que enmarcan
toda contribución a FreeBSD.

## Sección 3: Licencias y consideraciones legales

### Por qué las licencias importan desde el principio

La forma más fácil de que rechacen tu driver es usar la licencia
incorrecta. Las licencias no son una preferencia de procedimiento en
FreeBSD; son la base del funcionamiento del proyecto. El sistema
operativo FreeBSD se distribuye bajo una combinación de licencias
permisivas en las que los usuarios pueden confiar sin sorpresas. Una
contribución que lleve una licencia incompatible, una licencia poco
clara, o ninguna licencia en absoluto, no puede aceptarse en el árbol,
por excelente que sea el código en todos los demás aspectos.

No se trata de formalismo legal por sí mismo. Es una necesidad
práctica. FreeBSD se utiliza en muchos entornos, incluidos productos
comerciales que llegan a millones de usuarios. Esos usuarios se basan
en la licencia del proyecto para entender sus obligaciones. Un único
archivo en el árbol con una licencia inesperada podría exponer a todos
los usuarios finales del proyecto a obligaciones para las que no se
inscribieron. El proyecto no puede aceptar ese riesgo.

Para ti, como colaborador, la conclusión práctica es esta: define la
licencia correcta desde el principio. Es mucho más fácil, por un
margen amplio, que intentar corregirla después de que el proceso de
revisión la haya señalado. Esta sección recorre lo que el proyecto
acepta, lo que no acepta, y cómo estructurar la cabecera de copyright
para que tu envío supere sin problemas la verificación de licencias.

### Qué licencias acepta FreeBSD

El proyecto FreeBSD prefiere, como opción predeterminada, la licencia
BSD de dos cláusulas, que se escribe habitualmente como BSD-2-Clause.
Es la licencia permisiva bajo la que se distribuye la mayor parte del
propio FreeBSD, y es la recomendación predeterminada para el código
nuevo. BSD-2-Clause permite la redistribución en forma de código fuente
y binario, con o sin modificaciones, siempre que se conserven el aviso
de copyright y el texto de la licencia. No impone ningún requisito a
los usuarios finales de distribuir su código fuente, ninguna exigencia
de avisos de compatibilidad, y ninguna cláusula de concesión de
patentes que pueda complicar el uso comercial.

La licencia BSD de tres cláusulas, BSD-3-Clause, también es aceptada.
Añade una cláusula que prohíbe el uso del nombre del autor en avales
o respaldos. Parte del código más antiguo de FreeBSD usa esta forma,
y es equivalente para la mayoría de los propósitos prácticos.

Unas pocas licencias permisivas más aparecen en el árbol para archivos
concretos que fueron contribuidos bajo ellas históricamente. Las
licencias de estilo MIT y la licencia ISC aparecen en algunos lugares.
La licencia Beerware, una licencia permisiva extravagante introducida
por Poul-Henning Kamp, también aparece en algunos archivos como
`/usr/src/sys/dev/led/led.c`. Estas licencias son compatibles con el
esquema general de licencias de FreeBSD y se aceptan cuando acompañan
a código concreto contribuido bajo ellas.

Para un driver nuevo que escribas tú mismo, el valor predeterminado
correcto es BSD-2-Clause. A menos que tengas una razón concreta para
usar una licencia diferente, usa BSD-2-Clause. Es la licencia que
esperarán tus revisores, y cualquier desviación de ella provocará una
conversación que probablemente no quieres tener en un primer envío.

### Qué licencias no acepta FreeBSD

Varias licencias no son compatibles con el árbol de código fuente de
FreeBSD y el código bajo ellas no puede fusionarse. Las más comunes
que los colaboradores noveles intentan usar a veces son:

- La GNU General Public Licence (GPL), en cualquier versión. El código bajo GPL no es compatible con el modelo de licencias de FreeBSD porque impone obligaciones de distribución del código fuente a los usuarios posteriores que el resto del árbol no contempla. FreeBSD sí incluye algunos componentes bajo GPL en el userland, como el GNU Compiler Collection, pero estos son casos históricos concretos y no sirven como modelo para nuevas contribuciones. El código de drivers, en particular, no se acepta bajo GPL.
- La Lesser GPL (LGPL). El mismo razonamiento que para la GPL.
- La Apache Licence, versión 2 o cualquier otra, salvo que exista una discusión y aprobación específica. La Apache Licence incluye una cláusula de cesión de patentes que interactúa de manera compleja con las licencias BSD permisivas. Parte del código bajo Apache Licence se acepta en contextos específicos, pero no es la opción por defecto para código nuevo.
- La licencia MIT en sus distintas variantes, que aunque técnicamente permisiva, no es la primera elección para FreeBSD. Si tienes una razón concreta para usar MIT, consúltalo con un revisor antes de enviar la contribución.
- Cualquier cosa propietaria. El árbol no puede aceptar código cuya licencia restrinja la redistribución o la modificación.
- Código con licencia incierta, lo que incluye código copiado de otros proyectos cuya licencia no se conoce, código generado por herramientas cuyos términos de licencia no están claros, y código aportado sin una declaración de licencia explícita.

Si estás portando o adaptando código de otro proyecto de código abierto, revisa la licencia del proyecto de origen con cuidado antes de comenzar. Incorporar código procedente de un proyecto bajo GPL a tu driver, aunque sea una sola función pequeña, contamina el driver y le impide entrar en el árbol de FreeBSD.

### La cabecera de copyright en detalle

La cabecera de copyright que aparece al principio de cada archivo fuente del árbol tiene una estructura específica, documentada en `style(9)`. Vamos a recorrer una cabecera completa y examinar cada una de sus partes.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
```

La apertura `/*-` no es una errata. El guion tras el asterisco es significativo. Un script automatizado del árbol recopila información de licencias de los archivos buscando comentarios multilínea que empiecen en la columna 1 con la secuencia `/*-`. Usar `/*-` marca el bloque como licencia; usar simplemente `/*` no lo hace. `style(9)` es explícito al respecto: si quieres que el recolector de licencias del árbol capture correctamente tu licencia, usa `/*-` en la línea de apertura.

Inmediatamente después de la apertura aparece la línea SPDX-License-Identifier. SPDX es un vocabulario estandarizado para describir licencias en forma legible por máquinas. Esta línea indica al recolector bajo qué licencia está el archivo, y lo hace de una forma que no puede interpretarse erróneamente. Usa `BSD-2-Clause` para una licencia BSD de dos cláusulas y `BSD-3-Clause` para una de tres cláusulas. Para otras licencias, consulta la lista de identificadores SPDX en `https://spdx.org/licenses/`. No inventes identificadores.

La línea de copyright indica el año y el titular de los derechos de autor. Usa tu nombre legal completo, o el nombre de tu empleador si estás contribuyendo trabajo realizado en el marco de una relación laboral, seguido de una dirección de correo electrónico lo suficientemente estable como para que puedan localizarte años después. Si contribuyes como particular, usa tu correo personal en lugar de una dirección temporal.

Pueden aparecer varias líneas de copyright si el archivo ha tenido varios autores. Cuando añadas una línea de copyright, agrégala al final de la lista existente, no al principio. No elimines la línea de copyright de nadie. Las atribuciones existentes tienen relevancia legal.

El propio texto de la licencia sigue a continuación. El texto reproducido arriba es el texto estándar de BSD-2-Clause. No lo modifiques. El redactado es jurídicamente específico, y cambiarlo, aunque el resultado parezca más claro, puede hacer que la licencia sea legalmente distinta de lo que el proyecto acepta.

Por último, hay una línea en blanco después del cierre `*/`, antes de que comience el código. Esta línea en blanco es una convención del árbol y aparece mencionada en `style(9)`. Su propósito es puramente visual.

### Leer cabeceras existentes para desarrollar intuición

La mejor manera de interiorizar las convenciones de la cabecera de licencia es examinar cabeceras reales del árbol. Abre `/usr/src/sys/dev/null/null.c` y lee su cabecera. Abre `/usr/src/sys/dev/led/led.c` y lee la suya (que está bajo la licencia Beerware, un caso inusual pero aceptado). Abre un par de drivers de red en `/usr/src/sys/dev/re/` o `/usr/src/sys/dev/e1000/` y lee las suyas. En quince minutos habrás asimilado el patrón.

Algunas cosas que observarás:

- Algunos archivos más antiguos del árbol no tienen identificadores SPDX. Son anteriores a la convención SPDX. Para las nuevas contribuciones, usa SPDX.
- Algunos archivos antiguos todavía contienen una etiqueta `$FreeBSD$` cerca de la parte superior. Era un marcador de la era CVS que ya no está activo desde que el proyecto migró a git. Las nuevas contribuciones no incluyen etiquetas `$FreeBSD$`.
- Algunos archivos tienen varias líneas de copyright que abarcan múltiples colaboradores a lo largo de los años. Esto es normal y correcto. Cuando añadas una línea de copyright a un archivo existente, agrégala al final.
- Unos pocos archivos tienen licencias no estándar (Beerware, estilo MIT, ISC). Son históricas y se aceptan caso por caso. No las uses como plantillas para nuevas contribuciones.

### Obras derivadas y código externo

Si tu driver es enteramente trabajo propio, la cabecera es sencilla. Si incluye código derivado de otro proyecto, la situación es más compleja.

Cualquier código que copies o adaptes de otro proyecto lleva consigo la licencia de ese proyecto. Si la licencia del proyecto es compatible con BSD, puedes usar el código, pero debes conservar el aviso de copyright original y hacer visible la adaptación. Si la licencia no es compatible con BSD, como ocurre con GPL, no puedes usar el código en absoluto.

La convención del árbol para las obras derivadas es conservar la línea de copyright original y añadir la tuya como una línea separada:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1998 Original Author <original@example.com>
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * [licence text]
 */
```

Si la licencia original es BSD-3-Clause y tú contribuyes tus añadidos bajo BSD-2-Clause, el archivo es efectivamente BSD-3-Clause en su conjunto, porque el requisito de tres cláusulas se traslada a las obras derivadas. Usa la más restrictiva de las dos licencias en el identificador SPDX, o mantén licencias separadas a nivel de sección si el código es claramente separable. Ante cualquier duda, consulta con un revisor.

Si el código procede de una fuente externa específica cuyo origen es relevante, indícalo en un comentario junto a la función correspondiente:

```c
/*
 * Adapted from the NetBSD driver at
 * src/sys/dev/foo/foo.c, revision 1.23.
 */
```

Esto ayuda a los revisores y a los futuros responsables de mantenimiento a comprender la procedencia del código. También facilita que cualquiera pueda rastrear los errores hasta su corrección en upstream.

### Código adaptado de fuentes de proveedores

Un escenario habitual en los drivers de hardware es que el fabricante proporcione código de ejemplo o un driver de referencia bajo alguna licencia. Si el código del fabricante está bajo una licencia compatible con BSD, es posible que puedas utilizarlo directamente, con las adaptaciones necesarias, conservando el copyright del fabricante. Lee con atención la licencia del fabricante. Si esa licencia no es compatible con BSD, no puedes usar el código del fabricante en un driver destinado al árbol. Es posible que puedas usar la documentación del fabricante como referencia para implementar el driver de forma independiente, pero no puedes tomar el código directamente.

Si el fabricante proporciona documentación bajo un acuerdo de no divulgación (NDA), la situación es aún más delicada. Un NDA generalmente te prohíbe revelar la documentación. Puede que no te prohíba usar esa documentación para escribir código, pero el código resultante debe ser trabajo tuyo, no una copia de ningún código que haya proporcionado el fabricante. Sé escrupuloso al mantener esta distinción clara. Si hay alguna duda, no procedas sin asesoramiento jurídico.

### Código que no has escrito tú pero que estás enviando

Si estás enviando código que ha escrito otra persona, como la contribución de un compañero, necesitas su permiso explícito y su línea de copyright en la cabecera. No puedes contribuir código en nombre de otra persona sin su conocimiento. La línea `Signed-off-by:` que el proyecto solicita es, en parte, un mecanismo para registrar esto; el Developer Certificate of Origin que certifica esa línea incluye una declaración de que tienes derecho a contribuir el código.

Si eres empleado de una empresa y contribuyes trabajo realizado en el marco de esa relación laboral, el copyright lo tiene habitualmente tu empleador, no tú. La línea de copyright debe indicar el nombre del empleador. Muchas empresas tienen procesos internos para aprobar contribuciones al software de código abierto; síguelos antes de enviar. Algunas empresas prefieren que sus empleados firmen un acuerdo de licencia de colaborador (CLA) con la FreeBSD Foundation para mayor claridad; si la tuya lo hace, coordínate con tu empresa antes de enviar.

### Añadir cabeceras de licencia a un driver existente

Si estás adaptando un driver que ya has escrito pero que nunca preparaste para su envío, tendrás que añadir la cabecera adecuada a cada archivo. Los pasos son:

1. Decide la licencia. Para un nuevo driver, usa BSD-2-Clause.
2. Escribe la línea del identificador SPDX.
3. Escribe la línea de copyright con tu nombre, correo electrónico y el año de la primera creación.
4. Pega el texto estándar de la licencia BSD-2-Clause.
5. Verifica que la apertura sea `/*-` y que el archivo empiece en la columna 1.
6. Verifica que haya una sola línea en blanco después del cierre `*/`.
7. Repite el proceso para cada archivo: los archivos `.c`, los archivos `.h`, la página de manual (donde la licencia aparece como comentarios estilo `.\" -` en lugar del estilo `/*-`) y cualquier otro archivo que contenga contenido sustancial.

En el caso del Makefile, como se indicó anteriormente, la cabecera de licencia se omite convencionalmente en los archivos triviales. El Makefile del módulo que se mostró en la sección 2 es suficientemente trivial como para que no sea necesaria ninguna cabecera.

### Validar la cabecera

No existe una única herramienta automatizada que valide todos los aspectos de una cabecera de copyright de FreeBSD. El script `checkstyle9.pl` detecta algunos tipos de errores de formato relacionados con la cabecera. El recolector de licencias del árbol trabaja con el marcador `/*-` y la línea SPDX. La validación más fiable, sin embargo, consiste en comparar tu cabecera directamente con una cabecera de referencia conocida de un commit reciente del árbol, como la cabecera de `/usr/src/sys/dev/null/null.c` o la de cualquier driver añadido recientemente.

Adquiere un pequeño hábito: cuando abras un nuevo archivo fuente, pega una cabecera conocida y correcta como primera acción. Esto evita el error fácil de olvidar la cabecera por completo y garantiza además que la estructura sea correcta desde el principio.

### Cerrando la sección 3

La gestión de licencias es uno de los aspectos en los que hacerlo bien desde el principio ahorra una cantidad enorme de tiempo. El proyecto FreeBSD acepta BSD-2-Clause, BSD-3-Clause y algunas otras licencias permisivas para archivos históricos. Las nuevas contribuciones deben usar BSD-2-Clause por defecto. La cabecera de copyright tiene una forma específica: abre con `/*-`, seguida de un identificador SPDX, seguida de una o más líneas de copyright, seguida del texto estándar de la licencia. El código derivado de otros proyectos mantiene sus obligaciones de licencia originales, y las obras derivadas deben conservar las atribuciones originales. El código que no has escrito tú requiere el permiso y la atribución del autor.

Con el aspecto legal resuelto, podemos pasar a la página de manual. Cada driver del árbol se acompaña de una página de manual, y escribir una buena es uno de los puntos donde los colaboradores por primera vez subestiman con más frecuencia el esfuerzo necesario. La sección 4 recorre las convenciones y proporciona una plantilla que puedes adaptar.

## Sección 4: Cómo escribir una página de manual para tu driver

### Por qué importa la página de manual

La página de manual es la mitad del driver orientada al usuario. Cuando alguien encuentre tu driver en el árbol y quiera saber qué hace, no leerá el código fuente. Ejecutará `man 4 mydev`. Lo que vea será, para la mayoría de ellos, la única documentación que tendrán nunca de tu driver. Si la página de manual es clara, completa y está bien organizada, los usuarios entenderán qué soporta el driver, cómo usarlo y cuáles son sus limitaciones. Si la página de manual falta, es escasa o está mal organizada, los usuarios se confundirán, abrirán informes de errores que en realidad son problemas de documentación y, comprensiblemente, se formarán una impresión negativa del driver.

Desde el punto de vista del proyecto, la página de manual es un artefacto de primera clase de la contribución. Un driver sin página de manual no puede fusionarse. Un driver con una página de manual deficiente quedará retenido en revisión hasta que la página alcance el estándar requerido. Debes pensar en la página de manual como parte del driver, no como algo que se añade al final.

Desde un punto de vista práctico, escribir la página de manual es en sí mismo una disciplina útil. El acto de explicar a un usuario qué hace el driver, qué hardware soporta, qué parámetros ajustables expone y cuáles son sus limitaciones conocidas te obliga a articular esas cosas con claridad. No es infrecuente que una página de manual bien escrita ponga de manifiesto preguntas que el diseño del driver todavía no había resuelto. Escribir la página de manual forma, por tanto, parte del trabajo de terminar el driver, no es un paso posterior a que el driver esté listo.

### Las secciones de las páginas de manual: una orientación rápida

Las páginas de manual de FreeBSD están organizadas en secciones numeradas. Las secciones son:

- Sección 1: Comandos de usuario generales.
- Sección 2: Llamadas al sistema.
- Sección 3: Llamadas a biblioteca.
- Sección 4: Interfaces del kernel (dispositivos, drivers de dispositivo).
- Sección 5: Formatos de archivo.
- Sección 6: Juegos.
- Sección 7: Miscelánea y convenciones.
- Sección 8: Administración del sistema y comandos privilegiados.
- Sección 9: Componentes internos del kernel (APIs y subsistemas).

Tu driver pertenece a la sección 4. El archivo de la página de manual va en `/usr/src/share/man/man4/` y se llama convencionalmente `<driver>.4`, por ejemplo `mydev.4`. El sufijo `.4` es la convención de las páginas de manual; marca el archivo como una página de la sección 4.

El archivo en sí está escrito en el lenguaje de macros mdoc, no en texto plano. Mdoc es un conjunto de macros estructurado que produce páginas de manual formateadas a partir de un archivo fuente más o menos legible por personas. El estilo del proyecto para mdoc está documentado en `/usr/src/share/man/man5/style.mdoc.5`; deberías leer ese archivo antes de escribir tu primera página de manual, aunque gran parte de lo que dice tendrá más sentido después de que hayas intentado escribir una.

### La estructura de una página de manual de la sección 4

Una página de manual de la sección 4 tiene una estructura bien establecida. Las siguientes secciones aparecen más o menos en este orden:

1. `NAME`: El nombre del driver y una descripción de una línea.
2. `SYNOPSIS`: Cómo incluir el driver en el kernel o cargarlo como módulo.
3. `DESCRIPTION`: Lo que hace el driver, en prosa.
4. `HARDWARE`: La lista de hardware que el driver es compatible. Esta sección es obligatoria en las páginas de la sección 4 y se incorpora textualmente en las Release Hardware Notes.
5. `LOADER TUNABLES`, `SYSCTL VARIABLES`: Si el driver expone ajustes configurables, documéntalos aquí.
6. `FILES`: Los nodos de dispositivo y cualquier archivo de configuración.
7. `EXAMPLES`: Ejemplos de uso, cuando sea relevante.
8. `DIAGNOSTICS`: Explicaciones de los mensajes de registro del driver.
9. `SEE ALSO`: Referencias cruzadas a páginas de manual y documentos relacionados.
10. `HISTORY`: Cuándo apareció el driver por primera vez.
11. `AUTHORS`: El autor o autores principales del driver.
12. `BUGS`: Problemas conocidos y limitaciones.

No todas las secciones son obligatorias para todos los drivers. Para un driver sencillo, `NAME`, `DESCRIPTION`, `HARDWARE`, `SEE ALSO` e `HISTORY` son el mínimo. Para un driver más complejo, añade las demás según corresponda.

### Una página de manual mínima y funcional

A continuación se muestra una página de manual de la sección 4 completa y funcional para un driver hipotético llamado `mydev`. Guárdala como `mydev.4`, ejecútala con `mandoc -Tlint` y verás que pasa sin errores. Este es el tipo de página de manual que puedes adaptar a tu propio driver.

```text
.\"-
.\" SPDX-License-Identifier: BSD-2-Clause
.\"
.\" Copyright (c) 2026 Your Name <you@example.com>
.\"
.\" Redistribution and use in source and binary forms, with or
.\" without modification, are permitted provided that the
.\" following conditions are met:
.\" 1. Redistributions of source code must retain the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer.
.\" 2. Redistributions in binary form must reproduce the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer in the documentation and/or other materials
.\"    provided with the distribution.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
.\" EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
.\" THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
.\" PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
.\" AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
.\" SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
.\" NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
.\" LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
.\" CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
.\" OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
.\" EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.\"
.Dd April 20, 2026
.Dt MYDEV 4
.Os
.Sh NAME
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
.Sh SYNOPSIS
To compile this driver into the kernel,
place the following line in your
kernel configuration file:
.Bd -ragged -offset indent
.Cd "device mydev"
.Ed
.Pp
Alternatively, to load the driver as a
module at boot time, place the following line in
.Xr loader.conf 5 :
.Bd -literal -offset indent
mydev_load="YES"
.Ed
.Sh DESCRIPTION
The
.Nm
driver provides support for FooCorp FC100 series PCI-attached
environmental sensor boards.
It exposes a character device at
.Pa /dev/mydev0
that userland programs can open, read, and write using standard
system calls.
.Pp
Each attached board is enumerated with an integer unit number
beginning at 0.
The driver supports probe, attach, and detach through the
standard Newbus framework.
.Sh HARDWARE
The
.Nm
driver supports the following hardware:
.Pp
.Bl -bullet -compact
.It
FooCorp FC100 rev 1.0
.It
FooCorp FC100 rev 1.1
.It
FooCorp FC200 (compatibility mode)
.El
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.El
.Sh SEE ALSO
.Xr pci 4
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
.Sh AUTHORS
.An -nosplit
The
.Nm
driver and this manual page were written by
.An Your Name Aq Mt you@example.com .
```

Esa página de manual es una página de la sección 4 completa y válida. Es breve porque el driver hipotético es simple. Un driver más complejo tendría secciones `DESCRIPTION`, `HARDWARE` y, posiblemente, `LOADER TUNABLES`, `SYSCTL VARIABLES`, `DIAGNOSTICS` y `BUGS` más extensas. Pero el esqueleto es el mismo.

Repasemos las partes que con más frecuencia se malinterpretan.

### El bloque de cabecera

El bloque de cabecera al principio es un conjunto de líneas de comentario que comienzan con `.\"`. Son comentarios de mdoc. No aparecen en la salida de la página de manual. Su propósito es contener la cabecera de copyright y cualquier nota para futuros editores.

El marcador de apertura es `.\"-` con un guion, equivalente al `/*-` de los archivos C. La herramienta de recopilación de licencias lo reconoce.

La macro `.Dd` establece la fecha del documento. Se formatea como mes, día y año con el nombre completo del mes. El estilo mdoc del proyecto dicta actualizar `.Dd` siempre que el contenido de la página de manual cambie de forma significativa. No actualices la fecha por cambios triviales como correcciones de espacios en blanco, pero sí hazlo ante cualquier cambio semántico.

La macro `.Dt` establece el título del documento. La convención es el nombre del driver en mayúsculas seguido del número de sección: `MYDEV 4`.

La macro `.Os` emite el identificador del sistema operativo en el pie de página. Úsala sin argumentos; mdoc completará el valor correcto a partir de las macros de compilación.

### La sección NAME

La macro `.Sh NAME` abre la sección NAME. El contenido es un par de macros:

```text
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
```

`.Nm` establece el nombre del elemento documentado. Una vez definido, `.Nm` sin argumentos en cualquier otra parte de la página se expande al nombre, lo que evita repetir el nombre del driver una y otra vez.

`.Nd` es la descripción breve, un fragmento de oración con la forma "driver for ..." o "API for ..." o "device for ...". No pongas en mayúscula la primera palabra ni añadas un punto al final.

### La sección SYNOPSIS

En un driver, la sección SYNOPSIS muestra normalmente dos cosas: cómo compilar el driver en el kernel como componente integrado y cómo cargarlo como módulo. La forma integrada usa `.Cd` para una línea de configuración del kernel. La forma cargable muestra la entrada `_load="YES"` para `loader.conf`.

Si el driver expone un archivo de cabecera que los programas del espacio de usuario deben incluir, o si expone una API similar a una biblioteca, la sección SYNOPSIS puede incluir también `.In` para la directiva de inclusión y `.Ft`/`.Fn` para los prototipos de función. Consulta `/usr/src/share/man/man4/led.4` para ver un ejemplo de página de manual de driver cuya sección SYNOPSIS muestra prototipos de función.

### La sección DESCRIPTION

La sección DESCRIPTION es donde explicas, en prosa, qué hace el driver. Escríbela pensando en un usuario que ha instalado FreeBSD, tiene el hardware delante y quiere saber qué ofrece el driver.

Mantén los párrafos centrados. Usa `.Pp` para separar párrafos. Usa `.Nm` para referirte al driver, no escribiendo su nombre directamente. Usa `.Pa` para rutas de archivo, `.Xr` para referencias cruzadas a otras páginas de manual, `.Ar` para nombres de argumento y `.Va` para nombres de variable.

Describe el comportamiento del driver, su ciclo de vida (probe, attach, detach), la estructura de sus nodos de dispositivo y cualquier concepto que el usuario deba entender antes de interactuar con él. No documentes aquí los detalles internos de implementación; el código fuente es el lugar adecuado para eso.

### La sección HARDWARE

La sección HARDWARE es obligatoria en las páginas de la sección 4. Es la sección que se incorpora textualmente en las Release Hardware Notes, el documento que los usuarios consultan para saber si su hardware es compatible.

A esta sección se aplican varias reglas específicas. Estas reglas están documentadas en `/usr/src/share/man/man5/style.mdoc.5`:

- La oración introductoria debe tener la forma: "The .Nm driver supports the following \<device class\>:" seguida de la lista.
- La lista debe ser una lista `.Bl -bullet -compact` con un modelo de hardware por entrada `.It`.
- Cada modelo debe identificarse con su nombre comercial oficial, no con un nombre interno de código ni con la revisión del chip.
- La lista debe incluir todo el hardware del que se sepa que funciona, incluidas las revisiones.
- La lista no debe incluir hardware del que se sepa que no funciona; esos casos pertenecen a la sección `BUGS`.

Para un driver completamente nuevo, la lista puede ser breve. No hay ningún problema. Para un driver que lleva un tiempo en el árbol de código fuente y ha acumulado soporte para muchas variantes de hardware, la lista crece con el tiempo a medida que se prueba cada nueva variante.

### La sección FILES

La sección FILES enumera los nodos de dispositivo y los archivos de configuración que usa el driver. Utiliza una lista `.Bl -tag` con entradas `.Pa` para los nombres de archivo. Por ejemplo:

```text
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.It Pa /dev/mydev1
Second unit of the driver.
.El
```

Mantén el valor de `.Bl -tag -width` lo suficientemente amplio como para acomodar la ruta más larga de la lista. Si los anchos no coinciden, la lista se renderizará incorrectamente.

### La sección SEE ALSO

La sección SEE ALSO incluye referencias cruzadas a páginas de manual relacionadas. Se escribe como una lista de referencias cruzadas `.Xr`, separadas por comas, ordenada primero por número de sección y después alfabéticamente dentro de cada sección:

```text
.Sh SEE ALSO
.Xr pci 4 ,
.Xr sysctl 8 ,
.Xr style 9
```

La sección SEE ALSO de un driver incluye habitualmente el bus al que se conecta (como `pci(4)`, `usb(4)` o `iicbus(4)`), cualquier herramienta del espacio de usuario que interactúe con él y cualquier API de la sección 9 que sea central para la implementación del driver.

### La sección HISTORY

La sección HISTORY indica cuándo apareció el driver por primera vez. Para un driver completamente nuevo que aparecerá por primera vez en la próxima versión, escribe el número de versión del lanzamiento como marcador de posición:

```text
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
```

El committer que integre tu parche verificará el número de versión según el calendario de lanzamientos y puede ajustarlo. No hay ningún problema con eso.

### La sección AUTHORS

La sección AUTHORS indica los autores principales del driver. Usa `.An -nosplit` al principio para indicar a mdoc que no divida la lista de autores entre líneas en los límites de los nombres. A continuación, usa `.An` para cada autor con `.Aq Mt` para la dirección de correo electrónico.

```text
.Sh AUTHORS
.An -nosplit
The
.Nm
driver was written by
.An Your Name Aq Mt you@example.com .
```

En un driver con varios autores, inclúyelos en orden de contribución, con el autor principal en primer lugar.

### Validar la página de manual

Una vez escrita la página de manual, valídala con `mandoc(1)`:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

`mandoc -Tlint` pasa la página por el analizador de mandoc en modo estricto y notifica cualquier problema estructural o semántico. Corrige todos los avisos. Una ejecución limpia de `mandoc -Tlint` es un requisito previo para el envío.

También puedes renderizar la página para ver su aspecto:

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

Lee la salida renderizada como lo haría un usuario. Si algo resulta difícil de leer, corrige el código fuente. Si una referencia cruzada se renderiza de una forma que no esperabas, comprueba el uso de las macros. Lee la salida al menos dos veces.

El proyecto también recomienda la herramienta `igor(1)`, disponible en el árbol de ports como `textproc/igor`. `igor` detecta problemas a nivel de prosa que `mandoc` no detecta, como espacios dobles, comillas sin cerrar y errores comunes de redacción. Instálala con `pkg install igor` y ejecútala en tu página:

```sh
igor /usr/src/share/man/man4/mydev.4
```

Corrige todos los avisos que genere.

### La regla de una oración por línea

Una convención importante en las páginas mdoc de FreeBSD es la regla de una oración por línea. Cada oración del código fuente de la página de manual comienza en una línea nueva, independientemente del ancho de línea. Esto no tiene que ver con el formato de visualización; mdoc reorganizará el texto para mostrarlo. Tiene que ver con la legibilidad del código fuente y con la forma en que `diff` muestra los cambios. Cuando los cambios están orientados a líneas, un diff de un cambio en la página de manual muestra qué oraciones cambiaron; cuando las oraciones abarcan varias líneas, el diff es más difícil de leer.

El archivo `CONTRIBUTING.md` es explícito al respecto:

> Please be sure to observe the one-sentence-per-line rule so
> manual pages properly render. Any semantic changes to the
> manual pages should bump the date.

En la práctica, esto significa que escribes:

```text
The driver supports the FC100 family.
It attaches through the standard PCI bus framework.
Each unit exposes a character device under /dev/mydev.
```

Y no:

```text
The driver supports the FC100 family. It attaches through the
standard PCI bus framework. Each unit exposes a character device
under /dev/mydev.
```

La primera forma es la convencional; la segunda no lo es.

### Errores frecuentes en páginas de manual

Varios errores se repiten en los primeros envíos:

- Ausencia de la sección HARDWARE. Las páginas de la sección 4 deben tenerla. Si tu driver aún no es compatible con ningún hardware (porque es un pseudo-dispositivo), documenta eso explícitamente.
- Punto al final de la descripción NAME. La descripción de `.Nd` debe ser un fragmento sin punto final.
- Encabezados de sección escritos incorrectamente. Los encabezados son canónicos. Usa `DESCRIPTION`, no `DESCRIPTIONS`. Usa `SEE ALSO`, no `See Also`. Usa `HISTORY`, no `History`.
- Párrafos con varias oraciones sin la separación `.Pp`. Usa `.Pp` entre párrafos en prosa.
- Olvidar actualizar `.Dd` al realizar cambios semánticos. Si modificas el contenido de la página de manual, actualiza la fecha.
- Usar `.Cm` o `.Nm` donde corresponde `.Ql` (cita literal) o texto sin formato.
- Pares `.Bl`/`.El` ausentes o mal formados. Toda lista debe abrirse y cerrarse correctamente.

Ejecutar `mandoc -Tlint` detecta la mayoría de estos errores. Ejecutar `igor` detecta algunos más. Leer la salida renderizada detecta el resto.

### Examinar páginas de manual reales

Antes de finalizar tu propia página de manual, dedica tiempo a leer páginas reales. Tres buenos modelos:

- `/usr/src/share/man/man4/null.4` es una página mínima. Ideal para ver la estructura básica.
- `/usr/src/share/man/man4/led.4` es una página algo más compleja que muestra la sección SYNOPSIS con prototipos de función.
- `/usr/src/share/man/man4/re.4` es una página de driver de red completa. Ideal para ver en funcionamiento las secciones HARDWARE, LOADER TUNABLES, SYSCTL VARIABLES, DIAGNOSTICS y BUGS.

Léelas todas. Ábrelas en `less`, lee la versión renderizada y luego abre el código fuente en un editor. Compara la salida renderizada con el código fuente. Verás cómo las macros producen el texto formateado y absorberás las convenciones de manera natural.

### Cerrando la sección 4

La página de manual no es algo secundario. Es un artefacto de primera clase que se incluye con tu driver y es la documentación principal orientada al usuario. Una buena página de manual tiene una estructura específica (NAME, SYNOPSIS, DESCRIPTION, HARDWARE, etc.), está escrita en mdoc, sigue la regla de una oración por línea y pasa `mandoc -Tlint` sin errores. La página merece el mismo cuidado que el código. Un driver con una página de manual deficiente quedará retenido en la revisión hasta que la página alcance el estándar exigido; un driver con una buena página superará esa parte de la revisión sin dificultad.

Con la licencia y la página de manual en mano, tienes cubierto todo el papeleo de un envío de driver. La siguiente sección aborda el aspecto técnico de las pruebas, porque un driver que compila sin errores y pasa las comprobaciones de estilo aún necesita construirse correctamente en todas las arquitecturas compatibles y funcionar de forma adecuada en diversas situaciones. La sección 5 repasa esas pruebas.

## Sección 5: Probar tu driver antes del envío

### Las pruebas que importan

Probar un driver antes de enviarlo no es una acción única. Es una secuencia de verificaciones, cada una de las cuales comprueba una propiedad diferente. Un driver que supera todas ellas es un driver sobre el que los revisores pueden centrarse en términos de diseño e intención, en lugar de tener que atender problemas mecánicos evitables. Un driver que omite alguna de ellas es un driver que presentará problemas evitables durante la revisión, y cada uno de esos problemas añade una vuelta al ciclo de revisión.

Las pruebas se dividen en varias categorías:

1. Pruebas de estilo de código, que verifican que el fuente se ajusta a `style(9)`.
2. Pruebas de página de manual, que verifican que el fuente mdoc es sintácticamente válido y se renderiza sin errores.
3. Pruebas de build local, que verifican que el driver compila como módulo del kernel cargable contra el árbol de código fuente actual de FreeBSD.
4. Pruebas en tiempo de ejecución, que verifican que el driver se carga, se adjunta a su dispositivo, gestiona una carga de trabajo básica y se desadjunta limpiamente.
5. Pruebas de build para múltiples arquitecturas, que verifican que el driver compila en cada arquitectura que el proyecto soporta.
6. Pruebas de lint y análisis estático, que detectan errores que el compilador no señala pero que son visibles para herramientas más agresivas.

Cada categoría tiene sus propias herramientas y su propio flujo de trabajo. Esta sección los recorre en orden.

### Pruebas de estilo de código

Ya vimos `tools/build/checkstyle9.pl` en la sección 2. Aquí ampliaremos su uso.

El script reside en `/usr/src/tools/build/checkstyle9.pl`. Es un programa Perl, por lo que se invoca como un script con Perl:

```sh
perl /usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

O bien, si el script es ejecutable y Perl está en su línea shebang, simplemente:

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

La salida es una lista de advertencias con números de línea. Las advertencias habituales incluyen:

- "space(s) before tab"
- "missing blank line after variable declarations"
- "unused variable"
- "return statement without parentheses"
- "function name is not followed by a newline"

Cada advertencia corresponde a una regla específica de `style(9)`. Corrige cada una. Vuelve a ejecutarlo. Repite hasta que la salida esté limpia.

Si discrepas con alguna advertencia, consulta primero `style(9)`. Es posible que el script genere falsos positivos, pero son poco frecuentes. La mayoría de las veces, una discrepancia con el comprobador de estilo refleja una comprensión incorrecta de `style(9)`. Lee la página de manual antes de discutir.

Ejecuta `checkstyle9.pl` en todos los archivos `.c` y `.h` de tu driver. El Makefile no necesita superarlo, ya que no es código C.

### Pruebas de página de manual

Para la página de manual, la prueba canónica es `mandoc -Tlint`:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

Corrige cada advertencia. Vuelve a ejecutarlo. Repite hasta que la salida esté limpia.

Además, ejecuta `igor` si lo tienes instalado:

```sh
igor /usr/src/share/man/man4/mydev.4
```

Y renderiza la página para leerla como lo haría un usuario:

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

También puedes instalar tu página en el sistema para una prueba más realista:

```sh
cp /usr/src/share/man/man4/mydev.4 /usr/share/man/man4/
makewhatis /usr/share/man
man 4 mydev
```

Esta última comprobación es útil porque verifica que `man` puede encontrar la página, que `apropos` puede encontrarla a través de `whatis`, y que la página se renderiza correctamente en el paginador estándar.

### Pruebas de build local

Antes de hacer cualquier otra cosa con el driver, verifica que compila. Desde el directorio del módulo:

```sh
cd /usr/src/sys/modules/mydev
make clean
make obj
make depend
make
```

La salida debe ser un único archivo `mydev.ko` en el directorio de objetos del módulo. Sin advertencias, sin errores. Si ves advertencias, corrígelas. `style(9)` indica que las advertencias no deben ignorarse; las contribuciones que las introducen quedan en espera de revisión.

Si estás ejecutando en la misma máquina donde cargarás el módulo, instálalo:

```sh
sudo make install
```

Esto copia `mydev.ko` a `/boot/modules/` para que `kldload` pueda encontrarlo.

### Pruebas en tiempo de ejecución

Una vez que el módulo está compilado e instalado, pruébalo:

```sh
sudo kldload mydev
dmesg | tail
```

La salida de `dmesg` debería mostrar tu driver realizando el probe, adjuntándose al hardware disponible y completando el attach sin errores. Si no hay hardware compatible, el driver simplemente no se adjuntará, lo cual está bien para la prueba de carga.

Ejercita el driver como lo haría un usuario. Abre sus nodos de dispositivo, lee y escribe en ellos, ejecuta las operaciones que soporta y observa si aparece algún mensaje de diagnóstico. Ejecútalo bajo carga. Ejecútalo con múltiples aperturas simultáneas. Ejecútalo con entradas en los límites. Este tipo de pruebas detecta errores que el compilador no puede ver.

Luego descárgalo:

```sh
sudo kldunload mydev
dmesg | tail
```

La descarga debe completarse en silencio, sin errores de "device busy" y sin panics. Si la descarga produce una advertencia sobre recursos ocupados, la ruta de detach del driver tiene una fuga; corrígela antes de enviarlo.

Repite el ciclo de carga/descarga varias veces. Un driver que se carga y descarga una vez no es lo mismo que un driver que se carga y descarga repetidamente. Los errores en la ruta de detach suelen aparecer solo en la segunda o tercera descarga, cuando el estado que queda de la primera descarga interfiere con la segunda carga.

### Pruebas de build para múltiples arquitecturas

FreeBSD soporta varias arquitecturas. Las activas a partir de FreeBSD 14.3 incluyen:

- `amd64` (x86 de 64 bits).
- `arm64` (ARM de 64 bits, también llamada aarch64).
- `i386` (x86 de 32 bits).
- `powerpc64` y `powerpc64le` (POWER).
- `riscv64` (RISC-V de 64 bits).
- `armv7` (ARM de 32 bits).

Un driver que compila en `amd64` puede o no compilar en todas las demás. Los problemas habituales en compilación cruzada incluyen:

- Suposiciones sobre el tamaño de los enteros. Un `long` ocupa 64 bits en `amd64` y `arm64`, pero 32 bits en `i386` y `armv7`. Si tu código asume `sizeof(long) == 8`, fallará en las arquitecturas de 32 bits. Usa `int64_t`, `uint64_t` o tipos de tamaño fijo similares cuando el tamaño importa.
- Suposiciones sobre el tamaño de los punteros. Del mismo modo, los punteros son de 64 bits en `amd64` y de 32 bits en `i386`. Las conversiones entre punteros y enteros requieren `intptr_t`/`uintptr_t`.
- Endianness. Algunas arquitecturas son little-endian, otras son big-endian y otras son configurables. Si tu driver lee o escribe datos en order de bytes de red, usa las macros explícitas de intercambio de bytes (`htonl`, `htons`, `bswap_32` y similares), no conversiones artesanales.
- Alineación. Algunas arquitecturas imponen una alineación estricta en las cargas de múltiples bytes. Usa `memcpy` o la API `bus_space(9)` en lugar de casts directos al acceder a los registros de hardware.
- Abstracciones de bus. La API `bus_space(9)` abstrae correctamente el acceso al hardware en todas las arquitecturas; el uso de casts inline `volatile *` no lo hace.

La mejor manera de detectar problemas entre arquitecturas es compilar el driver para cada una de ellas. Afortunadamente, FreeBSD dispone de un objetivo de build que hace exactamente eso:

```sh
cd /usr/src
make universe
```

`make universe` construye el mundo y el kernel para cada arquitectura soportada. El build completo puede tardar una hora o más dependiendo de la máquina, por lo que no es algo que ejecutes con cada cambio, pero es la prueba canónica previa a la contribución. El `Makefile` de `/usr/src/` lo describe así:

> `universe` - `Really` build everything (buildworld and all kernels on all architectures).

Si no quieres construir todo, puedes construir solo una arquitectura:

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

Esto es más rápido y suele ser suficiente para detectar los problemas habituales entre arquitecturas.

Para compilar solo tu módulo, a veces puedes hacer una compilación cruzada con:

```sh
cd /usr/src
make buildenv TARGET_ARCH=aarch64
cd sys/modules/mydev
make
```

Pero `make universe` y `make buildkernel TARGET=...` son las pruebas canónicas, y cualquier contribución seria debería superarlas.

### tinderbox: la variante de seguimiento de fallos de universe

Una variante de `make universe` es `make tinderbox`:

```sh
cd /usr/src
make tinderbox
```

Tinderbox es igual que universe, pero al final informa de la lista de arquitecturas que fallaron y termina con un error si alguna lo hizo. Para un flujo de trabajo de contribución, esto suele ser más útil que el simple `universe`, porque la lista de fallos es una tarea de acción clara.

### Ejecución de herramientas de lint del kernel

El build del kernel de FreeBSD ejecuta opcionalmente comprobaciones adicionales. La configuración del kernel `LINT` es un kernel compilado con todos los drivers y opciones activados, lo que pone de manifiesto problemas transversales que los kernels de una sola característica pasan por alto. Compilar el kernel LINT no suele ser necesario para una contribución de driver, pero es una comprobación útil si estás modificando algo de uso generalizado.

El propio `clang`, como compilador predeterminado de FreeBSD, realiza un análisis estático sofisticado durante la compilación normal. Compila con `WARNS=6` para ver el conjunto de advertencias más agresivo:

```sh
cd /usr/src/sys/modules/mydev
make WARNS=6
```

Y corrige cualquier advertencia que aparezca. Clang también dispone de una herramienta scan-build que ejecuta el análisis estático como un paso separado:

```sh
scan-build make
```

Instálala desde el árbol de Ports Collection (`devel/llvm`) si no está disponible.

### Pruebas en una máquina virtual

Gran parte de este capítulo asume que estás probando en una máquina real o en una máquina virtual. Las máquinas virtuales son especialmente útiles para las pruebas de drivers porque un panic no tiene más coste que un reinicio. Dos enfoques habituales:

- bhyve, el hipervisor nativo de FreeBSD. Un sistema invitado FreeBSD bajo bhyve puede ser un buen entorno de pruebas, en particular para drivers de red que usan `virtio`.
- QEMU. QEMU puede emular arquitecturas distintas a la del host, lo que lo hace útil para probar builds de múltiples arquitecturas en tiempo de ejecución sin necesitar hardware físico para cada arquitectura.

Para las pruebas en tiempo de ejecución con múltiples arquitecturas, QEMU con una imagen de FreeBSD en la arquitectura de destino es un buen flujo de trabajo. Compila el módulo para la arquitectura de destino, cópialo en la VM de QEMU y ejecuta `kldload` allí. Los cuelgues dentro de la VM no afectan al host.

### Pruebas contra HEAD

La rama `main` del árbol de código fuente de FreeBSD se denomina a veces HEAD, en el sentido de la ingeniería de versiones. Tu driver debería compilar y ejecutarse contra HEAD, porque es ahí donde tu parche se aplicará por primera vez. Si has estado desarrollando contra una rama más antigua, actualiza a HEAD antes de las pruebas finales:

```sh
cd /usr/src
git pull
```

Luego vuelve a compilar y a probar. Las API del kernel cambian; un driver que compilaba contra un árbol de hace seis meses puede necesitar pequeños ajustes para compilar contra HEAD actual.

### Un script de shell para todo el pipeline

Para una contribución seria, considera incluir la secuencia de pruebas en un script de shell. Los ejemplos complementarios incluyen uno, pero el esqueleto es sencillo:

```sh
#!/bin/sh
# pre-submission-test.sh
set -e

SRC=/usr/src/sys/dev/mydev
MOD=/usr/src/sys/modules/mydev
MAN=/usr/src/share/man/man4/mydev.4

echo "--- style check ---"
perl /usr/src/tools/build/checkstyle9.pl "$SRC"/*.c "$SRC"/*.h

echo "--- mandoc lint ---"
mandoc -Tlint "$MAN"

echo "--- local build ---"
(cd "$MOD" && make clean && make obj && make depend && make)

echo "--- load/unload cycle ---"
sudo kldload "$MOD"/mydev.ko
sudo kldunload mydev

echo "--- cross-architecture build (arm64) ---"
(cd /usr/src && make TARGET=arm64 buildkernel KERNCONF=GENERIC)

echo "all tests passed"
```

Ejecuta este script antes de cada contribución. Si termina limpio, tu driver ha superado todas las pruebas mecánicas. Los revisores podrán entonces centrarse en el diseño.

### Lo que las pruebas no detectan

Las pruebas te dicen que tu driver compila y que funciona en las situaciones que has probado. No te dicen que funcione en todas las situaciones. Un driver que supera todas las pruebas puede seguir teniendo errores que solo afloran bajo cargas de trabajo poco frecuentes, en hardware inusual o en combinaciones poco habituales del estado del kernel.

Esto es normal. El software nunca está completamente probado. El papel de las pruebas previas a la contribución no es demostrar que el driver es correcto, sino detectar los errores que son fáciles de detectar. Los errores de diseño, las condiciones de carrera poco frecuentes y las violaciones sutiles de protocolo seguirán llegando al árbol y serán detectados, a veces mucho después, por los usuarios que los encuentren. Para eso existe el mantenimiento posterior a la integración, y lo trataremos en la sección 8.

### Cerrando la sección 5

Las pruebas son un proceso de verificación de múltiples etapas. Las comprobaciones de estilo, los lints de páginas de manual, los builds locales, los ciclos de carga/descarga en tiempo de ejecución, los builds para múltiples arquitecturas y el análisis estático comprueban cada uno una propiedad diferente. Un driver que supera todos ellos es un driver listo para revisión. Las herramientas son estándar: `checkstyle9.pl`, `mandoc -Tlint`, `make`, `make universe`, `make tinderbox` y el análisis integrado de clang. La disciplina consiste en ejecutarlos en orden, corregir cada advertencia que produzcan y no enviar hasta que todos pasen limpiamente.

Con el driver probado, podemos pasar ahora a la mecánica de enviarlo para su revisión. La sección 6 recorre la generación de parches y el flujo de trabajo de contribución.

## Sección 6: envío de un parche para revisión

### Qué es un parche en el sentido de FreeBSD

Un parche, en el sentido de FreeBSD, es una unidad de cambio revisable. Puede ser un único commit o una serie de commits. Representa un cambio lógico en el árbol. Para una nueva entrega de un driver, el parche consiste habitualmente en uno o dos commits que introducen los nuevos archivos del driver, el Makefile del módulo y la nueva página de manual.

La forma mecánica de un parche es una representación textual de los cambios, normalmente en formato unified-diff. Existen varias formas de generar esa representación:

- `git diff` produce un diff entre dos commits o entre un commit y el árbol de trabajo.
- `git format-patch` genera un archivo de parche por commit, con todos los metadatos del commit incluidos, en un formato adecuado para enviarlo por correo electrónico o adjuntarlo a una revisión.
- `arc diff`, de las herramientas de línea de comandos de Phabricator, publica el estado de trabajo actual como una revisión de Phabricator.
- `gh pr create`, de las herramientas de línea de comandos de GitHub, abre una pull request en GitHub.

La herramienta adecuada depende de dónde vayas a enviar el parche. Para Phabricator, `arc diff` es lo estándar. Para GitHub, `gh pr create` o la interfaz web de GitHub es lo habitual. Para las listas de correo, `git format-patch` con `git send-email` es el método estándar.

Todas ellas se apoyan en el mismo commit de git subyacente. Antes de preocuparte por la herramienta de envío, asegúrate de que el commit en sí esté limpio.

### Preparación del commit

Comienza con un clon limpio y actualizado del árbol de código fuente de FreeBSD:

```sh
git clone https://git.FreeBSD.org/src.git /usr/src
```

O, si ya tienes un clon, actualízalo:

```sh
cd /usr/src
git fetch origin
git checkout main
git pull
```

Crea una rama temática para tu trabajo:

```sh
git checkout -b mydev-driver
```

Haz tus cambios: añade los archivos del driver, el Makefile del módulo
y la página de manual. Ejecuta todas las pruebas de la Sección 5. Corrige
cualquier problema.

Confirma tus cambios con un commit. El commit debe ser una única unidad
lógica de cambio. Si estás introduciendo un driver completamente nuevo, un
solo commit suele ser lo apropiado: "mydev: Add driver for FooCorp FC100
sensor boards." El mensaje del commit debe seguir las convenciones de la
Sección 2.

```sh
git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
git commit -s
```

La opción `-s` añade una línea `Signed-off-by:`. El editor se abre para el
mensaje del commit; rellena la línea de asunto y el cuerpo siguiendo las
convenciones de la Sección 2.

Revisa el commit:

```sh
git show HEAD
```

Lee cada línea. Comprueba que no se incluyen archivos no relacionados. Verifica
que no hay espacios en blanco al final de línea. Asegúrate de que el mensaje
del commit se lee bien. Si algo no está en orden, enmíendalo:

```sh
git commit --amend
```

Repite hasta que el commit sea exactamente como lo deseas.

### Generación de un parche para revisión

Una vez que el commit esté limpio, genera el parche. Para una revisión en
Phabricator, usa `arc diff`:

```sh
cd /usr/src
arc diff main
```

`arc` detectará que estás en una rama temática, generará el diff y abrirá
una revisión de Phabricator en tu navegador. Rellena el resumen, etiqueta
a los revisores si conoces alguno, y envía.

Para un pull request en GitHub, sube tu rama y usa `gh`:

```sh
git push origin mydev-driver
gh pr create --base main --head mydev-driver
```

O abre el pull request a través de la interfaz web de GitHub. El formulario
del pull request solicita un título (usa la línea de asunto del commit) y
un cuerpo (usa el cuerpo del commit). El título y el cuerpo forman la
descripción del pull request; deben coincidir con lo que el commit integrará
finalmente.

Para una contribución a una lista de correo o un correo electrónico a un
mantenedor:

```sh
git format-patch -1 HEAD
```

Esto genera un archivo como `0001-mydev-Add-driver-for-FooCorp-FC100-sensor-boards.patch`
que contiene el commit. Puedes adjuntarlo a un correo electrónico o enviarlo
en línea con `git send-email`. Las contribuciones por lista de correo son
menos habituales hoy en día que Phabricator o GitHub, pero siguen siendo
aceptadas.

### Qué ruta elegir

El archivo `CONTRIBUTING.md` ofrece orientación específica sobre qué ruta
usar en cada caso. El resumen:

- Los pull requests de GitHub son preferibles cuando el cambio es pequeño
  (menos de unos 10 archivos y 200 líneas), está autocontenido, pasa el CI
  sin problemas y requiere poco tiempo de los desarrolladores para integrarse.
- Phabricator es preferible para cambios más grandes, para trabajos que
  necesitan una revisión extensa y para subsistemas cuyos mantenedores
  prefieren Phabricator.
- Bugzilla es adecuado cuando el parche corrige un bug concreto que ha sido
  reportado.
- El correo electrónico directo a un committer es adecuado cuando conoces al
  mantenedor del subsistema y el cambio es lo suficientemente pequeño para
  gestionarlo de forma informal.

Un driver nuevo suele estar en algún punto intermedio entre el límite de
tamaño de GitHub y lo que encaja en Phabricator. Si tu driver tiene menos de
10 archivos y menos de 200 líneas, un GitHub PR funcionará. Si es más grande,
prueba primero con Phabricator.

Independientemente de lo que elijas, asegúrate de que el driver ha pasado
todas las pruebas previas al envío. Tanto el CI que se ejecuta en GitHub
como el proceso de revisión en Phabricator detectarán problemas, pero es más
eficiente para todos si ya los has detectado tú antes.

### Cómo escribir una buena descripción de revisión

El parche en sí es solo la mitad de lo que envías. La otra mitad es la
descripción: el texto que acompaña al parche y explica qué hace, por qué es
necesario y cómo se ha probado.

En Phabricator, la descripción es el campo Summary. En GitHub, es el cuerpo
del PR. En una lista de correo, es el cuerpo del correo electrónico.

Una buena descripción tiene tres partes:

1. Un resumen de un párrafo sobre lo que hace el parche.
2. Una explicación del diseño y de cualquier decisión de interés.
3. Una lista de lo que se ha probado.

Para una contribución de driver, una descripción típica podría ser así:

> Este parche añade un driver para las placas de sensores ambientales FooCorp
> FC100. Las placas se conectan por PCI y exponen una interfaz sencilla de
> comando y estado a través de un único BAR. El driver implementa
> probe/attach/detach siguiendo las convenciones de Newbus, expone un
> dispositivo de caracteres para la interacción desde el userland y documenta
> los intervalos de muestreo configurables mediante sysctl.
>
> El FC100 está documentado en el Programmer's Reference Manual de FooCorp,
> versión 1.4. El driver es compatible con las revisiones 1.0 y 1.1 de la
> placa, y hace funcionar el FC200 en su modo de compatibilidad con el FC100.
>
> Probado en amd64 y arm64 con una placa FC100 rev 1.1 física. Supera las
> pruebas de `make universe`, `mandoc -Tlint` y `checkstyle9.pl`. El ciclo
> de carga/descarga se ha verificado 50 veces sin fugas.
>
> Se agradecen sugerencias de los revisores sobre la estructura del sysctl.

Esta descripción hace varias cosas bien. Explica el driver de una forma que
un revisor que nunca lo ha visto puede entender. Establece qué se ha probado.
Invita explícitamente a dar opinión sobre una pregunta de diseño concreta. Se
lee como una solicitud de revisión colaborativa, no como una exigencia del
tipo "aquí está mi código, fusiónalo".

### El borrador de correo electrónico para la lista de correo

Aunque pienses enviar tu contribución a través de Phabricator o GitHub, un
borrador de correo electrónico a una de las listas de correo de FreeBSD puede
ser una buena introducción. La lista de uso general para preguntas de
desarrollo es `freebsd-hackers@FreeBSD.org`; también hay listas por subsistema,
como `freebsd-net@` para drivers de red y `freebsd-scsi@` para almacenamiento.
Elige la lista que mejor se ajuste al subsistema en el que vive tu driver y,
si tienes dudas, empieza por `freebsd-hackers@`.

Un borrador de correo electrónico podría tener este aspecto:

```text
To: freebsd-hackers@FreeBSD.org
Subject: New driver: FooCorp FC100 sensor boards

Hello,

I am working on a driver for FooCorp FC100 PCI-attached
environmental sensor boards. The boards are documented, I have
two hardware samples to test against, and the driver is in a
state that passes mandoc -Tlint, checkstyle9.pl, and make
universe clean.

Before I open a review, I wanted to ask the list if anyone has:

* Experience with similar sensor boards that might inform the
  sysctl structure.
* Strong preferences about whether the driver should expose a
  character device or a sysctl tree as the primary interface.
* Comments on the draft manual page (attached).

The code is available at https://github.com/<me>/<branch> for
anyone who wants to take an early look.

Thanks,
Your Name <you@example.com>
```

Este es el tipo de correo electrónico que tiende a generar respuestas útiles.
Demuestra que el trabajo es serio, hace preguntas concretas y ofrece una forma
de ver el código. Muchas contribuciones exitosas a FreeBSD comienzan con un
correo electrónico así.

Para este libro, no es necesario que envíes realmente ese correo electrónico.
Los ejemplos complementarios incluyen un borrador que puedes usar como plantilla.
El ejercicio de la Sección 4 de este capítulo incluye la redacción de tu propio
borrador.

### Qué ocurre tras enviar tu contribución

Una vez que envías, comienza el proceso de revisión. El flujo exacto depende de
la ruta de envío, pero el patrón general es similar en todas las rutas.

Para una revisión en Phabricator:

- La revisión aparece en la cola de Phabricator. Se suscribe automáticamente a
  las listas de correo relevantes para el subsistema.
- Los revisores pueden recoger la revisión de la cola, o puedes etiquetar a
  revisores concretos que consideres relevantes.
- Los revisores dejan comentarios en líneas concretas, comentarios generales y
  cambios solicitados.
- Atiendes los comentarios actualizando el commit y ejecutando
  `arc diff --update` para refrescar la revisión.
- El ciclo de revisión se repite hasta que los revisores estén satisfechos.
- Eventualmente, un committer integra el parche, con el crédito para ti como
  autor.

Para un pull request en GitHub:

- El PR aparece en la cola de GitHub para `freebsd/freebsd-src`.
- Los trabajos de CI se ejecutan automáticamente; deben pasar.
- Los revisores comentan el PR, dejan comentarios en líneas concretas o
  solicitan cambios.
- Atiendes los comentarios haciendo commits de corrección en tu rama.
  Finalmente, aplastas las correcciones en el commit original.
- Cuando un revisor esté listo, fusionará el PR él mismo (si es committer) o
  lo llevará a Phabricator para una revisión más profunda.
- El commit fusionado conserva tu autoría.

Para una contribución por lista de correo:

- Los lectores de la lista responden con comentarios.
- Iteras según los comentarios y envías versiones actualizadas como respuestas
  al hilo original.
- Cuando un committer esté listo, integrará el parche, con el crédito para ti.

En todos los casos, la iteración es parte del proceso. Es raro que un parche
entre en el árbol exactamente tal como fue enviado por primera vez. Espera al
menos una ronda de comentarios, a menudo varias. Cada ronda es la forma en que
los revisores te ayudan a pulir la contribución.

### Tiempo de respuesta y paciencia

Los revisores son voluntarios, incluso los que reciben un salario de sus
empleadores por trabajar en FreeBSD. Su tiempo es limitado. El tiempo de
respuesta a una revisión puede oscilar entre horas (para un parche pequeño y
bien preparado que coincide con los intereses actuales del revisor) y semanas
(para un parche grande y complejo que requiere una lectura cuidadosa).

Si tu parche no ha recibido respuesta en un tiempo razonable, es aceptable
enviar un recordatorio educado. Las convenciones habituales:

- Espera al menos una semana antes de enviar el recordatorio.
- Mantenlo breve: "Solo un aviso amistoso sobre esta revisión, por si se ha
  perdido en el radar de alguien." Nada más.
- No envíes más de un recordatorio por semana. Si un parche no recibe atención
  tras varios recordatorios, el problema probablemente no es que los revisores
  lo hayan olvidado; puede ser que el parche necesite más trabajo, o que los
  revisores que conocen el subsistema estén ocupados con otras cosas.
- Considera pedir atención para la revisión en la lista de correo
  correspondiente. Una solicitud pública a veces es más efectiva que los
  recordatorios privados.

No respondas al silencio con ira o presión. El proyecto lo gestionan
voluntarios. Una revisión que tarda más de lo esperado no es un insulto
personal.

### Iteración y actualizaciones del parche

Cada ronda de revisión tendrá comentarios que deberás atender. Algunos serán
pequeños (cambiar el nombre de una variable, añadir un comentario, corregir
una errata en la página de manual). Otros serán más grandes (reescribir una
función, cambiar una interfaz, añadir una prueba).

Atiende cada comentario. Si no estás de acuerdo con uno, responde explicando
tu razonamiento; no lo ignores sin más. Los revisores están abiertos a ser
convencidos, pero solo si presentas argumentos.

Cuando actualices el parche, mantén el historial de commits limpio. Si antes
hiciste un commit de "fixup", aplástalo en el commit original antes de la
presentación final. Los commits del árbol deben ser cada uno lógicamente
completos; no deben contener pasos incrementales desordenados.

El flujo de trabajo para actualizar un GitHub PR suele ser así:

```sh
# make the fixes
git add -p
git commit --amend
git push --force-with-lease
```

Para una actualización en Phabricator:

```sh
# make the fixes
git add -p
git commit --amend
arc diff --update
```

Usa siempre `--force-with-lease` en lugar de `--force` cuando hagas un
force-push. `--force-with-lease` rechaza el push si el remoto ha avanzado de
una forma que desconocías, lo que evita sobrescribir accidentalmente los
cambios de un revisor.

### Errores comunes al enviar contribuciones

Algunos errores comunes en el momento del envío:

- Enviar un borrador. Pule el parche primero. Enviar un parche que sabes que
  no está listo es un desperdicio del tiempo de los revisores.
- Enviar contra un árbol desactualizado. Haz un rebase contra el HEAD actual
  antes de enviar.
- Incluir cambios no relacionados. Cada envío debe ser un único cambio lógico.
  Las limpiezas de estilo, los arreglos de bugs no relacionados y las mejoras
  aleatorias deben ser envíos separados.
- No responder a los comentarios. Un parche que se paraliza en revisión porque
  el autor nunca respondió es un parche que acabará muriendo.
- Rechazar los comentarios de forma defensiva. Los revisores están intentando
  ayudar. Responder de forma defensiva a los comentarios es una manera rápida
  de deteriorar la relación.
- Enviar el mismo parche a varias rutas simultáneamente. Elige una ruta. Si
  envías a Phabricator, no abras también un GitHub PR con el mismo contenido.

### Cierre de la Sección 6

Enviar un parche para su revisión es un proceso mecánico una vez que el parche
está en forma. El parche es un commit (o una serie de commits) con un mensaje
adecuado, sobre un árbol actualizado. La ruta de envío depende del tamaño y la
naturaleza del cambio: los cambios pequeños y autocontenidos van a GitHub PRs,
los cambios más grandes o profundos van a Phabricator, y los arreglos de bugs
concretos pueden adjuntarse a entradas de Bugzilla. La descripción adjunta
explica el parche e invita a la revisión. Lo que sigue es un ciclo de revisión
iterativo que termina con un committer integrando el parche.

La siguiente sección aborda el lado humano de esa iteración: cómo trabajar con
un mentor o committer, cómo gestionar los comentarios y cómo convertir una
primera contribución en el comienzo de una relación más duradera con el
proyecto.

## Sección 7: Trabajar con un mentor o committer

### Por qué importa el lado humano

El proceso de envío es, en última instancia, una colaboración con personas, no con una plataforma. El parche que envías lo revisan ingenieros que tienen su propio contexto, su propia carga de trabajo y su propia experiencia acumulada sobre qué hace que un driver sea fácil o difícil de revisar. El éxito o el fracaso de tu envío depende tanto de cómo te relacionas con esas personas como de la calidad técnica del código.

Ese enfoque incomoda a algunos colaboradores noveles, que preferirían que el trabajo técnico se sostuviese por sí solo. La preferencia es comprensible, pero no se corresponde con la realidad. FreeBSD es un proyecto comunitario, no un servicio de recepción de código. Los revisores ofrecen su tiempo porque les importa el proyecto y porque disfrutan ayudando a otros colaboradores a tener éxito. Cuando ese cuidado es recíproco, la experiencia resulta positiva para todos. Cuando no lo es, la experiencia puede volverse frustrante incluso cuando los parches son técnicamente buenos.

Esta sección recorre el lado humano del proceso de contribución. Parte de lo que encontrarás aquí te parecerá evidente. Gran parte rara vez se discute de forma explícita, y por eso algunos colaboradores noveles tropiezan incluso cuando su código es sólido.

### El papel de un mentor

Un mentor, en el contexto de FreeBSD, es un committer que ha aceptado guiar a un nuevo colaborador específico a través de sus primeras contribuciones. No toda contribución implica un mentor; muchas primeras entregas llegan por la vía de revisión ordinaria sin una mentoría formal. Pero cuando hay un mentor implicado, la relación tiene una forma concreta.

Un mentor suele hacer lo siguiente:

- Revisa tus parches en detalle, a menudo antes de que pasen a una revisión más amplia.
- Te ayuda a entender las convenciones del proyecto y el subsistema específico en el que trabajas.
- Patrocina commits en tu nombre, es decir, incorpora el parche al árbol acreditándote como autor.
- Responde preguntas sobre el proceso del proyecto, el estilo y las normas del colectivo.
- Avala tu candidatura en las discusiones de nominación si, más adelante, obtener derechos de commit resulta apropiado.

Un mentor no hace tu trabajo. Acelera tu integración en el proyecto. Un buen mentor es paciente, está dispuesto a explicar y dispuesto a señalar cuando vas en una dirección equivocada. Un buen aprendiz es diligente, está dispuesto a escuchar y dispuesto a hacer el trabajo de iterar.

Encontrar un mentor suele ser algo orgánico. Ocurre porque has interactuado de forma productiva con un committer concreto a lo largo de varias rondas de revisión, y ese committer ha ofrecido asumir un papel de mentoría más estructurado. Rara vez sucede porque lo hayas pedido en frío. Si te interesa encontrar un mentor, lo más adecuado es empezar a contribuir de forma visible y productiva, y dejar que la relación se desarrolle sola.

El FreeBSD Project también cuenta con programas de mentoría más formales en distintos momentos, incluyendo para grupos demográficos específicos o subsistemas concretos. Estos programas son el lugar indicado para buscar un mentor si quieres un punto de partida estructurado.

### El patrocinio: la vía del commit

Un patrocinador es un committer que incorpora un parche al árbol en nombre de un colaborador. Toda contribución procedente de alguien sin derechos de commit pasa por un patrocinador en el momento del commit. El patrocinador no es necesariamente la misma persona que el revisor principal, ni necesariamente un mentor, aunque puede serlo.

Encontrar un patrocinador para un parche suele ser sencillo. Si el parche ha superado la revisión y al menos un committer lo ha aprobado, ese committer suele estar dispuesto a patrocinar el commit. No hace falta pedirlo formalmente; el commit ocurrirá cuando el revisor esté listo.

Si el parche ha sido revisado pero nadie ha dado el paso de incorporarlo, es apropiado hacer una pregunta educada: «¿Hay alguien en posición de patrocinar el commit de este parche?». Preguntar en el hilo de revisión o en la lista de correo relevante suele bastar para encontrar a alguien.

No confundas el patrocinio con el apoyo en sentido abstracto. Un patrocinador es específicamente la persona que ejecuta el `git push` que incorpora tu parche. Asumen una pequeña cuota de responsabilidad: su nombre aparece en los metadatos del commit, y están certificando implícitamente que el parche estaba listo para ser incorporado.

### Recibir el feedback con ecuanimidad

El feedback sobre tu parche puede ser difícil de leer, especialmente la primera vez. Los revisores escriben en modo de revisión de código, lo que significa que señalan cosas concretas que hay que cambiar. Ese modo puede parecer negativo incluso cuando la valoración general del parche es abrumadoramente positiva. Una revisión que dice «esto es un buen comienzo, pero hay veinte cosas que corregir» es algo normal en una primera entrega.

La respuesta correcta al feedback es atenderlo. Para cada comentario:

- Léelo con atención. Asegúrate de entender lo que pide el revisor.
- Si el comentario es claro y accionable, haz el cambio. No discutas solo porque la sugerencia no era tu primera opción.
- Si el comentario no está claro, pide una aclaración. «¿Puedes explicar más a qué te refieres con X?» es una respuesta perfectamente válida.
- Si no estás de acuerdo con el comentario, responde explicando tu razonamiento. Sé específico: «Pensé en usar X pero opté por Y porque Z». Los revisores están abiertos a ser persuadidos.
- Si el comentario está fuera del alcance del parche, dilo y propón tratarlo por separado. «Buena observación, pero esto es realmente un cambio separado; lo enviaré como parche de seguimiento».

Nunca respondas con hostilidad. Aunque creas que el revisor está equivocado, responde con calma y con argumentos. Un hilo de revisión que degenera en enfado es uno del que el revisor acabará desconectándose, y tu parche quedará paralizado.

Algunas respuestas concretas que debes evitar:

- «El código ya funciona». Que el código funcione no es la cuestión. La cuestión es si se ajusta a las convenciones y expectativas de diseño del árbol.
- «Esto es solo estilo; el código está bien». El estilo es parte de la calidad de ingeniería. Los revisores no te hacen perder el tiempo cuando preguntan sobre estilo.
- «Otros drivers del árbol lo hacen así». Puede que sí, y el árbol tiene muchos drivers más antiguos que no se ajustan a las convenciones modernas. El objetivo para las nuevas contribuciones es seguir las convenciones modernas, no reproducir la deriva histórica.
- «Lo haré más adelante». Si dices que lo harás más adelante, el revisor no tiene forma de verificarlo. Hazlo ahora, o explica por qué una corrección posterior es apropiada.

El proceso de revisión es cooperativo. El revisor no es tu adversario. Cada comentario, incluso uno con el que no estés de acuerdo, es el revisor invirtiendo tiempo en tu parche. Responde a esa inversión con la tuya propia.

### Iteración y paciencia

La revisión de parches es iterativa por diseño. Una primera entrega típica de un driver pasa por tres a cinco rondas de revisión antes de ser incorporada. Cada ronda lleva días o semanas, en función de la disponibilidad de los revisores y del tamaño de los cambios solicitados.

El tiempo total transcurrido desde la primera entrega hasta la fusión, para un nuevo driver, suele ser de varias semanas. A veces son meses. Eso es normal. FreeBSD es un proyecto meticuloso; la revisión cuidadosa lleva tiempo.

Algunos hábitos que ayudan en la iteración:

- Responde rápido. Cuanto más rápido respondas al feedback, más rápido avanza la revisión. Los retrasos de tu parte son tan perjudiciales para los plazos como los retrasos del revisor.
- Agrupa los cambios pequeños. Si el revisor deja diez comentarios, haz las diez correcciones en una sola actualización en lugar de enviar diez actualizaciones individuales. Los revisores prefieren ver el trabajo integrado.
- Mantén el commit limpio. A medida que iteras, modifica el commit original en lugar de apilar commits de corrección. El commit que finalmente se incorpore debe ser un único commit limpio, no un historial desordenado.
- Prueba antes de volver a enviar. Cada iteración debe superar las mismas pruebas previas a la entrega que la primera. No rompas las pruebas entre rondas.
- Resume cada iteración. Cuando actualices el parche, una breve respuesta en la revisión indicando «actualizado para atender todos los comentarios; concretamente: hice X, hice Y, aclaré Z» ayuda a los revisores a reorientarse rápidamente.

Sobre todo, ten paciencia. El proceso de revisión existe porque la calidad del código importa. Apresurarlo compromete la calidad y produce un parche que se incorpora rápido pero genera problemas más adelante.

### Gestionar los desacuerdos

En ocasiones, los revisores dejarán feedback con el que realmente no estás de acuerdo. El comentario no es confuso; has reflexionado sobre él y crees que el revisor está equivocado. ¿Qué haces?

Primero, considera que puedes estar equivocado. La mayoría de las veces, cuando un revisor plantea una objeción, hay algo detrás que puede que no estés viendo. El revisor tiene contexto sobre el árbol, el subsistema y la historia que quizás tú no tienes. Da por sentado que la objeción es legítima hasta que tengas evidencia de lo contrario.

Segundo, si tras reflexionar sigues en desacuerdo, responde con argumentos. Explica tu perspectiva. Cita detalles concretos del código, del datasheet o del árbol. Pide al revisor que responda a tus razonamientos.

Tercero, si el desacuerdo persiste, escala con cuidado. Pide una segunda opinión a otro committer. Publica en la lista de correo relevante describiendo la cuestión. A veces los desacuerdos revelan que hay múltiples respuestas defensables y que el proyecto no ha llegado a una posición clara; eso es información útil que merece salir a la luz.

Cuarto, si el desacuerdo continúa y nada lo resuelve, tienes una elección. Puedes hacer el cambio que el revisor ha pedido, aunque no estés de acuerdo, e incorporar el parche. O puedes retirar el parche. Ambas opciones son legítimas. La cultura del proyecto no consiste en obligar a los colaboradores a hacer cosas con las que no están de acuerdo, pero tampoco en dar el visto bueno automáticamente a parches sobre los que la comunidad de committers tiene dudas. Si el desacuerdo es fundamental, retirar el parche es a veces el resultado correcto.

Los desacuerdos de esta profundidad son poco frecuentes. La mayor parte del feedback es práctico y está claramente en lo correcto o es claramente adaptable. Los desacuerdos serios, cuando ocurren, suelen ser sobre decisiones de diseño en las que múltiples respuestas son defensables.

### Construir una relación a largo plazo

Una primera entrega no es el final del trabajo. Si va bien, puede ser el comienzo de una relación a largo plazo con el proyecto. Muchos committers empezaron como colaboradores por primera vez cuyos primeros parches salieron bien, cuyos parches posteriores se construyeron sobre esa confianza, y cuya implicación creció hasta el punto en que los derechos de commit tenían pleno sentido.

Construir ese tipo de relación no consiste en actuar. Se trata de seguir contribuyendo de forma consistente y productiva. Algunos hábitos que ayudan:

- Responde a los informes de errores sobre tu driver. Si un usuario informa de un error, clasifícalo, confírmalo o desmíntelo, y haz seguimiento. Un driver cuyo autor responde es un driver que el proyecto valora.
- Revisa los parches de otras personas. Una vez que conoces bien un subsistema, puedes revisar nuevos parches en ese subsistema. Revisar es la forma de hacerte conocer como experto, y de interiorizar las convenciones del subsistema.
- Participa en las discusiones. Las listas de correo y los canales de IRC tienen discusiones técnicas en curso. Participar, con reflexión, es parte de pertenecer a la comunidad.
- Mantén tu driver actualizado. Si las APIs del kernel cambian, actualiza tu driver. Si aparecen nuevas variantes de hardware, añade soporte. El driver no está terminado al fusionarse; es un artefacto vivo del que eres responsable.

Nada de esto es obligatorio. El proyecto agradece cualquier contribución, incluidos los parches puntuales de colaboradores que no vuelven. Pero si te interesa una implicación más profunda, estos son los caminos.

### Identificar a los maintainers existentes

Muchos subsistemas de FreeBSD tienen maintainers identificables o colaboradores de largo recorrido. Encontrarlos es útil porque suelen ser los mejores revisores para trabajos relacionados.

Varias formas de identificar maintainers:

- `git log --format="%an %ae" <file>` muestra quién ha incorporado cambios a un archivo concreto. Los nombres que aparecen con frecuencia son los maintainers activos.
- `git blame <file>` muestra quién escribió cada línea. Si estás extendiendo una función concreta, la persona que la escribió suele ser la indicada a quien preguntar.
- El archivo `MAINTAINERS`, donde existe, lista a los maintainers formales. FreeBSD no tiene un único archivo `MAINTAINERS` para todo el árbol, pero algunos subsistemas tienen equivalentes informales.
- La sección `AUTHORS` de la página de manual nombra al autor principal.

Para un driver que extiende una familia existente, el autor del driver existente suele ser el primer revisor al que acudir. Tiene el contexto y la autoridad. Para un driver completamente nuevo en un área nueva, busca un revisor preguntando en la lista de correo relevante.

### Ejercicio: identifica al maintainer de un driver similar

Antes de continuar, elige un driver del árbol que sea similar en alcance al que estás desarrollando. Usa `git log` para identificar a su maintainer. Anota su nombre y su correo electrónico. Después lee algunos de sus commits y observa con qué revisores suelen interactuar.

Esto te da un modelo mental de quiénes son las personas del subsistema, y hace que el lado humano de la entrega parezca más concreto.

No se espera que les contactes a menos que tengas una pregunta concreta. El ejercicio consiste en construir conciencia.

### Cerrando la sección 7

El lado humano del proceso de revisión es tan importante como el técnico. Un mentor o committer que participa activamente en tu parche es un recurso; tratarlo con respeto, responder al feedback de manera constructiva e iterar con paciencia son las disciplinas prácticas de esa colaboración. Los desacuerdos ocurren y suelen ser productivos; la actitud defensiva es el principal riesgo que hay que evitar. Una primera entrega, bien gestionada, puede ser el comienzo de una relación duradera con el proyecto.

La última parte del flujo de envío es lo que ocurre después de que el parche está en el árbol. La sección 8 cubre el largo recorrido del mantenimiento.

## Sección 8: Mantenimiento y soporte del driver tras la integración

### La integración no es el final

Cuando tu parche llega al árbol de FreeBSD, la sensación natural es que el trabajo ha terminado. El driver está dentro. La revisión ha concluido. El commit está en el historial. Puedes pasar a otra cosa.

La sensación es comprensible, pero el panorama está incompleto. Integrar el driver en el árbol es el comienzo de un tipo de trabajo diferente, no el final de la vida del driver. Mientras el driver esté en el árbol, necesitará mantenimiento ocasional. Mientras se use, de vez en cuando saldrán bugs a la superficie. Mientras el kernel evolucione, sus APIs cambiarán y el driver tendrá que seguirles el paso.

Esta sección recorre el panorama del mantenimiento posterior a la integración. Las expectativas no son exigentes, pero sí son reales. Un driver cuyo autor desaparece tras la integración es un driver que el proyecto tiene que mantener por su cuenta, y si nadie lo asume con el tiempo, eso puede acabar siendo una razón para marcarlo como obsoleto.

### Seguimiento en Bugzilla

El Bugzilla de FreeBSD en `https://bugs.freebsd.org/` es el sistema de seguimiento de bugs principal del proyecto. Los bugs registrados contra tu driver aparecerán allí. No estás obligado a suscribirte a Bugzilla como colaborador, pero al menos deberías saber cómo buscar bugs abiertos contra tu driver.

Una manera sencilla de comprobarlo:

```text
https://bugs.freebsd.org/bugzilla/buglist.cgi?component=kern&query_format=advanced&short_desc=mydev
```

Sustituye `mydev` por el nombre de tu driver. La consulta devuelve los bugs cuyo resumen menciona tu driver.

Si se registra un bug:

- Lee el informe con atención.
- Si puedes reproducirlo, hazlo.
- Si puedes corregirlo, prepara un parche. El parche pasa por el mismo proceso de revisión que cualquier otro cambio.
- Si no puedes reproducirlo, pide al informador más información: versión de FreeBSD, detalles del hardware, salida de log relevante.
- Si es un bug real pero no tienes tiempo ni capacidad para corregirlo, dilo en el informe. Un bug con un autor implicado que de momento no puede corregirlo es diferente de un bug con un autor ausente. Como mínimo, esa implicación significa que cualquier otra persona que mire el bug tiene contexto con el que trabajar.

Bugzilla también aloja peticiones de mejora (solicitudes de nuevas funcionalidades). Tienen menor prioridad que los informes de bugs, pero son señales útiles sobre lo que los usuarios necesitan. No es necesario implementar cada petición de mejora, pero reconocerlas y discutir prioridades forma parte del mantenimiento.

### Responder al feedback de la comunidad

Además de Bugzilla, el feedback de la comunidad puede llegarte a través de otros canales:

- Correo electrónico directo de usuarios.
- Debates en las listas de correo.
- Preguntas en canales de IRC.
- Comentarios en hilos de revisión de trabajos relacionados.

La expectativa para un mantenedor accesible no es que respondas a todo esto de inmediato. La expectativa es que seas localizable en la dirección de correo registrada para el driver (la que figura en la sección `AUTHORS` de la página de manual y en el historial de commits), y que cuando respondas a algo, lo hagas de forma productiva.

Un ritmo práctico podría ser este: una vez a la semana o una vez cada dos semanas, revisa el correo relacionado con tu driver y las consultas en Bugzilla. Responde a todo lo que esté pendiente. Clasifica lo nuevo. Mantén los tiempos de respuesta razonables, del orden de una o dos semanas, no de meses.

Si tus circunstancias cambian y ya no puedes mantener el driver, dilo públicamente. El proyecto puede encontrar nuevos mantenedores si se conoce la necesidad. El peor resultado es desaparecer en silencio, dejando bugs sin reconocer y usuarios que no saben si el driver se mantiene.

### Desviación de la API del kernel

El kernel de FreeBSD evoluciona. Las APIs que eran estables cuando se escribió tu driver pueden cambiar. Cuando ocurre esto, el driver necesita actualizarse, y tú eres la primera persona a la que recurrirá el proyecto para hacer la actualización.

Varios tipos de desviación de API que afectan habitualmente a los drivers:

- Cambios en el framework Newbus: nuevas firmas de métodos, nuevas categorías de métodos, cambios en las macros `device_method_t`.
- Cambios en los patrones de attachment específicos del bus: PCI, USB, iicbus, spibus y otros evolucionan con el tiempo.
- Cambios en la interfaz `bus_space(9)`.
- Cambios en la interfaz de dispositivo de caracteres (`cdevsw`, `make_dev`, etc.).
- Cambios en la API de asignación de memoria (`malloc(9)`, `contigmalloc`, `bus_dma`).
- Cambios en los primitivos de sincronización (`mtx`, `sx`, `rw`).
- Obsolescencia de APIs antiguas en favor de nuevas.

Normalmente, estos cambios se anuncian en las listas de correo antes de integrarse, y a veces vienen acompañados de un commit de «barrido del árbol» que actualiza todos los usuarios de la API antigua a la nueva. Si tu driver está en el árbol, el barrido lo actualizará automáticamente por lo general. Pero no siempre; a veces el barrido es conservador y deja los drivers que no puede actualizar mecánicamente para que los gestione el mantenedor.

Un buen hábito: consulta `freebsd-current@` al menos ocasionalmente para ver los debates sobre cambios de API que afecten a tu driver. Si encuentras alguno, comprueba si tu driver sigue compilando contra el HEAD actual. Si no es así, envía un parche para actualizarlo.

### El archivo UPDATING

El proyecto mantiene un archivo `UPDATING` en `/usr/src/UPDATING` que enumera los cambios significativos en el árbol de código fuente, incluidos los cambios de API a los que los drivers pueden necesitar responder. Léelo ocasionalmente, especialmente antes de actualizar tu árbol, para comprobar si algo afecta a tu driver.

Una entrada típica en UPDATING podría decir:

```text
20260315:
	The bus_foo_bar() API has changed to require an explicit
	flags argument.  Drivers using bus_foo_bar() should pass
	BUS_FOO_FLAG_DEFAULT to preserve historical behaviour.
	Drivers using bus_foo_bar_old() should migrate to the new
	API as bus_foo_bar_old() will be removed in FreeBSD 16.
```

Si ves una entrada como esta que menciona una función que usa tu driver, actualiza el driver en consecuencia.

### Refactorizaciones de todo el árbol

Ocasionalmente, el proyecto realiza refactorizaciones de todo el árbol que afectan a todos los drivers. Ejemplos de la historia de FreeBSD incluyen:

- La conversión de las etiquetas CVS `$FreeBSD$` a metadatos exclusivos de git.
- La introducción de líneas SPDX-License-Identifier en todo el árbol.
- Renombrados masivos de APIs como la familia `make_dev` o la familia `contigmalloc`.

Cuando se produce una refactorización de todo el árbol, el commit de refactorización actualiza tu driver junto con los del resto. Puede que no necesites hacer nada. Pero la refactorización aparecerá en `git log` contra tu driver, y los desarrolladores futuros que consulten el historial la verán. Comprende qué ha ocurrido para poder explicarlo si te lo preguntan.

### Participar en lanzamientos futuros

FreeBSD tiene un ciclo de lanzamientos de aproximadamente un año entre versiones mayores, con versiones de punto en un calendario más frecuente. Tu driver participa en este ciclo tanto si haces algo activo como si no.

Hay algunas cosas que merece la pena entender:

- Tu driver se compila para cada versión que salga de una rama en la que viva. Si vive en `main`, estará en el próximo lanzamiento mayor. Si también se incorpora mediante cherry-pick a una rama `stable/`, estará en el próximo lanzamiento de punto de esa rama.
- Antes de los lanzamientos mayores, el equipo de ingeniería de versiones puede pedir a los mantenedores que confirmen que sus drivers están en buen estado. Si recibes tal petición para tu driver, responde. Es una acción sencilla que ayuda al proyecto a planificar el lanzamiento.
- Tras un lanzamiento, tu driver está en circulación en cada instalación que use esa versión. Los informes de bugs pueden ser más frecuentes justo después de un lanzamiento.

Participar en el ciclo de lanzamiento es una forma de mantenimiento de bajo esfuerzo. Consiste principalmente en estar disponible para que los ingenieros de versiones puedan contactarte si lo necesitan.

### Mantener el código actualizado: un ritmo

Un ritmo razonable para mantener un driver en el árbol:

- Mensualmente: comprueba en Bugzilla los bugs abiertos contra el driver. Responde a cualquier asunto pendiente.
- Mensualmente: recompila el driver contra el HEAD actual y comprueba si hay advertencias o fallos. Si algo falla, investiga y corrige.
- Trimestralmente: vuelve a leer la página de manual. Actualízala si el driver ha cambiado desde la última revisión.
- Antes de lanzamientos mayores: ejecuta la batería completa de pruebas previa a la entrega (style, mandoc, build, universe) sobre tu driver tal como está en ese momento. Corrige cualquier cosa que se haya desviado.
- Siempre que tengas un ejemplar de hardware y una tarde libre: prueba el driver en el hardware y asegúrate de que sigue funcionando.

Este ritmo no es obligatorio. Un driver puede pasar meses sin mantenimiento si no hay nada roto. Pero tener un ritmo en mente mantiene el driver sano con el paso del tiempo.

### Ejercicio: crear una lista de comprobación mensual de mantenimiento

Antes de cerrar esta sección, abre un archivo de texto y escribe una lista de comprobación mensual de mantenimiento para tu driver. Incluye:

- La URL de la consulta en Bugzilla que muestra los bugs contra el driver.
- Los comandos para recompilar el driver contra el HEAD actual.
- Los comandos para ejecutar las comprobaciones de estilo y lint.
- Los comandos para comprobar la desviación de API (por ejemplo, `grep` para llamadas obsoletas).
- Una nota sobre la dirección de correo donde los usuarios pueden encontrarte.
- Un recordatorio para actualizar la fecha de la página de manual si realizas cambios semánticos.

Guarda esta lista junto al código fuente del driver o en tus notas personales. El acto de escribirla te compromete con el ritmo. Las listas que existen en papel se siguen; las que viven solo en la memoria, no.

### Cuando ya no puedes seguir manteniendo

La vida cambia. Los trabajos cambian. Las prioridades se desplazan. En algún momento puede que te encuentres con que ya no puedes mantener tu driver como antes. Esto es normal y el proyecto tiene procesos para gestionarlo.

La decisión correcta es decirlo públicamente. Opciones:

- Publica en `freebsd-hackers@` o en la lista del subsistema correspondiente diciendo que te retiras del driver e invitando a alguien a hacerse cargo.
- Registra una entrada en Bugzilla etiquetada como pregunta de transición de mantenimiento.
- Envía un correo a los committers que han revisado tus parches e informales directamente.

El proyecto entonces encontrará un nuevo mantenedor, marcará el driver como huérfano o decidirá algún otro camino. Lo importante es que el estado sea conocido. El abandono silencioso es peor que cualquiera de las alternativas.

Si nadie asume el driver y este sigue usándose, el proyecto puede acabar marcándolo como obsoleto. Esto no es un fracaso; es una respuesta razonable a código que nadie cuida activamente. Los drivers pueden quedar obsoletos, eliminarse y volver a añadirse después si alguien da un paso adelante. La historia del árbol está llena de esos ciclos.

### Cerrando la sección 8

El mantenimiento posterior a la integración es una actividad más ligera que la entrega inicial, pero es real. Las expectativas son: vigilar Bugzilla por bugs contra tu driver, responder a los usuarios que se pongan en contacto, mantener el driver compilando contra el HEAD actual a medida que el kernel evoluciona, participar en los ciclos de lanzamiento y decirlo públicamente si ya no puedes seguir manteniendo. Un driver cuyo autor permanece implicado con el tiempo es un driver que el proyecto valora más allá de la integración inicial.

Con las secciones 1 a 8 completadas, hemos recorrido el arco completo de una entrega de driver: desde comprender el proyecto hasta preparar los archivos, pasando por la licencia, las páginas de manual, las pruebas, la entrega, la iteración de revisión y el mantenimiento a largo plazo. El resto del capítulo ofrece laboratorios prácticos y ejercicios de desafío que te permiten practicar el flujo de trabajo con código real, seguidos de una consolidación del modelo mental y un puente hacia el capítulo final.

## Laboratorios prácticos

Los laboratorios de este capítulo están diseñados para realizarse con un driver real. El enfoque más sencillo es tomar un driver que ya hayas escrito durante el libro, como el driver de LED de capítulos anteriores o el dispositivo simulado del Capítulo 36, y llevarlo a través del flujo de trabajo de preparación de la entrega.

Si no tienes un driver a mano, los ejemplos complementarios en `examples/part-07/ch37-upstream/` incluyen un driver esqueleto que puedes usar.

Todos los laboratorios pueden realizarse en una máquina virtual de desarrollo con FreeBSD 14.3. Ninguno de ellos enviará nada al proyecto real de FreeBSD, así que puedes trabajar con libertad sin preocuparte por publicar accidentalmente trabajo a medias.

### Laboratorio 1: preparar la estructura de archivos

Objetivo: tomar un driver existente y reorganizar sus archivos en la estructura convencional de FreeBSD.

Pasos:

1. Identifica el driver con el que vas a trabajar. Llámalo
   `mydev`.
2. Crea la estructura de directorios:
   - `sys/dev/mydev/` para los archivos fuente del driver.
   - `sys/modules/mydev/` para el Makefile del módulo.
   - `share/man/man4/` para la página de manual.
3. Mueve o copia los archivos `.c` y `.h` a `sys/dev/mydev/`.
   Renómbralos si es necesario, de forma que el archivo fuente principal sea
   `mydev.c`, la cabecera interna sea `mydev.h` y las definiciones de
   registros de hardware queden en `mydevreg.h`.
4. Escribe el Makefile del módulo en `sys/modules/mydev/Makefile`
   siguiendo la plantilla de la Sección 2.
5. Construye el módulo con `make`. Corrige cualquier error de compilación.
6. Verifica que el módulo carga con `kldload` y se descarga con
   `kldunload`.

Criterio de éxito: el driver se compila como módulo cargable y
la disposición de archivos sigue las convenciones del árbol.

Tiempo estimado: entre 30 y 60 minutos para un driver pequeño.

Problemas habituales:

- Rutas de include que asumían la estructura antigua. Corrige los includes para
  usar `<dev/mydev/mydev.h>` en lugar de `"mydev.h"`.
- Entradas olvidadas en `SRCS`. Si tienes varios archivos `.c`,
  inclúyelos todos.
- Ausencia de `.PATH`. El Makefile necesita `.PATH:
  ${SRCTOP}/sys/dev/mydev` para que make pueda encontrar los archivos fuente.

### Laboratorio 2: Auditoría del estilo del código

Objetivo: poner el código fuente del driver en conformidad con `style(9)`.

Pasos:

1. Ejecuta `/usr/src/tools/build/checkstyle9.pl` contra todos los archivos `.c`
   y `.h` del driver. Captura la salida.
2. Lee cada advertencia con detenimiento. Consulta
   `style(9)` para entender qué regla se aplica en cada caso.
3. Corrige cada advertencia en el código fuente. Vuelve a ejecutar el comprobador de estilo
   después de cada lote de correcciones.
4. Cuando el comprobador no muestre ninguna advertencia, repasa el código fuente
   a ojo buscando cosas que el comprobador pueda haber pasado por alto: sangrado
   inconsistente dentro de argumentos multilínea, estilos de comentario,
   agrupaciones de declaraciones de variables.
5. Asegúrate de que toda función que no se exporta tiene la palabra clave `static`.
   Asegúrate de que toda función exportada tiene una declaración en
   un archivo de cabecera.

Criterio de éxito: el comprobador de estilo no produce ninguna advertencia contra
ningún archivo del driver.

Tiempo estimado: entre una y tres horas para un driver que no haya pasado
previamente por una auditoría de estilo.

Sorpresas habituales:

- Advertencias de espacio en lugar de tabulador en líneas que creías correctas.
  El comprobador es estricto; confía en él.
- Advertencias sobre líneas en blanco entre declaraciones de variables y
  el código. `style(9)` las exige.
- Advertencias sobre expresiones de retorno sin paréntesis. Corrígelas
  añadiendo los paréntesis.

### Laboratorio 3: Añadir la cabecera de copyright

Objetivo: asegurarte de que todos los archivos fuente tienen una cabecera de copyright
correcta al estilo FreeBSD.

Pasos:

1. Identifica todos los archivos del driver que necesitan cabecera: cada
   archivo `.c`, cada archivo `.h` y la página de manual.
2. Para cada archivo, comprueba la cabecera existente. Si falta o está
   mal formada, reemplázala por una plantilla correcta conocida.
3. Usa `/*-` como apertura de la cabecera en los archivos `.c` y `.h`.
   Usa `.\"-` como apertura en la página de manual.
4. Incluye la línea `SPDX-License-Identifier` con la licencia apropiada,
   normalmente `BSD-2-Clause`.
5. Añade tu nombre y dirección de correo electrónico en la línea de Copyright.
6. Incluye el texto de licencia estándar.
7. Verifica que el archivo comienza en la columna 1 con la apertura `/*-` o
   `.\"-`.

Criterio de éxito: todos los archivos tienen una cabecera correctamente formateada
que se ajusta a las convenciones de los archivos ya presentes en el árbol.

Tiempo estimado: 30 minutos.

Verificación:

- Compara tu cabecera con la cabecera de
  `/usr/src/sys/dev/null/null.c`. Deben ser estructuralmente
  idénticas.
- Si utilizas alguna herramienta automatizada de recolección de licencias, debería
  reconocer tus cabeceras.

### Laboratorio 4: Redactar la página de manual

Objetivo: escribir una página de manual completa de la sección 4 para el driver.

Pasos:

1. Crea `share/man/man4/mydev.4`.
2. Parte de la plantilla de la Sección 4 de este capítulo, o del
   ejemplo descargable.
3. Rellena cada sección para tu driver:
   - NAME y la descripción de NAME.
   - SYNOPSIS que muestre cómo compilar el driver en el kernel o cargarlo como
     módulo.
   - DESCRIPTION en prosa.
   - HARDWARE con los dispositivos compatibles.
   - FILES con los nodos de dispositivo.
   - SEE ALSO con las referencias cruzadas relevantes.
   - HISTORY indicando cuándo apareció el driver por primera vez.
   - AUTHORS con tu nombre y correo electrónico.
4. Sigue la regla de una oración por línea en todo el documento.
5. Ejecuta `mandoc -Tlint mydev.4` y corrige cada advertencia.
6. Renderiza la página con `mandoc mydev.4 | less -R` y léela
   desde la perspectiva de un usuario. Corrige cualquier cosa que resulte incómoda.
7. Si tienes `igor` instalado, ejecútalo y atiende sus
   advertencias.

Criterio de éxito: `mandoc -Tlint` no produce ninguna salida, y la página
renderizada se lee con claridad.

Tiempo estimado: entre una y dos horas para una primera página de manual.

Lectura previa al laboratorio: antes de empezar, lee
`/usr/src/share/man/man4/null.4`, `/usr/src/share/man/man4/led.4`,
y `/usr/src/share/man/man4/re.4`. Estas tres páginas abarcan
el rango de complejidad que pueden tener las páginas de manual de la sección 4, y
te darán una intuición sólida sobre cómo debería quedar la tuya.

### Laboratorio 5: Automatización del ciclo de compilación y carga

Objetivo: escribir un script de shell que automatice el ciclo de compilación
y carga previo a la presentación.

Pasos:

1. Crea un script llamado `pre-submission-test.sh` en el directorio
   de ejemplos descargables.
2. El script debe, en orden:
   - Ejecutar el comprobador de estilo sobre todos los archivos fuente.
   - Ejecutar `mandoc -Tlint` sobre la página de manual.
   - Ejecutar `make clean && make obj && make depend && make` en el
     directorio del módulo.
   - Cargar el módulo resultante con `kldload`.
   - Descargar el módulo con `kldunload`.
   - Informar del éxito o del fallo de forma clara.
3. Usa `set -e` para que el script se detenga ante el primer error.
4. Incluye sentencias echo descriptivas que anuncien cada etapa.
5. Prueba el script con tu driver.

Criterio de éxito: el script se ejecuta sin errores con un driver listo
para presentar, y produce una salida de error clara con un driver que tiene problemas.

Tiempo estimado: 30 minutos para un script sencillo; más si añades refinamientos.

### Laboratorio 6: Generar un parche de prueba

Objetivo: practicar el flujo de trabajo de generación de parches sin llegar
a presentar nada.

Pasos:

1. En un clon de trabajo del árbol, crea una rama temática
   para tu driver:

   ```sh
   git checkout -b mydev-driver
   ```

2. Añade los archivos del driver:

   ```sh
   git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
   ```

3. Haz el commit con un mensaje adecuado siguiendo las convenciones de la
   Sección 2:

   ```sh
   git commit -s
   ```

4. Genera el parche:

   ```sh
   git format-patch -1 HEAD
   ```

5. Lee el archivo `.patch` generado. Verifica que tiene buen aspecto:
   sin cambios no relacionados, sin espacios en blanco al final de línea, un mensaje de
   commit bien formado.
6. Aplica el parche a un clon limpio para verificar que se aplica
   correctamente:

   ```sh
   git am < 0001-mydev-Add-driver.patch
   ```

Criterio de éxito: tienes un archivo de parche limpio que representa
la presentación del driver, y se aplica sin problemas a un árbol limpio.

Tiempo estimado: 30 minutos.

Sorpresas habituales:

- `git format-patch` produce un archivo por commit. Si tienes
  tres commits en tu rama, obtendrás tres archivos `.patch`. Para una presentación de driver
  que debería ser un único commit, primero usa amend o squash.
- Los espacios en blanco al final de línea en el commit aparecen como secuencias `^I`
  en el parche. Elimínalos antes de hacer el commit.
- Problemas con los finales de línea. Asegúrate de que tu editor usa LF, no
  CRLF.

### Laboratorio 7: Redactar una carta de presentación para la revisión

Objetivo: practicar la redacción de la descripción que acompaña a una
presentación.

Pasos:

1. Abre un editor de texto y escribe una carta de presentación en formato de correo electrónico para
   tu presentación del driver.
2. Incluye:
   - Un asunto apropiado para un mensaje en una lista de correo.
   - Un párrafo que resuma qué hace el driver.
   - Una descripción del hardware compatible.
   - Una lista de lo que se ha probado.
   - Una declaración de qué tipo de comentarios estás solicitando.
3. Mantén un tono profesional y colaborativo. Estás pidiendo
   revisión, no exigiendo aprobación.
4. Guarda la carta como `cover-letter.txt` en tu directorio de
   ejemplos descargables.
5. Compártela con un amigo o colega para recibir sus impresiones antes de
   continuar.

Criterio de éxito: la carta de presentación se lee como una invitación productiva
a la revisión.

Tiempo estimado: entre 15 y 30 minutos.

### Laboratorio 8: Simulacro de ciclo de revisión

Objetivo: ensayar la parte iterativa del ciclo de revisión.

Pasos:

1. Pide a un colega que lea tu parche y tu carta de presentación como si
   fuera un revisor.
2. Captura sus comentarios como una lista.
3. Trata cada comentario como un comentario de revisión real. Responde a cada
   uno: aplica la corrección, explica tu razonamiento o discrepa
   de forma constructiva.
4. Actualiza el commit y regenera el parche.
5. Repite el proceso al menos durante dos rondas de comentarios.

Criterio de éxito: tienes experiencia iterando sobre un parche en
respuesta a comentarios, y tu commit al final sigue siendo
un único commit limpio en lugar de un historial desordenado.

Tiempo estimado: variable, en función de la disponibilidad del colega.

Variante: si no tienes un colega disponible, pide a alguien que lea
el código de ejemplo y actúe como revisor ficticio. También puedes usar un simulador
de revisión de código en línea si dispones de uno en tu entorno.

## Ejercicios de desafío

Los ejercicios de desafío son opcionales, pero muy recomendables. Cada uno
de ellos toma una idea del capítulo y la lleva a un territorio que pondrá
a prueba tu criterio.

### Desafío 1: Auditar un driver histórico

Elige un driver más antiguo de `/usr/src/sys/dev/` que lleve en el
árbol al menos cinco años. Examina su estado actual e identifica:

- Partes de la cabecera de copyright que no se ajustan a las convenciones modernas.
- Infracciones de estilo que `checkstyle9.pl` detecta.
- Secciones de la página de manual que no se ajustan al estilo moderno.
- APIs obsoletas que el driver todavía utiliza.

Redacta tus hallazgos como un breve informe. No presentes un parche
para corregirlos (los drivers más antiguos suelen tener buenas razones para su
forma histórica), pero comprende por qué tienen el aspecto que tienen.

El objetivo es desarrollar el ojo para distinguir entre convenciones modernas y
convenciones históricas. Después de este ejercicio, reconocerás de un vistazo
qué partes de un driver se escribieron recientemente y cuáles son legado.

Tiempo estimado: dos horas.

### Desafío 2: Depuración entre arquitecturas

Toma tu driver e intenta compilarlo para una arquitectura distinta a la nativa,
por ejemplo `arm64` si estás en `amd64`:

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

Identifica cualquier advertencia o error específico de la arquitectura de destino.
Corrígelos. Vuelve a compilar. Repite.

Si tu driver compila correctamente en `amd64` y en `arm64`, prueba con
`i386`. Si quieres un desafío adicional, prueba con `powerpc64` o
`riscv64`. Cada arquitectura sacará a la luz diferentes tipos de
problemas.

Escribe una breve nota sobre lo que encontraste y cómo lo corregiste. La disciplina
entre arquitecturas es una de las cosas que separa un driver escrito
de forma casual de uno de calidad para producción.

Tiempo estimado: entre tres y seis horas, dependiendo del número de
arquitecturas que pruebes.

### Desafío 3: Profundidad en la página de manual

Elige un driver del árbol cuya página de manual te parezca impresionante.
Copia la estructura de esa página y úsala como plantilla para
reescribir la tuya con un nivel de profundidad similar.

Tu página de manual reescrita debe:

- Tener un SYNOPSIS que muestre todas las formas de cargar y configurar
  el driver.
- Tener una DESCRIPTION que ofrezca al usuario una imagen completa de
  lo que hace el driver.
- Tener una sección HARDWARE completa, incluyendo información de revisión
  donde sea relevante.
- Tener secciones LOADER TUNABLES, SYSCTL VARIABLES o DIAGNOSTICS
  si tu driver dispone de estas características.
- Tener una sección BUGS que sea honesta sobre los problemas conocidos.
- Pasar `mandoc -Tlint` e `igor` sin advertencias.

El objetivo es producir una página de manual que se lea como un
ejemplo de primera categoría dentro del género, no como un artefacto de cumplimiento mínimo.

Tiempo estimado: entre tres y cinco horas.

### Desafío 4: Revisión cruzada simulada

Busca a otro lector de este libro, o a un colega familiarizado con FreeBSD. Intercambiad drivers. Tú revisas el suyo.
Él revisa el tuyo.

Como revisor, haz lo siguiente con el driver que estás revisando:

- Ejecuta todas las pruebas previas a la presentación tú mismo y captura los
  resultados.
- Lee el código con detenimiento. Haz comentarios específicos sobre cualquier cosa
  que te parezca poco clara, incorrecta o no idiomática.
- Lee la página de manual. Haz comentarios sobre cualquier cosa que parezca
  incompleta o poco clara.
- Escribe una nota de revisión resumida que incluya tu impresión general,
  los cambios que solicitas y las preguntas que tienes.

Como colaborador que recibe la revisión, haz lo siguiente:

- Lee los comentarios con atención.
- Responde a cada comentario de forma constructiva.
- Actualiza el parche.
- Envía el parche actualizado de vuelta.

Realiza al menos dos rondas. Al final, escribe una breve reflexión sobre
lo que aprendiste de ambos lados.

El objetivo es experimentar los dos lados del proceso de revisión
antes de presentar nunca un parche al proyecto real. Después de este ejercicio,
la primera revisión real te resultará mucho más familiar.

Tiempo estimado: variable, pero al menos un fin de semana completo.

### Desafío 5: Rastrear la vida de un commit real

Elige un commit reciente relacionado con un driver en el árbol de FreeBSD, a ser posible
uno que haya sido aportado por alguien que no es committer y que cuente con un patrocinador. Usa
`git log` para encontrarlo, o navega por los archivos de Phabricator.

Rastrea su historia:

- ¿Cuándo se abrió la revisión por primera vez?
- ¿Qué aspecto tenía la primera versión?
- ¿Qué comentarios dejaron los revisores?
- ¿Cómo respondió el autor?
- ¿Cómo evolucionó el parche?
- ¿Cuándo se incorporó finalmente?
- ¿Qué dice el mensaje del commit final?

Redacta una pequeña narrativa de lo que encontraste. Este ejercicio
desarrolla la intuición sobre cómo se ve una revisión real desde
dentro.

Tiempo estimado: dos horas.

## Resolución de problemas y errores frecuentes

Incluso con una preparación cuidadosa, las cosas pueden salir mal. Esta sección recoge los problemas más habituales que encuentran los colaboradores primerizos y explica cómo diagnosticarlos y resolverlos.

### Parche rechazado por cuestiones de estilo

Síntoma: los revisores dejan muchos comentarios pequeños sobre la indentación, los nombres de variables, el formato de los comentarios o los paréntesis de las sentencias return.

Causa: el parche se envió sin ejecutar `tools/build/checkstyle9.pl` antes, o el autor ignoró algunas advertencias.

Solución: ejecuta `checkstyle9.pl` contra cada archivo de código fuente. Corrige cada advertencia. Vuelve a compilar y a probar. Reenvía el parche.

Prevención: incorpora `checkstyle9.pl` a tu script de pre-envío. Ejecútalo antes de cada envío.

### Parche rechazado por problemas en la página de manual

Síntoma: el revisor indica que la página de manual tiene errores de lint, o que no cumple el estilo mdoc del proyecto.

Causa: la página de manual no se validó con `mandoc -Tlint` antes del envío, o no se siguió la regla de una oración por línea.

Solución: ejecuta `mandoc -Tlint` contra la página de manual. Corrige cada advertencia. Lee la salida renderizada para verificar que se lee bien. Reenvía el parche.

Prevención: trata la página de manual con el mismo cuidado que el código. Inclúyela en tu script de pre-envío.

### El parche no se aplica limpiamente

Síntoma: el revisor informa de que el parche no se aplica al HEAD actual, o el CI falla en la etapa `git apply`.

Causa: el parche se generó contra una versión antigua del árbol, y HEAD se ha movido desde entonces.

Solución: descarga el HEAD más reciente, haz rebase de tu rama sobre él, resuelve cualquier conflicto, vuelve a probar y regenera el parche.

Prevención: haz rebase sobre el HEAD actual justo antes de enviar. No envíes un parche generado hace una semana.

### Kernel panic al cargar

Síntoma: `kldload` provoca un panic en el kernel.

Causa: con frecuencia, una desreferencia de puntero NULL en la rutina `probe` o `attach` del driver, o un paso de inicialización omitido.

Solución: depura con las herramientas estándar de debug del kernel (tratadas en el Capítulo 34). Las causas específicas más habituales son:

- `device_get_softc(dev)` devuelve NULL porque el campo `driver_t.size` no está establecido a `sizeof(struct mydev_softc)`.
- `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` devuelve NULL y el driver no comprueba el NULL antes de usar el resultado.
- Una variable static inicializada incorrectamente, lo que provoca comportamiento indefinido.

Prevención: prueba en una VM de desarrollo antes de enviar. Carga y descarga el módulo repetidamente para detectar errores de inicialización.

### Kernel panic al descargar

Síntoma: `kldunload` provoca un panic, o el módulo se niega a descargarse.

Causa: la ruta de detach está incompleta. Las causas específicas más habituales son:

- Un callout que sigue programado cuando se libera el softc. Usa `callout_drain`, no `callout_stop`.
- Una tarea del taskqueue que sigue pendiente. Usa `taskqueue_drain` en cada tarea.
- Un manejador de interrupción que sigue instalado cuando se libera el recurso. Desmonta el manejador con `bus_teardown_intr` antes de llamar a `bus_release_resource`.
- Un nodo de dispositivo que sigue abierto cuando se llama a `destroy_dev`. Usa `destroy_dev_drain` si el nodo puede estar abierto.

Solución: audita la ruta de detach. Asegúrate de que cada recurso se libera, cada callout se drena, cada tarea se drena, cada manejador se desmonta y cada nodo de dispositivo se destruye antes de liberar el softc.

Prevención: estructura el código de detach en orden inverso al de attach. Cada paso de `attach` tiene su correspondiente paso de `detach`, y el orden es estricto.

### El driver compila pero no hace probe

Síntoma: el módulo carga, pero cuando el hardware está presente, el driver no se vincula a él. `pciconf -l` muestra el dispositivo sin driver.

Causa: habitualmente, una discrepancia en la rutina `probe` entre el vendor/device ID esperado por el driver y el real, o el driver usa `ENXIO` de forma incorrecta.

Solución: comprueba los vendor y device ID. Verifica con `pciconf -lv`. Confirma que `probe` devuelve `BUS_PROBE_DEFAULT` o `BUS_PROBE_GENERIC` cuando el dispositivo coincide, no un código de error.

Prevención: prueba con hardware real antes de enviar.

### La página de manual no se muestra

Síntoma: `man 4 mydev` no muestra nada, o muestra el código fuente mdoc en bruto.

Causa: habitualmente, el archivo está en el lugar equivocado, no está nombrado correctamente, o no se ha ejecutado `makewhatis`.

Solución: verifica la ruta (`/usr/share/man/man4/mydev.4`), verifica el nombre (debe terminar en `.4`) y ejecuta `makewhatis /usr/share/man` para reconstruir la base de datos de manuales.

Prevención: prueba la instalación de la página de manual antes de enviar.

### El revisor no responde

Síntoma: enviaste un parche, respondiste a los comentarios iniciales y después el revisor dejó de responder.

Causa: los revisores son voluntarios. Su tiempo es limitado. A veces un parche se pierde de vista.

Solución: espera al menos una semana. Después envía un recordatorio amable en el hilo de revisión o en la lista de correo correspondiente. Si sigue sin respuesta, considera pedir que otro revisor lo tome.

Prevención: envía parches pequeños, bien preparados y fáciles de revisar. Los parches más pequeños reciben revisiones más rápidas.

### El parche está aprobado pero no ha sido confirmado

Síntoma: un revisor ha dicho explícitamente que el parche tiene buena pinta, pero no ha sido confirmado en el árbol.

Causa: el revisor puede no ser un committer, o puede serlo pero estar esperando una segunda opinión, o estar ocupado con otras cosas.

Solución: pregunta amablemente si alguien está en condiciones de confirmar el parche. «¿Puede alguien apadrinar el commit de este parche? He respondido a todos los comentarios y la revisión está aprobada.»

Prevención: ninguna en particular; esto forma parte del flujo normal del proyecto.

### El parche fue confirmado pero no se te acreditó

Síntoma: consultas el registro de commits y ves tu parche confirmado, pero el campo de autoría es incorrecto.

Causa: un committer puede haber aplicado el parche accidentalmente sin conservar la autoría. Es poco frecuente, pero ocurre.

Solución: envía un correo amable al committer preguntando si se puede corregir la autoría. Un `git commit --amend` con el autor correcto puede arreglarlo antes del push; después del push el commit es inmutable, pero el committer puede añadir una nota o enmendar el mensaje del commit original en casos excepcionales.

Prevención: cuando generes un parche con `git format-patch`, asegúrate de que tu `user.name` y `user.email` están configurados correctamente.

### Tu driver fue aceptado pero la elección de interfaz fue incorrecta

Síntoma: tu driver está en el árbol, pero más tarde te das cuenta de que la interfaz de espacio de usuario que diseñaste no era la más adecuada.

Causa: las decisiones de diseño tomadas antes de tener experiencia de uso completa a veces resultan equivocadas.

Solución: este es un problema de ingeniería real que el proyecto gestiona con regularidad. Las opciones incluyen: añadir una nueva interfaz junto a la antigua y marcar la antigua como obsoleta; documentar la interfaz antigua como legada e introducir una sucesora; o, excepcionalmente, hacer un cambio con ruptura de compatibilidad si el driver tiene suficientemente pocos usuarios como para que la rotura sea aceptable.

Prevención: consulta en la lista sobre el diseño de la interfaz antes de implementarla, especialmente para interfaces que serán visibles en el espacio de usuario durante mucho tiempo.

## Cerrando el capítulo

Enviar un driver al Proyecto FreeBSD es un proceso con muchos pasos, pero no es misterioso. Los pasos, seguidos en orden, llevan de un driver funcional en tu máquina a un driver mantenido en el árbol de código fuente de FreeBSD. El proceso implica entender cómo está organizado el proyecto, preparar los archivos según las convenciones del proyecto, gestionar la licencia correctamente, escribir una página de manual apropiada, probar en las arquitecturas que soporta el proyecto, generar un parche limpio, navegar el proceso de revisión con paciencia y comprometerse con el largo arco del mantenimiento una vez que el driver se integra.

Varios temas han recorrido el capítulo y merecen un resumen final explícito.

El primer tema es que un driver funcional no es lo mismo que un driver listo para upstream. El código que escribiste en el libro era código funcional; convertirlo en algo listo para upstream es trabajo adicional, y la mayor parte de ese trabajo está en pequeñas convenciones más que en grandes cambios. Prestar atención a esas convenciones es la diferencia entre una primera entrega bien recibida y una que queda bloqueada repetidamente en revisión.

El segundo tema es que la revisión upstream es colaborativa, no adversarial. Los revisores al otro lado del parche están intentando ayudar a que tu driver aterrice en una forma que el árbol pueda sostener en el futuro. Sus comentarios son inversiones de su tiempo, no ataques a tu competencia. Responder a esos comentarios de forma productiva, paciente y sustancial es el arte del proceso de revisión. Los colaboradores primerizos que internalizan esta perspectiva tienen revisiones más sencillas que quienes no lo hacen.

El tercer tema es que la documentación, la licencia y el estilo son parte de la calidad de ingeniería, no de la burocracia. La página de manual que escribes es la interfaz a través de la cual los usuarios entenderán tu driver mientras este exista. La licencia que adjuntas determina si el driver puede integrarse en absoluto. El estilo que sigues determina si los futuros mantenedores entenderán el código. Nada de esto es carga administrativa; todo ello forma parte del trabajo de ser ingeniero de software en una base de código compartida y extensa.

El cuarto tema es que la integración es un comienzo, no un fin. El driver en el árbol requiere atención continua: triaje de bugs, correcciones por deriva del API, revisiones en tiempo de release y mejoras ocasionales. Esa atención es menor que el envío inicial, pero es real, y forma parte de lo que convierte un envío puntual en una contribución sostenida al proyecto.

El quinto y más importante tema es que todo esto se puede aprender. Ninguna de las habilidades de este capítulo requiere un talento mayor del que ya has desarrollado a lo largo del libro. Requieren atención al detalle, paciencia con la iteración y disposición a implicarse con una comunidad. Esas cualidades se pueden desarrollar con práctica. Los committers del Proyecto FreeBSD empezaron todos donde estás tú ahora, como colaboradores escribiendo sus primeros parches, y fueron construyendo su posición a través de la misma acumulación constante de trabajo cuidadoso que tú puedes hacer.

Tómate un momento para apreciar lo que ha cambiado en tu conjunto de herramientas. Antes de este capítulo, enviar un driver a FreeBSD era probablemente una aspiración vaga. Ahora es un proceso concreto con un número finito de pasos, cada uno de los cuales has visto en detalle. Los laboratorios te han dado práctica. Los desafíos te han dado profundidad. La sección de errores frecuentes te ha dado un mapa de los escollos más comunes. Si decides enviar un driver real en las próximas semanas o meses, tienes todo lo que necesitas para empezar.

Algunos de los detalles específicos de este capítulo cambiarán con el tiempo. El equilibrio entre Phabricator y GitHub puede inclinarse más hacia GitHub, volver atrás o no moverse en ningún sentido. Las herramientas de comprobación de estilo pueden evolucionar. Las convenciones de revisión pueden recibir pequeños refinamientos. Donde sabemos que una convención está en movimiento, lo hemos indicado. Donde citamos un archivo concreto, lo nombramos para que puedas abrirlo tú mismo y comprobar el estado actual. El lector que confía pero verifica es el lector del que más se beneficia el proyecto.

Estás ahora, en el sentido práctico, preparado para contribuir. Si lo haces o no depende de ti. Muchos lectores de un libro como este nunca contribuyen; está bien, y las habilidades que has desarrollado aquí te sirven en tu propio trabajo de todas formas. Algunos lectores contribuirán una vez, integrarán un parche y seguirán adelante; también está bien, y el proyecto se lo agradece. Un número menor descubrirá que disfruta de la colaboración lo suficiente como para seguir contribuyendo y, con el tiempo, se implicará profundamente. Cualquiera de estos caminos es legítimo. La elección es tuya.

## Puente hacia el Capítulo 38: Reflexiones finales y próximos pasos

Este capítulo ha sido, en cierto sentido, la culminación del arco práctico del libro. Empezaste sin conocimiento del kernel, trabajaste con UNIX y C, aprendiste la forma de un driver de FreeBSD, construiste drivers de caracteres y de red, te integraste con el framework Newbus, y recorriste una serie de temas avanzados que cubren los escenarios especializados a los que se enfrentan los drivers en producción. El capítulo que acabas de terminar te guió por el proceso mediante el cual un driver que has construido puede llegar a formar parte del propio sistema operativo FreeBSD, mantenido por una comunidad de ingenieros y distribuido a los usuarios en cada versión.

El capítulo 38 es el capítulo final del libro. No es otro capítulo técnico. Su papel es diferente. Es una oportunidad para hacer balance de tu progreso, reflexionar sobre lo que has aprendido, valorar en qué punto te encuentras ahora y pensar hacia dónde podrías dirigirte a continuación.

Varios temas del capítulo 37 fluirán de forma natural hacia el capítulo 38. La idea de que el merge es un comienzo y no un final, por ejemplo, se aplica no solo a los drivers individuales, sino a la relación del lector con FreeBSD en su conjunto. Escribir un driver, o dos, o diez, es un comienzo; el compromiso sostenido con el proyecto es el arco más largo. La mentalidad colaborativa que este capítulo defendió en el contexto de la revisión de código es la misma mentalidad que convierte a alguien en un miembro valioso de la comunidad con el tiempo. La disciplina de documentación, licencias y estilo que este capítulo defendió en el contexto de un único driver se escala hasta la disciplina de ser un ingeniero cuidadoso en cualquier base de código de gran tamaño.

El capítulo 38 también abordará temas que este libro no cubrió en profundidad, como la integración con el sistema de archivos, la integración con la pila de red (Netgraph, por ejemplo), los dispositivos USB compuestos, el hotplug de PCI, el ajuste de SMP y los drivers con soporte NUMA. Cada uno de ellos es un tema de entidad propia, y el capítulo final te señalará los recursos que puedes utilizar para estudiarlos por tu cuenta. El libro te ha dado los fundamentos; los temas del capítulo 38 son las direcciones hacia las que puedes extender esos fundamentos.

También están los otros BSDs. Gran parte de lo que has aprendido se transfiere, con modificaciones, a OpenBSD y NetBSD. Los drivers que escribas para FreeBSD pueden encontrar analogías útiles en esos proyectos, y algunos de los temas avanzados de la Parte 7 tienen equivalentes directos en cada uno de los otros BSDs. Si te interesa el mundo BSD en su conjunto, el capítulo 38 te sugerirá dónde buscar.

Y está la cuestión de la comunidad. El proyecto FreeBSD no es una abstracción; es una comunidad de ingenieros, documentadores, gestores de versiones y usuarios que juntos producen y mantienen el sistema operativo. El capítulo 38 reflexionará sobre lo que significa formar parte de esa comunidad, cómo encontrar tu lugar en ella y cómo contribuir a ella más allá de los envíos de drivers. Las traducciones, la documentación, las pruebas, el triaje de errores y la mentoría son todas formas de contribución, y el proyecto valora cada una de ellas.

Una última reflexión antes de cerrar este capítulo. Enviar un driver es, en el fondo, un acto de confianza. Estás ofreciendo tu código a una comunidad de ingenieros que lo llevará adelante. Ellos, a su vez, están ofreciendo su atención, su tiempo de revisión y sus derechos de commit a un colaborador que era un desconocido hasta que llegó ese parche. La confianza funciona en ambas direcciones. Se construye, con el tiempo, a través de muchos pequeños actos de trabajo cuidadoso y compromiso responsable. El primer envío es el comienzo de esa confianza, no su final. Para cuando seas committer, si ese es el camino que eliges, la confianza será algo que habrás ganado en cientos de pequeñas interacciones.

Has hecho la mayor parte del trabajo para convertirte en alguien en quien el proyecto pueda confiar. El resto es práctica, tiempo y paciencia.

El capítulo 38 cerrará el libro con reflexiones, sugerencias para el aprendizaje continuo y unas palabras finales sobre hacia dónde podrías dirigirte a partir de aquí. Respira, cierra el portátil un momento y deja que el material de este capítulo se asiente. Cuando estés listo, pasa la página.
