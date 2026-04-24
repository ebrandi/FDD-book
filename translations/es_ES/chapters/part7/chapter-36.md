---
title: "Creación de drivers sin documentación (ingeniería inversa)"
description: "Técnicas para desarrollar drivers cuando no se dispone de documentación"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 36
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 180
language: "es-ES"
---
# Creación de drivers sin documentación (ingeniería inversa)

## Introducción

Hasta este punto del libro, siempre hemos escrito drivers para dispositivos cuyo comportamiento estaba al menos parcialmente documentado. A veces la documentación era generosa, con un manual de referencia completo para el programador que nombraba cada registro, definía cada bit y describía cada comando. Otras veces era escasa, con solo un archivo de cabecera del fabricante y una breve lista de opcodes. Incluso en los casos más escasos, sin embargo, siempre teníamos un punto de partida: una pista, un datasheet parcial, un ejemplo de una familia de dispositivos relacionada o una página de manual que nos decía qué se suponía que debía hacer el dispositivo.

Este capítulo cambia esa suposición. Aquí aprenderemos a escribir un driver para un dispositivo cuya documentación está ausente, se ha perdido o ha sido retenida deliberadamente. El hardware existe. Funciona bajo algún otro sistema operativo, o alguna vez lo hizo, o alguien ha capturado unos pocos segundos de su comportamiento con un analizador lógico. Pero no hay referencia de registros, ni lista de comandos, ni descripción de los formatos de datos. Cada dato que necesitamos tendremos que descubrirlo nosotros mismos.

Si eso parece intimidante, respira hondo. La ingeniería inversa de un dispositivo hardware es un oficio serio, pero no es magia. Es la misma disciplina de ingeniería que ya has practicado a lo largo del libro, aplicada en una dirección ligeramente diferente. En lugar de leer un datasheet y escribir código que lo implemente, observaremos el dispositivo, formaremos hipótesis sobre lo que hace, las pondremos a prueba con pequeños experimentos y escribiremos el driver un hecho verificado a la vez. El resultado final es el mismo tipo de driver que hemos escrito en capítulos anteriores, con el mismo `cdevsw`, la misma tabla `device_method_t`, las mismas llamadas a `bus_space(9)` y el mismo build del módulo. La única diferencia es cómo llegamos a su contenido.

La ingeniería inversa merece un capítulo propio por varias razones. La primera es que la situación es más común de lo que los recién llegados suelen imaginar. Hardware antiguo cuyos fabricantes han cerrado; dispositivos de consumo que nunca se documentaron correctamente; periféricos embebidos en una placa personalizada donde el fabricante simplemente entrega un blob binario y el datasheet caduca cuando lo hace el contrato; equipos industriales, científicos o médicos especializados cuya documentación vive en un CD que nadie encuentra. Todas estas son situaciones reales con las que se encuentran desarrolladores de FreeBSD reales, y varios drivers importantes de FreeBSD existen hoy en día únicamente porque alguien tuvo la paciencia de hacer el trabajo.

La segunda razón es que la ingeniería inversa es un trabajo que requiere una disciplina especial, y esa disciplina vale la pena aprenderla aunque nunca escribas un driver completamente obtenido mediante ingeniería inversa. El hábito de separar la observación de la hipótesis, el hábito de anotar cada suposición antes de ponerla a prueba y el hábito de negarse a adivinar cuando un error podría dañar el hardware: estos hábitos mejoran el trabajo ordinario de desarrollo de drivers, no solo el trabajo de ingeniería inversa.

La tercera razón es que este trabajo ocurre en una frontera donde la distinción entre escribir software y hacer ciencia experimental se vuelve muy tenue. Una sesión de ingeniería inversa se parece más a un cuaderno de laboratorio que a una sesión de programación. Ejecutarás un experimento, observarás un resultado, anotarás lo que viste, propondrás una explicación, diseñarás el siguiente experimento y construirás poco a poco una imagen de un sistema desconocido. Ese tipo de trabajo tiene su propio ritmo, su propia cadencia y su propio conjunto reducido de hábitos profesionales, y este capítulo es donde los aprenderemos.

Comenzaremos preguntándonos por qué la ingeniería inversa es necesaria en absoluto y dónde están los límites legales y éticos. A continuación, construiremos un laboratorio pequeño, seguro y controlado donde el trabajo pueda realizarse sin riesgo para los sistemas en producción ni para el hardware costoso. Estudiaremos las herramientas estándar compatibles con FreeBSD para observar dispositivos, desde `usbconfig(8)` y `pciconf(8)` hasta `usbdump(8)`, `dtrace(1)`, `ktr(9)` y la propia API `bus_space(9)`. Veremos cómo capturar la secuencia de inicialización de un dispositivo, cómo comparar ejecuciones para aislar bits individuales de significado y cómo ensamblar un mapa de registros mediante experimentos.

Estudiaremos cómo reconocer las formas recurrentes que el hardware suele utilizar: ring buffers, colas de comandos, descriptor rings, pares de registros de estado y control, paquetes de comandos de formato fijo. Escribiremos un driver mínimo desde cero, una función verificada a la vez, comenzando por el reset y añadiendo lentamente más comportamiento. Validaremos en un simulador todas las suposiciones que podamos antes de arriesgarlas en hardware real. Estudiaremos cómo la comunidad de FreeBSD ya aborda este trabajo, dónde encontrar trabajo previo y cómo publicar los resultados en una forma que ayude a otros.

Y por último, porque es la parte que los recién llegados suelen subestimar con más frecuencia, dedicaremos una cantidad considerable de tiempo a la seguridad. Algunas conjeturas pueden dañar el hardware. Ciertos patrones de exploración de registros desconocidos pueden borrar memoria no volátil, inutilizar una placa o dejar un dispositivo en un estado del que solo una herramienta de reparación exclusiva del fabricante puede recuperarlo. El capítulo te mostrará cómo pensar sobre ese riesgo, cómo diseñar wrappers que lo limiten y cómo reconocer las operaciones que nunca deben realizarse sin evidencia sólida de que son seguras.

El código complementario de este capítulo, en `examples/part-07/ch36-reverse-engineering/`, incluye una pequeña colección de herramientas que puedes construir y ejecutar en una VM de desarrollo de FreeBSD 14.3 normal: un script que identifica y vuelca información del dispositivo para un dispositivo USB o PCI objetivo, un módulo del kernel que realiza una exploración segura de registros en una región de memoria que tú le indicas, un dispositivo simulado que puedes usar para validar el código del driver antes de tener hardware real delante, y una plantilla en Markdown para el tipo de pseudo-datasheet que debería ser el producto final de una sesión de ingeniería inversa. Nada en los laboratorios toca el hardware de una forma que pueda dañarlo, y todos los ejemplos son seguros para ejecutar dentro de una máquina virtual.

Al final de este capítulo, tendrás un método claro y reproducible para abordar un dispositivo no documentado. Sabrás cómo construir el laboratorio, cómo observar, cómo formular hipótesis, cómo probar y cómo documentar. Conocerás los límites legales que enmarcan el trabajo en los Estados Unidos y la Unión Europea, y conocerás los hábitos profesionales que te protegen tanto a ti como al hardware. No terminarás este capítulo como un experto en ingeniería inversa, porque esa pericia se construye a lo largo de años de práctica, pero sabrás lo suficiente para empezar y sabrás cómo mantenerte a salvo, y cómo mantener el hardware a salvo, mientras aprendes.

## Guía para el lector: cómo usar este capítulo

Este capítulo se encuentra en la Parte 7 del libro, en la sección de Temas de Maestría, directamente después del capítulo sobre I/O asíncrono. Da por sentado que has leído el capítulo de I/O asíncrono, el capítulo de depuración avanzada y el capítulo de rendimiento, porque las herramientas y los hábitos de esos capítulos son las mismas herramientas y hábitos que usarás aquí. Si esos capítulos te resultan inciertos, una revisión rápida se amortizará con creces en este.

No necesitas ningún hardware especial para seguir el capítulo. Los ejemplos trabajados utilizan un pequeño módulo del kernel que explora una región de memoria que el operador le indica, o un dispositivo simulado que reproduce en software el comportamiento de hardware desconocido. Ambos funcionan en una máquina virtual de FreeBSD 14.3 normal. Si tienes un dispositivo hardware desconocido real que desearías investigar, el capítulo te dará las técnicas y los hábitos de seguridad para empezar, pero los laboratorios en sí no lo requerirán.

El capítulo es intencionalmente largo porque la ingeniería inversa es un campo donde el conocimiento parcial es peligroso. Un lector que aprende las partes llamativas del oficio y se salta las partes de seguridad probablemente inutilizará algo costoso. Los laboratorios y las secciones de seguridad merecen el mismo cuidado que las secciones de técnicas.

Un plan de lectura razonable tiene este aspecto. Lee las tres primeras secciones en una sola sesión. Son la base conceptual: por qué importa la ingeniería inversa, cómo es el panorama legal y cómo configurar el laboratorio. Toma un descanso. Lee las secciones 4 a 6 en una segunda sesión. Cubren las técnicas centrales del oficio: construir un mapa de registros, identificar buffers y escribir un driver mínimo. Toma otro descanso. Lee las secciones 7 a 11 en una tercera sesión. Cubren el trabajo disciplinado de ampliar, validar, colaborar, mantener la seguridad y documentar. Reserva los laboratorios para un fin de semana o para varias tardes cortas, porque los laboratorios se asimilan mucho mejor si tienes tiempo de sentarte con los datos capturados y examinarlos detenidamente.

Algunas de las técnicas de este capítulo son lentas a propósito. Capturar la secuencia de inicialización de un dispositivo, por ejemplo, puede requerir repetir el mismo boot diez veces y hacer un diff de las capturas para aislar los bits que cambian. El diff es parte de la lección. Si te encuentras con ganas de saltar a la parte donde el driver funciona, recuerda que el driver solo funcionará si has realizado primero el trabajo lento y cuidadoso de observación. La ingeniería inversa recompensa la paciencia como pocas otras partes de la programación de sistemas.

Varios de los pequeños módulos del kernel del capítulo están escritos deliberadamente como andamios de exploración, no como drivers de producción. Están comentados como tales. No los cargues en un sistema en producción. Una máquina virtual de desarrollo donde un kernel panic no cuesta más que un reinicio es el entorno adecuado para este tipo de trabajo.

Si tienes hardware que desearías investigar después de leer el capítulo, hazlo con calma. Empieza con las herramientas de observación más seguras. Resiste el impulso de escribir en cualquier registro hasta que tengas una hipótesis clara y por escrito sobre qué debería hacer esa escritura y cómo sería el peor caso. Si una conjetura podría provocar un borrado de la flash, no hagas esa conjetura. El capítulo detallará qué operaciones merecen una precaución especial y por qué.

## Cómo sacar el máximo partido a este capítulo

El capítulo sigue un patrón que refleja el flujo de trabajo de una sesión de ingeniería inversa real. Cada sección enseña una técnica que encaja en una fase de ese flujo de trabajo o muestra cómo la técnica se conecta con la disciplina subyacente. Si aprendes el flujo de trabajo en su conjunto, las técnicas individuales encajarán de forma natural.

Algunos hábitos te ayudarán a asimilar el material.

Mantén un cuaderno abierto junto al teclado. Un cuaderno real, no un archivo de texto, si puedes. La ingeniería inversa es una disciplina de toma de notas, y quienes mejor dominan esta disciplina mantienen registros escritos de lo que observan, lo que hipotetizan, lo que prueban y lo que aprenden. El acto de escribir ralentiza tu pensamiento lo suficiente para detectar errores, y un cuaderno de papel resiste la tentación de reorganizar el registro a posteriori. Si un cuaderno de papel no es práctico, usa un archivo Markdown en un repositorio Git para que puedas ver cómo ha evolucionado tu comprensión con el tiempo.

Mantén un terminal abierto en tu máquina de desarrollo de FreeBSD y otro abierto en `/usr/src/`. El capítulo hace referencia a varios archivos fuente reales de FreeBSD, incluidos drivers y utilidades bajo `/usr/src/usr.sbin/` y `/usr/src/sys/dev/`. Leer esos archivos es parte de la lección. El código fuente de FreeBSD es en sí mismo un conjunto de trabajo de ingeniería inversa, porque muchos drivers del árbol existen porque alguien observó un dispositivo no documentado con suficiente cuidado como para escribir código para él.

Escribe tú mismo los módulos y scripts de ejemplo la primera vez que los veas. Los archivos complementarios en `examples/part-07/ch36-reverse-engineering/` están ahí como red de seguridad y como referencia para cuando quieras comparar tu código con una versión conocida como correcta, pero escribir el código la primera vez es la parte que construye la intuición. Todo el capítulo trata de construir intuición para sistemas desconocidos, y no hay atajos para eso.

Presta mucha atención al lenguaje que usamos para describir lo que sabemos y lo que sospechamos. La escritura en ingeniería inversa traza una línea clara entre una observación, una hipótesis y un hecho verificado. Una observación es lo que viste. Una hipótesis es lo que crees que significa esa observación. Un hecho verificado es una hipótesis que ha sobrevivido intentos deliberados de refutarla. Los distintos tipos de afirmación merecen distintos niveles de confianza, y el capítulo modelará el lenguaje con cuidado para que puedas adoptar la misma precisión en tus propias notas.

Tómate en serio los consejos de seguridad. El tipo de error más doloroso en este trabajo es el que destruye precisamente la pieza de hardware que necesitabas estudiar. Varias de las técnicas descritas aquí pueden, si se aplican sin cuidado, escribir en la memoria flash, cambiar un ID de dispositivo de forma permanente o dejar una placa en un estado del que el fabricante no puede recuperarla. El capítulo te indicará qué patrones merecen mayor precaución. Trata ese consejo como tratarías una señal de advertencia en un laboratorio de química.

Por último, permítete ir despacio. La ingeniería inversa no es una carrera de velocidad. Un mapa de registros completo para un periférico serio es el resultado de semanas o meses de observación paciente, y los resultados publicados de los proyectos de la comunidad suelen ocultar una enorme cantidad de trabajo minucioso tras un resumen ordenado. Si un determinado dispositivo resiste tu comprensión durante mucho tiempo, eso no es un fracaso. Así es como suele sentirse este trabajo.

Con esos hábitos en mente, empecemos por la pregunta de por qué este trabajo es necesario en absoluto.

## 1. Por qué puede ser necesaria la ingeniería inversa

Cuando un autor novel de drivers oye hablar por primera vez de la ingeniería inversa, la pregunta inmediata suele ser alguna variante de "¿por qué debería ser esto necesario?". Si un fabricante de hardware quiere que su dispositivo sea útil, ¿por qué ocultaría su interfaz de programación? Y si el dispositivo es conocido, ¿cómo puede haber nunca un problema de documentación?

La respuesta honesta es que el mundo del hardware y los sistemas operativos es más complicado que el de los estándares bien documentados. Existen muchas situaciones reales en las que hay un dispositivo funcional, en las que algún sistema operativo ya lo soporta, pero en las que no existe documentación pública, legible por máquina y redistribuible para el programador que quiere escribir un driver desde cero. La primera sección de este capítulo recorre las más frecuentes de estas situaciones para que puedas reconocerlas cuando las encuentres y sepas qué tipo de investigación exige cada una.

### 1.1 Hardware heredado sin soporte del fabricante

La situación de ingeniería inversa más común es la del dispositivo antiguo cuyo fabricante ya no existe. Una pequeña placa de instrumentación de los años noventa, una tarjeta de red de una empresa que fue adquirida y cerrada, un controlador embebido de un proyecto de investigación que duró unos años y luego terminó. El hardware funciona. El hardware estaba documentado cuando se vendió. Pero la documentación era un manual en papel guardado en una carpeta, o un PDF en un CD incluido con el dispositivo, y veinte años después ni la carpeta ni el CD existen.

En esta situación, la ingeniería inversa es la única forma de devolver el dispositivo a un uso productivo. A veces alguna comunidad ya ha hecho parte del trabajo y ha publicado notas parciales. A veces existe un driver de Linux o NetBSD que puede leerse en busca de pistas; los dos casos no son equivalentes, y la distinción importa tanto desde el punto de vista legal como técnico. Un driver de OpenBSD o NetBSD tiene licencia BSD y puede leerse, citarse y, manteniendo la atribución, portarse directamente. Un driver de Linux tiene casi siempre licencia GPL, lo que significa que puede leerse para comprenderlo, pero no puede copiarse en un driver con licencia BSD. Incluso dejando de lado la licencia, un port directo desde Linux rara vez funciona, porque los primitivos de locking del kernel de Linux, sus asignadores de memoria y su modelo de dispositivos difieren de los de FreeBSD de formas que afectan profundamente a cada línea de código. Volvemos tanto al marco legal como a los problemas técnicos de la lectura entre plataformas en la sección 12 y en los casos prácticos de la sección 13. A veces el dispositivo es tan poco conocido que no existe ningún trabajo previo, y toda la tarea recae sobre quien quiera hacerlo funcionar.

FreeBSD tiene una larga historia de soporte de dispositivos en esta categoría. Una lectura cuidadosa del código fuente bajo `/usr/src/sys/dev/` revelará muchos drivers cuyos comentarios mencionan "no datasheet", "based on observation" u otras expresiones similares. Los autores del driver hicieron el trabajo y la comunidad se beneficia. No es una actividad marginal; forma parte de cómo FreeBSD ha dado siempre soporte a una larga cola de dispositivos que sus fabricantes originales abandonaron hace mucho tiempo.

Los desafíos en el caso del hardware heredado tienden a ser técnicos más que legales. El hardware es lo suficientemente antiguo como para que el fabricante original o ya no exista o ya no le importe. Las patentes han caducado. Los secretos comerciales, si los había, ya no se defienden. El riesgo es principalmente que la documentación ha desaparecido de verdad, y ninguna cantidad de preguntas amables la recuperará.

### 1.2 Dispositivos con drivers solo en binario para otros sistemas operativos

Una segunda situación común es la del dispositivo cuyo fabricante publica un driver solo en binario para uno o más sistemas operativos y se niega a publicar documentación que permitiría a otros sistemas operativos dar soporte al dispositivo. Este es el caso de muchos periféricos propietarios: tarjetas gráficas de ciertos fabricantes, dispositivos especializados de captura de audio o vídeo, instrumentos científicos con un driver exclusivo para Windows o Linux, lectores de huellas dactilares en portátiles, determinados chipsets inalámbricos, etcétera.

En esta situación el dispositivo está en producción activa. Su documentación existe, pero el fabricante o bien la trata como un secreto comercial, o bien la restringe a empresas que firman un acuerdo de no divulgación, o simplemente no ha visto ningún motivo comercial para publicarla. La postura oficial del fabricante puede ser que los usuarios de FreeBSD no deben esperar soporte, aunque el hardware subyacente fuera perfectamente capaz de funcionar con un driver de FreeBSD correctamente escrito.

La ingeniería inversa en esta situación es delicada, porque el panorama legal y ético es más complicado que en el caso del hardware heredado. El fabricante puede tener derechos de autor sobre el binario del driver. El firmware que se ejecuta en el dispositivo puede estar protegido por derechos de autor. La distribución y el uso del driver pueden estar regulados por un acuerdo de licencia de usuario final que restringe ciertos tipos de análisis. Volveremos a las cuestiones legales al final de esta sección. Por ahora, basta con señalar que la situación existe y que es una de las razones recurrentes por las que la ingeniería inversa importa.

Los desafíos técnicos en este caso suelen ser más ricos que en el caso heredado, porque hay más material con el que trabajar. Tienes un driver en ejecución que puedes observar. Tienes un dispositivo funcional cuyo comportamiento puedes capturar. Puede que tengas una imagen de firmware que puedas analizar estáticamente. La investigación puede ser muy productiva, pero también requiere prestar más atención al contexto legal del trabajo.

### 1.3 Sistemas embebidos personalizados con poca o ninguna documentación

Una tercera situación, cada vez más frecuente en el trabajo industrial y embebido, es la de la placa personalizada con un chip personalizado. Una pequeña empresa diseña un instrumento o un controlador para una aplicación concreta. Encargan un circuito integrado personalizado, o programan un microcontrolador estándar con firmware propietario, o ensamblan una placa con componentes estándar en una configuración que nunca se ha utilizado en otro lugar. Dan soporte al dispositivo únicamente en su propio entorno operativo, frecuentemente una distribución de Linux personalizada o un pequeño sistema operativo de tiempo real.

Cuando esa empresa pide a un contratista que integre el dispositivo en un sistema mayor que ejecuta FreeBSD, o cuando un usuario final compra el hardware y quiere usarlo desde FreeBSD, la única información disponible puede ser un dibujo mecánico de una página, un breve archivo README y el firmware binario. Sin mapa de registros, sin conjunto de comandos, sin descripción de cómo arranca el dispositivo.

Este caso es en ciertos aspectos el más difícil, porque el dispositivo es específico de una empresa y un cliente. No hay comunidad, porque nadie más tiene uno. No hay ningún driver previo que leer, porque nadie más ha escrito uno. El investigador está verdaderamente solo con el hardware, el tráfico capturado y todo lo que pueda deducirse de observar el firmware existente. En los laboratorios veremos cómo abordar este tipo de investigación de forma sistemática, y en la sección 9 veremos cómo documentar tus propios hallazgos con suficiente detalle para que otra persona pueda construir sobre ellos más adelante.

### 1.4 El hilo conductor común

Todas estas situaciones comparten una sola propiedad subyacente: el dispositivo existe y funciona, pero falta la descripción que permitiría escribir un nuevo driver a partir de una especificación. El oficio mecánico de escribir el driver es el mismo que hemos practicado en el resto del libro. Lo que cambia es la forma del trabajo que precede a la escritura. Tenemos que descubrir lo que normalmente buscaríamos en una referencia. Eso es lo que enseña este capítulo.

Vale la pena notar lo que la ingeniería inversa no es. No es adivinar. No es tantear registros al azar con la esperanza de que ocurra algo interesante. No es intentar eludir la protección de copia, romper el cifrado ni hacer nada más que se adentre en un universo ético diferente. Es el proceso paciente, estructurado y documentado de inferir cómo funciona un componente de hardware observando lo que hace y lo que produce, y luego escribir software que interactúe con él correctamente.

Un proyecto de ingeniería inversa bien realizado se parece mucho más a una ciencia de laboratorio que al estereotipo del hacker. Hay una hipótesis, un experimento, una medición y una conclusión, repetidos cientos de veces hasta que se acumulan suficientes conclusiones para escribir un driver que funcione. El romance de la escena en la que alguien teclea frenéticamente y una pantalla de texto revela "el secreto" solo existe en las películas. La realidad se parece más a una construcción lenta, metódica y prolongada del conocimiento, un registro y un bit a la vez.

### 1.5 Consideraciones legales y éticas

Antes de tocar ninguna herramienta, tenemos que hablar del marco legal que rodea este trabajo. El panorama no es complicado, pero es real, y un principiante que lo ignore puede tropezar con problemas que nada en las secciones técnicas de este capítulo le ayudará a resolver.

La ingeniería inversa con fines de interoperabilidad, que es el propósito que nos ocupa en este capítulo, está ampliamente aceptada tanto en el derecho de Estados Unidos como en el de la Unión Europea. El objetivo del trabajo de interoperabilidad es permitir que un dispositivo, formato o interfaz se utilice con un software que el fabricante original no proporcionó. Escribir un driver de FreeBSD para un adaptador USB Wi-Fi que se distribuye con un driver de Windows es un caso de libro de interoperabilidad. El driver permite que FreeBSD se comunique con un componente de hardware que el usuario ya posee. No copia el driver del fabricante. No redistribuye el firmware del fabricante de una forma que vulnere una licencia. No elude ninguna medida de seguridad que proteja contenido con derechos de autor. Produce un software independiente que realiza la misma función que el driver del fabricante, escrito a partir de una comprensión limpia de la interfaz subyacente.

En Estados Unidos, el marco legal pertinente es la doctrina del uso justo ("fair use") en el derecho de autor, con una larga serie de resoluciones judiciales que reconocen la ingeniería inversa con fines de interoperabilidad como un uso justo legítimo. El caso Sega contra Accolade de 1992, el caso Sony contra Connectix del año 2000 y varios precedentes similares han establecido que desensamblar código para comprender su interfaz es un uso justo, siempre que el propósito sea la interoperabilidad legítima y el producto resultante no contenga el código original protegido por derechos de autor. La ley que en ocasiones complica las cosas es la Digital Millennium Copyright Act, que prohíbe eludir "medidas tecnológicas" que protegen obras con derechos de autor. La DMCA incluye exenciones específicas para la investigación de interoperabilidad, pero esas exenciones son más estrechas que el derecho subyacente de uso justo. Para el trabajo ordinario de drivers, la DMCA rara vez supone un problema, pero si alguna vez necesitas vencer el cifrado para leer firmware, el panorama legal se vuelve más complejo y consultar a un abogado real pasa a ser una inversión que merece la pena.

En la Unión Europea, el marco pertinente es la Directiva sobre software, originalmente la Directiva 91/250 y actualizada en 2009 como Directiva 2009/24. El artículo 6 de la Directiva sobre software permite explícitamente la descompilación con el fin de lograr la interoperabilidad con un programa creado de forma independiente, siempre que se cumplan varias condiciones: la persona que realiza la descompilación tiene derecho a utilizar el programa, la información necesaria para la interoperabilidad no ha estado fácilmente disponible, y la descompilación se limita a las partes del programa necesarias para la interoperabilidad. Es uno de los reconocimientos legales más explícitos de la ingeniería inversa con fines de interoperabilidad en cualquier gran jurisdicción.

Fuera de esos dos sistemas, el panorama varía. Muchos países siguen principios similares en la práctica. Unos pocos no. Si trabajas en una jurisdicción donde la ley no está clara, o donde la aplicación es impredecible, consulta a un abogado que conozca realmente el derecho local de autor en materia de software. El coste de una sola hora de asesoramiento jurídico es pequeño comparado con el coste de aprender la lección por las malas.

Existe una línea ética clara entre el trabajo de interoperabilidad y el trabajo que causaría daño al fabricante o al usuario. El trabajo de interoperabilidad produce un nuevo programa que permite a un dispositivo hardware realizar su función prevista bajo un nuevo sistema operativo. No redistribuye código protegido por derechos de autor. No elimina las restricciones de licencia de un producto adquirido. No elude medidas de seguridad que protegen algo distinto del interés comercial del fabricante en la propia interfaz. Si te encuentras queriendo hacer cualquiera de esas cosas, ya no estás realizando trabajo de interoperabilidad, y el resto del capítulo no se aplica a tu situación.

Una segunda línea ética es la que separa la observación de la manipulación. Observar cómo se comporta un dispositivo es observación. Capturar el tráfico entre el dispositivo y el driver del fabricante es observación. Leer el firmware que el fabricante distribuyó en una forma destinada a ser distribuible es observación. Escribir tu propio driver basado en lo que observaste es el resultado legítimo de ese proceso. Escribir firmware modificado que reemplaza el firmware del fabricante en el dispositivo, o distribuir dicho firmware modificado, es una categoría diferente de trabajo que conlleva consideraciones legales y éticas distintas. No abordaremos esa actividad en este capítulo. El capítulo trata de escribir un driver limpio y original que se comunica con el hardware original en su configuración original.

Una tercera consideración ética es la honestidad sobre tu trabajo. Documenta de dónde proviene tu información. Si una definición de registro concreta provino de leer el driver de código abierto del fabricante, indícalo. Si un formato de paquete provino de una especificación publicada, cítala. Si un comportamiento se dedujo de tus propias capturas, describe las capturas. Esta honestidad es en parte una cuestión legal, porque te permite demostrar que realizaste un trabajo de sala limpia, y en parte una cuestión comunitaria, porque permite a otros construir sobre tu trabajo sin tener que rehacer las partes que tú ya has completado.

### 1.6 Cerrando la sección 1

Esta sección ha preparado el terreno para el resto del capítulo. Hemos visto las tres situaciones más habituales en las que la ingeniería inversa resulta necesaria: hardware heredado, dispositivos con soporte exclusivo del fabricante en otros sistemas operativos, y sistemas embebidos a medida sin documentación. Hemos comprobado que la propiedad subyacente es la misma en todos los casos: un dispositivo que funciona pero cuya interfaz de programación no está documentada. Y hemos repasado el marco legal que rodea este trabajo en Estados Unidos y en la Unión Europea, con una distinción clara entre el trabajo legítimo de interoperabilidad y otras actividades de mayor riesgo jurídico que este capítulo no abordará.

La siguiente sección pasa del porqué al cómo. Pondremos en marcha el pequeño laboratorio donde se desarrollará el trabajo de ingeniería inversa e introduciremos las herramientas que necesitarás. El laboratorio es la base de todo lo que viene a continuación, y unas pocas horas dedicadas a montarlo correctamente te ahorrarán muchas horas de confusión más adelante.

## 2. Preparar el proceso de ingeniería inversa

Una sesión de ingeniería inversa es, en esencia, una actividad experimental. Realizarás experimentos sobre un dispositivo de hardware, capturarás los resultados, los analizarás y diseñarás experimentos de seguimiento. Como cualquier actividad experimental, se beneficia de un laboratorio correctamente equipado. El laboratorio no tiene que ser caro. La mayor parte de lo que necesitamos es software que ya forma parte del sistema base de FreeBSD, y el resto puede reunirse a partir de una breve lista de herramientas gratuitas o de bajo coste. La inversión consiste principalmente en configurar el equipo correctamente para que tus capturas sean fiables y tus experimentos reproducibles.

Esta sección recorre el kit completo. Comienza con el modelo mental de qué hace el laboratorio, luego enumera las herramientas de software, después trata las herramientas de hardware opcionales, describe cómo arrancar un driver del fabricante bajo otro sistema operativo en la misma máquina para observar su comportamiento y, por último, propone un flujo de trabajo para mantener el laboratorio organizado.

### 2.1 El modelo mental: para qué sirve el laboratorio

Antes de enumerar las herramientas, conviene imaginarse cómo quedará el laboratorio una vez montado. El laboratorio es el lugar donde:

1. **Identificarás** el dispositivo, registrando todos los datos públicos disponibles sobre él.
2. **Observarás** el dispositivo en estados de funcionamiento conocidos.
3. **Capturarás** el tráfico entre el dispositivo y un driver existente.
4. **Experimentarás** con lecturas y escrituras para descubrir el comportamiento de los registros.
5. **Documentarás** cada observación en el momento en que la realices.
6. **Validarás** cada hipótesis mediante experimento, preferiblemente con un simulador antes de arriesgar el dispositivo real.

El laboratorio es, por tanto, un pequeño sistema diseñado para sostener el bucle observar-hipotetizar-probar-documentar que impulsa todo el oficio. Cada herramienta que añadas debe servir a ese bucle de alguna manera identificable. Las herramientas que parecen impresionantes pero no alimentan ese bucle son ruido, y el ruido te ralentiza.

El lector que se adentra por primera vez en este terreno a veces da por supuesto que la ingeniería inversa requiere equipamiento profesional muy caro. No es así. Una modesta máquina de desarrollo con FreeBSD, un sistema objetivo capaz de ejecutar el driver del fabricante y un pequeño presupuesto para cables y adaptadores bastarán para afrontar la mayor parte de los proyectos. Las herramientas caras están bien tenerlas y las mencionaremos, pero el trabajo fundamental se hace con el software al que ya tienes acceso.

### 2.2 El kit de herramientas de software del sistema base de FreeBSD

El sistema base de FreeBSD incluye una notable colección de herramientas que, en conjunto, cubren la mayor parte de lo que un autor de drivers necesita en la vertiente de software del laboratorio. Las repasaremos en el orden en que una sesión típica de ingeniería inversa suele utilizarlas.

**`pciconf(8)`** es el punto de partida para cualquier dispositivo PCI o PCI Express. Es una interfaz hacia el ioctl de `pci(4)` que el kernel expone a través de `/dev/pci`. Ejecutado como root, lista todos los dispositivos PCI que el kernel ha enumerado, incluidos aquellos para los que no se ha cargado ningún driver. Las invocaciones más importantes para la ingeniería inversa son:

```sh
$ sudo pciconf -lv
```

Esto produce un resumen de una línea por cada dispositivo PCI del sistema, seguido de las cadenas legibles de fabricante y dispositivo si una base de datos conocida los reconoce. Los dispositivos sin driver aparecen como `noneN@pci0:...`. Para cada dispositivo desconocido, esta línea te indica la dirección del bus PCI, el identificador del fabricante, el identificador del dispositivo, los identificadores de subsistema, el código de clase y la revisión. Esos identificadores son la primera pieza de información pública que registrarás sobre el dispositivo.

```sh
$ sudo pciconf -lvc
```

El flag `-c` añade una lista de las capacidades PCI del dispositivo, como MSI, MSI-X, gestión de energía, capacidades específicas del fabricante y datos de entrenamiento de enlace PCI Express. Repetir `-c -c` aumenta la verbosidad para algunos tipos de capacidades. Para los dispositivos PCI Express, aquí encontrarás también información del estado del enlace que te indica si el dispositivo negoció el ancho y la velocidad de enlace que debería haber negociado. Un número sorprendente de problemas del tipo «este dispositivo no funciona» resultan ser problemas de entrenamiento de enlace que este único comando habría revelado.

```sh
$ sudo pciconf -r device addr:addr2
```

La forma `-r` lee directamente los valores de los registros de configuración PCI, devolviendo los bytes en bruto en un desplazamiento dentro del espacio de configuración. Es la forma más segura de inspeccionar un dispositivo, ya que el espacio de configuración está diseñado para leerse sin efectos secundarios. La forma complementaria `-w` escribe registros de configuración; úsala con extrema precaución, porque algunos registros de configuración cambian el comportamiento del dispositivo de forma permanente.

