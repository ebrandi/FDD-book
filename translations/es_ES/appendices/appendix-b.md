---
title: "Algoritmos y lógica para la programación de sistemas"
description: "Una guía de patrones sobre las estructuras de datos, el flujo de control y los patrones de razonamiento que aparecen a lo largo del código del kernel y los drivers de FreeBSD."
appendix: "B"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 30
language: "es-ES"
---
# Apéndice B: Algoritmos y lógica para la programación de sistemas

## Cómo usar este apéndice

Los capítulos principales enseñan las primitivas que usa a diario el autor de un driver. Junto a esas primitivas existe una segunda capa de conocimiento que el libro da por sentada sin nombrarla siempre: el pequeño catálogo de estructuras de datos, formas de flujo de control y patrones de razonamiento que aparecen en casi todos los drivers que leerás. Un árbol rojo-negro en un subsistema de memoria virtual, una `STAILQ` de peticiones pendientes en el softc, una escalera `goto out` que deshace una rutina `attach`, un switch sobre un enum de estado que controla un handshake de protocolo. Ninguna de estas ideas es difícil por sí sola. Lo que las hace parecer difíciles es encontrarlas por primera vez insertas en código real del kernel, donde el patrón es implícito y el nombre no aparece en ningún lugar de la página.

Este apéndice es ese nombre que falta. Es una guía de campo breve y práctica sobre los patrones algorítmicos que se repiten en el código del kernel y los drivers de FreeBSD, organizada para que puedas reconocer un patrón cuando lo veas y decidir cuál encaja cuando tengas que escribir uno. No es un libro de texto de informática, ni tampoco un sustituto de los capítulos que ya enseñan las piezas específicas que usarás con mayor frecuencia. Lo que ofrece es una capa intermedia: suficiente reconocimiento de patrones para leer código de drivers con confianza, suficiente modelado mental para elegir la estructura correcta cuando la página en blanco te mira fijamente, y suficiente conciencia de advertencias para evitar los errores que todo autor de drivers comete tarde o temprano.

### Qué encontrarás aquí

El apéndice está organizado por familia de problemas, no por taxonomía abstracta. Cada familia reúne unos pocos patrones relacionados con una estructura breve y coherente:

- **Qué es.** Una o dos frases.
- **Por qué lo usan los drivers.** El papel concreto en el código del kernel o del driver.
- **Úsalo cuando.** Una pista de decisión compacta.
- **Evítalo cuando.** La otra cara de la misma decisión.
- **Operaciones principales.** Los pocos nombres que necesitas tener en la memoria muscular.
- **Trampas comunes.** Los errores que realmente cuestan tiempo.
- **Dónde lo enseña el libro.** Un puntero de vuelta a los capítulos principales.
- **Qué leer a continuación.** Una página de manual o un ejemplo realista del árbol de código fuente.

Cuando un patrón tiene una implementación real en FreeBSD con una interfaz estable, la entrada usa esos nombres directamente. Cuando el patrón es genérico, la entrada sigue anclada en cómo los drivers de FreeBSD lo aplican en la práctica.

### Qué no es este apéndice

No es un primer curso de algoritmos. Si nunca has visto una lista enlazada o una máquina de estados, este apéndice te parecerá demasiado denso; lee primero los Capítulos 4 y 5 y vuelve después. Tampoco es una referencia teórica profunda. Aquí encontrarás muy poco análisis asintótico, porque los autores de drivers raramente eligen entre O(log n) y O(n) sobre el papel. Eligen entre patrones cuyas compensaciones giran principalmente en torno al locking, el comportamiento de la cache y los invariantes. Por último, no es un sustituto del Apéndice A (que cubre las APIs), el Apéndice C (que cubre los conceptos de hardware) o el Apéndice E (que cubre los subsistemas del kernel). Si el patrón que buscas te apunta allí, ve y vuelve.

## Orientación para el lector

Hay tres formas de leer este apéndice, y cada una requiere una estrategia diferente.

Si estás **escribiendo código nuevo**, hojea la familia de problemas que se ajusta a tu situación, lee la entrada o las dos entradas que correspondan, echa un vistazo a la línea de **Trampas comunes** y cierra el apéndice. Con treinta segundos es suficiente. Empieza por el patrón, no por el código.

Si estás **leyendo el driver de otra persona**, trata el apéndice como un traductor. Cuando veas un idioma desconocido, busca la familia que lo nombra, lee **Qué es** y **Operaciones principales**, y sigue adelante. La comprensión completa puede llegar después; ahora mismo el objetivo es formarse un modelo mental de lo que está haciendo el driver.

Si estás **depurando**, lee las líneas de **Trampas comunes** en la familia de patrones relacionada con el error. La mayoría de los bugs en drivers que parecen misteriosos son advertencias ordinarias que el autor no respetó. Un buffer circular con una comprobación `full` obsoleta, una escalera de deshacimiento que libera en el orden incorrecto, una máquina de estados que olvidó un estado transitorio. Las trampas de este apéndice no son exhaustivas, pero cubren las que se repiten.

A lo largo del apéndice se aplican algunas convenciones:

- Las rutas del código fuente se muestran en la forma orientada al libro, `/usr/src/sys/...`, coincidiendo con un sistema FreeBSD estándar.
- Las páginas de manual se citan al estilo habitual de FreeBSD. Las páginas autoritativas de las macros de estructuras de datos viven en la sección 3: `queue(3)`, `tree(3)`, `bitstring(3)`. Las de los servicios del kernel, como `hashinit(9)`, `buf_ring(9)`, `refcount(9)`, viven en la sección 9. La distinción solo importa cuando escribes `man` en el prompt.
- Cuando una entrada apunta al código fuente real como ejemplo de lectura, apunta a un archivo que un principiante puede abrir y navegar. Existen subsistemas más grandes que también usan el patrón; estos se mencionan solo cuando son el lugar canónico para ver el patrón en la práctica.

Con eso en mente, empezamos con las estructuras que un driver usa para mantener el estado.

## Estructuras de datos en el kernel

Un driver sin estado es algo raro. Casi todos los drivers no triviales mantienen una colección de algo: peticiones pendientes, handles abiertos, estadísticas por CPU, configuración por canal, contextos por cliente. La pregunta es qué forma de colección se ajusta al patrón de acceso. El kernel incluye un pequeño conjunto de estructuras de datos en archivos de cabecera al que deberías recurrir por defecto en lugar de crear las tuyas propias. Cada una resuelve un problema diferente; confundirlas es uno de los errores de categoría más comunes en el diseño de drivers.

### La familia `<sys/queue.h>`

**Qué es.** Una familia de macros de listas enlazadas intrusivas. Defines una cabeza y una entrada, insertas la entrada dentro de tu estructura de elemento, y las macros te proporcionan inserción, eliminación y recorrido sin necesidad de asignar memoria en el heap para los nodos de la lista.

La cabecera define cuatro variantes:

- `SLIST` es una lista simplemente enlazada. La más económica, solo hacia adelante, eliminación de elementos arbitrarios en O(n).
- `LIST` es una lista doblemente enlazada. Recorrido hacia adelante y hacia atrás, eliminación de un elemento arbitrario en O(1).
- `STAILQ` es una cola simplemente enlazada con puntero al final. Solo hacia adelante, pero con inserción rápida en la cola. FIFO clásico.
- `TAILQ` es una cola doblemente enlazada con puntero al final. Todas las operaciones que puedas necesitar razonablemente, al coste de dos punteros por elemento.

**Por qué los usan los drivers.** Casi todas las colecciones internas de los drivers del árbol de código fuente son una de estas. Comandos pendientes, handles de archivo abiertos, callbacks registrados, dispositivos hijos. Son predecibles, no requieren asignación de memoria al nivel de la lista, y las macros hacen que el código se lea casi como pseudocódigo.

