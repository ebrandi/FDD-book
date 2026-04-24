---
title: "Técnicas avanzadas de depuración"
description: "Métodos sofisticados de depuración para problemas complejos de driver"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 34
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 135
language: "es-ES"
---
# Técnicas avanzadas de depuración

## Introducción

En el capítulo anterior aprendimos a medir qué hace un driver y con qué rapidez lo hace. Observamos cómo crecían los contadores de rendimiento, ejecutamos agregaciones de DTrace para detectar los caminos más calientes y usamos `pmcstat` para ver qué instrucciones consumían realmente los ciclos. La medición nos dio un lenguaje para preguntarnos si el driver se comporta como esperamos.

La depuración hace una pregunta diferente. En lugar de «¿qué tan rápido es esto?», pregunta «¿por qué esto está mal?». Un problema de rendimiento suele producir código lento pero funcional. Un problema de corrección puede producir un crash, un deadlock, corrupción silenciosa de datos, un driver que se niega a descargarse, un puntero que desreferencia basura, o un lock que de algún modo no está en posesión de nadie. Estos son los bugs que hacen que un ingeniero de kernel experimentado respire hondo y busque mejores herramientas.

FreeBSD nos proporciona esas herramientas. Abarcan desde aserciones muy pequeñas y muy rápidas que viven dentro del kernel y capturan el bug en el instante en que ocurre, hasta el análisis post-mortem completo de un crash dump en una máquina que ya no está en ejecución. Existen anillos de trazado ligeros que cuestan casi nada en tiempo de ejecución, trazadores pesados que pueden desenredar el grafo de llamadas completo, y asignadores de memoria que se pueden activar durante el desarrollo para convertir sutiles bugs de use-after-free en crashes inmediatos y diagnosticables. Un autor de drivers bien equipado aprende a recurrir a la herramienta correcta para el bug correcto, en lugar de mirar fijamente la salida de `printf` con la esperanza de obtener iluminación.

El objetivo de este capítulo es enseñarte ese conjunto de herramientas. Comenzaremos por entender cuándo la depuración avanzada es la respuesta correcta y cuándo un enfoque más sencillo servirá mejor. Luego trabajaremos con las macros de aserción dentro del kernel, el camino del panic y cómo leer y analizar un crash dump fuera de línea con `kgdb`. Construiremos un kernel apto para la depuración para que esas herramientas estén disponibles cuando las necesitemos, aprenderemos a rastrear el comportamiento del driver con DTrace y `ktrace`, y finalmente estudiaremos cómo cazar fugas de memoria y accesos inválidos con `memguard(9)`, redzone y guard pages. Cerraremos con la disciplina de la depuración en sistemas en producción, donde cada acción tiene consecuencias, y con un breve estudio de cómo refactorizar un driver tras un fallo grave para que sea más resistente al siguiente.

A lo largo del capítulo utilizaremos un pequeño driver acompañante llamado `bugdemo`. Es un pseudo-dispositivo con bugs deliberados y controlados que podemos activar mediante simples llamadas `ioctl(2)` y luego cazar con cada una de las técnicas que el capítulo enseña. Nada de lo que hacemos toca hardware real, por lo que el entorno de laboratorio permanece seguro incluso cuando deliberadamente provocamos un crash del kernel.

Al final del capítulo serás capaz de añadir aserciones defensivas a un driver, construir un kernel de depuración, capturar un crash dump, abrirlo en `kgdb`, rastrear el comportamiento en vivo con DTrace y `ktrace`, atrapar el mal uso de la memoria con `memguard(9)` y herramientas similares, y aplicar toda esta disciplina de forma segura en sistemas donde otras personas dependen de la máquina.

## Guía de lectura: cómo usar este capítulo

Este capítulo se encuentra en la Parte 7 del libro, junto con el ajuste de rendimiento, la E/S asíncrona y otros temas de nivel avanzado. Asume que ya has escrito al menos un driver de dispositivo de caracteres sencillo, que comprendes el ciclo de vida de carga y descarga, y que has trabajado con `sysctl`, `counter(9)` y DTrace al nivel introducido en el Capítulo 33. Si alguno de esos temas te resulta incierto, una revisita rápida a los Capítulos 8 al 14 y al Capítulo 33 se amortizará con creces en este capítulo.

### Lectura de este capítulo junto al Capítulo 23

Este capítulo recoge deliberadamente donde lo dejó el Capítulo 23. El Capítulo 23, «Debugging and Tracing», introdujo los fundamentos: cómo pensar sobre los bugs, cómo recurrir a `printf`, cómo usar `dmesg` y el log del kernel, cómo leer un panic sencillo, cómo activar las sondas de DTrace y cómo hacer que un driver sea más fácil de observar desde el principio. Se ciñe a los hábitos cotidianos de depuración que necesita un nuevo autor de drivers.

El Capítulo 23 también termina con una transición explícita. Señala que el scripting avanzado de `kgdb` sobre un crash dump y los flujos de trabajo de puntos de interrupción en el kernel en vivo quedan reservados para un capítulo posterior y más avanzado. Ese capítulo posterior es este. Estás leyendo la segunda mitad de un par. Si el Capítulo 23 es el botiquín de primeros auxilios, el Capítulo 34 es el maletín clínico completo.

En la práctica, esto significa dos cosas. Primero, no volveremos a explicar los fundamentos que el Capítulo 23 ya cubrió; daremos por sentado que te sientes cómodo con `printf`, la lectura básica de panics y DTrace introductorio. Si alguno de esos temas te resulta inseguro, vuelve a leer la sección correspondiente del Capítulo 23 antes, porque el material avanzado se construye directamente sobre esos hábitos. Segundo, cuando una técnica de este capítulo tenga un equivalente más sencillo en el Capítulo 23 (por ejemplo, un `bt` básico en `kgdb` es más sencillo que recorrer los campos de `struct thread`), señalaremos la versión del Capítulo 23 y luego mostraremos por qué la versión avanzada justifica su complejidad adicional.

Piensa en los dos capítulos como un arco único. El Capítulo 23 te enseña a detectar que algo está mal y a echar un primer vistazo. El Capítulo 34 te enseña a reconstruir, en detalle, lo que el kernel estaba haciendo en el momento en que ocurrió el bug, incluso en una máquina que ya no está en ejecución.

El material es acumulativo. Cada sección añade otra capa al driver `bugdemo`, por lo que los laboratorios se leen con mayor naturalidad en orden. Puedes hojear hacia adelante como referencia, pero si este es tu primer encuentro con las herramientas de depuración del kernel, recorrer los laboratorios en orden construirá el modelo mental que buscamos.

No necesitas ningún hardware especial. Una máquina virtual FreeBSD 14.3 modesta es suficiente para todos los laboratorios del capítulo. Para el Laboratorio 3 y el Laboratorio 4 querrás tener configurado un dispositivo de crash dump, algo que el capítulo explica paso a paso, y para el Laboratorio 5 necesitarás tener DTrace disponible en tu kernel. Ambos son estándar en una instalación normal de FreeBSD.

Algunas de las técnicas de este capítulo provocan deliberadamente un crash del kernel. Esto es seguro en una máquina de desarrollo y es algo esperado como parte del proceso de aprendizaje. No es seguro en una máquina en producción donde otras personas dependen de un servicio ininterrumpido. La sección final del capítulo está dedicada a esa distinción, porque la disciplina de saber cuándo no usar una herramienta es tan importante como saber cómo usarla.

## Cómo sacar el máximo partido a este capítulo

El capítulo está organizado en torno a un patrón que verás repetirse a lo largo de él. Primero explicamos en qué consiste una técnica, luego explicamos por qué existe y qué tipo de bug está pensada para capturar, luego la anclamos en código fuente real de FreeBSD para que puedas ver dónde vive la idea en el kernel, y finalmente la aplicamos al driver `bugdemo` a través de un pequeño laboratorio. Leer y experimentar a la vez es el enfoque más eficaz. Los laboratorios son deliberadamente pequeños para completarse en pocos minutos cada uno.

Algunos hábitos harán el trabajo más fluido. Mantén un terminal abierto en `/usr/src/` para poder consultar el código real cuando el capítulo lo referencie. El libro enseña mediante la observación de la práctica real de FreeBSD, no a través de pseudocódigo inventado, y construirás una intuición más sólida al confirmar con tus propios ojos que `KASSERT` está realmente definido donde el capítulo dice que lo está, o que `memguard(9)` realmente tiene la API que describimos.

Mantén un segundo terminal abierto en tu VM de prueba, donde cargarás el driver `bugdemo`, provocarás bugs y observarás la salida. Si puedes conectar una consola serie a la VM, hazlo. Una consola serie es la forma más fiable de capturar el final de un mensaje de panic antes de que la máquina se reinicie, y la usaremos en varios laboratorios.

Por último, calibra tus expectativas. Los bugs del kernel a menudo no son lo que parecen a primera vista. Un use-after-free puede presentarse primero como una corrupción aleatoria de datos en un subsistema no relacionado. Un deadlock puede parecer primero una llamada al sistema lenta. Una de las habilidades más valiosas que enseña este capítulo es la paciencia: recopilar evidencias antes de formular una hipótesis, y confirmar la hipótesis antes de comprometerse con una solución. Las herramientas ayudan, pero la disciplina es lo que separa una caza de bugs rápida de una prolongada.

Con esas expectativas establecidas, comencemos discutiendo cuándo la depuración avanzada es realmente la respuesta correcta a un problema.

## 1. Cuándo y por qué necesitas la depuración avanzada

La mayoría de los bugs en un driver se pueden resolver sin necesidad de recurrir a un crash dump o a un marco de trazado. Una lectura cuidadosa del código, un `printf` adicional, una segunda mirada al valor de retorno de una función, un vistazo a `dmesg`: todo ello en conjunto resuelve la mayoría de los defectos que encuentra un autor de drivers. Si puedes ver el problema, reproducirlo fácilmente y mantener el código relevante en tu mente, la herramienta más sencilla es la correcta.

La depuración avanzada existe para los bugs que no ceden ante ese enfoque. Es el conjunto de herramientas al que recurrimos cuando el problema es poco frecuente, cuando aparece lejos de su causa, cuando solo se manifiesta bajo una sincronización específica, cuando el driver se queda colgado en lugar de sufrir un crash, o cuando el síntoma es corrupción en lugar de fallo. Esos bugs comparten una propiedad común: requieren evidencias que no se pueden recopilar fácilmente leyendo el código, y requieren un control sobre la ejecución del kernel que un proceso de usuario normal no tiene.

### Bugs que necesitan más que un printf

La primera clase de bug que exige herramientas avanzadas es el bug que destruye las evidencias de su propia causa. Un use-after-free es el ejemplo canónico. El driver libera un objeto, y luego algún código posterior, posiblemente en una función diferente o en un thread diferente, lee o escribe esa memoria. Para cuando se produce el crash, la liberación ocurrió hace mucho tiempo, la memoria ha sido reutilizada para algo no relacionado, y el backtrace en el punto del crash apunta a la víctima, no al culpable. Un `printf` añadido en el lugar del crash imprimirá fielmente el sinsentido que ve. No te dirá quién liberó la memoria ni cuándo.

Una segunda clase es el bug que solo aparece bajo concurrencia. Dos threads compiten por un lock. Uno de ellos toma el lock en el orden incorrecto, lo que provoca un deadlock con otro thread que tomó los mismos locks en orden inverso. El sistema queda en silencio, y el bug no deja ningún mensaje en la consola. Añadir llamadas `printf` a la ruta de adquisición de locks a menudo perturba la sincronización lo suficiente como para hacer que el bug desaparezca, una propiedad frustrante con la que quienes han tenido que lidiar con algún Heisenbug están bien familiarizados. La comprobación estática del orden de locks, que FreeBSD proporciona a través de `WITNESS`, existe precisamente porque esta clase de bug es difícil de encontrar de cualquier otra manera.

Una tercera clase es el bug que no se puede observar en absoluto en el espacio de usuario. El driver corrompe una estructura de datos del kernel en un camino de código determinado, y las consecuencias aparecen muchos minutos después en un subsistema no relacionado. El proceso que desencadena la corrupción hace mucho que desapareció para cuando algo sale mal. La única forma de correlacionar causa y efecto es capturar el estado completo del kernel en el momento del panic y recorrerlo fuera de línea con `kgdb`, o trazar el kernel de forma continua con DTrace para que el evento sospechoso deje un rastro.

Una cuarta clase es el bug que solo aparece en hardware al que no puedes conectar un depurador, o en configuraciones de producción que no puedes instrumentar directamente. El driver se ejecuta en la máquina de un cliente, falla una vez a la semana, y nadie quiere que tu estación de trabajo de desarrollo esté físicamente conectada a ella. La herramienta para esta situación es el crash dump: una instantánea de la memoria del kernel escrita en disco en el momento del panic, llevada a un entorno seguro y analizada allí. `dumpon(8)` configura adónde va el dump, `savecore(8)` lo recupera después del reinicio, y `kgdb` lo lee fuera de línea.

Cada una de estas clases de bugs tiene su propia herramienta en el conjunto de herramientas de depuración de FreeBSD. El resto del capítulo los introduce uno a uno. El propósito de esta sección introductoria es simplemente establecer expectativas: no vamos a aprender una única técnica que reemplace a `printf`. Estamos aprendiendo una familia de técnicas, cada una adecuada para un tipo particular de dificultad.

### El coste de las herramientas avanzadas

La depuración avanzada no es gratuita. Cada una de las técnicas que estudiaremos conlleva alguna combinación de coste en tiempo de compilación, coste en tiempo de ejecución y coste disciplinario.

El coste en tiempo de compilación es el más fácil de describir. `INVARIANTS` y `WITNESS` hacen que el kernel sea más lento porque añaden comprobaciones que un kernel de producción omite. `DEBUG_MEMGUARD` ralentiza drásticamente ciertas asignaciones porque las sustituye por mapeos de página completa que se desasignan al liberar la memoria. Un kernel de depuración construido con `makeoptions DEBUG=-g` es varias veces más grande que un kernel de producción porque cada función incluye información de depuración completa. Ninguno de estos costes importa en una máquina de desarrollo, donde la corrección vale órdenes de magnitud más que la velocidad. Todos ellos importan en producción.

El coste en tiempo de ejecución se aplica a las herramientas que habilitas en un kernel en funcionamiento. Las sondas de DTrace deshabilitadas no tienen prácticamente ningún coste, pero una sonda habilitada sigue ejecutándose en cada llamada a la función instrumentada. Las entradas de `ktr(9)` son muy baratas, pero no gratuitas. Una sesión de trazado detallada puede generar suficiente salida de log como para llenar un disco. Una sesión de `kdb` pausa todo el kernel, lo que en una máquina que la gente está usando supone un desastre. Cada herramienta tiene un presupuesto de tiempo de ejecución, y parte de la disciplina de este capítulo consiste en saber cuál es ese presupuesto.

El coste disciplinario es el más difícil de cuantificar, pero el más fácil de subestimar. La depuración avanzada requiere paciencia, tomar notas con cuidado y estar dispuesto a convivir con información incompleta. Requiere resistir el impulso de parchear un síntoma visible antes de comprender el defecto subyacente. Un fallo que ocurre en el módulo X casi nunca significa que el bug esté en el módulo X. El lector que aprenda a recopilar evidencias antes de formular una hipótesis lo tendrá más fácil con este capítulo que el lector que quiere aplicar una corrección lo antes posible.

### Un marco de decisión

Con esos costes en mente, aquí tienes un marco de decisión sencillo para elegir tu herramienta. Si el bug es fácil de reproducir y la causa probablemente es visible en el código cercano, empieza leyendo el código y añadiendo sentencias `printf` o `log(9)` estratégicas. Si el bug solo aparece bajo carga o con concurrencia, activa `INVARIANTS` y `WITNESS` y reconstruye el kernel. Si el bug produce un panic, captura el dump y ábrelo en `kgdb`. Si el bug implica corrupción de memoria, activa `DEBUG_MEMGUARD` en el tipo de asignación sospechoso. Si el bug implica un mal comportamiento silencioso en lugar de un crash, añade sondas SDT y obsérvalas con DTrace. Si necesitas entender la temporización entre eventos en un manejador de interrupciones, usa `ktr(9)`. Y si el bug está en una máquina de producción, consulta la Sección 7 antes de hacer cualquier cosa.

Dedicaremos el resto del capítulo a enseñar cada una de estas técnicas en profundidad. El driver `bugdemo` que vamos a conocer nos proporciona un lugar seguro donde aplicar todas ellas, con bugs conocidos que cazar y respuestas conocidas que encontrar.

### Conociendo el driver bugdemo

El driver `bugdemo` es un pequeño pseudo-dispositivo que usaremos como sujeto de laboratorio a lo largo del capítulo. No tiene hardware que gestionar. Expone un nodo de dispositivo en `/dev/bugdemo` y acepta un puñado de comandos `ioctl(2)` que desencadenan deliberadamente diferentes clases de bug: una desreferencia de puntero nulo, un acceso sin lock que `WITNESS` puede detectar, un use-after-free, una fuga de memoria, un bucle infinito dentro de un spinlock, y otros similares. Cada ioctl está controlado por un interruptor sysctl para que el driver pueda cargarse de forma segura en un sistema de desarrollo sin activar nada accidentalmente.

Presentaremos el driver formalmente en el Laboratorio 1, una vez que tengamos las macros de aserción en la mano. Por ahora, ten en cuenta que todas las técnicas que estudiamos pueden demostrarse en `bugdemo` con un punto de partida conocido y una respuesta conocida. Esa disciplina, reproducir bugs en un entorno controlado, es en sí misma una de las habilidades más importantes que este capítulo pretende enseñar.

Ahora estamos listos para comenzar con el conjunto de herramientas propiamente dicho, empezando por las macros de aserción que capturan los bugs en el momento exacto en que ocurren.

## 2. Uso de KASSERT, panic y macros relacionadas

La programación defensiva en espacio de usuario suele girar en torno a comprobaciones en tiempo de ejecución y un manejo cuidadoso de errores. La programación defensiva en el kernel añade una herramienta más: la macro de aserción. Una aserción establece una condición que debe ser verdadera en un momento dado. Si la condición es falsa, algo está muy mal, y la respuesta más segura es detener el kernel de inmediato, antes de que el estado incorrecto tenga oportunidad de propagarse. Las aserciones son la herramienta de depuración más barata y eficaz que FreeBSD nos ofrece, y deberían estar presentes en cualquier driver serio.

Empezaremos con las dos macros más importantes, `KASSERT(9)` y `panic(9)`, veremos un puñado de compañeras útiles y luego discutiremos cuándo es apropiada cada una.

> **Una nota sobre los números de línea.** Cuando el capítulo cita código de `kassert.h`, `kern_shutdown.c` o `cdefs.h`, el punto de referencia es siempre el nombre de la macro o función. `KASSERT`, `kassert_panic`, `panic` y `__dead2` seguirán siendo localizables por esos nombres en cualquier árbol FreeBSD 14.x, aunque las líneas a su alrededor cambien. El backtrace de ejemplo que verás más adelante, que cita pares `file:line` como `kern_shutdown.c:400`, refleja un árbol 14.3 en el momento de la escritura y no coincidirá línea a línea con un sistema actualizado recientemente. Busca el símbolo con grep en lugar de desplazarte hasta el número.

### KASSERT: una comprobación que desaparece en producción

`KASSERT` es el equivalente en el kernel del macro `assert()` de espacio de usuario, pero más inteligente. Recibe una condición y un mensaje. Si la condición es falsa, el kernel entra en panic con ese mensaje. Si el kernel se compiló sin la opción `INVARIANTS`, toda la comprobación desaparece en tiempo de compilación y no tiene ningún coste en tiempo de ejecución.

La macro reside en `/usr/src/sys/sys/kassert.h`. En un árbol de código fuente de FreeBSD 14.3 tiene este aspecto:

```c
#if (defined(_KERNEL) && defined(INVARIANTS)) || defined(_STANDALONE)
#define KASSERT(exp,msg) do {                                           \
        if (__predict_false(!(exp)))                                    \
                kassert_panic msg;                                      \
} while (0)
#else /* !(KERNEL && INVARIANTS) && !STANDALONE */
#define KASSERT(exp,msg) do { \
} while (0)
#endif /* KERNEL && INVARIANTS */
```

Cuatro detalles de esta definición merecen una pausa.

En primer lugar, la macro se define de manera diferente según si `INVARIANTS` está activado o no. Si no lo está, `KASSERT` se expande a un bloque vacío `do { } while (0)`, que el compilador elimina por completo. Un kernel de producción construido sin `INVARIANTS` no paga ningún coste en tiempo de ejecución por las llamadas a `KASSERT`, independientemente de cuántas contenga el driver. Esta es la propiedad que nos permite escribir aserciones generosas durante el desarrollo sin preocuparnos por el rendimiento en producción. La rama `_STANDALONE` permite que la misma macro funcione en el bootloader, donde `INVARIANTS` puede estar ausente pero la comprobación sigue siendo deseable.

En segundo lugar, la sugerencia `__predict_false` le indica al compilador que la condición es casi siempre verdadera. Esto mejora la generación de código para el camino habitual, porque el compilador organizará el salto de manera que el camino caliente no lo tome. Usar `__predict_false` es una de las pequeñas disciplinas de rendimiento que mantiene usable un kernel de depuración.

En tercer lugar, el cuerpo de una aserción fallida llama a `kassert_panic`, no a `panic` directamente. Este es un detalle de implementación para facilitar el análisis de los mensajes de aserción, pero tiene importancia cuando ves un mensaje de panic en la práctica: los fallos de `KASSERT` producen un prefijo distintivo que reconoceremos más adelante.

En cuarto lugar, observa que el argumento `msg` se pasa entre dobles paréntesis. Esto se debe a que la macro lo pasa directamente a `kassert_panic`, que tiene una firma de estilo `printf`. En la práctica se escribe así:

```c
KASSERT(ptr != NULL, ("ptr must not be NULL in %s", __func__));
```

Los paréntesis exteriores pertenecen a la macro. Los paréntesis interiores son la lista de argumentos de `kassert_panic`. Un error frecuente entre principiantes es escribir `KASSERT(ptr != NULL, "ptr is NULL")` con un solo par de paréntesis, lo que no compilará. Los dobles paréntesis son la disciplina que nos recuerda que una aserción fallida se formateará como un `printf`.

### INVARIANTS e INVARIANT_SUPPORT

`INVARIANTS` es la opción de construcción del kernel que controla si `KASSERT` está activo. Un kernel de depuración lo activa. La configuración `GENERIC-DEBUG` incluida con FreeBSD 14.3 lo activa mediante la inclusión de `std.debug`, que puedes ver en `/usr/src/sys/conf/std.debug`. Un kernel `GENERIC` de producción no lo activa.

Existe también una opción relacionada llamada `INVARIANT_SUPPORT`. `INVARIANT_SUPPORT` compila las funciones que las aserciones pueden llamar, sin hacerlas obligatorias. Esto permite que los módulos del kernel cargables construidos con `INVARIANTS` puedan cargarse en un kernel que no fue construido con `INVARIANTS`, siempre que `INVARIANT_SUPPORT` esté presente. Para el autor de un driver, la consecuencia práctica es esta: si construyes tu módulo con `INVARIANTS`, asegúrate de que el kernel en el que lo cargas tiene al menos `INVARIANT_SUPPORT`. El kernel `GENERIC-DEBUG` tiene ambas, lo que es una de las razones por las que recomendamos usarlo durante todo el desarrollo.

### MPASS: KASSERT con un mensaje predeterminado

Escribir un mensaje para cada aserción puede resultar tedioso, especialmente para invariantes sencillos. FreeBSD proporciona `MPASS` como abreviatura de `KASSERT(expr, ("Assertion expr failed at file:line"))`:

```c
#define MPASS(ex)               MPASS4(ex, #ex, __FILE__, __LINE__)
#define MPASS2(ex, what)        MPASS4(ex, what, __FILE__, __LINE__)
#define MPASS3(ex, file, line)  MPASS4(ex, #ex, file, line)
#define MPASS4(ex, what, file, line)                                    \
        KASSERT((ex), ("Assertion %s failed at %s:%d", what, file, line))
```

Las cuatro formas permiten personalizar el mensaje, el archivo, o ambos. La forma más sencilla, `MPASS(ptr != NULL)`, convierte la expresión en cadena y añade la ubicación automáticamente. Cuando el mensaje puede ser conciso, `MPASS` genera menos ruido visual en el código fuente. Cuando el mensaje necesita contexto que un lector futuro agradecerá, prefiere `KASSERT` con un mensaje escrito.

Una regla práctica razonable es que `MPASS` es para invariantes internos que nunca deberían ocurrir y donde la identidad de la expresión es autoexplicativa. `KASSERT` es para condiciones en las que el modo de fallo merece un mensaje descriptivo.

### CTASSERT: aserciones en tiempo de compilación

A veces la condición que quieres comprobar puede resolverse en tiempo de compilación. `sizeof(struct foo) == 64`, por ejemplo, o `MY_CONST >= 8`. Para esos casos, FreeBSD proporciona `CTASSERT`, también en `/usr/src/sys/sys/kassert.h`:

```c
#define CTASSERT(x)     _Static_assert(x, "compile-time assertion failed")
```

`CTASSERT` usa `_Static_assert` de C11. Produce un error en tiempo de compilación si la condición es falsa y tiene coste cero en tiempo de ejecución porque no existe tiempo de ejecución implicado. Esta es la herramienta ideal para comprobar la disposición de estructuras que deben mantenerse para que el driver sea correcto.

Un uso típico en el kernel es proteger una estructura frente a cambios accidentales de tamaño:

```c
struct bugdemo_command {
        uint32_t        op;
        uint32_t        flags;
        uint64_t        arg;
};

CTASSERT(sizeof(struct bugdemo_command) == 16);
```

Si alguien añade un campo más adelante sin ajustar el comentario de tamaño o sin reorganizar cuidadosamente, el build falla de inmediato. Esto es mucho mejor que descubrir en tiempo de ejecución que la estructura ha crecido y que el ioctl ya no coincide con las expectativas del espacio de usuario.

### panic: la parada incondicional

Mientras que `KASSERT` es una comprobación condicional, `panic` es la versión incondicional. Se llama cuando has decidido que continuar la ejecución sería peor que detenerse:

```c
void panic(const char *, ...) __dead2 __printflike(1, 2);
```

La declaración reside en `/usr/src/sys/sys/kassert.h` y la implementación en `/usr/src/sys/kern/kern_shutdown.c`. El atributo `__dead2` le indica al compilador que `panic` no retorna, lo que le permite generar mejor código a continuación. El atributo `__printflike(1, 2)` le indica que el primer argumento es una cadena de formato de estilo `printf`, de modo que el compilador puede comprobar el tipo del formato frente a sus argumentos.