**`devinfo(8)`** imprime el árbol de dispositivos de FreeBSD tal como lo ve el kernel. Donde `pciconf` te muestra el nivel del bus, `devinfo` te muestra la jerarquía completa: de qué bus cuelga el dispositivo, cuál es el controlador padre, qué recursos le han sido asignados y qué nombre le ha dado el kernel. La forma detallada `devinfo -rv` resulta especialmente útil en las primeras etapas, porque muestra los rangos exactos de memoria y puertos I/O asignados al dispositivo, y esos rangos son el campo de juego en el que se desarrollarán todos los experimentos de bus space.

**`devctl(8)`** es la utilidad de control de dispositivos que te permite desconectar un driver de un dispositivo, conectar un driver diferente, listar eventos y deshabilitar dispositivos concretos a nivel del kernel. Durante la ingeniería inversa, las invocaciones más útiles son `devctl detach deviceN` para retirar el driver del árbol de fuentes de un dispositivo y `devctl attach deviceN` para volver a cargarlo. Desconectar un driver es a veces necesario para que tu driver experimental pueda reclamar el dispositivo, y poder devolver el driver del árbol de fuentes sin reiniciar el sistema ahorra mucho tiempo.

**`usbconfig(8)`** es el equivalente USB de `pciconf`. Enumera los dispositivos USB, vuelca sus descriptores y modifica su estado. Sus invocaciones más importantes para la ingeniería inversa son:

```sh
$ sudo usbconfig
$ sudo usbconfig -d ugen0.3 dump_device_desc
$ sudo usbconfig -d ugen0.3 dump_curr_config_desc
$ sudo usbconfig -d ugen0.3 dump_all_config_desc
```

La primera forma lista todos los dispositivos USB que ve el sistema, con su número de unidad y su dirección. La forma `dump_device_desc` imprime el descriptor de dispositivo USB: bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0, idVendor, idProduct, bcdDevice, las cadenas de fabricante y producto, y el número de configuraciones. Las formas `dump_curr_config_desc` y `dump_all_config_desc` recorren los descriptores de configuración e imprimen las interfaces, las configuraciones alternativas y los endpoints que contienen. Juntos, esos tres comandos te ofrecen una imagen estática casi completa de lo que el dispositivo USB dice ser, y esa imagen es el punto de partida de toda investigación USB.

**`usbdump(8)`** es el equivalente FreeBSD de `usbmon` de Linux. Captura paquetes USB abriendo `/dev/bpf` y asociándose a una interfaz clonada `usbusN` creada por el módulo de filtrado de paquetes `usbpf`, y escribe los paquetes capturados en un archivo con un formato compatible con libpcap. Las capturas pueden guardarse con `-w file`, reproducirse con `-r file` y filtrarse con el lenguaje estándar de expresiones BPF. Para la ingeniería inversa, el flujo de trabajo habitual es:

```sh
$ sudo usbdump -i usbus0 -w session1.pcap
```

Esto captura todo el tráfico del bus USB indicado en un archivo. Una vez realizada la captura, el archivo puede leerse de nuevo con `usbdump -r session1.pcap`, abrirse en Wireshark o procesarse con scripts personalizados. El formato del archivo capturado registra cada transferencia USB, incluidos los paquetes SETUP, los datos IN y OUT, las respuestas de estado y la información de temporización. Las múltiples sesiones capturadas durante distintas operaciones pueden compararse entre sí para aislar los paquetes responsables de un comportamiento concreto, y esa comparación es una de las técnicas más eficaces del kit.

**`dtrace(1)`** es la herramienta de rastreo dinámico que hemos utilizado en capítulos anteriores. Para la ingeniería inversa, DTrace resulta especialmente útil para rastrear los puntos en los que el kernel interactúa con un driver: qué `device_probe` se está invocando para qué dispositivo, qué manejadores de interrupción se activan y cuándo, y qué operaciones `bus_space` realiza el driver del árbol de fuentes. Unas pocas sondas DTrace bien elegidas pueden decirte con detalle qué está haciendo el driver existente, incluso cuando no existe ninguna otra documentación.

**`ktr(9)`** es la herramienta de rastreo en el kernel utilizada para el seguimiento detallado de eventos. Es más intrusiva que DTrace, porque requiere opciones del kernel en el momento de la compilación, pero ofrece un registro de alta resolución de cada evento rastreado. Para la ingeniería inversa, `ktr` resulta más útil cuando se añade a tu propio driver experimental, de modo que el orden temporal de los accesos a registros pueda reconstruirse con exactitud.

**`vmstat -i`** y **`procstat`**: utilidades más pequeñas que te permiten observar la tasa de interrupciones que genera un dispositivo y los procesos que interactúan con él. Ambas forman parte del sistema base. Durante un experimento, puede ser útil observar cómo cambia el contador de interrupciones mientras ejercitas el dispositivo, porque un cambio repentino en la tasa de interrupciones es en sí mismo una observación significativa.

**`hexdump(1)`**, **`xxd(1)`**, **`od(1)`**, **`bsdiff(1)`** y **`sdiff(1)`**: utilidades ordinarias del espacio de usuario para examinar datos binarios y comparar archivos. Las usarás constantemente. Un archivo de captura visualizado con `xxd` es con frecuencia donde los patrones se hacen visibles por primera vez, porque el ojo puede distinguir estructuras repetidas en un volcado hexadecimal que ninguna herramienta automatizada detectaría sin que le indicaras qué buscar. Usar `sdiff` sobre dos salidas de `xxd` es una de las formas más antiguas y fiables de encontrar qué difiere entre dos capturas.

Esta lista no es exhaustiva, pero cubre las herramientas a las que recurrirás durante las primeras semanas de cualquier proyecto. Todas ellas se encuentran en el sistema base de FreeBSD, con páginas de manual accesibles mediante `man pciconf`, `man usbconfig`, etcétera.

Más allá del sistema base, una pequeña familia de desensambladores y decompiladores de terceros cobra importancia cuando el único artefacto que tienes es un binario del proveedor, habitualmente un binario de driver de Windows, una imagen de firmware extraída de un dispositivo o una ROM de opción sacada de una tarjeta PCI. **Ghidra**, la suite de ingeniería inversa de código abierto publicada por la Agencia de Seguridad Nacional de los Estados Unidos, es la herramienta a la que la mayoría de los desarrolladores de FreeBSD recurren en primer lugar porque es gratuita, multiplataforma y descompila con comodidad x86, ARM y muchas arquitecturas embebidas en pseudocódigo legible con aspecto de C. **radare2** y su compañero gráfico **Cutter** son una alternativa de código abierto más ligera, muy adecuada para la inspección rápida de pequeños blobs de firmware. **IDA Pro** es el producto comercial de larga trayectoria en el sector; su decompilador sigue siendo la implementación de referencia en la industria, aunque su precio lo sitúa fuera del presupuesto de la mayoría de los desarrolladores individuales. No necesitas ninguna de estas herramientas para hacer un trabajo excelente con dispositivos cuyo comportamiento puede reconstruirse únicamente a partir de capturas. Cuando sí las necesitas, el objetivo es siempre acotado y documentado: identificar los nombres de los registros, identificar la estructura de los buffers de comandos, comprender el orden en que el código del proveedor programa el hardware. No copias el código del proveedor. Anotas lo que el binario revelaba sobre la interfaz del hardware y luego descartas el desensamblado. Esta es la disciplina clean-room descrita en la sección 1.5 llevada a la práctica: el binario es una fuente de datos sobre el hardware, no una fuente de código que copiar. Usa el desensamblador brevemente, anota lo que aprendiste y construye tu driver a partir de esas notas.

### 2.3 Herramientas de hardware opcionales

Las herramientas de hardware resultan valiosas cuando la interacción del dispositivo con el host no es visible para las capturas por software. El tráfico de un dispositivo USB pasa por el controlador del host y puede capturarse con `usbdump`. Las transacciones de un dispositivo PCI Express atraviesan el complejo raíz y no son capturables con nada de lo que ofrece el sistema base. Un periférico SPI en una placa embebida puede comunicarse con el procesador principal a través de cables que ninguna herramienta del sistema operativo puede ver. Para esos casos, las herramientas de hardware entran en escena.

Un **analizador lógico** es la herramienta de hardware más versátil. Se conecta a los cables y registra el voltaje en cada uno de ellos a lo largo del tiempo, generando una traza digital que puede decodificarse en el protocolo que transportan esos cables. Para buses SPI, I2C, UART y similares de baja velocidad, basta con un analizador lógico básico de ocho o dieciséis canales. La familia Saleae Logic está muy extendida en el trabajo profesional y cuenta con buen soporte en `sigrok`, la suite de código abierto para analizar capturas de analizadores lógicos. La interfaz gráfica `pulseview` de Sigrok permite importar una captura, decodificarla como SPI o I2C, y recorrer el tráfico del bus byte a byte.

Un **analizador de protocolo USB** es un dispositivo hardware especializado que se sitúa en el bus USB y captura cada paquete, incluidos los eventos de estado del bus que no son visibles desde el host. Los analizadores Beagle y Total Phase son las herramientas de gama alta en esta categoría. Son caros, pero para la ingeniería inversa USB seria revelan comportamientos que la captura por software simplemente no puede ver. La mayoría del trabajo a nivel de aficionado, sin embargo, funciona perfectamente con `usbdump` y una metodología cuidadosa.

Un **analizador de protocolo PCI Express** es todavía más especializado, y prácticamente nada en el mundo del código abierto cubre este nicho. Para el trabajo con PCI Express, el recurso habitual es capturar el comportamiento del dispositivo desde el lado del kernel usando DTrace, `ktr(9)` y tu propio driver instrumentado, y usar `pciconf -lvc` para inspeccionar el estado del espacio de configuración. Existen analizadores PCIe reales de empresas como Teledyne y Keysight, pero su coste los sitúa fuera del alcance de la mayoría de los desarrolladores individuales.

Para el trabajo embebido, un **osciloscopio** resulta a veces útil para diagnosticar problemas eléctricos que confunden a las herramientas de nivel superior. Un driver que agota su tiempo de espera por razones desconocidas puede estar haciéndolo porque la señal de reloj del dispositivo está degradada; un osciloscopio lo mostrará cuando ninguna herramienta de software pueda hacerlo. Un osciloscopio modesto conectado por USB es una inversión razonable para cualquiera que realice trabajo embebido en serio.

Es posible hacer un excelente trabajo de ingeniería inversa sin ninguna de estas herramientas de hardware. La mayoría de los dispositivos USB y PCI de consumo son accesibles únicamente a través de capturas por software y la propia introspección del kernel. Las herramientas de hardware cobran importancia cuando la investigación se adentra en territorio verdaderamente de bajo nivel: integridad de señal, temporización de bus, protocolos embebidos, dispositivos diseñados para industrias que nunca esperaban ser analizados desde dentro.

### 2.4 La plataforma de observación

Una vez que dispones de las herramientas, la siguiente decisión es la forma de la plataforma de observación. Esta plataforma es la combinación de máquinas y sistemas operativos en la que observarás el driver existente en funcionamiento. Existen varias configuraciones habituales, cada una con sus propias ventajas.

La configuración más sencilla es **un host, dos sistemas operativos**. La misma máquina física arranca en FreeBSD, donde escribirás el nuevo driver, o en otro sistema operativo cuyo driver ya admite el dispositivo, donde lo observarás. Arrancarás en el otro SO para observar; arrancarás en FreeBSD para desarrollar. La configuración es sencilla y funciona bien cuando el dispositivo está conectado permanentemente al host. La desventaja es que no puedes observar y experimentar en la misma sesión, por lo que la iteración es más lenta.

Una configuración más flexible es **dos hosts**: uno ejecutando el sistema operativo cuyo driver admite el dispositivo, y el otro ejecutando FreeBSD como entorno de desarrollo. El dispositivo puede conectarse a un host, observarse y luego trasladarse al otro. Las capturas, las notas y el código viajan entre los dos a través de la red. Esta configuración funciona bien cuando ambas máquinas caben en un escritorio y cuando el dispositivo puede moverse sin daño.

Para dispositivos USB, **un único host FreeBSD con otro SO en una máquina virtual** suele ser la configuración más eficiente. El dispositivo se conecta al host FreeBSD, donde `usbdump` puede capturar su tráfico. La máquina virtual se configura para recibir el dispositivo mediante passthrough USB, de modo que el driver del fabricante dentro de la máquina virtual ve el dispositivo. Mientras el driver del fabricante opera el dispositivo, `usbdump` en el host registra cada paquete. Esta configuración permite tanto la observación como la iteración rápida en una única sesión, ya que no es necesario reiniciar nada para alternar entre observar y experimentar.

Para dispositivos PCI, el equivalente es el **passthrough de bhyve mediante el driver `ppt(4)`**. El host FreeBSD desconecta el dispositivo de su driver en el árbol de código fuente, lo conecta a `ppt(4)` con la configuración de kernel apropiada, y lo expone a la máquina invitada de bhyve con `bhyve -s slot,passthru,bus/slot/function`. El driver del fabricante en la máquina invitada opera entonces el dispositivo. El passthrough de bhyve es una técnica valiosa para el trabajo con PCI y tiene la gran ventaja de mantener todas las herramientas de observación en un único host FreeBSD.

Para hardware muy especializado, el **hardware de captura dedicado** es la única opción. Un analizador lógico conectado permanentemente al bus SPI de una placa, o un analizador de protocolo USB insertado entre un dispositivo USB y su host, proporciona una observación que ninguna herramienta por software puede ofrecer. La contrapartida es que el entorno es más complejo de configurar y los datos capturados están en un formato que requiere sus propias herramientas para analizarlos.

### 2.5 Instalación del driver del fabricante en otro sistema operativo

Si vas a observar el driver del fabricante en funcionamiento, necesitas una instalación operativa de un sistema operativo que el fabricante admita. La elección depende de lo que el fabricante suministre. Para drivers de Linux, una distribución Linux estable reciente suele ser la opción correcta. Para drivers de Windows, el procedimiento estándar es una instalación de Windows en una máquina virtual; esto funciona bien siempre que el dispositivo pueda pasarse a la máquina invitada. Para sistemas operativos embebidos especializados, la situación es más variable.

Sea cual sea el SO que elijas, instálalo con el mínimo de software adicional posible. El laboratorio debe estar tranquilo. La actividad en segundo plano de otros drivers, las actualizaciones automáticas, la telemetría o los procesos no relacionados añaden ruido a tus capturas. Una instalación ligera permite que el tráfico del dispositivo destaque con claridad.

Para un dispositivo USB cuyo fabricante suministra un driver de Linux, la configuración recomendada es:

1. Instala una distribución Linux estable reciente en una máquina virtual en tu host FreeBSD.
2. Configura bhyve para pasar el dispositivo USB a la máquina invitada.
3. Dentro de la máquina invitada, instala el driver del fabricante y verifica que el dispositivo funciona.
4. En el host, conecta `usbdump` al bus USB a través del cual se comunica el dispositivo.
5. Repite las operaciones del dispositivo en la máquina invitada mientras capturas en el host.

Para un dispositivo USB cuyo fabricante suministra un driver de Windows, la configuración es similar, con Windows en la máquina invitada en lugar de Linux. El passthrough USB de Windows a través de bhyve ha mejorado de forma constante y es viable para la mayoría de los dispositivos de consumo.

Para un dispositivo PCI, la configuración análoga usa el passthrough PCI de bhyve mediante el driver `ppt(4)`. Desconecta el dispositivo de su driver del host con `devctl detach`, conéctalo a `ppt(4)` con la configuración de kernel apropiada, y expónlo a la máquina invitada de bhyve con `bhyve -s slot,passthru,bus/slot/function`. El driver del fabricante en la máquina invitada opera entonces el dispositivo. La observación por software desde el host es más difícil para PCI que para USB, porque los accesos al espacio de configuración y al espacio de memoria no son visibles para el host una vez que el dispositivo ha sido pasado a la máquina invitada. El recurso alternativo es instrumentar intensamente tu propio driver experimental y aprender de las diferencias entre lo que hace tu driver y lo que parece estar haciendo el driver del fabricante.

### 2.6 El cuaderno de laboratorio

Igual de importante que las herramientas es la disciplina de registrar lo que haces. Un cuaderno de laboratorio, en papel o digital, no es opcional en ingeniería inversa. Sin uno, perderás la noción de qué has probado, qué has observado y qué has concluido. Con uno, construyes un artefacto que, por sí solo, forma parte del resultado del proyecto.

El cuaderno debe registrar:

- La fecha y la hora de cada sesión de observación.
- La configuración exacta del laboratorio cuando se realizó la observación: versión del kernel, versiones de las herramientas, el estado del dispositivo antes de que comenzara la observación.
- Los comandos exactos que ejecutaste.
- Los datos capturados, o un puntero claro a dónde están almacenados.
- Tu interpretación inmediata de lo que observaste, con la palabra "observación" o "hipótesis" claramente indicada.
- Cualquier decisión que tomaste sobre qué probar a continuación, y por qué.

Una buena entrada del cuaderno se lee como un protocolo científico. Debe poder ser reproducida por otra persona que tenga acceso al mismo laboratorio, y debe indicar a un lector futuro qué sabías en ese momento y cómo lo sabías. Cuando el proyecto esté terminado y escribas el pseudo-datasheet que resume todo lo que aprendiste, el cuaderno es de donde provienen las referencias. Cuando algo resulte ser incorrecto más adelante, el cuaderno es el lugar al que acudirás para averiguar cuándo entró esa creencia errónea y qué otras conclusiones podrían estar contaminadas por ella.

La disciplina de escribir las hipótesis antes de probarlas es especialmente importante. Sin esa disciplina, es muy fácil convencerse, a posteriori, de que predijiste un resultado que en realidad no predijiste. Con ella, puedes determinar con precisión qué experimentos confirmaron tu comprensión y cuáles te sorprendieron. Las sorpresas son las observaciones más valiosas, porque son los lugares donde tu modelo es incorrecto, pero solo se hacen evidentes cuando la predicción se escribió antes de conocer el resultado.

### 2.7 Un ejemplo de configuración de laboratorio

A modo de ejemplo concreto, aquí tienes una configuración que ha funcionado bien en muchos proyectos de ingeniería inversa en FreeBSD.

El host es un pequeño equipo de escritorio con FreeBSD 14.3 y al menos 16 GB de memoria. Ejecuta la versión en desarrollo del driver, las herramientas de observación y el hipervisor bhyve. Su `/usr/src/` está poblado con el código fuente de FreeBSD para que la navegación por el código sea rápida.

Una máquina virtual dentro de bhyve ejecuta una distribución Linux reciente. La máquina virtual tiene instalado el driver del fabricante y está configurada para passthrough USB o PCI según corresponda.

Un repositorio Git separado, en el host, contiene el cuaderno del proyecto, los archivos pcap capturados, el código del driver experimental y el pseudo-datasheet a medida que crece. Cada commit tiene fecha y descripción, de modo que queda preservado el historial de la comprensión del proyecto.

Un segundo terminal en el host tiene siempre ejecutándose `tail -F /var/log/messages`, para que cualquier mensaje del kernel producido por el driver experimental sea visible de inmediato.

Esta configuración no es la única que funciona, pero es un punto de partida razonable. Las características clave son: un entorno de desarrollo FreeBSD limpio, una forma de ejecutar el driver del fabricante, una forma de observar el dispositivo y un cuaderno con seguimiento en Git que crece con el proyecto.

### 2.8 Cerrando la sección 2

Ahora disponemos de un conjunto de herramientas y un laboratorio. El sistema base nos proporciona `pciconf`, `usbconfig`, `usbdump`, `devinfo`, `devctl`, `dtrace` y `ktr`. Las herramientas de hardware opcionales nos dan mayor visibilidad cuando la captura por software no es suficiente. Bhyve y `ppt(4)` nos dan una forma de ejecutar el driver del fabricante dentro de una máquina virtual mientras observamos desde el host FreeBSD. Y un cuaderno de laboratorio escrito nos aporta la disciplina que convierte la experimentación improvisada en ingeniería reproducible.

La siguiente sección pone el laboratorio en práctica. Veremos cómo capturar el comportamiento de un dispositivo de forma sistemática, cómo reconocer los patrones que la mayoría del hardware utiliza para comunicarse con su driver, y cómo convertir capturas en bruto en los primeros indicios de un modelo mental emergente. Realizaremos trabajo experimental de verdad, y la disciplina que esta sección ha establecido se vuelve esencial cuando empecemos a producir los datos sobre los que se construirá todo lo demás.

## 3. Observación del comportamiento del dispositivo en un entorno controlado

Con el laboratorio en marcha, pasamos ahora a la observación. Esta es la fase en la que recopilas los datos brutos sobre los que se construirá todo lo demás. El objetivo no es comprender el dispositivo todavía. El objetivo es capturar, con la mayor fidelidad posible, lo que hace el dispositivo en un conjunto pequeño de situaciones bien definidas. La comprensión surgirá más adelante, del análisis. La primera tarea es obtener capturas limpias y etiquetadas.

Esta sección recorre las técnicas de observación estándar en el orden en que un proyecto las aplica habitualmente. Comenzamos con los descriptores estáticos, que ofrecen una instantánea de la identidad del dispositivo. Pasamos a las capturas de inicialización, que muestran lo que hace el dispositivo cuando se enciende o se conecta por primera vez. Después examinamos las capturas funcionales, que muestran lo que hace el dispositivo cuando realiza cada una de sus operaciones útiles. A lo largo de todo el proceso, hacemos hincapié en la disciplina de la captura estructurada: cada captura se nombra, se fecha, se etiqueta con la operación que representa y se almacena junto a una nota breve que describe lo que hizo el usuario durante la captura y qué comportamiento se esperaba.

### 3.1 Descriptores estáticos e información de identidad

Lo primero que hay que capturar sobre cualquier dispositivo es su identidad. Para un dispositivo PCI o PCI Express, esto significa registrar la salida de:

```sh
$ sudo pciconf -lv
```

Para el dispositivo concreto, las líneas relevantes tienen este aspecto en la práctica. Supongamos que el dispositivo es el tercer dispositivo PCI sin driver que ve el kernel. Tras ejecutar `pciconf -lv`, podrías ver algo como:

```text
none2@pci0:0:18:0:    class=0x028000 card=0x12341234 chip=0xabcd5678 \
    rev=0x01 hdr=0x00
    vendor     = 'ExampleCorp'
    device     = 'XYZ Wireless Adapter'
    class      = network
    subclass   = network
```

Esta única línea registra seis datos que cualquier análisis posterior necesitará: la ubicación en el bus (`0:18:0`), el código de clase (`0x028000`, que la tabla de códigos de clase de FreeBSD identifica como controlador de red inalámbrica), el identificador de subsistema (`0x12341234`), el identificador del chip (`0xabcd5678`, con vendor ID `0xabcd` y device ID `0x5678`), la revisión (`0x01`) y el tipo de cabecera (`0x00`, un endpoint estándar). Todos estos datos pueden ser relevantes más adelante. El vendor ID y el device ID son la forma en que el kernel encontrará tu driver. El ID de subsistema es a veces la única manera de distinguir dos dispositivos que comparten un chip pero usan disposiciones diferentes. El código de clase indica a qué categoría pertenece el dispositivo. La revisión diferencia las revisiones de silicio que pueden comportarse de manera distinta. Regístralos todos.

Añade la lista de capacidades:

```sh
$ sudo pciconf -lvc none2@pci0:0:18:0
```

Esto añadirá una lista de capacidades PCI, cada una en una única línea con un nombre, un ID y una posición en el espacio de configuración. La lista típica de un dispositivo PCI Express moderno incluye gestión de energía, MSI o MSI-X, PCI Express, capacidades específicas del fabricante y una o varias capacidades extendidas. Las capacidades específicas del fabricante son especialmente interesantes, porque son el lugar donde los fabricantes ocultan funcionalidad no estándar y suelen ser el punto de entrada para la configuración específica del chip.

Para un dispositivo USB, la captura equivalente es:

```sh
$ sudo usbconfig
$ sudo usbconfig -d ugen0.5 dump_device_desc
$ sudo usbconfig -d ugen0.5 dump_curr_config_desc
```

Estos tres comandos producen conjuntamente el descriptor de dispositivo, el descriptor de configuración actual y una lista de todas las configuraciones. Una salida típica para un dispositivo USB sencillo tiene este aspecto:

```text
ugen0.5: <ExampleCorp Foo Device> at usbus0
  bLength = 0x0012
  bDescriptorType = 0x0001
  bcdUSB = 0x0210
  bDeviceClass = 0x00
  bDeviceSubClass = 0x00
  bDeviceProtocol = 0x00
  bMaxPacketSize0 = 0x0040
  idVendor = 0x1234
  idProduct = 0x5678
  bcdDevice = 0x0102
  iManufacturer = 0x0001  <ExampleCorp>
  iProduct = 0x0002  <Foo Device>
  iSerialNumber = 0x0003  <ABC123>
  bNumConfigurations = 0x0001
```

Cada campo es la respuesta a una pregunta. El valor `bcdUSB` indica la versión del protocolo USB que el dispositivo dice implementar. `bDeviceClass`, `bDeviceSubClass` y `bDeviceProtocol` corresponden al sistema de clases USB, que a veces identifica el dispositivo como miembro de una clase estándar (HID, almacenamiento masivo, audio, vídeo, etc.) y a veces deja los tres a cero, lo que significa que la clase se determina por interfaz dentro del descriptor de configuración. `idVendor` e `idProduct` son los identificadores numéricos únicos; combinados, son la forma en que un driver USB se vincula al dispositivo. `iManufacturer`, `iProduct` e `iSerialNumber` son índices en la tabla de cadenas del dispositivo; `usbconfig` los resuelve por ti e imprime las cadenas.

Vuelca también el descriptor de configuración:

```sh
$ sudo usbconfig -d ugen0.5 dump_curr_config_desc
```

Esto muestra los descriptores de interfaz y los descriptores de endpoint. Por cada interfaz verás su número, su configuración alternativa, su clase, subclase y protocolo, y la lista de endpoints. Por cada endpoint verás su dirección (que codifica tanto el número de endpoint como la dirección), sus atributos (que codifican el tipo de transferencia: control, isócrona, bulk o interrupción), su tamaño máximo de paquete y su intervalo de polling si se trata de un endpoint de interrupción.

Esta información estática constituye la identidad completa, visible desde el punto de vista de la programación, del dispositivo USB. Te dice exactamente qué tipos de pipes expone el dispositivo, en qué direcciones, de qué tipo y a qué tamaño. Solo con esto ya puedes hacer algunas suposiciones razonadas. Un dispositivo que expone un único endpoint bulk-IN y un único endpoint bulk-OUT probablemente sirve de transporte para algún protocolo específico de aplicación. Un dispositivo que expone dos endpoints interrupt-IN probablemente es una fuente de eventos de algún tipo. Un dispositivo con endpoints isócronos casi con toda seguridad maneja datos sensibles al tiempo, como audio o vídeo.

Guarda estos volcados en tu cuaderno. Son la identidad estática del dispositivo y no cambiarán entre capturas. Son la cabecera de cada informe que escribirás sobre el dispositivo.

### 3.2 La primera captura: inicialización

Una vez que tienes la identidad estática, la siguiente captura es la secuencia de inicialización. Esta es la secuencia de operaciones que el driver del fabricante realiza entre el momento en que el dispositivo se conecta y el momento en que el dispositivo está listo para su uso.

La secuencia de inicialización es una de las cosas más informativas que puedes capturar, porque normalmente ejercita todos los registros que tiene el dispositivo. El driver escribe valores iniciales, establece opciones de configuración, asigna buffers, configura interrupciones, habilita el flujo de datos e informa del éxito. Casi todos los registros que expone el dispositivo se tocarán al menos una vez durante esta secuencia, y muchos de ellos revelarán su propósito general simplemente por ser tocados de una manera que encaja con el patrón de inicialización estándar.

Para un dispositivo USB, la captura de inicialización es:

```sh
$ sudo usbdump -i usbus0 -w init.pcap
```

Inicia `usbdump`, luego conecta el dispositivo. Una vez que el dispositivo esté completamente inicializado, detén la captura. El archivo pcap resultante contiene todas las transferencias USB que pasaron por el bus entre el dispositivo y su driver, comenzando con la secuencia de enumeración USB (que debería coincidir con los volcados de descriptores estáticos), continuando con las transferencias de control específicas de clase o del fabricante que el driver usa para configurar el dispositivo, y terminando cuando el driver ha puesto el dispositivo en un estado listo.

Abre el archivo en Wireshark para verlo de forma interactiva, o procésalo con `usbdump -r init.pcap` para imprimirlo en formato de texto. Wireshark tiene disectores USB especialmente buenos que decodifican automáticamente muchas transferencias estándar específicas de clase. Para diseccionar transferencias específicas del fabricante, tendrás que leer los bytes en bruto tú mismo.

Para un dispositivo PCI, la captura por software de la inicialización es más difícil, porque las escrituras en el espacio de configuración y en el espacio de memoria no son visibles desde fuera del dispositivo una vez que no se usa passthrough. La técnica habitual es instrumentar tu propio driver experimental, o añadir trazas a una copia del driver del árbol de código fuente si existe alguno. Volveremos a este asunto en la sección 4 cuando hablemos de los mapas de registros. Por ahora, el equivalente de una "primera captura" para un dispositivo PCI es la salida de:

```sh
$ sudo devinfo -rv
```

restringida a tu dispositivo. Esto te muestra los recursos que el kernel ha asignado al dispositivo: los rangos de memoria, los rangos de puertos I/O y la asignación de interrupciones. Esos recursos te indican el alcance del terreno que vas a explorar. No te dicen qué está ocurriendo dentro de ese terreno, pero son las condiciones límite de todo lo que sigue.

### 3.3 Capturas funcionales

Una vez que tienes la inicialización, las siguientes capturas son funcionales. Por cada cosa que el dispositivo puede hacer, tomas una captura separada. Cada captura debe aislar una operación de la manera más limpia posible.

Para un dispositivo de red, podrías tomar capturas separadas para "enviar un ping", "recibir un paquete de tráfico no solicitado", "establecer la dirección MAC", "cambiar el canal". Para un sensor, podrías tomar capturas separadas para "leer una vez", "establecer la frecuencia de muestreo", "habilitar el modo continuo", "calibrar". Para una impresora, podrías capturar "imprimir una página de texto sin formato", "imprimir una imagen", "consultar el estado".

La disciplina de aislar operaciones no es opcional. Si tu archivo de captura contiene cien operaciones diferentes, determinar qué paquetes corresponden a qué operación será imposible. Si tu archivo de captura contiene exactamente una operación, los paquetes que contiene son exactamente los paquetes de esa operación, y tu trabajo es mucho más fácil.

Cada captura debe incluir:

1. La operación exacta que se está capturando, en lenguaje sencillo.
2. Las acciones exactas del usuario que desencadenaron la operación.
3. El momento exacto en que el usuario inició la acción, registrado como marca de tiempo en el nombre del archivo o en una nota adjunta.
4. El comportamiento esperado del dispositivo.
5. El comportamiento real observado.

La nota adjunta es esencial. Dentro de seis meses no recordarás qué captura correspondía a qué operación, y el nombre del archivo por sí solo no siempre será suficiente.

Un esquema de nombres que ha funcionado bien en muchos proyectos es:

```text
init-001-attach-cold.pcap
init-002-attach-hot.pcap
op-001-set-mac-address-aa-bb-cc-dd-ee-ff.pcap
op-002-send-icmp-echo-request.pcap
op-003-receive-broadcast-arp.pcap
err-001-attach-with-no-power.pcap
```

Los prefijos `init-`, `op-` y `err-` agrupan las capturas por propósito. El sufijo numérico es único. La descripción en el nombre del archivo es suficiente para encontrar una captura concreta sin abrirla. La nota adjunta vive junto al archivo como un archivo `.txt` o `.md` con el mismo nombre base.

### 3.4 El diff: aislando los fragmentos de significado

La técnica de análisis más importante en la ingeniería inversa es el diff. Dos capturas de operaciones similares probablemente serán casi idénticas, con algunas diferencias que corresponden a las diferencias en lo que se hizo. Esas diferencias son las partes que importan, porque son las partes cuyo significado es más probable que resulte visible.

Supongamos que tienes una captura de "establecer canal 1" y una captura de "establecer canal 6". Visualmente, las dos capturas parecerán casi idénticas. Comenzarán con la misma configuración inicial, realizarán las mismas operaciones preliminares y terminarán con el mismo cierre. Sin embargo, en algún lugar del medio habrá un pequeño número de bytes que difieren entre las dos capturas. Esos bytes casi con toda seguridad están relacionados con el número de canal. Comparándolos cuidadosamente, puedes deducir: qué transferencia lleva el valor del canal, dónde en esa transferencia vive el valor, qué codificación numérica se usa (valor numérico directo, un índice en una tabla, un campo de bits) y si el valor se envía en little-endian o big-endian.

La técnica del diff funciona mejor cuando las dos capturas difieren en exactamente una variable. Si comparas "establecer canal 1" con "establecer canal 6", y la diferencia de canal a canal es la única diferencia entre las capturas, el diff es limpio. Si comparas "establecer canal 1, modo A" con "establecer canal 6, modo B", tienes dos variables cambiando y el análisis es más difícil. Haz capturas que varíen una sola cosa cada vez.

Para capturas en formato de texto, `sdiff` es la herramienta más sencilla. Para capturas binarias, `bsdiff` produce parches compactos que se pueden inspeccionar para ver exactamente qué bytes cambiaron. Para archivos pcap, la combinación de `tshark -r file.pcap -T fields -e ...` para extraer campos específicos, seguida de `diff`, te proporciona una manera programable de comparar aspectos concretos de las capturas.