**Úsalo cuando** tengas una colección de objetos que ya asignas tú mismo y quieras operaciones de lista estándar sin tener que inventarlas. El diseño intrusivo significa que un objeto puede vivir en varias listas a la vez; eso es una característica, no un defecto.

**Evítalo cuando** necesites búsqueda ordenada por clave. Una lista tarda O(n) en buscar. Recurre a un árbol o una tabla hash en su lugar.

**Cómo elegir entre las cuatro.**

- Empieza con `TAILQ` a no ser que tengas una razón para no hacerlo. Es la más común y la más flexible.
- Baja a `STAILQ` cuando sepas que solo vas a hacer push y pop como en una cola. Ahorras un puntero por elemento.
- Usa `LIST` cuando quieras comportamiento doblemente enlazado pero no necesites un puntero al final. Conjuntos desordenados de elementos independientes, por ejemplo.
- Usa `SLIST` cuando cada byte importe y la lista sea corta o el recorrido sea lineal de todos modos. Las rutas rápidas en estructuras con acceso frecuente a veces lo justifican.

**Operaciones principales.** Las macros son uniformes en todas las variantes. Inserción: `TAILQ_INSERT_HEAD`, `TAILQ_INSERT_TAIL`, `TAILQ_INSERT_BEFORE`, `TAILQ_INSERT_AFTER`. Eliminación: `TAILQ_REMOVE`. Recorrido: `TAILQ_FOREACH`, más la variante `_SAFE` cuando puedas eliminar el elemento actual. Las mismas operaciones existen con los prefijos `LIST_`, `STAILQ_` y `SLIST_`. El almacenamiento del campo es un `TAILQ_ENTRY(type)` incrustado en tu struct.

**Trampas comunes.**

- Usar el foreach sin `_SAFE` mientras se eliminan elementos. `TAILQ_FOREACH(var, head, field)` a secas se expande a un bucle `for` que lee `TAILQ_NEXT(var, field)` al final de cada iteración. Si el cuerpo liberó `var`, el siguiente paso desreferencia memoria liberada. `TAILQ_FOREACH_SAFE(var, head, field, tmp)` guarda en caché el puntero al siguiente elemento en `tmp` antes de ejecutar el cuerpo, y es siempre la respuesta correcta cuando el cuerpo del bucle puede eliminar `var`.
- Olvidar que la lista es intrusiva. El mismo elemento no puede estar en dos `TAILQ`s a través del mismo campo de entrada a la vez. Si necesitas eso, define dos campos `TAILQ_ENTRY` con nombres diferentes.
- Mezclar variantes en la misma cabeza. `SLIST_INSERT_HEAD` sobre un `TAILQ_HEAD` compila pero produce corrupción silenciosa. Mantén las macros coherentes con el tipo de cabeza.

**Dónde lo enseña el libro.** Se introduce brevemente en el Capítulo 5 como parte del dialecto C del kernel y se retoma cada vez que un driver en partes posteriores del libro necesita una colección interna.

**Qué leer a continuación.** `queue(3)` para el catálogo completo de macros, y cualquier driver real que mantenga trabajo pendiente. `/usr/src/sys/net/if_tuntap.c` usa estos idiomas extensivamente.

### Árboles rojo-negro con `<sys/tree.h>`

**Qué es.** Un conjunto de macros que generan un árbol binario de búsqueda autoequilibrado, incrustado en tus tipos. La implementación de FreeBSD es de equilibrio por rango, pero expone el prefijo histórico `RB_` y se comporta como un árbol rojo-negro a todos los efectos prácticos.

**Por qué los usan los drivers.** Cuando necesitas búsqueda ordenada en O(log n) por una clave y la clave no es un entero pequeño, el árbol rojo-negro es la opción por defecto. Los mapas de memoria virtual, las colas de ejecución del planificador en algunos subsistemas y los registros de objetos con nombre por subsistema usan todos esta cabecera.

**Úsalo cuando** tengas una colección de objetos indexados por una clave, el conjunto crezca hasta cientos o miles de entradas y te importe el recorrido ordenado además de la búsqueda.

**Evítalo cuando** el conjunto sea pequeño (una docena de elementos) o el espacio de claves sea pequeño y denso. Un recorrido lineal sobre un array caliente en cache suele superar a un árbol en ambos casos, porque el factor constante de seguir punteros es mucho mayor que una lectura secuencial.

**Operaciones principales.** `RB_HEAD(name, type)` define el tipo de cabeza, `RB_ENTRY(type)` define el campo de nodo incrustado, `RB_PROTOTYPE` y `RB_GENERATE` instancian la familia de funciones (insert, remove, find, min, max, next, prev, foreach). También escribes una función de comparación que devuelve negativo, cero o positivo, igual que hace `strcmp(3)`. Una familia variante `SPLAY_` en la misma cabecera proporciona árboles splay cuando la localidad de referencia es importante.

**Trampas comunes.**

- Llamar a `RB_GENERATE` en una cabecera. Eso produce definiciones múltiples en tiempo de enlace. Mantén `RB_GENERATE` en exactamente un archivo `.c` y `RB_PROTOTYPE` en la cabecera que declara el tipo.
- Olvidar que el árbol es intrusivo. Un elemento no puede vivir en dos árboles a través de la misma entrada, y eliminar del árbol incorrecto corrompe ambos en silencio.
- Mantener un puntero a través de una inserción. Las rotaciones de equilibrio no mueven nodos, pero el comparador debe ser coherente; si tu clave cambia mientras el nodo está en el árbol, has invalidado la estructura de búsqueda y toda búsqueda posterior es indefinida.

**Dónde lo enseña el libro.** No se enseña como patrón dedicado en el libro. El lector lo encuentra a través del ejemplo cuando el Apéndice E recorre el subsistema de memoria virtual y cuando los drivers avanzados de la Parte 7 recurren a la búsqueda ordenada.

**Qué leer a continuación.** `tree(3)` para el catálogo de macros, y cualquier subsistema que mantenga un conjunto ordenado de elementos con nombre.

### Árboles radix con `<net/radix.h>`

**Qué es.** Un árbol radix (o Patricia) específico del kernel, usado principalmente por la tabla de enrutamiento para comparar direcciones con prefijos. Un árbol radix se diferencia de un árbol rojo-negro en que indexa por longitud de prefijo en lugar de por orden total, que es exactamente lo que necesita la coincidencia del prefijo más largo. La cabecera reside en `/usr/src/sys/net/radix.h`, junto al resto del código de red, en lugar de en `/usr/src/sys/sys/`.

**Por qué los drivers los usan.** Raras veces, de forma directa. El árbol radix existe porque la pila de red lo necesita, y la maquinaria está especializada para ese uso. Un driver casi nunca crea su propio árbol radix. Es posible que lo encuentres como llamador en código adyacente a la red, o que veas el shim de árbol radix de LinuxKPI en capas de compatibilidad.

**Úsalo cuando** realmente necesites coincidencia de prefijos sobre claves de longitud variable. Enrutamiento, tablas ACL, políticas basadas en prefijos.

**Evítalo cuando** lo que realmente necesitas es búsqueda ordenada por clave exacta. En ese caso, usa `<sys/tree.h>`.

**Operaciones principales.** `rn_inithead`, `rn_addroute`, `rn_delete`, `rn_lookup`, `rn_match`, `rn_walktree`. Son suficientemente especializadas como para que el subsistema de enrutamiento sea la referencia canónica.