¿Cuándo usarías `panic` directamente en lugar de `KASSERT`? Tres situaciones habituales. En primer lugar, cuando la condición es tan catastrófica que no existe continuación segura ni siquiera en un kernel de producción. No poder asignar un softc durante `attach`, por ejemplo, podría justificar un `panic` en lugar de una limpieza ordenada si el driver ya ha sido registrado parcialmente. En segundo lugar, cuando quieres que el mensaje aparezca incluso en builds sin depuración, porque el evento indica un fallo de hardware o de configuración que el usuario debe conocer. En tercer lugar, como marcador de posición durante el desarrollo inicial, para asegurarte de que los caminos supuestamente inalcanzables son realmente inalcanzables, antes de reemplazar el `panic` por un `KASSERT(0, ...)` en código maduro.

Algunos drivers en `/usr/src/sys/dev/` usan `panic` con moderación. Leer algunos ejemplos te dará una idea del tono: un mensaje de `panic` dice algo como «el controlador devolvió un estado imposible» o «hemos llegado a un caso que la máquina de estados afirma que no puede ocurrir». No es la respuesta habitual a un error de I/O. Es la respuesta a un invariante que ha sido violado de forma tan grave que no se puede confiar en que el driver continúe.

### __predict_false y __predict_true

Vimos `__predict_false` en la definición de `KASSERT`. Estas dos macros, definidas en `/usr/src/sys/sys/cdefs.h`, son sugerencias en tiempo de compilación para el predictor de saltos:

```c
#if __GNUC_PREREQ__(3, 0)
#define __predict_true(exp)     __builtin_expect((exp), 1)
#define __predict_false(exp)    __builtin_expect((exp), 0)
#else
#define __predict_true(exp)     (exp)
#define __predict_false(exp)    (exp)
#endif
```

No cambian la semántica de la expresión. Solo le indican al compilador qué resultado es más probable, lo que influye en cómo el compilador organiza el código. En un camino caliente, envolver una condición probablemente verdadera en `__predict_true` puede mejorar el comportamiento de la caché; envolver una probablemente falsa en `__predict_false` mantiene el código de manejo de errores fuera del camino rápido.

La primera regla para usar estas macros es ser correcto. Si la predicción es incorrecta, el código se ralentiza en lugar de acelerarse. La segunda regla es usarlas solo en caminos realmente calientes donde la diferencia importa. Para la mayor parte del código de drivers, las heurísticas predeterminadas del compilador son suficientes, y saturar el código con predicciones genera más problemas de los que resuelve.

### Dónde colocar las aserciones en un driver

Con estas macros en la mano, ¿dónde colocas realmente las aserciones? Unos pocos patrones han demostrado ser útiles en los drivers de FreeBSD.

El primero es en la entrada de la función, para precondiciones no triviales. Una función de driver que espera ser llamada con un determinado lock adquirido es un candidato perfecto:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        BUGDEMO_LOCK_ASSERT(sc);
        KASSERT(cmd != NULL, ("cmd must not be NULL"));
        KASSERT(cmd->op < BUGDEMO_OP_MAX,
            ("cmd->op %u out of range", cmd->op));
        /* ... */
}
```

`BUGDEMO_LOCK_ASSERT` es una convención de macro que adoptan muchos drivers y que envuelve una llamada a `mtx_assert(9)` o `sx_assert(9)`. Este patrón, en el que cada subsistema tiene su propia macro `_ASSERT` que comprueba su propio lock, escala bien en un driver de gran tamaño.

El segundo patrón aparece en las transiciones de estado. Si una máquina de estados del driver tiene cuatro estados válidos y un camino de ejecución en attach que solo debería ejecutarse en el estado `INIT`, una aserción al principio de attach detectará cualquier refactorización futura que rompa ese invariante:

```c
KASSERT(sc->state == BUGDEMO_STATE_INIT,
    ("attach called in state %d", sc->state));
```

El tercer patrón aparece después de aritmética delicada. Si un cálculo debe producir un valor dentro de un rango conocido, compruébalo:

```c
idx = (offset / PAGE_SIZE) & (SC_NRING - 1);
KASSERT(idx < SC_NRING, ("idx %u out of range", idx));
```

Esto es especialmente valioso en el código de ring buffer, donde un error de uno entre el productor y el consumidor puede provocar una corrupción de datos silenciosa.

El cuarto patrón corresponde a los punteros que podrían ser NULL pero no deberían serlo. Si una función recibe un argumento de tipo puntero que solo es válido cuando no es cero, un único `KASSERT(ptr != NULL, ...)` al principio de la función detecta años de mal uso futuro.

### Cuándo no usar aserciones

Las aserciones no son un sustituto del manejo de errores. La regla es: `KASSERT` comprueba aquello que el programador garantiza, no aquello que garantiza el entorno. Si una asignación de memoria con `M_NOWAIT` puede fallar cuando hay presión de memoria, no afirmes que tuvo éxito. Comprueba el valor de retorno y gestiona el fallo. Si un programa en espacio de usuario pasa una estructura más grande de lo esperado, devuelves `EINVAL`, no `KASSERT(0)`. La aserción sirve para garantizar la coherencia interna, no para validar entradas externas.

Otro antipatrón es usar aserciones para condiciones que solo se cumplen en determinadas configuraciones. `KASSERT(some_sysctl == default)` es incorrecto si `some_sysctl` puede ser ajustado por el usuario, porque la aserción fallará en cualquier sistema que lo haya modificado. Comprueba la configuración explícitamente y trátala de forma adecuada, o usa aserciones únicamente dentro de la rama donde la suposición realmente se cumple.

Un antipatrón más sutil es usar aserciones como documentación. "Así es como funciona, y más vale que siga siendo así" es un uso tentador de `KASSERT`, pero si la aserción solo se cumple hoy y podría razonablemente cambiar mañana, habrás creado un bug futuro para alguien que no recuerde tu promesa. Es mejor dejar un comentario que describa la suposición y dejar que el código evolucione. Las aserciones deben capturar invariantes permanentes, no decisiones de implementación temporales.

### Un pequeño ejemplo del mundo real

Veamos estas ideas aplicadas en código FreeBSD real. Abre `/usr/src/sys/dev/null/null.c` y observa una comprobación típica cerca del handler de lectura. El driver es extremadamente simple, así que hay pocas aserciones, pero muchos drivers en `/usr/src/sys/dev/` usan `KASSERT` con generosidad. Para un ejemplo más rico, consulta `/usr/src/sys/dev/uart/uart_bus_pci.c` o `/usr/src/sys/dev/mii/mii.c`, donde las aserciones al inicio de las funciones capturan a los llamantes que no mantienen los locks esperados.

La consistencia de este patrón a lo largo del árbol de código fuente no es accidental. Refleja una expectativa cultural de que los drivers expresarán sus invariantes en el código, no solo en los comentarios. Cuando adoptes el mismo hábito en tus propios drivers, te unes a esa cultura. Tus drivers serán más fáciles de portar, más fáciles de revisar y mucho más fáciles de depurar cuando algo acabe fallando.

### Un ejemplo rápido: añadir aserciones a bugdemo

Añadamos un pequeño conjunto de aserciones a un driver `bugdemo` imaginario. Supongamos que tenemos una estructura softc con un mutex, un campo de estado y un contador, y un handler `ioctl` que recibe un `struct bugdemo_command`.

```c
static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct bugdemo_softc *sc = dev->si_drv1;
        struct bugdemo_command *bcmd = (struct bugdemo_command *)data;

        KASSERT(sc != NULL, ("bugdemo: softc missing"));
        KASSERT(sc->state == BUGDEMO_STATE_READY,
            ("bugdemo: ioctl in state %d", sc->state));

        switch (cmd) {
        case BUGDEMO_TRIGGER:
                KASSERT(bcmd->op < BUGDEMO_OP_MAX,
                    ("bugdemo: op %u out of range", bcmd->op));
                BUGDEMO_LOCK(sc);
                bugdemo_process(sc, bcmd);
                BUGDEMO_UNLOCK(sc);
                return (0);
        default:
                return (ENOTTY);
        }
}
```

Cuatro aserciones, cada una capturando una clase diferente de bug futuro. La primera comprueba que el puntero privado del driver está realmente establecido, algo fácil de olvidar cuando `make_dev(9)` se llama con `NULL` por error. La segunda comprueba el estado del driver, que se disparará si alguien añade una ruta de código que puede alcanzar `ioctl` antes de que attach haya terminado. La tercera comprueba que la entrada proporcionada por el usuario está dentro del rango permitido, aunque en un contexto de producción esta comprobación también se haría como una validación real de la entrada que devuelve un error, porque `ioctl` es una interfaz pública. La cuarta, no mostrada aquí pero implícita en `bugdemo_process`, afirma que el lock está tomado.

Estas pocas líneas expresan muchos invariantes. En un kernel de depuración, capturarán bugs reales en el momento en que ocurran. En un kernel de producción, desaparecen por completo. Ese es el trato que ofrece `KASSERT`, y adoptarlo es uno de los mejores hábitos que puede cultivar un autor de drivers.

Con esta base establecida, podemos pasar a lo que ocurre cuando una aserción realmente se dispara, lo que nos lleva al camino del panic y el volcado de memoria.

## 3. Análisis de panics y volcados de memoria

Cuando un `KASSERT` falla o se llama a `panic`, el kernel sigue una serie de pasos bien definidos. Comprender esos pasos es la primera parte para entender un crash. La segunda parte es saber qué rastros deja el kernel y cómo leerlos después. Esta sección recorre ambas partes.

### Qué ocurre durante un panic

Un panic es el apagado controlado del kernel en respuesta a un error irrecuperable. La secuencia exacta depende de las opciones de compilación, pero un panic típico en un kernel FreeBSD 14.3 sigue este proceso.

En primer lugar, se llama a `panic()` o `kassert_panic()` con un mensaje. El mensaje se formatea y se escribe en el registro del sistema. Si hay una consola serie conectada, aparece allí de inmediato. Si solo hay disponible una consola gráfica, aparece en pantalla, aunque a menudo hay muy poco tiempo para leer una traza larga antes de que la máquina se reinicie, que es una de las razones por las que recomendamos una consola serie o virtual durante este capítulo.

En segundo lugar, el kernel captura un backtrace del thread que causó el panic. Lo verás en la consola como una lista de nombres de funciones con desplazamientos. El backtrace es la pieza de información más valiosa que produce un panic, porque te indica la cadena de llamadas que condujo al fallo. Leerlo de arriba abajo te muestra la función que llamó a `panic`, la función que llamó a esa, y así sucesivamente, hasta llegar al punto de entrada.

En tercer lugar, si el kernel se ha compilado con `KDB` habilitado y un backend como `DDB`, el kernel entra en el depurador. `DDB` es el depurador integrado en el kernel. Acepta comandos directamente en la consola: `bt` para mostrar un backtrace, `show registers` para volcar el estado de los registros, `show proc` para mostrar información de procesos, etcétera. Usaremos `DDB` brevemente en la Sección 4. Si `KDB` no está habilitado, o si el kernel está configurado para omitir el depurador en el panic, el kernel continúa.

En cuarto lugar, si hay un dispositivo de volcado configurado, el kernel escribe un volcado en él. El volcado es el contenido completo de la memoria del kernel, o al menos las partes marcadas como volcables, serializadas en el dispositivo de volcado. Este es el volcado de memoria que `savecore(8)` recuperará tras el reinicio.

En quinto lugar, el kernel reinicia la máquina, a menos que se le haya indicado que se detenga en el depurador. Tras el reinicio, cuando el sistema arranca, `savecore(8)` se ejecuta y escribe el volcado en `/var/crash/vmcore.N` junto con un resumen textual. Ahora tienes todo lo necesario para analizar el crash sin conexión.

Toda la secuencia puede durar desde una fracción de segundo hasta varios minutos, según el tamaño del kernel, la velocidad del dispositivo de volcado y la configuración del sistema. En una VM de desarrollo, volcar un kernel de unos cientos de megabytes en un disco virtual suele ser cuestión de segundos.

### Cómo leer un mensaje de panic

Un mensaje de panic en FreeBSD 14.3 tiene un aspecto similar a este:

```text
panic: bugdemo: softc missing
cpuid = 0
time = 1745188102
KDB: stack backtrace:
db_trace_self_wrapper() at db_trace_self_wrapper+0x2b
vpanic() at vpanic+0x182
panic() at panic+0x43
bugdemo_ioctl() at bugdemo_ioctl+0x24
devfs_ioctl() at devfs_ioctl+0xc2
VOP_IOCTL_APV() at VOP_IOCTL_APV+0x3f
vn_ioctl() at vn_ioctl+0xdc
devfs_ioctl_f() at devfs_ioctl_f+0x1a
kern_ioctl() at kern_ioctl+0x284
sys_ioctl() at sys_ioctl+0x12f
amd64_syscall() at amd64_syscall+0x111
fast_syscall_common() at fast_syscall_common+0xf8
--- syscall (54, FreeBSD ELF64, sys_ioctl), rip = ..., rsp = ...
```

Léelo de arriba abajo. La primera línea es el mensaje de panic en sí. Las líneas `cpuid` y `time` son metadatos; raramente son útiles para la depuración, pero ocasionalmente ayudan cuando se concilian múltiples registros. La línea `KDB: stack backtrace:` marca el inicio de la traza.

Los primeros frames son la propia infraestructura del panic: `db_trace_self_wrapper`, `vpanic`, `panic`. Siempre están presentes en un panic y se pueden omitir. El primer frame interesante es `bugdemo_ioctl`, que es donde nuestro driver llamó a `panic`. Los frames siguientes son el camino que nos llevó hasta `bugdemo_ioctl`: `devfs_ioctl`, `vn_ioctl`, `kern_ioctl`, `sys_ioctl`, `amd64_syscall`. Esto nos dice que el panic ocurrió durante una llamada al sistema ioctl, lo que ya es una pista útil. La línea final muestra el número de syscall (54, que corresponde a `ioctl`) y el puntero de instrucción en la entrada.

Los desplazamientos (`+0x24`, `+0xc2`) son desplazamientos en bytes dentro de cada función. Por sí solos no son legibles, pero permiten que `kgdb` resuelva la línea exacta del código fuente cuando el kernel de depuración está disponible.

Anotar este tipo de mensaje, o capturar el registro de la consola serie, es lo primero que debes hacer cuando ocurre un panic. Si la máquina se reinicia demasiado rápido para leer el mensaje, configura una consola serie o una consola virtual en modo texto donde se conserve el historial.

### Configuración del dispositivo de volcado

Para que `savecore(8)` tenga algo que recuperar, el kernel debe saber dónde escribir el volcado. FreeBSD denomina a esto el dispositivo de volcado, y `dumpon(8)` es la utilidad que lo configura.

Hay dos maneras habituales de configurar esto. La más sencilla es una partición de swap. Durante la instalación, `bsdinstall` crea normalmente una partición de swap suficientemente grande para la memoria del kernel, y FreeBSD 14.3 la configura automáticamente como dispositivo de volcado si habilitaste las opciones correspondientes. Puedes comprobarlo con:

```console
# dumpon -l
/dev/da0p3
```

Si ese comando lista tu dispositivo de swap, estás listo. Si indica que no hay ningún dispositivo de volcado configurado, puedes establecer uno manualmente:

```console
# dumpon /dev/da0p3
```

Para que sea persistente tras los reinicios, añádelo a `/etc/rc.conf`:

```sh
dumpdev="/dev/da0p3"
dumpon_flags=""
```

Puedes ver los valores predeterminados de estas variables en `/usr/src/libexec/rc/rc.conf`, que es la fuente autoritativa de todos los valores predeterminados de rc.conf en el sistema base. Busca con grep `dumpdev=` y `dumpon_flags=` para encontrar el bloque correspondiente.

Una alternativa, introducida en FreeBSD moderno, es usar un volcado respaldado por archivo. Esto evita la necesidad de dedicar una partición de disco a los volcados. Consulta `dumpon(8)` para conocer la sintaxis exacta; en resumen, puedes apuntar `dumpon` a un archivo en un sistema de archivos, y el kernel volcará en él cuando ocurra un panic. Los volcados respaldados por archivo son convenientes para las VMs de desarrollo donde no quieres reparticionar el disco.

Una segunda variable de rc.conf controla dónde coloca `savecore(8)` los volcados recuperados:

```sh
dumpdir="/var/crash"
savecore_enable="YES"
savecore_flags="-m 10"
```

El argumento `-m 10` conserva solo los diez volcados más recientes, lo cual es un valor predeterminado razonable. Si estás persiguiendo un bug poco frecuente, aumenta el número; si el espacio en disco es limitado, redúcelo. `savecore(8)` se ejecuta desde `/etc/rc.d/savecore` durante el arranque, antes de que la mayoría de los servicios estén activos, así que tu volcado queda preservado antes de que nada más acceda a `/var`.

### Habilitación de volcados en el kernel

Para que el kernel esté dispuesto a escribir un volcado, debe compilarse con las opciones correctas. En FreeBSD 14.3, el kernel `GENERIC` ya está configurado con los componentes del framework. Si miras `/usr/src/sys/amd64/conf/GENERIC` cerca del inicio del archivo, verás algo parecido a:

```text
options         KDB
options         KDB_TRACE
options         EKCD
options         DDB_CTF
```

`KDB` es el framework del depurador del kernel. `KDB_TRACE` habilita las trazas de pila automáticas en el panic. `EKCD` habilita los volcados de memoria del kernel cifrados, lo cual es útil cuando los volcados contienen datos sensibles. `DDB_CTF` indica al sistema de compilación que incluya información de tipos CTF para el depurador. Juntas, estas opciones producen un kernel con capacidad completa de volcado.

Observa lo que *no* está en `GENERIC`: `options DDB` y `options GDB` en sí mismos. El framework `KDB` está presente, pero el backend del depurador integrado en el kernel (`DDB`) y el stub remoto de GDB (`GDB`) los añade `std.debug`, que `GENERIC-DEBUG` incluye. Un kernel `GENERIC` simple seguirá escribiendo un volcado en el panic, pero si accedes a la consola en un sistema en ejecución, no habrá ningún prompt de `DDB` que te reciba.

Si estás compilando tu propio kernel, añade los backends explícitamente o, de forma más sencilla, empieza desde `GENERIC-DEBUG`, que los habilita junto con las opciones de depuración que necesitaremos para el resto del capítulo. `GENERIC-DEBUG` se encuentra en `/usr/src/sys/amd64/conf/GENERIC-DEBUG` y tiene solo dos líneas:

```text
include GENERIC
include "std.debug"
```

El archivo `std.debug` en `/usr/src/sys/conf/std.debug` añade `DDB`, `GDB`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, `WITNESS_SKIPSPIN`, `MALLOC_DEBUG_MAXZONES=8`, `ALT_BREAK_TO_DEBUGGER`, `DEADLKRES`, `BUF_TRACKING`, `FULL_BUF_TRACKING`, `QUEUE_MACRO_DEBUG_TRASH` y algunos indicadores de depuración específicos de subsistema. Ten en cuenta que `DDB` y `GDB` en sí mismos provienen de `std.debug`, no de `GENERIC`; el kernel de producción habilita `KDB` y `KDB_TRACE` pero deja los backends fuera a menos que optes por incluirlos. Este es el kernel de depuración recomendado para el desarrollo de drivers y el kernel que asumiremos en el resto del capítulo a menos que indiquemos lo contrario.

### Recuperación del volcado con savecore

Después de un panic y un reinicio, `savecore(8)` se ejecuta al principio de la secuencia de arranque. Cuando dispones de un prompt de shell, el volcado ya está en `/var/crash/`:

```console
# ls -l /var/crash/
total 524288
-rw-------  1 root  wheel         1 Apr 20 14:23 bounds
-rw-r--r--  1 root  wheel         5 Apr 20 14:23 minfree
-rw-------  1 root  wheel  11534336 Apr 20 14:23 info.0
-rw-------  1 root  wheel  11534336 Apr 20 14:23 info.last
-rw-------  1 root  wheel  524288000 Apr 20 14:23 vmcore.0
-rw-------  1 root  wheel  524288000 Apr 20 14:23 vmcore.last
```

El archivo `vmcore.N` es el volcado en sí. El archivo `info.N` es un resumen en texto del pánico, que incluye el mensaje de pánico, el backtrace y la versión del kernel. Lee siempre `info.N` primero. Si el mensaje y el backtrace son suficientes para identificar el bug, puede que no necesites ir más lejos.

Hay algunos problemas comunes a los que debes prestar atención. Si `ls` muestra únicamente `bounds` y `minfree`, todavía no se ha capturado ningún volcado. Esto suele significar que el dispositivo de volcado no está configurado o que el kernel no consiguió escribir en él antes de reiniciarse. Comprueba `dumpon -l` y provoca un nuevo pánico. Si `savecore` registra mensajes sobre una discrepancia en el checksum, el volcado fue truncado, lo que generalmente indica que el dispositivo de volcado era demasiado pequeño. Si la máquina nunca entró en pánico de forma limpia sino que simplemente se reinició, es probable que el kernel no tuviera `KDB` habilitado, por lo que no había mecanismo de volcado que invocar.

El archivo `info.N` es lo suficientemente corto como para leerlo completo. Incluye la versión del kernel, la cadena de pánico y el backtrace que el kernel capturó en el momento del pánico. En FreeBSD 14.3 tiene un aspecto parecido a este:

```text
Dump header from device: /dev/da0p3
  Architecture: amd64
  Architecture Version: 2
  Dump Length: 524288000
  Blocksize: 512
  Compression: none
  Dumptime: 2026-04-20 14:22:34 -0300
  Hostname: devbox
  Magic: FreeBSD Kernel Dump
  Version String: FreeBSD 14.3-RELEASE #0: ...
  Panic String: panic: bugdemo: softc missing
  Dump Parity: 3142...
  Bounds: 0
  Dump Status: good
```

Si el `Dump Status` es `good`, el volcado es utilizable. Si indica `bad`, el volcado fue truncado o el checksum falló.

### Cómo abrir un dump con kgdb

Una vez que tienes un dump, el siguiente paso es abrirlo con `kgdb`. `kgdb` es la versión de FreeBSD de `gdb`, especializada para imágenes del kernel. Necesita tres cosas: la imagen del kernel que generó el dump, la imagen del kernel de depuración que contiene los símbolos, y el propio archivo del dump. En la mayoría de los sistemas, los tres se encuentran en ubicaciones predecibles:

- El kernel en ejecución: `/boot/kernel/kernel`
- El kernel de depuración con todos los símbolos: `/usr/lib/debug/boot/kernel/kernel.debug`
- El dump: `/var/crash/vmcore.N`

La invocación más sencilla es:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

o de forma equivalente:

```console
# kgdb /usr/lib/debug/boot/kernel/kernel.debug /var/crash/vmcore.0
```

`kgdb` es una sesión de GDB normal con ajustes específicos para el kernel. Si el kernel se compiló con `makeoptions DEBUG=-g` (como hace `GENERIC-DEBUG`), los símbolos de depuración están incluidos y `kgdb` podrá resolver cada frame hasta el código fuente.

Cuando arranca `kgdb`, ejecuta automáticamente algunos comandos:

```console
(kgdb) bt
#0  __curthread () at /usr/src/sys/amd64/include/pcpu_aux.h:57
#1  doadump (textdump=...) at /usr/src/sys/kern/kern_shutdown.c:400
#2  0xffffffff80b6cf77 in kern_reboot (howto=260)
    at /usr/src/sys/kern/kern_shutdown.c:487
#3  0xffffffff80b6d472 in vpanic (fmt=..., ap=...)
    at /usr/src/sys/kern/kern_shutdown.c:920
#4  0xffffffff80b6d2c3 in panic (fmt=...)
    at /usr/src/sys/kern/kern_shutdown.c:844