Una técnica más sofisticada consiste en capturar múltiples instancias de la misma operación y compararlas. Las diferencias entre capturas de operaciones nominalmente idénticas revelan qué bytes son realmente constantes (las partes del protocolo) y qué bytes cambian entre ejecuciones aunque la operación no lo haga (números de secuencia, marcas de tiempo, valores aleatorios). Los bytes constantes son aquellos cuyo significado quieres deducir; los bytes variables son aquellos cuyo significado puedes ignorar por ahora.

Guarda todas las capturas. El almacenamiento es barato y la técnica del diff se vuelve más útil cuantas más capturas tengas. Un proyecto con cincuenta capturas de una operación puede responder muchas más preguntas que un proyecto con una sola captura, aunque ambos proyectos «hayan capturado la operación».

### 3.5 Wireshark y el disector USB

Para el trabajo con USB en particular, Wireshark es una herramienta imprescindible.
Wireshark disecciona el flujo de paquetes USB en una vista de árbol estructurada
que es mucho más fácil de leer que los bytes en bruto, y dispone de filtros de
visualización que permiten aislar un dispositivo concreto, un endpoint concreto
o una dirección del tráfico.

Los filtros más útiles son:

- `usb.device_address == 5` para limitar la vista a un dispositivo concreto del bus.
- `usb.endpoint_address == 0x81` para limitar la vista a un endpoint y una dirección concretos (en este caso, el endpoint IN 1).
- `usb.transfer_type == 2` para limitar la vista a transferencias bulk (1 = isócrona, 2 = bulk, 3 = interrupción).
- `usb.bRequest == 0xa0` para limitar la vista a una solicitud de control concreta, útil cuando se realiza ingeniería inversa sobre transferencias de control específicas del fabricante.

La combinación de estos filtros permite aislar exactamente la parte de la captura
que te interesa. El menú "Statistics" de Wireshark también ofrece vistas agregadas
útiles, como una lista de todos los endpoints detectados y el número de paquetes
en cada uno. Para USB, la vista "Endpoints" en particular suele ser lo primero que
compruebas al abrir una captura nueva.

Si tienes una captura que Wireshark disecciona en algo específico de clase (por
ejemplo, una captura de USB Mass Storage decodificada en comandos SCSI), el disector
habrá hecho prácticamente el trabajo más difícil por ti. Si tienes una captura que
Wireshark disecciona únicamente como transferencias bulk en bruto, tendrás que leer
los bytes tú mismo.

### 3.6 Patrones de observación que hay que reconocer

Incluso antes de entender un dispositivo concreto, ciertos patrones se repiten en
el protocolo de casi todos los dispositivos, y aprender a reconocerlos acelera
enormemente cada proyecto. Obsérvalos mientras examinas las capturas.

**Escrituras repetidas seguidas de una sola lectura.** Este es el patrón clásico de
"escribe un comando, luego lee el resultado". Las escrituras repetidas suelen configurar
una solicitud: código de comando, parámetros, longitud. La lectura obtiene la respuesta.
Muchos dispositivos utilizan esta forma para cualquier operación que devuelve datos.

**Banderas de estado que cambian antes y después de los eventos.** Un bit en algún
lugar de un registro de estado que se activa cuando el dispositivo termina su trabajo,
o que se desactiva cuando el trabajo comienza, es una de las formas más comunes de que
un dispositivo comunique su progreso. Busca bits que cambien de forma fiable a la par
que las operaciones que el usuario dispara.

**Una secuencia de escrituras en direcciones crecientes, todas múltiplos de cuatro.**
Esto suele ser un bloque de registros que se escribe en secuencia. La operación es un
reset seguido de configuración: se borran todos los registros, se establecen sus nuevos
valores y luego se dispara la operación escribiendo en un registro de "inicio" al final.

**Lecturas idénticas de la misma dirección hasta que un valor cambia.** Esto es
polling. El driver está esperando a que el dispositivo haga algo y comprueba
repetidamente un registro de estado. La dirección sometida a polling es casi con toda
seguridad un registro de estado. El valor que termina el polling te indica qué bit de
ese registro es el indicador de "listo".

**Bulk-OUT seguido de bulk-IN de la misma longitud.** Un patrón común para la
comunicación comando-respuesta en los endpoints bulk de USB es enviar un comando de
tamaño fijo por el endpoint OUT y luego leer una respuesta de tamaño fijo desde el
endpoint IN. Los dos endpoints funcionan juntos como un canal de solicitud-respuesta.

**Paquetes interrupt-IN periódicos con marcas de tiempo que aumentan de forma lineal.**
Este es un patrón de latido o estado periódico. El dispositivo informa de su estado a
una velocidad fija, independientemente de lo que haga el host. Los paquetes suelen
contener una pequeña estructura fija con bits de estado y contadores.

**Largas secuencias de escrituras sin respuesta observable alguna.** Esto suele ser
una descarga de firmware. El dispositivo tiene una región de código escribible y el
driver está cargando nuevas instrucciones en ella. Estas capturas suelen ser muy
grandes en comparación con otras operaciones, y a menudo comienzan con una cabecera
de tamaño fijo que identifica la imagen de firmware.

Estos patrones no son exhaustivos, pero aparecen con tanta frecuencia que reconocerlos
a primera vista ahorra una cantidad enorme de tiempo. La primera hora con un nuevo
archivo de captura se dedica casi siempre a identificar cuál de estos patrones
presenta la captura.

### 3.7 Cerrando la sección 3

Hemos construido un conjunto de capturas. Cada captura tiene nombre y fecha, está
etiquetada con la operación que representa y va acompañada de una breve nota que
describe las acciones del usuario y el comportamiento esperado del dispositivo. Hemos
utilizado `pciconf` y `usbconfig` para registrar la identidad estática del dispositivo.
Hemos utilizado `usbdump` para capturar su inicialización y sus operaciones funcionales.
Hemos aprendido que el diff entre dos capturas de operaciones similares es la mejor
herramienta que tenemos para extraer significado a nivel de bit. Y hemos aprendido
los patrones recurrentes que aparecen en el protocolo de casi todos los dispositivos.

La siguiente sección comienza la fase activa del trabajo. En lugar de limitarnos a
observar el driver existente, empezaremos a sondear el dispositivo nosotros mismos,
de forma controlada, para descubrir el significado de sus registros. Las capturas que
hicimos en esta sección son los datos con los que se medirán los experimentos de la
siguiente sección. Con las capturas en mano, sabemos cómo luce el "comportamiento
normal" y podemos comparar lo que ocurre cuando realizamos nuestras propias escrituras
con lo que ocurre cuando el driver del fabricante realiza las escrituras equivalentes.
Esa comparación es el núcleo de la construcción del mapa de registros.

## 4. Creación de un mapa de registros de hardware mediante experimentación

El mapa de registros es el documento que, una vez terminado, habría hecho innecesario
todo el trabajo anterior. Enumera todas las direcciones que el dispositivo expone,
indica qué hay en cada dirección, define el significado de cada bit y describe cualquier
efecto secundario que tengan las lecturas o escrituras. Con un mapa de registros completo
en mano, escribir el driver es un ejercicio de traducción sencillo. Sin él, el driver
no puede escribirse en absoluto. El mapa de registros es el artefacto que el resto de
este capítulo está diseñado, en muchos sentidos, para producir.

En ausencia de documentación, el mapa de registros debe construirse mediante
experimentación. Escribirás en direcciones, verás qué ocurre, leerás de vuelta las
direcciones, verás qué devuelven, cambiarás un bit a la vez, buscarás cambios en el
comportamiento y, poco a poco, acumularás un conjunto de hechos verificados sobre cada
dirección. El trabajo es paciente e incremental, y muchas de las técnicas son lo
bastante sencillas como para caber en un párrafo, pero la disciplina de aplicarlas
con seguridad y registrar los resultados cuidadosamente es lo que separa un proyecto
exitoso de una sesión que destruye el dispositivo.

Esta sección cubre las técnicas. La sección 10 volverá sobre el aspecto de seguridad
y explicará con detalle lo que no debes hacer. Léelas juntas; las técnicas de esta
sección solo son seguras en manos de alguien que haya interiorizado las advertencias
de la sección 10.

### 4.1 Mapeo del espacio de direcciones

Antes de poder sondear direcciones, necesitas saber qué direcciones existen. Para un
dispositivo PCI, los BARs (Base Address Registers) del dispositivo declaran las
regiones de memoria y los rangos de puertos I/O a los que el dispositivo responde. El
kernel ya los ha enumerado y los ha puesto a disposición mediante llamadas a
`bus_alloc_resource_any(9)` en la rutina `attach` de tu driver. La forma más sencilla
de verlos en funcionamiento es leerlos del kernel:

```sh
$ sudo devinfo -rv
```

Restringido a tu dispositivo, este comando lista los recursos que el kernel ha
asignado. Un dispositivo PCI típico podría producir una salida como esta:

```text
none2@pci0:0:18:0:
  pnpinfo vendor=0xabcd device=0x5678 ...
  Memory range:
    0xf7c00000-0xf7c0ffff (BAR 0, 64K, prefetch=no)
    0xf7800000-0xf7bfffff (BAR 2, 4M, prefetch=yes)
  Interrupt:
    irq 19
```

Dos regiones de memoria y una interrupción. La región de 64K es muy probablemente un
bloque de registros, porque los bloques de registros suelen ser pequeños. La región
de 4M es lo bastante grande como para ser un frame buffer, un anillo de descriptores
o un área de datos mapeada en memoria, pero es poco probable que sea espacio de
registros. Estas son suposiciones fundadas basadas en el tamaño, no hechos verificados
todavía. Anótalas como hipótesis.

El mismo tipo de intuición basada en el tamaño funciona en muchos casos. Una región
de registros rara vez supera un megabyte. Un buffer de datos o una cola rara vez son
menores de unos pocos kilobytes. Una región de exactamente 16 KB o 64 KB en límites
de potencia de dos resulta sospechosa de la manera correcta: parece espacio de
registros. Una región de varios megabytes y con prefetch es mucho más probable que
sea una región de datos.

Para un dispositivo USB, el equivalente al mapeo del espacio de direcciones es la
enumeración de endpoints. Cada endpoint es un "canal" del que puedes leer o en el que
puedes escribir. Las direcciones, tamaños y tipos de los endpoints fueron capturados
por `usbconfig dump_curr_config_desc` en la sección anterior. A partir de la lista de
endpoints ya sabes cuántos canales expone el dispositivo, de qué tipos y en qué
direcciones. No existe un equivalente al espacio de registros mapeado en memoria en
USB; todo se realiza a través de los endpoints, incluidas las lecturas y escrituras de
"registros" (que aparecen como transferencias de control, solicitudes específicas del
fabricante o datos dentro de transferencias bulk).

### 4.2 El principio de leer primero

La regla más importante para la exploración segura de registros es leer antes de
escribir. Una lectura de un registro desconocido es, casi siempre, inofensiva. El
hardware devuelve lo que considera que es el valor en esa dirección, con un efecto
secundario a lo sumo de borrar algunas banderas de estado concretas en algunos tipos
concretos de registros. Una escritura en un registro desconocido, en cambio, puede
hacer cualquier cosa: disparar un reset, iniciar una operación, cambiar un bit de
configuración, escribir en la memoria flash o, en el peor caso, poner el dispositivo
en un estado del que no pueda recuperarse fácilmente.

El principio es sencillo: no asumas nada sobre una escritura hasta que tengas
evidencia de que es segura. La evidencia puede provenir de las capturas que hiciste
en la sección 3 (una escritura que realiza el driver del fabricante es presumiblemente
una de las escrituras que el dispositivo espera), de registros análogos en dispositivos
similares, de un archivo de cabecera publicado o de un driver relacionado, o de tu
propio análisis del comportamiento del registro bajo lecturas.

Lee la región de registros completa de forma exhaustiva antes de realizar cualquier
escritura. Guarda los valores. Léela de nuevo diez minutos después. Compara. Los
registros cuyos valores cambian entre lecturas son interesantes: o bien son registros
de estado que reflejan algún estado activo, o bien son contadores que se incrementan
solos, o bien son registros conectados a entradas externas. Los registros cuyos valores
son estables son o bien registros de configuración (cuyos valores permanecen estables
hasta que algo los escribe) o bien registros de datos que por casualidad no han
cambiado durante la ventana de observación.

La herramienta más sencilla para este tipo de exploración es un pequeño módulo del
kernel que toma un rango de memoria y lo vuelca, y el código complementario en
`examples/part-07/ch36-reverse-engineering/lab03-register-map/` contiene exactamente
dicho módulo. El módulo se conecta como hijo del bus al que lo apuntas, asigna el
rango de memoria como un recurso y expone un sysctl que, al leerlo, vuelca cada
palabra del rango. Múltiples lecturas en diferentes momentos producen un cuadro de
qué palabras son estables y cuáles están cambiando.

### 4.3 El esqueleto del módulo de sondeo

Para mayor concreción, aquí está la estructura de un módulo de sondeo seguro. La
versión completa está en los archivos complementarios; lo que sigue es la estructura
esencial que deberías reconocer de los capítulos anteriores.

```c
struct regprobe_softc {
    device_t          sc_dev;
    struct mtx        sc_mtx;
    struct resource  *sc_mem;
    int               sc_rid;
    bus_size_t        sc_size;
};

static int
regprobe_probe(device_t dev)
{
    /* Match nothing automatically; only attach when explicitly
     * told to. The user adds the device by hand with devctl(8) so
     * that the wrong device cannot be probed by accident. */
    return (BUS_PROBE_NOWILDCARD);
}

static int
regprobe_attach(device_t dev)
{
    struct regprobe_softc *sc = device_get_softc(dev);

    sc->sc_dev = dev;
    sc->sc_rid = 0;
    mtx_init(&sc->sc_mtx, "regprobe", NULL, MTX_DEF);

    sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->sc_rid, RF_ACTIVE);
    if (sc->sc_mem == NULL) {
        device_printf(dev, "could not allocate memory resource\n");
        return (ENXIO);
    }
    sc->sc_size = rman_get_size(sc->sc_mem);
    device_printf(dev, "mapped %ju bytes\n",
        (uintmax_t)sc->sc_size);
    return (0);
}
```

El retorno de `BUS_PROBE_NOWILDCARD` desde `probe` es el gesto protector: este driver
no se conectará a ningún dispositivo a menos que el operador lo apunte explícitamente
a uno. Esto evita el caso peligroso en el que un módulo de sondeo se conecta
accidentalmente a un dispositivo que no era el que se pretendía investigar.

La función de lectura, llamada desde un manejador de sysctl, tiene este aspecto:

```c
static int
regprobe_dump_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct regprobe_softc *sc = arg1;
    char buf[16 * 1024];
    char *p = buf;
    bus_size_t off;
    uint32_t val;
    int error;

    if (sc->sc_size > sizeof(buf) - 64)
        return (E2BIG);

    mtx_lock(&sc->sc_mtx);
    for (off = 0; off < sc->sc_size; off += 4) {
        val = bus_read_4(sc->sc_mem, off);
        p += snprintf(p, sizeof(buf) - (p - buf),
            "%04jx: 0x%08x\n", (uintmax_t)off, val);
    }
    mtx_unlock(&sc->sc_mtx);

    error = sysctl_handle_string(oidp, buf, p - buf, req);
    return (error);
}
```

`bus_read_4` es un wrapper alrededor de `bus_space_read_4` que utiliza el recurso tanto como tag como handle, y es la forma más sencilla de leer cuatro bytes de una región mapeada en memoria. Observa que leemos en unidades de cuatro bytes, con offsets alineados a cuatro bytes. La mayoría de los dispositivos modernos esperan que los accesos a registros estén alineados y sean del tamaño natural. Los accesos no alineados, o los de tamaño incorrecto, pueden producir valores basura o, en determinado hardware, causar errores de bus que se manifiestan como kernel panics. Cuando el dispositivo pueda usar registros de 16 o de 8 bits, utiliza `bus_read_2` o `bus_read_1` respectivamente; cuando tengas dudas, empieza por lecturas de 4 bytes, ya que es la elección más habitual para los periféricos modernos.

Este módulo no realiza ninguna escritura. Es una herramienta de observación pura. Cargarlo en un dispositivo desconocido te proporciona una instantánea de la región de memoria del dispositivo sin modificar nada. Varias cargas, o varias invocaciones del sysctl de volcado, te ofrecen una secuencia de instantáneas cuyas diferencias revelan qué direcciones son dinámicas.

### 4.4 Deducir el propósito de los registros a partir del comportamiento

Después de leer el espacio de direcciones repetidamente, empiezas a detectar patrones. Cada patrón apunta a un propósito concreto del registro.

**Los valores estables** suelen ser registros de configuración, registros identificadores o valores por defecto que aún no se han modificado. Un registro en el desplazamiento 0 que siempre devuelve el mismo valor constante es con frecuencia un registro identificador del chip, a veces denominado «registro de versión» o «registro de número mágico». Muchos dispositivos incluyen este tipo de registro precisamente para que los drivers puedan confirmar que el chip es el esperado.

**Los valores que cambian lentamente** suelen ser contadores: bytes recibidos, paquetes enviados, errores detectados, tiempo transcurrido. El registro que se incrementa poco a poco es la señal característica de un contador, y la diferencia entre dos lecturas indica la tasa de incremento. Si un contador aumenta exactamente en uno cada segundo, probablemente has encontrado un registro de latido o de marca de tiempo. Si se incrementa varios miles de veces por segundo cuando hay tráfico y se queda estático cuando no lo hay, has encontrado un contador de paquetes o de bytes para el camino de datos activo.

**Los valores que cambian rápidamente y se desbordan cíclicamente** son habitualmente punteros de escritura del lado productor en un ring buffer. Un dispositivo que produce datos en un ring buffer expone habitualmente un registro que contiene la posición de escritura actual. El puntero de escritura se incrementa rápidamente mientras fluyen datos y da la vuelta al alcanzar el tamaño del buffer. El puntero de lectura correspondiente, escrito por el driver, le indica al dispositivo dónde se encuentra el consumidor.

**Los bits que alternan al ritmo de las operaciones** son bits de estado. Un bit que se pone a 1 cuando comienza una operación y a 0 cuando esta termina es un bit «busy» (ocupado). Un bit que se pone a 1 cuando ocurre un evento y permanece a 1 hasta que se borra es un bit de «pending interrupt» (interrupción pendiente). Un bit que pasa a 0 cuando el dispositivo funciona correctamente y a 1 cuando se produce un error es un indicador de error.

**Los registros que devuelven un valor distinto al que se escribió** pueden ser enmascarados (algunos bits no están implementados y devuelven siempre cero, independientemente de lo que se escriba), de un solo disparo (las escrituras tienen efectos secundarios pero el registro en sí no almacena el valor escrito) o de borrado automático (las escrituras activan bits que el dispositivo borra automáticamente cuando la operación finaliza). Los tres tipos son habituales, y el comportamiento en la lectura posterior es lo que permite distinguirlos.

Estas observaciones construyen una imagen del mapa de registros. Cada observación verificada queda anotada en el cuaderno, con la dirección, el patrón observado y el propósito propuesto. Con el tiempo, las entradas del cuaderno se consolidan en la hoja de datos provisional que se tratará en la Sección 11.

### 4.5 Comparar escrituras capturadas con hipótesis

Las capturas de la Sección 3 son el puente entre la observación y la hipótesis. Una vez que has propuesto que un registro determinado es, por ejemplo, el registro de selección de canal, puedes confirmar o refutar la hipótesis comparando lo que has visto escribir al driver del fabricante en ese registro con lo que sabes sobre las operaciones que realizó el usuario.

Supón que tu hipótesis es que el registro en el desplazamiento `0x40` es el registro de selección de canal, y que el valor de ese registro codifica el número de canal directamente como un entero pequeño. Tienes una captura en la que el usuario seleccionó el canal 6 y otra en la que seleccionó el canal 11. Si tu hipótesis es correcta, la secuencia del canal 6 debería contener una escritura de `0x06` en el equivalente de `0x40`, y la secuencia del canal 11 debería contener una escritura de `0x0b` en el mismo lugar. Si encuentras esas escrituras, la hipótesis queda respaldada. Si las capturas muestran escrituras de valores diferentes, o ninguna escritura en ese desplazamiento, la hipótesis es incorrecta.

Este es el paso de falsación que convierte el trabajo en algo científico. Una hipótesis que supera un intento de falsación vale más que diez hipótesis que solo han sido propuestas de forma informal.

Para un dispositivo USB, el mismo tipo de comparación se aplica entre las transferencias de control capturadas, los paquetes bulk o los paquetes de interrupción y el significado propuesto. Si crees que una transferencia de control específica del fabricante con `bRequest = 0xa0` y `wValue` en el byte bajo es el comando «set channel», puedes verificarlo examinando las capturas de las selecciones del canal 1, el canal 6 y el canal 11, y confirmando que exactamente esa transferencia aparece en cada una de ellas, con el `wValue` esperado.

### 4.6 La disciplina de una hipótesis a la vez

Una disciplina que es fácil olvidar bajo presión es la de probar una hipótesis a la vez. Cuando hay varias hipótesis en el aire, la tentación es diseñar un experimento que las pruebe todas a la vez. El experimento se ejecuta, el dispositivo produce alguna salida y esa salida es coherente con una de las hipótesis pero no con otra. Ahora has aprendido algo sobre la hipótesis A. Pero como también has cambiado las condiciones para la hipótesis B, has perdido la capacidad de interpretar lo que ocurrió desde la perspectiva de B. El mismo experimento tendrá que ejecutarse de nuevo, variando B de forma independiente.

La disciplina correcta es: varía una sola cosa, observa el resultado, extrae la conclusión que corresponde a esa variable y pasa al siguiente experimento. Es más lento por experimento, pero más rápido en conjunto, porque cada experimento deja tras de sí un hecho claro y aislado en lugar de una maraña de información parcial.

Los cuadernos de laboratorio de Newton están llenos de experimentos de una sola variable. Lo mismo ocurre con los cuadernos de todo proyecto de ingeniería inversa exitoso. Los experimentos multivariables pertenecen a una fase posterior, cuando entiendes cada variable lo suficientemente bien como para predecir sus interacciones; en la fase de descubrimiento, suelen generar confusión.

### 4.7 Patrones habituales de organización de registros

En muchos dispositivos, algunos patrones de organización de registros se repiten con la suficiente frecuencia como para que merezca la pena conocerlos de antemano. Reconocerlos puede ahorrar semanas de trabajo.

**Status / Control / Data**: un pequeño grupo de registros en el que uno contiene el estado actual de una operación (Status), otro acepta comandos o cambios de configuración (Control) y uno o varios registros contienen los datos que se mueven hacia o desde el dispositivo (Data). Muchos periféricos sencillos utilizan exactamente esta forma, y muchos más la emplean como estructura de una de las varias unidades funcionales dentro del dispositivo.

**Interrupt Enable / Interrupt Status / Interrupt Acknowledge**: tres registros que juntos implementan el mecanismo de interrupción. El registro Enable controla qué condiciones provocan una interrupción. El registro Status informa de qué condiciones están generando una interrupción en ese momento. El registro Acknowledge borra las condiciones de interrupción una vez que han sido atendidas. El patrón es tan habitual que, al explorar un dispositivo desconocido, deberías buscarlo explícitamente: tres registros adyacentes cerca del inicio del mapa de registros, con nombres identificativos como `INT_ENABLE`, `INT_STATUS`, `INT_CLEAR` si puedes encontrar alguna documentación.

**Producer Pointer / Consumer Pointer**: dos registros que juntos implementan un ring buffer. El Producer Pointer lo actualiza quien produce los datos (a veces el dispositivo, a veces el driver) e indica el siguiente espacio libre. El Consumer Pointer lo actualiza quien consume los datos e indica el siguiente elemento a procesar. La diferencia entre ambos indica cuántos elementos están actualmente ocupados. Este patrón es la base de casi todas las interfaces de I/O de alta velocidad en el hardware moderno, incluidos la mayoría de los controladores de red, los controladores USB y los controladores de disco.

**Window / Index / Data**: un patrón de acceso indirecto en el que el dispositivo expone un número reducido de registros pero los usa para acceder a un espacio de direcciones interno mucho mayor. Un registro Window selecciona qué «página» es visible en ese momento, un registro Index selecciona qué registro dentro de la página y un registro Data lee o escribe el registro seleccionado. El patrón es habitual en dispositivos más antiguos y en dispositivos con espacios de registros internos muy grandes.

**Capability Pointer**: un registro o pequeño grupo de registros que apunta al inicio de una cadena de descriptores de capacidades. PCI Express define una forma estándar de este patrón; muchos otros dispositivos utilizan variantes ad hoc. La lista de capacidades permite al driver descubrir qué características opcionales admite el chip sin necesidad de consultar documentación externa.

Cuando estás explorando un dispositivo desconocido, buscar en el espacio de registros alguno de estos patrones recurrentes es una de las primeras cosas que debes hacer. Un dispositivo que utiliza una organización Status / Control / Data es mucho más fácil de gestionar que uno que no la utiliza, y reconocer la organización a tiempo ahorra una cantidad considerable de trabajo.

### 4.8 Lo que no hay que hacer todavía

Antes de continuar, un breve recordatorio de contención. La tentación, cuando has construido un mapa de registros que funciona en su mayor parte, es intentar escribir en ellos y ver qué ocurre. Resiste esta tentación durante la exploración de registros. Escribe solo cuando tengas una hipótesis específica que probar, cuando la hipótesis prediga un resultado concreto, cuando tengas un modo de verificar que ese resultado se ha producido y cuando hayas considerado cuál sería el peor resultado plausible de una interpretación no intencionada. La disciplina que se detallará en la Sección 10 se construye sobre esta contención, y la fase de mapeo de registros es exactamente donde debe aplicarse primero.

### 4.9 Cerrando la Sección 4

Hemos aprendido a mapear el espacio de direcciones de un dispositivo, a leerlo de forma segura, a interpretar los patrones de valores estables, de valores que cambian lentamente y de valores que cambian rápidamente, y a comparar escrituras capturadas con hipótesis para confirmar o refutar los significados de registros propuestos. Hemos aprendido las organizaciones recurrentes que el hardware suele utilizar. Y hemos preparado el terreno para el tratamiento de la seguridad en la Sección 10, que es la condición previa para que todo este trabajo se realice de forma responsable.

La siguiente sección sube un nivel de abstracción. Una vez identificados los registros individuales, observamos cómo se agrupan en estructuras de mayor tamaño: buffers de datos, colas de comandos, anillos de descriptores. Estas estructuras de nivel superior son el modo en que los dispositivos mueven datos de verdad, e identificarlas es el siguiente paso para convertir el conocimiento de los registros en la base de un driver funcional.

## 5. Identificación de buffers de datos e interfaces de comandos

Los registros son el modo de comunicarte con la superficie de control de un dispositivo. Los buffers de datos son el modo en que el dispositivo mueve información real. El mapa de registros que construiste en la sección anterior es necesario pero no suficiente. Para completar el cuadro también necesitas entender cómo gestiona el dispositivo los datos: dónde viven sus buffers, cómo se definen sus límites, qué forma toman los datos en su interior y cómo se coordinan el productor y el consumidor.

Esta sección recorre las estructuras de buffer y de comandos más habituales que utiliza el hardware, con notas específicas de FreeBSD sobre cómo identificarlas mediante observación y cómo configurar las correspondientes asignaciones de `bus_dma(9)` en el driver.

### 5.1 Los tres grandes: lineal, anillo y descriptor

Casi todos los buffers de datos del hardware moderno pertenecen a una de estas tres formas.

Un **buffer lineal** es un bloque contiguo de memoria que el driver entrega al dispositivo para una operación. El driver lo rellena con datos, el dispositivo los consume y el driver lo recupera cuando lo necesita. Los buffers lineales son la forma más sencilla y aparecen en patrones de solicitud-respuesta donde cada operación tiene su propio buffer dedicado. Son fáciles de identificar porque aparecen en las capturas como un único bloque de bytes cuyo tamaño coincide con el de la operación esperada.

Un **ring buffer** (buffer circular) es un buffer circular con un puntero productor y un puntero consumidor. Las escrituras del productor van a la ranura indicada por el puntero productor, y después este avanza. Las lecturas del consumidor provienen de la ranura indicada por el puntero consumidor, y después este avanza. Ambos punteros vuelven al inicio cuando llegan al final del buffer. El buffer está lleno cuando el productor alcanza al consumidor; está vacío cuando los dos punteros son iguales en el sentido contrario. Los ring buffers están presentes en todas partes en redes de alta velocidad y almacenamiento, porque permiten que el productor y el consumidor funcionen a velocidades distintas sin necesidad de sincronizaciones coordinadas.

Un **descriptor ring** (anillo de descriptores) es un tipo especial de ring buffer en el que cada ranura no contiene el dato en sí, sino un pequeño descriptor de tamaño fijo que apunta al dato. El descriptor contiene normalmente una dirección de memoria (donde reside el dato), una longitud, un campo de estado (que el dispositivo rellena tras el procesamiento) y algunos bits de control. Los descriptor rings permiten al dispositivo realizar DMA de tipo scatter-gather, aceptar buffers de tamaño variable y devolver al driver el estado de cada buffer. Son algo más complejos que los ring buffers, pero mucho más flexibles, y constituyen el patrón dominante en los controladores de red y almacenamiento de alto rendimiento.

Identificar qué forma utiliza un determinado dispositivo es la primera tarea. Las señales suelen ser bastante claras. Si las capturas muestran bloques de tamaño fijo transferidos de uno en uno sin metadatos, probablemente se trate de una disposición de buffer lineal. Si las capturas muestran datos fluyendo de forma continua sin un límite evidente entre operaciones, y hay un registro que parece un puntero productor, probablemente sea un ring buffer. Si la documentación del dispositivo, fuentes de drivers anteriores o hojas de datos de dispositivos relacionados mencionan «descriptores», casi con toda seguridad se trata de un descriptor ring.

### 5.2 Cómo distinguir unos buffers de otros

Una vez que has identificado que existe un buffer, la siguiente pregunta es: ¿qué tamaño tiene y cómo está organizado internamente? Las capturas de la sección 3 contienen los datos; la pregunta es qué forma tiene esa información.

Varias observaciones pueden ayudar.

**Tamaño de la región de memoria subyacente.** Si un PCI BAR ocupa exactamente 4 KB y un sysctl revela que el dispositivo tiene un registro de «tamaño de anillo» con valor 64, la conclusión natural es que el anillo tiene 64 entradas de 64 bytes cada una. Muchos dispositivos utilizan potencias de dos tanto para el tamaño del anillo como para el tamaño de cada entrada, y el producto de ambos debe coincidir con el tamaño del BAR.

**Estructura periódica en los datos.** Una captura vista con `xxd` o Wireshark a veces muestra una estructura periódica evidente: cada 32 bytes aparece un pequeño patrón fijo, cada 64 bytes hay un contador, cada 16 bytes hay un byte de estado que varía de forma predecible. Una estructura periódica de tamaño N sugiere con fuerza que el buffer está organizado como una secuencia de registros de tamaño N.

**Alineación de los valores de puntero.** Si ves que el dispositivo o el driver utiliza «direcciones» cuyos bits bajos son siempre cero, esas direcciones están alineadas a una frontera de potencia de dos. Si la alineación es de 16 bytes, las entradas probablemente tienen 16 bytes o más. Si la alineación es de un megabyte, las entradas son muy grandes o el dispositivo impone un requisito de alineación gruesa por algún otro motivo.

**Cabeceras y tráileres.** Muchos formatos de registro incluyen una cabecera que identifica el tipo de registro, una carga útil y un tráiler que contiene un checksum o una longitud. La cabecera suele ser un valor mágico que se puede reconocer a simple vista.

**Pistas multiplataforma.** Si el dispositivo tiene un driver de Linux en el árbol de código abierto, el código de ese driver puede ya definir la estructura del registro. Leer otro driver para conocer los formatos de registro es una de las técnicas de investigación más eficaces disponibles, incluso cuando tienes intención de escribir el driver de FreeBSD desde cero con un enfoque de sala limpia. Conviene mencionar dos advertencias en este mismo punto para que el ritmo del capítulo no las deje pasar desapercibidas. La primera es de carácter legal: los drivers de Linux tienen licencia GPL casi siempre, por lo que leerlos está bien, pero copiar o transcribir casi literalmente su código en un driver de FreeBSD con licencia BSD no lo está. La segunda es técnica: la información sobre la disposición de los registros se traslada con limpieza entre kernels porque describe el dispositivo, no el host, pero el código que la rodea no. Las rutas de asignación de buffers de Linux, sus primitivas de locking y sus funciones auxiliares para anillos de descriptores están moldeadas por su propio kernel, y un driver de FreeBSD que intente replicarlas línea por línea se enfrentará al kernel anfitrión a cada paso. Lee para entender el formato; luego escribe código FreeBSD a partir de tus propias notas. La sección 12 trata el marco legal en detalle; la sección 13 muestra cómo los drivers reales han hecho esto correctamente.

### 5.3 Colas de comandos y su secuenciación

En dispositivos con interfaces de comandos, la forma habitual es una cola de estructuras de comando, cada una de las cuales el dispositivo procesa en orden, con el estado devuelto a través de un registro separado o de un campo de estado en la propia estructura de comando. Las colas de comandos suelen ser una forma especial de anillo de descriptores.

La secuenciación importa. Algunos dispositivos procesan los comandos estrictamente en orden; otros los procesan fuera de orden e identifican la finalización mediante una etiqueta en la estructura de comando. Algunos dispositivos procesan los comandos de uno en uno; otros los procesan muchos en paralelo. Algunos dispositivos requieren que el driver espere a que un comando se complete antes de publicar el siguiente; otros aceptan muchos comandos concurrentes. Identificar en qué modo se encuentra el dispositivo forma parte de la investigación, y las capturas de la sección 3 suelen ser lo bastante detalladas como para hacer visible la respuesta.