**Errores habituales.** La cabecera y las rutinas están estrechamente acopladas a la abstracción de la tabla de enrutamiento. Intentar reutilizar el árbol radix para estructuras de datos ajenas al enrutamiento es casi siempre un error; la abstracción se filtrará de maneras incómodas. El árbol radix de LinuxKPI en `/usr/src/sys/compat/linuxkpi/common/include/linux/radix-tree.h` es una estructura diferente y no debe confundirse con el nativo.

**Dónde trata este tema el libro.** Solo se menciona en el Apéndice E junto con la tabla de enrutamiento. Este apéndice lo nombra para que puedas reconocerlo de pasada.

**Qué leer a continuación.** El código de la tabla de enrutamiento en `/usr/src/sys/net/`.

### Bitmaps y cadenas de bits mediante `<sys/bitstring.h>`

**Qué es.** Una abstracción compacta de conjuntos de bits respaldada por un array de palabras de máquina. Se asigna una cadena de bits de N bits y luego se establecen, borran, comprueban y recorren los bits individuales.

**Por qué los drivers los utilizan.** Siempre que se tiene un universo finito denso y se quiere registrar el estado booleano de cada elemento: mapas de ranuras libres para un pool fijo de descriptores de hardware, asignaciones de interrupciones, números menores o flags de habilitación por canal. Un bitmap usa un bit por elemento y permite búsquedas eficientes del primer hueco libre.

**Úsalo cuando** el universo sea denso, se conozca de antemano y se rastree un estado booleano por elemento. Los bitmaps son especialmente eficaces cuando se necesita "encontrar la primera ranura libre" o "cuántos están activos".

**Evítalo cuando** el universo sea disperso. Un conjunto de unos pocos miles de números de ranuras usadas en un espacio de claves de 32 bits no pertenece a un bitmap; usa una tabla hash o un árbol.

**Operaciones principales.** `bit_alloc` (variante del asignador del kernel) y `bit_decl` (pila), `bit_set`, `bit_clear`, `bit_test`, `bit_nset`, `bit_nclear` para rangos, `bit_ffs` para el primer bit activo, `bit_ffc` para el primer bit libre y `bit_foreach` para la iteración. Los tamaños se calculan con `bitstr_size(nbits)`.

**Escollos habituales.**

- Olvidar que los índices de bits son de base cero y que las macros `bit_ffs` / `bit_ffc` no devuelven un valor, sino que escriben el índice encontrado en `*_resultp` y lo fijan a `-1` cuando no hay coincidencia. Comprueba siempre `if (result < 0)` antes de usar el índice.
- Condiciones de carrera bajo modificaciones concurrentes. `bitstring(3)` no es atómico; si dos contextos pueden establecer o borrar bits de forma concurrente, necesitan un lock o una transferencia coordinada.
- Asignar el tamaño incorrecto. Usa `bitstr_size(nbits)` en lugar de hacer la división manualmente.

**Dónde lo enseña el libro.** No se enseña directamente. Un driver que necesite un bitmap de ranuras libres recurrirá a esta cabecera, y esta entrada es la referencia.

**Qué leer a continuación.** `bitstring(3)`.

### Tablas hash con `hashinit(9)`

**Qué es.** Un pequeño patrón auxiliar para construir tablas hash a partir de listas de `<sys/queue.h>`. `hashinit(9)` asigna un array de cubos `LIST_HEAD` con un número de elementos que es potencia de dos y devuelve el array junto con una máscara. El código hashea la clave a un valor de 32 bits, aplica AND con la máscara y recorre el cubo resultante.

**Por qué los drivers los utilizan.** Cuando se necesita búsqueda desordenada por clave y el conjunto es lo bastante grande para que un recorrido lineal sea demasiado lento. Las cachés de nombres de sistemas de archivos, las tablas de archivos abiertos por proceso y cualquier driver que mantenga un registro indexado por identificador hasheable siguen este patrón.

**Úsalo cuando** el conjunto sea grande, las búsquedas sean frecuentes y el orden no importe. Una tabla hash supera a un árbol en el caso promedio de búsqueda y es más fácil de bloquear de forma gruesa (un lock por cubo, o un lock para toda la tabla cuando las escrituras son raras).

**Evítalo cuando** el conjunto sea pequeño (un árbol o una lista son más sencillos) o se necesite un recorrido ordenado (`RB_FOREACH` es la respuesta, no el hashing).

**Operaciones principales.** `void *hashinit(int elements, struct malloc_type *type, u_long *hashmask);` devuelve el array de cubos y escribe la máscara en `*hashmask`. Cada cubo es un `LIST_HEAD`, por lo que se inserta e itera con las macros `LIST_` habituales. `hashdestroy` lo desmonta. `hashinit_flags` acepta una palabra de flags adicional (`HASH_WAITOK` o `HASH_NOWAIT`) cuando se necesita la variante no bloqueante; `hashinit` por sí solo equivale a `HASH_WAITOK`. `phashinit` y `phashinit_flags` proporcionan una tabla de tamaño primo cuando se prefiere la aritmética modular al enmascaramiento, y escriben el tamaño elegido (no una máscara) a través del puntero de salida. Una pequeña cabecera `<sys/hash.h>` ofrece funciones hash básicas como `hash32_buf` y `hash32_str`, junto con `jenkins_hash` y `murmur3_32_hash`; la familia Fowler-Noll-Vo reside en `<sys/fnv_hash.h>`.

**Escollos habituales.**

- Hashear un puntero directamente. Los bits bajos de un puntero suelen estar alineados y producen una distribución de cubos deficiente. Usa una función hash adecuada sobre el contenido o sobre el puntero desplazado.
- Suponer que `hashinit` usa el tamaño solicitado. Redondea hacia abajo a la potencia de dos más cercana y devuelve la máscara para ese tamaño. Una petición de 100 elementos produce una tabla de 64 cubos, no de 100.
- Olvidar la gestión de locks. Una tabla hash sin protección no es segura para threads. Decide de antemano si quieres un único lock, locks por cubo o algo más sofisticado, y documéntalo en el softc.

**Dónde lo enseña el libro.** Se menciona brevemente en los drivers de la Parte 7 que mantienen un registro por cliente. Esta entrada proporciona el andamiaje para cuando necesites construir uno tú mismo.

**Qué leer a continuación.** `hashinit(9)`, y la implementación en `/usr/src/sys/kern/subr_hash.c`.

## Buffers circulares y en anillo

Los buffers en anillo aparecen por doquier en un driver que media entre hardware y software. Una NIC distribuye un anillo de descriptores para la transmisión de paquetes. Una UART almacena caracteres entre la interrupción que los recibe y el proceso que los consume. Un driver de comandos agrupa peticiones en un anillo para que el motor de hardware las recoja. La forma es siempre la misma: un array de tamaño fijo más un índice de productor y un índice de consumidor que dan la vuelta. Lo que varía es quién es concurrente con quién, qué primitivas se usan y si el anillo debe funcionar sin locks.

### Buffers en anillo con productor único y consumidor único

**Qué es.** Un buffer circular de tamaño fijo donde exactamente un contexto produce y exactamente uno consume. El productor avanza un índice `head` (o `tail`) tras escribir; el consumidor avanza un índice `tail` (o `head`) tras leer; ninguno escribe el índice del otro.

**Por qué los drivers los utilizan.** El manejador de interrupciones produce datos (un carácter recibido, un evento de finalización) y un thread los consume. Esa es la forma SPSC prototípica. Es sin locks por construcción, dado que ningún contexto modifica la variable del otro.

**Úsalo cuando** tengas exactamente un productor y un consumidor, y quieras evitar un lock. Los buffers circulares de dispositivos de caracteres son el caso clásico.