#5  0xffffffff83e01234 in bugdemo_ioctl (dev=..., cmd=..., data=..., fflag=..., td=...)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:142
...
```

El frame superior corresponde a la infraestructura del panic. El frame interesante es el frame 5, `bugdemo_ioctl` en `bugdemo.c:142`. Para saltar a ese frame:

```console
(kgdb) frame 5
#5  0xffffffff83e01234 in bugdemo_ioctl (dev=..., cmd=...)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:142
142         KASSERT(sc != NULL, ("bugdemo: softc missing"));
```

`kgdb` imprime la línea de código fuente. Desde aquí puedes inspeccionar las variables locales con `info locals`, examinar `sc` directamente con `print sc`, o listar el código fuente circundante con `list`:

```console
(kgdb) print sc
$1 = (struct bugdemo_softc *) 0x0
```

Esto nos confirma que `sc` realmente es NULL, lo que corrobora el mensaje del panic. Ahora podemos averiguar por qué, lo que normalmente implica recorrer la pila hacia arriba para encontrar dónde debería haberse asignado `sc`:

```console
(kgdb) frame 6
```

y así sucesivamente. La secuencia de `frame N`, `print VAR`, `list` es el recurso fundamental en cualquier análisis con `kgdb`. Es la misma conversación que tiene un usuario de gdb con cualquier programa que falla, adaptada al kernel.

### Comandos útiles de kgdb

Más allá de `bt` y `frame`, un puñado de comandos cubre la mayoría de las sesiones de depuración.

- `info threads` lista todos los threads del sistema volcado. En un kernel moderno puede haber centenares de entradas. Cada uno tiene un número y un estado.
- `thread N` cambia a un thread concreto, como si ese thread hubiera sido el que provocó el panic. Esto es indispensable cuando se ha producido un deadlock y el thread que generó el panic no es el que sostiene el lock problemático.
- `bt full` imprime un backtrace con las variables locales de cada frame. Suele ser la forma más rápida de ver el estado de una función implicada en el panic.
- `info locals` muestra las variables locales del frame actual.
- `print *SOMETHING` desreferencia un puntero e imprime el contenido de la estructura a la que apunta.
- `list` muestra el código fuente alrededor de la línea actual; `list FUNC` muestra el código fuente de una función por su nombre.

Hay muchos más, documentados en `gdb(1)`, pero estos son los que un autor de drivers consulta con mayor frecuencia.

### Recorriendo struct thread en un dump

Un backtrace del panic responde a «¿dónde se produjo el fallo?», pero raramente responde a «¿quién estaba haciendo qué?». El kernel mantiene un registro detallado de todos los threads activos en `struct thread`, y una vez que el dump está abierto en `kgdb`, podemos leer ese registro directamente. Para un autor de drivers, el valor es concreto: los campos de `struct thread` indican qué tarea estaba realizando ese thread cuando el kernel falló, qué lock esperaba, a qué proceso pertenecía y si todavía estaba dentro de tu código cuando se produjo el panic.

`struct thread` se define en `/usr/src/sys/sys/proc.h`. Es una estructura grande, así que en lugar de leer todos sus campos, nos centramos en el subconjunto que más importa para la depuración de drivers. En `kgdb`, la forma más rápida de ver esos campos es tomar el thread actual y desreferenciarlo:

```console
(kgdb) print *(struct thread *)curthread
```

En la CPU que generó el panic, `curthread` ya es correcto, pero también puedes llegar a un thread concreto desde el listado de `info threads`. `kgdb` numera cada thread de forma secuencial. Una vez que conoces el número, `thread N` cambia el contexto y, desde ahí, `print *$td` (o `print *(struct thread *)0xADDR` si tienes la dirección en crudo) imprime la estructura.

Los campos que debes conocer son los siguientes. `td_name` es un nombre corto y legible del thread, establecido habitualmente por `kthread_add(9)` o por el programa en espacio de usuario que lo creó. Cuando un driver crea su propio thread del kernel, este es el nombre que aparece. `td_tid` es el identificador numérico del thread que asigna el kernel; `ps -H` en espacio de usuario muestra el mismo número. `td_proc` es un puntero al proceso propietario, lo que nos da acceso al `struct proc` más amplio para obtener más contexto. `td_flags` lleva el campo de bits `TDF_*` que registra el estado del planificador y del depurador; las definiciones se encuentran junto a la estructura en `/usr/src/sys/sys/proc.h`, y muchos panics pueden explicarse parcialmente leyendo esos bits. `td_lock` es el spin mutex que protege en ese momento el estado del planificador de este thread. En un kernel en ejecución casi siempre es un lock local a la CPU; en un dump, un `td_lock` que apunte a una dirección inesperada es una señal clara de que algo corrompió la visión del planificador sobre este thread.

Hay otros dos campos determinantes cuando el panic implica un estado de espera o bloqueo. `td_wchan` es el «canal de espera», la dirección del kernel sobre la que duerme el thread. `td_wmesg` es una cadena corta legible que describe el motivo (por ejemplo, `"biord"` para un thread que espera una lectura de buf, o `"select"` para un thread dentro de `select(2)`). Si el panic ocurrió mientras los threads estaban durmiendo, estos dos campos te indican qué esperaba cada uno. `td_state` es el valor de estado TDS_* (definido justo después de `struct thread`); indica si el thread estaba en ejecución, listo para ejecutarse o inhibido en el momento del fallo.

Para los fallos relacionados con locks en concreto, `td_locks` cuenta los locks no spin que el thread mantiene en ese momento, y `td_lockname` registra el nombre del lock sobre el que el thread está bloqueado actualmente, si hay alguno. Si un thread entra en panic con `td_locks` distinto de cero, ese thread mantenía uno o más sleep locks en el momento del fallo: información útil cuando el panic muestra un mensaje `mutex not owned` o `Lock (sleep mutex) ... is not sleepable`.

Una sesión breve de `kgdb` que extrae estos campos podría tener este aspecto:

```console
(kgdb) thread 42
[Switching to thread 42 ...]
(kgdb) set $td = curthread
(kgdb) print $td->td_name
$2 = "bugdemo_worker"
(kgdb) print $td->td_tid
$3 = 100472
(kgdb) print $td->td_state
$4 = TDS_RUNNING
(kgdb) print $td->td_wmesg
$5 = 0x0
(kgdb) print $td->td_locks
$6 = 1
(kgdb) print $td->td_proc->p_pid
$7 = 0
(kgdb) print $td->td_proc->p_comm
$8 = "kernel"
```

La lectura es la siguiente: el thread 42 era un thread del kernel llamado `bugdemo_worker`, en ejecución cuando se produjo el panic, sin dormir sobre nada (`td_wmesg` es NULL), y aún mantenía exactamente un sleep lock. El proceso propietario es el proc del kernel con pid 0 y nombre de comando `kernel`, que es el propietario esperado de los threads exclusivos del kernel. El dato relevante es `td_locks == 1`, porque nos indica que el thread mantenía un lock en el momento del panic; una consulta posterior con `show alllocks` en DDB, o con `show lockedvnods` si los file locks son relevantes, nos indicaría cuál exactamente.

### Recorriendo struct proc en un dump

Cada thread pertenece a un `struct proc`, definido junto a `struct thread` en `/usr/src/sys/sys/proc.h`. `struct proc` contiene el contexto del proceso: identidad, credenciales, espacio de direcciones, archivos abiertos y relación con el proceso padre. Para los fallos de drivers, un subconjunto de estos campos resulta especialmente útil.

`p_pid` es el identificador del proceso, el mismo número que ve el espacio de usuario en `ps`. `p_comm` es el nombre del comando del proceso, truncado a `MAXCOMLEN` bytes. Juntos te indican qué proceso en espacio de usuario disparó la ruta del kernel que generó el panic. `p_state` es el estado del proceso PRS_*, que permite distinguir un proceso recién creado mediante fork, uno en ejecución y un zombie. `p_numthreads` indica cuántos threads tiene este proceso; en el caso de un programa en espacio de usuario multithreaded que llamó a tu driver, la cifra puede sorprender. `p_flag` almacena los bits de indicador P_*, que codifican propiedades como el rastreo, la contabilidad y el modo de un único thread; `/usr/src/sys/sys/proc.h` documenta cada bit junto al bloque de indicadores.

Tres punteros te dan el contexto más amplio. `p_ucred` referencia las credenciales del proceso, útil cuando el panic podría estar relacionado con una comprobación de privilegios que realizó tu driver. `p_vmspace` apunta al espacio de direcciones, relevante cuando el panic implica un puntero de usuario que resultó pertenecer a un proceso inesperado. `p_pptr` apunta al proceso padre; recorrer esta cadena con `p_pptr->p_pptr` conduce eventualmente a `initproc`, el ancestro de todos los procesos en espacio de usuario.

Un breve recorrido de un thread hasta su proceso tiene este aspecto en `kgdb`:

```console
(kgdb) set $p = curthread->td_proc
(kgdb) print $p->p_pid
$9 = 3418
(kgdb) print $p->p_comm
$10 = "devctl"
(kgdb) print $p->p_state
$11 = PRS_NORMAL
(kgdb) print $p->p_numthreads
$12 = 4
(kgdb) print $p->p_flag
$13 = 536871424
```

Ahora sabemos que el panic ocurrió mientras un proceso `devctl` en espacio de usuario con pid 3418 estaba en ejecución, que el proceso tenía cuatro threads, y que sus bits de indicador decodificados mediante las constantes P_* en `/usr/src/sys/sys/proc.h` nos dirán si estaba siendo rastreado, si se estaba contabilizando o si estaba en medio de un exec. El entero de indicadores, por sí solo, resulta opaco, pero en `kgdb` puedes dejar que las macros P_*, similares a enumeraciones, hagan la decodificación mediante una conversión de tipo o usando `info macro P_TRACED`.

Para los drivers que exponen un dispositivo de caracteres, `p_fd` también merece atención. Apunta a la tabla de descriptores de archivo del proceso que llamó a tu driver y, en una sesión avanzada, puedes recorrerla para encontrar por qué descriptor llegó la llamada. Normalmente esto va más allá de lo que necesita un análisis inicial de fallo, pero el mecanismo vale la pena recordarlo para el raro fallo que depende de cómo el espacio de usuario tenía abierto el dispositivo.

Entre `struct thread` y `struct proc`, es posible reconstruir una cantidad asombrosa de contexto a partir de un dump que a primera vista solo muestra un mensaje de panic y un backtrace. El coste es leer `/usr/src/sys/sys/proc.h` una vez con atención; después, el mismo vocabulario estará a tu disposición en cada sesión de depuración durante el resto de tu carrera.

### Cómo usar kgdb sobre un kernel en vivo

Hasta ahora hemos tratado `kgdb` como una herramienta post-mortem: abrir un dump, explorarlo sin conexión y reflexionar a tu propio ritmo. `kgdb` también tiene un segundo modo, en el que se conecta a un kernel en ejecución a través de `/dev/mem` en lugar de a un dump guardado. Este modo es potente, pero también es la herramienta que más fácilmente se usa de forma incorrecta de toda la caja de herramientas de depuración, así que lo trataremos con advertencias explícitas.

La invocación es casi idéntica a la forma post-mortem, salvo que el «core» es `/dev/mem`:

```console
# kgdb /boot/kernel/kernel /dev/mem
```

Lo que ocurre realmente es que `kgdb` utiliza la librería libkvm para leer la memoria del kernel a través de `/dev/mem`. La interfaz está documentada en `/usr/src/lib/libkvm/kvm_open.3`, que deja clara la distinción: el argumento «core» puede ser un archivo producido por `savecore(8)` o `/dev/mem`, y en este último caso el objetivo es el kernel que está en ejecución.

Esto es realmente útil. Puedes inspeccionar variables globales, recorrer grafos de locks, examinar I/O en curso y confirmar si un sysctl que acabas de establecer ha tenido efecto. Puedes hacerlo sin reiniciar, sin interrumpir el servicio y sin necesidad de reproducir un fallo. En un sistema de desarrollo que está ejecutando una prueba de larga duración, suele ser la forma más rápida de responder a «¿qué está haciendo el driver en este momento?».

Los riesgos son reales. En primer lugar, el kernel está en ejecución mientras lees. Las estructuras de datos cambian mientras trabajas. Una lista enlazada que comienzas a recorrer puede perder una entrada a mitad del proceso; un contador que imprimes puede haberse incrementado entre el momento en que lo solicitaste y el momento en que `kgdb` lo mostró; un puntero que sigues puede reasignarse antes de que se complete la desreferencia. A diferencia de un dump, estás leyendo un objetivo en movimiento y, en ocasiones, verás estados que son transitoriamente inconsistentes.

En segundo lugar, `kgdb` sobre un kernel en vivo es estrictamente de solo lectura en uso práctico. Puedes leer memoria, imprimir estructuras y recorrer datos, pero no debes escribir en la memoria del kernel por esta vía. La interfaz libkvm no proporciona locking ni barreras, y una escritura no coordinada entraría en condición de carrera con el propio kernel. Trata cada operación a través de `/dev/mem` como una inspección, nunca como una modificación. Si quieres cambiar el estado del kernel en ejecución, usa `sysctl(8)` o `sysctl(3)`, carga un módulo o usa DDB desde la consola. Esos mecanismos están diseñados para coordinarse con el resto del kernel; las escrituras directas a través de `/dev/mem` no lo están.

En tercer lugar, la perturbación no es nula. Leer a través de `/dev/mem` puede generar tráfico de TLB y, en estructuras grandes, el coste es apreciable. Si también estás perfilando, atribuye el ruido en consecuencia.

Por último, el acceso a `/dev/mem` requiere privilegios de root, por razones obvias: cualquier proceso que pueda leer `/dev/mem` puede leer cualquier secreto que el kernel haya tenido alguna vez. En sistemas en producción, restringir esto es una cuestión de seguridad, y la política sobre quién puede ejecutar `kgdb` contra un kernel activo debe reflejar esa realidad.

Teniendo en cuenta estas advertencias, la orientación es sencilla. Prefiere un volcado de memoria (crash dump) para cualquier sesión en la que quieras tomarte tu tiempo, compartir el estado con un colega o garantizar la consistencia. Prefiere una sesión de `kgdb` en vivo para inspecciones rápidas de solo lectura en un sistema en funcionamiento, cuando la pregunta es pequeña y el coste de un reinicio sería elevado. Ante la duda, genera un volcado con `sysctl debug.kdb.panic=1` (si el sistema es prescindible) o con `dumpon` y un evento desencadenante deliberado, y realiza el análisis sobre la instantánea congelada. La instantánea seguirá ahí mañana; el kernel en ejecución, no.

### Una nota sobre símbolos y módulos

Cuando el driver que genera el pánico es un módulo cargable, `kgdb` también necesita la información de depuración del módulo. Si el módulo se encuentra en `/boot/modules/bugdemo.ko` y fue compilado con `DEBUG_FLAGS=-g`, los símbolos de depuración están incorporados en él. `kgdb` los cargará automáticamente cuando resuelva los frames de ese módulo.

Si el módulo reside en una ubicación no estándar, puede que tengas que indicarle a `kgdb` dónde encontrar su información de depuración:

```console
(kgdb) add-symbol-file /path/to/bugdemo.ko.debug ADDRESS
```

donde `ADDRESS` es la dirección de carga del módulo, que puedes encontrar en la salida de `kldstat(8)`. En la práctica, esto rara vez es necesario en un sistema FreeBSD moderno, porque `kgdb` busca en los lugares correctos por defecto.

Lo que sí debes evitar es mezclar kernels. Si el kernel en ejecución y el kernel de depuración provienen de compilaciones distintas, los símbolos no coincidirán y `kgdb` mostrará información confusa o incorrecta. Compila ambos desde el mismo árbol de código fuente o mantén pares coincidentes. En un sistema de desarrollo esto no suele ser un problema, porque compilas e instalas ambos a la vez.

### Reflexiones finales sobre los dumps

El crash dump es valioso porque preserva el estado del kernel en el momento del pánico. A diferencia de un sistema en ejecución, donde cada lectura altera el estado, un dump es una instantánea congelada. Puedes examinarlo todo el tiempo que quieras, retomarlo mañana, compartirlo con un compañero o comparar el estado con el código fuente. Incluso cuando ya hayas pasado a otros bugs, merece la pena conservar un dump de un fallo interesante, porque a menudo es el único registro de esa secuencia exacta de eventos.

Con la mecánica del pánico y el análisis de dumps ya vistos, podemos avanzar hacia las opciones de configuración del kernel que hacen que la depuración sea realmente cómoda. Ese es el tema de la Sección 4.

## 4. Construyendo un entorno de kernel apto para la depuración

Todo lo que hemos aprendido hasta ahora depende de tener habilitadas las opciones correctas del kernel. Un kernel `GENERIC` estándar es una configuración de producción: está optimizado para la velocidad, no incluye información de depuración y carece de las comprobaciones que detectan muchos bugs de drivers. Para el trabajo de este capítulo queremos lo contrario: un kernel lento pero exhaustivo, que lleve símbolos de depuración completos y que busque activamente bugs en lugar de confiar en que el driver se comporte correctamente. FreeBSD lo denomina `GENERIC-DEBUG`, y configurarlo es el tema de esta sección.

Recorreremos el proceso de compilar e instalar un kernel de depuración y luego examinaremos en detalle cada una de las opciones interesantes, incluidos los backends de depuración (`DDB`, `GDB`), las comprobaciones de invariantes (`INVARIANTS`, `WITNESS`), los depuradores de memoria (`DEBUG_MEMGUARD`, `DEBUG_REDZONE`) y los controles de consola que te permiten entrar al depurador desde el teclado.

### Compilando GENERIC-DEBUG

En un sistema FreeBSD 14.3 con `/usr/src/` poblado, compilar un kernel de depuración es una operación de tres comandos. Desde `/usr/src/`:

```console
# make buildkernel KERNCONF=GENERIC-DEBUG
# make installkernel KERNCONF=GENERIC-DEBUG
# reboot
```

El paso `buildkernel` tarda más que una compilación de lanzamiento porque se genera información de depuración y se compilan muchas más comprobaciones. En una VM modesta de cuatro núcleos suele tardar entre veinte y treinta minutos. `installkernel` coloca el resultado en `/boot/kernel/` y guarda el kernel anterior en `/boot/kernel.old/`, lo que sirve de red de seguridad si el nuevo kernel no arranca.

Tras el reinicio puedes confirmar el kernel en ejecución con `uname -v`:

```console
# uname -v
FreeBSD 14.3-RELEASE-p2 #0: ...
```

El `#0` indica un kernel compilado localmente. También puedes comprobar que las opciones de depuración están activas leyendo las entradas `sysctl debug`, a las que volveremos en breve.

### Qué activa GENERIC-DEBUG

Como vimos en la Sección 3, `GENERIC-DEBUG` es una configuración ligera que simplemente incluye `GENERIC` y `std.debug`. El contenido interesante está en `std.debug`, que merece la pena leer entero porque documenta la opinión del kernel sobre cómo deben ser las buenas opciones de depuración. En un árbol FreeBSD 14.3, el archivo está en `/usr/src/sys/conf/std.debug`, y las opciones principales son:

```text
options         BUF_TRACKING
options         DDB
options         FULL_BUF_TRACKING
options         GDB
options         DEADLKRES
options         INVARIANTS
options         INVARIANT_SUPPORT
options         QUEUE_MACRO_DEBUG_TRASH
options         WITNESS
options         WITNESS_SKIPSPIN
options         MALLOC_DEBUG_MAXZONES=8
options         VERBOSE_SYSINIT=0
options         ALT_BREAK_TO_DEBUGGER
```

Más un puñado de flags de depuración específicos de subsistemas para redes, USB, HID y CAM en los que no necesitamos detenernos. Examinemos cada una de las opciones relevantes para drivers.

Fíjate en algo que `std.debug` *no* contiene: `makeoptions DEBUG=-g`. Esa línea reside en el propio `GENERIC`, cerca del principio de `/usr/src/sys/amd64/conf/GENERIC`. Un kernel `GENERIC` de lanzamiento ya se compila con `-g`, porque el proceso de ingeniería de lanzamientos quiere que la información de depuración esté disponible incluso cuando `INVARIANTS` y `WITNESS` están desactivados. `GENERIC-DEBUG` hereda esto a través de su `include "GENERIC"`.

### makeoptions DEBUG=-g

Esto pasa `-g` al compilador para cada archivo del kernel, produciendo un kernel con información de depuración DWARF completa. `kgdb` utiliza esta información para traducir direcciones de vuelta a líneas de código fuente. Sin `-g`, `kgdb` puede mostrar nombres de funciones, pero no puede mostrar la línea de código fuente donde ocurrió el fallo, y `print someVariable` se convierte en `print *(char *)0xffffffff...` sin nombres simbólicos.

El coste es que el binario del kernel es más grande. En amd64, un kernel `GENERIC-DEBUG` de depuración es varias veces mayor que un kernel `GENERIC` sin depuración. Para una VM de desarrollo esto no importa. En un sistema de producción es a menudo la razón para mantener la información de depuración en un archivo separado (`/usr/lib/debug/boot/kernel/kernel.debug`) mientras el kernel en ejecución está despojado de símbolos.

### INVARIANTS and INVARIANT_SUPPORT

Los vimos en la Sección 2. `INVARIANTS` activa `KASSERT` y una serie de comprobaciones en tiempo de ejecución distribuidas por todo el kernel. Las funciones de todo `/usr/src/sys/` tienen bloques `#ifdef INVARIANTS` que comprueban cosas como «esta lista está bien formada», «este puntero apunta a una zona válida» o «este contador de referencias no es cero». Con `INVARIANTS` habilitado, estas comprobaciones se disparan en tiempo de ejecución. Sin él, se eliminan en tiempo de compilación.

Las comprobaciones consumen ciclos de CPU. Como cifra aproximada en hardware típico FreeBSD 14.3-amd64, un kernel `INVARIANTS` con carga intensa es aproximadamente un cinco a un veinte por ciento más lento que un kernel de lanzamiento, y a veces más en cargas de trabajo con muchas asignaciones de memoria. Esta es la razón por la que `INVARIANTS` no está habilitado en `GENERIC`. Para el desarrollo de drivers, este coste merece aceptarse a cambio de los bugs que detecta. Consulta el Apéndice F para ver una carga de trabajo reproducible que mide esta relación en tu propio hardware.

`INVARIANT_SUPPORT` compila las rutinas auxiliares que llaman las aserciones, sin activar las aserciones en el código base del kernel. Como se indicó antes, permite que los módulos compilados con `INVARIANTS` se carguen en kernels sin `INVARIANTS`. Casi siempre querrás ambas opciones.

### WITNESS: el verificador de orden de locks

`WITNESS` es una de las herramientas de depuración más eficaces del arsenal de FreeBSD. Rastrea cada operación de lock y cada dependencia de lock en el kernel, y emite una advertencia si detecta algún orden de locks que podría provocar un deadlock. Dado que los deadlocks son una clase de bug extremadamente difícil de detectar por otros medios, `WITNESS` es indispensable para cualquier driver que adquiera más de un lock.

Vale la pena entender cómo funciona `WITNESS`. Cada vez que un thread adquiere un lock, `WITNESS` anota qué otros locks ya mantiene ese thread. A partir de estas observaciones construye un grafo de orden de locks: «se ha visto que el lock A se adquiere antes que el lock B», y así sucesivamente. Si el grafo contiene alguna vez un ciclo, se trata de un deadlock potencial, y `WITNESS` imprime una advertencia en la consola con backtraces de las adquisiciones involucradas.

La salida tiene más o menos este aspecto:

```text
lock order reversal:
 1st 0xfffff80003abc000 bugdemo_sc_mutex (bugdemo_sc_mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:203
 2nd 0xfffff80003def000 sysctl_lock (sysctl_lock) @ /usr/src/sys/kern/kern_sysctl.c:1842
stack backtrace:
 #0 kdb_backtrace+0x71
 #1 witness_checkorder+0xc95
 #2 __mtx_lock_flags+0x8f
 ...
```

Léelo así: el `bugdemo_sc_mutex` de tu driver fue adquirido en primer lugar, y luego se observó que otro thread tomaba primero `sysctl_lock` y después `bugdemo_sc_mutex`. Se trata de un deadlock potencial, porque con suficiente actividad concurrente los dos threads podrían quedarse esperando el uno al otro. La solución siempre es la misma: establece un orden de locks consistente en todos los caminos que adquieran ambos locks y cúmplelo.

`WITNESS` no es barato. Añade contabilidad a cada adquisición y liberación de lock. En nuestro entorno de laboratorio, con un kernel bajo carga intensa con muchos locks, el coste puede acercarse al veinte por ciento; la cifra exacta varía según la cantidad de locking que realice la carga de trabajo. Pero los bugs que detecta son el tipo de bugs que destruyen la disponibilidad en producción cuando se escapan, así que la inversión merece la pena durante el desarrollo. Consulta el Apéndice F para ver una carga de trabajo reproducible que aísla este coste frente a un kernel de referencia.

`WITNESS_SKIPSPIN` desactiva `WITNESS` en los spin mutexes. Los spinlocks son habitualmente de corta duración y críticos para el rendimiento, por lo que comprobarlos añade coste donde más importa. La opción predeterminada es comprobarlos igualmente, pero `std.debug` deshabilita esa comprobación para mantener el kernel utilizable. Puedes volver a habilitarla si estás buscando específicamente bugs de spinlock.

### Un ejemplo práctico de condición de carrera: bug de orden de locks en bugdemo

Leer sobre `WITNESS` en abstracto es una cosa; detectar una inversión real de orden de locks en un driver que has escrito es otra. Esta subsección recorre un ciclo completo: introducimos un bug de orden deliberado en `bugdemo`, lo ejecutamos en un kernel `GENERIC-DEBUG`, leemos el informe de `WITNESS` y corregimos el bug. El ejemplo es breve, pero el patrón se repite en cada deadlock que depures.

Supón que nuestro driver `bugdemo` ha adquirido dos locks a medida que ganaba funcionalidades. `sc_mtx` protege el estado por unidad, y `cfg_mtx` protege un blob de configuración compartido entre unidades. La mayor parte del driver ya los adquiere en el orden «estado primero, configuración después», que es una elección razonable y que el autor ha seguido en `bugdemo_ioctl` y en los puntos de entrada de lectura/escritura. Pero un handler de sysctl reciente, escrito con prisa, adquirió primero el lock de configuración para validar un valor y luego tomó el lock de estado para aplicarlo. En el código fuente, los dos fragmentos relevantes tienen este aspecto:

```c
/* bugdemo_ioctl: established ordering, state then config */
mtx_lock(&sc->sc_mtx);
/* inspect per-unit state */
mtx_lock(&cfg_mtx);
/* adjust shared config */
mtx_unlock(&cfg_mtx);
mtx_unlock(&sc->sc_mtx);
```

```c
/* bugdemo_sysctl_set: new path, config then state */
mtx_lock(&cfg_mtx);
/* validate new value */
mtx_lock(&sc->sc_mtx);
/* propagate into per-unit state */
mtx_unlock(&sc->sc_mtx);
mtx_unlock(&cfg_mtx);
```

Cada camino por separado está bien. El problema es que, juntos, forman un ciclo. Si el thread A entra en `bugdemo_ioctl` y adquiere `sc_mtx`, y el thread B entra concurrentemente en `bugdemo_sysctl_set` y adquiere `cfg_mtx`, cada thread queda en espera del lock que mantiene el otro. Esto es un deadlock AB-BA clásico. Puede que no se produzca en cada ejecución; depende del momento concreto. `WITNESS` es la herramienta que se niega a esperar a un fallo raro en producción para descubrirlo.

En un kernel `GENERIC-DEBUG`, la inversión se detecta la primera vez que se observan ambos órdenes, aunque aún no haya ocurrido ningún deadlock real. El mensaje de consola tiene una forma específica. Usando el formato emitido por `witness_output` en `/usr/src/sys/kern/subr_witness.c`, que imprime el puntero, el nombre del lock, el nombre de witness, la clase de lock y la ubicación en el código fuente para cada lock involucrado, el informe real tiene este aspecto:

```text
lock order reversal:
 1st 0xfffff80012345000 bugdemo sc_mtx (bugdemo sc_mtx, sleep mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:412
 2nd 0xfffff80012346000 bugdemo cfg_mtx (bugdemo cfg_mtx, sleep mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:417
lock order bugdemo cfg_mtx -> bugdemo sc_mtx established at:
 #0 witness_checkorder+0xc95
 #1 __mtx_lock_flags+0x8f
 #2 bugdemo_sysctl_set+0x7a
 #3 sysctl_root_handler_locked+0x9c
 ...
stack backtrace:
 #0 kdb_backtrace+0x71
 #1 witness_checkorder+0xc95
 #2 __mtx_lock_flags+0x8f
 #3 bugdemo_ioctl+0xd4
 ...
```

Cada línea `1st` y `2nd` contiene cuatro datos. El puntero (`0xfffff80012345000`) es la dirección del objeto lock en la memoria del kernel. La primera cadena es el nombre de instancia, establecido cuando se inicializó el lock. Las dos cadenas entre paréntesis son el nombre `WITNESS` para la clase de lock y la clase de lock en sí, en este caso `sleep mutex`. La ruta y la línea son el lugar donde el lock fue adquirido por última vez en este orden invertido. El bloque tras `lock order ... established at:` muestra el backtrace anterior que enseñó por primera vez a `WITNESS` el orden ahora violado, y el `stack backtrace` final muestra la ruta de llamada actual que lo viola.