Una técnica útil consiste en ejercitar el dispositivo deliberadamente con operaciones que se solapen en el tiempo y observar cómo responde el driver. Si el driver siempre espera a que un comando se complete antes de publicar el siguiente, el dispositivo probablemente utiliza procesamiento secuencial. Si el driver publica varios comandos en rápida sucesión y luego espera varias finalizaciones, el dispositivo probablemente utiliza procesamiento concurrente. Si las finalizaciones llegan en un orden distinto al de las publicaciones, el dispositivo está realizando procesamiento fuera de orden.

### 5.4 DMA y bus mastering

La mayoría de los dispositivos modernos que mueven cantidades significativas de datos utilizan DMA. El dispositivo, actuando como bus master, lee y escribe directamente en la memoria del host, sin necesitar que la CPU copie cada byte. Desde el lado del driver, DMA introduce varias restricciones que debes gestionar correctamente.

Los buffers deben ser físicamente contiguos, o al menos dispersos de una manera que el dispositivo pueda expresar. Deben estar alineados en una frontera que el dispositivo requiera. Deben ser visibles para el dispositivo, lo que en FreeBSD significa que deben configurarse a través del framework `bus_dma(9)`. Y el driver y el dispositivo deben sincronizar sus vistas del buffer mediante llamadas a `bus_dmamap_sync(9)`, porque los procesadores modernos mantienen cachés que pueden no ser coherentes con el DMA del dispositivo.

La mecánica completa de `bus_dma(9)` se trata en el capítulo 21 y no es necesario repetirla aquí. Para la ingeniería inversa, las implicaciones son:

1. El dispositivo casi con certeza impone restricciones de alineación y tamaño en sus buffers. Las capturas que muestran buffers que siempre comienzan en direcciones con los mismos bits bajos están revelando la alineación.
2. El dispositivo puede imponer un tamaño máximo de buffer. Las capturas que muestran operaciones grandes divididas en varias transferencias más pequeñas están revelando ese máximo.
3. El dispositivo puede utilizar bits de dirección DMA específicos para codificar metadatos. Un puntero de 32 bits en un campo de dirección de 64 bits, por ejemplo, a veces deja los 32 bits superiores disponibles para otros usos, y el dispositivo puede interpretar esos bits como indicadores o etiquetas.

Identificar estas restricciones forma parte del trabajo de ingeniería inversa de cualquier dispositivo con capacidad DMA. Las capturas y el código fuente del driver existente, si está disponible, son las fuentes primarias de evidencia.

### 5.5 Ingeniería inversa de los formatos de paquetes

Cuando la ruta de datos de un dispositivo utiliza paquetes estructurados, aplicar ingeniería inversa al formato del paquete es una de las partes más gratificantes del trabajo. Las técnicas recurrentes son:

**Busca bytes fijos que nunca cambien.** Suelen ser números mágicos, bytes de versión o códigos de categoría. Márcalos y explora qué significan.

**Busca campos cuyos valores se correlacionen con las operaciones.** Un byte que toma un conjunto pequeño de valores distintos, con cada valor apareciendo en exactamente un tipo de operación, es casi con certeza un opcode o un tipo de comando.

**Busca campos de longitud.** Un campo de dos o cuatro bytes cuyo valor coincide con el tamaño del resto del paquete, en alguna codificación predecible, es un campo de longitud. Los campos de longitud suelen estar cerca del inicio del paquete y a menudo son el campo que te permite analizar correctamente los paquetes de longitud variable.

**Busca números de secuencia o identificadores de sesión.** Los campos que se incrementan monótonamente a lo largo de una sesión, o que cambian una vez por operación, suelen ser números de secuencia. Su valor raramente es interesante, pero su presencia y posición te ayudan a ignorarlos mientras buscas el contenido real.

**Busca checksums.** Un campo cuyo valor depende del resto del paquete, de una manera que no puedes predecir a partir de ningún byte individual, es probablemente un checksum. Los checksums estándar (CRC-16, CRC-32, sumas simples) a veces pueden identificarse calculándolos sobre rangos candidatos y comprobando cuál coincide. Si existe un checksum, necesitarás calcularlo correctamente cuando el driver construya sus propios paquetes, por lo que identificar el algoritmo es necesario.

Cada una de estas observaciones construye la parte del formato de paquete del pseudodatasheet. Combinadas con el mapa de registros de la sección 4, forman la documentación que la sección 11 reunirá en un sustituto adecuado del datasheet.

### 5.6 Identificar qué operaciones son de lectura y cuáles de escritura

Muchos dispositivos tienen una ruta de datos asimétrica: algunas operaciones hacen que el dispositivo lea de la memoria del host, otras hacen que escriba en ella. Identificar cuál es cuál es fundamental para establecer correctamente la dirección del DMA.

La evidencia más clara está en las capturas. Una captura USB, por ejemplo, distingue explícitamente los endpoints IN (del dispositivo al host) de los endpoints OUT (del host al dispositivo). Las capturas PCI, cuando puedes obtenerlas, distinguen las lecturas de memoria de las escrituras de memoria por el tipo de transacción. El DMA masivo desde un controlador de red hacia el host se registra como el dispositivo escribiendo en la memoria del host. El DMA masivo desde el host hacia el controlador de red se registra como el dispositivo leyendo de la memoria del host.

Si las capturas no están disponibles, una técnica útil consiste en establecer el buffer con un patrón reconocible (todos los bytes a `0xCC`, por ejemplo) antes de activar la operación e inspeccionar el buffer después. Si el patrón del buffer se sobreescribe con nuevos datos, la operación fue una transferencia del dispositivo al host (el dispositivo escribió en el buffer). Si el patrón del buffer no cambió, la operación fue una transferencia del host al dispositivo (el dispositivo solo leyó del buffer). Este tipo de experimento discriminatorio es uno de los ejemplos más claros de un experimento cuyo resultado te revelará un dato que no puedes aprender fácilmente de ninguna otra manera.

### 5.7 Cerrando la sección 5

Hemos aprendido a reconocer las tres grandes formas de organización de buffers: buffers lineales, buffers circulares y anillos de descriptores. Hemos visto cómo deducir los tamaños de buffer y de entrada a partir de estructuras periódicas, alineaciones y las regiones de memoria subyacentes. Hemos aprendido a identificar las formas de las colas de comandos y los modelos de secuenciación. Hemos señalado las restricciones que impone el DMA y los tipos de evidencia que las revelan. Hemos repasado las técnicas recurrentes para aplicar ingeniería inversa a los formatos de paquetes. Y hemos visto cómo identificar la dirección de una transferencia cuando las propias capturas no lo indican directamente.

La siguiente sección transforma esta comprensión creciente en código. Escribiremos la primera versión de un driver real, uno que implementa únicamente la parte funcional más pequeña que resulte útil: normalmente un reset, a veces reset más una única lectura de estado, a veces el mínimo absoluto necesario para poner el dispositivo en un estado desde el que pueda controlarse más. El principio es que un driver pequeño que hace algo verificablemente correcto es un mejor punto de partida que un driver grande que hace muchas cosas de forma incierta.

## 6. Reimplementar la funcionalidad mínima del dispositivo

Tras la observación llega la reconstrucción. Con las capturas en mano, las hipótesis sobre los registros confirmadas y al menos la forma aproximada de la estructura del buffer entendida, puedes empezar a escribir un driver. La palabra clave es «empezar». La primera versión de un driver obtenido por ingeniería inversa no tiene que hacer todo lo que el dispositivo puede hacer. Ni siquiera tiene que hacer la mayor parte de lo que el dispositivo puede hacer. Tiene que hacer la parte más pequeña que sea útil, y hacerlo correctamente.

Esta sección recorre los principios de empezar en pequeño y los pasos prácticos para escribir el primer driver mínimo. El trabajo aquí se apoya en todos los capítulos anteriores del libro, porque la mecánica de «escribir un driver» es exactamente la misma mecánica que hemos venido practicando. Lo que cambia es la disciplina en torno a qué características implementar primero y cómo ganar confianza en que cada característica es correcta.

### 6.1 Empieza siempre con el reset

La primera característica que implementes debe ser casi siempre el reset. Una operación de reset es la base de todas las demás operaciones, por varias razones.

El reset suele ser la operación más sencilla que tiene un dispositivo. A menudo basta con una sola escritura en un único registro, o una secuencia de tres o cuatro escrituras que ponen el dispositivo en un estado conocido. La hipótesis es pequeña y fácil de probar.

Reset es la operación cuyo resultado se verifica con mayor facilidad. Antes del reset, los registros del dispositivo pueden contener valores arbitrarios. Tras un reset exitoso, contienen valores predeterminados predecibles: configuración a cero, sin interrupciones pendientes, sin operaciones activas, y los registros de identificación mostrando sus valores mágicos constantes. Si realizas un reset y luego lees los registros, deberías ver el estado de «recién encendido».

Reset es la operación cuyos modos de fallo son los más benignos. Si tu intento de reset no consigue realmente restablecer el dispositivo, este quedará en un estado desconocido, y podrás recuperarte descargando tu driver y dejando que el kernel vuelva a realizar el probe del dispositivo. Un reset fallido raramente causa daños; simplemente te deja en la misma situación que antes.

Reset es también la condición previa para casi todas las demás operaciones. Un driver que no puede poner el dispositivo en un estado conocido no puede fiarse de ninguna operación posterior. Al implementar el reset primero, construyes los cimientos en los que se apoyará cada prueba posterior de cada funcionalidad futura.

### 6.2 El esqueleto de un driver mínimo

Para un dispositivo PCI, el esqueleto del driver tiene un aspecto muy similar al de cualquier otro driver PCI que has visto en el libro. Esta es la forma esencial:

```c
struct mydev_softc {
    device_t          sc_dev;
    struct mtx        sc_mtx;
    struct resource  *sc_mem;
    int               sc_rid;
    struct resource  *sc_irq;
    int               sc_irid;
    void             *sc_ih;
    bus_size_t        sc_size;
    /* Driver-specific state goes here as it accumulates. */
};

static int
mydev_probe(device_t dev)
{
    if (pci_get_vendor(dev) == 0xabcd &&
        pci_get_device(dev) == 0x5678) {
        device_set_desc(dev, "ExampleCorp XYZ Reverse-Engineered");
        return (BUS_PROBE_DEFAULT);
    }
    return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
    struct mydev_softc *sc = device_get_softc(dev);
    int error;

    sc->sc_dev = dev;
    mtx_init(&sc->sc_mtx, "mydev", NULL, MTX_DEF);

    sc->sc_rid = PCIR_BAR(0);
    sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->sc_rid, RF_ACTIVE);
    if (sc->sc_mem == NULL)
        goto fail;
    sc->sc_size = rman_get_size(sc->sc_mem);

    sc->sc_irid = 0;
    sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->sc_irid, RF_SHAREABLE | RF_ACTIVE);
    if (sc->sc_irq == NULL)
        goto fail;

    error = mydev_reset(sc);
    if (error != 0)
        goto fail;

    error = mydev_verify_id(sc);
    if (error != 0)
        goto fail;

    device_printf(dev, "attached, mapped %ju bytes\n",
        (uintmax_t)sc->sc_size);
    return (0);

fail:
    mydev_detach(dev);
    return (ENXIO);
}
```

La estructura debería resultarte familiar. El driver asigna un recurso de memoria para su región de registros y un recurso IRQ para su interrupción. A continuación, realiza un reset y una comprobación del identificador. Si alguno de los dos falla, el driver sale de forma limpia. El reset y la comprobación del identificador son las dos primeras funciones que merece la pena escribir.

### 6.3 Implementación del reset

La función de reset es, en el caso más sencillo, tres líneas de código:

```c
static int
mydev_reset(struct mydev_softc *sc)
{
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, MYDEV_CTRL_RESET);
    pause("mydev_reset", hz / 100);
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, 0);
    return (0);
}
```

Escribimos el bit de reset en el registro de control, esperamos un breve instante para que el dispositivo actúe sobre él y, a continuación, limpiamos el registro de control para dejar el dispositivo en un estado quiescente. Las constantes `MYDEV_REG_CONTROL` y `MYDEV_CTRL_RESET` provienen del mapa de registros que construiste en la sección 4.

En la práctica, el reset rara vez es tan sencillo. Muchos dispositivos tienen un protocolo de reset específico que implica varias escrituras en registros en un orden determinado, o que implica hacer polling de un registro de estado hasta que el reset se completa, o que implica esperar un tiempo concreto antes de continuar. Las capturas de la sección 3 te muestran lo que el driver del fabricante hace en el momento del attach. Replicar esa secuencia es la implementación de reset más segura.

Un reset más realista podría tener el siguiente aspecto:

```c
static int
mydev_reset(struct mydev_softc *sc)
{
    int i;
    uint32_t status;

    /* Disable any pending interrupts. */
    bus_write_4(sc->sc_mem, MYDEV_REG_INT_ENABLE, 0);

    /* Trigger reset. */
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, MYDEV_CTRL_RESET);

    /* Wait for reset complete, with timeout. */
    for (i = 0; i < 100; i++) {
        status = bus_read_4(sc->sc_mem, MYDEV_REG_STATUS);
        if ((status & MYDEV_STATUS_RESETTING) == 0)
            break;
        pause("mydev_reset", hz / 100);
    }
    if (i == 100) {
        device_printf(sc->sc_dev, "reset timed out\n");
        return (ETIMEDOUT);
    }

    /* Clear any pending interrupt status. */
    bus_write_4(sc->sc_mem, MYDEV_REG_INT_STATUS, 0xffffffff);

    return (0);
}
```

Esta versión gestiona el caso en el que el reset tarda un tiempo en completarse y en el que el dispositivo expone un bit de estado que indica «reset en curso». Añadir un timeout, con una ruta de error explícita cuando expire, es un hábito que merece adoptar desde el principio. Un driver que no puede distinguir entre «el dispositivo va lento hoy» y «el dispositivo ha fallado» acabará bloqueando el kernel, y el coste de añadir el timeout es trivial.

### 6.4 Verificación del identificador

Todo driver obtenido por ingeniería inversa debe verificar que el dispositivo al que se ha hecho attach es el dispositivo que el driver espera. La verificación consiste normalmente en una o dos lecturas de registros, comparando los valores con constantes conocidas:

```c
static int
mydev_verify_id(struct mydev_softc *sc)
{
    uint32_t id, version;

    id = bus_read_4(sc->sc_mem, MYDEV_REG_ID);
    if (id != MYDEV_ID_MAGIC) {
        device_printf(sc->sc_dev,
            "unexpected ID 0x%08x (expected 0x%08x)\n",
            id, MYDEV_ID_MAGIC);
        return (ENODEV);
    }

    version = bus_read_4(sc->sc_mem, MYDEV_REG_VERSION);
    if ((version >> 16) != MYDEV_VERSION_MAJOR) {
        device_printf(sc->sc_dev,
            "unsupported version 0x%08x\n", version);
        return (ENODEV);
    }

    device_printf(sc->sc_dev, "ID 0x%08x version 0x%08x\n",
        id, version);
    return (0);
}
```

La comprobación tiene dos propósitos. En primer lugar, confirma que la comprensión que el driver tiene del mapa de registros es correcta: si el registro identificador contiene el valor esperado en el offset esperado, el mapa es al menos coherente con lo que creemos. En segundo lugar, te proporciona una ruta de fallo rápido si el driver se asocia por algún motivo al dispositivo equivocado, quizás porque dos dispositivos comparten un prefijo de ID PCI o porque el dispositivo tiene varias revisiones de silicio.

### 6.5 Añadir logging

El logging es tus ojos durante la ingeniería inversa. Cada escritura en un registro, cada lectura de un registro cuyo valor importe, cada hito en la secuencia de arranque del driver: regístralo. El `device_printf` del kernel va a parar a `dmesg`, donde puedes verlo en tiempo real mientras el driver se carga.

El logging no es una ayuda para la depuración que se elimina después. En un proyecto de ingeniería inversa, el logging forma parte del driver y permanece en él hasta que el driver se comprende bien. El coste es pequeño: unas pocas llamadas a `device_printf` consumen un tiempo de CPU despreciable y generan unas pocas líneas de salida. El beneficio es grande: cuando algo va mal, tienes un registro completo de lo que el driver intentó hacer.

Un patrón habitual es envolver el acceso a los registros en un pequeño helper inline que registre el acceso:

```c
static inline uint32_t
mydev_read(struct mydev_softc *sc, bus_size_t off)
{
    uint32_t val = bus_read_4(sc->sc_mem, off);
    if (mydev_log_reads)
        device_printf(sc->sc_dev,
            "read  off=0x%04jx val=0x%08x\n",
            (uintmax_t)off, val);
    return (val);
}

static inline void
mydev_write(struct mydev_softc *sc, bus_size_t off, uint32_t val)
{
    if (mydev_log_writes)
        device_printf(sc->sc_dev,
            "write off=0x%04jx val=0x%08x\n",
            (uintmax_t)off, val);
    bus_write_4(sc->sc_mem, off, val);
}
```

Los flags `mydev_log_reads` y `mydev_log_writes` pueden ser sysctls configurables, de modo que puedas activar o desactivar el logging sin recompilar el driver. El código de ejemplo en `examples/part-07/ch36-reverse-engineering/lab03-register-map/` usa exactamente este patrón.

### 6.6 Comparación de la salida del log con las capturas

Con el logging en su lugar, puedes comparar lo que hace tu driver con lo que hacía el driver del fabricante. Ejecuta el driver del fabricante en el entorno de observación y captura el resultado. Ejecuta tu driver en tu entorno de desarrollo y captura su salida de log. Compara.

Si las dos secuencias coinciden, tu driver está haciendo lo mismo que el driver del fabricante, al menos para las operaciones que has implementado. Si difieren, las diferencias te están diciendo algo. Quizás tu driver omite una escritura que el driver del fabricante realiza. Quizás tu driver escribe un valor diferente del que escribía el driver del fabricante. Quizás tu driver lee un registro que el driver del fabricante no lee. Cada diferencia es una hipótesis a investigar.

Esta técnica de comparación es una de las más eficaces de las que dispones. Convierte las capturas de evidencia pasiva en una especificación activa: el driver debe producir la misma secuencia de operaciones que muestran las capturas. Allí donde el driver se desvía, la desviación es un bug o una pieza que falta, y las capturas te indican qué añadir.

### 6.7 Escritura del boilerplate del módulo

El driver mínimo también necesita el boilerplate estándar del módulo. A estas alturas del libro, el boilerplate debería resultarte familiar:

```c
static device_method_t mydev_methods[] = {
    DEVMETHOD(device_probe,   mydev_probe),
    DEVMETHOD(device_attach,  mydev_attach),
    DEVMETHOD(device_detach,  mydev_detach),
    DEVMETHOD_END
};

static driver_t mydev_driver = {
    "mydev",
    mydev_methods,
    sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, NULL, NULL);
MODULE_VERSION(mydev, 1);
```

El padre `pci` en `DRIVER_MODULE` hace que el kernel ofrezca este driver a cada dispositivo PCI durante el probe, y el método probe del driver decide si reclamar cada uno. La macro `MODULE_VERSION` es una cortesía para cualquiera que use `kldstat -v` para inspeccionar los módulos cargados; también es obligatoria si otros módulos quieren declarar una dependencia del tuyo.

El Makefile es el Makefile estándar de módulo del kernel de FreeBSD:

```makefile
KMOD=   mydev
SRCS=   mydev.c device_if.h bus_if.h pci_if.h

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Los archivos `device_if.h`, `bus_if.h` y `pci_if.h` incluidos en `SRCS` son archivos de cabecera generados. El sistema de construcción los crea bajo demanda, y listarlos le indica a `make` que lo haga antes de compilar `mydev.c`. Si el driver se vuelve más complejo y se divide en varios archivos de código fuente, lista cada archivo de código fuente por separado.

### 6.8 La primera carga con éxito

Cuando cargas el driver mínimo por primera vez, el criterio de éxito es modesto. El driver debe:

1. Cargarse correctamente con `kldload`.
2. Reconocer el dispositivo mediante probe.
3. Asignar sus recursos de memoria e IRQ mediante `bus_alloc_resource_any`.
4. Realizar el reset del dispositivo.
5. Verificar el identificador.
6. Informar del éxito en `dmesg` y permanecer cargado sin errores.
7. Descargarse correctamente con `kldunload` sin provocar un panic.

Un driver que cumpla estos criterios ha realizado un trabajo útil. Ha demostrado que el mapa de registros es al menos parcialmente correcto, que la secuencia de reset funciona y que el registro identificador está donde creías. Comparado con el punto de partida, esto es un avance considerable.

Si alguno de los pasos falla, el fallo es informativo. Un fallo de `bus_alloc_resource_any` suele significar que el mapa de BARs no es el esperado. Un fallo de verificación del identificador suele significar que el offset del registro es incorrecto o que el valor del identificador del dispositivo no es el que suponías. Un panic en la descarga suele significar que un recurso no se liberó correctamente; esto es un bug en la ruta de detach que debe corregirse antes de continuar con cualquier otro trabajo.

### 6.9 Descarga limpia

La ruta de detach es al menos tan importante como la ruta de attach. Un driver que se puede cargar pero no descargar de forma limpia es un driver que requiere un reinicio cada vez que quieres probar un cambio, y el reinicio se convertirá rápidamente en el cuello de botella de tu trabajo. Dedicar tiempo a una ruta de detach limpia desde el principio produce grandes beneficios en velocidad de desarrollo.

```c
static int
mydev_detach(device_t dev)
{
    struct mydev_softc *sc = device_get_softc(dev);

    if (sc->sc_ih != NULL)
        bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
    if (sc->sc_irq != NULL)
        bus_release_resource(dev, SYS_RES_IRQ,
            sc->sc_irid, sc->sc_irq);
    if (sc->sc_mem != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY,
            sc->sc_rid, sc->sc_mem);
    mtx_destroy(&sc->sc_mtx);
    return (0);
}
```

Cada recurso que se asignó debe liberarse. Cada manejador de interrupciones registrado debe desmontarse. Cada mutex debe destruirse. El orden es el inverso del orden de asignación, lo que constituye una regla general útil.

### 6.10 Cerrando la sección 6

El driver mínimo es una base. Hace muy poco, pero ese poco lo hace correctamente. El reset funciona, la verificación del identificador funciona, las asignaciones de recursos son correctas y la ruta de detach es limpia. A partir de esta base, cada nueva funcionalidad puede añadirse de forma incremental, con la confianza de que el andamiaje subyacente es sólido.

La siguiente sección toma el driver mínimo y muestra cómo hacerlo crecer una funcionalidad a la vez, confirmando siempre que cada nueva funcionalidad es correcta antes de añadir la siguiente. La técnica es la disciplina del desarrollo incremental aplicada a un dominio en el que no existe especificación a la que referirse: en lugar de validar contra una especificación, validamos contra capturas y contra el comportamiento medido. El resultado es el mismo: un driver que hace lo que creemos que hace, respaldado por evidencias y no por esperanzas.

## 7. Expansión iterativa del driver y confirmación del comportamiento

El driver mínimo de la sección 6 es la semilla del driver real. Para hacerlo crecer hasta convertirlo en algo útil, añades funcionalidades de una en una y, para cada funcionalidad, confirmas que lo que hace en el driver en ejecución coincide con lo que has observado que hace el dispositivo bajo el driver del fabricante. El proceso es sencillo pero exigente: cada paso debe verificarse, cada verificación debe registrarse, y cada registro pasa a formar parte del pseudo-datasheet que la sección 11 ensamblará.

Esta sección recorre la metodología de la expansión incremental. No introduce nuevas técnicas de bajo nivel, porque las técnicas de bajo nivel son las mismas que en las secciones 4 y 6. Lo que añade es la disciplina de hacer crecer un driver de forma responsable bajo la incertidumbre.

### 7.1 La regla de una funcionalidad a la vez

La regla está en el nombre. Añade una funcionalidad, verifica que funciona, haz commit del resultado en tu repositorio y, a continuación, añade la siguiente. No añadas tres funcionalidades a la vez e intentes depurar la combinación. No añadas una funcionalidad y pases a la siguiente sin verificación. No omitas el commit, porque el commit es lo que te permite revertir cuando un cambio posterior rompe algo que antes funcionaba.

La disciplina es la misma que el desarrollo guiado por pruebas, con una adaptación importante: la prueba es a menudo una comparación con las capturas de la sección 3, no una prueba unitaria en el sentido tradicional. La «prueba» es: «el driver, cuando se invoca para realizar la operación X, produce la misma secuencia de accesos a registros que el driver del fabricante». La comparación es mecánica, pero es una prueba real: las secuencias coinciden o no coinciden, y un fallo te indica que algo ha cambiado.

Una secuencia típica de funcionalidades para un controlador de red podría ser:

1. Leer la dirección MAC.
2. Establecer la dirección MAC.
3. Configurar el anillo de descriptores de recepción.
4. Recibir un paquete.
5. Configurar el anillo de descriptores de transmisión.
6. Enviar un paquete.
7. Activar las interrupciones.
8. Implementar el manejador de interrupciones.
9. Levantar el enlace.
10. Bajar el enlace.
11. Resetear y recuperarse de un error.

Cada elemento es una funcionalidad discreta. Cada elemento se construye sobre los anteriores. Cada elemento tiene un criterio de éxito claro (la operación funciona o no funciona). Y cada elemento tiene una captura correspondiente de la sección 3 con la que se puede comparar el comportamiento del driver.

### 7.2 El bucle de verificación

Para cada nueva funcionalidad, el bucle de verificación es el siguiente:

1. Formula la hipótesis: esta funcionalidad funciona haciendo X.
2. Implementa la funcionalidad en el driver, con logging que registre cada acceso a un registro.
3. Activa la funcionalidad mediante un pequeño programa en espacio de usuario, un sysctl o la interfaz que hayas conectado.
4. Captura la salida del log del kernel.
5. Compara con la captura relevante de la sección 3.
6. Si coinciden, declara la funcionalidad implementada.
7. Si no coinciden, identifica la diferencia y decide si tu implementación es incorrecta o si tu hipótesis era incorrecta.

El bucle es lento. Una sola funcionalidad puede requerir varias iteraciones del bucle, con revisiones de la hipótesis entre medias. Pero la lentitud es precisamente el objetivo. Cada iteración es una prueba deliberada, con una predicción deliberada y una observación deliberada. A lo largo de muchas iteraciones, el conjunto de hechos verificados crece de forma constante y la probabilidad de que una creencia incorrecta sobreviva hasta el driver final disminuye rápidamente.

### 7.3 El patrón de comparación con el driver del fabricante

La comparación con el driver del fabricante es tan central en este trabajo que merece su propio apartado. El patrón tiene el siguiente aspecto.

Dispones de una captura obtenida en la sección 3 del driver del fabricante realizando la operación X. La captura contiene una lista de accesos a registros (en el caso de dispositivos con memoria mapeada) o transferencias USB (en el caso de dispositivos USB), cada uno etiquetado con una marca de tiempo, dirección o endpoint, sentido y valor.

Dispones de tu driver, con el logging activado, realizando la misma operación X. El log del kernel contiene una lista de accesos a registros o transferencias USB, cada uno etiquetado con una marca de tiempo, dirección o endpoint, sentido y valor.

Comparas ambas listas. Los accesos deberían aparecer en el mismo orden, a las mismas direcciones o endpoints, en el mismo sentido, con valores que o bien coinciden exactamente o difieren de formas que puedes explicar (porque, por ejemplo, los valores incluyen números de secuencia o marcas de tiempo que varían de forma natural entre ejecuciones).

Donde ambas listas coinciden, tu driver está haciendo lo que hace el driver del fabricante. Donde discrepan, tienes una discrepancia que investigar. Algunas discrepancias son benignas: tu driver podría omitir una lectura de registro redundante que el driver del fabricante realiza por alguna razón interna, y esa omisión no tiene ningún efecto sobre el comportamiento del dispositivo. Otras discrepancias son errores reales: un valor que escribiste de forma incorrecta, un paso de la secuencia que omitiste, un registro que no sabías que la operación necesitaba.

El patrón de comparación con el driver del fabricante es esencialmente una especificación por ejemplo. La especificación de la operación X es «la secuencia de accesos que realiza el driver del fabricante cuando ejecuta la operación X». Tu implementación cumple la especificación cuando su secuencia de accesos coincide. La técnica no es perfecta: no puede detectar errores que afecten al estado interno del dispositivo sin afectar a la secuencia de accesos, ni puede detectar errores en los valores que escribes si esos valores también cambian entre ejecuciones en la captura del fabricante. Pero detecta la gran mayoría de los errores de implementación, y los detecta exactamente en el nivel de detalle donde se producen.

### 7.4 Documentar cada registro descubierto

A medida que añades funciones, vas descubriendo registros. Cada registro en el que escribes o del que lees en cualquier función forma parte del mapa de registros, y todos ellos deben añadirse al pseudo-datasheet que estás construyendo.

Una técnica útil es mantener el pseudo-datasheet como un archivo Markdown independiente junto al código fuente del driver, y actualizarlo cada vez que descubres un nuevo dato. La sección 11 tratará la estructura del pseudo-datasheet en detalle; por ahora, la regla es simplemente que ningún dato que hayas aprendido debe quedarse únicamente en el código del driver. El código del driver es la implementación; el pseudo-datasheet es la documentación. Sirven a propósitos distintos, y cualquier persona que lea cualquiera de los dos debería poder entender el dispositivo.

Un error frecuente es aplazar la documentación hasta que el proyecto esté "terminado". Los proyectos de ingeniería inversa rara vez terminan en ese sentido. Alcanzan niveles crecientes de completitud con el tiempo, y la documentación debe crecer junto con la implementación. Un driver con veinte funciones y un pseudo-datasheet de un párrafo es un desastre de mantenimiento en ciernes, porque nadie puede extenderlo sin redescubrir todo lo que tú ya sabías.

### 7.5 Cuando el driver del fabricante no concuerda consigo mismo

Una complicación sutil surge cuando el driver del fabricante realiza la misma operación de manera diferente en distintas ocasiones. A veces esto ocurre porque el driver tiene múltiples rutas de código para la misma operación, dependiendo del estado del dispositivo o de la configuración del usuario. A veces ocurre porque el driver tiene un comportamiento opcional que se activa bajo condiciones específicas.

Cuando te encuentres con esa variación, la respuesta correcta es capturar más, no rendirse. Toma capturas de la operación bajo varias condiciones de partida diferentes y compáralas. Si puedes identificar qué condición selecciona cada variante, habrás aprendido algo sobre la lógica del driver. Si las variantes son funcionalmente equivalentes (todas alcanzan el mismo estado final), puedes elegir la más sencilla como implementación canónica. Si no son funcionalmente equivalentes, el dispositivo puede tener múltiples modos de operación que necesitarás soportar.

A veces la variación es simple no determinismo: el driver del fabricante eligió un número de secuencia de un contador, o usó una marca de tiempo, y la captura resultante difiere ligeramente de una ejecución a otra. El no determinismo es benigno, y aprender a reconocerlo (para poder ignorarlo) es parte de la habilidad de comparar capturas.

### 7.6 Ramificar el driver cuando las hipótesis divergen

Cuando tienes dos hipótesis en competencia y un experimento que puede distinguirlas, crea una rama en el driver. Haz una rama de Git para el experimento, implementa el cambio, ejecuta el experimento, observa el resultado y, a continuación, fusiona la rama o descártala. Las ramas son baratas; puedes tener varias ramas de prueba de hipótesis en marcha al mismo tiempo, y puedes volver a cualquiera de ellas cuando un nuevo experimento parezca valioso.

La disciplina de ramificación es especialmente valiosa cuando un experimento requiere cambios significativos en el código. Sin ramas, podrías ser reticente a dedicar una hora a escribir código experimental que luego descartarás. Con ramas, el código experimental vive en su propia rama y tu rama principal permanece limpia. Si el experimento tiene éxito, fusionas. Si falla, descartas. En cualquier caso, tienes un historial limpio.

### 7.7 Cerrando la sección 7

Hemos aprendido cómo hacer crecer un driver mínimo hasta convertirse en uno más capaz mediante un ciclo disciplinado de implementación función a función, comparación con capturas y documentación de cada dato descubierto. La mecánica es simple. La disciplina es exigente. La recompensa es un driver en el que cada función está respaldada por evidencia, cuyo mapa de registros se ha ido construyendo a medida que crece, y cuyo comportamiento puede defenderse ante cualquier desafío futuro.

La siguiente sección aborda un tema ligeramente diferente: cómo generar confianza en tu driver antes de arriesgarlo contra hardware real. La simulación, los dispositivos mock y los bucles de validación son las técnicas que te permiten encontrar errores en un entorno seguro, donde un kernel panic o un bloqueo solo cuesta un reinicio de la VM, en lugar de encontrarlos en producción, donde el coste puede ser mucho mayor.

## 8. Validar suposiciones con herramientas de simulación

El trabajo de ingeniería inversa se mueve entre dos tipos de riesgo. El primero es el riesgo de equivocarse: escribir código que hace algo diferente a lo que crees que hace. El segundo es el riesgo de tener razón demasiado tarde: descubrir un error solo cuando ya ha causado daño, cuando ha llevado al dispositivo a un estado irrecuperable o cuando ha bloqueado una máquina en producción. El primer riesgo es inevitable; los errores son inherentes al trabajo. El segundo riesgo es en gran medida evitable, y la manera de evitarlo es la simulación.

Un simulador es cualquier cosa que te permita ejecutar el código de tu driver sin que el dispositivo real esté conectado. Los simuladores van desde implementaciones mock sencillas de unas pocas lecturas de registro, pasando por emulaciones software completas de un dispositivo entero, hasta entornos de hardware virtualizado que se hacen pasar por el dispositivo real para la pila del sistema operativo que se encuentra por encima. FreeBSD proporciona varias herramientas similares a simuladores que encajan de forma natural en el flujo de trabajo de ingeniería inversa.

Esta sección cubre tres tipos de simulación: dispositivos mock que escribes dentro del kernel, el framework de plantillas USB para emular dispositivos USB y el passthrough de bhyve para ejecutar un dispositivo real sin modificar bajo un hipervisor controlado. Veremos cómo cada tipo ayuda a validar suposiciones, dónde resulta más útil cada uno y cómo combinarlos con las capturas y el trabajo de mapeo de registros de las secciones anteriores.

### 8.1 El dispositivo mock en espacio del kernel

La forma más sencilla de simulación es un pequeño módulo del kernel que expone las mismas interfaces que el dispositivo real pero las implementa en software. El módulo mock se conecta como hijo de un bus existente, asigna una "región de registros" respaldada por software e implementa las lecturas y escrituras de registros en código C que se ejecuta en el kernel.

Para un dispositivo mapeado en memoria, el mock funciona así. Donde el driver real asignaría `SYS_RES_MEMORY` del bus y usaría `bus_read_4` y `bus_write_4` para comunicarse con el dispositivo, el mock le entrega al driver un puntero a un array de palabras asignado por software. Las lecturas devuelven el valor actual de la palabra en el desplazamiento solicitado. Las escrituras actualizan la palabra y disparan los efectos secundarios implementados en software. Los efectos secundarios son la forma en que el mock implementa el comportamiento del dispositivo: cuando el driver escribe el bit "go" en el registro de control, el mock podría iniciar internamente un callout que, tras un tiempo de procesamiento simulado, actualiza el registro de estado para indicar la finalización y lanza una interrupción simulada.

El mock es pequeño, a menudo de unos cientos de líneas para un dispositivo cuyo driver real podría tener varios miles de líneas. El objetivo no es implementarlo todo; el objetivo es implementar lo suficiente como para que el comportamiento del driver pueda probarse de forma aislada. Un driver que maneja correctamente "comando emitido, comando completado, resultados devueltos" frente a un mock ha demostrado al menos que su secuenciación de alto nivel es correcta, aunque el mock no pueda probar qué ocurre cuando el dispositivo real produce valores que el driver no anticipó.

Los archivos complementarios en `examples/part-07/ch36-reverse-engineering/lab04-mock-device/` contienen un pequeño dispositivo mock que muestra el patrón. El mock implementa una interfaz mínima de "comando y estado": hay un registro `CMD` en el que el driver escribe, un registro `STATUS` que el mock actualiza tras un retardo simulado y un registro `DATA` que el driver lee cuando el estado indica la finalización. El mock es intencionadamente sencillo, pero muestra la estructura: define las direcciones de los registros, asigna almacenamiento de respaldo para ellos, implementa los callbacks de lectura y escritura y organiza los callbacks para que lleven a cabo la lógica de efecto secundario que requiera el dispositivo simulado.

### 8.2 Interceptar los accesos del driver

Un mock más sofisticado puede construirse interponiendo en la propia capa bus space. La API `bus_space(9)` de FreeBSD utiliza manejadores opacos, y una implementación personalizada de bus space puede interceptar cada lectura y escritura que realiza el driver. La intercepción puede registrar el acceso, modificar el valor, devolver una respuesta predeterminada o simular cualquier otro comportamiento.

Esta técnica es útil cuando quieres probar un driver que ya ha sido escrito para un dispositivo real, frente a un simulador, con cambios mínimos en el propio driver. El driver continúa usando `bus_read_4` y `bus_write_4` como siempre. La capa de intercepción recibe las llamadas y las envía al dispositivo real o las falsifica según sea necesario.

La técnica también es útil cuando quieres registrar exactamente lo que hace el driver durante una prueba, en una forma que pueda compararse con las capturas de la sección 3. Una capa bus space con intercepción puede producir un archivo de registro que tiene la misma estructura que un archivo de captura, listo para la comparación directa.

Implementar este tipo de intercepción es más complejo que el mock simple, porque tienes que proporcionar una implementación completa de bus space. El patrón está bien establecido en el código fuente de FreeBSD. Los lectores interesados en la versión más detallada de esta técnica pueden estudiar los backends de bus space específicos de la arquitectura en `/usr/src/sys/x86/include/bus.h` y los archivos relacionados; el enfoque de tabla de punteros a funciones que se usa allí es el mismo que usarías para una capa de intercepción.

### 8.3 Drivers de plantilla USB

Para los dispositivos USB, FreeBSD incluye un mecanismo de simulación especialmente capaz: el framework de plantillas USB. El framework de plantillas permite al kernel hacerse pasar por un dispositivo USB, con un conjunto programable de descriptores, exponiendo el endpoint del dispositivo USB a través del controlador host en modo dispositivo. Los archivos fuente relevantes están en `/usr/src/sys/dev/usb/template/`, y el framework incluye plantillas listas para usar para varias clases comunes de dispositivos:

- `usb_template_audio.c` para dispositivos de audio USB.
- `usb_template_kbd.c` para teclados USB.
- `usb_template_msc.c` para dispositivos de almacenamiento masivo USB.
- `usb_template_mouse.c` para ratones USB.
- `usb_template_modem.c` para módems USB.
- `usb_template_serialnet.c` para dispositivos compuestos de red serie USB.

Varias plantillas adicionales en el mismo directorio cubren otras clases, entre ellas CDC Ethernet (`usb_template_cdce.c`), CDC EEM (`usb_template_cdceem.c`), MIDI (`usb_template_midi.c`), MTP (`usb_template_mtp.c`) y dispositivos compuestos multifunción y para teléfonos (`usb_template_multi.c`, `usb_template_phone.c`). Explora el directorio en tu propio sistema para ver la lista completa; el conjunto se amplía con el tiempo a medida que los colaboradores añaden nuevas clases de dispositivos.

Cada plantilla es una descripción programable de un dispositivo USB: qué clase dice ser, qué endpoints expone y cómo son sus descriptores. Cargada en un controlador host USB que funciona en modo dispositivo (el controlador se hace pasar por el lado del dispositivo de un cable USB), la plantilla permite al host ver un dispositivo USB que coincide con los descriptores.

Para el trabajo de ingeniería inversa, las plantillas tienen dos usos. El primero es como base de prueba de sondeo: puedes escribir un descriptor de dispositivo USB que imite la identidad estática de un dispositivo desconocido y usar el framework de plantillas para exponerlo a una máquina de desarrollo. La pila USB de la máquina de desarrollo sondeará los descriptores, conectará drivers (el tuyo, o los drivers del árbol si tus descriptores colisionan) y podrás observar qué espera el host de un dispositivo con esta identidad. El ejercicio es especialmente valioso cuando has capturado el descriptor de un dispositivo desconocido pero no puedes llevar el propio dispositivo a tu máquina de desarrollo; la plantilla te permite simular lo que vería el host.

El segundo uso es como base para un simulador software que implementa el protocolo completo de un dispositivo USB desconocido. Construir semejante simulador es una tarea considerable, pero para objetivos de ingeniería inversa muy importantes puede ser la única manera de probar el código del driver sin arriesgar el hardware real. Las plantillas aportan la parte de los descriptores en la ecuación; la lógica de gestión de datos tendrás que añadirla tú mismo.

### 8.4 bhyve y passthrough

bhyve es el hipervisor nativo de FreeBSD, y su compatibilidad con passthrough PCI es una herramienta muy valiosa para la ingeniería inversa. El código relevante se encuentra en `/usr/src/usr.sbin/bhyve/pci_passthru.c`, y el lado del kernel utiliza el driver `ppt(4)` (`/usr/src/sys/amd64/vmm/io/ppt.c`) para reclamar el dispositivo y exponerlo al guest.

El flujo de trabajo del passthrough es:

1. En el host, desvincula el dispositivo de cualquier driver incluido en el árbol de código fuente:
   ```sh
   $ sudo devctl detach pci0:0:18:0
   ```
2. Configura `ppt(4)` para que reclame el dispositivo añadiendo una entrada a `/boot/loader.conf`:
   ```text
   pptdevs="0/18/0"
   ```
   Esto requiere reiniciar el sistema para que tenga efecto; después, el dispositivo será reclamado por `ppt(4)` en lugar de cualquier otro driver.
3. Inicia un guest bhyve con el dispositivo en passthrough:
   ```sh
   $ sudo bhyve -c 2 -m 2G \
       -s 0,hostbridge \
       -s 1,virtio-blk,disk.img \
       -s 5,passthru,0/18/0 \
       -s 31,lpc \
       -l com1,stdio \
       myguest
   ```
   El argumento `-s 5,passthru,0/18/0` le indica a bhyve que exponga al guest el dispositivo PCI del host situado en 0/18/0, donde aparecerá en el bus PCI virtual.

Dentro del guest, el dispositivo se comporta como un dispositivo PCI normal. El driver del fabricante, ejecutándose en el guest, puede acceder a él exactamente igual que lo haría sobre hardware real. Desde el host, no puedes ver directamente los accesos a los registros del dispositivo, porque el hipervisor los gestiona en nombre del guest, pero puedes usar las facilidades de logging de bhyve para ver los accesos al espacio de configuración, el enrutamiento de interrupciones y otras operaciones que pasan a través del hipervisor.

Para la ingeniería inversa, el valor del passthrough en bhyve es que te permite ejecutar el driver del fabricante en un entorno controlado donde puedes capturar algunos tipos de actividad que de otro modo serían invisibles. También te permite crear snapshots del host y restaurarlo entre experimentos, de modo que puedes recuperarte rápidamente si un experimento sale mal.

Un patrón especialmente útil es desarrollar tu propio driver en el host, con el dispositivo desvinculado de `ppt(4)`, y cambiar al passthrough en el guest solo cuando quieras comparar el comportamiento de tu driver con el del fabricante. El cambio requiere modificar la configuración de `pptdevs` y reiniciar, pero el inconveniente es pequeño comparado con la ventaja de poder ejecutar ambas implementaciones sobre el mismo hardware.

### 8.5 Bucle con estados de retorno conocidos

Una forma de simulación simple pero eficaz es el bucle de validación con estados de retorno conocidos. La idea es instrumentar el driver para que, en modo de prueba, ciertas lecturas de registros devuelvan valores que tú especificas en lugar de valores procedentes del dispositivo. A continuación, ejercitas el driver y observas qué hace en respuesta a cada valor elegido manualmente.

Por ejemplo, supón que quieres saber qué hace tu driver cuando el dispositivo devuelve un valor de estado inesperado. En producción, el dispositivo podría no producir nunca ese valor, y no hay una forma obvia de probar esa ruta. Con un bucle de validación, sustituyes la lectura del registro de estado por una función que devuelve una secuencia de valores que tú especificas: 0x00, luego 0x01, luego 0xff y de nuevo 0x00. El driver se comporta como si el dispositivo hubiera devuelto cada uno de esos valores por turno, y puedes verificar que la respuesta del driver es correcta en cada caso.

La técnica del bucle de validación resulta especialmente valiosa para las rutas de error. La mayoría de las condiciones de error son poco frecuentes en operación real, y esperar a que se produzcan de forma natural llevaría una eternidad. Al forzar que se produzcan a demanda, puedes probar las rutas de error de forma sistemática. La técnica es, en esencia, la misma que las técnicas de inyección de fallos utilizadas en el software de sistemas maduro, adaptada al contexto de la ingeniería inversa donde el propio driver es el artefacto bajo prueba.

### 8.6 Límites de la simulación

La simulación es enormemente valiosa, pero tiene límites. Un simulador implementa la comprensión que tiene el simulador del dispositivo, no el dispositivo en sí. Cuando el simulador y el dispositivo real difieren, las pruebas del simulador pasan y el dispositivo real se comporta mal, y habrás aprendido que tu comprensión del dispositivo era incompleta de alguna manera específica.

La actitud correcta es tratar la simulación como una forma de encontrar ciertas clases de errores (errores de lógica del driver, errores de secuencia, errores en el manejo de errores) y no como una garantía de que el driver funcionará en hardware real. Los errores en la comprensión que tiene el driver del contrato del dispositivo no serán detectados por un simulador que comparte ese malentendido. Los errores en el comportamiento real del dispositivo que difieren de lo que implementa el simulador no serán detectados por las pruebas del simulador en absoluto.

La combinación es la respuesta. Las pruebas con el simulador detectan los errores que pueden detectar. Las pruebas con el dispositivo real, realizadas con cuidado e incrementalmente con las técnicas de seguridad de la Sección 10, detectan el resto. Juntas son mucho mejores que cualquiera de ellas por separado.

### 8.7 Cerrando la Sección 8

Hemos visto las herramientas de simulación que complementan el flujo de trabajo de la ingeniería inversa: pequeños dispositivos simulados en el kernel, el framework de plantillas USB, el passthrough con bhyve y los bucles de validación con estados de retorno conocidos. Cada herramienta aborda un aspecto diferente del problema, y juntas permiten encontrar una gran cantidad de errores antes de que alguno de ellos llegue al dispositivo real.

La siguiente sección da un paso atrás respecto al trabajo técnico y examina el contexto social: cómo colaboran los proyectos de ingeniería inversa, dónde encontrar trabajo previo y cómo publicar tus propios hallazgos. La parte técnica de la ingeniería inversa es la parte visible, pero la parte comunitaria es lo que hace que el trabajo perdure. Un driver bien documentado y bien compartido dura. Un driver escrito rápidamente y que nunca se publica suele desaparecer con su autor.

## 9. Colaboración comunitaria en la ingeniería inversa

La ingeniería inversa raramente es una actividad solitaria, aunque lo parezca. Casi todos los proyectos de ingeniería inversa exitosos se construyen sobre trabajo previo o acaban contribuyendo a él. La comunidad de personas que se preocupa por un dispositivo concreto, o por una familia concreta de dispositivos, es pequeña pero persistente, y aprender a encontrar esa comunidad y contribuir a ella es una de las cosas más eficientes que puede hacer quien se dedica a la ingeniería inversa.

Esta sección trata de cómo encontrar trabajo previo, cómo evaluar su fiabilidad, cómo construir sobre él sin problemas legales o éticos, y cómo compartir tus propios hallazgos de una forma que ayude a los demás. El trabajo no es glamuroso, pero es lo que convierte la ingeniería inversa en una actividad productiva a largo plazo en lugar de una serie de esfuerzos inconexos.

### 9.1 Encontrar trabajo previo

Cuando empiezas a investigar un dispositivo, lo primero que debes hacer es buscar trabajo previo. Incluso los dispositivos que parecen oscuros son a veces objeto de investigación previa significativa, y encontrar esa investigación ahorra una enorme cantidad de tiempo.

Las fuentes más valiosas, en orden aproximado de utilidad:

**Árbol de código fuente de FreeBSD.** Busca en `/usr/src/sys/dev/` el vendor ID del dispositivo, el device ID, el descriptor USB o el nombre del fabricante. Un driver en el árbol de FreeBSD es el estándar de referencia del trabajo previo, porque ya está validado, compila con el kernel actual y sigue las convenciones de FreeBSD. Incluso cuando no existe ningún driver, los drivers relacionados del mismo fabricante suelen compartir patrones aplicables.

**Árboles de código fuente de OpenBSD y NetBSD.** Los demás BSDs tienen sus propias colecciones de drivers, y a menudo tienen drivers para dispositivos que FreeBSD aún no admite. El código es generalmente sencillo de leer y de portar. La licencia suele ser compatible con FreeBSD (la mayor parte del código bajo licencia BSD lo es).

**Árbol de código fuente de Linux.** El kernel de Linux tiene la colección más amplia de drivers de dispositivo de cualquier proyecto de código abierto. La mayoría están bajo licencia GPL, lo que significa que no puedes copiar código en un driver de FreeBSD, pero puedes leerlos para entender el dispositivo y escribir tu propia implementación desde cero. El enfoque de sala limpia de leer el driver de Linux para entender el dispositivo y luego escribir código original a partir de esa comprensión está bien establecido y es legalmente aceptado en casos de interoperabilidad; el marco completo para hacerlo de forma segura, incluido el rastro documental del que realmente depende una defensa de sala limpia, aparece más adelante en este capítulo en la Sección 12. Incluso cuando la cuestión legal está resuelta, un port directo de Linux a FreeBSD raramente es viable por razones puramente técnicas. Los drivers de Linux se apoyan en APIs del kernel que no tienen un equivalente directo en FreeBSD: sus primitivas de spinlock y mutex tienen una semántica de despertar diferente, sus asignadores de memoria distinguen contextos que FreeBSD expresa de forma diferente, y su modelo de dispositivos (con `struct device`, sysfs y el flujo de probe del driver-core) no se corresponde con Newbus. La forma correcta de usar un driver de Linux es extraer de él los hechos a nivel de dispositivo (distribución de registros, valores mágicos, orden de inicialización, tablas de quirks) y luego reconstruir el flujo de control usando las propias primitivas de FreeBSD. La Sección 13 muestra dos drivers de FreeBSD que hicieron exactamente eso.

**Sitios comunitarios específicos del hardware.** Para muchos dispositivos, los entusiastas han creado sitios dedicados que recopilan todo lo conocido sobre una familia concreta. La wiki de Wireshark tiene documentación extensa sobre protocolos USB. El proyecto OpenWrt tiene documentación sobre muchos dispositivos embebidos. Muchas familias de hardware específicas tienen sus propias wikis o foros comunitarios.

**Listas de correo de fabricantes, archivos de soporte técnico, notas de aplicación.** Algunos fabricantes publican más información en contextos de soporte técnico que en su documentación pública. Una búsqueda en sus archivos de soporte o en las listas de correo para desarrolladores puede revelar a veces información que no aparece en la documentación oficial.

**Patentes.** Las solicitudes de patentes contienen a menudo descripciones detalladas de cómo funciona un dispositivo, porque el requisito legal de describir la invención obliga a cierto nivel de divulgación. Las bases de datos de patentes se pueden consultar por empresa y por año, y una búsqueda de las patentes del fabricante adecuado puede revelar a veces una gran cantidad de detalles.

**Artículos académicos.** Cuando el dispositivo pertenece a un campo especializado (instrumentación científica, control industrial, ciertas clases de equipos de red), los artículos académicos de ese campo pueden haber documentado ya la interfaz del dispositivo con fines de investigación.

La primera hora de cualquier proyecto debe dedicarse a buscar en estas fuentes. Incluso una búsqueda infructuosa es informativa: saber que no existe trabajo previo cambia significativamente el alcance del proyecto.

### 9.2 Evaluar la fiabilidad

No todo el trabajo previo es igualmente fiable. Un driver de código abierto que funciona es muy fiable, porque las afirmaciones del autor están respaldadas por código que funciona de forma demostrable. Una página de wiki comunitaria escrita por un entusiasta puede ser o no fiable, según el historial de la fuente. Un documento técnico de un fabricante es generalmente fiable en los puntos que el fabricante quiere destacar, y posiblemente guarda silencio o puede inducir a error en los puntos que preferiría no comentar.

La habilidad de evaluar fuentes es la misma que desarrollan los historiadores y los periodistas. Te preguntas: ¿quién escribió esto, cuándo, sobre qué base y con qué motivaciones? Comparas afirmaciones entre múltiples fuentes. Das más peso a las afirmaciones respaldadas por código o por medición directa. Das menos peso a las afirmaciones que dependen únicamente de la reputación del autor.

Cuando adoptas un dato del trabajo previo, registra de dónde proviene. La entrada en el cuaderno debe decir «del driver ath5k de Linux, el registro XYZ en el desplazamiento N es el selector de canal». Un año después, cuando ese dato resulte ser incorrecto, sabrás de dónde vino el error y qué otros datos de la misma fuente podrían también ser incorrectos.

### 9.3 La disciplina de sala limpia

La disciplina de sala limpia es la metodología estándar para utilizar trabajo previo sin infringir sus derechos de autor. La disciplina es sencilla pero requiere cuidado.

En la forma estricta de sala limpia intervienen dos personas. Una lee el trabajo previo (un driver del fabricante, una especificación filtrada, un binario desensamblado) y produce una descripción de lo que hace el dispositivo. La descripción está en lenguaje natural y no contiene material protegido por derechos de autor. La segunda persona lee únicamente la descripción y escribe el nuevo driver. La segunda persona nunca ve el trabajo original. Como el nuevo driver se escribió sin tomar como referencia el original, no puede considerarse una obra derivada de él según la legislación de derechos de autor.

En la forma relajada, que se utiliza con frecuencia en proyectos en solitario, la misma persona desempeña ambos roles, pero tiene cuidado de mantenerlos separados en el tiempo y de documentar esa separación. Lee el trabajo previo. Toma notas. Deja el trabajo previo a un lado. Espera. Después, trabajando únicamente a partir de sus notas, escribe el driver. Las notas son el puente; el código original nunca está delante de ti mientras escribes el nuevo código.

La forma relajada es jurídicamente menos sólida que la forma estricta, pero es la que utilizan en la práctica la mayoría de los ingenieros inversos que trabajan solos, y la jurisprudencia sobre trabajos de interoperabilidad generalmente la respalda. Lo esencial es la disciplina de separar la fase de lectura de la fase de escritura, de modo que puedas demostrar (y demostrarte a ti mismo) que el nuevo código es independiente.

En todos los casos, el nuevo driver no debe contener código copiado, macros de nombres de registros copiadas, estructuras de datos copiadas ni comentarios copiados del original. Puede contener ideas, patrones estructurales y la propia interfaz del dispositivo, porque esos elementos no son susceptibles de protección por derechos de autor. Todo aquello que sí lo sea debe reexpresarse con tus propias palabras y con tu propia estructura.

### 9.4 Publicación de los hallazgos

El trabajo de ingeniería inversa, cuando está terminado, debe publicarse. La publicación tiene varios propósitos: permite que otros usen el driver, que otros lo extiendan, preserva el conocimiento en caso de que dejes de trabajar en él y contribuye al cuerpo de conocimiento público sobre el dispositivo.

La publicación mínima útil es un repositorio de Git que contiene:

- El código fuente del driver, con una licencia clara.
- Un README que explique qué hace el driver y cómo compilarlo.
- El pseudo-datasheet (sección 11) que documenta el dispositivo.
- Los programas de prueba y cualquier otra herramienta necesaria para usar el driver.
- El cuaderno de notas, o una versión depurada del mismo, para que otros puedan rastrear el razonamiento detrás de cada afirmación.

Una publicación más elaborada añade:

- Un documento que describa la metodología, para que otros puedan reproducir el trabajo.
- Una lista de preguntas abiertas y cuestiones sin resolver.
- Una distinción clara entre hechos verificados e hipótesis pendientes.
- Atribución a todas las fuentes de trabajo previo, con enlaces.

La licencia importa. Para un driver destinado a su inclusión en FreeBSD, la licencia BSD de dos cláusulas es la opción estándar. La licencia debe hacer que el trabajo sea compatible con los requisitos de licencia del árbol de código fuente de FreeBSD. Si has usado material de un proyecto con licencia GPL, incluso de forma cleanroom, documenta la procedencia con cuidado para que cualquier duda sobre derivación pueda resolverse examinando tus notas.

Una vez publicado el trabajo, anúncialo en los lugares donde lo verán las personas interesadas: la lista de correo FreeBSD-arm para trabajo embebido, la lista FreeBSD-net para drivers de red, las comunidades relevantes de cada proyecto y tus canales personales. El anuncio debe ser breve pero informativo: qué hace el driver, en qué estado de completitud se encuentra, dónde está el repositorio y quién puede contribuir.

### 9.5 Mantenimiento de un pseudo-datasheet en Markdown o Git

El pseudo-datasheet en sí merece una atención especial. Es el artefacto más valioso que produces, a menudo más valioso que el propio código del driver. El código del driver es una implementación de tu comprensión del dispositivo; el pseudo-datasheet es la comprensión misma, y a partir de él se puede escribir cualquier número de drivers (FreeBSD, Linux, NetBSD, sistemas embebidos personalizados).

El formato que ha demostrado ser más útil en proyectos comunitarios es un archivo Markdown (o un pequeño conjunto de archivos) con seguimiento en Git, estructurado en torno a los componentes lógicos del dispositivo: identidad, mapa de registros, disposición de los buffers, conjunto de comandos, códigos de error, secuencia de inicialización y ejemplos de programación. Examinaremos la estructura en detalle en la sección 11.

El seguimiento en Git es esencial. El pseudo-datasheet es un documento vivo; se actualizará a medida que se descubran nuevos hechos, se corrijan los antiguos y trabajo adicional de ingeniería inversa amplíe su cobertura. El historial de Git es el registro de cómo ha evolucionado esa comprensión. Cuando se corrige un hecho, el log de Git muestra cuándo se corrigió y quién lo hizo, de modo que otros lectores puedan saber cuándo su copia impresa ha quedado desactualizada.

El código de acompañamiento en `examples/part-07/ch36-reverse-engineering/lab06-pseudo-datasheet/` contiene una plantilla Markdown para un pseudo-datasheet, adecuada como punto de partida para tus propios proyectos. La plantilla tiene una estructura bien definida, porque una estructura coherente hace el documento más fácil de leer, más fácil de comparar con otros pseudo-datasheets y más fácil de extraer automáticamente a otros formatos.

### 9.6 Cerrando la sección 9

La faceta de colaboración comunitaria de la ingeniería inversa es la que da longevidad al trabajo. Un driver escrito en aislamiento y nunca publicado puede servir bien a su autor, pero desaparece con él. Un driver escrito en diálogo con el trabajo previo y publicado de forma clara sirve a un público mucho más amplio y dura mucho más tiempo.

La siguiente sección aborda el aspecto de seguridad del trabajo, al que hemos hecho referencia repetidamente. La seguridad merece su propia sección dedicada, porque las consecuencias de equivocarse pueden incluir daños permanentes en hardware costoso, y las técnicas para evitar esos daños son lo suficientemente concretas como para enseñarse de forma explícita.

## 10. Cómo evitar inutilizar o dañar el hardware

Esta es la sección que el capítulo ha estado posponiendo todo el tiempo. La ingeniería inversa, realizada sin cuidado, puede dañar el hardware. Algunos tipos de daño son recuperables con esfuerzo. Otros convierten un dispositivo en funcionamiento en un ladrillo inerte que nada, salvo una herramienta de recuperación especializada del fabricante (si es que existe), puede reparar. Un ingeniero inverso que trabaja sin entender qué patrones son peligrosos acabará destruyendo algo. El objetivo de esta sección es indicarte, de forma concreta, qué patrones evitar y cuáles son seguros.

Los consejos de esta sección se aplican a todas las demás secciones del capítulo. Las técnicas de sondeo de la sección 4 son seguras únicamente cuando se combinan con estas advertencias. El driver mínimo de la sección 6 es seguro únicamente cuando sus escrituras están restringidas tal como se describe aquí. La expansión iterativa de la sección 7 es segura únicamente cuando cada nueva funcionalidad se evalúa según estos criterios antes de permitirle escribir en el dispositivo. Lee esta sección completa; no saltes a las secciones de técnicas sin haberla leído.

### 10.1 El principio general

El principio general es sencillo de enunciar y, con práctica, fácil de aplicar.

> **Nunca escribas en un registro desconocido ni envíes un comando desconocido sin evidencias sólidas de que la operación es segura.**

Las evidencias sólidas pueden provenir de varias fuentes, y cada una merece su propia breve discusión.

**El driver del fabricante realizó la misma operación.** Si has capturado al driver del fabricante escribiendo un valor específico en un registro específico, y el comportamiento resultante del dispositivo fue correcto, entonces realizar la misma escritura en tu driver es, casi con toda seguridad, seguro. Se supone que el driver del fabricante está bien probado y no haría casualmente algo que inutilice el dispositivo.

**La operación es una operación estándar documentada.** Algunas operaciones son tan universales que pueden considerarse seguras. Las lecturas del espacio de configuración PCI son seguras por diseño. Las escrituras en los registros de control estándar PCI (borrar interrupciones pendientes, habilitar bus mastering, activar el acceso a memoria) son seguras en el rango estándar. Las transferencias de control USB mediante peticiones estándar en endpoints estándar son seguras. Las partes estandarizadas de un protocolo son de confianza; las partes específicas del fabricante no lo son sin evidencias.

**La operación es reversible.** Una escritura que puedes deshacer inmediatamente es mucho más segura que una que no puedes. Establecer un bit de configuración, observar el efecto y luego restaurar el valor original es mucho más seguro que realizar una operación con consecuencias permanentes.

**La operación se ha probado en simulación.** Una escritura que has probado primero contra un mock o un simulador, donde el peor resultado posible es un kernel panic en una VM de prueba, es más segura que una escritura que solo has modelado mentalmente.

En ausencia de cualquiera de estas formas de evidencia, la operación es desconocida, y la regla es: no la realices.

### 10.2 Operaciones que merecen especial precaución

Algunas categorías de operaciones son suficientemente peligrosas como para merecer atención explícita. Estas son las operaciones en las que una escritura descuidada puede dañar permanentemente un dispositivo, y son las que nunca deben realizarse sin evidencias sólidas.

**Escrituras en flash.** Muchos dispositivos contienen memoria flash utilizada para firmware, configuración, calibración o identificadores del dispositivo. Las escrituras en flash son irreversibles (o, en el mejor de los casos, reversibles restaurando una copia de seguridad). Un driver que escribe accidentalmente en flash puede corromper el firmware (dejando el dispositivo sin posibilidad de arranque), sobreescribir datos de calibración (dejando el dispositivo impreciso o inutilizable) o cambiar la identidad del dispositivo (haciéndolo irreconocible para su propio firmware). Los patrones que desencadenan escrituras en flash varían según el dispositivo, pero a menudo implican secuencias de "desbloqueo" específicas seguidas de escrituras en direcciones que corresponden explícitamente a la región de flash. Si tus capturas muestran dicha secuencia en un contexto en el que el usuario realizó explícitamente una operación de "actualización de firmware", no repliques la secuencia en tu driver a menos que estés implementando explícitamente la actualización de firmware.

**Hard resets que afectan al estado no volátil.** La mayoría de los reinicios son soft resets que devuelven el dispositivo a su estado inicial en memoria. Algunos reinicios, sin embargo, también borran el estado no volátil: datos de calibración, configuración, programación de identificadores. Estas operaciones de "restablecimiento de fábrica" a veces se activan con el mismo registro que activa un soft reset, pero con un patrón de bits diferente. Usa únicamente el patrón de soft reset que el driver del fabricante utiliza en attach, y no experimentes con patrones de bits que no hayas visto usar al driver del fabricante.

**Escrituras en EEPROM.** Al igual que la flash, la EEPROM contiene configuración a largo plazo. Las escrituras en EEPROM suelen orquestarse mediante un protocolo específico de escrituras en registros, y un protocolo incorrecto puede corromper el estado de la EEPROM. Evita las escrituras en EEPROM durante la exploración; si debes realizarlas, hazlo solo con una comprensión clara de qué valor debe quedar en la EEPROM y con un modo de verificar el resultado.

**Modificación de identificadores del dispositivo.** Algunos dispositivos almacenan sus IDs de fabricante y de dispositivo en flash o EEPROM, y un bug del driver puede sobreescribirlos. Un dispositivo cuyo ID ha cambiado no será reconocido por ningún driver que busque coincidencia con el ID original, y su recuperación puede requerir una reprogramación física con un programador de hardware. No escribas en ningún registro que pueda estar relacionado con la identificación del dispositivo.

**Cambios de estado de gestión de energía.** Algunos dispositivos tienen estados de energía cuyas transiciones se gestionan mediante secuencias complejas de escrituras en registros. Una secuencia incorrecta puede dejar el dispositivo en un estado del que no puede recuperarse fácilmente, lo que a veces requiere apagar y encender el sistema o incluso retirar el dispositivo físicamente. La gestión de energía es una de las áreas más frágiles de la interfaz de cualquier dispositivo.

**Configuración de PHY o PLL.** Los dispositivos que incluyen su propia generación de reloj (los dispositivos PCI Express en particular) tienen una configuración de PLL que, si se establece incorrectamente, puede dejar el dispositivo incapaz de comunicarse. La configuración de PHY en dispositivos de red tiene escollos similares. Estos son subsistemas en los que el driver del fabricante debe seguirse exactamente, porque no hay una buena forma de recuperarse de una configuración incorrecta mediante ninguna configuración adicional.

**Habilitación de bus master con una dirección DMA mal configurada.** Si habilitas el bus mastering en un dispositivo que tiene una dirección DMA apuntando a una ubicación de memoria arbitraria, el dispositivo puede leer o escribir en esa ubicación y corromper la memoria del sistema. Esta es una de las pocas formas en que un bug del driver puede hacer caer el sistema a través de una acción de hardware. Establece siempre mapeos DMA válidos antes de habilitar el bus mastering, y nunca habilites el bus mastering si aún no estás preparado para gestionar DMA.

Estas categorías no son exhaustivas, pero cubren las formas más comunes de dañar un dispositivo. La lección general es: respeta las operaciones cuyas consecuencias no entiendes completamente, y no experimentes con ellas.

### 10.3 Soft resets y temporizadores watchdog

La faceta constructiva del debate sobre seguridad es qué hacer cuando algo sale mal. La primera técnica es el soft reset.

Un soft reset es el mecanismo del dispositivo que equivale a decir "me he quedado en un mal estado, por favor reiníciame". El patrón es el mismo que el reinicio que implementaste en la sección 6: una escritura en el registro de control que devuelve el dispositivo a su estado inicial conocido. Usado con cautela, un soft reset puede recuperarse de muchos tipos de problemas sin necesidad de intervención.

El patrón en código es sencillo:

```c
static void
mydev_recover(struct mydev_softc *sc)
{
    device_printf(sc->sc_dev, "recovering device\n");
    mydev_reset(sc);
    /* Reapply any necessary configuration. */
    mydev_init_after_reset(sc);
}
```

La rutina de recuperación debe llamarse desde cualquier ruta de código que detecte que algo ha salido mal: un timeout, un estado de error, un valor de registro inesperado. El coste de un reinicio innecesario es pequeño; el coste de un error sin recuperar puede ser mucho mayor.

Un watchdog timer es la siguiente capa de defensa. El driver comprueba periódicamente que el dispositivo está progresando y, si el progreso se ha detenido, activa una recuperación. El patrón es:

```c
static void
mydev_watchdog(void *arg)
{
    struct mydev_softc *sc = arg;
    uint32_t counter;

    mtx_lock(&sc->sc_mtx);
    counter = bus_read_4(sc->sc_mem, MYDEV_REG_COUNTER);
    if (counter == sc->sc_last_counter) {
        sc->sc_stall_ticks++;
        if (sc->sc_stall_ticks >= MYDEV_STALL_LIMIT) {
            device_printf(sc->sc_dev, "device stalled, resetting\n");
            mydev_recover(sc);
            sc->sc_stall_ticks = 0;
        }
    } else {
        sc->sc_stall_ticks = 0;
    }
    sc->sc_last_counter = counter;
    callout_reset(&sc->sc_watchdog, hz, mydev_watchdog, sc);
    mtx_unlock(&sc->sc_mtx);
}
```

El watchdog lee un contador que el dispositivo debería incrementar durante el funcionamiento normal. Si el contador no cambia durante varias iteraciones, el watchdog asume que el dispositivo se ha bloqueado y activa la recuperación. El patrón es robusto frente a ralentizaciones transitorias (se tolera un único incremento perdido) pero detecta bloqueos genuinos en pocos segundos.

### 10.4 Hacer una copia de seguridad del firmware antes de cualquier operación arriesgada

En los dispositivos con firmware (que son la mayoría de los dispositivos modernos), conviene hacer una copia de seguridad del propio firmware antes de cualquier operación arriesgada. Si tus capturas muestran que el firmware lo carga el host durante la inicialización, entonces la imagen del firmware probablemente ya existe en algún lugar como archivo y puedes guardar una copia. Si el firmware reside en la memoria flash del dispositivo y no lo carga el host, tienes que leerlo a través del mecanismo que el dispositivo exponga para leer la flash y almacenar el resultado.

La copia de seguridad es tu seguro frente al caso en que algún experimento sobreescriba el firmware. Con la copia puedes restaurar el original. Sin ella, puede que tengas un ladrillo.

La disciplina de hacer una copia antes de cualquier operación arriesgada es la misma que la de hacer una copia antes de cualquier tarea de administración del sistema que entrañe riesgo. El coste es pequeño, el valor cuando importa es enorme, y las personas que se la saltan son las que, tarde o temprano, se arrepienten de haberlo hecho.

### 10.5 Sondeos de solo lectura siempre que sea posible

Siempre que un experimento pueda realizarse en modo de solo lectura, realízalo así. La información obtenida suele valer mucho más que el tiempo ahorrado al escribir en lugar de leer. El trabajo de mapeo de registros de la sección 4 fue casi enteramente de solo lectura por una razón: leer es seguro, escribir no lo es.

Un patrón útil cuando debes investigar el efecto de las escrituras es leer el valor, calcular cuál esperas que sea el nuevo valor, realizar la escritura, leer el valor de nuevo y verificar que el resultado coincide con tu expectativa. El paso de verificación te permite detectar el caso en que la escritura no tuvo el efecto esperado antes de basar trabajo posterior en una suposición incorrecta.

```c
static int
mydev_set_field(struct mydev_softc *sc, bus_size_t off,
    uint32_t mask, uint32_t value)
{
    uint32_t old, new, readback;

    old = bus_read_4(sc->sc_mem, off);
    new = (old & ~mask) | (value & mask);
    bus_write_4(sc->sc_mem, off, new);
    readback = bus_read_4(sc->sc_mem, off);
    if ((readback & mask) != (value & mask)) {
        device_printf(sc->sc_dev,
            "set_field off=0x%04jx mask=0x%08x value=0x%08x "
            "readback=0x%08x mismatch\n",
            (uintmax_t)off, mask, value, readback);
        return (EIO);
    }
    return (0);
}
```

El helper ejecuta el ciclo de leer-modificar-escribir-verificar en un único lugar, registrando cualquier discrepancia. Usado de forma consistente, detecta los casos en que una escritura no se consolidó (porque el registro es de solo lectura, porque la escritura codificó el valor de forma diferente a lo esperado, o porque el estado del dispositivo no permitía el cambio en ese momento) antes de que provoquen confusiones posteriores.

### 10.6 Envoltorios de sondeo seguros

El código complementario en `examples/part-07/ch36-reverse-engineering/lab05-safe-wrapper/` contiene una pequeña biblioteca de envoltorios de sondeo seguros que combinan varias de las técnicas aquí descritas: timeouts en cada operación, recuperación automática ante bloqueos detectados, modos de solo lectura y registro por operación. Los envoltorios añaden unos pocos cientos de microsegundos a cada operación, algo irrelevante durante la exploración, y detectan la gran mayoría de los problemas de seguridad antes de que se vuelvan perjudiciales.

Este patrón se recomienda para todos los drivers exploratorios. El driver de producción, cuando llegue a existir, puede prescindir de los envoltorios en favor de accesos directos más eficientes, pero durante la exploración los envoltorios merecen su coste en términos de seguridad.

### 10.7 Reconocer cuándo detenerse

La decisión de seguridad más difícil es a veces la de detenerse. Cuando un experimento no se comporta como se esperaba, cuando el dispositivo produce valores que no puedes explicar, cuando todas tus hipótesis son erróneas y todavía no tienes otras nuevas, la tentación es seguir investigando hasta que algo se aclare. Resiste esa tentación. Para, toma un descanso, vuelve a mirar las capturas, habla con alguien sobre lo que has observado y deja que la situación se aclare antes de reanudar el trabajo.

La razón es que los momentos de confusión son exactamente los momentos en que el daño accidental es más probable. Una mente despejada es cuidadosa acerca de qué escrituras son seguras; una mente frustrada prueba cosas que parecen prometedoras y descubre, demasiado tarde, que una de ellas era destructiva.

Un breve descanso también permite que tu subconsciente trabaje. Muchos de los hallazgos más útiles en la ingeniería inversa llegan mientras haces otra cosa: caminar, dormir, trabajar en un problema sin relación. El cerebro tiene una capacidad notable para integrar observaciones en una comprensión coherente cuando se le da la oportunidad. Obligarte a seguir al teclado cuando el trabajo se ha atascado a menudo no produce nada útil y a veces produce desastres.

### 10.8 Cerrando la sección 10

La seguridad en la ingeniería inversa se construye sobre un pequeño conjunto de disciplinas: nunca escribas sin evidencia sólida, reconoce las operaciones que merecen especial cautela, incorpora recuperación mediante soft-reset y watchdog timers, haz copias de seguridad del firmware antes de operaciones arriesgadas, prefiere los experimentos de solo lectura, usa envoltorios de sondeo seguros y sabe cuándo detenerte. Ninguna de estas disciplinas es complicada. Todas ellas son necesarias. Un ingeniero de ingeniería inversa que las siga difícilmente dañará el hardware. Uno que las ignore, tarde o temprano, lo hará con toda seguridad.

La siguiente sección cierra la parte técnica del capítulo explicando cómo convertir el conjunto de trabajo que las secciones anteriores han ido construyendo en un driver mantenible y un pseudo-datasheet utilizable. El proceso de ingeniería inversa que comenzó con observaciones e hipótesis termina con un driver y un documento, y la forma en que se construyen esos artefactos finales determina la utilidad que tendrá el proyecto para sus futuros lectores.

## 11. Refactorización y documentación del dispositivo analizado

El fin de un proyecto de ingeniería inversa no es el momento en que el driver funciona. Es el momento en que el driver y el pseudo-datasheet juntos pueden entregarse a otro ingeniero que pueda leerlos y comprender tanto el dispositivo como la implementación. Hasta ese momento, el proyecto está incompleto, aunque el driver parezca funcionar. Esta sección recorre el trabajo de consolidar los hallazgos del proyecto en una forma mantenible.

El trabajo tiene dos partes. El driver en sí necesita limpiarse, reestructurarse para seguir las convenciones normales de escritura de drivers, y documentarse del modo en que cualquier driver de FreeBSD debería estar documentado. El pseudo-datasheet, que ha ido creciendo a lo largo del proyecto como un cuaderno de hechos, debe reorganizarse en un documento independiente que alguien que nunca ha visto el proyecto pueda leer y del que pueda aprender.

### 11.1 La limpieza del driver

Un driver obtenido por ingeniería inversa, en su estado funcional pero sin limpiar, suele llevar rastros de su historia: comentarios que hacen referencia a "el supuesto registro de control", bloques de código que prueban múltiples hipótesis con compilación condicional, sysctls de depuración que se añadieron durante una investigación concreta, mensajes de log que fueron útiles en su momento pero ya no son necesarios. Limpiar significa eliminar el ruido histórico preservando el contenido sustancial.

La lista de comprobación de limpieza:

1. Elimina cada bloque de compilación condicional que se usó para probar alternativas. La alternativa elegida debe permanecer; las demás deben eliminarse.
2. Sustituye los nombres especulativos por nombres confirmados. Si un registro se llamaba originalmente `MYDEV_REG_UNKNOWN_40` y ahora se sabe que es el registro de selección de canal, renómbralo como `MYDEV_REG_CHANNEL`.
3. Sustituye los comentarios de investigación por comentarios explicativos. Un comentario que dice "Creo que esto podría ser el retardo de polling" es un comentario hipotético de la fase de investigación; sustitúyelo por uno que diga "Esperar a que el dispositivo complete el reset" una vez confirmada la hipótesis.
4. Elimina los sysctls de depuración que ya no sean útiles y conserva los que operadores o mantenedores querrán tener.
5. Elimina las llamadas a `device_printf` que fueron valiosas durante la investigación pero ahora son ruido en producción.
6. Verifica que la ruta de detach esté limpia. Un driver que carga correctamente pero falla al descargarse es un problema de mantenimiento.
7. Verifica que todas las rutas de error liberen sus recursos correctamente. Un driver que pierde recursos en caso de error es un problema que tarde o temprano se manifestará.

El código complementario en `examples/part-07/ch36-reverse-engineering/` incluye tanto el andamiaje de la fase de investigación como la forma limpia, para que puedas ver la diferencia. La forma limpia es la que resultaría adecuada para su inclusión en el árbol de código fuente de FreeBSD (hablaremos de la inclusión en el Capítulo 37).

### 11.2 Documentación del driver

Un driver de FreeBSD debería tener al menos una página de manual en la sección 4, e idealmente también una sección en la documentación para desarrolladores del kernel. La página de manual es para los usuarios que quieren saber qué hace el driver y cómo configurarlo. La documentación para desarrolladores es para los kernel hackers que quieren entender el driver internamente.

La página de manual sigue el estilo estándar de FreeBSD. La página de manual `style(4)` documenta las convenciones, y las páginas de manual de drivers similares son buenos ejemplos a seguir. Una página de manual típica de un driver cubre:

- El nombre del driver y una descripción de una línea.
- La sinopsis que muestra cómo cargar el driver en `loader.conf`.
- La descripción, que explica qué hardware soporta el driver y qué características ofrece.
- La sección de soporte de hardware, que enumera los dispositivos concretos con los que funciona el driver.
- La sección de configuración, que describe los sysctls o variables del cargador que expone el driver.
- La sección de diagnóstico, que enumera los mensajes que puede producir el driver y lo que significan.
- La sección de referencias cruzadas.
- La sección de historial, que explica cuándo apareció el driver.
- La sección de autoría.

En el caso de un driver obtenido por ingeniería inversa, la descripción debe ser honesta sobre la procedencia del driver: fue desarrollado mediante ingeniería inversa, el conjunto de funcionalidades soportadas es lo que fue alcanzable mediante ese proceso, y ciertas partes del dispositivo pueden comportarse de manera diferente a lo que el driver espera. Los operadores agradecen la documentación honesta; las afirmaciones vagas de soporte completo preparan a los usuarios para confusiones cuando encuentran comportamientos no implementados.

### 11.3 La estructura del pseudo-datasheet

El pseudo-datasheet es el documento independiente que recoge todo lo que has aprendido sobre el dispositivo, en una forma que alguien que nunca ha visto tu driver pueda leer y entender. Un pseudo-datasheet bien estructurado suele convertirse en el documento más consultado en cualquier proyecto que use el dispositivo, porque responde preguntas que el código fuente del driver no puede responder sin una lectura detenida.

Una estructura práctica de pseudo-datasheet tiene este aspecto:

```text
1. Identity
   1.1 Vendor and device IDs
   1.2 USB descriptors (if applicable)
   1.3 Class codes (if applicable)
   1.4 Subsystem identifiers (if applicable)
   1.5 Hardware revisions covered