**Evítalo cuando** más de un thread pueda producir o más de un thread pueda consumir. Añadir un segundo productor rompe el invariante; se necesita un lock, o bien `buf_ring(9)`.

**Operaciones principales e invariantes.** Los invariantes habituales son:

- `head` avanza solo el productor; `tail` avanza solo el consumidor.
- `(head - tail) mod capacidad` es el número de ranuras ocupadas.
- `capacidad - (head - tail) mod capacidad` es el número de ranuras libres (menos uno, según la codificación).
- El buffer está vacío cuando `head == tail`.
- El buffer está lleno cuando `(head + 1) mod capacidad == tail`. La ranura sacrificada es la forma de distinguir lleno de vacío.

La codificación alternativa usa una variable `count` separada o bien índices de ejecución libre módulo la capacidad con una máscara (`head & mask`, `tail & mask`). Los índices de ejecución libre convierten la comparación lleno/vacío en una comparación con signo y evitan la ranura sacrificada.

**Escollos habituales.**

- Confundir lleno y vacío sin la convención de la ranura sacrificada. Todo diseño de anillo debe tener una respuesta clara; elige una y escríbela al inicio del archivo.
- No ordenar la memoria correctamente. En las CPU modernas, las escrituras pueden hacerse visibles fuera de orden. El productor debe garantizar que el contenido del buffer sea visible para el consumidor antes de que lo sea el `head` actualizado. En el kernel esto implica una barrera de memoria explícita (`atomic_thread_fence_rel` en el productor, `atomic_thread_fence_acq` en el consumidor), o el uso de las variantes de almacenamiento-liberación y carga-adquisición atómicas.
- Asumir que `capacity` es una potencia de dos cuando el código no lo exige. El enmascaramiento requiere una potencia de dos; el módulo no. El código y el invariante deben coincidir.

**Dónde lo enseña el libro.** El Capítulo 10 construye un buffer circular para el dispositivo de caracteres del driver `myfirst` y enseña la disciplina cabeza/cola/lleno/vacío desde los primeros principios. Se revisita en los Capítulos 11 y 12 cuando el anillo se enfrenta a la sincronización.

**Qué leer a continuación.** El ejemplo de buffer circular del Capítulo 10 en el árbol `examples/` del libro, y cualquier driver pequeño de dispositivo de caracteres que use un anillo para hacer de puente entre el contexto de interrupción y el de proceso.

### `buf_ring(9)` para anillos MPMC

**Qué es.** La biblioteca de buffers en anillo del kernel de FreeBSD. `buf_ring` proporciona un anillo multiproductor-multiconsumidor con encolado sin locks y rutas de desencolado tanto para consumidor único como para múltiples consumidores. Se usa de forma intensiva en iflib, los drivers de red y en otras partes del árbol.

<!-- remove after Chapter 11 revision is durable -->
Consulta el Capítulo 11 para la introducción y la página de manual `buf_ring(9)` para el contrato de concurrencia de referencia.

**Por qué los drivers los utilizan.** Cuando más de un contexto puede encolar (varias CPU enviando paquetes a la misma cola de transmisión) o más de un contexto puede desencolar (varios threads de trabajo vaciando la misma cola de trabajo), no es posible mantener los invariantes SPSC. `buf_ring` realiza el trabajo de comparación e intercambio atómico y oculta los detalles de ordenación de memoria tras una interfaz estable.

**Úsalo cuando** tengas múltiples productores o múltiples consumidores y quieras una ruta sin locks. Los drivers relacionados con la red son el caso canónico.

**Evítalo cuando** realmente tengas un único productor y un único consumidor. El anillo SPSC es más sencillo, más barato y más fácil de razonar.

**Operaciones principales.** `buf_ring_alloc(count, type, flags, lock)` y `buf_ring_free` en la configuración y el desmontaje. `buf_ring_enqueue(br, buf)` encola, devolviendo `0` en caso de éxito o `ENOBUFS` cuando está lleno. `buf_ring_dequeue_mc(br)` desencola de forma segura con múltiples consumidores; `buf_ring_dequeue_sc(br)` es más rápido cuando quien llama garantiza semántica de consumidor único. `buf_ring_peek`, `buf_ring_count`, `buf_ring_empty` y `buf_ring_full` permiten inspeccionar sin extraer.

**Escollos habituales.**

- Llamar a `buf_ring_dequeue_sc` desde varios consumidores a la vez. La ruta SC asume que solo hay un consumidor en ejecución; violar esto corrompe el índice de cola de forma silenciosa.
- Tratar `ENOBUFS` como un error grave. Que `buf_ring_enqueue` devuelva `ENOBUFS` es una señal ordinaria de contrapresión; quien llama debe reintentar, descartar o encolar en una ruta más lenta.
- Olvidar que `buf_ring_count` y `buf_ring_empty` son orientativos. Otra CPU puede encolar o desencolar en el instante siguiente a la comprobación. Diseña quien llama en torno a la consistencia eventual, no a la verdad instantánea.

**Dónde lo enseña el libro.** Mencionado en el Capítulo 10 como la contraparte de nivel de producción del anillo SPSC pedagógico. Se usa en los capítulos de red de la Parte 5 cuando los ejemplos de drivers necesitan semántica de anillo real.

**Qué leer a continuación.** `buf_ring(9)`, y la propia cabecera en `/usr/src/sys/sys/buf_ring.h`. Para un uso real, explora iflib o cualquier driver de NIC de alto rendimiento en `/usr/src/sys/dev/`.

### Aritmética circular y el problema de lleno frente a vacío

Todo anillo debe distinguir el estado lleno del vacío. Los dos estados satisfacen `head == tail` o difieren en una ranura, y la codificación elegida determina qué comparación usar. Conviene nombrar las dos convenciones más habituales para poder reconocerlas en el código.

- **Ranura sacrificada.** El anillo alberga `capacity - 1` ranuras útiles. Vacío es `head == tail`. Lleno es `(head + 1) mod capacity == tail`. Sencillo, barato, funciona con cualquier capacidad.
- **Índices de ejecución libre.** `head` y `tail` cuentan cada operación desde la creación del anillo y nunca dan la vuelta. El índice modular para el acceso al array es `head & mask` (lo que exige que `capacity` sea potencia de dos). El recuento de ocupación es `head - tail` con aritmética sin signo; lleno es `occupied == capacity`; vacío es `occupied == 0`. La ventaja es que nunca se sacrifica una ranura; el coste es que hay que escribir `(uint32_t)(head - tail)` con cuidado.

Ambas convenciones son correctas. Elige una y exprésala en un comentario de una línea al inicio del archivo. Los lectores que conozcan los buffers en anillo sabrán qué buscar; los que no, tendrán el comentario.

## Ordenación y búsqueda

Los drivers ordenan y buscan con menos frecuencia de lo que recorren listas, anillos o árboles. Pero cuando lo hacen, un pequeño conjunto de herramientas cubre casi todos los casos, y saber qué herramienta es la adecuada ahorra tiempo.

### `qsort` y `bsearch` en el kernel

**Qué es.** El kernel proporciona los conocidos `qsort` y `bsearch` de la biblioteca estándar, declarados en `/usr/src/sys/sys/libkern.h`. La variante thread-safe `qsort_r` pasa un puntero de contexto del usuario al comparador.

**Cuándo usarlo.** Cuando necesites ordenar un array en su lugar una vez o de forma ocasional, o cuando necesites búsqueda binaria sobre un array ordenado. Las tablas de ID de dispositivo, la concordancia de compatibilidad y las instantáneas ordenadas encajan bien aquí.