Leyendo todo esto, el diagnóstico es inmediato. El driver ha establecido `sc_mtx -> cfg_mtx` en sus caminos normales, y `bugdemo_sysctl_set` acaba de tomar `cfg_mtx -> sc_mtx`. Ambos caminos son nuestros. La solución es elegir un orden (aquí, el establecido) y reescribir el camino problemático para que coincida:

```c
/* bugdemo_sysctl_set: corrected to follow house ordering */
mtx_lock(&sc->sc_mtx);
mtx_lock(&cfg_mtx);
/* validate new value and propagate in one atomic window */
mtx_unlock(&cfg_mtx);
mtx_unlock(&sc->sc_mtx);
```

Si la región con lock necesita ser más estrecha, un patrón habitual es leer el estado bajo `sc_mtx`, soltarlo, validar sin mantener ningún lock y luego readquirir en el orden establecido para aplicar los cambios. En cualquier caso, el orden se fija a nivel del driver, no en el punto de llamada. Un buen hábito es documentar el orden en un comentario cerca de las declaraciones de los locks para que futuros colaboradores no tengan que redescubrirlo.

Tras la corrección, volver a compilar `bugdemo` en el mismo kernel de depuración y ejecutar de nuevo el test que provocaba el problema no genera más salida de `WITNESS`. Si la inversión reaparece, `WITNESS` también permite consultar el grafo de forma interactiva con `show all_locks` en DDB, lo que puede mostrar el estado actual incluso sin un informe completo de inversión; para una introspección más profunda, el código fuente de `/usr/src/sys/kern/subr_witness.c` es la referencia definitiva tanto sobre el seguimiento interno como sobre el formato del informe.

### El mismo error visto a través de lockstat(1)

`WITNESS` te indica que un orden es incorrecto, pero no te dice con qué frecuencia hay contención real sobre cada lock, cuánto tarda en completarse cada adquisición ni qué llamadores presionan más un lock determinado. Esas son preguntas sobre contención, no sobre corrección, y `lockstat(1)` es la herramienta adecuada para ellas.

`lockstat(1)` es un profiler respaldado por DTrace para los locks del kernel. Funciona instrumentando los puntos de entrada y salida de las primitivas de lock y elaborando resúmenes que incluyen el tiempo de spin en mutex adaptativos, el tiempo de sleep en sx locks y el tiempo de retención cuando se solicita. La invocación clásica es `lockstat sleep N`, que recoge datos durante N segundos y a continuación imprime un resumen.

Si ejecutamos el `bugdemo` defectuoso bajo una carga de trabajo que estrese ambos caminos (un pequeño programa en espacio de usuario que abre varios nodos de unidad y al mismo tiempo manipula el sysctl en un bucle ajustado) y perfilamos con `lockstat` durante cinco segundos, la salida en un sistema FreeBSD tiene un aspecto similar al siguiente:

```console
# lockstat sleep 5

Adaptive mutex spin: 7314 events in 5.018 seconds (1458 events/sec)

Count indv cuml rcnt     nsec Lock                   Caller
-------------------------------------------------------------------------------
3612  49%  49% 0.00     4172 bugdemo sc_mtx         bugdemo_ioctl+0xd4
2894  40%  89% 0.00     3908 bugdemo cfg_mtx        bugdemo_sysctl_set+0x7a
 412   6%  95% 0.00     1205 bugdemo sc_mtx         bugdemo_read+0x2f
 220   3%  98% 0.00      902 bugdemo cfg_mtx        bugdemo_ioctl+0xe6
 176   2% 100% 0.00      511 Giant                  sysctl_root_handler_locked+0x4d
-------------------------------------------------------------------------------

Adaptive mutex block: 22 events in 5.018 seconds (4 events/sec)

Count indv cuml rcnt     nsec Lock                   Caller
-------------------------------------------------------------------------------
  14  63%  63% 0.00   184012 bugdemo sc_mtx         bugdemo_sysctl_set+0x8b
   8  36% 100% 0.00    41877 bugdemo cfg_mtx        bugdemo_ioctl+0xe6
-------------------------------------------------------------------------------
```

Cada tabla sigue la misma convención de columnas: `Count` es el número de eventos de ese tipo observados, `indv` es el porcentaje de eventos en esa clase, `cuml` es el total acumulado, `rcnt` es el recuento de referencias promedio (siempre 1 para mutex), `nsec` es la duración media en nanosegundos, y las dos últimas columnas identifican la instancia del lock y el llamador. La línea de cabecera `Adaptive mutex spin` indica contención resuelta mediante un spin corto; `Adaptive mutex block` indica contención que obligó a un thread a dormir sobre el mutex. Esas cabeceras, junto con el diseño de columnas, forman la salida estándar de `lockstat`; el formato está documentado en `/usr/src/cddl/contrib/opensolaris/cmd/lockstat/lockstat.1`, junto con ejemplos resueltos al final de esa página de manual.

Hay dos cosas que merece la pena destacar. En primer lugar, tanto `bugdemo sc_mtx` como `bugdemo cfg_mtx` aparecen en ambas direcciones de la tabla: el camino del sysctl se bloqueó sobre `sc_mtx` (línea 1 de la tabla de bloqueos), y el camino del ioctl se bloqueó sobre `cfg_mtx` (línea 2). Esa es la firma de contención del mismo error de ordenación que `WITNESS` notificó, vista desde el otro lado. `WITNESS` nos dijo que el orden era inseguro; `lockstat` nos dice que, bajo esta carga de trabajo, ese orden inseguro también tiene un coste real en tiempo.

En segundo lugar, una vez aplicada la corrección de la subsección anterior, `lockstat` se convierte en una herramienta de validación: vuelve a ejecutarlo con la misma carga de trabajo y la tabla `Adaptive mutex block` debería reducirse drásticamente, porque la espera mutua entre los dos caminos ha desaparecido. Si no se reduce, hemos corregido el orden pero hemos creado un problema de contención pura, y el siguiente paso es estrechar la sección crítica en lugar de cambiar el orden.

Entre las opciones útiles de `lockstat` más allá de los valores predeterminados destacan `-H` para observar eventos de retención (cuánto tiempo se mantiene un lock, no solo si hay contención), `-D N` para mostrar únicamente las N filas superiores por tabla, `-s 8` para incluir trazas de pila de ocho fotogramas con cada fila, y `-f FUNC` para filtrar por una única función. Para trabajo con drivers, `lockstat -H -s 8 sleep 10` mientras se ejecuta una prueba dirigida es un punto de partida extraordinariamente productivo.

### Usar WITNESS y lockstat conjuntamente

`WITNESS` y `lockstat` son herramientas complementarias. `WITNESS` es una herramienta de corrección: detecta errores que acabarán produciendo un deadlock, independientemente de que la carga de trabajo actual los active o no. `lockstat` es una herramienta de rendimiento: cuantifica cuánto tráfico actual toca cada lock y cuánto tiempo espera ese tráfico. El mismo camino de un driver aparece a menudo en ambas herramientas, y las dos perspectivas juntas suelen ser definitivas.

Una disciplina útil cuando un driver supera su primer lock es incorporar ambas herramientas a la rutina habitual. Ejecuta `GENERIC-DEBUG` durante el desarrollo para que `WITNESS` observe cada nuevo camino de código en el momento en que se ejecuta. Ejecuta `lockstat` periódicamente sobre una carga de trabajo realista para comprobar si alguno de tus locks está convirtiéndose en un cuello de botella aunque su orden sea correcto. Un lock que supera `WITNESS` y muestra una contención baja en `lockstat` es un lock del que puedes dejar de preocuparte en gran medida. Un lock que supera `WITNESS` pero domina la salida de `lockstat` es un problema de rendimiento que espera una refactorización, no un error de corrección. Un lock que falla en `WITNESS` es un error independientemente de lo que diga `lockstat`.

Con ese marco en mente, podemos continuar examinando las demás opciones del kernel de depuración que ponen de manifiesto distintas clases de errores.

### MALLOC_DEBUG_MAXZONES

El asignador de memoria del kernel de FreeBSD (`malloc(9)`) agrupa las asignaciones similares en zonas para mayor velocidad. `MALLOC_DEBUG_MAXZONES=8` aumenta el número de zonas que utiliza `malloc`, lo que distribuye las asignaciones entre regiones de memoria más diferenciadas. El efecto práctico es que los errores de uso tras liberación y de liberación inválida tienen más probabilidades de caer en una zona diferente a la de la asignación original, lo que los hace más detectables.

Es una opción de bajo coste y siempre está activada en los kernels de depuración.

### ALT_BREAK_TO_DEBUGGER and BREAK_TO_DEBUGGER

Estas dos opciones controlan cómo accede el usuario al depurador del kernel desde la consola. `BREAK_TO_DEBUGGER` activa la secuencia tradicional `Ctrl-Alt-Esc` o la secuencia BREAK serie. `ALT_BREAK_TO_DEBUGGER` activa una secuencia alternativa, escrita como `CR ~ Ctrl-B`, que resulta útil en consolas de red (ssh, virtio_console, etc.) donde enviar un BREAK real es incómodo.

`GENERIC` incluye `BREAK_TO_DEBUGGER` activado. `GENERIC-DEBUG` añade `ALT_BREAK_TO_DEBUGGER`. Si estás en una consola serie, cualquiera de las dos secuencias te llevará a `DDB`. Desde `DDB` puedes inspeccionar el estado del kernel, establecer puntos de interrupción y, opcionalmente, continuar la ejecución o provocar un panic.

Esta es una comodidad importante durante el desarrollo. Un driver que bloquea el sistema sin provocar un panic puede investigarse entrando al depurador a voluntad.

### DEADLKRES: el detector de deadlocks

`DEADLKRES` activa el resolutor de deadlocks, que es un thread periódico que vigila los threads atascados en espera ininterrumpible durante demasiado tiempo. Si encuentra alguno, imprime una advertencia y opcionalmente provoca un panic. Esto complementa a `WITNESS` al detectar deadlocks que `WITNESS` no predijo, lo que ocurre cuando el grafo de locks no se puede recorrer de forma estática (por ejemplo, cuando los locks se adquieren por dirección a través de una API de bloqueo genérica).

`DEADLKRES` tiene algunos falsos positivos en la práctica, especialmente en operaciones de larga duración como I/O de sistema de archivos bajo carga elevada. Leer la advertencia y decidir si se trata de un deadlock real forma parte de la habilidad de depuración que este capítulo enseña.

### BUF_TRACKING

`BUF_TRACKING` registra un breve historial de operaciones sobre cada buffer en la caché de buffers. Cuando se detecta una corrupción, puede imprimirse el historial del buffer, mostrando qué caminos de código lo tocaron y en qué orden. Esto es útil para errores de drivers de almacenamiento, pero raramente necesario en otros drivers.

### QUEUE_MACRO_DEBUG_TRASH

Las macros de `queue(3)` (`LIST_`, `TAILQ_`, `STAILQ_`, etc.) se utilizan de forma generalizada en el kernel para listas enlazadas. Cuando se elimina un elemento de una lista, el comportamiento habitual es dejar intactos los punteros del elemento. `QUEUE_MACRO_DEBUG_TRASH` los sobreescribe con valores basura reconocibles. Cualquier intento posterior de desreferenciar esos punteros provocará un fallo de forma reconocible, en lugar de corromper la lista silenciosamente.

Es una opción barata y detecta una clase de error muy común: olvidar eliminar un elemento de una lista antes de liberarlo, para encontrar la lista corrompida más adelante.

### Depuradores de memoria: DEBUG_MEMGUARD y DEBUG_REDZONE

Otras dos opciones que merecen atención son `DEBUG_MEMGUARD` y `DEBUG_REDZONE`. No forman parte de `std.debug`, pero se añaden habitualmente en sesiones de depuración de memoria.

`DEBUG_MEMGUARD` es un asignador especializado que puede sustituir al asignador habitual para tipos concretos de `malloc(9)`. Respalda cada asignación con una página separada o un conjunto de páginas, marca las páginas en torno a la asignación como inaccesibles y desmapea la asignación al liberarla. Cualquier acceso fuera de los límites de la asignación, o cualquier acceso tras la liberación, provoca un fallo de página que resulta trivial de diagnosticar. El inconveniente es que cada asignación consume una página completa de memoria virtual más contabilidad del kernel, por lo que normalmente se activa `DEBUG_MEMGUARD` para un único tipo de malloc a la vez.

La cabecera correspondiente es `/usr/src/sys/vm/memguard.h`, y la configuración aparece en `/usr/src/sys/conf/NOTES` en la línea `options DEBUG_MEMGUARD`. Utilizaremos `memguard(9)` con detalle en la sección 6.

`DEBUG_REDZONE` es un depurador de memoria más ligero que coloca bytes de guarda antes y después de cada asignación. Cuando se libera la asignación, se verifican los bytes de guarda y se notifica cualquier corrupción. No detecta el uso tras liberación, pero es muy eficaz para detectar desbordamientos de buffer por exceso y por defecto. Consulta la línea `options DEBUG_REDZONE` en `/usr/src/sys/conf/NOTES` para la configuración.

Tanto `DEBUG_MEMGUARD` como `DEBUG_REDZONE` consumen memoria. En un kernel de depuración sobre una VM de desarrollo, ambas se activan con frecuencia. En un servidor de producción grande, ninguna de las dos.

### KDB, DDB y GDB juntos

Hemos mencionado estas tres opciones a lo largo de este capítulo. Conviene aclarar la distinción, porque confunde a muchos principiantes.

`KDB` es el framework del depurador del kernel. Es la fontanería. Define los puntos de entrada que el resto del kernel invoca cuando se produce un panic o un evento de entrada al depurador. También define una interfaz para los backends.

`DDB` y `GDB` son dos de esos backends. `DDB` es el depurador interactivo dentro del kernel. Cuando se activa `KDB_ENTER` y `DDB` es el backend seleccionado, caes en un prompt interactivo sobre la consola. `DDB` dispone de un pequeño conjunto de comandos: `bt`, `show`, `print`, `break`, `step`, `continue` y algunos más. Es primitivo pero autónomo: no se necesita ningún otro equipo.

`GDB` es el backend remoto. Cuando se activa `KDB_ENTER` y `GDB` es el backend seleccionado, el kernel espera a que un cliente GDB remoto se conecte a través de una línea serie o una conexión de red. El cliente se ejecuta en un equipo diferente y envía comandos mediante un protocolo llamado GDB remote serial protocol. Esto es mucho más flexible porque dispones de `gdb` completo en el lado del cliente, pero requiere un segundo equipo (u otra VM) y una conexión entre ambos.

En la práctica, se activan ambos backends y se conmuta entre ellos en tiempo de ejecución. `sysctl debug.kdb.current_backend` indica el backend activo. `sysctl debug.kdb.supported_backends` lista todos los backends compilados. Puedes establecer `debug.kdb.current_backend` en `ddb` o `gdb` según el tipo de sesión que desees. Es una comodidad útil, porque el coste de tener ambos compilados es insignificante en comparación con la flexibilidad que ofrecen.

El soporte de KDB en `GENERIC` es suficiente para la mayoría de los panics. Usaremos `GDB` en la sección 7 cuando hablemos de depuración remota.

### KDB_UNATTENDED

Otra opción que merece mención es `KDB_UNATTENDED`. Esta opción hace que el kernel omita la entrada al depurador en caso de panic y pase directamente al volcado y al reinicio. En sistemas de producción sin nadie vigilando la consola, es un valor predeterminado razonable; no tiene sentido esperar indefinidamente una interacción con el depurador que nunca va a producirse. Durante el desarrollo, normalmente querrás lo contrario: permanecer en `DDB` después de un panic para poder investigar antes de que el estado se pierda con el reinicio. Establece esta opción mediante el sysctl `debug.debugger_on_panic` en tiempo de ejecución, o con `options KDB_UNATTENDED` en una configuración del kernel.

### CTF y rutas de información de depuración

El último elemento del entorno de depuración es CTF, el Compact C Type Format. CTF es una representación comprimida de la información de tipos que DTrace utiliza para comprender las estructuras del kernel. `GENERIC` incluye `options DDB_CTF`, que indica al proceso de construcción que genere información CTF para el kernel. En un kernel de depuración, la información CTF permite a DTrace imprimir los campos de las estructuras por nombre en lugar de por desplazamientos hexadecimales, lo que hace que su salida sea mucho más útil.

Puedes confirmar la presencia de CTF con `ctfdump`:

```console
# ctfdump -t /boot/kernel/kernel | head
```

Si esto produce salida, el kernel tiene CTF. Si no, o bien el build no incluyó `DDB_CTF` o bien la herramienta de generación CTF (`ctfconvert`) no estaba instalada. En FreeBSD 14.3 ambas son estándar.

Para los módulos, necesitas compilar con `WITH_CTF=1` en tu entorno (o pasarlo a `make`) para obtener la información CTF del módulo. Esto es lo que permite a DTrace comprender las estructuras que define tu driver.

### Confirmando tu kernel de depuración

Cuando arrancas por primera vez con un kernel de depuración, dedica un momento a verificar que las opciones que te interesan están realmente activas. Sysctls de utilidad:

```console
# sysctl debug.kdb.current_backend
debug.kdb.current_backend: ddb
# sysctl debug.kdb.supported_backends
debug.kdb.supported_backends: ddb gdb
# sysctl debug.debugger_on_panic
debug.debugger_on_panic: 1
# sysctl debug.ddb.
debug.ddb.capture.inprogress: 0
debug.ddb.capture.bufsize: 0
...
```

Si devuelven valores coherentes, tu kernel de depuración está correctamente configurado. Si `debug.kdb.supported_backends` muestra solo `ddb` pero esperabas `gdb`, hay algo mal en tu configuración. Vuelve atrás y comprueba que `options GDB` esté en la configuración de tu kernel o en `std.debug`.

### Trabajando sobre el kernel de depuración

Con el kernel de depuración en ejecución, el resto de las técnicas del capítulo pasan a estar disponibles. `KASSERT` realmente dispara. `WITNESS` realmente se queja del orden de los locks. `DDB` está ahí cuando pulsas la secuencia de interrupción al depurador. Los volcados de memoria incluyen información de depuración completa que `kgdb` puede usar para mostrar líneas de código fuente. Has pasado de un kernel que confía en el driver a un kernel que te ayuda activamente a demostrar que el driver es correcto.

Una consecuencia pequeña pero significativa de ejecutar un kernel de depuración durante el desarrollo de drivers es que verás los bugs mucho antes, antes de que lleguen al campo, y te resultará más fácil corregirlos cuando aparezcan. La disciplina de desarrollar siempre sobre un kernel de depuración, incluso cuando estás escribiendo código sencillo, es uno de los hábitos que separa los drivers de aficionado casual de los drivers suficientemente fiables para un uso serio.

Con el entorno configurado, podemos pasar al siguiente grupo de herramientas: el trazado. A diferencia de las aserciones, que capturan fallos, el trazado registra lo que ocurre para que puedas comprender la forma de un bug incluso cuando no provoca un fallo. Ese es el tema de la Sección 5.

## 5. Trazado del comportamiento del driver: DTrace, ktrace y ktr(9)

Las aserciones capturan lo que está mal. El trazado muestra lo que está ocurriendo. Cuando un driver se comporta de forma incorrecta sin llegar a fallar, o cuando necesitas comprender el orden preciso de los eventos a lo largo de varios threads, el trazado suele ser la herramienta adecuada. FreeBSD ofrece tres mecanismos de trazado complementarios para el código del kernel: DTrace, `ktrace(1)` y `ktr(9)`. Cada uno tiene su punto fuerte, y quien escribe drivers debe saber cuándo recurrir a cada uno.

El Capítulo 33 presentó DTrace como herramienta de medición del rendimiento. Aquí volvemos a él como herramienta de depuración de corrección, porque el mismo framework que puede agregar funciones calientes también puede seguir un bug a través del kernel. También conoceremos `ktr(9)`, el anillo de trazado ligero en el kernel, y `ktrace(1)`, que traza las llamadas al sistema desde el espacio de usuario.

### DTrace para la depuración de corrección

DTrace es el framework de trazado dinámico de nivel productivo de FreeBSD. Funciona permitiéndote adjuntar pequeños scripts a puntos de sonda (probes) a lo largo de todo el kernel. Una sonda es un punto con nombre en el código que puede instrumentarse. Cuando una sonda se activa, el script se ejecuta. Si el script tiene información útil que registrar, la registra; si no, la sonda es prácticamente gratuita.

El Capítulo 33 usó DTrace con el proveedor `profile` para el muestreo de CPU. En este capítulo utilizaremos distintos proveedores con distintos propósitos: `fbt` (trazado de límites de función) para seguir la entrada y salida de funciones, `sdt` (trazado definido estáticamente) para activar sondas colocadas explícitamente en nuestro driver, y `syscall` para observar las transiciones entre el espacio de usuario y el kernel.

Veremos cada uno por turno.

### El proveedor fbt

El proveedor `fbt` te da una sonda en cada entrada y salida de función del kernel. Para listar todas las sondas fbt de nuestro driver:

```console
# dtrace -l -P fbt -m bugdemo
```

Cada función produce dos sondas, una `entry` y una `return`. Puedes adjuntar acciones a cualquiera de las dos. Un primer paso habitual al depurar un bug nuevo es simplemente ver qué funciones se están llamando:

```console
dtrace -n 'fbt::bugdemo_*:entry { printf("%s\n", probefunc); }'
```

Esto imprime cada entrada a cualquier función del módulo `bugdemo`, mostrando el orden en que se llaman. Si sospechas que una función en particular se está alcanzando o no, este one-liner te lo dirá de inmediato.

Para una vista más detallada, también puedes registrar argumentos. Los argumentos de la sonda `fbt` son los parámetros de la función, accesibles como `arg0`, `arg1`, etc.:

```console
dtrace -n 'fbt::bugdemo_ioctl:entry { printf("cmd=0x%lx\n", arg1); }'
```

Aquí `arg1` es el segundo parámetro de `bugdemo_ioctl`, que es el número de comando del ioctl. Puedes observar el flujo de llamadas ioctl en tiempo real.

Las sondas de salida permiten ver los valores de retorno:

```console
dtrace -n 'fbt::bugdemo_ioctl:return { printf("rv=%d\n", arg1); }'
```

En una sonda de retorno, `arg1` es el valor de retorno. Un flujo de entradas `rv=0` confirma el éxito. Un `rv=22` repentino (que corresponde a `EINVAL`) te indica que el driver rechazó una llamada. Combinando sondas de entrada y retorno, puedes asociar cada llamada con su resultado.

### Sondas SDT: trazado definido estáticamente

`fbt` es flexible, pero te ofrece los límites de las funciones, no los eventos semánticos. Si quieres una sonda que se active en un punto específico dentro de una función, representando un evento concreto, utilizas SDT. Las sondas SDT se colocan explícitamente en el código. No tienen prácticamente ningún coste cuando están desactivadas, y producen exactamente la información que quieres cuando están activadas.

En FreeBSD 14.3, las sondas SDT se definen mediante macros de `/usr/src/sys/sys/sdt.h`. Las macros principales son:

```c
SDT_PROVIDER_DEFINE(bugdemo);

SDT_PROBE_DEFINE2(bugdemo, , , cmd__start,
    "struct bugdemo_softc *", "int");

SDT_PROBE_DEFINE3(bugdemo, , , cmd__done,
    "struct bugdemo_softc *", "int", "int");
```

La convención de nombres es `provider:module:function:name`. El `bugdemo` inicial es el proveedor. Las dos cadenas vacías son el módulo y la función, que dejamos vacías para una sonda a nivel de driver. El nombre final identifica la sonda. La convención del doble guion bajo en los nombres de sonda es un idioma de DTrace que se convierte en un guion en el nombre visible para el usuario.

El sufijo numérico en `SDT_PROBE_DEFINE` indica cuántos argumentos acepta la sonda. Los argumentos de tipo cadena son los nombres de tipo C de esos argumentos, que DTrace utiliza para la visualización.

Para activar una sonda en el driver:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        SDT_PROBE2(bugdemo, , , cmd__start, sc, cmd->op);

        /* ... actual work ... */

        SDT_PROBE3(bugdemo, , , cmd__done, sc, cmd->op, error);
}
```

`SDT_PROBE2` y `SDT_PROBE3` activan la sonda correspondiente con los argumentos indicados.

Ahora en DTrace puedes observar estas sondas:

```console
dtrace -n 'sdt:bugdemo::cmd-start { printf("op=%d\n", arg1); }'
```

Fíjate en el guion en `cmd-start`: DTrace convierte el doble guion bajo del nombre en un guion en la especificación de la sonda. `arg0` es el softc, `arg1` es la operación.

Las sondas SDT son especialmente útiles para las transiciones de estado. Si tu driver tiene tres estados y quieres seguir la secuencia, define sondas en cada transición y agrégalas:

```console
dtrace -n 'sdt:bugdemo::state-change { @[arg1, arg2] = count(); }'
```

Esto cuenta con qué frecuencia ocurre cada par (from_state, to_state), lo que proporciona una distribución del comportamiento de la máquina de estados durante tu carga de trabajo.

### Seguir un bug con DTrace

Imagina el siguiente escenario. El driver `bugdemo` devuelve a veces `EIO` al espacio de usuario, pero desde el espacio de usuario no puedes saber qué camino de código produjo ese error. Con DTrace, puedes retroceder desde el retorno hasta el origen:

```console
dtrace -n '
fbt::bugdemo_ioctl:return
/arg1 == 5/
{
        stack();
}
'
```

`arg1 == 5` comprueba el valor de retorno 5, que corresponde a `EIO`. Cuando el retorno coincide, `stack()` imprime la traza de pila del kernel en el punto del retorno. Esto te indica exactamente qué camino de código devolvió el error.

Una versión más sofisticada registra el tiempo de inicio y la duración:

```console
dtrace -n '
fbt::bugdemo_ioctl:entry
{
        self->start = timestamp;
}