2. Provenance
   2.1 Sources consulted
   2.2 Methodology used
   2.3 Verification status of each fact (high / medium / low)

3. Resources
   3.1 Memory regions and their sizes
   3.2 I/O ports (if applicable)
   3.3 Interrupt assignment

4. Register Map
   4.1 Register list with offsets and short descriptions
   4.2 Per-register details: size, access type, reset value, fields

5. Buffer Layouts
   5.1 Ring buffer layouts
   5.2 Descriptor formats
   5.3 Packet formats

6. Command Interface
   6.1 Command sequencing
   6.2 Command list with formats and responses
   6.3 Error reporting

7. Initialization
   7.1 Cold attach sequence
   7.2 Warm reset sequence
   7.3 Required register settings before operation

8. Operating Patterns
   8.1 Data flow
   8.2 Interrupt handling
   8.3 Status polling

9. Quirks and Errata
   9.1 Known bugs in the device
   9.2 Workarounds in the driver
   9.3 Edge cases that have not been fully characterised

10. Open Questions
    10.1 Behaviours observed but not understood
    10.2 Registers whose purpose is not yet known
    10.3 Operations not yet investigated

11. References
    11.1 Prior work consulted
    11.2 Related drivers in other operating systems
    11.3 Public documentation, if any
```

La estructura es exhaustiva y no todos los proyectos completarán cada sección. Las secciones que no sean relevantes pueden omitirse; la plantilla es una lista de comprobación de "qué valdría la pena documentar si existiera la información relevante", no una exigencia de inventar información que no existe.

Las secciones más importantes son Provenance, Register Map y Open Questions. Provenance permite a los lectores evaluar la fiabilidad del documento. Register Map es la referencia central. Open Questions indica a los futuros colaboradores dónde el trabajo necesita más atención.

### 11.4 La sección Provenance en detalle

La sección Provenance merece especial atención porque es el modo en que un pseudo-datasheet establece su credibilidad. La sección debe responder a:

- ¿Qué fuentes de información se utilizaron? (Capturas, código previo, experimentos, documentación pública.)
- ¿Qué metodología se aplicó a cada fuente? (Lectura directa, comparación de diferencias, análisis estadístico.)
- ¿Qué hechos proceden de qué fuentes?
- Para cada hecho, ¿cuál es el estado de verificación?

Una convención útil es etiquetar cada hecho sustancial del mapa de registros y demás secciones con una etiqueta corta de verificación:

- **HIGH**: confirmado por múltiples observaciones independientes y por experimento.
- **MEDIUM**: confirmado por una única fuente o un único experimento.
- **LOW**: hipótesis basada en inferencia, aún no probada directamente.
- **UNKNOWN**: mencionado por completitud, pero sin evidencia.

Los lectores pueden así ponderar cada hecho según su estado de verificación, y los colaboradores pueden priorizar dónde invertir más investigación. La convención cuesta poco de mantener y da al documento un carácter mucho más honesto que una declaración plana de hechos que trata todas las afirmaciones como iguales.

### 11.5 Los registros como tablas

El mapa de registros en sí se presenta mejor en forma de tabla. Para cada registro, la tabla debe indicar:

- Desplazamiento dentro del bloque de registros.
- Nombre simbólico (el nombre de la macro en el driver).
- Tamaño (8, 16, 32 o 64 bits, habitualmente).
- Tipo de acceso (solo lectura, lectura-escritura, solo escritura, escritura de 1 para limpiar, etc.).
- Valor de reset.
- Descripción en una línea.

Una entrada separada y más detallada para cada registro enumera los campos que lo componen. Por ejemplo:

```text
MYDEV_REG_CONTROL (offset 0x10, RW, 32 bits, reset 0x00000000)

  Bits  Name         Description
  --------------------------------------------------
  0     RESET        Write 1 to trigger reset.
  1     ENABLE       Set to enable normal operation.
  2-3   MODE         Operating mode (0=idle, 1=rx, 2=tx, 3=both).
  4     INT_ENABLE   Enable interrupts globally.
  31:5  reserved     Read as zero, write as zero.
```

Este formato de tabla es la convención de FreeBSD. Escala bien, es fácil de mantener en Markdown y es lo que los lectores esperan.

### 11.6 El pseudo-datasheet como documento vivo

El pseudo-datasheet rara vez está completo. A medida que el driver evoluciona, se descubren nuevos comportamientos, se refinan hechos anteriores y se caracterizan casos límite. El formato Markdown con seguimiento en Git permite que el documento evolucione de forma natural, con cada commit explicando qué se aprendió y cuándo.

La disciplina que sostiene esto consiste en actualizar el pseudo-datasheet primero y después actualizar el driver para que coincida. El pseudo-datasheet es la especificación; el driver es la implementación. Cuando descubras que el bit 3 de un registro tiene un propósito que antes desconocías, escribe primero la nueva descripción del bit 3 en el pseudo-datasheet, con su procedencia, y después actualiza el driver para usarlo. El orden importa porque te obliga a pensar en el cambio como un hecho sobre el dispositivo, de forma independiente a un cambio en tu código.

Con el tiempo, el pseudo-datasheet se convierte en el artefacto de confianza, y el driver pasa a ser una de las posibles implementaciones del contrato que el documento especifica. Nuevas implementaciones (en NetBSD, en Linux, en código embebido personalizado) pueden escribirse a partir del pseudo-datasheet por sí solo, sin necesidad de rederivarlo todo desde cero.

### 11.7 Control de versiones del driver

Los ejemplos trabajados del libro utilizan cadenas de versión como `v2.5-async` para marcar las iteraciones principales de un driver. Para los drivers obtenidos por ingeniería inversa, una convención útil es usar el sufijo `-rev` para indicar la naturaleza de ingeniería inversa del trabajo, con números de versión que reflejan la madurez de la implementación:

- `v0.1-rev`: driver mínimo, solo reset e identificador.
- `v0.2-rev`: ruta de lectura implementada y verificada.
- `v0.5-rev`: la mayoría de las operaciones implementadas, algunas peculiaridades comprendidas.
- `v1.0-rev`: funcionalidad completa, todas las peculiaridades conocidas gestionadas.
- `v2.1-rev`: driver estable, maduro y refactorizado, apto para uso general.

La cadena de versión puede aparecer en la página de manual, en la macro `MODULE_VERSION` y en el pseudo-datasheet. Informa a los operadores sobre el nivel de soporte que pueden esperar de cada build.

### 11.8 Cerrando la sección 11

Hemos visto cómo consolidar el trabajo de un proyecto de ingeniería inversa en un driver mantenible y un pseudo-datasheet independiente. El driver se limpia, se documenta en la página de manual y se versiona para indicar su madurez. El pseudo-datasheet captura lo aprendido sobre el dispositivo, con su procedencia, en una forma estructurada que los futuros colaboradores pueden ampliar. Juntos, los dos artefactos son lo que justifica la considerable inversión que requirió el proyecto de ingeniería inversa.

Antes de pasar a la práctica, dos secciones más breves completan el material teórico. La sección 12 revisita el marco legal y ético de la sección 1 con una mirada práctica, ofreciéndote un conjunto compacto de reglas sobre compatibilidad de licencias, restricciones contractuales, disciplina de sala limpia y actividades en puerto seguro. La sección 13 recorre después dos casos prácticos del propio árbol de FreeBSD, mostrando cómo aparecen las técnicas del capítulo en drivers que están en producción hoy en día. Tras esas dos secciones, el material restante del capítulo te proporciona laboratorios para aplicar lo aprendido, ejercicios de desafío para ampliar tu comprensión, notas de resolución de problemas para cuando las cosas van mal, y una transición orientada al futuro hacia el capítulo 37, donde veremos cómo tomar un driver como el que acabas de construir y enviarlo para su inclusión en el árbol de código fuente de FreeBSD.

## 12. Salvaguardas legales y éticas en la práctica

La sección 1 abrió este capítulo trazando el panorama legal y ético en el que se desarrolla la ingeniería inversa para FreeBSD. Ese esbozo fue deliberadamente amplio, porque tenía que introducir conceptos como el uso legítimo, la investigación de interoperabilidad y el método de sala limpia antes de que el lector hubiera visto nada del trabajo técnico que cubre el resto del capítulo. Ahora, al final de ese trabajo técnico, merece la pena hacer un segundo repaso más práctico. El objetivo de esta sección no es convertirte en abogado. Es darte un pequeño conjunto de hábitos que te protejan a ti, al proyecto y al lector de tu código de las formas predecibles en que un esfuerzo de ingeniería inversa puede salir mal. Cada hábito es concreto, cada uno puede documentarse dentro de tu pseudo-datasheet de la sección 11, y cada uno está directamente inspirado en cómo ha gestionado el árbol de FreeBSD preguntas similares en el pasado.

### 12.1 Por qué una segunda sección legal

La sección 1 respondía a la pregunta «¿está permitido?». La sección 12 responde a la pregunta «¿cómo lo hago de manera que siga estando permitido?». La distinción importa, porque muchas de las actividades que son legales en principio se vuelven arriesgadas en la práctica si se realizan sin estructura. Leer el código de otro driver para entender un chip es legal. Leer el código de otro driver y después escribir el tuyo de memoria, sin ningún documento intermedio que muestre de dónde proviene tu comprensión, tiene el mismo aspecto desde fuera, pero es mucho más difícil de defender si alguna vez se plantea una pregunta. Tratar una hoja de datos propietaria como referencia es legal. Citar pasajes largos de esa hoja de datos en los comentarios del código fuente de tu driver no lo es. La diferencia en ambos casos es el proceso, no la intención.

El resto de esta sección trabaja a través de cuatro áreas concretas: compatibilidad de licencias (qué tienes permitido copiar), restricciones contractuales (qué tienes permitido revelar), práctica de sala limpia (cómo mantener el trabajo de interoperabilidad defendible) y actividades en puerto seguro (qué está siempre permitido). Nada del contenido que sigue sustituye al asesoramiento legal en una situación específica; es en cambio la disciplina común que los desarrolladores de FreeBSD con experiencia ya siguen, escrita en un solo lugar para que un nuevo autor pueda adoptarla sin tener que reconstruirla.

### 12.2 Compatibilidad de licencias

El kernel de FreeBSD utiliza una licencia permisiva de tipo BSD. Cuando extraes conocimiento de otra base de código para tu driver, la licencia de esa otra base de código restringe lo que tienes permitido copiar, aunque no restringe lo que tienes permitido aprender. Las categorías que aparecen en la práctica son BSD, GPL, CDDL y propietario, y cada una tiene una regla diferente.

**Fuentes con licencia BSD.** Los drivers que ya residen en los árboles de OpenBSD o NetBSD, o en ports de FreeBSD anteriores del mismo dispositivo, utilizan una licencia permisiva compatible con el kernel de FreeBSD. Puedes copiar código directamente, con la atribución adecuada en el bloque de copyright. El driver inalámbrico `zyd` en `/usr/src/sys/dev/usb/wlan/if_zyd.c` es un ejemplo concreto: la cabecera del archivo preserva las etiquetas originales `$OpenBSD$` y `$NetBSD$` en la parte superior del código fuente, indicando que el código fue portado de esos árboles, y el copyright de estilo BSD de Damien Bergamini permanece intacto junto a los copyrights de los colaboradores posteriores de FreeBSD. Cuando la licencia es compatible, mover código entre árboles es una forma legítima de amortizar el trabajo en todo el ecosistema BSD, y ya forma parte de la práctica habitual de FreeBSD.

**Fuentes con licencia GPL.** Los drivers de Linux están casi siempre bajo licencia GPL. Tienes permitido leer código GPL para entender cómo funciona un dispositivo, porque leer no es copiar. No tienes permitido pegar código GPL en un driver con licencia BSD, ni en pequeñas cantidades, ni temporalmente, ni siquiera con atribución. La posición del proyecto FreeBSD es clara: el kernel no acepta código con licencia incompatible. Un driver que fuera revisado y se descubriera que contiene código GPL copiado sería rechazado, y el mantenedor perdería credibilidad en revisiones futuras. La regla es estricta porque el coste de relajarla sería la integridad de la licencia del árbol.

**Fuentes con licencia CDDL.** Algunos drivers de dispositivo en los árboles de OpenSolaris e Illumos se publican bajo la CDDL. La CDDL es copyleft a nivel de archivo, lo que significa que no puedes mezclar libremente código fuente CDDL y código fuente BSD dentro del mismo archivo. El código ZFS en FreeBSD es una conocida acomodación a esta regla: los archivos CDDL se mantienen en su propio directorio bajo `/usr/src/sys/contrib/openzfs/`, su cabecera de licencia se preserva, y el código de pegamento con licencia BSD que los rodea reside en archivos separados. Para un driver de dispositivo, esa estructura rara vez constituye un flujo de trabajo razonable, por lo que la regla más segura es que el código CDDL es de mirar pero no copiar, del mismo modo que el código GPL.

**Fuentes propietarias.** Los SDK de los fabricantes, los drivers de referencia y el código de muestra suelen distribuirse bajo una licencia propietaria que prohíbe la redistribución. Incluso cuando el fabricante te anima a utilizar el código como referencia, ese permiso no es transferible al árbol de código fuente de FreeBSD, porque el fabricante no puede hablar por cada usuario final de FreeBSD. Leer un driver propietario para entender un dispositivo es generalmente aceptable en una medida limitada, pero no puedes pegar fragmentos de él, y en muchas jurisdicciones tampoco puedes citarlo extensamente.

La regla práctica es sencilla: trata todo lo que no sea BSD puro como de solo lectura. Si quieres bytes en tu driver, esos bytes deben provenir de tu propia escritura o de una fuente cuya licencia el proyecto FreeBSD ya acepta. Cuando haces explícita esta regla en tu pseudo-datasheet, escribiendo junto a cada descripción de registro dónde se observó la información, creas un registro que seguirá siendo útil años después, cuando un nuevo mantenedor necesite saber cómo llegó el archivo a tener el aspecto que tiene.

### 12.3 Restricciones contractuales

Los contratos pueden vincularte en situaciones en las que la ley de derechos de autor no lo hace. Los tres tipos de contrato que aparecen en el trabajo de ingeniería inversa son los acuerdos de no divulgación, los acuerdos de licencia de usuario final y las licencias de firmware. Cada uno de ellos restringe una parte diferente del flujo de trabajo.

**Acuerdos de no divulgación.** Algunos fabricantes comparten documentación detallada a cambio de un NDA firmado. La documentación suele ser excelente, pero el NDA generalmente prohíbe la publicación de la información, a veces indefinidamente, y a veces con indemnizaciones pactadas. Firmar un NDA sobre un dispositivo y después escribir un driver de código abierto para ese mismo dispositivo es un campo de minas legal: cada registro que documentes en un pseudo-datasheet, cada nombre de campo, cada valor, podría ser impugnado más adelante como una divulgación. La opción más segura es casi siempre rechazar el NDA y trabajar con información públicamente observable. Si ya has firmado uno y después quieres contribuir con un driver abierto, la opción limpia es apartarte del trabajo de observación y dejar que un segundo autor construya el pseudo-datasheet desde cero, utilizando únicamente fuentes que nunca tocaste bajo el acuerdo.

**Acuerdos de licencia de usuario final.** Muchos drivers y SDK suministrados por fabricantes incluyen un EULA que prohíbe explícitamente la ingeniería inversa. La aplicabilidad de tales cláusulas varía según la jurisdicción, pero el camino más seguro es evitar hacer clic en «Acepto» desde el principio. Si solo has tocado el driver del fabricante como un binario incluido en una imagen de instalación, sin aceptar un acuerdo en línea, tu posición es más sólida que si te registraste en un programa de desarrolladores y descargaste el SDK bajo un acuerdo que prohibía el desensamblado. Registra qué materiales del fabricante consultaste y bajo qué condiciones. Ese registro forma parte del campo de procedencia de tu pseudo-datasheet.

**Licencias de firmware.** Muchos dispositivos modernos necesitan un blob de firmware binario que el driver carga en el momento del attach. El fabricante suele distribuir ese blob bajo una licencia de redistribución más permisiva que el código del driver circundante, pero que aún no es totalmente libre. El árbol `ports` de FreeBSD tiene un patrón bien establecido de empaquetar blobs de firmware en ports dedicados `-firmware-kmod` para que el código del kernel permanezca BSD mientras el blob conserva su licencia de fabricante. No necesitas hacer ingeniería inversa del firmware en sí; necesitas entender cómo el driver lo entrega al dispositivo. El marco legal del firmware es independiente del marco legal del driver, y ambos deben cumplirse de forma independiente.

### 12.4 Práctica de sala limpia

Leer un driver con licencia GPL para entender qué hace un dispositivo y después escribir código BSD que haga lo mismo es una técnica de interoperabilidad reconocida. Los tribunales de Estados Unidos la han avalado en casos como Sega v. Accolade y Sony v. Connectix, y el artículo 6 de la Directiva sobre el software de la Unión Europea (2009/24/CE) contempla expresamente la ingeniería inversa con fines de interoperabilidad. Sin embargo, la protección legal depende de cómo se realice el trabajo, no solo de lo que se hace. Un driver que tenga el mismo aspecto que el original de Linux línea por línea no estará protegido por la defensa de sala limpia, porque el expediente demostrará que no existió ninguna sala limpia.

Una buena práctica de sala limpia tiene dos elementos: separación estructural y documentación estructural.

La separación estructural significa que quien lee el driver de otra plataforma y quien escribe el driver de FreeBSD realizan actividades distintas. En un equipo de dos personas, son literalmente personas diferentes. En un proyecto en solitario, son sesiones de trabajo distintas con artefactos distintos. El resultado de quien lee es un pseudo-datasheet: un documento en lenguaje llano que describe registros, campos de bits, secuencias de comandos y particularidades, sin ninguna referencia a los identificadores específicos que utiliza el otro driver. La entrada de quien escribe es ese pseudo-datasheet, junto con las cabeceras de FreeBSD y los ejemplos de drivers. Quien escribe nunca abre el driver de la otra plataforma mientras se está desarrollando el código de FreeBSD.

La documentación estructural significa que el pseudo-datasheet registra de dónde proviene cada dato. La descripción de un registro podría llevar la anotación "observado en el bus con `usbdump(8)` el 2026-02-14", o "deducido de la sección 3.4 del datasheet del fabricante", o "deducido del driver de Linux, reescrito en prosa". Esa anotación es el registro que, si alguna vez fuera necesario, respaldaría la afirmación de que tu driver se escribió a partir de la comprensión del funcionamiento y no de la copia. Algunos casos de patentes y derechos de autor han girado precisamente en torno a la calidad de este tipo de registro, y mantenerlo no es paranoia; es la misma disciplina que da rigor al resto de tu trabajo de ingeniería.

En la práctica, el coste en tiempo de la disciplina de sala limpia es pequeño una vez que el hábito del pseudo-datasheet de la Sección 11 ya forma parte de tu flujo de trabajo. El valor legal es grande, porque te mueve de "espero que esto no sea un problema" a "puedo demostrar cómo se hizo".

### 12.5 Puerto seguro

Conviene saber qué está siempre a salvo, para poder afrontar los casos dudosos a través de la estructura de esta sección sin cuestionarte cada tecla que pulsas. Las siguientes actividades son defendibles en cualquier jurisdicción que importe al proyecto FreeBSD, y puedes llevarlas a cabo sin dudarlo.

**Leer código es siempre seguro.** Sea cual sea la licencia que porte el driver de otra plataforma, leerlo para comprender cómo funciona un dispositivo no constituye una infracción de copyright. A veces se denomina uso justo y otras veces investigación de interoperabilidad; la etiqueta depende de la jurisdicción, pero el principio es sólido en todos los sistemas jurídicos que el proyecto FreeBSD tiene probabilidades de encontrar.

**Observar tu propio hardware es siempre seguro.** Ejecutar un dispositivo de tu propiedad en una máquina de tu propiedad y registrar lo que ocurre en el bus no es impugnable bajo la ley de copyright, contratos ni secretos comerciales. Los registros que obtienes con `usbdump(8)`, con `pciconf(8)`, con sondas JTAG y con analizadores lógicos son obra tuya, y puedes describirlos y publicarlos libremente.

**Escribir a partir de la comprensión es siempre seguro.** Si puedes describir el dispositivo con tus propias palabras, en un pseudo-datasheet, puedes entonces escribir un driver de FreeBSD a partir de ese documento sin restricción alguna. Lo escrito es tuyo, derivado de los hechos sobre el dispositivo y no de la expresión de otro autor.

**Publicar información de interoperabilidad es generalmente seguro.** Documentar el mapa de registros, una secuencia de comandos o un protocolo de bus es publicar hechos sobre un dispositivo, no publicar el código de otra persona. Incluso cuando un fabricante no ve con buenos ojos esa publicación, rara vez tiene base legal para impedirla, y los tribunales tanto de Estados Unidos como de la Unión Europea han apoyado de manera consistente la investigación de interoperabilidad.

Las actividades que quedan fuera de esta lista no son automáticamente inseguras, pero merecen una reflexión deliberada. Ante la duda, amplía tu sala limpia, refuerza tu documentación y pide una segunda opinión en la lista de correo `freebsd-hackers` o en un canal de chat del proyecto antes de confirmar tus cambios. Los mantenedores han visto estas preguntas muchas veces, y el coste de preguntar es bajo.

### 12.6 Cerrando la sección 12

Hemos dado un segundo repaso práctico al marco legal y ético presentado en la sección 1. La compatibilidad de licencias dicta qué puedes copiar; las restricciones contractuales dictan qué puedes revelar; la práctica de sala limpia mantiene la investigación de interoperabilidad en terreno defendible; y una breve lista de actividades siempre seguras te da margen para trabajar sin preocupación constante. Los hábitos son sencillos de adoptar una vez que el pseudo-datasheet de la sección 11 forma ya parte de tu flujo de trabajo, porque el propio pseudo-datasheet es la pista de auditoría de la sala limpia.

Con esta base establecida, estamos listos para ver cómo el propio árbol de FreeBSD documenta el trabajo de ingeniería inversa. La siguiente sección presenta dos casos de estudio trabajados, ambos extraídos directamente del código fuente actual, donde los autores de los drivers registraron su razonamiento en comentarios que aún hoy son legibles.

## 13. Casos de estudio del árbol de FreeBSD

Ya hemos cubierto las técnicas, la disciplina de documentación y el marco legal. Ha llegado el momento de observar dos drivers reales que fueron escritos bajo exactamente estas restricciones y ver cómo sus autores abordaron el trabajo. Ambos drivers están en el árbol de FreeBSD 14.3 ahora mismo. Ambos tienen comentarios de cabecera y notas en línea que registran, con las propias palabras del autor, lo que el datasheet no decía y lo que el driver hace al respecto. Leer esos comentarios te ofrece una visión directa de la disciplina de ingeniería inversa tal como aparece en código de producción, sin retoques y sin pulido retrospectivo.

### 13.1 Cómo leer estos casos de estudio

Para cada driver examinaremos cuatro aspectos. En primer lugar, una breve descripción del dispositivo y su historia, para que sepas qué tipo de hardware está implicado y aproximadamente cuándo se realizó el trabajo. En segundo lugar, el enfoque que utilizó el autor para sortear la documentación incompleta, incluidas las fuentes consultadas y las observaciones realizadas. En tercer lugar, el código específico que codifica el hallazgo, con rutas de archivo exactas, nombres de funciones e identificadores de registros o constantes que el driver utiliza hoy. En cuarto lugar, el contexto ético y legal en el que se realizó el trabajo, incluida la manera en que la licencia de cada fuente determinó lo que el autor podía escribir. Un párrafo de cierre traduce después el método histórico a su forma moderna, para que puedas ver cómo abordarías el mismo problema hoy con las técnicas presentadas anteriormente en este capítulo.

Nada de la historia que sigue es especulativo. Cada afirmación está anclada en un archivo que puedes abrir en tu propio sistema FreeBSD, y cada dato sobre el código es directamente observable en el código fuente actual. Si algo aquí queda desfasado en una versión futura, los propios archivos seguirán siendo la fuente de verdad, y el método para leerlos seguirá siendo válido.

### 13.2 Caso de estudio 1: el driver `umcs` y su GPIO no documentado

**Dispositivo e historia.** El MosChip MCS7820 y el MCS7840 son chips puente USB a serie que aparecen en adaptadores RS-232 multipuerto de bajo coste. El MCS7820 es una variante de dos puertos, el MCS7840 es una variante de cuatro puertos, y la interfaz USB es eléctricamente idéntica en ambos casos. El driver de FreeBSD, `umcs`, fue escrito por Lev Serebryakov en 2010 y vive actualmente en `/usr/src/sys/dev/usb/serial/umcs.c`. El archivo de cabecera asociado, `/usr/src/sys/dev/usb/serial/umcs.h`, detalla el mapa de registros.

**Enfoque.** El comentario de cabecera al inicio de `umcs.c` es inusualmente franco respecto a la situación de la documentación. El autor escribe que el driver admite los modelos mos7820 y mos7840, y señala directamente que el datasheet público no contiene información de programación completa para el chip. Un datasheet completo, distribuido de forma restringida por el soporte técnico de MosChip, cubrió algunas de las lagunas, y un driver de referencia suministrado por el fabricante cubrió el resto. La tarea del autor fue escribir un driver BSD original que se comportara de la manera que la información confirmada indicaba, utilizando el driver del fabricante como verificación observacional y no como fuente a copiar.

El lugar más claro para ver esta disciplina en acción es la detección del número de puertos dentro de la rutina attach. Un programa que gobierna un chip USB a serie debe saber si el chip tiene dos puertos o cuatro, porque los nodos de dispositivo visibles para el usuario y las estructuras de datos internas dependen de ese recuento. El datasheet prescribe un método, el driver del fabricante usa otro, y los experimentos sobre hardware real demuestran que el método del datasheet es poco fiable.

**Código.** En `/usr/src/sys/dev/usb/serial/umcs.c`, la función `umcs7840_attach` realiza la detección. El comentario en línea registra el problema en texto plano, y el código registra la solución elegida. El fragmento relevante es el siguiente:

```c
/*
 * Documentation (full datasheet) says, that number of ports
 * is set as MCS7840_DEV_MODE_SELECT24S bit in MODE R/Only
 * register. But vendor driver uses these undocumented
 * register & bit. Experiments show, that MODE register can
 * have `0' (4 ports) bit on 2-port device, so use vendor
 * driver's way.
 */