**Cuándo evitarlo.** Cuando la colección tenga una vida larga y cambie con frecuencia. En ese caso necesitas un árbol, no un par ordenación-búsqueda. Evita también `qsort` en rutas críticas: la sobrecarga de llamada a función del comparador domina en arrays pequeños.

**Operaciones principales.** `qsort(base, nmemb, size, cmp)` y `qsort_r(base, nmemb, size, cmp, thunk)` para ordenar; `bsearch(key, base, nmemb, size, cmp)` para buscar. El comparador devuelve un valor negativo, cero o positivo al estilo de `strcmp(3)`. Los tres están declarados en `libkern.h` y tienen páginas de manual de la sección 3 (`qsort(3)`, `bsearch(3)`).

**Errores frecuentes.**

- Comparaciones no estables. `qsort` no garantiza estabilidad. Si el orden entre claves iguales importa, añade un desempate al comparador.
- Pasar un comparador que usa la resta (`a->x - b->x`) cuando los campos son anchos. El desbordamiento de entero puede hacer que un valor negativo parezca positivo. Utiliza comparaciones explícitas (`a->x < b->x ? -1 : a->x > b->x ? 1 : 0`).

**Dónde se enseña esto en el libro.** Se menciona en el Capítulo 4 (el recorrido por C) y en la Parte 7, donde ciertos drivers ordenan una tabla una vez en el attach.

**Qué leer a continuación.** `qsort(3)`, `bsearch(3)`.

### Búsqueda binaria sobre tablas de ID de dispositivo

**Qué es.** Una aplicación especializada de `bsearch`. Un driver mantiene una tabla ordenada de claves `(vendor, device)` o cadenas de compatibilidad, y la ruta de probe realiza una búsqueda binaria para determinar si el hardware es uno de los que soporta.

**Por qué lo usan los drivers.** Un driver PCI puede coincidir con docenas de ID de dispositivo. Un recorrido lineal por la tabla funciona perfectamente en el momento del attach. Hacerlo con `bsearch` es una pequeña mejora, legible y rápida, cuando la tabla crece.

**Úsalo cuando** la tabla de ID tiene más de unos pocos elementos y ordenarla por clave sea sencillo. Puedes ordenarla en tiempo de compilación declarando la tabla estáticamente ordenada, o una sola vez al cargar el módulo con `qsort`.

**Evítalo cuando** la coincidencia no sea una simple comparación de claves. La coincidencia PCI a veces involucra campos de subclase e interfaz de programación; expresa la lógica de coincidencia directamente en lugar de forzarla dentro de un comparador.

**Trampas habituales.** Ordenar en tiempo de ejecución y luego depender de ese orden para la compatibilidad ABI. Si alguna otra parte del código lee la tabla por índice, cambiar el orden la rompe. Declara la tabla como `const` y ordénala en tiempo de build siempre que puedas.

**Dónde lo enseña el libro.** Se referencia en el Capítulo 18 (PCI) y en la discusión sobre coincidencia de dispositivos del Capítulo 6.

**Qué leer a continuación.** La tabla de ID y la ruta de probe de cualquier driver PCI. `/usr/src/sys/dev/uart/uart_bus_pci.c` es un ejemplo legible.

### Cuándo gana la búsqueda lineal

**Qué es.** Un recordatorio de que para colecciones pequeñas, la búsqueda lineal sobre un array caliente en caché es la búsqueda más rápida que puedes escribir. Las cachés de CPU premian las lecturas secuenciales y penalizan el salto entre punteros.

**Úsalo cuando** la colección tenga menos de unas pocas docenas de elementos. En la práctica, un recorrido lineal sobre veinte elementos es más rápido que cualquier búsqueda en árbol, porque el recorrido del árbol provoca varios fallos de caché mientras que el recorrido lineal provoca solo uno.

**Evítalo cuando** la colección crezca sin límite, o cuando el requisito de latencia en el camino crítico sea estricto y el peor caso importe más que el caso promedio.

**Trampas habituales.** Optimizar el lado equivocado. No introduzcas un árbol para curar "la búsqueda lineal es lenta" antes de medir. El coste real casi siempre está en otro sitio.

## Máquinas de estados y manejo de protocolos

Muchos drivers son máquinas de estados disfrazadas. Un dispositivo USB hace probe, se hace el attach, queda inactivo, se reanuda, se suspende, se desconecta. Un driver de red pasa por enlace caído, enlace activo, negociación, estado estable, recuperación de errores. Cuando las transiciones de estado son complicadas, hacer el estado explícito compensa de inmediato. Cuando son simples, un enum y un switch suelen ser suficientes.

### Enum de estado explícito frente a bits de flag implícitos

**Qué es.** Dos formas de representar estado. Un enum dice "este dispositivo está en exactamente uno de N estados". Una palabra de flags dice "este dispositivo tiene cualquier combinación de N booleanos independientes".

**Usa el enum cuando** las condiciones sean mutuamente excluyentes y nombrar las transiciones sea importante. Un enlace es uno de `LINK_DOWN`, `LINK_NEGOTIATING` o `LINK_UP`. Un comando es uno de `CMD_IDLE`, `CMD_PENDING`, `CMD_RUNNING` o `CMD_DONE`. Un enum obliga a que cada transición sea explícita; no puedes estar accidentalmente en dos estados a la vez.

**Usa bits de flag cuando** las condiciones sean independientes y puedan combinarse. "Interrupción habilitada", "autonegociación permitida", "en modo promiscuo". Mezclar condiciones mutuamente excluyentes e independientes en la misma palabra de flags es el error habitual; sepáralas en un enum para las primeras y una palabra de flags para las segundas.

**Trampas habituales.**

- Codificar una máquina de estados como bits de flag y luego tener que decidir qué significa "tanto `CMD_PENDING` como `CMD_DONE` activos". Eso nunca significa nada útil. Cambia a un enum.
- Codificar booleanos independientes como un único estado comprimido. Acabarás reinventando el OR bit a bit, pero mal.

### FSMs basadas en switch

**Qué es.** La máquina de estados explícita más simple: un enum para el estado, un switch en una función que recibe un evento, y un cuerpo por caso que actualiza el estado y ejecuta la acción.

**Úsalo cuando** el espacio de estados sea pequeño (quizá diez estados) y las transiciones sean superficiales. Un switch es fácil de leer, fácil de extender, y el compilador detecta los casos que faltan si activas la advertencia adecuada.

**Evítalo cuando** el switch crezca hasta cientos de líneas o la misma lógica de transición se duplique en varios estados. Esa es la señal para pasar a una FSM dirigida por tablas.

**Trampas habituales.**

- Olvidar un estado. Compila con `-Wswitch-enum` para que el compilador te lo diga.
- Hacer trabajo no trivial dentro del switch. Mantén el switch como un despachador; delega el trabajo real en funciones auxiliares con nombre.

**Dónde lo enseña el libro.** El patrón aparece de forma natural en muchos ejemplos de drivers. El Capítulo 5 lo introduce en su forma más simple.

### FSMs dirigidas por tablas

**Qué es.** La máquina de estados expresada como una tabla bidimensional indexada por `(estado, evento)`. Cada celda contiene el siguiente estado y, típicamente, un puntero a función con la acción que ejecutar en esa transición.

**Úsalo cuando** la FSM tenga muchos estados y muchos eventos y quieras que la lógica de transición sea datos, no código. Visualizar la matriz suele ser más fácil que seguir un switch gigantesco. Las pilas de protocolos, la lógica de enumeración de bus y las máquinas de estados de suspend y resume son usos clásicos.

**Evítalo cuando** la FSM sea pequeña. Un switch de cinco estados es más fácil de leer que una matriz de cinco por cinco.