fbt::bugdemo_ioctl:return
/self->start != 0/
{
        @latency["bugdemo_ioctl", probefunc] = quantize(timestamp - self->start);
        self->start = 0;
}
'
```

Esto produce una distribución de latencia para el ioctl, lo que resulta útil cuando un bug se manifiesta como una latencia inusual. La notación `self->` es el almacenamiento local de thread de DTrace, con ámbito restringido al thread actual.

Estos scripts no son programas completos; son pequeñas observaciones sobre las que se itera. El ciclo de "añadir una sonda, ejecutar la carga de trabajo, leer la salida, refinar la sonda" es uno de los puntos fuertes de DTrace. Una sesión de depuración completa puede pasar por una docena de variaciones de un script antes de que la forma del bug quede clara.

### Entendiendo ktrace(1)

`ktrace(1)` es una herramienta distinta. Traza las llamadas al sistema realizadas por un proceso del espacio de usuario, junto con sus argumentos y valores de retorno. No trata el comportamiento interno del kernel, sino la interfaz entre el espacio de usuario y el kernel. Cuando una herramienta del espacio de usuario está usando un driver y ocurre algo extraño, `ktrace(1)` es a menudo la primera herramienta a la que acudir, porque muestra exactamente qué le pidió el proceso al kernel.

Para trazar un programa:

```console
# ktrace -t cnsi ./test_bugdemo
# kdump
```

`ktrace` escribe un archivo de traza binario (`ktrace.out` por defecto), y `kdump` lo convierte a texto legible. Las opciones `-t` seleccionan qué trazar: `c` para llamadas al sistema, `n` para namei (búsquedas de nombres de ruta), `s` para señales, `i` para ioctls. Para la depuración de drivers, `i` es la más directamente útil.

Ejemplo de salida:

```text
  5890 test_bugdemo CALL  ioctl(0x3,BUGDEMO_TRIGGER,0x7fffffffe0c0)
  5890 test_bugdemo RET   ioctl 0
  5890 test_bugdemo CALL  read(0x3,0x7fffffffe0d0,0x100)
  5890 test_bugdemo RET   read 32/0x20
```

El proceso realizó dos llamadas al sistema. Un ioctl sobre el descriptor de archivo 3 con el comando `BUGDEMO_TRIGGER` tuvo éxito. Una lectura en el mismo fd devolvió 32 bytes. Si la prueba falla, la traza te indica exactamente qué se le pidió al kernel y qué devolvió.

Ten en cuenta que `ktrace(1)` no muestra el comportamiento interno del kernel. Para eso necesitas DTrace o `ktr(9)`. Pero `ktrace(1)` es la forma canónica de ver las interacciones del espacio de usuario y, combinado con DTrace, ofrece una imagen completa.

`ktrace(1)` también puede adjuntarse a un proceso en ejecución:

```console
# ktrace -p PID
```

y desadjuntarse:

```console
# ktrace -C
```

Para un driver que está siendo usado por un demonio de larga ejecución, esto es más práctico que reiniciar el demonio bajo `ktrace`.

### ktr(9): trazado ligero en el kernel

`ktr(9)` es el anillo de trazado en el kernel de FreeBSD. Es un buffer circular de entradas de traza en el que el código puede escribir de forma económica. Cada entrada incluye una marca de tiempo, el número de CPU, el puntero al thread, una cadena de formato y hasta seis argumentos. El tamaño del anillo lo determina la opción de configuración del kernel `KTR_ENTRIES`, y su contenido puede volcarse desde `DDB` o desde el espacio de usuario.

`ktr(9)` es la herramienta adecuada cuando necesitas información muy detallada sobre tiempos u ordenación, especialmente en el contexto de una interrupción, donde `printf` es demasiado lento. Como cada entrada es pequeña y las escrituras no requieren lock, `ktr(9)` puede usarse en caminos de código críticos sin distorsionar el comportamiento que intentas observar.

Las macros están en `/usr/src/sys/sys/ktr.h`. Las más habituales son `CTR0` hasta `CTR6`, que varían según el número de argumentos que siguen a la cadena de formato. Cada macro toma una máscara de clase como primer argumento, después la cadena de formato y luego los valores:

```c
#include <sys/ktr.h>

static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        int error;

        CTR2(KTR_DEV, "bugdemo_process: sc=%p op=%d", sc, cmd->op);
        /* ... */
        CTR1(KTR_DEV, "bugdemo_process: done rv=%d", error);
}
```

`CTR2` escribe una entrada con dos argumentos en el anillo de trazado. `KTR_DEV` es la máscara de clase: el kernel decide en tiempo de ejecución si las entradas de una clase dada se registran, en función de `debug.ktr.mask`. En tiempo de compilación, `KTR_COMPILE` (el conjunto de clases realmente compiladas) controla qué llamadas se emiten. Las clases que no están en `KTR_COMPILE` desaparecen por completo, por lo que puedes dejar las llamadas en el código fuente de forma permanente sin pagar por ellas cuando la clase está desactivada.

Las clases se definen en `/usr/src/sys/sys/ktr_class.h`. Entre las más habituales están `KTR_GEN` (uso general), `KTR_DEV` (drivers de dispositivo), `KTR_NET` (red) y muchas más. Para un driver, normalmente elegirías `KTR_DEV` o, en subsistemas más grandes, definirías un bit nuevo junto a los existentes.

Para habilitar y ver el anillo de trazado:

```console
# sysctl debug.ktr.mask=0x4          # enable KTR_DEV (bit 0x04)
# sysctl debug.ktr.entries
```

y volcarlo con:

```console
# ktrdump
```

`ktrdump(8)` lee el buffer de trazado del kernel a través de `/dev/kmem` y lo formatea. La salida es una lista ordenada cronológicamente de entradas con marcas de tiempo, CPUs, threads y mensajes.

La ventaja de `ktr(9)` es su bajo coste. Una entrada de trazado equivale esencialmente a un puñado de escrituras en memoria. Puedes dejarlas en el código, compilarlas en un kernel de depuración y activarlas en tiempo de ejecución cuando las necesites. Son especialmente valiosas para la depuración de manejadores de interrupciones, donde `printf` añadiría milisegundos de retardo y modificaría realmente el comportamiento que estás midiendo.

### Cuándo usar cada herramienta

Con tres herramientas de trazado, la pregunta es a cuál recurrir primero.

Usa DTrace cuando el bug tenga que ver con lo que está haciendo el kernel, cuando necesites agregar a lo largo de muchos eventos, cuando necesites filtrado, o cuando las sondas puedan colocarse de forma dinámica. DTrace es la más potente de las tres, pero requiere un kernel en ejecución y una tasa razonable de activaciones de sondas.

Usa `ktrace(1)` cuando el bug tenga que ver con lo que el espacio de usuario le está pidiendo al kernel, cuando el síntoma sea un valor de retorno incorrecto o una secuencia de llamadas al sistema que no coincide con lo esperado. `ktrace(1)` es simple, rápido, y muestra de inmediato el límite entre el kernel y el espacio de usuario.

Usa `ktr(9)` cuando necesites la menor sobrecarga posible, cuando el código que estás rastreando se encuentre en un manejador de interrupciones, o cuando quieras puntos de traza persistentes que puedan activarse en producción con un riesgo mínimo. `ktr(9)` es el más primitivo de los tres, pero también el más resistente.

En la práctica, una sesión de depuración suele combinar dos o tres herramientas. Puedes empezar con `ktrace(1)` para ver la secuencia de syscalls, añadir después sondas DTrace para acotar qué función del driver se está comportando mal y, finalmente, incorporar entradas de `ktr(9)` para determinar con precisión el momento exacto en el camino de las interrupciones. Cada herramienta responde a una pregunta distinta, y para obtener una imagen completa a menudo son necesarias las tres.

### Seguimiento y producción

Una breve nota sobre el entorno de producción. DTrace es seguro para entornos de producción en la mayoría de configuraciones; su diseño incluye específicamente salvaguardas contra los bucles infinitos y contra provocar un pánico del kernel a causa de una sonda defectuosa. Puedes ejecutar DTrace en un servidor de producción con carga elevada sin detenerlo. `ktr(9)` también es seguro para producción, con la salvedad de que habilitar clases en modo detallado tiene un coste en CPU. `ktrace(1)` escribe en un archivo y puede crecer sin límite si no se controla; úsalo con límites de tamaño.

A diferencia de estos, los volcados de memoria, `DDB` y `memguard(9)` son herramientas exclusivas para el desarrollo. La distinción importa porque la sección 7 retomará la pregunta de qué es seguro hacer en una máquina de producción. Por ahora, recuerda que el seguimiento es una de las técnicas menos intrusivas que tenemos, y por eso suele ser el primer paso adecuado al diagnosticar un problema en un sistema activo.

Con el seguimiento en mano, podemos pasar a los bugs que el seguimiento y las aserciones suelen pasar por alto: los bugs de memoria que corrompen el estado sin producir síntomas claros hasta mucho más adelante. Ese es el dominio de la sección 6.

## 6. Búsqueda de fugas de memoria y accesos inválidos a memoria

Los bugs de memoria son los más traicioneros a los que se enfrenta el autor de un driver. Rara vez son visibles en el momento en que ocurren. Corrompen el estado en silencio, se acumulan a lo largo de muchas ejecuciones y se manifiestan mucho más tarde de formas que parecen completamente ajenas al defecto original. Un use-after-free puede aparecer como una estructura corrupta en un subsistema diferente. Un desbordamiento de buffer puede sobrescribir la siguiente asignación y manifestarse como un valor de campo incorrecto varios minutos después. Una pequeña fuga puede agotar la memoria durante días hasta que el kernel finalmente rechaza una asignación y el sistema queda bloqueado.

FreeBSD proporciona una familia de herramientas para estos bugs: `memguard(9)` para detectar use-after-free y modify-after-free, `redzone` para desbordamientos de buffer, páginas de guarda en la capa VM y sysctls que exponen el estado del asignador de memoria del kernel. Usadas conjuntamente, pueden transformar una clase de bug que era casi imposible de encontrar en una que provoca un pánico de inmediato en el momento del uso incorrecto.

### Cómo funciona el asignador de memoria del kernel

Para usar estas herramientas de forma eficaz necesitamos un modelo mental aproximado de cómo el kernel asigna memoria. FreeBSD tiene dos asignadores principales, ambos en `/usr/src/sys/kern/`:

`kern_malloc.c` implementa `malloc(9)`, el asignador de propósito general. Es una capa delgada sobre UMA, el Universal Memory Allocator, con contabilidad por tipo de malloc. Cada asignación se imputa a un `struct malloc_type` (habitualmente declarado con `MALLOC_DEFINE(9)`), lo que permite al kernel rastrear cuánta memoria ha utilizado cada subsistema.

`subr_vmem.c` y `uma_core.c` implementan las capas inferiores. UMA es un asignador de slabs: mantiene cachés por CPU y slabs centrales, de modo que la mayoría de las asignaciones son muy rápidas y sin contención. Cuando un driver llama a `malloc(9)` o a `uma_zalloc(9)`, lo que ocurre realmente depende del tamaño, la configuración de la zona y el estado de la caché.

Para la depuración, la consecuencia práctica es que una asignación corrupta puede manifestarse de forma diferente según dónde haya ido a parar. El mismo bug puede producir síntomas distintos en diferentes kernels o bajo diferentes cargas, simplemente porque el diseño de la memoria subyacente es diferente.

### sysctl vm y kern.malloc: observar el estado de las asignaciones

Antes de recurrir a los depuradores de memoria, un primer paso útil es observar el estado activo del asignador. Dos sysctls son especialmente útiles:

```console
# sysctl vm.uma
# sysctl kern.malloc
```

El primero vuelca estadísticas por zona para UMA: cuántos elementos están asignados, cuántos están libres, cuántos fallos han ocurrido y cuántas páginas usa cada zona. La salida es larga, pero se puede buscar en texto. Si sospechas de una fuga en un tipo de driver concreto, busca su zona en la salida y obsérvala crecer.

El segundo vuelca estadísticas por tipo para `malloc(9)`. Cada entrada muestra el nombre del tipo, el número de solicitudes, la cantidad asignada y la marca de nivel máximo. Ejecutar el driver bajo una carga de trabajo y comparar el antes y el después es una técnica sencilla de detección de fugas que no requiere herramientas especiales:

```console
# sysctl kern.malloc | grep bugdemo
bugdemo:
        inuse = 0
        memuse = 0K
```

Ejecuta una carga de trabajo, vuelve a consultar y compara. Si `inuse` sube y no baja tras finalizar la carga de trabajo, algo está perdiendo memoria.

El comando `vmstat(8)` relacionado tiene una opción `-m` que presenta el mismo estado de `malloc(9)` en un formato más compacto:

```console
# vmstat -m | head
         Type InUse MemUse HighUse Requests  Size(s)
          acl     0     0K       -        0  16,32,64,128,256,1024
         amd6     4    64K       -        4  16384
        bpf_i     0     0K       -        2
        ...
```

Para una monitorización continua durante una carga de trabajo:

```console
# vmstat -m | grep -E 'bugdemo|Type'
```

te ofrece una instantánea periódica del uso de un único tipo.

### memguard(9): búsqueda de use-after-free

`memguard(9)` es un asignador especial que puede reemplazar a `malloc(9)` para un tipo concreto. La idea es sencilla: en lugar de devolver un fragmento de memoria de un slab, devuelve memoria respaldada por páginas dedicadas. Cuando se libera la memoria, las páginas no se devuelven al pool; se desasignan, de modo que cualquier acceso posterior provoca un fallo. Y las páginas que rodean la asignación permanecen inaccesibles, por lo que cualquier lectura o escritura más allá del final de la asignación también provoca un fallo. Esto convierte los bugs de use-after-free, desbordamiento de buffer y subdesbordamiento de buffer de corruptores silenciosos en pánicos inmediatos con un backtrace que apunta directamente al uso incorrecto.

El coste es que cada asignación requiere ahora al menos una página completa de memoria virtual (más la sobrecarga de gestión), y cada liberación consume una página desasignada. Por ese motivo, `memguard(9)` suele activarse para un único tipo de malloc cada vez, no para todo.

La configuración implica dos pasos. Primero, el kernel debe construirse con `options DEBUG_MEMGUARD`, que `std.debug` no activa por defecto. Lo añades a tu configuración del kernel:

```text
include "std.debug"
options DEBUG_MEMGUARD
```

y lo reconstruyes.

Segundo, en tiempo de ejecución le dices a `memguard` qué tipo de malloc debe vigilar:

```console
# sysctl vm.memguard.desc=bugdemo
```

A partir de ese momento, toda asignación del tipo `bugdemo` pasa por `memguard`. Ten en cuenta que la cadena del tipo debe coincidir con el nombre pasado a `MALLOC_DEFINE(9)` en el código fuente del driver. Los errores tipográficos aquí no producen ningún efecto y no dan ningún aviso.

También puedes usar `vm.memguard.desc=*` para vigilarlo todo, pero como se ha indicado, esto es costoso. Para una búsqueda de bugs localizada, vigila solo el tipo que sospechas.

### Una sesión de memguard en acción

Imagina que `bugdemo` tiene un bug de use-after-free: el driver libera un buffer cuando su ioctl se completa, pero luego un gestor de interrupciones lee del mismo buffer un momento después. Sin `memguard`, la lectura suele tener éxito porque el asignador de slabs aún no ha reutilizado la memoria, o devuelve datos no relacionados que casualmente han reemplazado al buffer. El driver obtiene una salida plausible pero incorrecta, que corrompe algún estado posterior, que se manifiesta mucho más tarde como un bug sutil.

Con `memguard` habilitado para el tipo de malloc del driver, la misma secuencia de eventos provoca un fallo de página en el instante en que el gestor de interrupciones desreferencia el puntero liberado. El fallo produce un pánico con un backtrace a través del gestor de interrupciones. El mensaje del pánico identifica la dirección que causó el fallo como parte de una región de `memguard`, y `kgdb` sobre el volcado te muestra exactamente qué función desreferenció la memoria liberada.

Compara eso con los días de trabajo de investigación que el bug habría exigido sin `memguard`, y entenderás por qué esta herramienta es tan valiosa.

### redzone: detección de desbordamientos de buffer

`memguard` es pesado. Para el caso más concreto de los desbordamientos de buffer (overruns y underruns), FreeBSD ofrece `DEBUG_REDZONE`, un depurador más ligero que añade unos pocos bytes de guarda antes y después de cada asignación. Cuando se libera la asignación, se comprueban los bytes de guarda. Si han sido modificados, `redzone` notifica la corrupción, incluyendo el backtrace en el momento de la asignación.

`DEBUG_REDZONE` se añade a la configuración del kernel:

```text
options DEBUG_REDZONE
```

A diferencia de `memguard`, está siempre activo una vez compilado, y se aplica a todas las asignaciones. Su sobrecarga es de memoria, no de tiempo: cada asignación crece en unos pocos bytes.

`redzone` no detecta los use-after-free, porque la memoria que vigila sigue dentro de la asignación original. Sí detecta las escrituras que se desplazan más allá del buffer previsto, que es una clase habitual de bug en drivers que calculan desplazamientos a partir de tamaños proporcionados por el usuario.

### Páginas de guarda en la capa VM

Un tercer mecanismo, disponible de forma independiente a `memguard` y `redzone`, es el uso de páginas de guarda alrededor de las asignaciones críticas del kernel. El sistema VM permite asignar una región de memoria con páginas inaccesibles colocadas antes y después. Las pilas de threads del kernel utilizan este mecanismo: la página que hay debajo de cada pila está desasignada, de modo que una recursión descontrolada provoca un fallo en lugar de sobrescribir la asignación adyacente.

Los drivers que asignan objetos similares a pilas pueden usar `kmem_malloc(9)` con los flags adecuados, o bien configurar páginas de guarda manualmente mediante `vm_map_find(9)`. En la práctica, el código de driver rara vez hace esto directamente; el mecanismo lo usan con más frecuencia los subsistemas que gestionan sus propias regiones de memoria. Pero es útil saber que la capacidad existe, porque puedes encontrarla en los mensajes del kernel y querrás entender qué significa.

### Detección de fugas en la práctica

Las fugas son la clase más silenciosa de bug de memoria. No producen pánicos, ni fallos, ni aserciones. El único síntoma es que el uso de memoria crece con el tiempo. FreeBSD te proporciona algunas herramientas para encontrarlas.

La primera, como vimos, es `kern.malloc`. Toma una instantánea antes, ejecuta la carga de trabajo, toma una instantánea después y busca los tipos cuyo `inuse` haya crecido y no haya disminuido. Es rudimentario pero efectivo para las fugas en drivers.

La segunda es añadir contadores a tu driver. Si cada asignación incrementa un `counter(9)` y cada liberación lo decrementa, un valor positivo persistente en el momento de la descarga te indica que el driver ha perdido algo de memoria. Un sysctl complementario expone el contador para su inspección:

```c
static counter_u64_t bugdemo_inflight;

/* in attach: */
bugdemo_inflight = counter_u64_alloc(M_WAITOK);

/* in allocation path: */
counter_u64_add(bugdemo_inflight, 1);

/* in free path: */
counter_u64_add(bugdemo_inflight, -1);

/* in unload: */
KASSERT(counter_u64_fetch(bugdemo_inflight) == 0,
    ("bugdemo: %ld buffers leaked at unload",
     (long)counter_u64_fetch(bugdemo_inflight)));
```

Esta práctica, contar explícitamente las asignaciones en curso, es útil en cualquier subsistema que posea un pool de objetos. La aserción en el momento de la descarga se activa si algo ha perdido memoria, dándote un informe inmediato en el momento en que se detecta la fuga, no horas después.

La tercera herramienta es DTrace. Si sabes qué tipo de malloc está perdiendo memoria pero no por qué, un script de DTrace puede rastrear cada asignación y cada liberación, acumulando la diferencia por backtrace:

```console
dtrace -n '
fbt::malloc:entry
/arg1 == (uint64_t)&M_BUGDEMO/
{
        @allocs[stack()] = count();
}

fbt::free:entry
/arg1 == (uint64_t)&M_BUGDEMO/
{
        @frees[stack()] = count();
}
'
```

Tras una carga de trabajo, comparar las dos agregaciones a menudo revela un camino de código que asigna pero nunca libera. Los backtraces te señalan directamente los puntos de llamada problemáticos.

### Cuando los bugs de memoria se esconden

A veces los bugs de memoria no encajan en ninguno de estos patrones. El síntoma es un pánico en un subsistema no relacionado, con un backtrace que parece imposible. El driver parece correcto en la revisión; sus asignaciones y liberaciones parecen equilibradas. Sin embargo, el kernel sigue sufriendo pánicos con mensajes sobre listas corruptas o punteros inválidos.

La causa habitual en estos casos es que el driver escribe más allá del final de un buffer hacia la siguiente asignación. La siguiente asignación pertenece a otro; tu desbordamiento corrompe silenciosamente los datos de ese otro subsistema. El pánico ocurre la próxima vez que el otro subsistema accede a sus datos corruptos, lo que puede suceder pronto o mucho más tarde.

Para esta clase de bug, el diagnóstico consiste en habilitar `DEBUG_REDZONE` y estar atento a los avisos. Cuando `redzone` informa de que los bytes de guarda han sido modificados, el backtrace que imprime para la asignación corresponde a la asignación en cuestión, y el código que la desbordó es el código que estaba escribiendo en esa asignación. El informe de `redzone` te muestra ambos extremos del bug.

Otro truco es habilitar `MALLOC_DEBUG_MAXZONES=N` con una N grande. Esto distribuye las asignaciones entre más zonas, de modo que las asignaciones de un driver tienen menos probabilidades de compartir una zona con subsistemas no relacionados. Si el síntoma desaparece o cambia con más zonas, es una pista clara de que el bug implica corrupción entre zonas.

### Trabajar con DDB ante bugs de memoria

Cuando el kernel sufre un pánico por un bug de memoria, entrar en `DDB` puede ayudar a acotar la causa. Los comandos de `DDB` más útiles son:

- `show malloc` vuelca el estado de `malloc(9)`.
- `show uma` vuelca el estado de las zonas UMA.
- `show vmochk` ejecuta una comprobación de consistencia sobre el árbol de objetos VM.
- `show allpcpu` muestra el estado por CPU.

Estos comandos producen una salida que ayuda a correlacionar un crash con el estado de los asignadores en el momento del crash. No reemplazan el análisis con `kgdb`, pero pueden ser más rápidos de consultar cuando ya estás en `DDB`.

### Perspectiva real sobre los depuradores de memoria

`memguard`, `redzone` y sus variantes son efectivos. También son disruptivos. Cambian el comportamiento del asignador, ralentizan el kernel y algunos consumen memoria de forma agresiva. Dejarlos activos en producción no es una buena idea.

El uso correcto es dirigido. Cuando aparece un bug, activa el depurador apropiado, reproduce el bug, captura la evidencia y luego desactívalo. La mayor parte del desarrollo de tu driver ocurre con un kernel que tiene `INVARIANTS` y `WITNESS` pero sin `DEBUG_MEMGUARD`. `DEBUG_MEMGUARD` entra en juego cuando estás persiguiendo activamente un bug de memoria y desaparece cuando terminas.

Una última consideración. Algunos depuradores de memoria, en particular `memguard`, cambian el comportamiento observable del asignador de formas que pueden enmascarar bugs. Si un driver depende de que dos asignaciones sean contiguas en memoria (lo que nunca debería ocurrir, pero a veces ocurre como invariante accidental), `memguard` romperá esa dependencia y hará que el bug desaparezca. Esto no significa que el bug esté corregido; significa que el bug ahora está latente. Prueba siempre sin `memguard` después de una corrección, para asegurarte de que la corrección es real y no un artefacto de la presencia del depurador.

### Cerrando la sección de memoria

Los bugs de memoria son los asesinos silenciosos del código de drivers. La paciencia para encontrarlos se construye sobre un conjunto pequeño y enfocado de herramientas. `memguard(9)` detecta directamente el uso tras liberación y los desbordamientos de buffer. `redzone` detecta desbordamientos con menos sobrecarga. Los sysctls de `kern.malloc` y UMA exponen el estado del asignador que el código normal no puede ver. Y la disciplina de contar asignaciones activas en tu propio driver detecta fugas en el momento de la descarga. Combinando todo esto, una clase de bug que antes tardaba días en encontrarse puede hacerse evidente en minutos.

Con las principales herramientas técnicas ya cubiertas, pasamos a la disciplina de usarlas de forma segura, especialmente en sistemas donde otras personas están mirando. Ese es el contenido de la Sección 7.

## 7. Prácticas de depuración segura

Cada herramienta que hemos aprendido en este capítulo tiene un coste, y cada coste tiene un contexto en el que es aceptable. Un kernel de depuración en una VM de desarrollo es un precio pequeño por detectar bugs pronto. El mismo kernel de depuración en un servidor de producción es un desastre en cámara lenta. Saber qué herramientas usar en qué contexto es parte de lo que distingue a un autor de drivers competente de uno peligroso.

Esta sección reúne las prácticas que te mantienen fuera de problemas: las convenciones para usar cada herramienta de forma segura, las señales de que estás a punto de cometer un error y la mentalidad que te ayuda a trabajar con disciplina cuando las apuestas son altas.

### La división entre desarrollo y producción

La distinción más importante en la depuración segura es entre un sistema de desarrollo, donde puedes hacer crash al kernel libremente, y un sistema de producción, donde no puedes.

En un sistema de desarrollo, todo lo que hay en este capítulo está disponible. Provoca panics deliberadamente. Activa `DEBUG_MEMGUARD`. Carga y descarga el driver repetidamente. Conecta `kgdb` al kernel en vivo. Ejecuta scripts de DTrace que recopilen megabytes de datos. Lo peor que puede ocurrir es que reinicies la VM, lo que se mide en segundos.

En un sistema de producción, aplica la postura contraria. No actives opciones de depuración a menos que tengas una razón específica y concreta. No cargues drivers experimentales. No ejecutes scripts de DTrace que puedan desestabilizar el framework de sondas. No entres en `DDB` en un sistema en vivo. Cada intervención va precedida de una respuesta clara a la pregunta "¿qué hago si esto va mal?".

La disciplina de mantener estos dos entornos separados es la forma más eficaz de evitar romper producción accidentalmente. Ten una VM de desarrollo, mantén el kernel de producción en una partición diferente y nunca confundas los dos.

### Qué es seguro en producción

Una cantidad sorprendente del conjunto de herramientas de depuración es realmente segura en producción, si se usa con cuidado. Aquí tienes una lista parcial.

Los scripts de DTrace son en general seguros en producción. El framework de DTrace está diseñado específicamente con garantías de seguridad: las acciones de las sondas no pueden entrar en bucles infinitos, no pueden asignar memoria arbitraria y no pueden desreferenciar punteros arbitrarios sin caer en un camino de recuperación bien definido. Puedes ejecutar agregaciones de DTrace en un servidor muy cargado sin tumbarlo. Las advertencias son que las sondas de muy alta frecuencia pueden consumir CPU de forma significativa (una sonda por cada paquete en un driver de red difícilmente será gratuita), y que la salida de DTrace puede llenar el espacio del sistema de archivos si no se limita en frecuencia.

`ktrace(1)` sobre un proceso específico es seguro, aunque escribe en un archivo que crece de forma ilimitada. Establece un límite de tamaño o vigila el archivo.

`ktr(9)` es seguro si las clases relevantes ya están compiladas. Activar una clase a través de `sysctl debug.ktr.mask=` es seguro. Compilar una nueva clase requiere reconstruir el kernel, lo cual es una actividad de desarrollo.

Leer sysctls siempre es seguro. `kern.malloc`, `vm.uma`, `hw.ncpu`, `debug.kdb.*` y todos los demás exponen el estado sin cambiar nada. Un sistema de producción con un driver enfermo puede ser interrogado extensamente solo a través de sysctl.

### Qué no es seguro en producción

Una lista más corta, pero importante.

Los panics no son seguros. Hacer crash deliberadamente a un servidor de producción solo es aceptable como último recurso cuando el servidor ya está irrecuperablemente dañado y un dump es el mejor camino para entender la causa. `sysctl debug.kdb.panic=1` provoca un panic y un dump inmediatos. No lo hagas a la ligera.

Entrar en `DDB` en una consola de producción no es seguro. El kernel entero se detiene mientras estás en `DDB`. Los procesos de usuario se congelan. Las conexiones de red expiran. El trabajo en tiempo real se detiene. A menos que la alternativa sea peor (como suele ocurrir durante un crash catastrófico), mantente fuera de `DDB` en producción.

`DEBUG_MEMGUARD` sobre todos los tipos de asignación no es seguro. El uso de memoria se dispara. El rendimiento cae bruscamente. Las cargas de trabajo intensivas en memoria pueden fallar por completo. Si absolutamente debes usar `memguard` en producción, limítalo a un tipo malloc a la vez y monitoriza el uso de memoria.

Los módulos del kernel cargables son un riesgo. Cargar o descargar un módulo toca el estado del kernel. Un módulo con bugs puede hacer crash el kernel en el momento de la carga, en el de la descarga o en cualquier momento intermedio. En producción, carga solo módulos que hayan sido probados en el entorno de desarrollo con el mismo kernel.

Los scripts de DTrace demasiado agresivos pueden desestabilizar el sistema. Las agregaciones que registran stack traces producen presión de memoria. Las sondas con efectos secundarios pueden interactuar con la carga de trabajo de formas inesperadas. Ejecuta scripts de DTrace con límites de tiempo explícitos y revisa las agregaciones con cuidado antes de dejarlos en ejecución.

### Captura de evidencia en sistemas de producción

Cuando algo va mal en producción y el bug es poco frecuente o difícil de reproducir en desarrollo, el reto es capturar suficiente evidencia para diagnosticar el problema sin desestabilizar el servicio en ejecución. Varias estrategias ayudan.

Primero, empieza con observación pasiva. `sysctl`, `vmstat -m`, `netstat -m`, `dmesg` y los distintos comandos de estadísticas del sistema con `-s` pueden ejecutarse mientras el sistema está en vivo y cuestan casi nada. Si el bug produce síntomas visibles en estos informes, captúralos periódicamente.

Segundo, usa DTrace con límites estrictos. Un script que se ejecuta durante sesenta segundos y termina produce una instantánea sin dejar un riesgo permanente. Las agregaciones son especialmente adecuadas para este estilo: recopilan estadísticas durante una ventana, las imprimen y se detienen.

Tercero, si se necesita un dump y el sistema aún no ha hecho crash, el enfoque más seguro es esperar al crash. Los mecanismos de dump modernos están diseñados para capturar el estado del kernel en el momento del panic; un dump provocado manualmente solo es útil cuando sabes que el sistema ya es irrecuperable.

Cuarto, cuando se produce un crash, trabaja sobre el dump, no sobre el sistema en vivo. Un reinicio con un kernel nuevo restaura el servicio, mientras el dump permanece disponible para análisis fuera de línea con calma. La disciplina de "reiniciar rápido, analizar después" suele ser el compromiso correcto en hardware de producción.

### Usar `log(9)` en lugar de `printf` para diagnósticos

A lo largo del capítulo hemos usado `printf` como abreviatura para el registro en el lado del kernel, que es como se presenta habitualmente en los libros de texto. En un sistema de producción deberías preferir `log(9)`, que escribe a través del sistema `syslogd(8)` en lugar de directamente en la consola. Los motivos son prácticos: la salida por consola no tiene buffer y es lenta, `log(9)` tiene limitación de frecuencia y buffer, y `log(9)` acaba en `/var/log/messages`, donde está disponible para las herramientas de análisis de registros.

La API está en `/usr/src/sys/sys/syslog.h` y `/usr/src/sys/kern/subr_prf.c`. Uso:

```c
#include <sys/syslog.h>