umcs7840_get_reg_sync(sc, MCS7840_DEV_REG_GPIO, &data);
if (data & MCS7840_DEV_GPIO_4PORTS) {
```

El registro implicado es `MCS7840_DEV_REG_GPIO`, definido en el desplazamiento 0x07 en `/usr/src/sys/dev/usb/serial/umcs.h`. El archivo de cabecera es igualmente franco sobre su condición. El bloque de registros en su conjunto está anotado con una nota que explica que los registros solo están documentados en el datasheet completo, que puede solicitarse al soporte técnico de MosChip, y el registro GPIO en concreto está comentado como aquel que contiene los bits GPIO_0 y GPIO_1 que no aparecen documentados en el datasheet público. Una nota más extensa más adelante en el archivo de cabecera explica que `GPIO_0` debe estar conectado a tierra en placas de dos puertos y debe llevarse a nivel alto en placas de cuatro puertos, y que esta convención la imponen los diseñadores de la placa y no el propio chip. El indicador de un solo bit `MCS7840_DEV_GPIO_4PORTS`, definido como `0x01`, es lo que la rutina attach compara con el valor devuelto por `umcs7840_get_reg_sync`.

Lo que hace de este un buen caso de estudio es que el código registra la historia. Un lector que encuentre el driver quince años después de que fuera escrito puede reconstruir el razonamiento: el datasheet decía una cosa, el driver del fabricante hacía otra, se probó el hardware real y el método elegido queda explicado en el comentario. Nadie tiene que redescubrir el mismo callejón sin salida.

**Contexto ético y legal.** El autor tenía a su disposición dos clases de fuentes: el datasheet completo restringido, que MosChip distribuye bajo petición, y el driver de referencia del fabricante, que se incluye con el kit de evaluación del chip. Ambas son propietarias en el sentido de que no son redistribuibles libremente, y ninguna puede copiarse en un driver de código abierto. Lo que sí puedes hacer es aprender de ellas y luego escribir tu propio código a partir de la información que contienen. El comentario de `umcs` hace exactamente eso. Nombra el comportamiento que exhibe el driver del fabricante, nombra el experimento que confirmó ese comportamiento en hardware real, y el código que produce es prosa original con licencia BSD. El bloque de copyright al inicio del archivo fuente identifica a Lev Serebryakov como único autor. Este es un resultado de sala limpia ejemplar: el driver se beneficia de la información del fabricante sin importar el código del fabricante.

**Replicación moderna.** Si escribieras `umcs` hoy, el flujo de trabajo sería el mismo, con mejores herramientas. Capturarías una traza de `usbdump(8)` de la secuencia de attach del driver del fabricante para ver la lectura GPIO exacta, ejecutarías la misma lectura en tu propio hardware en placas conocidas de dos y cuatro puertos, y registrarías la discrepancia entre el datasheet y el comportamiento observado en tu pseudo-datasheet con una línea de procedencia clara para cada fuente. El driver implementaría entonces el método observado, con un comentario idéntico en espíritu al que escribió Lev Serebryakov. La técnica no ha cambiado; las herramientas a su alrededor han mejorado.

### 13.3 Caso de estudio 2: el driver `axe` y la inicialización IPG que faltaba

**Dispositivo e historia.** El ASIX AX88172 es un chip adaptador Ethernet USB 1.1, y el AX88178 y el AX88772 son sus descendientes USB 2.0. Los dongles Ethernet económicos basados en estos componentes son habituales desde principios de los años 2000, y el driver de FreeBSD, `axe`, vive en `/usr/src/sys/dev/usb/net/if_axe.c`. El código original fue escrito por Bill Paul entre 1997 y 2003, y el soporte para el AX88178 y el AX88772 fue retroportado desde OpenBSD por J. R. Oldroyd en 2007. El archivo de cabecera de registros vive junto al driver en `/usr/src/sys/dev/usb/net/if_axereg.h`.

**Enfoque.** El bloque de comentarios al inicio del archivo en `/usr/src/sys/dev/usb/net/if_axe.c` dice, en parte, que hay información ausente del manual del chip que el driver debe conocer para que el chip funcione en absoluto. El autor enumera dos hechos específicos. Un bit debe establecerse en el registro de control RX o el chip no recibirá ningún paquete. Los tres registros de inter-packet-gap deben inicializarse todos, o el chip no enviará ningún paquete. Ninguno de estos requisitos aparece en el datasheet público, y ambos se determinaron leyendo el driver de Linux del fabricante y observando el chip en hardware real.

La historia de los registros IPG es especialmente clara. El inter-packet gap es un concepto Ethernet estándar: el transmisor debe esperar un número mínimo de tiempos de bit entre tramas, y el número exacto depende de la velocidad del enlace. Un diseñador de silicio tiene varias formas de exponer el IPG al software, y el AX88172 eligió exponer tres registros distintos que el driver debe escribir durante la inicialización. El datasheet nombra los registros pero no dice nada sobre la necesidad de programarlos, de modo que un driver ingenuo que siguiera únicamente el datasheet los dejaría en sus valores de reset y descubriría que el chip, misteriosamente, se niega a transmitir.

**Código.** La secuencia de inicialización aparece dentro de la función `axe_init` en `/usr/src/sys/dev/usb/net/if_axe.c`. Un pequeño auxiliar llamado `axe_cmd` proporciona la forma estándar de emitir una solicitud de control USB específica del fabricante, y `axe_init` lo llama una vez por cada escritura de IPG. El fragmento relevante es el siguiente:

```c
if (AXE_IS_178_FAMILY(sc)) {
    axe_cmd(sc, AXE_178_CMD_WRITE_NODEID, 0, 0, if_getlladdr(ifp));
    axe_cmd(sc, AXE_178_CMD_WRITE_IPG012, sc->sc_ipgs[2],
        (sc->sc_ipgs[1] << 8) | (sc->sc_ipgs[0]), NULL);
} else {
    axe_cmd(sc, AXE_172_CMD_WRITE_NODEID, 0, 0, if_getlladdr(ifp));
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG0, 0, sc->sc_ipgs[0], NULL);
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG1, 0, sc->sc_ipgs[1], NULL);
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG2, 0, sc->sc_ipgs[2], NULL);
}
```

Las dos ramas revelan un segundo dato del proceso de ingeniería inversa que no aparece en el datasheet. En el AX88172 más antiguo, cada uno de los tres valores IPG se programa con un comando independiente: AXE_172_CMD_WRITE_IPG0, AXE_172_CMD_WRITE_IPG1 y AXE_172_CMD_WRITE_IPG2, definidos en `/usr/src/sys/dev/usb/net/if_axereg.h`. En los modelos más recientes AX88178 y AX88772, un único comando escribe los tres a la vez empaquetándolos en la misma petición de control: `AXE_178_CMD_WRITE_IPG012`. Los dos opcodes tienen el mismo valor numérico; el AX88178 reutilizó la ranura de opcode que el AX88172 empleaba para escribir un único registro IPG y amplió su semántica para cubrir los tres a la vez. Un driver que tratara las dos familias de chips como intercambiables corrompería la programación de los IPG en una de ellas. La única forma de distinguir las familias es el macro `AXE_IS_178_FAMILY(sc)`, y la necesidad de ese macro es en sí misma un hallazgo del trabajo de ingeniería inversa.

**Contexto ético y legal.** El driver axe tiene una larga historia, y su procedencia queda registrada en el bloque de derechos de autor. Bill Paul escribió el driver original para el AX88172, y los comentarios muestran que su información procedía de una combinación del datasheet público y de trabajo de observación. El soporte para el AX88178 de J. R. Oldroyd fue portado desde el driver hermano de OpenBSD, publicado bajo licencia BSD y directamente compatible con FreeBSD. Ese port fue legalmente sencillo: el código provenía de un árbol de código fuente con licencia permisiva y el bloque de derechos de autor se preservó durante el proceso. Las notas de ingeniería inversa sobre IPG y RX-control viajaron junto con el código, de modo que el hallazgo quedó documentado para siempre en la cabecera del driver.

Lo que no ocurrió es tan informativo como lo que sí ocurrió. El driver axe no importa código del driver `asix_usb` de Linux, aunque ese driver ya existía cuando Bill Paul estaba escribiendo el equivalente de FreeBSD. Linux usa la GPL, y pegar código de él habría hecho que axe no pudiera distribuirse conforme a las reglas de licencia del kernel. Leer ese código para entenderlo es exactamente lo que hizo Bill Paul, y el driver de FreeBSD es obra suya de principio a fin.

**Replicación moderna.** Si escribieras axe hoy, empezarías cargando el driver de Linux en una máquina de pruebas con Linux y ejecutando `usbdump(8)` en una máquina FreeBSD conectada al mismo hub USB, de modo que pudieras capturar la secuencia de attach del driver del fabricante sin necesidad de leer ni una sola línea de código GPL. La salida de `usbdump(8)` mostraría las tres escrituras IPG directamente, porque viajan por el cable como transferencias de control USB visibles. Registrarías la observación en tu pseudo-datasheet, citarías el archivo de captura de paquetes como referencia de procedencia e implementarías las escrituras en tu propio driver. El comentario encima de `axe_init` sería muy parecido al que escribió Bill Paul en su momento.

### 13.4 Lecciones compartidas

Leer los dos drivers juntos saca a la luz una serie de lecciones que merece la pena nombrar de forma explícita.

**El comentario es el registro de auditoría.** Tanto en `umcs` como en `axe`, los comentarios de cabecera y los comentarios en línea son el lugar donde se preserva la historia de la ingeniería inversa. Sin esos comentarios, un futuro mantenedor que se encontrara frente a `MCS7840_DEV_REG_GPIO` o a `AXE_178_CMD_WRITE_IPG012` no tendría manera de saber de dónde procedía la información ni por qué el driver hace lo que hace. En ambos casos, el autor se tomó el tiempo extra de escribir el razonamiento, y el resultado es un driver que sigue siendo mantenible quince o veinte años después de haber sido escrito. La pseudo-hoja de datos del autor, en cada caso, quedó absorbida en parte en los comentarios y preservada en parte en la estructura del archivo de cabecera. Esa absorción es lo que hace que el código siga siendo legible hoy en día.

**La observación prevalece sobre la documentación.** Ambos drivers confían en la observación por encima de la hoja de datos cuando las dos discrepan. `umcs` confía en la lectura de GPIO verificada experimentalmente por encima del método del registro MODE indicado en la hoja de datos, y `axe` confía en la observación de que el chip se niega a transmitir sin la programación de IPG por encima del silencio de la hoja de datos al respecto. Esto no supone un rechazo a la documentación; ambos autores leyeron las hojas de datos con detenimiento. Es el reconocimiento de que una hoja de datos es una descripción del comportamiento previsto, y el comportamiento real del chip es lo que el driver tiene que reproducir.

**La disciplina en las licencias no es algo secundario.** Ambos drivers respetan la licencia de las fuentes de las que se nutrieron. `umcs` utiliza un driver de referencia propietario como verificación observacional sin copiar de él. `axe` importa el soporte para AX88178 y AX88772 desde OpenBSD, que es compatible en cuanto a licencia, y se mantiene alejado del driver Linux `asix_usb`, que no lo es. Ninguno de los dos drivers incluye código que el proyecto FreeBSD hubiera tenido que eliminar durante la revisión, y ninguno necesitó la revisión de un abogado antes de que se integrara. Los hábitos de la Sección 12 ya formaban parte de la manera de trabajar de los autores.

**El trabajo es reproducible.** Todo lo que ambos drivers hicieron puede reproducirse hoy con herramientas modernas. `usbdump(8)`, `usbconfig(8)` y las técnicas de trazado en el espacio del bus descritas anteriormente en el capítulo te darían la misma información que los autores originales tuvieron que recopilar a mano, y el hábito de la pseudo-hoja de datos de la Sección 11 te proporcionaría un registro de auditoría más claro que un conjunto de comentarios en línea. Los drivers que escribas hoy para hardware poco conocido estarán mejor documentados que los drivers de 2007 y 2010, porque las técnicas han madurado. El espíritu del trabajo, sin embargo, es el mismo.

### 13.5 Cerrando la sección 13

Dos casos de estudio han mostrado cómo es un esfuerzo de ingeniería inversa disciplinado cuando ha sido integrado en el árbol de FreeBSD. En ambos casos, el autor identificó una laguna concreta en la documentación pública, confirmó el comportamiento correcto mediante observación, registró el hallazgo en un comentario que aún es legible hoy en día, y escribió código original bajo una licencia compatible con FreeBSD. Los drivers funcionan, el código es mantenible y el registro legal está limpio.

Con el marco legal de la Sección 12 y los ejemplos trabajados de la Sección 13, ahora dispones tanto de los principios como de un conjunto de precedentes en los que apoyarte. Las secciones restantes del capítulo te ofrecen práctica con las técnicas que ilustran los casos de estudio, ejercicios de desafío para profundizar en tu comprensión, notas de resolución de problemas para las formas más comunes en que el trabajo puede ir mal, y un puente de cierre hacia el Capítulo 37.

## Laboratorios prácticos

Los laboratorios de este capítulo te ofrecen práctica segura y repetible con las técnicas que el capítulo ha cubierto. Ninguno de ellos toca hardware desconocido real de una manera que pueda dañar nada; el «dispositivo desconocido» en los laboratorios es un mock de software o una región de memoria inofensiva que tú controlas. Trata cada laboratorio como una oportunidad de interiorizar una técnica específica antes de añadirla a tu repertorio.

El código complementario se encuentra en `examples/part-07/ch36-reverse-engineering/`. Cada subcarpeta de laboratorio tiene su propio `README.md` con instrucciones paso a paso, y el código está organizado de manera que puedas construir y ejecutar cada laboratorio de forma independiente de los demás. Como en capítulos anteriores, escribe el código tú mismo la primera vez que trabajes en un laboratorio; los archivos complementarios están ahí como referencia y como versión verificada con la que comparar.

### Laboratorio 1: Identificar un dispositivo y volcar descriptores

Este laboratorio es el ejercicio de ingeniería inversa más sencillo posible. Utilizarás `pciconf(8)` y `usbconfig(8)` para enumerar todos los dispositivos de tu sistema FreeBSD y volcar los descriptores estáticos de un dispositivo de tu elección. El resultado del laboratorio es un pequeño archivo de texto que registra, para un dispositivo concreto, todos los datos públicos que el kernel puede ofrecerte sobre él.

Pasos:

1. Ejecuta `sudo pciconf -lvc` y guarda la salida. Observa cualquier dispositivo que aparezca como `noneN@...`, lo que indica que ningún driver incluido en el árbol lo ha reclamado.
2. Ejecuta `sudo usbconfig` y guarda la salida. Elige un dispositivo USB que conozcas bien (una memoria USB, por ejemplo) y que sea físicamente tuyo.
3. Ejecuta `sudo usbconfig -d ugen0.X dump_device_desc` y `sudo usbconfig -d ugen0.X dump_curr_config_desc` para el dispositivo elegido, donde `ugen0.X` es el identificador del dispositivo en `usbconfig`.
4. Abre la salida capturada e identifica cada campo: bDeviceClass, idVendor, idProduct, bMaxPacketSize0, bNumConfigurations, los descriptores de endpoints, etcétera.
5. Escribe un resumen de una página que identifique la identidad del dispositivo, su clase, sus endpoints y cualquier característica destacable.

El laboratorio es intencionadamente sencillo. El objetivo es interiorizar la forma de la captura de identidad estática antes de aplicarla a un dispositivo cuya identidad sea genuinamente desconocida.

### Laboratorio 2: Capturar una secuencia de inicialización USB

Este laboratorio pasa de la identidad estática al comportamiento dinámico. Utilizarás `usbdump(8)` para capturar la secuencia de inicialización de un dispositivo USB, guardar la captura y explorarla en Wireshark.

Pasos:

1. Identifica un dispositivo USB que puedas conectar y desconectar libremente (una memoria USB es una buena elección, porque puedes desenchufar y volver a enchufar sin consecuencias).
2. Ejecuta `sudo usbdump -i usbus0 -w stick-init.pcap` para empezar a capturar en el bus USB al que está conectado el dispositivo. Usa el número de bus correcto para tu sistema; `usbconfig` te indicará en qué bus está el dispositivo.
3. Con `usbdump` en ejecución, conecta el dispositivo. Espera a que el kernel lo reconozca. Luego desconéctalo.
4. Detén `usbdump` con Control-C.
5. Abre el archivo pcap resultante en Wireshark. Deberías ver una serie de transferencias USB correspondientes a la enumeración: las peticiones GET_DESCRIPTOR para el dispositivo, los descriptores de configuración y de cadena, la petición SET_ADDRESS, la petición SET_CONFIGURATION, etcétera.
6. Identifica cada transferencia y anota qué hace. Compara el descriptor de dispositivo capturado con la salida de `usbconfig dump_device_desc` del Laboratorio 1; deberían coincidir campo por campo.

Este laboratorio es la base de toda la ingeniería inversa USB. Cualquier dispositivo USB que investigues pasará por una secuencia de enumeración en el momento de la conexión, y ser capaz de leer esa secuencia de enumeración es el punto de entrada para comprender qué está haciendo el dispositivo.

### Laboratorio 3: Construir un módulo de sondeo de registros seguro

Este laboratorio presenta la herramienta del lado del kernel que utilizarás para el trabajo con el mapa de registros en proyectos reales. Construirás un pequeño módulo del kernel llamado `regprobe` que asigna un recurso de memoria en un dispositivo de tu elección y expone un sysctl que vuelca el contenido del recurso. No se realizan escrituras.

Pasos:

1. Construye el módulo `regprobe` desde `examples/part-07/ch36-reverse-engineering/lab03-register-map/`.
2. Identifica un dispositivo PCI en tu sistema que no necesites para nada más. Una tarjeta de red de repuesto o cualquier dispositivo PCI sin usar es ideal. (No uses el dispositivo que respalda tu consola o tu almacenamiento.)
3. Desconecta el driver incluido en el árbol del dispositivo con `sudo devctl detach <device>`.
4. Sigue los procedimientos operativos del README del laboratorio para adjuntar `regprobe` al dispositivo.
5. Lee el sysctl de volcado varias veces, con unos segundos entre cada lectura. Compara los volcados e identifica qué palabras son estables y cuáles están cambiando.
6. Vuelve a conectar el driver incluido en el árbol con `sudo devctl attach <device>`.

El laboratorio demuestra dos cosas: que un sondeo de solo lectura es seguro, y que incluso un sondeo de solo lectura puede revelar una estructura interesante en el espacio de registros de un dispositivo. Las palabras dinámicas son probablemente contadores o registros de estado; las palabras estables son probablemente registros de configuración o identificadores.

### Laboratorio 4: Escribir y controlar un dispositivo simulado

Este laboratorio presenta la vertiente de simulación del flujo de trabajo. Construirás un pequeño módulo del kernel que simula un pequeño dispositivo de «comando y estado» completamente en software, y un pequeño programa de prueba en espacio de usuario que controla el dispositivo simulado a través de sus sysctls.

Pasos:

1. Construye el módulo `mockdev` desde `examples/part-07/ch36-reverse-engineering/lab04-mock-device/`.
2. Carga el módulo con `sudo kldload ./mockdev.ko`.
3. Utiliza el programa de prueba (también en la carpeta del laboratorio) para enviar algunos comandos al dispositivo simulado.
4. Observa el log del kernel para ver cómo se procesan los comandos y las actualizaciones de estado simuladas.
5. Modifica el dispositivo simulado para introducir un error deliberado: haz que informe de un fallo para un código de comando específico. Verifica que el programa de prueba detecta el fallo correctamente.
6. Modifica el programa de prueba para que utilice un watchdog: si el dispositivo simulado no completa un comando dentro de un tiempo de espera, el programa debería reportar un fallo en lugar de bloquearse.

El laboratorio enseña la estructura de las pruebas basadas en mock en un entorno donde el mock es lo suficientemente pequeño como para entenderlo completamente. En proyectos reales, los mocks se vuelven más complejos, pero la estructura es la misma.

### Laboratorio 5: Construir un envoltorio de sondeo seguro

Este laboratorio consolida las técnicas de seguridad de la Sección 10. Construirás una pequeña biblioteca de envoltorios de sondeo seguros (lectura-modificación-escritura-verificación, operaciones protegidas por tiempo de espera, recuperación automática ante fallos) y los utilizarás para realizar un experimento en el dispositivo simulado del Laboratorio 4.

Pasos:

1. Abre la carpeta del laboratorio `safeprobe`.
2. Lee la biblioteca de envoltorios detenidamente. Observa cómo cada operación está protegida por un tiempo de espera, cómo cada escritura en un registro va seguida de una relectura, y cómo los fallos se reportan con claridad.
3. Construye la biblioteca de envoltorios y el driver de ejemplo que la utiliza.
4. Carga el dispositivo simulado del Laboratorio 4.
5. Ejecuta el driver de ejemplo contra el dispositivo simulado. Observa cómo los envoltorios reportan las operaciones que realizan.
6. Modifica el dispositivo simulado para inyectar un fallo (un valor de relectura inesperado, por ejemplo). Verifica que los envoltorios detectan el fallo y lo reportan claramente, en lugar de permitir que el driver continúe con un estado corrupto.

Este laboratorio enseña los envoltorios de seguridad como una herramienta que puedes aplicar a tus propios drivers. El coste de utilizarlos es pequeño; la información que producen cuando algo va mal es grande.

### Laboratorio 6: Escribir una pseudo-hoja de datos

Este laboratorio es el equivalente en documentación de los laboratorios de técnicas. Tomarás el dispositivo simulado del Laboratorio 4 y escribirás una pseudo-hoja de datos para él, siguiendo la estructura de la Sección 11.

Pasos:

1. Abre la plantilla de pseudo-hoja de datos en `examples/part-07/ch36-reverse-engineering/lab06-pseudo-datasheet/`.
2. Lee la plantilla detenidamente y comprende la estructura.
3. Examina el código fuente del dispositivo simulado para conocer su distribución de registros, su conjunto de comandos y su comportamiento.
4. Rellena la plantilla con la información del dispositivo simulado, siguiendo la estructura: identidad, procedencia, recursos, mapa de registros, distribución de buffers, interfaz de comandos, inicialización, patrones de operación, peculiaridades, preguntas abiertas, referencias.
5. Guarda el resultado como un archivo Markdown junto al código fuente del dispositivo simulado.

El dispositivo simulado es lo suficientemente pequeño como para que el pseudo-datasheet pueda completarse en una hora o dos. El ejercicio te enseña la estructura del documento y el nivel de detalle que merece cada sección. Cuando más adelante redactes un pseudo-datasheet para un dispositivo real, ya sabrás cómo organizar la información, aunque la información en sí sea mucho más extensa.

## Ejercicios de desafío

Los ejercicios de desafío amplían las técnicas de los laboratorios hacia investigaciones más abiertas y de mayor envergadura. Ninguno requiere hardware exótico; todos utilizan dispositivos reales comunes o los dispositivos simulados de los laboratorios. Tómate tu tiempo con ellos. Los laboratorios te han dado las técnicas; los desafíos te dan la práctica de aplicarlas en entornos menos guiados.

### Desafío 1: Analiza una captura desconocida

Los archivos complementarios incluyen un pequeño conjunto de capturas pcap del tráfico USB de un dispositivo desconocido. Abre las capturas en Wireshark. Solo mediante análisis, identifica:

- Los identificadores de fabricante y producto del dispositivo.
- La clase del dispositivo (HID, almacenamiento masivo, específica del fabricante, etc.).
- La distribución de endpoints del dispositivo (qué endpoints, de qué tipos y en qué direcciones).
- La forma general del flujo de datos del dispositivo (¿parece utilizar transferencias bulk, transferencias de control o transferencias de interrupción?).
- Cualquier patrón evidente en los datos (transferencias periódicas, comando y respuesta, streaming continuo).

Escribe un resumen de un párrafo sobre qué tipo de dispositivo generó esas capturas y qué protocolo parece utilizar. Las capturas incluyen la respuesta en un archivo que no debes consultar hasta haber escrito tu propia respuesta; compara y reflexiona.

### Desafío 2: Amplía el dispositivo simulado

Toma el dispositivo simulado del Laboratorio 4 y amplíalo para admitir una transferencia de datos de varios bytes a través de un pequeño ring buffer. El ring buffer debe tener un puntero productor que el simulador avanza cuando se «producen» datos (puedes simularlo con un callout que genera datos sintéticos a una tasa fija), un puntero consumidor que el driver avanza cuando se consumen datos, y un registro de «tamaño de cola» que el driver puede leer para conocer la capacidad del ring.

Escribe un pequeño driver que:

1. Identifique el dispositivo simulado.
2. Lea el registro de tamaño de cola para conocer la capacidad del ring.
3. Lea periódicamente el puntero productor para saber cuántas entradas nuevas están disponibles.
4. Lea cada nueva entrada y la imprima en el log del kernel.
5. Actualice el puntero consumidor tras procesar cada entrada.

El ejercicio practica la habilidad de reconocimiento de ring buffers de la Sección 5 en un entorno donde controlas ambos lados. El desafío consiste en escribir el driver de forma que gestione correctamente tanto el caso de ring vacío (sin entradas nuevas) como el caso de ring lleno (más entradas de las que el driver ha consumido).

### Desafío 3: Detecta la identidad del dispositivo solo por su comportamiento

El dispositivo simulado del Laboratorio 4 tiene, en su código complementario, un «modo misterio» que deshabilita el registro de identificación y modifica parte de su comportamiento para ocultar su identidad. Sin leer el código fuente del dispositivo simulado, escribe un pequeño driver que:

1. Sondee el dispositivo simulado en modo misterio.
2. Realice una serie de lecturas seguras de registros.
3. Observe la respuesta del dispositivo a un pequeño conjunto de operaciones de prueba.
4. Identifique, basándose únicamente en el comportamiento observado, cuál de las tres identidades conocidas corresponde al simulador.

El desafío enseña la habilidad de nivel superior de identificar un dispositivo mediante su comportamiento en lugar de mediante identificadores explícitos. Los dispositivos reales a veces ocultan su identidad por razones de compatibilidad (presentándose como un chip más común), y la única forma de admitirlos correctamente es identificarlos por su comportamiento.

### Desafío 4: Documenta un dispositivo que tengas

Elige un dispositivo USB que sea tuyo y que no tenga funcionalidad especialmente sensible. Un mando de juego USB, una webcam, un adaptador USB a serie, un adaptador USB Wi-Fi. Escribe una pseudohoja de datos para él, usando únicamente lo que puedas aprender mediante `usbconfig`, `usbdump` y Wireshark.

El desafío requiere que apliques, en secuencia, todas las técnicas del capítulo: identificación, captura, observación, hipótesis y documentación. El resultado es un artefacto real: una pseudohoja de datos para un dispositivo real. Muchas de estas pseudohojas de datos han dado lugar a proyectos de la comunidad que han producido drivers de producción; la tuya podría ser la siguiente.

## Ejercicio práctico: Tu propia observación

Los laboratorios te han dado práctica controlada con simuladores de software y dispositivos conocidos, y los desafíos te han pedido que apliques esas técnicas a objetivos algo menos estructurados. Los casos de estudio de la Sección 13 te guiaron a través de trabajos de ingeniería inversa que hace tiempo fueron incorporados al árbol de FreeBSD. Lo que ninguno de ellos te ha pedido es que te sientes con un dispositivo de tu propia elección, lo observes partiendo de cero, mapees lo que ves en estructuras de interfaz y endpoint, y esboCES el inicio de un driver desde cero. Eso es exactamente lo que hace este ejercicio, y es lo más cerca que este capítulo puede llevarte al trabajo que los autores de `umcs` y `axe` realizaron al comienzo de sus proyectos.

Trata el ejercicio como una prueba culminante más que como otro laboratorio. No introduce una técnica nueva; te pide que combines las técnicas que ya has practicado en una sola sesión autodirigida, utilizando un dispositivo que tienes físicamente y herramientas del sistema base de FreeBSD. El objetivo no es un driver listo para distribuir. El objetivo es un registro breve y fiel de lo que observaste, un archivo esqueleto que compila y se engancha correctamente, y una lista de las preguntas que tu observación no respondió. Si terminas con esos tres artefactos, el ejercicio habrá funcionado como se pretendía.

### Antes de empezar: La barrera ética

Dado que este ejercicio implica un dispositivo real que es tuyo, el marco legal y ético de las Secciones 1 y 12 se aplica directamente a él, y se aplica antes de ejecutar el primer comando. Repasa la breve lista de comprobación siguiente, en orden. No continúes hasta que cada elemento tenga una respuesta clara.

1. **Elige un dispositivo que sea completamente tuyo.** El ejercicio no es una licencia para analizar equipos que hayas tomado prestados, alquilado o a los que se te haya concedido acceso limitado. El dispositivo debe ser tuyo, y debe ser un dispositivo que estés dispuesto a ver funcionar mal brevemente durante la observación. Una regla práctica es que si no te sentirías cómodo desconectando el dispositivo a mitad de su funcionamiento normal, no es un buen candidato para este ejercicio.

2. **Elige un dispositivo cuyo firmware y protocolo no estén protegidos por el fabricante.** Los buenos candidatos son dispositivos USB conformes con una clase que implementen una especificación abierta. Un teclado o ratón USB HID, un adaptador USB a serie basado en un chip de mercado, una interfaz de audio USB conforme con una clase, o una pequeña carcasa de LED conectada por USB con un protocolo específico del fabricante sencillo son todas opciones razonables. Descarta cualquier dispositivo cuyo firmware se sepa que está sujeto a DRM, cualquiera cuyo protocolo esté cubierto por un acuerdo de no divulgación que hayas firmado, y cualquiera cuyo driver del fabricante se distribuya bajo una licencia de usuario final que prohíba específicamente el tipo de observación descrita aquí. Cuando un dispositivo sea ambiguo, déjalo de lado y elige otra cosa. El ejercicio trata de técnica, no de analizar ningún hardware específico.

3. **Solo observa, no importes.** Todo lo que el recorrido te pide que hagas es observación pasiva del tráfico en un bus que tú controlas, seguida de escribir tu propio código a partir de tus propias notas. Eso se mantiene firmemente dentro del marco de sala limpia de la Sección 12. En el momento en que pasas de la observación a copiar el firmware del fabricante, pegar el código fuente del driver del fabricante o redistribuir un blob binario que no has creado tú, abandonas ese marco y entras en un espacio donde la respuesta depende de la licencia del material específico. No cruces esa línea durante el ejercicio. Si más adelante quieres llevar el trabajo hacia un driver real, revisa las Secciones 9 y 12 y trabaja con cuidado las cuestiones de licencia antes de hacerlo.

4. **Respeta las salvaguardas existentes del capítulo.** La disciplina descrita en las Secciones 1 y 12 es el marco de autoridad para este proyecto. Si algún paso de los que siguen parece estar en conflicto con esas secciones, considera las secciones como correctas y ajusta el paso, no al contrario. El recorrido aquí es una aplicación concreta de ese marco, no una excepción al mismo.

Con esos cuatro puntos resueltos, puede comenzar la observación. El resto del ejercicio asume que los cuatro están despejados; si alguno de ellos es incierto, cierra esta sección y elige un dispositivo diferente.

### Paso 1: Identificación inicial con `usbconfig(8)`

Conecta el dispositivo objetivo a tu sistema FreeBSD y confirma que el kernel lo ha enumerado:

```console
$ sudo usbconfig list
ugen0.1: <0x1022 XHCI root HUB> at usbus0, cfg=0 md=HOST spd=SUPER (5.0Gbps) pwr=SAVE (0mA)
ugen0.2: <Example device Example vendor> at usbus0, cfg=0 md=HOST spd=FULL (12Mbps) pwr=ON (100mA)
```

Anota la coordenada `ugenB.D` de tu dispositivo. Esa coordenada es la forma en que todos los comandos `usbconfig` posteriores lo identificarán.

A continuación, comprueba qué driver de FreeBSD, si lo hay, ha reclamado una interfaz en el dispositivo:

```console
$ sudo usbconfig -d ugen0.2 show_ifdrv
```

Si un driver del kernel ya tiene la interfaz, la salida lo indica. Un dispositivo cuya interfaz ya está reclamada por un driver existente seguirá respondiendo a las consultas de descriptores de solo lectura, pero no debes ejecutar tu propio código experimental contra él hasta que hayas desenganchado el driver existente con `devctl detach`. Para una primera pasada, elige un dispositivo cuya interfaz esté sin reclamar o cuyo driver estés dispuesto a desenganchar.

Por último, vuelca el árbol completo de descriptores y guárdalo para más adelante:

```console
$ sudo usbconfig -d ugen0.2 dump_all_desc > device-descriptors.txt
```

La salida lista el descriptor de dispositivo, cada descriptor de configuración, cada descriptor de interfaz y cada descriptor de endpoint. Compararás este archivo con tu captura de paquetes en el Paso 3, y lo utilizarás como material en bruto para el fragmento corto de pseudohoja de datos al final del ejercicio.

### Paso 2: Observación a nivel de paquete con `usbdump(8)`

Los descriptores te dicen lo que el dispositivo anuncia sobre sí mismo. Para ver lo que el dispositivo hace realmente, captura el bus USB mientras se usa el dispositivo:

```console
$ sudo usbdump -i usbus0 -s 2048 -w trace.pcap
```

El argumento `-i usbus0` selecciona el bus USB que se va a escuchar; usa el número de bus al que está conectado tu dispositivo, según lo indicado por `usbconfig list`. El argumento `-s 2048` limita cada payload capturado a 2048 bytes, que está por encima del tamaño máximo de paquete de cualquier endpoint de velocidad completa o alta velocidad que probablemente encuentres en este ejercicio. El argumento `-w trace.pcap` escribe un archivo pcap binario que Wireshark puede abrir y diseccionar con el disector USB incorporado.

Con `usbdump` en ejecución, usa el dispositivo. Para un teclado HID, pulsa algunas teclas. Para un adaptador USB a serie, abre el puerto desde otra herramienta y envía unos pocos bytes en cada dirección. Para una carcasa de LED USB, cicla el LED por sus estados admitidos. Cada acción produce transferencias USB en el bus, y cada transferencia termina en la captura.

Detén `usbdump` con Control-C. Un registro de texto legible también es útil cuando anotas las transferencias a mano, así que vuelve a ejecutar la captura una vez con la salida redirigida a un archivo de texto:

```console
$ sudo usbdump -i usbus0 -s 2048 > trace.txt
```

Conserva ambas versiones. La forma de texto es más fácil de leer línea a línea y de anotar en un editor de texto; la forma pcap es más fácil de navegar en Wireshark cuando quieres seguir un flujo específico.

### Paso 3: Mapea los descriptores en estructuras de interfaz y endpoint

Abre `device-descriptors.txt` junto a `trace.txt`. Repasa el volcado de descriptores y, para cada descriptor de interfaz, anota:

- La clase del dispositivo o interfaz (`bInterfaceClass`), la subclase (`bInterfaceSubClass`) y el protocolo (`bInterfaceProtocol`). Los valores estándar están tabulados en las especificaciones de clase USB: HID es la clase `0x03`, almacenamiento masivo es la clase `0x08`, la clase de dispositivo de comunicaciones es `0x02`, audio es `0x01`, y específica del fabricante es `0xFF`, entre otros.
- La dirección de cada endpoint (`bEndpointAddress`), el sentido de la transferencia (el bit alto de la dirección: activado significa IN, desactivado significa OUT) y el tipo de transferencia (los dos bits bajos de `bmAttributes`: `0` control, `1` isócrono, `2` bulk, `3` interrupción).
- El tamaño máximo de paquete de cada endpoint (`wMaxPacketSize`).

Los dispositivos sencillos suelen exponer una sola interfaz con un número reducido de endpoints. Un teclado USB HID tiene habitualmente un único endpoint de interrupción IN que el host sondea en busca de informes de estado de teclas. Un adaptador USB a serie suele tener un endpoint bulk IN para los datos procedentes del dispositivo, un endpoint bulk OUT para los datos enviados al dispositivo, y utiliza el endpoint de control para los comandos de estado de línea.

Contrasta lo que has anotado con la captura. Cada dirección de endpoint que hayas registrado en el volcado del descriptor debería aparecer en `trace.txt` como origen o destino de al menos una transferencia. Cada tipo de transferencia que hayas registrado (bulk, interrupción, control) debería aparecer en la captura en la dirección que esperas. Si los dos no coinciden, vuelve a revisar antes de esbozar ningún código del driver; una de las dos observaciones es incorrecta y conviene averiguar cuál.

### Paso 4: Esboza un esqueleto de driver al estilo Newbus

Con la identidad del dispositivo, sus interfaces y sus endpoints en mano, puedes trazar el contorno de un driver que se engancharía a él. El esbozo no es un driver funcional. Es el andamio en el que más adelante incorporarías la configuración real de transferencias, el manejo real de datos y el tratamiento real de errores. El objetivo de producirlo ahora es confirmar que tus observaciones son suficientemente coherentes como para dar forma a un driver en absoluto.

Elige un identificador Newbus corto y en minúsculas. La convención en el árbol USB de FreeBSD es una cadena breve en minúsculas que evoca el nombre de la familia de chip o la función del dispositivo: `umcs` para el puente serie de MosChip, `axe` para el chip Ethernet ASIX, `uftdi` para los adaptadores FTDI, `ukbd` para los teclados USB HID. Elige algo que no colisione con ningún nombre que ya exista en `/usr/src/sys/dev/usb/`. Tu esbozo local puede usar un marcador provisional como `myusb` hasta que te decidas por un nombre definitivo.

El esqueleto que aparece a continuación es el mínimo que necesita un driver USB, y también es lo que encontrarás en el archivo de acompañamiento `skeleton-template.c`:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

/* TODO: replace with the VID/PID you observed in Step 1. */
static const STRUCT_USB_HOST_ID myusb_devs[] = {
    { USB_VP(0x0000 /* VID */, 0x0000 /* PID */) },
};

static device_probe_t  myusb_probe;
static device_attach_t myusb_attach;
static device_detach_t myusb_detach;

static int
myusb_probe(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);

    if (uaa->usb_mode != USB_MODE_HOST)
        return (ENXIO);

    return (usbd_lookup_id_by_uaa(myusb_devs,
        sizeof(myusb_devs), uaa));
}

static int
myusb_attach(device_t dev)
{
    /* TODO: allocate the endpoints you catalogued in Step 3. */
    /* TODO: set up the usb_config entries that match them.   */
    device_printf(dev, "attached\n");
    return (0);
}

static int
myusb_detach(device_t dev)
{
    /* TODO: unwind whatever attach allocated, in reverse order. */
    device_printf(dev, "detached\n");
    return (0);
}

static device_method_t myusb_methods[] = {
    DEVMETHOD(device_probe,  myusb_probe),
    DEVMETHOD(device_attach, myusb_attach),
    DEVMETHOD(device_detach, myusb_detach),
    DEVMETHOD_END
};

static driver_t myusb_driver = {
    .name    = "myusb",
    .methods = myusb_methods,
    .size    = 0,
};

DRIVER_MODULE(myusb, uhub, myusb_driver, NULL, NULL);
MODULE_DEPEND(myusb, usb, 1, 1, 1);
MODULE_VERSION(myusb, 1);
```