**Operaciones principales.** Define el enum de estado, el enum de evento y una estructura de transición que contenga `next_state` y `action_fn`. Declara la tabla como un array bidimensional estático constante. El driver avanza la máquina con `table[current_state][event]`.

**Trampas habituales.**

- Punteros de acción nulos sin comprobación. O bien la tabla debe ser densa (cada celda es una transición válida) o bien el despachador debe tratar un puntero nulo como "ignorar", y la convención debe ser coherente.
- Incluir locks dentro de las funciones de acción sin una disciplina clara. La transición de estado es una sección crítica; decide si el llamador mantiene el lock o si el despachador lo toma, y mantén esa decisión de forma consistente.

### Despacho por puntero a función

**Qué es.** Una alternativa más ligera a una tabla de estados completa. El "modo" actual del driver es un puntero a un conjunto de funciones manejadoras; cambiar de estado es tan simple como reasignar el puntero.

**Úsalo cuando** el "estado" sea realmente un conjunto de métodos que el driver ejecuta de forma diferente según el modo. Un enlace que acaba de subir tiene una ruta de recepción diferente a la de uno que aún está negociando. Un dispositivo que está iniciando su firmware despacha comandos de forma diferente a uno que ya está en ejecución.

**Evítalo cuando** las transiciones de estado sean ad-hoc o numerosas. Un intercambio de puntero a función es una operación gruesa; reservarlo para unos pocos modos con nombre bien definidos mantiene el código honesto.

**Trampas habituales.**

- Condición de carrera con el intercambio. Si una CPU llama a través del puntero mientras otra lo está reemplazando, la llamada puede despachar a código obsoleto. El intercambio necesita ordenamiento. En el kernel, eso normalmente significa un lock o un free diferido al estilo RCU mediante `epoch(9)` para cualquier estructura de estado a la que el puntero antiguo hacía referencia.
- Recursos huérfanos. Al intercambiar la tabla de punteros a función, asegúrate de que los recursos que poseía el modo anterior se transfieren o liberan limpiamente. El propio intercambio no libera nada.

### Reentrada y finalización parcial

**Qué es.** Una cuestión de diseño de máquina de estados más que una estructura de datos. Los drivers reales reciben eventos en orden entrelazado: una interrupción se dispara mientras el driver está a mitad de procesar un ioctl; una finalización llega mientras el comando que la desencadenó todavía se está enviando; un detach compite con un open. La máquina de estados tiene que tolerarlo.

**Principios de diseño.**

- Haz que cada transición sea una actualización única y atómica. Lee el estado actual, calcula el siguiente estado, confírmalo con un lock o un atómico. No ejecutes la acción antes de confirmar el estado; si se produce un error o retornas antes, el estado debe reflejar la realidad.
- Separa "qué hay que hacer" de "quién lo ejecuta". La transición de estado decide qué debe ocurrir; un taskqueue o un thread trabajador lo ejecuta realmente. Así la transición puede completarse antes de que empiece el trabajo costoso, y un segundo llamador ve un estado consistente.
- Ten un estado "transitorio" para cada transición que lleve tiempo. Un comando que está siendo enviado no es ni `IDLE` ni `RUNNING`; es `SENDING`. Un enlace que está bajando no es ni `UP` ni `DOWN`; es `GOING_DOWN`. Los estados transitorios dan a los llamadores concurrentes algo con lo que esperar o reintentar.

**Trampas habituales.** Asumir que los eventos están serializados. No lo están. Una interrupción puede llegar en cualquier momento, un detach puede competir con un open, un close desde el espacio de usuario puede competir con el desmontaje interno de un dispositivo. Construye la máquina de estados de modo que cada evento se maneje desde cada estado, aunque el manejo sea simplemente "rechazar" o "diferir".

**Dónde lo enseña el libro.** Se retoma en el Capítulo 11 (sincronización), el Capítulo 14 (interrupciones en profundidad) y en la Parte 5, cuando los drivers de red reales se enfrentan a estos casos con toda su intensidad.

## Patrones de manejo de errores

Un driver que acierta en el camino feliz pero falla en el camino de error corromperá memoria, perderá recursos o provocará un panic del kernel. El buen manejo de errores en el código de un driver es casi siempre una cuestión de estructura, no de ingeniosidad. El kernel tiene un idioma fuerte y uniforme para ello, y esta sección le pone nombre.

### El idioma `goto out`

**Qué es.** El patrón canónico de limpieza del kernel. Al principio de la función, declara cada recurso a `NULL`. En cada adquisición, comprueba el resultado; ante un fallo, haz `goto fail_N`, donde `fail_N` es una etiqueta que libera exactamente los recursos adquiridos hasta ese momento. Al final de la función, el camino de éxito retorna; la escalera de limpieza se ejecuta en orden inverso al de adquisición y cae a través de cada etiqueta hasta el retorno final.

**Por qué lo usan los drivers.** Porque funciona. La escalera hace imposible olvidar una liberación, y el orden de liberación es visualmente idéntico al inverso del orden de adquisición. El patrón está por todas partes en el árbol de FreeBSD.

**Úsalo cuando** una función adquiere más de un recurso. Tres es normalmente el umbral a partir del cual una escalera es más clara que un `if` anidado.

**Evítalo cuando** la función sea simple. Una única adquisición, una única liberación y un único camino de error no necesitan una escalera; una función en línea recta con dos retornos es más clara.

**La forma.**

```c
int
my_attach(device_t dev)
{
    struct my_softc *sc;
    struct resource *mem = NULL;
    void *ih = NULL;
    int error, rid;

    sc = device_get_softc(dev);
    sc->dev = dev;

    rid = 0;
    mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (mem == NULL) {
        device_printf(dev, "cannot allocate memory window\n");
        error = ENXIO;
        goto fail_mem;
    }

    error = bus_setup_intr(dev, sc->irq, INTR_TYPE_MISC | INTR_MPSAFE,
        NULL, my_intr, sc, &ih);
    if (error != 0) {
        device_printf(dev, "cannot setup interrupt: %d\n", error);
        goto fail_intr;
    }

    sc->mem = mem;
    sc->ih  = ih;
    return (0);

fail_intr:
    bus_release_resource(dev, SYS_RES_MEMORY, rid, mem);
fail_mem:
    return (error);
}
```

La forma es lo que importa. Cada etiqueta libera exactamente los recursos adquiridos entre ella y el inicio de la función. El fall-through es intencionado; las etiquetas están apiladas en el orden en que un humano las leería desde la cima de la función.

**Variantes.**

- Una única etiqueta `out:` con comprobaciones condicionales (`if (mem != NULL) bus_release_resource(...)`) funciona para funciones cortas. Para las largas, la variante de etiqueta numerada es más fácil de auditar porque codifica el orden directamente.
- Algunos códigos nombran las etiquetas `err_mem`, `err_intr` en lugar de `fail_mem`, `fail_intr`. El prefijo no importa; la coherencia dentro de un archivo sí.

**Trampas habituales.**

- Liberar en el orden incorrecto. Libera siempre en orden inverso al de asignación. El orden visual de la escalera es un recordatorio, no una prueba; revisa las etiquetas cuando añadas un nuevo recurso.
- Inicializar punteros a algo distinto de `NULL`. Si una escalera libera condicionalmente, la condición tiene que ser fiable. Los punteros no inicializados son comportamiento indefinido; liberar memoria no inicializada provoca un panic.
- Retornar en el camino de éxito con un estado parcial. El camino de éxito al final debe o bien transferir la propiedad completa al llamador, o bien devolver un error y deshacer todo. No existe una tercera opción.

**Dónde lo enseña el libro.** El Capítulo 5 introduce el patrón de forma explícita y el libro lo usa de forma consistente desde el Capítulo 7 en adelante.