log(LOG_WARNING, "bugdemo: unexpected state %d\n", sc->state);
```

El nivel de prioridad (`LOG_DEBUG`, `LOG_INFO`, `LOG_NOTICE`, `LOG_WARNING`, `LOG_ERR`, `LOG_CRIT`, `LOG_ALERT`, `LOG_EMERG`) permite a `syslogd` enrutar los mensajes de forma diferente.

Una extensión habitual es el registro con limitación de frecuencia, para que un driver enfermo no inunde `/var/log/messages` con millones de entradas por segundo. FreeBSD proporciona el primitivo `ratecheck(9)` que puedes envolver alrededor de tus propias llamadas a `log`:

```c
#include <sys/time.h>

static struct timeval lastlog;
static struct timeval interval = { 5, 0 };   /* 5 seconds */

if (ratecheck(&lastlog, &interval))
        log(LOG_WARNING, "bugdemo: error (rate-limited)\n");
```

`ratecheck(9)` devuelve un valor distinto de cero una vez por intervalo, suprimiendo los registros repetidos en el ínterin. La técnica es esencial para cualquier driver que pueda observar el mismo error repetidamente.

### No mezcles kernels de depuración y de producción en una misma flota

Una trampa sutil es ejecutar una mezcla de kernels de depuración y de producción en una flota. La intuición es que los kernels de depuración ofrecen mejores diagnósticos si aparece un bug. La realidad es que un kernel de depuración rinde notablemente peor que uno de producción, tiene un uso de memoria diferente y puede mostrar tiempos distintos. Si un bug es sensible a esos factores (y muchos bugs de concurrencia lo son), ejecutar kernels mixtos garantiza que tu entorno de reproducción no coincide con tu entorno de producción.

El enfoque correcto es uniforme: o toda la flota ejecuta kernels de producción (y depuras en hardware de desarrollo), o toda la flota ejecuta kernels de depuración (y aceptas la sobrecarga). Los despliegues mixtos son una tercera opción solo para experimentos muy controlados.

### Trabajar con un plan de recuperación

Antes de ejecutar cualquier acción de depuración arriesgada, conoce tu plan de recuperación. Si el sistema se cuelga, ¿cómo te recuperas? ¿Hay una interfaz IPMI que pueda emitir un reinicio hardware? ¿Hay un segundo administrador que pueda cortar la alimentación si hace falta? ¿Cuánta pérdida de datos es aceptable?

Un buen plan de recuperación tiene dos pasos. Primero, volver a tener el sistema en marcha rápidamente. Segundo, capturar la evidencia (dump, registros) para el análisis fuera de línea. Estos dos pasos a menudo implican a personas distintas o escalas de tiempo diferentes, y pensar en ambos de antemano evita el pánico en el momento.

### Mantén un diario de depuración

Cuando depures un bug difícil, un registro escrito es invaluable. Cada entrada debería incluir:

- Qué hipótesis estabas probando.
- Qué acción tomaste.
- Qué resultado observaste.
- Qué descarta o confirma el resultado.

Esto puede sonar pedante, pero es genuinamente útil. Una sesión de depuración larga implica docenas de micro-hipótesis, y perder la pista de cuáles ya has probado malgasta un tiempo enorme. El registro escrito también ayuda cuando vuelves al bug después de un fin de semana, o cuando se lo pasas a un compañero.

Para un bug de driver que se extiende por varios sistemas, un registro compartido (sistema de seguimiento de bugs, wiki o ticket interno) es aún más valioso. Cada persona que toca el bug puede ver lo que los demás ya han intentado, y nadie repite el mismo experimento dos veces.

### Practicar con tus propios drivers

Un hábito que da frutos con el tiempo es mantener una versión deliberadamente defectuosa de tu driver para practicar. Cada vez que encuentres un bug interesante en un trabajo real, añade una variante de él a tu driver de práctica. Luego, periódicamente, repasa ese driver con ojos frescos y asegúrate de que todavía puedes encontrar los bugs usando las herramientas de este capítulo. Esto desarrolla una memoria muscular que resulta invaluable cuando aparece un bug real bajo presión de tiempo.

El driver `bugdemo` que hemos utilizado a lo largo de este capítulo es un buen punto de partida para ese driver de práctica. Haz un fork de él, añade tus propios bugs y úsalo para mantenerte en forma.

### Saber cuándo detenerse

Una última pieza de sabiduría para depurar de forma segura es saber cuándo detenerse. No todos los bugs deben cazarse hasta la última instrucción. Si un bug es poco frecuente, tiene una solución alternativa y el coste de encontrar su causa raíz se mide en días, a veces tiene sentido documentar esa solución alternativa y seguir adelante. Esto es una decisión de criterio, no una regla, pero la capacidad de tomarla forma parte de la madurez profesional.

El error contrario (declarar la victoria demasiado pronto, aceptar un arreglo superficial que no aborda el defecto subyacente) también es habitual. El síntoma es un bug que sigue reapareciendo en nuevas formas. Cuando un "arreglo" no produce un resultado estable, algo más profundo está mal y se requiere una investigación más exhaustiva.

Entre estos extremos está la zona saludable donde inviertes tiempo proporcional a la importancia del bug. El desarrollo del kernel premia la paciencia, pero también premia el pragmatismo. Las herramientas de este capítulo existen para hacer eficiente esa inversión, no para convertir cada bug en un proyecto de investigación de varios días.

Con estas prácticas seguras establecidas, podemos pasar al último tema principal del capítulo: qué hacer tras una sesión de depuración que ha encontrado algo grave, y cómo hacer que el driver sea más resiliente ante la próxima vez que algo similar falle.

## 8. Refactorización tras una sesión de depuración: recuperación y resiliencia

Una victoria de depuración ganada con esfuerzo no es el final del trabajo. Encontrar el bug es encontrar evidencia. La pregunta real es: ¿qué nos dice esa evidencia sobre el driver y cómo debería cambiar el driver en respuesta?

Un modo de fallo habitual es parchear el síntoma inmediato y seguir adelante. El parche hace que la prueba pase, el crash se detenga y la corrupción desaparezca. Pero la debilidad subyacente que permitió el bug en primer lugar sigue ahí, al acecho. El siguiente cambio sutil en el código circundante, o el siguiente entorno nuevo, encuentra la misma debilidad y produce el siguiente bug.

Esta sección trata de resistir ese modo de fallo. Recorreremos un pequeño conjunto de técnicas para usar el resultado de una depuración con el fin de fortalecer el driver, no solo para corregir el bug en cuestión.

### Leer el bug como un mensaje

Todo bug lleva un mensaje sobre el diseño. Un use-after-free dice "el modelo de propiedad del driver para este buffer no está claro". Un deadlock dice "el orden de lock del driver no está documentado ni aplicado de forma explícita". Una fuga de memoria dice "el ciclo de vida del driver para este objeto está incompleto". Un pánico en `attach` dice "la recuperación de errores del driver durante la inicialización es débil". Una condición de carrera dice "las suposiciones del driver sobre el contexto del thread no son lo suficientemente estrictas".

Cuando encuentres un bug, dedica unos minutos a preguntarte qué te está diciendo sobre el diseño. El defecto concreto suele ser un síntoma de un patrón más amplio, y comprender ese patrón facilita la prevención de futuros bugs.

### Reforzar invariantes

Una respuesta concreta ante un bug es añadir aserciones que lo habrían detectado antes. Si el bug era un use-after-free, añade un `KASSERT` en algún punto del camino que confirme que el buffer sigue siendo válido cuando se usa. Si se violó el orden de lock, añade un `mtx_assert(9)` en el punto donde se produjo la violación. Si un campo de una estructura estaba corrompido, añade un `CTASSERT` sobre su alineación o una comprobación en tiempo de ejecución sobre su valor.

El objetivo no es duplicar cada comprobación con una aserción, sino convertir cada bug en uno o dos invariantes nuevos que hagan imposible la misma clase de bug en el futuro. Con el tiempo, el driver acumula un conjunto de comprobaciones defensivas que reflejan su comportamiento real, documentadas en el código en lugar de en tu cabeza.

### Documentar el modelo de propiedad

Otra respuesta habitual es aclarar la documentación. Muchos bugs surgen porque la propiedad de un recurso (quién lo asignó, quién es responsable de liberarlo, cuándo es seguro acceder a él) es implícita. Escribir unas pocas líneas de comentario que enuncien explícitamente las reglas de propiedad hace que esas reglas sean visibles para el siguiente lector, y a menudo te obliga a confrontar casos en los que las reglas no eran realmente coherentes.

Por ejemplo, un comentario como:

```c
/*
 * bugdemo_buffer is owned by the softc from attach until detach.
 * It may be accessed from any thread that holds sc->sc_lock.
 * It must not be accessed in interrupt context because the lock
 * is a regular mutex, not a spin mutex.
 */
struct bugdemo_buffer *sc_buffer;
```

Este comentario no es decorativo. Es una declaración de los invariantes que el driver va a aplicar. Si un bug futuro viola los invariantes, el comentario sirve de punto de referencia para entender qué salió mal.

### Reducir la superficie de la API

Una tercera respuesta es reducir la API. Si el bug se produjo porque se llamó a una función en un contexto en el que no debería haberse llamado, ¿puede hacerse privada esa función, de modo que solo se la llame desde contextos en los que sea seguro hacerlo? Si se llegó a un estado a través de un camino que no debería haber existido, ¿puede hacerse inalcanzable ese estado?

El principio es que cada punto de entrada externo en un driver es una superficie para bugs. Reducir esa superficie, haciendo privadas las funciones, ocultando el estado detrás de accesores y combinando operaciones relacionadas en transacciones atómicas, hace que el driver sea más difícil de usar incorrectamente.

Esto no es minimalismo ideológico. Se trata de reconocer que la superficie es proporcional al riesgo de bugs, y que muchos bugs pueden prevenirse con el simple expediente de no exponer aquello que fue mal utilizado.

### Reforzar la ruta de descarga

La ruta de descarga es una de las rutas que más a menudo quedan sin reforzar en los drivers. La ruta de attach suele estar bien probada; la de descarga, por lo general, no. Esta es una fuente importante de bugs: un driver que funciona perfectamente en uso prolongado podría sufrir un crash al ejecutar `kldunload`.

Una buena ruta de descarga satisface varios invariantes. Cada objeto asignado en attach se libera. Cada thread lanzado por el driver ha terminado. Cada temporizador está cancelado. Cada callout está vaciado. Cada taskqueue ha terminado su trabajo pendiente. Cada nodo de dispositivo se destruye antes de que se libere la memoria que lo sustenta.

Tras un bug en una ruta de descarga, audita la función de descarga completa comparándola con esta lista de comprobación. Cada elemento es un invariante que el driver debería mantener, y las violaciones son habituales.

### La forma de un driver resiliente

Reuniendo todos estos hábitos, ¿qué aspecto tiene un driver resiliente? Destacan algunos rasgos.

Su bloqueo es explícito. Cada estructura de datos compartida está protegida por un lock con nombre, y cada función que accede a la estructura tiene o bien una aserción de que el lock está tomado o bien una razón documentada de por qué no es necesario. Los órdenes de lock están documentados en comentarios en cada punto de adquisición de varios locks. `WITNESS` no produce ninguna advertencia durante el funcionamiento normal.

Su gestión de errores es completa. Cada asignación tiene su correspondiente liberación. Cada `attach` tiene un `detach` completo. Cada camino a través del código se limpia tras de sí en caso de fallo. Los estados parciales no persisten. El driver no se queda atascado en estados a medio inicializar o a medio desmontar.

Sus invariantes están expresados en código. Las precondiciones se comprueban con `KASSERT` a la entrada de la función. Los invariantes estructurales se comprueban con `CTASSERT` en tiempo de compilación. Las transiciones de estado se verifican con comprobaciones explícitas.

Su observabilidad está integrada. Los contadores exponen las tasas de asignación y de errores. Las sondas SDT se activan en los eventos clave. Los sysctls exponen suficiente estado para que un operador pueda inspeccionar el driver sin un depurador. El driver te dice qué está haciendo.

Sus mensajes de error son útiles. Los mensajes de `log(9)` incluyen el nombre del subsistema, el error concreto y suficiente contexto para localizar el problema. Tienen limitación de tasa. No registran advertencias espurias que acaben entrenando al operador a ignorarlas.

Estos rasgos no son gratuitos. Llevan tiempo implementarlos y disciplina mantenerlos. Pero una vez que un driver los tiene, el coste de los bugs futuros se reduce drásticamente, porque los bugs se detectan antes, se diagnostican con mayor facilidad y se corrigen de forma más definitiva.

### Volviendo al driver bugdemo

Al final de los laboratorios del capítulo, habremos aplicado muchas de estas ideas al driver `bugdemo`. Lo que comienza como un puñado de rutas de código deliberadamente rotas crece, mediante iteración, hasta convertirse en un driver con aserciones en cada punto clave, contadores en cada operación, sondas SDT en cada evento interesante y una ruta de descarga que supera el escrutinio. La trayectoria está diseñada deliberadamente para reflejar la de un driver real a medida que madura.

### Cerrar el ciclo de refactorización

Un último pensamiento sobre la refactorización. Cada vez que modificas un driver en respuesta a un bug, asumes un pequeño riesgo de que la modificación introduzca un bug nuevo. Este riesgo es inevitable pero manejable. Algunas prácticas ayudan.

En primer lugar, aísla el cambio. Haz la modificación mínima que aborde la causa raíz y confírmala en el repositorio por separado de los cambios cosméticos. Si algo regresa, es fácil identificar la causa.

En segundo lugar, añade una prueba. Si el bug se desencadenó por una secuencia concreta de ioctls, añade un pequeño programa de prueba que ejecute esa secuencia y verifique el resultado correcto. Conserva la prueba en tu repositorio. Un conjunto de pruebas que crece con cada bug se convierte en un activo a lo largo de los años.

En tercer lugar, ejecuta las pruebas existentes. Si el driver tiene alguna prueba automatizada, ejecútala tras la corrección. Sorprendentemente, así se detectan muchas regresiones, incluso con un conjunto de pruebas pequeño.

En cuarto lugar, anota la lección. En tu diario de depuración o en el mensaje de confirmación, escribe una breve nota sobre lo que el bug reveló acerca del diseño del driver. La nota es un regalo para tu yo futuro, que encontrará patrones similares.

Con estos hábitos establecidos, la depuración se convierte en un ciclo de descubrimiento en lugar de una sucesión de urgencias. Cada bug enseña algo, cada lección fortalece el driver y cada driver fortalecido resulta más fácil de utilizar. Las herramientas de este capítulo son el medio por el que ese ciclo avanza.

Con el material conceptual ya cubierto, podemos pasar a la sección de laboratorios prácticos, donde aplicaremos cada técnica del capítulo al driver `bugdemo` y veremos cómo cada una produce un resultado concreto.

## Laboratorios prácticos

Cada laboratorio de esta sección es autónomo, pero se apoya en los anteriores. Utilizan el driver `bugdemo`, cuyo código fuente complementario está en `examples/part-07/ch34-advanced-debugging/`.

Antes de empezar, asegúrate de tener una VM de desarrollo con FreeBSD 14.3 en la que puedas hacer crashear el kernel de forma segura, una copia del árbol de código fuente de FreeBSD en `/usr/src/` y la capacidad de conectar una consola serie o virtual que conserve la salida tras un reinicio.

### Laboratorio 1: añadir aserciones a bugdemo

En este laboratorio construimos la primera versión de `bugdemo` y añadimos aserciones que detectan inconsistencias internas. El objetivo es ver `KASSERT`, `MPASS` y `CTASSERT` funcionando en la práctica.

**Paso 1: construir y cargar el driver de referencia.**

El driver de referencia se encuentra en `examples/part-07/ch34-advanced-debugging/lab01-kassert/`. Es un pseudo-dispositivo mínimo con un único ioctl que desencadena un bug cuando se le indica. Desde el directorio del laboratorio:

```console
$ make
$ sudo kldload ./bugdemo.ko
$ ls -l /dev/bugdemo
```

Si aparece el nodo de dispositivo, el driver se cargó correctamente.

**Paso 2: ejecutar la herramienta de prueba para confirmar que el driver funciona.**

El laboratorio también contiene un pequeño programa en espacio de usuario, `bugdemo_test`, que abre el dispositivo y emite ioctls:

```console
$ ./bugdemo_test hello
$ ./bugdemo_test noop
```

Ambos deberían devolver éxito. Sin que se desencadene ningún bug, el driver se comporta correctamente.

**Paso 3: inspeccionar las aserciones en el código fuente.**

Abre `bugdemo.c` y localiza la función `bugdemo_process`. Verás algo parecido a esto:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        KASSERT(sc != NULL, ("bugdemo: softc missing"));
        KASSERT(cmd != NULL, ("bugdemo: cmd missing"));
        KASSERT(cmd->op < BUGDEMO_OP_MAX,
            ("bugdemo: op %u out of range", cmd->op));
        MPASS(sc->state == BUGDEMO_STATE_READY);
        /* ... */
}
```

Cada aserción documenta un invariante. Si alguna se activa, el kernel entra en pánico con un mensaje que identifica el invariante roto.

**Paso 4: desencadenar una aserción.**

El driver tiene un ioctl llamado `BUGDEMO_FORCE_BAD_OP` que establece intencionadamente `cmd->op` en un valor fuera de rango antes de llamar a `bugdemo_process`:

```console
$ ./bugdemo_test force-bad-op
```

Con un kernel de depuración, esto produce un pánico inmediato:

```text
panic: bugdemo: op 255 out of range
```

y el sistema se reinicia. En un kernel de producción (sin `INVARIANTS`), el `KASSERT` se elimina en la compilación y el driver continúa con un valor fuera de rango. La diferencia es exactamente el valor de tener un kernel de depuración durante el desarrollo.

**Paso 5: confirmar que la aserción se activa en la línea correcta.**

Tras el reinicio, si se capturó un volcado, ábrelo con `kgdb`:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
(kgdb) bt
```

El backtrace mostrará `bugdemo_process`, y `frame N` hacia esa entrada mostrará la línea de la aserción. Esta es la cadena completa: la aserción se dispara, el kernel entra en pánico, el volcado captura el estado y `kgdb` identifica el código.

**Paso 6: Añade tu propia aserción.**

Modifica el driver para añadir una aserción que compruebe que un contador no es cero en una ruta de código específica. Vuelve a compilar, recarga y provoca un caso que haga que ese contador valga cero. Observa que tu aserción se dispara tal como se esperaba.

**Qué enseña este laboratorio.** La macro `KASSERT` es una comprobación real, no teórica. Se dispara, provoca un pánico e identifica el código. La disciplina de añadir aserciones está respaldada por la disciplina de comprobar que se disparan cuando deben.

### Laboratorio 2: Captura y análisis de un panic con kgdb

En este laboratorio nos centramos en el flujo de trabajo post mortem. Partiendo de un kernel de depuración limpio, provocamos un panic, capturamos el volcado y lo analizamos con `kgdb`.

**Paso 1: Confirma que el dispositivo de volcado está configurado.**

En la VM, ejecuta:

```console
# dumpon -l
```

Si la salida muestra una ruta de dispositivo (normalmente la partición swap), estás listo. Si no, configura una:

```console
# dumpon /dev/ada0p3        # replace with your swap partition
# echo 'dumpdev="/dev/ada0p3"' >> /etc/rc.conf
```

**Paso 2: Confirma que el kernel de depuración está en ejecución.**

```console
# uname -v
# sysctl debug.debugger_on_panic
```

`debug.debugger_on_panic` debería ser `0` o `1` dependiendo de si quieres pausar en el depurador antes del volcado. Para un laboratorio automatizado, `0` es más sencillo; para exploración interactiva, `1` es más instructivo.

```console
# sysctl debug.debugger_on_panic=0
```

**Paso 3: Carga bugdemo y provoca un panic.**

```console
# kldload ./bugdemo.ko
# ./bugdemo_test null-softc
panic: bugdemo: softc missing
Dumping ...
Rebooting ...
```

El mensaje de panic, el aviso de volcado y el reinicio aparecen todos en la consola. La escritura del volcado tarda unos segundos en una VM con disco virtual.

**Paso 4: Tras el reinicio, inspecciona el volcado guardado.**

```console
# ls /var/crash/
bounds  info.0  info.last  minfree  vmcore.0  vmcore.last
# cat /var/crash/info.0
```

El archivo `info.0` resume el panic: la versión del kernel, el mensaje y el backtrace inicial capturado antes del volcado.

**Paso 5: Abre el volcado en kgdb.**

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

`kgdb` ejecuta automáticamente un backtrace. Identifica el frame que está dentro de `bugdemo_ioctl` o `bugdemo_process`. Cambia a él:

```console
(kgdb) frame 5
(kgdb) list
(kgdb) info locals
(kgdb) print sc
```

Observa que `sc` es NULL, lo que confirma el mensaje de panic.

**Paso 6: Explora el estado adyacente.**

Desde `kgdb`, examina el proceso que provocó el panic:

```console
(kgdb) info threads
(kgdb) thread N       # where N is the panicking thread
(kgdb) proc          # driver-specific helper for process state
```

`proc` es un comando específico del kernel que imprime el proceso actual. Entre estos comandos y `bt`, puedes construir una imagen completa del contexto del panic.

**Paso 7: Sal de kgdb.**

```console
(kgdb) quit
```

El volcado permanece en disco; puedes abrirlo de nuevo en cualquier momento.

**Qué enseña este laboratorio.** El ciclo completo de panic, volcado y análisis sin conexión es rutinario, no misterioso. Una VM de desarrollo debería poder completar este ciclo en menos de un minuto. La disciplina consiste en practicarlo antes del primer error real, para que cuando lo necesites no estés aprendiendo la herramienta con prisa.

### Laboratorio 3: Compilación de GENERIC-DEBUG y verificación de las opciones activas

Este laboratorio trata sobre la configuración del kernel, no sobre el código. El objetivo es recorrer el proceso completo de compilación, instalación y validación de un kernel de depuración.

**Paso 1: Parte de un árbol `/usr/src/` limpio.**

Si ya tienes un árbol de código fuente, actualízalo. Si no, instala uno con:

```console
# git clone --depth 1 -b releng/14.3 https://git.freebsd.org/src.git /usr/src
```

**Paso 2: Revisa la configuración GENERIC-DEBUG existente.**

```console
$ ls /usr/src/sys/amd64/conf/GENERIC*
$ cat /usr/src/sys/amd64/conf/GENERIC-DEBUG
```

Observa que solo tiene dos líneas: `include GENERIC` e `include "std.debug"`. Revisa `std.debug` a continuación:

```console
$ cat /usr/src/sys/conf/std.debug
```

Confirma las opciones que hemos tratado: `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS` y demás.

**Paso 3: Compila el kernel.**

```console
# cd /usr/src
# make buildkernel KERNCONF=GENERIC-DEBUG
```

En una VM modesta, esto lleva entre veinte y cuarenta minutos. El build produce una salida detallada; si se detiene con un error, investiga y vuelve a intentarlo.

**Paso 4: Instala el kernel.**

```console
# make installkernel KERNCONF=GENERIC-DEBUG
# ls -l /boot/kernel/kernel /boot/kernel.old/kernel
```

El kernel anterior se conserva en `/boot/kernel.old/` como opción de recuperación.

**Paso 5: Reinicia con el nuevo kernel.**

```console
# shutdown -r now
```

Tras el reinicio, confirma:

```console
$ uname -v
$ sysctl debug.kdb.current_backend
$ sysctl debug.kdb.supported_backends
```

El backend debería listar tanto `ddb` como `gdb`.

**Paso 6: Confirma que INVARIANTS está activo.**

Compila y carga el `bugdemo.ko` del lab01, luego activa la operación fuera de rango como en el Laboratorio 1. En un kernel de depuración, el panic se dispara. En un kernel de producción, no. Este ciclo completo confirma que `INVARIANTS` está genuinamente compilado.

**Paso 7: Confirma que WITNESS está activo.**

La variante lab03 de `bugdemo` tiene una inversión deliberada del orden de los locks, activada por un ioctl específico. Cárgala, ejecuta la prueba desencadenante y observa si aparece un aviso de `WITNESS` en la consola:

```text
lock order reversal:
 ...