Las marcas `TODO` señalan dónde se incorporan al driver las observaciones de los Pasos 1 al 3. Los identificadores de fabricante y producto proceden directamente del descriptor de dispositivo que guardaste. La asignación de endpoints y la configuración de transferencias dentro de `myusb_attach` reflejan los endpoints que catalogaste. En la forma completa del driver, cada endpoint se convierte en una entrada en un array `struct usb_config` cuyos campos `.type` y `.direction` registran lo que observaste: un endpoint bulk IN usa `.type = UE_BULK` y `.direction = UE_DIR_IN`, un endpoint de interrupción IN usa `.type = UE_INTERRUPT` y `.direction = UE_DIR_IN`, y así sucesivamente. Los drivers existentes en `/usr/src/sys/dev/usb/` están llenos de ejemplos de este patrón; `/usr/src/sys/dev/usb/serial/umcs.c` y `/usr/src/sys/dev/usb/serial/uftdi.c` son referencias razonables.

No intentes completar los cuerpos de las transferencias todavía. El objetivo en esta etapa es un esqueleto que compile, se cargue y se enganche a tu dispositivo, y que imprima un mensaje corto al engancharse y otro al desengancharse. Si el esqueleto hace esas tres cosas sin problemas, tus observaciones eran coherentes. Si entra en pánico, se niega a engancharse o imprime algo sin sentido, el fallo casi siempre está en el mapeo de descriptores y no en el esqueleto en sí; vuelve al Paso 3 y comprueba las direcciones IN/OUT y los tipos de transferencia antes de tocar el código.

### Paso 5: Anota lo que has aprendido

Cierra el ejercicio produciendo dos breves documentos escritos para conservar junto al volcado de descriptores, el archivo de captura y el esqueleto del driver:

1. Un **fragmento de pseudo-datasheet**, siguiendo la estructura de la Sección 11. Identifica el dispositivo, enumera su clase y subclase, lista sus endpoints con sus tipos de transferencia, direcciones y tamaños máximos de paquete, y anota cualquier comportamiento que hayas observado en la captura que te haya sorprendido. Mantén el fragmento breve. El objetivo es tener algo que pudieras entregar a un colaborador si los dos quisierais retomar el trabajo más adelante.

2. Una **lista de preguntas abiertas** que no pudiste responder solo con la observación. Todo trabajo honesto de ingeniería inversa termina con preguntas abiertas, y escribirlas es la manera de evitar fingir que sabes cosas que no sabes. Las entradas típicas incluyen una solicitud de control específica del fabricante cuyo propósito no está claro, un bloque de bytes dentro de una transferencia bulk cuya estructura no es obvia, o un campo de descriptor cuyo significado depende de un estado del dispositivo que todavía no has visto.

Estos dos documentos, junto con `device-descriptors.txt`, `trace.pcap`, `trace.txt` y `skeleton-template.c`, constituyen el resultado del ejercicio. Son, a menor escala, el mismo tipo de salida que los autores de `umcs` y `axe` habrían producido al inicio de sus propios proyectos.

### Un recordatorio final sobre el alcance

Todo lo que este ejercicio te ha pedido que hagas es observación pasiva del tráfico en tu propio bus, seguida de la escritura de tu propio código a partir de tus propias notas. Eso se sitúa de lleno dentro de la tradición de sala limpia que el capítulo ha descrito. Si, tras trabajar en los pasos, quieres llevar el trabajo más adelante hacia un driver publicable, detente y vuelve a leer las Secciones 9, 11 y 12 antes de la siguiente sesión. Las técnicas escalan, pero el marco legal y ético escala con ellas, y es más fácil aplicarlo pronto que deshacer un commit más tarde. La carpeta de acompañamiento de este ejercicio, en `examples/part-07/ch36-reverse-engineering/exercise-your-own/`, reúne un script de guía de plantilla, un archivo fuente de esqueleto con las marcas `TODO` mostradas arriba, y un breve `README` que agrupa las notas de seguridad en un solo lugar.

## Errores comunes y resolución de problemas

La ingeniería inversa tiene un pequeño conjunto de errores que casi todo el mundo comete al principio, y un conjunto igualmente pequeño de errores en los que, incluso con experiencia, es fácil caer. Reconocerlos de antemano acorta de forma significativa la dolorosa curva de aprendizaje.

### Error 1: Creer en la primera hipótesis

El error más común es formarse una hipótesis pronto y luego interpretar cada observación posterior como si la respaldara, incluso cuando una lectura más cuidadosa sugeriría una explicación diferente. El sesgo de confirmación es humano, y es especialmente peligroso en ingeniería inversa porque las observaciones son ruidosas y el espacio de hipótesis es grande.

La defensa es la disciplina de anotar cada hipótesis explícitamente, diseñar una prueba que pueda falsificarla, ejecutar la prueba y aceptar el resultado con honestidad. Si la prueba no falsifica la hipótesis, esta sobrevive, pero no está aún "probada"; simplemente "aún no ha sido refutada". La siguiente prueba podría todavía eliminarla.

### Error 2: Saltarse el cuaderno de notas

El segundo error más común es saltarse el cuaderno de notas porque el teclado es más rápido. El cuaderno parece un lastre. Los resultados, en el momento, parecen claros. Los patrones son obvios. No hace falta anotarlos, porque se recordarán.

Una semana después, los patrones ya no son obvios. Otra semana más tarde, se han olvidado. Un mes después, el proyecto se ha estancado porque nadie, incluido el propio autor, puede reconstruir qué se sabía. El cuaderno es lo que lo habría evitado. Prescinde de él bajo tu propio riesgo.

### Error 3: Escribir en registros desconocidos

El tercer error común es escribir en un registro sin evidencia suficiente de que la escritura es segura. La tentación es fuerte: tienes una hipótesis, quieres probarla, la prueba implica una escritura, y ¿qué es lo peor que podría pasar? La Sección 10 ya ha explicado qué puede pasar, pero vale la pena repetirlo aquí. Los dispositivos pueden dañarse de forma permanente por escrituras descuidadas, y quienes aprenden esto por experiencia suelen aprenderlo de forma costosa.

La defensa es la disciplina de evidencia-antes-de-escritura. Antes de realizar una escritura, nombra la fuente de evidencia que demuestra que es segura. Si no puedes nombrar una fuente, no realices la escritura.

### Error 4: Tratar el driver del fabricante como completo

Un cuarto error común es asumir que el driver del fabricante implementa la funcionalidad completa del dispositivo, y que reproducir el comportamiento del driver del fabricante es suficiente para dar soporte completo al dispositivo. Esto suele ser incorrecto. Los drivers de los fabricantes a veces implementan únicamente el subconjunto de funcionalidad del dispositivo que utilizan los productos del fabricante; el dispositivo completo puede tener características que el driver del fabricante nunca ejercita. A la inversa, el driver del fabricante a veces contiene soluciones para bugs de hardware que no serían necesarias si las operaciones se implementaran de otra manera.

La defensa es leer el driver del fabricante como una fuente de información entre muchas, con la comprensión de que representa las decisiones del fabricante, no la descripción completa del dispositivo.

### Error 5: No probar el camino de detach

Un quinto error común es centrarse en el camino de attach, donde se ve el progreso visible, y descuidar el camino de detach. El resultado es un driver que se carga y funciona, pero que no puede descargarse de manera limpia. Cada ciclo de prueba requiere entonces un reinicio, lo que ralentiza el trabajo en un orden de magnitud.

La defensa es escribir el camino de detach pronto, probarlo en cada build y tratar los pánicos en el momento de la descarga como bugs graves que bloquean el trabajo posterior. Un driver que no puede descargarse es, a efectos prácticos, un driver que requiere un reinicio para probar los cambios, y esa no es una manera productiva de trabajar.

### Error 6: No guardar las capturas

Un sexto error es descartar las capturas porque "no mostraban nada nuevo". Puede que hoy no muestren nada nuevo. Puede que muestren algo importante dentro de seis semanas, cuando hayas aprendido lo suficiente como para interpretarlas. Guarda cada captura. El almacenamiento es barato. Las propias capturas forman parte del historial del proyecto, y a veces son la única manera de reconstruir qué se sabía en un momento dado.

### Error 7: Trabajar solo

Un séptimo error, especialmente común en principiantes, es trabajar solo. La ingeniería inversa es mucho más rápida cuando hay al menos otra persona con quien hablar sobre el trabajo, aunque esa persona no sea también ingeniera inversa. El acto de explicar qué has observado, qué crees y qué estás a punto de probar impone claridad a la explicación, y la claridad es lo que hace avanzar el trabajo.

Si no tienes un colega interesado, busca una comunidad que lo esté. Las listas de correo, los canales IRC y los foros de la familia del dispositivo están llenos de personas que entienden los problemas y que a veces pueden proporcionar la pieza que falta. El aspecto de colaboración comunitaria de la Sección 9 no trata solo de consumir trabajo previo; también trata de contribuir a una discusión que ayuda a todos los participantes.

### Resolución de problemas: cuando el driver no hace nada en silencio

A veces el driver se carga, el dispositivo parece engancharse, el registro dice que todo está bien, pero no se produce el comportamiento esperado. Las causas más comunes:

- El driver cree que ha configurado un manejador de interrupciones, pero la interrupción no se está entregando realmente. Comprueba `vmstat -i` para ver si llegan interrupciones. Si no es así, la configuración de la interrupción es incorrecta.
- El driver está leyendo el registro equivocado y ve lo que parece un valor legítimo. Compara con la salida de `pciconf -r` o `regprobe` para verificar que los valores que estás leyendo coinciden con los valores presentes en el dispositivo.
- El driver está usando el orden de bytes incorrecto para valores multibyte. Esto es especialmente común al leer valores que parecen enteros pero que en realidad son cadenas de bytes.
- El driver tiene un bug en su secuencia: realizó el paso B antes del paso A, o saltó un paso obligatorio.

La defensa en cada caso es la comparación con una referencia conocida: las capturas de la Sección 3, el volcado de `regprobe`, el driver del fabricante que funciona. Cuando el driver no se comporta como se esperaba, la pregunta es: ¿dónde diverge por primera vez su comportamiento de la referencia?

### Resolución de problemas: cuando el driver entra en pánico al descargarse

Un pánico al descargar normalmente indica que el driver liberó algo prematuramente o mantuvo una referencia a algo que el kernel ya ha destruido.

Las causas más comunes:

- Un callout no se drenó antes de liberar el softc. Comprueba que cada `callout_init` tiene su correspondiente `callout_drain` en el camino de detach.
- Una tarea de taskqueue estaba pendiente cuando se destruyó el taskqueue. Comprueba que cada `taskqueue_enqueue` tiene su correspondiente `taskqueue_drain` en el camino de detach.
- Un manejador de interrupciones no se desmontó antes de liberar el recurso IRQ. El orden importa: desmonta el manejador con `bus_teardown_intr`, después libera el recurso con `bus_release_resource`.
- Un nodo de dispositivo de caracteres no se destruyó antes de liberar el softc. Usa `destroy_dev_drain` si existe alguna posibilidad de que el dispositivo todavía esté abierto.

Estos son los mismos patrones que estudiamos en el Capítulo 35 para los drivers asíncronos; los drivers obtenidos por ingeniería inversa se enfrentan a los mismos problemas, con la complicación adicional de que es posible que todavía no comprendas del todo qué recursos están en uso en el momento de la descarga.

### Resolución de problemas: cuando el comportamiento varía entre ejecuciones

A veces el dispositivo se comporta de forma diferente de una ejecución a la siguiente, aunque no haya cambiado nada en el driver. Las causas suelen ser una de las siguientes:

- El dispositivo contiene estado que no se restablece entre ejecuciones. Investiga qué limpia realmente el reset del dispositivo y qué deja intacto.
- El driver tiene una condición de carrera que produce resultados distintos según el timing.
- El dispositivo es sensible a condiciones ambientales (temperatura, tensión) que varían ligeramente entre ejecuciones.
- Un segundo driver, o un programa en espacio de usuario, también accede al dispositivo e interfiere con tu investigación.

Distinguir estas causas requiere ejecuciones cuidadosas y repetidas con cada variable controlada. La ingeniería inversa sobre un dispositivo cuyo comportamiento no es determinista es considerablemente más difícil que el caso determinista, e identificar el origen del no determinismo es en sí parte del trabajo.

## Cerrando

La ingeniería inversa es un oficio construido sobre la paciencia, la disciplina y la documentación cuidadosa. El capítulo ha recorrido todo el proceso: por qué este trabajo es a veces necesario, dónde están los límites legales, cómo configurar el laboratorio, cómo observar de forma sistemática, cómo construir un mapa de registros, cómo identificar estructuras de buffer, cómo escribir un driver mínimo y hacerlo crecer de forma incremental, cómo validar hipótesis en simulación, cómo colaborar con la comunidad, cómo evitar dañar el hardware, y cómo consolidar el resultado en un driver mantenible y un pseudo-datasheet utilizable.

Las técnicas son concretas. La disciplina es lo que las mantiene unidas. Un ingeniero inverso que siga la disciplina será capaz, con el tiempo, de tomar un dispositivo sin documentación y producir un driver funcional. Un ingeniero inverso que omita la disciplina producirá código que funciona a veces, que falla por razones difíciles de diagnosticar, y que no puede mantenerse ni ampliarse sin redescubrir todo lo que se olvidó la primera vez.

A lo largo del capítulo han discurrido varios temas que merecen un resumen final explícito.

El primer tema es la separación entre observación, hipótesis y hecho verificado. Una observación es lo que viste. Una hipótesis es lo que crees que significa. Un hecho verificado es una hipótesis que ha sobrevivido a intentos deliberados de falsificarla. Mezclar las tres produce confusión; mantenerlas separadas produce claridad. La disciplina del cuaderno de notas que el capítulo ha subrayado es, en el fondo, la disciplina de mantener estas tres categorías bien diferenciadas.

El segundo tema es el valor de empezar por lo pequeño. Un driver mínimo que hace una cosa correctamente es una base mejor que un driver grande que hace muchas cosas con incertidumbre. Cada nueva funcionalidad añadida de forma incremental, con verificación, es mucho más barata de hacer bien que un cambio amplio con múltiples funcionalidades. El ritmo de trabajo en ingeniería inversa siempre debería sentirse más lento que el ritmo del desarrollo normal de drivers; esa lentitud es lo que atrapa los bugs que de otro modo llegarían hasta el código final.

El tercer tema es la centralidad de la seguridad. La ingeniería inversa es una de las pocas actividades de software en las que los errores descuidados pueden dañar permanentemente hardware real. Las técnicas de seguridad de la sección 10, las técnicas de simulación de la sección 8 y el principio de leer primero de la sección 4 son todas manifestaciones del mismo principio subyacente: respeta lo desconocido y gánate el derecho a realizar una operación acumulando evidencia de que esa operación es segura.

El cuarto tema es la importancia de la documentación. El pseudo-datasheet, el cuaderno de notas, la página de manual, los comentarios en el código: no son elementos secundarios, forman parte del resultado del trabajo. Un driver sin documentación es un driver que nadie puede mantener, incluido su propio autor seis meses después. Un pseudo-datasheet que registra lo aprendido es el artefacto que da durabilidad al trabajo más allá de la implicación del autor.

El quinto tema es la comunidad. La ingeniería inversa rara vez es un trabajo verdaderamente solitario. Para casi cualquier dispositivo que valga la pena investigar existe trabajo previo; el trabajo nuevo, cuando está bien hecho, contribuye al conocimiento colectivo de la comunidad. Buscar, evaluar y contribuir son parte del oficio. Un ingeniero inverso que trabaja en aislamiento reinventa ruedas; uno que se compromete con la comunidad construye sobre el trabajo de otros y sirve de base a quienes vienen después.

Ya has visto la forma completa del trabajo. Las técnicas están al alcance. La disciplina requiere práctica. El primer proyecto serio será lento y estará lleno de errores; el segundo será más rápido; para el tercero o el cuarto, el ritmo comenzará a sentirse natural. Los laboratorios de este capítulo son el inicio de esa práctica, y los ejercicios de desafío son el paso siguiente. Los proyectos reales sobre dispositivos reales son donde las habilidades se consolidan, y la comunidad FreeBSD tiene abundantes dispositivos que se beneficiarían de que alguien esté dispuesto a hacer ese trabajo.

Tómate un momento para apreciar lo que ha cambiado en tu conjunto de herramientas. Antes de este capítulo, un dispositivo sin documentación era una señal de stop. Ahora es un proyecto. Los métodos que has aprendido son los mismos que los autores de muchos drivers en `/usr/src/sys/dev/` utilizaron para traer esos drivers a la existencia. Algunos trabajaron solos, otros en grupos pequeños, pero todos siguieron un flujo de trabajo reconociblemente igual: observar, formular hipótesis, probar, documentar. Ahora sabes cómo hacer ese trabajo.

## Puente hacia el capítulo 37: envío de tu driver al proyecto FreeBSD

El driver que acabas de aprender a construir, ya sea uno que implementa un dispositivo completamente reconstruido por ingeniería inversa o algún hardware más convencional, es más útil cuando otras personas pueden encontrarlo, compilarlo y confiar en él. Hasta ahora hemos tratado el driver como un proyecto privado, algo que cargas en tus propios sistemas y que documentas para tu propia referencia futura. El próximo capítulo cambia eso: veremos cómo tomar un driver terminado y ofrecerlo para su inclusión en el árbol de código fuente de FreeBSD.

El cambio es significativo. Un driver en tu repositorio privado te sirve a ti y a cualquiera que encuentre casualmente tu repositorio. Un driver en el árbol de código fuente de FreeBSD se compila en cada release de FreeBSD, queda expuesto a todos los usuarios de FreeBSD, es mantenido por el proyecto FreeBSD y se prueba con cada commit que toca el código circundante. La amplificación del valor es enorme, y también lo son las responsabilidades que conlleva. El proceso de envío es el mecanismo que FreeBSD utiliza para asegurarse de que el driver cumple los estándares del árbol de código fuente antes de permitirle la entrada.

El capítulo 37 recorre ese proceso. Examinaremos el modelo de desarrollo de FreeBSD: la diferencia entre un contribuidor y un committer, el papel de la organización del árbol de código fuente, el proceso de revisión y las convenciones que el árbol de código fuente de FreeBSD aplica. Aprenderemos las guías de estilo (`style(9)` para el código y `style(4)` para las páginas de manual, junto con algunas convenciones relacionadas para makefiles, cabeceras y nomenclatura), cómo estructurar los archivos de tu driver para su inclusión en `/usr/src/sys/dev/`, cómo escribir la página de manual que todo driver debería incluir, y cómo redactar los mensajes de commit en la forma que FreeBSD espera.

También examinaremos la dinámica social de la contribución: cómo interactuar con los revisores, cómo responder a los comentarios, cómo iterar sobre una serie de parches, y cómo participar en el proyecto de una manera que construya reputación a largo plazo. La parte técnica del proceso de envío es directa; la parte social es donde la mayoría de los contribuidores por primera vez encuentran las sorpresas.

Varios temas de este capítulo se proyectarán hacia adelante. El camino de detach limpio es esencial, porque los revisores lo comprobarán. Los envoltorios de seguridad y el comportamiento conservador de la sección 10 son las mismas disciplinas que exhiben los drivers de nivel de producción, y serán apreciados por los revisores. El pseudo-datasheet no forma parte del envío, pero la comprensión que representa es lo que justifica las afirmaciones del driver y lo que permite a los revisores confiar en la implementación. La participación en la comunidad de la sección 9 es la base de la relación a más largo plazo que la contribución pretende construir.

El driver obtenido por ingeniería inversa es un caso particularmente interesante para la inclusión en FreeBSD, porque el proyecto tiene una larga experiencia con este tipo de drivers y procesos bien desarrollados para gestionarlos. La disciplina de procedencia de la sección 11 es exactamente lo que el proyecto necesita para evaluar si un driver está libre de preocupaciones sobre derechos de autor. Las limitaciones documentadas y las preguntas abiertas del pseudo-datasheet ayudan al proyecto a establecer expectativas apropiadas para los usuarios. Los envoltorios de seguridad y el comportamiento conservador ayudan a los revisores a confiar en que el driver no dañará el hardware de los usuarios.

Si has seguido los laboratorios y los ejercicios de desafío de este capítulo, habrás producido al menos algunas piezas pequeñas de código que podrían, con algo de pulido, ser candidatas para su inclusión. El capítulo 37 te mostrará cómo tomar una de esas piezas de código y guiarla a través del proceso de envío. El driver del dispositivo simulado del laboratorio 4 puede que no sea un candidato (no maneja hardware real), pero los patrones que demuestra son exactamente los patrones que debería seguir un driver real, y los envoltorios de seguridad del laboratorio 5 son patrones que los revisores reconocerán y aprobarán.

El libro se acerca a sus capítulos finales. Empezaste en la parte 1 sin ningún conocimiento del kernel. Aprendiste UNIX y C en las partes 1 y 2. Aprendiste la estructura y el ciclo de vida de un driver de FreeBSD en las partes 3 y 4. Aprendiste los patrones de bus, red, almacenamiento y pseudo-dispositivo en las partes 4 y 5. Aprendiste el framework Newbus en detalle en la parte 6. Ahora has trabajado a través de los temas de maestría de la parte 7: portabilidad, virtualización, seguridad, FDT, rendimiento, depuración avanzada, I/O asíncrona e ingeniería inversa. El conjunto de habilidades es completo. Los capítulos restantes lo vinculan de nuevo al proyecto, la comunidad y la práctica de contribuir a un sistema operativo del mundo real.

Tómate un momento, antes de pasar al capítulo 37, para mirar atrás y ver lo que ya eres capaz de hacer. Puedes escribir un driver de caracteres simple desde cero. Puedes escribir un driver de red que participe en la pila de red del kernel. Puedes escribir un driver para un dispositivo descubierto a través de Newbus. Puedes escribir un driver asíncrono que soporte `poll`, `kqueue` y `SIGIO`. Puedes depurar un driver con `dtrace`, `KASSERT` y `kgdb`. Puedes trabajar con un dispositivo para el que no existe documentación. Cada una de estas capacidades es real, construida sobre las anteriores, y juntas constituyen un conocimiento funcional del desarrollo de drivers de dispositivo en FreeBSD.

El próximo capítulo te ayudará a tomar ese conocimiento y convertirlo en una contribución al proyecto FreeBSD. Veamos cómo funciona ese proceso.