**Qué leer a continuación.** Funciones attach reales en `/usr/src/sys/dev/`. Casi cualquier driver muestra el patrón en acción.

### Convenciones de códigos de retorno

**Qué es.** La convención del kernel: códigos de retorno enteros donde `0` significa éxito y un valor positivo de errno significa fallo. `1` no es un error; es un retorno con valor verdadero que resultaría confuso junto a `EPERM` (que también vale `1`). Devuelve siempre `0` en caso de éxito.

**Convenciones principales.**

- `0` en caso de éxito, un errno positivo (`EINVAL`, `ENOMEM`, `ENXIO`, `EIO`, `EBUSY`, `EAGAIN`) en caso de fallo.
- Nunca devuelvas un código de error negativo. Esa es una convención de Linux y no encaja con el resto de FreeBSD.
- Propaga los códigos de error hacia arriba sin modificarlos, salvo que puedas hacerlos más específicos.
- Cuando una función devuelve un puntero, la convención es devolver `NULL` en caso de fallo y establecer un `int` separado si el llamador necesita distinguir el motivo.
- Las rutinas probe siguen una regla distinta: devuelven `BUS_PROBE_DEFAULT` y similares en caso de éxito, y `ENXIO` cuando no hay coincidencia. Consulta el Apéndice A para la lista completa.

**Trampas habituales.**

- Devolver `-1` desde una función del kernel. Ese es un modismo del espacio de usuario; no lo hagas en código del kernel.
- Sobrecargar un único valor de retorno para que signifique a la vez "un error" y "un recuento de recursos". Usa un parámetro de salida separado si necesitas ambas cosas.
- Ignorar los errores. Un código de error procedente de una llamada de nivel inferior es un contrato; o lo gestionas o lo propagas. Devolver `0` tras un `copyin` fallido es la receta para provocar un kernel panic.

**Dónde lo enseña el libro.** El capítulo 5, junto con la escalera de limpieza.

### Adquisición y liberación de recursos en orden

**Qué es.** La disciplina que establece: todo recurso que adquieras en attach debe liberarse en detach, en orden inverso. Todo lock que tomes debe liberarse en todos los caminos de salida. Toda referencia que añadas debe liberarse antes de soltar la última. La escalera de limpieza anterior es una expresión local de este principio; el driver completo es una expresión global.

**Principios fundamentales.**

- Attach adquiere en el orden A, B, C. Detach libera en el orden C, B, A. Los dos son imágenes especulares.
- Cada camino de error dentro de attach deshace lo que se ha adquirido hasta ese punto, en orden inverso. Esa es la escalera.
- Los locks se mantienen el menor tiempo posible y se liberan en todas las salidas, incluidos los caminos de error. Una escalera de `goto` que deshace la memoria pero olvida el mutex sigue siendo incorrecta.
- La propiedad debe ser local. Una función adquiere; la misma función libera, o transfiere la propiedad explícitamente al llamador. Una función que «filtra» la propiedad como efecto secundario de un parámetro es casi siempre un bug en espera de materializarse.

**Dónde lo enseña el libro.** El capítulo 7 introduce la disciplina de imagen especular para un primer driver, y todos los capítulos de drivers posteriores la repiten.

## Patrones de concurrencia

Dos threads que tocan el mismo estado es donde nacen los bugs del kernel. Las primitivas de sincronización del Apéndice A son las herramientas; esta sección recopila los patrones que las utilizan bien. El objetivo aquí es el reconocimiento de patrones, no un curso completo de concurrencia.

### Productor-consumidor con variables de condición

**Qué es.** Un contexto produce datos (llena un buffer, publica un comando, recibe un carácter) y otro los consume. Un estado compartido protegido por un mutex y una variable de condición en la que el consumidor espera cuando el estado está vacío.

**Modelo mental.**

- El mutex protege el estado compartido. Esto no es negociable.
- El consumidor comprueba el estado bajo el mutex. Si no hay nada que hacer, duerme en la variable de condición mediante `cv_wait`. `cv_wait` suelta el mutex de forma atómica mientras duerme y lo readquiere al retornar.
- El productor también mantiene el mutex mientras modifica el estado y luego señaliza la variable de condición.
- El consumidor vuelve a comprobar el estado tras despertar, porque son posibles los despertares espurios y las señales compartidas. La comprobación es siempre un bucle, no un `if`.

**La forma.**

```c
/* Producer. */
mtx_lock(&sc->lock);
put_into_buffer(sc, item);
cv_signal(&sc->cv);
mtx_unlock(&sc->lock);

/* Consumer. */
mtx_lock(&sc->lock);
while (buffer_is_empty(sc))
    cv_wait(&sc->cv, &sc->lock);
item = take_from_buffer(sc);
mtx_unlock(&sc->lock);
```

**Trampas habituales.**

- Usar `if` en lugar de `while` alrededor de `cv_wait`. Los despertares espurios son reales; comprueba siempre.
- Señalizar sin mantener el lock. FreeBSD lo permite, pero casi siempre es más fácil razonar si señalizas mientras mantienes el lock.
- `cv_signal` cuando querías `cv_broadcast`. `cv_signal` despierta exactamente a un esperador; si varios esperadores aguardan condiciones previas distintas, solo uno de ellos será desbloqueado, posiblemente el equivocado.
- Dormir bajo un spinlock. `cv_wait` puede dormir; necesitas un mutex normal (`MTX_DEF`), no un spin mutex (`MTX_SPIN`).

**Dónde lo enseña el libro.** El capítulo 10 construye el patrón de forma implícita alrededor del buffer circular; los capítulos 11 y 12 lo formalizan.

### Lector-escritor: `rmlock(9)` frente a `sx(9)`

**Qué es.** Dos locks de lectura-escritura distintos, con perfiles de coste diferentes.

- `sx(9)` es un lock compartido-exclusivo que puede dormir. Los lectores bloquean a los escritores; los escritores bloquean a los lectores. Ambos lados pueden dormir dentro de la sección crítica.
- `rmlock(9)` es un lock de lectura predominante con un camino de lectura extremadamente barato (esencialmente un indicador por CPU) y un camino de escritura mucho más costoso (el escritor debe esperar a que todos los lectores terminen).

**Cómo elegir entre ellos.**

- Si las lecturas son frecuentes, las escrituras son raras y el camino de lectura no debe dormir, elige `rmlock`.
- Si las lecturas son frecuentes, las escrituras son ocasionales y el camino de lectura necesita dormir (por ejemplo, llama a `copyout`), elige `sx`.
- Si lecturas y escrituras están más o menos equilibradas o si la sección crítica es corta, ninguno de los dos es adecuado. Usa un mutex sencillo.

**Operaciones principales.**

- `sx`: `sx_slock`, `sx_sunlock` para compartido; `sx_xlock`, `sx_xunlock` para exclusivo; `sx_try_slock`, `sx_try_xlock`.
- `rmlock`: `rm_rlock(rm, tracker)`, `rm_runlock(rm, tracker)` para lectores (cada lector necesita su propio `struct rm_priotracker`, generalmente en la pila); `rm_wlock(rm)`, `rm_wunlock(rm)` para escritores.

**Trampas habituales.**

- Usar `rmlock` para una carga de trabajo equilibrada. El camino de escritura es genuinamente lento; úsalo solo cuando las lecturas superen ampliamente a las escrituras.
- Dejar que un lector de `rmlock` duerma sin inicializar con `RM_SLEEPABLE`. El `rmlock` por defecto prohíbe dormir en el camino de lectura.
- Promover un lock compartido a exclusivo. Ni `sx` ni `rmlock` admiten una promoción sin lock. Suelta el lock compartido, adquiere el exclusivo y vuelve a comprobar el estado.