```

No se produce ningún panic, solo un aviso. Este es el comportamiento esperado: `WITNESS` detecta posibles deadlocks y los notifica, sin forzar un fallo del sistema.

**Paso 8: Recuperación si el nuevo kernel no arranca.**

Si tu nuevo kernel no arranca por algún motivo, el boot loader de FreeBSD ofrece una opción de recuperación. En el menú del cargador, selecciona "Boot Kernel" y luego "kernel.old". Tu kernel anterior arranca y puedes investigar el fallo del kernel de depuración con calma.

**Qué enseña este laboratorio.** Compilar un kernel de depuración no es una operación misteriosa. Es una recompilación con diferentes opciones y un reinicio. Los riesgos son predecibles: tiempos de compilación largos, binarios de gran tamaño y la necesidad de mantener el kernel anterior disponible como respaldo.

### Laboratorio 4: Trazado de bugdemo con DTrace y ktrace

Este laboratorio ejercita las tres herramientas de trazado que hemos estudiado: las sondas `fbt` de DTrace, las sondas SDT de DTrace y `ktrace(1)`.

**Paso 1: Carga una variante de bugdemo con sondas SDT.**

La variante `lab04-tracing` de bugdemo define sondas SDT en puntos clave:

```c
SDT_PROVIDER_DEFINE(bugdemo);
SDT_PROBE_DEFINE2(bugdemo, , , cmd__start, "struct bugdemo_softc *", "int");
SDT_PROBE_DEFINE3(bugdemo, , , cmd__done, "struct bugdemo_softc *", "int", "int");
```

Cárgala:

```console
# kldload ./bugdemo.ko
```

**Paso 2: Lista las sondas.**

```console
# dtrace -l -P sdt -n 'bugdemo:::*'
```

Deberías ver las sondas `cmd-start` y `cmd-done` en la lista.

**Paso 3: Observa cómo se activan las sondas.**

En un terminal:

```console
# dtrace -n 'sdt:bugdemo::cmd-start { printf("op=%d\n", arg1); }'
```

En otro terminal:

```console
$ ./bugdemo_test noop
$ ./bugdemo_test hello
```

El primer terminal muestra cada sonda activándose con su valor op.

**Paso 4: Mide la latencia por operación.**

```console
# dtrace -n '
sdt:bugdemo::cmd-start
{
        self->start = timestamp;
}

sdt:bugdemo::cmd-done
/self->start != 0/
{
        @by_op[arg1] = quantize(timestamp - self->start);
        self->start = 0;
}
'
```

Ejecuta una carga de trabajo con muchos ioctls y luego pulsa Ctrl-C sobre DTrace. Se imprime una agregación con un histograma de latencia por operación.

**Paso 5: Usa fbt para trazar las entradas.**

```console
# dtrace -n 'fbt::bugdemo_*:entry { printf("%s\n", probefunc); }'
```

Lanza algunos ioctls desde el espacio de usuario. El terminal de DTrace muestra cada entrada, ofreciéndote una vista en vivo del flujo del driver.

**Paso 6: Usa ktrace para trazar el lado del espacio de usuario.**

```console
$ ktrace -t ci ./bugdemo_test hello
$ kdump
```

Observa las llamadas ioctl visibles en la salida de kdump.

**Paso 7: Combina ktrace y DTrace.**

Ejecuta DTrace en un terminal, observando las sondas SDT, mientras ejecutas ktrace en otro sobre la prueba del espacio de usuario. Las dos salidas, leídas juntas, ofrecen una imagen completa de la interacción desde el espacio de usuario hasta el kernel y de vuelta.

**Qué enseña este laboratorio.** El trazado no es una única herramienta; es toda una familia. DTrace es la más completa, `ktrace(1)` es la forma más sencilla de ver el límite entre el espacio de usuario y el kernel, y combinarlas ofrece la visión más completa.

### Laboratorio 5: Detección de un use-after-free con memguard

Este laboratorio recorre un escenario real de depuración de memoria. La variante `lab05-memguard` de `bugdemo` contiene un error use-after-free deliberado: bajo ciertas secuencias de ioctl, el driver libera un buffer y luego lo lee desde un callout.

**Paso 1: Compila un kernel con DEBUG_MEMGUARD.**

Añade `options DEBUG_MEMGUARD` a tu configuración `GENERIC-DEBUG` o crea una nueva:

```text
include GENERIC
include "std.debug"
options DEBUG_MEMGUARD
```

Recompila e instala como en el Laboratorio 3.

**Paso 2: Carga el bugdemo del lab05 y activa memguard.**

```console
# kldload ./bugdemo.ko
# sysctl vm.memguard.desc=bugdemo
```

El segundo comando indica a `memguard(9)` que proteja todas las asignaciones realizadas con el tipo malloc `bugdemo`. El nombre exacto del tipo proviene de la llamada `MALLOC_DEFINE` del driver.

**Paso 3: Provoca el use-after-free.**

```console
$ ./bugdemo_test use-after-free
```

La llamada desde el espacio de usuario regresa rápidamente. Un momento después (cuando el callout se dispara), el kernel entra en panic con un fallo de página dentro de la rutina callout:

```text
Fatal trap 12: page fault while in kernel mode
fault virtual address = 0xfffff80002abcdef
...
KDB: stack backtrace:
db_trace_self_wrapper()
...
bugdemo_callout()
...
```

`memguard(9)` ha convertido un use-after-free silencioso en un fallo de página inmediato. El backtrace apunta directamente a `bugdemo_callout`.

**Paso 4: Analiza el volcado con kgdb.**

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
(kgdb) bt
(kgdb) frame N      # into bugdemo_callout
(kgdb) list
(kgdb) print buffer
```

La línea fuente muestra la lectura de `buffer`, y `buffer` es una dirección liberada y protegida por `memguard`. `kgdb` la muestra como una dirección que ya no está mapeada.

**Paso 5: Corrige el error y verifica.**

La solución es cancelar el callout antes de liberar el buffer. Modifica el código fuente del driver en consecuencia, recompila, recarga y ejecuta la misma prueba. El panic ya no se produce. Mantén `memguard` habilitado durante la verificación, luego desactívalo y vuelve a probar:

```console
# sysctl vm.memguard.desc=
```

Ambas ejecuciones deberían tener éxito. Si la ejecución en modo de producción (sin `memguard`) sigue fallando, el error no está completamente corregido.

**Paso 6: Cuenta las asignaciones en curso.**

El laboratorio también muestra una técnica alternativa: contar las asignaciones en curso. Añade un `counter(9)` al driver, incrementa en la asignación y decrementa en la liberación. En la descarga, comprueba que el contador sea cero:

```c
KASSERT(counter_u64_fetch(bugdemo_inflight) == 0,
    ("bugdemo: leaked %ld buffers",
     (long)counter_u64_fetch(bugdemo_inflight)));
```

Descarga sin liberar primero todos los buffers y observa cómo se dispara la aserción.

**Qué enseña este laboratorio.** `memguard(9)` es una herramienta específica para una clase específica de error. Cuando es aplicable, convierte errores difíciles en fáciles. Saber cuándo recurrir a ella es la habilidad práctica fundamental.

### Laboratorio 6: Depuración remota con el stub GDB

Este laboratorio demuestra la depuración remota a través de un puerto serie virtual. Asume que utilizas bhyve o QEMU con una consola serie expuesta al host.

**Paso 1: Configura KDB y GDB en el kernel.**

Ambos deberían estar ya presentes en `GENERIC-DEBUG`. Confírmalo con:

```console
# sysctl debug.kdb.supported_backends
```

**Paso 2: Configura la consola serie en la VM.**

En bhyve, añade `-l com1,stdio` al comando de lanzamiento, o el equivalente. En QEMU, usa `-serial stdio` o `-serial pty`. El objetivo es disponer de un puerto serie virtual accesible desde el host.

**Paso 3: En la VM, cambia al backend GDB.**

```console
# sysctl debug.kdb.current_backend=gdb
```

**Paso 4: En la VM, entra en el depurador.**

Envía la secuencia de interrupción hacia el depurador en la consola serie o provoca un panic:

```console
# sysctl debug.kdb.enter=1
```

El kernel se detiene. La consola serie muestra:

```text
KDB: enter: sysctl debug.kdb.enter
[ thread pid 500 tid 100012 ]
Stopped at     kdb_enter+0x37: movq  $0,kdb_why
gdb>
```

**Paso 5: En el host, conecta kgdb.**

```console
$ kgdb /boot/kernel/kernel
(kgdb) target remote /dev/ttyXX    # the host-side serial device
```

`kgdb` en el host se conecta al kernel a través de la línea serie. Ahora puedes ejecutar comandos completos de `kgdb` sobre el kernel en vivo: `bt`, `info threads`, `print`, `set variable`, etcétera.

**Paso 6: Establece un punto de interrupción.**

```console
(kgdb) break bugdemo_ioctl
(kgdb) continue
```

La VM se reanuda. En la VM, ejecuta `./bugdemo_test hello`. El punto de interrupción se activa y `kgdb` en el host muestra el estado.

**Paso 7: Desconecta limpiamente.**

```console
(kgdb) detach
(kgdb) quit
```

En la VM, el kernel retoma su ejecución.

**Qué enseña este laboratorio.** La depuración remota es una herramienta especializada pero muy valiosa. Resulta especialmente útil cuando necesitas inspeccionar en vivo un kernel en ejecución, en particular para errores intermitentes difíciles de capturar como volcados.

## Ejercicios de desafío

Los siguientes desafíos se basan en los laboratorios. Son abiertos por diseño: existen múltiples enfoques válidos, y el objetivo es practicar la elección de la herramienta adecuada para cada error.

### Desafío 1: Encuentra el error silencioso

La variante `lab-challenges/silent-bug` de `bugdemo` contiene un error que no produce ni crash ni mensaje de error. En cambio, un contador informa a veces de un valor incorrecto tras una secuencia ioctl concreta. Tu tarea:

1. Escribe un programa de prueba que reproduzca el error.
2. Usa DTrace para identificar qué función produce el valor incorrecto del contador.
3. Corrige el error y verifica que la firma DTrace desaparece.

Pista: el error es una barrera de memoria ausente, no un lock que falta. El síntoma es de coherencia de caché, no de contención.

### Desafío 2: Caza la fuga

La variante `lab-challenges/leaky-driver` pierde un objeto cada vez que se recorre una ruta ioctl concreta. Tu tarea:

1. Confirma la fuga usando `vmstat -m` antes y después de una carga de trabajo.
2. Usa DTrace para registrar cada asignación y liberación del tipo de objeto perdido, agrupadas por pila de llamadas.
3. Identifica la ruta de código que asigna sin liberar.
4. Añade una comprobación en curso basada en `counter(9)` al driver y verifica que se activa cuando se toma la ruta con el error.

### Desafío 3: Diagnostica el deadlock

La variante `lab-challenges/deadlock` a veces se bloquea cuando se ejecutan dos ioctls de forma simultánea. Tu tarea:

1. Reproduce el bloqueo.
2. Conéctate al kernel bloqueado con `kgdb` (o entra en `DDB`).
3. Usa `info threads` y `bt` en cada thread bloqueado para identificar el orden de los locks.
4. Determina la solución (reordenar los locks o eliminar uno de ellos).

### Desafío 4: Lee un panic real

Carga un módulo del kernel que no hayas escrito tú (por ejemplo, uno de los drivers de clase USB o un módulo de sistema de archivos). Provoca deliberadamente una interacción incorrecta enviando entrada malformada desde el espacio de usuario. Cuando entre en panic (o si no lo hace), redacta:

1. La secuencia exacta que provocó el síntoma.
2. El backtrace o error observado.
3. Si el módulo contaba con assertions que habrían detectado el problema antes.
4. Una sugerencia para reforzar los invariantes del módulo.

### Desafío 5: Crea tu propia variante de bugdemo

Crea una nueva variante de `bugdemo` que contenga un bug que hayas encontrado en código real. Escribe un programa de prueba que provoque el bug de forma determinista. Después, usando cualquier subconjunto de las técnicas de este capítulo, diagnostica el bug desde cero. Documenta lo que hayas aprendido. El objetivo es practicar la conversión de «reconozco este patrón» en material didáctico reproducible.

## Resolución de problemas comunes

Incluso las mejores herramientas se topan con problemas en la práctica. Esta sección recoge los problemas que es más probable que encuentres y cómo resolverlos.

### El volcado no se está capturando

Tras un panic, `/var/crash/` muestra únicamente `bounds` y `minfree`, pero ningún `vmcore.N`. Posibles causas:

- **No hay ningún dispositivo de volcado configurado.** Ejecuta `dumpon -l` tras un arranque normal. Si informa de "no dump device configured", configura uno con `dumpon /dev/DEVICE` y hazlo persistente en `/etc/rc.conf` con `dumpdev=`.
- **El dispositivo de volcado es demasiado pequeño.** Un volcado necesita espacio igual a la memoria del kernel más cierta sobrecarga. Una partición de swap de 1 GB no podrá almacenar el volcado de una máquina con 8 GB de memoria. Amplía el dispositivo de volcado o utiliza un volcado comprimido (`dumpon -z`).
- **savecore desactivado.** Comprueba `/etc/rc.conf` en busca de `savecore_enable="NO"`. Cámbialo a `YES` y reinicia.
- **El fallo es demasiado grave.** Si el propio panic impide que el mecanismo de volcado se ejecute, es posible que no veas ninguna salida. En ese caso, una consola serie es imprescindible para capturar al menos el mensaje de panic.

### kgdb indica «No Symbols»

Al abrir un volcado, `kgdb` imprime "no debugging symbols found" o algo similar. Posibles causas:

- **El kernel se compiló sin `-g`.** Los kernels de depuración incluyen `-g` automáticamente mediante `makeoptions DEBUG=-g`. Los kernels de versión final no. O bien construye un kernel de depuración o instala el paquete de símbolos de depuración si está disponible.
- **Desajuste entre el kernel y el volcado.** Si el volcado proviene de un kernel distinto al que está cargando `kgdb`, los símbolos no coincidirán. Utiliza el binario exacto del kernel que estaba en ejecución en el momento del panic.
- **Faltan los símbolos del módulo.** Si el panic se produjo dentro de un módulo que se compiló sin `-g`, `kgdb` mostrará direcciones sin líneas de código fuente para ese módulo. Recompila el módulo con `DEBUG_FLAGS=-g`.

### DDB congela el sistema

Entrar en `DDB` detiene el kernel de forma intencionada. Esto es por diseño, pero en un sistema similar a producción puede parecer un cuelgue. Si estás en `DDB` y quieres continuar:

- `continue` sale de `DDB` y devuelve el control al kernel.
- `reset` reinicia inmediatamente.
- `call doadump` fuerza un volcado y después reinicia.

Si has entrado en `DDB` accidentalmente, `continue` es casi siempre la acción correcta.

### Un módulo se niega a descargarse

`kldunload bugdemo` devuelve `Device busy`. Causas:

- **Descriptores de archivo abiertos.** Algo todavía tiene `/dev/bugdemo` abierto. Usa `fstat | grep bugdemo` para encontrar los procesos y cerrarlos.
- **Contadores de referencias.** Otro módulo hace referencia a este. Descarga primero ese módulo.
- **Trabajo pendiente.** Hay un callout o taskqueue todavía programado. Espera a que se vacíe, o haz que el driver lo cancele y lo drene explícitamente en su ruta de descarga.
- **Thread bloqueado.** Un thread del kernel lanzado por el driver no ha terminado. Termínalo desde dentro del driver al descargarlo.

### memguard no hace nada

`vm.memguard.desc=bugdemo` está configurado, pero memguard no parece estar detectando ningún bug. Causas:

- **Nombre de tipo incorrecto.** `vm.memguard.desc` debe coincidir exactamente con un tipo pasado a `MALLOC_DEFINE(9)`. Si configuras `vm.memguard.desc=BugDemo` pero el driver usa `MALLOC_DEFINE(..., "bugdemo", ...)`, los nombres no coinciden.
- **El kernel no se compiló con `DEBUG_MEMGUARD`.** El nodo sysctl solo existe si la opción está compilada. Comprueba `sysctl vm.memguard.waste` o similar; si devuelve "unknown oid", la funcionalidad no está compilada.
- **No se ha recorrido la ruta de asignación.** Si la ruta de código donde vive el bug no usa realmente el tipo vigilado, `memguard` no puede detectarlo. Confirma el tipo de asignación con `vmstat -m`.

### DTrace indica «Probe Does Not Exist»

```text
dtrace: invalid probe specifier sdt:bugdemo::cmd-start: probe does not exist
```

Causas:

- **El módulo no está cargado.** Las sondas SDT las define el módulo que las proporciona. Si el módulo no está cargado, las sondas no existen.
- **Nombre de sonda incorrecto.** El nombre en el código fuente tiene dobles guiones bajos (`cmd__start`), pero DTrace usa un guion simple (`cmd-start`). Esta es la regla de conversión: la forma con guion bajo aparece en C, la forma con guion aparece en DTrace.
- **Proveedor no definido.** Si `SDT_PROVIDER_DEFINE(bugdemo)` falta o está en un archivo distinto al de `SDT_PROBE_DEFINE`, las sondas no existirán.

### Las compilaciones del kernel fallan con conflictos de símbolos

Al compilar el kernel con combinaciones de opciones poco habituales, es posible que veas errores de enlazado como "multiple definition of X". Causas:

- **Opciones conflictivas.** Algunas opciones son mutuamente excluyentes. Revisa la documentación de opciones en `/usr/src/sys/conf/NOTES`.
- **Objetos obsoletos.** Los artefactos de compilación antiguos pueden interferir con nuevas compilaciones. Prueba con `make cleandir && make cleandir` en el directorio de compilación del kernel.
- **Inconsistencia del árbol.** Una actualización parcial de `/usr/src/` puede dejar las cabeceras y los fuentes desincronizados. Ejecuta un `svnlite update` o `git pull` completo y vuelve a intentarlo.

### El sistema arranca con el kernel antiguo

Tras ejecutar `installkernel`, reinicias y `uname -v` muestra el kernel antiguo. Causas:

- **La entrada de arranque no se ha actualizado.** El valor por defecto es `kernel`, que apunta al kernel actual. Si instalaste con `KERNCONF=GENERIC-DEBUG` pero no ejecutaste `make installkernel` de forma limpia, el binario antiguo puede seguir en su lugar. Comprueba la marca de tiempo de `/boot/kernel/kernel`.
- **Kernel incorrecto seleccionado en el cargador.** El menú del cargador de FreeBSD tiene una opción "Boot Kernel" que permite seleccionar entre los kernels disponibles. Elige el correcto, o establece `kernel="kernel"` en `/boot/loader.conf`.
- **La partición de arranque no ha cambiado.** En algunos sistemas la partición de arranque es independiente y requiere una copia manual. Comprueba que has instalado en la partición correcta.

### WITNESS informa de falsos positivos

A veces `WITNESS` advierte sobre un orden de locks que sabes que es seguro. Posibles motivos y respuestas:

- **El orden realmente es inseguro, aunque en la práctica sea inofensivo.** `WITNESS` informa de deadlocks potenciales, no de los reales. Un ciclo en el grafo de locks que nunca se ejercita de forma concurrente sigue siendo un bug latente. Refactoriza el sistema de locking.
- **Los locks se adquieren por dirección.** El código genérico que hace locking por puntero puede producir órdenes que dependen de datos en tiempo de ejecución, no de la estructura estática. Consulta `witness(4)` para ver cómo suprimir órdenes específicos con `witness_skipspin` o anulaciones manuales.
- **Múltiples locks del mismo tipo.** Adquirir dos instancias de la misma clase de lock siempre es un problema potencial. Usa `mtx_init(9)` con un nombre de tipo distinto por instancia si necesitas que se traten como clases separadas.

## Integrándolo todo: un recorrido por una sesión de depuración

Antes de terminar, recorramos una sesión de depuración completa que utiliza varias de las técnicas que hemos estudiado. El escenario es ficticio pero realista: un driver que a veces falla con un error engañoso, y lo rastreamos desde el primer síntoma hasta la causa raíz.

### El síntoma

Un usuario informa de que su programa a veces recibe `EBUSY` de un ioctl sobre `/dev/bugdemo`. El programa siempre llama al ioctl de la misma forma y, la mayor parte del tiempo, funciona. Solo bajo carga elevada aparece `EBUSY`, y de forma inconsistente.

### Paso 1: Recopilar evidencias

El primer paso es observar el fenómeno sin perturbarlo. Ejecutamos el programa del usuario bajo `ktrace(1)` para confirmar el síntoma:

```console
$ ktrace -t ci ./user_program
$ kdump | grep ioctl
```

La salida confirma que un ioctl concreto devuelve `EBUSY` de vez en cuando. Ninguna otra llamada en espacio de usuario se comporta mal. Esto nos indica que el bug está en el tratamiento que el kernel hace de ese ioctl, no en la lógica del programa del usuario.

### Paso 2: Formular una hipótesis

El código de error `EBUSY` suele indicar un conflicto de recursos. Leyendo el código fuente del driver, descubrimos que `EBUSY` se devuelve cuando un flag interno indica que una operación anterior sigue en curso. El flag lo borra un callout que completa la operación.

Nuestra hipótesis: bajo carga elevada, el callout se retrasa lo suficiente como para que llegue un nuevo ioctl antes de que el anterior haya completado. El driver no fue diseñado para serializar dichas peticiones, por lo que rechaza a la recién llegada.

### Paso 3: Contrastar la hipótesis con DTrace

Escribimos un script de DTrace que registra el retardo entre ioctls consecutivos y el estado del flag de ocupado en cada entrada:

```console
dtrace -n '
fbt::bugdemo_ioctl:entry
{
        self->ts = timestamp;
}

fbt::bugdemo_ioctl:return
/self->ts != 0/
{
        @[pid, self->result] = lquantize(timestamp - self->ts, 0, 1000000, 10000);
        self->ts = 0;
}
'
```

Ejecutando el programa del usuario bajo carga, observamos que los retornos de `EBUSY` ocurren casi exclusivamente cuando el ioctl anterior completó hace más de 50 microsegundos y el callout aún no ha disparado. Esto corrobora la hipótesis.

### Paso 4: Confirmar con sondas SDT

Añadimos sondas SDT alrededor de la manipulación del flag de ocupado y las observamos:

```console
dtrace -n '
sdt:bugdemo::set-busy
{
        printf("%lld set busy\n", timestamp);
}

sdt:bugdemo::clear-busy
{
        printf("%lld clear busy\n", timestamp);
}

sdt:bugdemo::reject-busy
{
        printf("%lld reject busy\n", timestamp);
}
'
```

La traza muestra un patrón claro: set, reject, reject, reject, clear, set, clear. El clear llega tarde porque el callout compite por un taskqueue compartido con otro trabajo.

### Paso 5: Identificar la solución

Con la evidencia recopilada, la solución es clara. O bien el driver necesita serializar los ioctls entrantes en lugar de rechazarlos (mediante una cola o una espera), o bien necesita completar la operación anterior de forma síncrona en lugar de hacerlo mediante callout.

Optamos por el enfoque de cola porque preserva las ventajas del callout. El driver acumula las peticiones pendientes y las despacha cuando el callout dispara. Bajo carga ligera, nada cambia. Bajo carga elevada, las peticiones esperan en lugar de fallar.

### Paso 6: Implementar y verificar

Modificamos el driver. Ejecutamos el programa del usuario bajo la carga de trabajo original. `EBUSY` ya no aparece. La distribución de latencias de DTrace muestra ahora una cola que refleja el retardo de la cola de espera, lo cual es aceptable para el caso de uso de este driver.

También habilitamos `DEBUG_MEMGUARD` en el tipo malloc del driver y ejecutamos la carga de trabajo durante un tiempo, para asegurarnos de que el código de cola no introduce bugs de memoria. No se dispara ningún fallo.

Por último, ejecutamos la suite de pruebas completa. Todo pasa. El arreglo se confirma con un mensaje descriptivo que explica la causa raíz, no solo el síntoma.

### Lecciones de esta sesión

Hay dos cosas que merece la pena destacar.

En primer lugar, las herramientas que usamos eran relativamente ligeras. No fue necesario ningún volcado de memoria. No entramos en `DDB`. El bug se diagnosticó mediante observación pasiva, DTrace y una lectura atenta. Para muchos bugs de drivers, esta es la forma de una sesión: no un panic dramático, sino un estrechamiento sistemático de hipótesis.

En segundo lugar, el arreglo abordó la causa raíz, no el síntoma. Un arreglo superficial podría haber sido aumentar la prioridad del taskqueue del callout. Eso habría reducido la frecuencia del bug sin eliminarlo. Un arreglo más fundamentado cambia el contrato del driver de «rechazar si está ocupado» a «encolar y servir». Esta es la mentalidad de refactorización que discutimos en la Sección 8: todo bug es un mensaje sobre el diseño.

## Técnicas adicionales que conviene conocer

El capítulo ha cubierto el núcleo del conjunto de herramientas de depuración de FreeBSD. Unas pocas técnicas adicionales no encajaron en la narrativa principal, pero merecen mención, porque las encontrarás tarde o temprano.

### witness_checkorder con listas manuales

`WITNESS` puede ajustarse. En `/usr/src/sys/kern/subr_witness.c` hay una tabla de órdenes de lock conocidos como correctos que el kernel reconoce. Al construir un driver que usa un subsistema bloqueado por un lock existente, añadir el propio lock del driver a esta tabla permite a `WITNESS` verificar el orden combinado a lo largo del driver y el subsistema.

Esto raramente se necesita en drivers pequeños, pero resulta útil para drivers que interactúan profundamente con múltiples subsistemas.

### sysctl debug.ktr

Más allá de simplemente habilitar y deshabilitar clases de `ktr(9)`, hay controles adicionales:

- `debug.ktr.clear=1` borra el buffer.
- `debug.ktr.verbose=1` envía las entradas de traza a la consola en tiempo real, además del anillo.
- `debug.ktr.stamp=1` añade marcas de tiempo a cada entrada.

La combinación de ambos resulta especialmente útil cuando quieres observar una traza en vivo sin tener que ejecutar `ktrdump(8)` repetidamente.

### Más comandos de DDB

`DDB` dispone de un amplio conjunto de comandos que están escasamente documentados.
Algunos resultan especialmente útiles para los autores de drivers:

- `show all procs` lista todos los procesos.
- `show lockedvnods` muestra los vnodes bloqueados en ese momento (útil para
  bugs en drivers de almacenamiento).
- `show mount` muestra los sistemas de archivos montados.
- `show registers` vuelca los registros de la CPU.
- `break FUNC` establece un breakpoint.
- `step` y `next` avanzan una instrucción o una línea.
- `watch` establece un watchpoint en una dirección.

El comando `help` de `DDB` lista todos los comandos disponibles.
Leerla una vez es una buena forma de descubrir funciones que no conocías.

### La opción de kernel KDB_TRACE

`KDB_TRACE` hace que el kernel imprima un stack trace en cada panic, aunque el
operador no interactúe con el depurador. Esto resulta útil en pruebas
automatizadas donde nadie observa la consola. Ya está incluida en `GENERIC`.

### EKCD: volcados cifrados del kernel

Si los volcados del kernel contienen datos sensibles (memoria de procesos,
credenciales, claves), el kernel puede cifrarlos en el momento del volcado. La
opción `EKCD` activa esta función. Se carga una clave pública en tiempo de
ejecución con `dumpon -k`; la clave privada correspondiente se usa en el
momento de `savecore` para descifrar.

Esto importa en sistemas de producción donde los volcados pueden transportarse
por canales no confiables. En una VM de desarrollo no es relevante.

### Salida de depuración ligera: bootverbose

Otra opción de bajo coste es `bootverbose`. Activar `boot_verbose` en el
cargador o `bootverbose=1` mediante sysctl hace que muchos subsistemas del
kernel impriman información de diagnóstico adicional durante el arranque. Si tu
driver todavía no ha llegado al punto en que DTrace sea aplicable,
`bootverbose` puede ayudarte a ver qué hace el driver durante el `attach`.

La forma de hacer que tu propio driver respete `bootverbose` es comprobar
`bootverbose` en el código de probe o attach:

```c
if (bootverbose)
        device_printf(dev, "detailed attach info: ...\n");
```

Este es un patrón bien establecido en los drivers de `/usr/src/sys/dev/`.

## Una mirada más detallada a DDB

El depurador interno del kernel, `DDB`, merece más atención de la que le hemos
prestado hasta ahora. Muchos autores de drivers usan `DDB` únicamente de forma
reactiva, cuando un panic les lleva a él de forma inesperada. Con un poco de
práctica, `DDB` también es una herramienta útil para entrar de forma deliberada
e inspeccionar interactivamente un kernel en funcionamiento.

### Entrar en DDB

Hay varias formas de entrar en `DDB`. Ya hemos visto algunas de ellas:

- Por panic, si `debug.debugger_on_panic` es distinto de cero.
- Mediante un BREAK serie (o `Ctrl-Alt-Esc` en una consola de teclado), cuando
  `BREAK_TO_DEBUGGER` está compilado.
- Mediante la secuencia alternativa `CR ~ Ctrl-B`, cuando
  `ALT_BREAK_TO_DEBUGGER` está compilado.
- De forma programática, con `sysctl debug.kdb.enter=1`.
- Desde el código, llamando a `kdb_enter(9)`.

En desarrollo, la entrada programática es la más cómoda. Puedes entrar en
`DDB` en un punto específico de un script sin necesidad de esperar a que ocurra
un panic.

### El prompt y los comandos de DDB

Una vez dentro, `DDB` muestra un prompt. El prompt estándar es simplemente
`db>`. Los comandos se escriben seguidos de Intro. `DDB` dispone de historial
de comandos (pulsa la flecha arriba en la consola serie) y autocompletado con
Tab para muchos nombres de comandos.

Un primer comando útil es `help`, que lista las categorías de comandos.
`help show` muestra los numerosos subcomandos de `show`. La mayor parte de la
exploración se realiza a través de `show`.

### Recorrer un thread

La tarea de diagnóstico más habitual en `DDB` es recorrer un thread concreto.
Empieza con `ps`, que lista todos los procesos:

```console
db> ps
  pid  ppid  pgrp  uid  state  wmesg   wchan    cmd
    0     0     0    0  RL     (swapper) [...] swapper
    1     0     1    0  SLs    wait     [...] init
  ...
  500   499   500    0  SL     nanslp   [...] user_program
```

Elige el thread que te interese. En `DDB`, cambiar a un thread se hace con el
comando `show thread`:

```console
db> show thread 100012
  Thread 100012 at 0xfffffe00...
  ...
db> bt
```

Esto recorre el stack de ese thread concreto. Una investigación de deadlock en
el kernel suele implicar recorrer cada thread bloqueado para ver dónde está
esperando.

### Inspeccionar estructuras

`DDB` puede desreferenciar punteros e imprimir campos de estructuras si el
kernel se compiló con `DDB_CTF`. Por ejemplo:

```console
db> show proc 500
db> show malloc
db> show uma
```

Cada uno de ellos imprime una vista formateada del estado del kernel
correspondiente. `show malloc` muestra una tabla de tipos malloc y sus
asignaciones actuales. `show uma` hace lo mismo con las zonas UMA. `show proc`
muestra un proceso concreto en detalle.

### Establecer breakpoints

`DDB` admite breakpoints. `break FUNC` establece un breakpoint en la entrada de
una función. `continue` reanuda la ejecución. Cuando el breakpoint se activa,
el kernel vuelve a `DDB` y puedes inspeccionar el estado en ese punto.

Este mecanismo es lo que convierte a `DDB` en un depurador real, no en un
simple inspector de crashes. Con breakpoints puedes pausar el kernel en una
ubicación de código concreta, examinar los argumentos y decidir si continuar.

La contrapartida es que un kernel pausado en `DDB` está realmente parado.
Mientras estás en `DDB`, ningún otro thread se ejecuta. En un servidor de red,
todos los clientes agotarán su tiempo de espera. En un equipo de escritorio, el
GUI se congela. Para depuración local en una VM de desarrollo, esto está bien.
Para cualquier uso remoto o compartido, no lo está.

### Scripting en DDB

`DDB` dispone de una sencilla función de scripting. Puedes definir scripts con
nombre que ejecuten una secuencia de comandos de `DDB`.
`script kdb.enter.panic=bt; show registers; show proc` hace que esos tres
comandos se ejecuten automáticamente cada vez que el depurador entra a causa de
un panic. Esto resulta útil para volcados desatendidos: la salida del script
aparece en la consola y en el volcado, ofreciéndote información sin necesidad
de una sesión interactiva.

Los scripts se almacenan en la memoria del kernel y pueden configurarse en el
arranque mediante `/boot/loader.conf` o en tiempo de ejecución mediante
llamadas a `sysctl`. Consulta `ddb(4)` para conocer la sintaxis exacta.

### Salir de DDB

Cuando hayas terminado, `continue` sale de `DDB` y el kernel se reanuda.
`reset` reinicia el sistema. `call doadump` fuerza un volcado y reinicia.
`call panic` provoca un panic intencionadamente (útil cuando quieres un volcado
del estado actual pero no llegaste a `DDB` por un panic).

Para el desarrollador que trabaja en una VM, `continue` es el comando que debes
recordar. Devuelve la vida al kernel y te permite seguir trabajando.

### DDB frente a kgdb: cuándo usar cada uno

`DDB` y `kgdb` se solapan, pero no son intercambiables.

Usa `DDB` cuando el kernel está en ejecución (o pausado en un evento concreto)
y quieres explorar el estado. `DDB` se ejecuta dentro del kernel y tiene acceso
directo a la memoria del kernel y a los threads. Es la herramienta adecuada
para comprobaciones rápidas de estado, para establecer breakpoints y para
detenerse en eventos concretos.

Usa `kgdb` sobre un volcado de crash, después de que la máquina haya
reiniciado. `kgdb` no tiene acceso a los threads de un sistema en ejecución,
pero dispone de todas las funciones de gdb para el análisis offline: historial
de comandos, navegación por el código fuente, scripting con Python y demás.

Para un kernel en ejecución que no puedes reiniciar, el backend GDB stub de
`KDB` cubre ese hueco: el kernel se pausa y `kgdb` en otra máquina se conecta
por una línea serie para disponer de todas las funciones de gdb sobre el estado
en vivo. Esta es la combinación más potente, pero requiere dos máquinas (o VMs).

## Ejemplo práctico: seguir un puntero nulo

Para poner en práctica todas las herramientas, vamos a recorrer un ejemplo más.
El síntoma: `bugdemo` produce ocasionalmente un panic con
`page fault: supervisor read instruction` y un backtrace a través de
`bugdemo_read`. La dirección del panic es baja, lo que sugiere una
desreferencia de puntero nulo.

### Paso 1: capturar el volcado

Tras el panic, confirmamos que se guardó un volcado:

```console
# ls -l /var/crash/
```

y lo abrimos:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
```

### Paso 2: leer el backtrace

```console
(kgdb) bt
#0  __curthread ()
#1  doadump (textdump=0) at /usr/src/sys/kern/kern_shutdown.c
#2  db_fncall_generic at /usr/src/sys/ddb/db_command.c
...
#8  bugdemo_read (dev=..., uio=..., ioflag=0)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:185
```

El frame interesante es el 8, `bugdemo_read`. El código en la línea 185 es:

```c
sc = dev->si_drv1;
amt = MIN(uio->uio_resid, sc->buflen);
```

### Paso 3: inspeccionar las variables

```console
(kgdb) frame 8
(kgdb) print sc
$1 = (struct bugdemo_softc *) 0x0
(kgdb) print dev->si_drv1
$2 = (void *) 0x0
```

`si_drv1` es NULL en el dev. Este es el puntero privado que establece
`make_dev(9)`; debería haberse establecido durante el attach.

### Paso 4: retroceder en el análisis

```console
(kgdb) print *dev
```

Vemos la estructura del dispositivo. El campo name dice "bugdemo", los flags
parecen correctos, pero `si_drv1` es NULL. Algo lo ha borrado.

### Paso 5: formular una hipótesis

En el código fuente, `si_drv1` se establece una sola vez, en `attach`, y se
lee en cada manejador de `read`, `write` e `ioctl`. Nunca se borra
explícitamente. Sin embargo, en la ruta de descarga, el dispositivo se destruye
con `destroy_dev(9)`, que retorna antes de que los manejadores pendientes
terminen. Si hay un `read` en curso cuando comienza la descarga, el dev puede
quedar parcialmente destruido.

### Paso 6: añadir una aserción

Un `KASSERT` al inicio de `bugdemo_read` captura este caso:

```c
KASSERT(sc != NULL, ("bugdemo_read: no softc"));
```

Con esta aserción en su lugar, el siguiente panic nos proporciona la misma
información sin necesidad de recorrer el volcado. Además, sabemos de inmediato
que la condición es real y no una corrupción aleatoria.

### Paso 7: corregir el bug

La solución real consiste en hacer que la ruta de descarga espere a que los
manejadores pendientes terminen antes de destruir el dispositivo. FreeBSD
proporciona `destroy_dev_drain(9)` exactamente para este propósito. Usándola:

```c
destroy_dev_drain(sc->dev);
```

se garantiza que no haya ningún read ni write en curso cuando se libera el
softc.

### Paso 8: verificar

Carga el driver corregido. Ejecuta lecturas y descargas concurrentes. El panic
no se reproduce. El `KASSERT` permanece en el código como red de seguridad para
futuras refactorizaciones.

### Conclusión

Este flujo de trabajo (capturar, leer, inspeccionar, formular hipótesis,
verificar) es la estructura de la mayoría de las sesiones de depuración
productivas. Cada herramienta desempeña un papel pequeño y concreto. La
disciplina consiste en reunir evidencias antes de actuar, y en dejar aserciones
como testigos para el futuro.

## Hacer que los drivers sean observables desde el primer día

Un tema que recorre todo este capítulo es que el mejor momento para añadir
infraestructura de depuración es antes de necesitarla. Un driver diseñado con
la observabilidad en mente es más fácil de depurar que uno diseñado únicamente
para el rendimiento.

Algunos hábitos concretos favorecen esto.

### Nombra cada tipo de asignador

`MALLOC_DEFINE(9)` requiere un nombre corto y uno largo. El nombre corto es el
que aparece en la salida de `vmstat -m` y como objetivo de `memguard(9)`.
Elegir un nombre descriptivo, único para el driver, facilita el diagnóstico
posterior. Nunca compartas un tipo malloc entre subsistemas no relacionados;
las herramientas no pueden distinguirlos.

### Cuenta los eventos importantes

Cada evento importante en un driver (apertura, cierre, lectura, escritura,
interrupción, error, transición de estado) es un candidato para un
`counter(9)`. Los contadores son baratos, se acumulan con el tiempo y se
exponen a través de sysctl. Un driver con buenos contadores responde a la
mayoría de las preguntas del tipo "¿qué está haciendo esto?" sin necesidad de
herramientas adicionales.

### Declara sondas SDT

Cada transición de estado es candidata a una sonda SDT. A diferencia de las
aserciones o los contadores, las sondas no tienen coste cuando están
desactivadas. Dejarlas en el código fuente durante toda la vida útil del driver
es una ganancia neta: cuando aparece un bug, DTrace puede ver el flujo de
eventos sin necesidad de recompilar.

### Usa mensajes de log coherentes

Los mensajes de `log(9)` deben seguir un formato coherente. Los elementos
esenciales son un prefijo que identifique el driver, un código de error o
estado concreto y suficiente contexto para localizar el problema. Evita el
ingenio en los mensajes de log; un lector bajo presión de tiempo quiere saber
qué ocurrió, no admirar tu prosa.

### Proporciona sysctls útiles

Cada flag interno, cada contador, cada valor de configuración debería exponerse
a través de sysctl salvo que haya una razón concreta para no hacerlo. El
desarrollador que necesite depurar tu driver te lo agradecerá; el que nunca
necesite depurarlo no paga nada por esa exposición.

### Escribe aserciones a medida que avanzas

El mejor momento para añadir un `KASSERT` es cuando la invariante está fresca
en tu mente, es decir, mientras escribes el código. Volver más tarde a añadir
aserciones es menos efectivo porque has olvidado algunas invariantes y has
racionalizado otras como "obvias".

### Expón el estado de la máquina de estados

Todo driver no trivial tiene una máquina de estados. Exponer el estado actual a
través de un sysctl, una sonda SDT en cada transición y un contador por estado
hace que la máquina de estados sea visible tanto para las personas como para las
herramientas. Esto es especialmente importante para los drivers asíncronos, que
es el tema del próximo capítulo.

### Prueba la ruta de descarga

Una ruta de descarga poco robusta es una fuente clásica de crashes. En el
desarrollo, escribe un test que cargue el driver, lo ejercite brevemente y lo
descargue, de forma repetida y bajo diversas condiciones. Si el driver no puede
aguantar cien ciclos de carga y descarga, tiene bugs.

Estos hábitos cuestan algo de tiempo durante el desarrollo y se amortizan con creces en la depuración. Un autor de drivers disciplinado los aplica todos, incluso a drivers que parecen demasiado simples para necesitarlos.

## Lista de lecturas recomendadas

Cada herramienta de este capítulo está documentada con más detalle en su propia página de manual o en su archivo fuente. Eso es una buena noticia: no necesitas cargar con toda la caja de herramientas en la cabeza. Cuando un bug te dirija hacia un subsistema concreto, abrir la página de manual o el archivo fuente adecuado te llevará casi siempre más lejos que cualquier capítulo. La siguiente lista reúne las referencias más importantes para este material, en el orden en que es probable que las necesites.

La página de manual de `witness(4)` es lo primero que debes leer cuando `GENERIC-DEBUG` empieza a imprimir inversiones de orden de lock y quieres entender exactamente qué significa la salida, qué `sysctl` controlan el comportamiento y qué contadores puedes inspeccionar. Documenta los sysctls `debug.witness.*`, el comando `show all_locks` de DDB y el enfoque general que `WITNESS` adopta para llevar sus registros internos. Para la implementación real, `/usr/src/sys/kern/subr_witness.c` es la fuente autoritativa. Leer las estructuras que mantiene y la forma de sus funciones de salida (las que producen las líneas "1st ... 2nd ..." que viste antes en este capítulo) elimina la mayor parte del misterio de un informe de `WITNESS`. El archivo es largo, pero el comentario inicial y las funciones que generan la salida, juntos, cubren la mayor parte de lo que un autor de drivers necesita saber.

Para el perfilado de locks, `lockstat(1)` está documentado en `/usr/src/cddl/contrib/opensolaris/cmd/lockstat/lockstat.1`. La página de manual termina con varios ejemplos resueltos cuyo formato de salida coincide con lo que verás en tus propios sistemas, lo que la convierte en una referencia útil para tener abierta la primera vez que pruebes `-H -s 8` con una carga de trabajo real. Como `lockstat(1)` está respaldado por DTrace, la página de manual de `dtrace(1)` es su compañera natural; puedes expresar las mismas consultas en D puro si necesitas una flexibilidad que las opciones de la línea de comandos de `lockstat` no ofrecen.

Para el trabajo con el depurador del kernel, `ddb(4)` documenta el depurador en el kernel de forma completa, incluyendo todos los comandos integrados, todos los hooks de scripts y todas las formas de entrar al depurador. Ante la duda, lee esta página antes de usar un comando de DDB que no hayas probado antes. Para el análisis post-mortem sin conexión al sistema en vivo, `kgdb(1)` en tu sistema FreeBSD instalado documenta las extensiones específicas del kernel sobre el `gdb` estándar. La capa de acceso subyacente está en libkvm, descrita en `/usr/src/lib/libkvm/kvm_open.3`, que explica tanto el modo de volcado como el modo de kernel en vivo que viste en la Sección 3.

Merece la pena mantener en tu lista de lecturas dos referencias más. La primera es `/usr/src/share/examples/witness/lockgraphs.sh`, un pequeño script de shell incluido en el sistema base que demuestra cómo convertir el grafo de orden de locks acumulado por `WITNESS` en un diagrama visual. Con un driver real, ejecutarlo una sola vez te da una imagen de dónde se sitúan tus locks en relación con el resto de la jerarquía de locks del kernel, y el resultado puede sorprenderte. La segunda es el propio árbol de código fuente de FreeBSD: leer `/usr/src/sys/kern/kern_shutdown.c` (la ruta de pánico y volcado) y `/usr/src/sys/kern/kern_mutex.c` (la implementación del mutex que `WITNESS` instrumenta) ancla todo el flujo de depuración en el código que realmente lo implementa.

Más allá del árbol de código fuente, el FreeBSD Developers' Handbook y el FreeBSD Architecture Handbook contienen artículos más extensos sobre depuración del kernel. Ambos se distribuyen junto con el conjunto de documentación en cualquier sistema FreeBSD y se mantienen actualizados junto con el código fuente. Vale la pena hojearlos al menos una vez, aunque no los leas de principio a fin, porque dan nombre a patrones que luego reconocerás en tus propias sesiones de depuración.

Una nota final sobre cómo elegir las referencias. Las páginas de manual envejecen más despacio que las entradas de blog, y los comentarios en el código fuente envejecen más despacio que las páginas de manual. Cuando dos referencias no coincidan, confía en el código fuente, luego en la página de manual, luego en el handbook y por último en todo lo demás. Esa jerarquía ha servido bien a los desarrolladores de FreeBSD durante décadas, y el hábito te servirá a lo largo del resto de este libro y del resto de tu carrera.

## Cerrando

La depuración avanzada es un oficio que requiere paciencia. Cada herramienta de este capítulo existe porque alguien, en algún lugar, se enfrentó a un bug que no podía encontrarse de ninguna otra manera. `KASSERT` existe porque las invariantes que solo viven en la cabeza del programador no son invariantes. `kgdb` y los volcados de memoria existen porque algunos bugs destruyen la máquina que los produjo. `DDB` existe porque un kernel bloqueado no puede explicarse a sí mismo por ningún otro canal. `WITNESS` existe porque los deadlocks son catastróficos en producción e imposibles de depurar a posteriori. `memguard(9)` existe porque la corrupción silenciosa de memoria era la clase de bug más difícil hasta que alguien construyó una herramienta que la hacía ruidosa.

Ninguna de estas herramientas sustituye a la comprensión. Un depurador no puede decirte qué debería hacer tu driver. Un volcado de memoria no puede decirte cuál es la disciplina de locking correcta. DTrace no puede inferir tu diseño. Las herramientas son instrumentos; tú eres el intérprete. La música es la forma del driver que estás construyendo.

Los hábitos que hacen que este oficio tenga éxito son sencillos y poco espectaculares. Desarrolla sobre un kernel de depuración. Añade aserciones para cada invariante que puedas articular. Captura volcados de forma rutinaria para poder abrirlos sin ceremonia. Lleva un diario cuando estés persiguiendo algo difícil. Lee el código fuente de FreeBSD cuando un mecanismo te resulte misterioso. Recurre a la herramienta más ligera que pueda responder tu pregunta, y pasa a herramientas más pesadas solo cuando las más ligeras se queden cortas.

La depuración es también un oficio social. Un bug que te lleva un día encontrar, documentado con claridad, puede ahorrarle a otro autor una semana. Los buenos mensajes de commit, los casos de prueba detallados y los relatos honestos de lo que funcionó y lo que no son contribuciones a la práctica común. La paciencia histórica del proyecto FreeBSD con los informes de bugs, su hábito de capturar las causas raíz en los logs de commits y su uso consistente de `KASSERT` y `WITNESS` a lo largo de décadas de drivers provienen todos de este hábito colectivo de tratar la búsqueda de bugs como una responsabilidad compartida.

Ahora tienes las herramientas para participar. Carga un kernel de depuración, escoge un driver en `/usr/src/sys/dev/` que te interese y léelo con ojo de depurador. ¿Dónde están las invariantes? ¿Dónde están las aserciones? ¿Dónde está la disciplina de locking? ¿Dónde podría esconderse un bug, y qué herramienta lo detectaría? El ejercicio afina los instintos que el resto de este libro ha ido construyendo.

En el próximo capítulo dejaremos atrás la corrección y veremos cómo los drivers gestionan la I/O asíncrona y el trabajo dirigido por eventos: los patrones con los que un driver atiende a muchos usuarios a la vez sin bloquearse, y las facilidades del kernel que hacen posibles esos diseños. Las habilidades de depuración que has adquirido aquí te serán de gran utilidad en ese territorio, porque el código asíncrono es donde tienden a vivir los bugs de concurrencia más sutiles. Un driver con aserciones sólidas, un orden de locking limpio verificado por `WITNESS` y un conjunto de sondas SDT para trazar su flujo de eventos es también un driver sobre el que resulta mucho más fácil razonar cuando su trabajo se reparte entre callbacks, temporizadores y threads del kernel.

## Puente hacia el Capítulo 35: I/O asíncrona y gestión de eventos

El Capítulo 35 retoma donde este termina. El código síncrono es sencillo de razonar: llega una llamada, el driver realiza su trabajo y la llamada regresa. El código asíncrono no lo es: los callbacks se disparan en momentos impredecibles, los eventos llegan desordenados y el driver debe gestionar un estado que persiste a través de muchos contextos de thread.

La complejidad de los drivers asíncronos es exactamente el tipo de complejidad que se beneficia de las herramientas de este capítulo. Un driver síncrono con un bug puede fallar en un lugar predecible. Un driver asíncrono con un bug puede fallar horas después, en un callback que no tiene ninguna conexión obvia con el comportamiento incorrecto original. `KASSERT` sobre el estado en la entrada de cada callback detecta esos bugs de forma temprana. Las sondas DTrace en cada transición de evento hacen visible la secuencia. `WITNESS` detecta los deadlocks que surgen de forma natural cuando múltiples rutas asíncronas necesitan coordinarse.

En el próximo capítulo conoceremos los bloques constructivos del trabajo asíncrono en FreeBSD: `callout(9)` para temporizadores diferidos, `taskqueue(9)` para trabajo en segundo plano, `kqueue(9)` para la notificación de eventos, y los patrones para usarlos correctamente. Construiremos un driver que atienda a muchos usuarios concurrentes sin bloquearse, y ejercitaremos las técnicas de depuración de este capítulo para mantener esa complejidad bajo control.

Cuando termines el Capítulo 35, tendrás el conjunto completo de herramientas síncronas y asíncronas: drivers que gestionan el tráfico con eficiencia, escalan a muchos usuarios, mantienen la corrección bajo concurrencia y pueden depurarse cuando algo va mal de todas formas. Esa combinación es lo que se necesita para escribir drivers que sobrevivan en producción.

Nos vemos en el Capítulo 35.