**Dónde lo enseña el libro.** El capítulo 12.

### Conteo de referencias con `refcount(9)`

**Qué es.** Un patrón bendecido por el kernel para contar cuántos contextos siguen usando un objeto. Un contador atómico, `refcount_acquire` para incrementarlo, `refcount_release` para decrementarlo y devolver `true` cuando el conteo llega a cero (para que el llamador pueda liberar).

**Por qué lo usan los drivers.** Siempre que un objeto pueda sobrevivir a la operación que lo produjo. Un `cdev` que se está cerrando mientras una lectura está en curso; un softc cuyo detach compite con un ioctl; un buffer pasado al hardware que no debe liberarse hasta que el hardware haya terminado.

**Úsalo cuando** tengas propiedad compartida y no haya un único lugar que pueda liberar el objeto de forma fiable. Si un fragmento de código posee el objeto de forma inequívoca, omite el conteo de referencias y libéralo allí.

**Operaciones principales.** `refcount_init(count, initial)`, `refcount_acquire(count)` que devuelve el valor anterior, `refcount_release(count)` que devuelve `true` si el conteo llegó a cero (el llamador debe liberar). Existen las variantes `_if_gt` e `_if_not_last` para la adquisición condicional, y `refcount_load` para la inspección de solo lectura.

**Trampas habituales.**

- Usar `refcount` para recursos con una jerarquía de propiedad real. Los conteos de referencias son para la propiedad genuinamente compartida, no para evitar decidir quién libera qué.
- Liberar antes de la bajada. Que `refcount_release` devuelva `true` es tu permiso para liberar; hazlo dentro de esa rama, no antes.
- Mezclar conteos de referencias con locks. Las operaciones de conteo de referencias son lock-free, pero «incrementar el conteo y luego desreferenciar» es una condición de carrera si otro thread puede soltar la última referencia entre los dos pasos. La solución habitual es mantener un lock mientras decides adquirir.

**Dónde lo enseña el libro.** Se menciona en el Apéndice A, con el contexto de concurrencia construido a lo largo de los capítulos 11 y 12.

**Qué leer a continuación.** `refcount(9)`, y cualquier subsistema que gestione tiempos de vida compartidos.

## Ayudas para la toma de decisiones

Las tablas compactas que siguen están pensadas para consultarse de un vistazo. No son oráculos de decisión; son recordatorios de qué familia explorar. La decisión real siempre es local al problema.

### Forma de la colección

| Tienes... | Recurre a |
| :-- | :-- |
| Una colección pequeña sin búsqueda ordenada | `TAILQ_` o `LIST_` de `<sys/queue.h>` |
| Una cola FIFO sin eliminación arbitraria | `STAILQ_` |
| Una colección ordenada por clave que puede crecer mucho | `RB_` de `<sys/tree.h>` |
| Un universo denso de indicadores booleanos | `bit_*` de `<sys/bitstring.h>` |
| Búsqueda desordenada en un conjunto grande | `hashinit(9)` más `LIST_` |
| Claves de red con coincidencia de prefijo | `radix.h` (normalmente no lo escribes tú) |

### Forma del ring buffer

| Tienes... | Recurre a |
| :-- | :-- |
| Un productor, un consumidor, lock-free | Ring SPSC con índices de cabeza/cola |
| Muchos productores, muchos consumidores | `buf_ring(9)` |
| El productor es una interrupción, el consumidor es un thread | Ring SPSC más variable de condición o `selrecord` |
| Cola suave con contrapresión hacia el espacio de usuario | Ring SPSC más integración con `poll(2)` o `kqueue(2)` |

### Representación de estado

| Tu estado es... | Recurre a |
| :-- | :-- |
| Un puñado de modos mutuamente excluyentes | `enum` más `switch` |
| Muchos estados, muchos eventos, transiciones complejas | FSM dirigida por tabla |
| Modos generales que cambian los métodos del driver | Despacho por puntero a función |
| Hechos booleanos independientes | Bits de indicador en un entero |

### Estrategia de limpieza

| Tu función... | Recurre a |
| :-- | :-- |
| Adquiere un recurso | Retorno anticipado único, liberación única |
| Adquiere dos o tres | `goto out:` con `if (ptr != NULL)` condicional |
| Adquiere muchos, sensible al orden | Escalera numerada `goto fail_N:` |
| Tiene que deshacer un éxito parcial | La misma escalera con etiqueta «rollback» explícita |

### Patrón de concurrencia

| Tienes... | Recurre a |
| :-- | :-- |
| Sección crítica corta, sin dormir | `mtx(9)` con `MTX_DEF` |
| Sección crítica corta en un filtro de interrupción | `mtx(9)` con `MTX_SPIN` |
| Muchos lectores, escritor ocasional, puede dormir | `sx(9)` |
| Muchos lectores, escritor raro, sin dormir en lectores | `rmlock(9)` |
| Un productor, un consumidor, esperando un evento | mutex más `cv(9)` |
| Propiedad compartida de un objeto sin un liberador único | `refcount(9)` |
| Un recurso contado (pool de ranuras) | `sema(9)` |
| Contador o indicador de una palabra | `atomic(9)` |

## En resumen: cómo reconocer estos patrones en código real

Reconocer un patrón en el driver de otra persona es una habilidad distinta a escribir el patrón uno mismo. La parte fácil es detectar la API: `TAILQ_FOREACH`, `buf_ring_enqueue`, `goto fail_mem`. La parte difícil es detectar el patrón cuando el autor no ha usado ninguna API, porque el patrón es puro flujo de control.

Tres hábitos ayudan.

El primero es preguntarse qué invariante intenta preservar el autor. Un ring buffer preserva «la interrupción y el thread nunca escriben el mismo índice». Una máquina de estados preserva «siempre estamos en exactamente un estado». Una escalera de limpieza preserva «todo recurso se libera exactamente una vez, en orden inverso». Cuando lees código nuevo, nombrar primero el invariante le da al resto del código un punto de anclaje.

El segundo es leer el comentario al principio del archivo. La mayoría de los drivers de FreeBSD incluyen un comentario en bloque breve que explica la disciplina de locking, la máquina de estados o las convenciones del ring que utilizan. Ese comentario existe precisamente porque un lector no puede deducirlo solo a partir del código. Léelo antes de leer el código, y léelo de nuevo cuando el código te sorprenda.

El tercero es dejar que los patrones de este apéndice colonicen tu vocabulario. Cuando te encuentres escribiendo «el ring buffer», sé capaz de decir si es SPSC o MPMC, si los índices son de ranura sacrificada o libres, dónde corresponde la barrera. Cuando te encuentres escribiendo «la máquina de estados», sé capaz de nombrar los estados, los eventos y los estados transitorios. Cuando te encuentres escribiendo «el camino de limpieza», sé capaz de enumerar los recursos en orden inverso de adquisición. El apéndice se vuelve útil cuando estas palabras acuden a la mente de forma automática.

Desde aquí puedes avanzar en varias direcciones. El Apéndice A contiene las entradas de API detalladas para cada primitiva mencionada anteriormente. El Apéndice C fundamenta los patrones del lado del hardware (rings de DMA, arrays de descriptores y la disciplina de coherencia) que se sitúan justo por debajo de estos patrones software. El Apéndice E cubre los subsistemas del kernel en cuyo interior nacieron muchos de estos patrones. Y cada capítulo del libro tiene laboratorios donde estos patrones aparecen en un driver funcional, para que la próxima vez que te encuentres uno en código real lo reconozcas sin detenerte. Eso es todo lo que es el reconocimiento de patrones en realidad: la confianza para seguir leyendo.
