---
title: "La anatomía de un driver de FreeBSD"
description: "La estructura interna, el ciclo de vida y los componentes esenciales que definen todo driver de dispositivo FreeBSD."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 6
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 1080
language: "es-ES"
---
# La anatomía de un driver de FreeBSD

## Introducción

El capítulo 5 te dejó con fluidez en el dialecto del kernel de C: conoces la forma segura de asignar memoria, proteger con locks, copiar datos y liberar recursos dentro del kernel, y has visto cómo un error de una sola línea puede costarte un kernel panic. Este capítulo toma esa fluidez y la enfoca hacia un tema concreto: **la forma de un driver de FreeBSD**. Piénsalo como pasar de aprender técnicas de carpintería a entender planos arquitectónicos: antes de construir una casa, necesitas saber dónde van los cimientos, cómo se conecta la estructura, por dónde pasan las instalaciones y cómo encajan todas las piezas.

**Importante**: Este capítulo se centra en comprender la estructura y los patrones de los drivers. Todavía no escribirás un driver completo y totalmente funcional en este capítulo; eso empieza en el capítulo 7. Aquí construimos primero tu modelo mental y tus habilidades de reconocimiento de patrones.

Escribir un driver de dispositivo puede parecer misterioso al principio. Sabes que habla con hardware, sabes que vive en el kernel, pero **¿cómo funciona todo?** ¿Cómo descubre el kernel tu driver? ¿Cómo decide cuándo llamar a tu código? ¿Qué ocurre cuando un programa de usuario abre `/dev/yourdevice`? Y lo más importante: ¿cómo es realmente el **plano** de un driver real y funcional?

Este capítulo responde a esas preguntas mostrándote la **anatomía de los drivers de FreeBSD**: las estructuras, patrones y ciclos de vida comunes que todos los drivers comparten. Aprenderás:

- Cómo los drivers **se conectan** a FreeBSD a través de newbus, devfs y el empaquetado de módulos
- Los patrones comunes que siguen los drivers de caracteres, de red y de almacenamiento
- El ciclo de vida desde el descubrimiento, pasando por probe, attach, operación y detach
- Cómo reconocer la estructura de un driver en el código fuente real de FreeBSD
- Cómo orientarte cuando lees o escribes drivers

Al final de este capítulo, no solo entenderás los drivers de forma conceptual, sino que podrás **leer código real de drivers de FreeBSD** e identificar los patrones de inmediato. Sabrás dónde buscar la vinculación del dispositivo, cómo ocurre la inicialización y cómo funciona la limpieza. Este capítulo es tu **plano** para entender cualquier driver que encuentres en el árbol de código fuente de FreeBSD.

### Qué ES este capítulo

Este capítulo es tu **visita guiada a la arquitectura** de la estructura de los drivers. Te enseña:

- **Reconocimiento de patrones**: Las formas e idiomas que todos los drivers siguen
- **Habilidades de navegación**: Dónde encontrar qué en el código fuente de un driver
- **Vocabulario**: Los nombres y conceptos (newbus, devfs, softc, cdevsw, ifnet)
- **Comprensión del ciclo de vida**: Cuándo y por qué se llama a cada función del driver
- **Visión estructural**: Cómo se conectan las piezas sin entrar en la implementación profunda

Piénsalo como aprender a leer planos antes de empezar a construir.

### Qué NO es este capítulo

Este capítulo **difiere deliberadamente la mecánica profunda** para poder centrarse en la estructura sin abrumar a los principiantes. **No** cubriremos en detalle:

- **Especificidades de bus (PCI/USB/ACPI/FDT):** Mencionaremos los buses de forma conceptual, pero omitiremos los detalles de descubrimiento y vinculación específicos del hardware y del bus.
- **Gestión de interrupciones:** Verás dónde encajan los manejadores en el ciclo de vida de un driver, no cómo programarlos o ajustarlos.
- **Programación DMA:** Reconoceremos el DMA y por qué existe, no cómo configurar mapas, etiquetas o sincronización.
- **I/O de registros de hardware:** Presentaremos `bus_space_*` a alto nivel, no los patrones completos de acceso MMIO/PIO.
- **Rutas de paquetes de red:** Señalaremos cómo `ifnet` expone una interfaz, no implementaremos pipelines de TX/RX de paquetes.
- **Internos de GEOM:** Introduciremos las superficies de almacenamiento, no la fontanería de provider/consumer ni las transformaciones de grafo.

Si sientes curiosidad por estos temas mientras lees, **estupendo**: apúntalos y sigue adelante. Este capítulo te da el **mapa**; los territorios en detalle vienen más adelante en el libro.

### Dónde encaja este capítulo

Estás entrando en el último capítulo de la **Parte 1: Fundamentos**. Cuando termines este capítulo, tendrás un **plano** claro de cómo se estructura un driver de FreeBSD y cómo se integra en el sistema, completando así los cimientos que has ido construyendo:

- **Capítulos 1 a 5 (hasta ahora):** por qué importan los drivers, un laboratorio seguro, conceptos básicos de UNIX/FreeBSD, C para espacio de usuario y C en el contexto del kernel.
- **Capítulo 6 (este capítulo):** la anatomía del driver: estructura, ciclo de vida y la superficie visible por el usuario, para que puedas reconocer las piezas antes de empezar a programar.

Con esos cimientos en su lugar, la **Parte 2: Construyendo tu primer driver** pasará de los conceptos al código, paso a paso:

- **Capítulo 7: Escribiendo tu primer driver** - escribe el esqueleto y carga un driver mínimo.
- **Capítulo 8: Trabajando con archivos de dispositivo** - crea un nodo en `/dev` y conecta los puntos de entrada básicos.
- **Capítulo 9: Leyendo y escribiendo en dispositivos** - implementa rutas de datos simples para `read(2)`/`write(2)`.
- **Capítulo 10: Gestionando la entrada y salida de forma eficiente** - introduce patrones de I/O ordenados y responsivos.

Piensa en el capítulo 6 como el **puente**: ya tienes el lenguaje (C) y el entorno (FreeBSD), y con esta anatomía en mente, estás listo para empezar a **construir** en la Parte 2.

Si estás hojeando: **Capítulo 6 = el plano. Parte 2 = la construcción.**

## Guía del lector: cómo usar este capítulo

Este capítulo está diseñado tanto como **referencia estructural** como **experiencia de lectura guiada**. A diferencia del enfoque práctico de programación del capítulo 7, este capítulo hace hincapié en la **comprensión, el reconocimiento de patrones y la navegación**. Dedicarás tiempo a examinar código real de drivers de FreeBSD, identificar estructuras y construir un modelo mental de cómo se conecta todo.

### Estimación del tiempo necesario

El tiempo total depende de con qué profundidad te involucres. Usa el itinerario que se adapte a tu ritmo.

**Itinerario A: solo lectura**
Calcula **8-10 horas** para asimilar los conceptos, ojear los diagramas y leer los fragmentos de código a un ritmo cómodo para principiantes. Esto te da un sólido modelo mental sin pasos prácticos.

**Itinerario B: lectura + seguimiento en `/usr/src`**
Calcula **12-14 horas** si abres los archivos referenciados bajo `/usr/src/sys` mientras lees, exploras el contexto circundante y escribes los micro-fragmentos en un archivo de prueba. Esto refuerza el reconocimiento de patrones y las habilidades de navegación.

**Itinerario C: lectura + seguimiento + los cuatro laboratorios**
Añade **2,5-3,5 horas** para completar los **cuatro** laboratorios seguros para principiantes de este capítulo:
Laboratorio 1 (Búsqueda del tesoro), Laboratorio 2 (Hola módulo), Laboratorio 3 (Nodo de dispositivo), Laboratorio 4 (Gestión de errores).
Son puntos de comprobación cortos y enfocados que validan lo que aprendiste en los recorridos y explicaciones del capítulo.

**Opcional: ejercicios de desafío**
Añade **2-4 horas** para abordar los desafíos al final del capítulo. Profundizan tu comprensión de los puntos de entrada, el desenrollado de errores, las dependencias y la clasificación mediante la lectura de drivers reales.

**Ritmo sugerido**
Divide este capítulo en dos o tres sesiones. Una división práctica es:
Sesión 1: Lee la sección sobre el modelo de driver, el esqueleto y el ciclo de vida mientras sigues el hilo en `/usr/src`.
Sesión 2: Completa los laboratorios 1 y 2.
Sesión 3: Completa los laboratorios 3 y 4 y, si lo deseas, los desafíos.

**Recordatorio**
No te apresures. El objetivo aquí es la **alfabetización en drivers**: la capacidad de abrir cualquier driver, localizar sus rutas probe/attach/detach, reconocer las formas de cdev/ifnet/GEOM y entender cómo se conecta a newbus y devfs. Dominar esto hace que el build del capítulo 7 sea mucho más rápido y con menos sorpresas.

### Qué tener preparado

Para sacar el máximo partido a este capítulo, prepara tu entorno de trabajo:

1. **Tu entorno de laboratorio FreeBSD** del capítulo 2 (VM o máquina física)
2. **FreeBSD 14.3 con `/usr/src` instalado** (haremos referencia a archivos reales del árbol de código fuente del kernel)
3. **Un terminal** donde puedas ejecutar comandos y examinar archivos
4. **Tu cuaderno de laboratorio** para notas y observaciones
5. **Acceso a las páginas de manual**: consultarás `man 9 <función>` con frecuencia

**Nota:** Todos los ejemplos se probaron en FreeBSD 14.3; ajusta los comandos si usas una versión diferente.

### Ritmo y enfoque

Este capítulo funciona mejor cuando:

- **Lees en orden**: Cada sección se apoya en la anterior. El orden importa.
- **Mantienes `/usr/src` abierto**: Cuando hagamos referencia a un archivo como `/usr/src/sys/dev/null/null.c`, ábrelo realmente y mira el contexto circundante.
- **Usas `man 9` mientras avanzas**: Cuando veas una función como `device_get_softc()`, ejecuta `man 9 device_get_softc` para ver la documentación oficial.
- **Escribes los micro-fragmentos tú mismo**: Incluso en este capítulo de «solo lectura», escribir patrones clave (como una función probe o una tabla de métodos) consolida las formas en tu memoria.
- **No te saltas los laboratorios**: Están diseñados como puntos de comprobación. Completa cada uno antes de pasar a la siguiente sección.

### Gestiona tu curiosidad

Mientras lees, encontrarás conceptos que te generan preguntas más profundas:

- «¿Cómo funcionan exactamente las interrupciones PCI?»
- «¿Cuáles son todos los flags de `bus_alloc_resource_any()`?»
- «¿Cómo llama la pila de red a mi función de transmisión?»

**Esto es esperado y saludable**. Pero resiste la tentación de adentrarte en cada madriguera ahora. Este capítulo trata de reconocer patrones y entender la estructura. La mecánica profunda tiene sus propios capítulos dedicados.

**Estrategia**: Lleva una *«Lista de curiosidades»* en tu cuaderno de laboratorio. Cuando algo despierte tu interés, anótalo con una nota sobre en qué parte del libro se tratará. Por ejemplo:

```html
Curiosity List:
- Interrupt handler details  ->  Chapter 19: Handling Interrupts and 
                             ->  Chapter 20: Advanced Interrupt Handling
- DMA buffer setup  ->  Chapter 21: DMA and High-Speed Data Transfer
- Network packet queues  ->  Chapter 28: Writing a Network Driver
- PCI configuration space  ->  Chapter 18: Writing a PCI Driver
```

Esto te permite reconocer tus preguntas sin desviarte de tu enfoque actual.

### Criterios de éxito

Cuando cierres este capítulo deberías ser capaz de:

- Abrir cualquier driver de FreeBSD y localizar de inmediato sus funciones probe, attach y detach.
- Identificar si un driver es de caracteres, de red, de almacenamiento u orientado a bus.
- Reconocer una tabla de métodos de dispositivo y entender qué mapea.
- Encontrar la estructura softc y entender su función.
- Trazar el ciclo de vida básico desde la carga del módulo hasta la operación del dispositivo.
- Leer los registros del sistema y asociarlos a eventos del ciclo de vida del driver.
- Localizar las páginas de manual relevantes para las funciones clave.

Si puedes hacer todo esto, estás listo para la programación práctica del capítulo 7.

## Cómo sacar el máximo partido a este capítulo

Ahora que sabes qué esperar y cómo organizarte, hablemos de **tácticas de aprendizaje** concretas que harán que la estructura de los drivers te resulte clara. Estas estrategias han demostrado ser eficaces para los principiantes que se enfrentan al modelo de drivers de FreeBSD.

### Mantén `/usr/src` cerca

Todos los ejemplos de código de este capítulo provienen de archivos reales del código fuente de FreeBSD 14.3. **No te limites a leer los fragmentos de este libro**: abre los archivos reales y míralos en su contexto.

**Por qué importa**:

Ver el archivo completo te muestra:

- Cómo se organizan los includes al principio
- Cómo se relacionan entre sí múltiples funciones
- Comentarios y documentación que dejaron los desarrolladores originales
- Patrones e idiomas del mundo real

#### Localizador rápido: ¿dónde está en el árbol de código fuente?

| Forma que estás estudiando | Lugar habitual en `/usr/src/sys` | Un archivo concreto para abrir primero |
|---|---|---|
| Dispositivo de caracteres mínimo (`cdevsw`) | `dev/null/` | `dev/null/null.c` |
| Dispositivo de infraestructura simple (LED) | `dev/led/` | `dev/led/led.c` |
| Interfaz de red pseudovirtual (tun/tap) | `net/` | `net/if_tuntap.c` |
| Ejemplo de «pegamento» UART PCI | `dev/uart/` | `dev/uart/uart_bus_pci.c` |
| Fontanería de bus (para consulta) | `dev/pci/`, `kern/`, `bus/` | explora `dev/pci/pcib*.*` y relacionados |

*Consejo: abre uno de estos en paralelo con las explicaciones para reforzar el reconocimiento de patrones.*

**Consejo práctico**: Mantén abierto un segundo terminal o ventana del editor. Cuando el texto diga:

> «Aquí tienes un ejemplo de `null_cdevsw` en `/usr/src/sys/dev/null/null.c`:»

Navega realmente hasta allí:
```bash
% cd /usr/src/sys/dev/null
% less null.c
```

Usa `/` en `less` para buscar patrones como `probe` o `cdevsw`, y salta directamente a las secciones relevantes.

> **Una nota sobre los números de línea.** Cada vez que este capítulo mencione algún número de línea, trátalo como válido para el árbol de FreeBSD 14.3 en el momento de la redacción, y nada más. Los nombres de funciones, estructuras y tablas son la referencia duradera. Cuando un ejercicio o una pista del capítulo habría citado números de línea, citamos en su lugar la función que los contiene, la estructura `cdevsw` o el array con nombre; abre el archivo y salta a ese símbolo.

### Escribe los micro-fragmentos tú mismo

Aunque el Capítulo 7 es donde escribirás drivers completos, **escribir patrones cortos ahora mismo** desarrolla fluidez.

Cuando veas un ejemplo de función probe, no te limites a leerlo, **escríbelo en un archivo de prueba**:

```c
static int
mydriver_probe(device_t dev)
{
    device_set_desc(dev, "My Example Driver");
    return (BUS_PROBE_DEFAULT);
}
```

**Por qué funciona esto**: Escribir activa la memoria muscular. Tus dedos aprenden las formas (`device_t`, `BUS_PROBE_DEFAULT`) más rápido que solo tus ojos. Cuando llegues al Capítulo 7, estos patrones te resultarán naturales.

**Consejo práctico**:

Crea un directorio de pruebas:

```bash
% mkdir -p ~/scratch/chapter06
% cd ~/scratch/chapter06
% vi patterns.c
```

Usa este espacio para reunir los patrones que estás aprendiendo.

### Trata los laboratorios como puntos de control

Este capítulo incluye cuatro laboratorios prácticos (consulta la sección «Laboratorios prácticos»):

1. **Laboratorio 1**: Búsqueda de solo lectura a través de drivers reales
2. **Laboratorio 2**: Construye y carga un módulo mínimo que solo registra mensajes
3. **Laboratorio 3**: Crea y elimina un nodo de dispositivo en `/dev`
4. **Laboratorio 4**: Manejo de errores y programación defensiva

**No los omitas**. Son tu comprobación de que los conceptos han pasado de «lo leí» a «soy capaz de hacerlo».

**Momento adecuado**: Completa cada laboratorio cuando llegues a la sección «Laboratorios prácticos», no antes. Los laboratorios dan por supuesto que has leído las secciones anteriores sobre estructura y patrones de drivers. Están diseñados para sintetizar todo en práctica directa.

**Mentalidad de éxito:** Los laboratorios están pensados para ser alcanzables. Si te quedas atascado, revisa la sección correspondiente, consulta las páginas `man 9` citadas en el texto y utiliza la **tabla de referencia rápida: elementos básicos de un driver de un vistazo** al final del capítulo. Cada laboratorio debería llevarte entre 20 y 45 minutos.

### Deja los mecanismos profundos para más adelante

Este capítulo repite frases del tipo:

- «Las interrupciones se tratan en los Capítulos 19 y 20»
- «Los detalles de DMA en el Capítulo 21»
- «El procesamiento de paquetes de red en el Capítulo 28»

**Confía en esta estructura**. Intentar aprenderlo todo a la vez lleva a la confusión y al agotamiento.

**Analogía**: Cuando aprendes a conducir, primero entiendes los controles del coche (volante, pedales, cambio de marchas) antes de estudiar la mecánica del motor. Del mismo modo, aprende la *estructura* del driver ahora, y estudia los *mecanismos* más adelante, cuando tengas contexto.

**Estrategia**: Cuando encuentres un momento de «esto lo dejo para después», acéptalo y sigue adelante. Los temas profundos están por llegar y tendrán mucho más sentido cuando hayas escrito un driver básico.

### Usa `man 9` como referencia

Las páginas del manual de la sección 9 de FreeBSD documentan las interfaces del kernel. Son de un valor incalculable, aunque pueden resultar densas.

**Cuándo usarlas**:

- Ves el nombre de una función que no reconoces
- Quieres conocer todos los parámetros y valores de retorno
- Necesitas confirmar el comportamiento

**Ejemplo**:
```bash
% man 9 device_get_softc
% man 9 bus_alloc_resource
% man 9 make_dev
```

**Consejo profesional**: Usa `apropos` para buscar funciones relacionadas:
```bash
% apropos device | grep "^device"
```

Esto te muestra todas las funciones relacionadas con dispositivos de una vez.

**Referencia complementaria**: Para un resumen curado e interno del libro de las mismas APIs que encontrarás a lo largo de este capítulo (`malloc(9)`, `mtx(9)`, `callout(9)`, `bus_alloc_resource_*`, `bus_space(9)`, macros de Newbus y más), el Apéndice A las agrupa en hojas de referencia rápida temáticas. No es un sustituto de `man 9`; es la consulta breve a la que acudes mientras lees para no perder el hilo del capítulo.

### Ojea el código antes de leer las explicaciones

Cuando una sección hace referencia a un archivo fuente, prueba este método:

1. **Ojea el archivo primero** (30 segundos)
2. **Fíjate en los patrones** (¿dónde están probe/attach? ¿qué includes hay?)
3. **Después lee la explicación** de este capítulo
4. **Vuelve al código** con la nueva comprensión adquirida

**Por qué funciona**: Tu cerebro crea primero un mapa mental aproximado y después la explicación rellena los detalles. Esto es más eficaz que leer explicación -> código, que trata el código como algo secundario.

### Visualiza mientras lees

La estructura de un driver tiene muchas piezas en movimiento: buses, dispositivos, métodos, ciclos de vida. **Dibuja diagramas** a medida que encuentres nuevos conceptos.

**Ejemplos de diagramas útiles**:

- Árbol de dispositivos que muestra las relaciones padre-hijo
- Diagrama de flujo del ciclo de vida (probe -> attach -> operate -> detach)
- Flujo del dispositivo de caracteres (open -> read/write -> close)
- Relación entre `device_t`, softc y `cdev`

**Herramientas**: El papel y el lápiz funcionan muy bien. También puedes usar texto simple como arte:

```bash
root
 |- nexus0
     |- acpi0
         |- pci0
             |- em0 (network)
             |- ahci0 (storage)
                 |- ada0 (disk)
```

### Estudia patrones en varios drivers

La sección «Visita de solo lectura a drivers reales pequeños» recorre cuatro drivers reales (null, led, tun y PCI mínimo). No te limites a leer cada uno de forma aislada, **compáralos**:

- ¿Cómo estructura `null.c` su cdevsw frente a `led.c`?
- ¿Dónde inicializa cada driver su softc?
- ¿Qué tienen en común sus funciones probe? ¿En qué se diferencian?

El **reconocimiento de patrones** es el objetivo. Una vez que veas la misma forma repetida, la reconocerás en todas partes.

### Establece expectativas realistas

**Planifica unas 18-22 horas si completas todas las actividades** de este capítulo (lectura, recorridos, laboratorios y repasos). Si también abordas los desafíos opcionales, calcula hasta 4 horas adicionales. A dos horas por día, espera aproximadamente una semana o algo más, **eso es normal y está previsto.**

Esto no es una carrera. El objetivo es el **dominio de la estructura**, que es la base de todos los capítulos siguientes.

**Mentalidad**: Piensa en este capítulo como un **programa de entrenamiento**, no como un sprint. Los deportistas no intentan ganar toda su fuerza en una sola sesión. Del mismo modo, estás desarrollando **fluidez con los drivers** de forma gradual.

### Cuándo hacer pausas

Sabrás que necesitas un descanso cuando:

- Hayas leído el mismo párrafo tres veces sin asimilarlo
- Los nombres de las funciones empiecen a mezclarse
- Te sientas abrumado por los detalles

**Solución**: Aléjate. Da un paseo, haz otra cosa y vuelve con energía renovada. Este material seguirá aquí y tu cerebro procesa la información compleja mejor con descanso.

### Estás construyendo una base

Recuerda: **este capítulo es tu plano**. El Capítulo 7 es donde construirás código real. Invertir tiempo aquí reporta enormes beneficios más adelante porque no tendrás que adivinar la estructura; la conocerás.

Empecemos con la visión global.

## La visión global: cómo ve FreeBSD los dispositivos y los drivers

Antes de examinar ningún código, necesitamos establecer un **modelo mental** de cómo FreeBSD organiza conceptualmente los dispositivos y los drivers. Entender este modelo es como aprender cómo funciona la fontanería de un edificio antes de sustituir una tubería: necesitas saber de dónde viene el agua y adónde va.

Esta sección proporciona la **visión de una página** que llevarás contigo a lo largo del resto del capítulo. Definiremos los términos clave, mostraremos cómo se conectan las piezas y te daremos el vocabulario justo para navegar por el resto del material sin ahogarte en los detalles.

### Ciclo de vida de un driver en una pantalla

```html
Boot/Hot-plug
|
v
[ Device enumerated by bus ]
| (PCI/USB/ACPI/FDT discovers hardware and creates device_t)
v
[ probe(dev) ]
| Decide: "Am I the right driver?" (score and return priority)
| If not mine  ->  return ENXIO / lower score
v
[ attach(dev) ]
| Allocate softc/state
| Claim resources (memory BARs/IRQ/etc.)
| Create user surface (e.g., make_dev / ifnet)
| Register callbacks, start timers
v
[ operate ]
| Runtime: open/read/write/ioctl, TX/RX, interrupts, callouts
| Normal errors handled; resources reused
v
[ detach(dev) ]
| Quiesce I/O and timers
| Destroy user surface (destroy_dev / if_detach / etc.)
| Free resources and state
v
Goodbye
```

*Ten presente este flujo mientras lees los recorridos; todos los drivers que verás encajan en este esquema.*

### Dispositivos, drivers y devclasses

FreeBSD utiliza una terminología precisa para los componentes de su modelo de dispositivos. Vamos a definirlos en lenguaje claro:

**Dispositivo**

Un **dispositivo** es la representación del kernel de un recurso hardware o una entidad lógica. Es una estructura `device_t` que el kernel crea y gestiona.

Piensa en él como una **etiqueta de nombre** para algo que el kernel necesita rastrear: una tarjeta de red, un controlador de disco, un teclado USB o incluso un pseudodispositivo como `/dev/null`.

**Idea clave**: Un dispositivo existe tanto si tiene un driver asociado como si no. Durante el boot, los buses enumeran el hardware y crean estructuras `device_t` para todo lo que encuentran. Estos dispositivos esperan a que los drivers los reclamen.

**Driver**

Un **driver** es **código** que sabe cómo controlar un tipo específico de dispositivo. Es la implementación: las funciones probe, attach y operacionales que hacen que el hardware sea útil.

Un único driver puede gestionar varios modelos de dispositivo. Por ejemplo, el driver `em` gestiona docenas de tarjetas Ethernet Intel diferentes comprobando los IDs de dispositivo y adaptando el comportamiento.

**Devclass**

Una **devclass** (clase de dispositivo) es una **agrupación** de dispositivos relacionados. Es la manera que tiene FreeBSD de llevar la cuenta de, por ejemplo, «todos los dispositivos UART» o «todos los controladores de disco».

Cuando ejecutas `sysctl dev.em`, estás consultando la devclass `em`, que muestra todas las instancias (em0, em1, etc.) gestionadas por ese driver.

**Ejemplo**:
```bash
devclass: uart
devices in this class: uart0, uart1, uart2
each device has a driver attached (or not)
```

**Resumen de relaciones**:

- **Devclass** = categoría (por ejemplo, «interfaces de red»)
- **Dispositivo** = instancia (por ejemplo, «em0»)
- **Driver** = código (por ejemplo, las funciones del driver em)

**Por qué importa esto**: Cuando escribas un driver, lo registrarás con una devclass y cada dispositivo al que se vincule pasará a formar parte de esa clase.

### La jerarquía de buses y Newbus (una página)

FreeBSD organiza los dispositivos en una **estructura de árbol** denominada **árbol de dispositivos**, con los buses como nodos internos y los dispositivos como hojas. Esto lo gestiona un framework llamado **Newbus**.

**¿Qué es un bus?**

Un **bus** es cualquier dispositivo que puede tener hijos. Ejemplos:

- **Bus PCI**: Contiene tarjetas PCI (controladores de red, gráficos y almacenamiento)
- **Hub USB**: Contiene periféricos USB
- **Bus ACPI**: Contiene dispositivos de plataforma enumerados por las tablas ACPI

**La estructura del árbol de dispositivos**:
```bash
root
 |- nexus0 (platform-specific root bus)
     |- acpi0 (ACPI bus)
         |- cpu0
         |- cpu1
         |- pci0 (PCI bus)
             |- em0 (network card)
             |- ahci0 (SATA controller)
             |   |- ada0 (disk)
             |   |- ada1 (disk)
             |- ehci0 (USB controller)
                 |- usbus0 (USB bus)
                     |- ukbd0 (USB keyboard)
```

**¿Qué es Newbus?**

**Newbus** es el framework de dispositivos orientado a objetos de FreeBSD. Proporciona:

- **Descubrimiento de dispositivos**: Los buses enumeran sus hijos
- **Correspondencia de drivers**: Las funciones probe determinan qué driver encaja con cada dispositivo
- **Gestión de recursos**: Los buses asignan IRQs, rangos de memoria y otros recursos a los dispositivos
- **Gestión del ciclo de vida**: Coordina probe, attach y detach

**El flujo probe-attach**:

1. Un bus (por ejemplo, PCI) **enumera** sus dispositivos explorando el hardware
2. Para cada dispositivo, el kernel crea un `device_t`
3. El kernel llama a la función **probe** de cada driver compatible: «¿Puedes gestionar esto?»
4. El driver que mejor encaje gana
5. El kernel llama a la función **attach** de ese driver para inicializarlo

**¿Por qué «Newbus»?**

Sustituyó a un framework de dispositivos más antiguo y menos flexible. El «new» (nuevo) es histórico; lleva décadas siendo el estándar.

**Tu papel como autor de drivers**:

- Escribes las funciones probe, attach y detach
- Newbus las invoca en el momento adecuado
- No buscas los dispositivos manualmente; Newbus te los trae

**Véalo en acción**:
```bash
% devinfo -rv
```

Esto muestra el árbol de dispositivos completo con las asignaciones de recursos.

### Del kernel a /dev: lo que presenta devfs

Muchos dispositivos (especialmente los dispositivos de caracteres) aparecen como **archivos en `/dev`**. ¿Cómo funciona eso?

**devfs (sistema de archivos de dispositivos)**

`devfs` es un sistema de archivos especial que presenta dinámicamente los nodos de dispositivo como archivos. Es **gestionado por el kernel**: cuando un driver crea un nodo de dispositivo, aparece al instante en `/dev`. Cuando el driver se descarga, el nodo desaparece.

**¿Por qué archivos?**

La filosofía UNIX de «todo es un archivo» implica un acceso uniforme:

```bash
% ls -l /dev/null
crw-rw-rw-  1 root  wheel  0x14 Oct 14 12:34 /dev/null
```

Esa `c` significa **dispositivo de caracteres**. El número mayor (parte de `0x14`) identifica el driver; el número menor identifica la instancia concreta.

**Nota:** Históricamente, el número de dispositivo se dividía en «mayor» (driver) y «menor» (instancia). Con devfs y los dispositivos dinámicos en el FreeBSD moderno, no te apoyas en valores mayor/menor fijos; trata ese número como un identificador interno y utiliza en su lugar las APIs de cdev y devfs.

**Vista desde el espacio de usuario**:

Cuando un programa abre `/dev/null`, el kernel:

1. Busca el dispositivo por su número mayor/menor
2. Encuentra el `cdev` (estructura de dispositivo de caracteres) asociado
3. Llama a la función **d_open** del driver
4. Devuelve un descriptor de archivo al programa

**Para lecturas/escrituras**:

- El programa de usuario llama a `read(fd, buf, len)`
- El kernel traduce esto a la función **d_read** del driver
- El driver lo gestiona y devuelve datos o un error
- El kernel devuelve el resultado al programa de usuario

**No todos los dispositivos aparecen en `/dev`**:

- **Las interfaces de red** (em0, wlan0) aparecen en `ifconfig`, no en `/dev`
- **Las capas de almacenamiento** suelen usar `/dev/ada0`, pero GEOM añade complejidad
- **Los pseudodispositivos** pueden crear nodos o no

**Conclusión clave**: Los drivers de caracteres crean habitualmente entradas en `/dev` mediante `make_dev()`, y `devfs` las hace visibles. Cubriremos esto en detalle en la sección «Creación y eliminación de nodos de dispositivo».

### Tu mapa de páginas de manual (léelo, no lo memorices)

Las páginas de manual de sección 9 de FreeBSD documentan las API del kernel. Aquí tienes un **mapa inicial** de las páginas más importantes para el desarrollo de drivers. No necesitas memorizarlas, solo saber que existen para poder consultarlas más adelante.

**API básicas de dispositivos y drivers**:

- `device(9)` - Visión general de la abstracción device_t
- `devclass(9)` - Gestión de clases de dispositivos
- `DRIVER_MODULE(9)` - Registro de tu driver en el kernel
- `DEVICE_PROBE(9)` - Cómo funcionan los métodos probe
- `DEVICE_ATTACH(9)` - Cómo funcionan los métodos attach
- `DEVICE_DETACH(9)` - Cómo funcionan los métodos detach

**Dispositivos de caracteres**:

- `make_dev(9)` - Creación de nodos de dispositivo en /dev
- `destroy_dev(9)` - Eliminación de nodos de dispositivo
- `cdev(9)` - Estructura y operaciones del dispositivo de caracteres

**Interfaces de red**:

- `ifnet(9)` - Estructura y registro de la interfaz de red
- `if_attach(9)` - Vinculación de una interfaz de red
- `mbuf(9)` - Gestión de buffers de red

**Almacenamiento**:

- `GEOM(4)` - Visión general de la capa de almacenamiento de FreeBSD (nota: sección 4, no 9)
- `g_bio(9)` - Estructura bio (E/S de bloque)

**Recursos y acceso al hardware**:

- `bus_alloc_resource(9)` - Reclamación de IRQ, memoria, etc.
- `bus_space(9)` - Acceso portable a MMIO y PIO
- `bus_dma(9)` - Gestión de memoria DMA

**Módulo y ciclo de vida**:

- `module(9)` - Infraestructura de módulos del kernel
- `MODULE_DEPEND(9)` - Declaración de dependencias de módulos
- `MODULE_VERSION(9)` - Control de versiones de tu módulo

**Sincronización y locks**:

- `mutex(9)` - Locks de exclusión mutua
- `sx(9)` - Locks compartidos/exclusivos
- `rmlock(9)` - Locks de lectura mayoritaria

**Funciones de utilidad**:

- `printf(9)` - Variantes de printf del kernel (incluyendo device_printf)
- `malloc(9)` - Asignación de memoria en el kernel
- `sysctl(9)` - Creación de nodos sysctl para observabilidad

**Cómo usar este mapa**:

Cuando encuentres una función o un concepto desconocido, comprueba si tiene una página de manual:
```bash
% man 9 <function_or_topic>
```

Ejemplos:
```bash
% man 9 device_get_softc
% man 9 bus_alloc_resource
% man 9 make_dev
```

Si no estás seguro del nombre exacto, usa `apropos`:
```bash
% apropos -s 9 device
```

**Consejo profesional**: Muchas páginas de manual incluyen secciones **SEE ALSO** al final, que apuntan a temas relacionados. Sigue esas pistas cuando explores.

**Esta es tu biblioteca de referencia**. No la lees de principio a fin, la consultas cuando la necesitas. A medida que trabajes en este capítulo y en los siguientes, irás familiarizándote de forma natural con las páginas más habituales.

**Resumen**

Ahora tienes la **visión general**:

- Los **dispositivos** son objetos del kernel, los **drivers** son código, los **devclasses** son agrupaciones
- **Newbus** gestiona el árbol de dispositivos y el ciclo de vida de los drivers (probe/attach/detach)
- **devfs** presenta los dispositivos como archivos en `/dev` (para dispositivos de caracteres)
- Las **páginas de manual** de la sección 9 son tu biblioteca de referencia

Este modelo mental es tu base. En la siguiente sección exploraremos las distintas **familias de drivers** y cómo elegir la forma adecuada para tu hardware.

## Familias de drivers: elegir la forma adecuada

No todos los drivers son iguales. Dependiendo de lo que haga tu hardware, tendrás que presentar la «cara» correcta al kernel de FreeBSD. Piensa en las familias de drivers como en especializaciones profesionales: un cardiólogo y un traumatólogo son ambos médicos, pero trabajan de formas muy distintas. Del mismo modo, un driver de dispositivo de caracteres y un driver de red interactúan con el hardware, pero se conectan a partes diferentes del kernel.

Esta sección te ayuda a **identificar a qué familia pertenece tu driver** y a comprender las diferencias estructurales entre ellas. Lo mantendremos al nivel del reconocimiento; los capítulos posteriores cubrirán la implementación.

### Dispositivos de caracteres

Los **dispositivos de caracteres** son la familia de drivers más sencilla y más común. Presentan una **interfaz orientada a flujos** a los programas de usuario: open, close, read, write e ioctl.

**Cuándo utilizarlos**:

- Hardware que envía o recibe datos byte a byte o en fragmentos arbitrarios
- Superficies de control para configuración (LEDs, pines GPIO)
- Sensores, puertos serie, tarjetas de sonido, hardware personalizado
- Pseudodispositivos que implementan funcionalidad software

**Vista desde el espacio de usuario**:
```bash
% ls -l /dev/cuau0
crw-rw----  1 root  dialer  0x4d Oct 14 10:23 /dev/cuau0
```

Los programas interactúan con los dispositivos de caracteres como si fueran archivos:
```c
int fd = open("/dev/cuau0", O_RDWR);
write(fd, "Hello", 5);
read(fd, buffer, sizeof(buffer));
ioctl(fd, SOME_COMMAND, &arg);
close(fd);
```

**Vista desde el kernel**:

Tu driver implementa un `struct cdevsw` (character device switch) con punteros a funciones:

```c
static struct cdevsw mydev_cdevsw = {
    .d_version = D_VERSION,
    .d_open    = mydev_open,
    .d_close   = mydev_close,
    .d_read    = mydev_read,
    .d_write   = mydev_write,
    .d_ioctl   = mydev_ioctl,
    .d_name    = "mydev",
};
```

Cuando un programa de usuario llama a `read()`, el kernel lo enruta hacia tu función `mydev_read()`.

**Ejemplos en FreeBSD**:

- `/dev/null`, `/dev/zero`, `/dev/random` - Pseudodispositivos
- `/dev/led/*` - Control de LEDs
- `/dev/cuau0` - Puerto serie
- `/dev/dsp` - Dispositivo de audio

**Por qué empezar aquí**: Los dispositivos de caracteres son la **familia más sencilla** de comprender e implementar. Si estás aprendiendo desarrollo de drivers, casi con toda seguridad empezarás con un dispositivo de caracteres. El primer driver del Capítulo 7 es un dispositivo de caracteres.

### Almacenamiento a través de GEOM (por qué los «dispositivos de bloques» son distintos aquí)

La arquitectura de almacenamiento de FreeBSD se basa en **GEOM** (Geometry Management), un framework modular para transformaciones y capas de almacenamiento.

**Nota histórica**: El UNIX tradicional tenía «dispositivos de bloques» y «dispositivos de caracteres». FreeBSD moderno **unificó esto**: todos los dispositivos son dispositivos de caracteres, y GEOM se sitúa por encima para ofrecer servicios de almacenamiento a nivel de bloque.

**Modelo conceptual de GEOM**:

- **Proveedores**: Suministran almacenamiento (por ejemplo, un disco: `ada0`)
- **Consumidores**: Usan el almacenamiento (por ejemplo, un sistema de archivos)
- **Geoms**: Transformaciones intermedias (particionado, RAID, cifrado)

**Ejemplo de pila**:

```html
Filesystem (UFS)
     ->  consumes
GEOM LABEL (geom_label)
     ->  consumes
GEOM PART (partition table)
     ->  consumes
ada0 (disk driver via CAM)
     ->  talks to
AHCI driver (hardware)
```

**Cuándo utilizarlo**:

- Estás escribiendo un driver para un controlador de disco (SATA, NVMe, SCSI)
- Estás implementando una transformación de almacenamiento (RAID por software, cifrado, compresión)
- Tu dispositivo presenta almacenamiento orientado a bloques

**Vista desde el espacio de usuario**:

```bash
% ls -l /dev/ada0
crw-r-----  1 root  operator  0xa9 Oct 14 10:23 /dev/ada0
```

Observa que sigue siendo un dispositivo de caracteres (`c`), pero GEOM y la caché de buffers proporcionan la semántica de bloque.

**Vista desde el kernel**:

Los drivers de almacenamiento interactúan habitualmente con **CAM (Common Access Method)**, la capa SCSI/ATA de FreeBSD. Registras un **SIM (SCSI Interface Module)** que gestiona las peticiones de E/S.

También puedes crear una clase GEOM que procese peticiones **bio (block I/O)**.

**Ejemplos**:

- `ahci` - Driver de controlador SATA
- `nvd` - Driver de disco NVMe
- `gmirror` - Espejo GEOM (RAID 1)
- `geli` - Capa de cifrado GEOM

**Por qué esto es avanzado**

Los drivers de almacenamiento requieren comprender:

- DMA y listas de scatter-gather
- Planificación de E/S de bloque
- Los frameworks CAM o GEOM
- Integridad de datos y gestión de errores

No cubriremos esto en profundidad hasta mucho más adelante. Por ahora, basta con reconocer que los drivers de almacenamiento tienen una forma distinta a la de los dispositivos de caracteres.

### Red a través de ifnet

Los **drivers de red** no aparecen en `/dev`. En cambio, se registran como **interfaces de red** que aparecen en `ifconfig` y se integran con la pila de red de FreeBSD.

**Cuándo utilizarlos**:

- Tarjetas Ethernet
- Adaptadores inalámbricos
- Interfaces de red virtuales (túneles, puentes, VPN)
- Cualquier dispositivo que envíe o reciba paquetes de red

**Vista desde el espacio de usuario**:
```bash
% ifconfig em0
em0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
    ether 00:0c:29:3a:4f:1e
    inet 192.168.1.100 netmask 0xffffff00 broadcast 192.168.1.255
```

Los programas no abren las interfaces de red directamente. En cambio, crean sockets y el kernel enruta los paquetes a través de la interfaz adecuada.

**Vista desde el kernel**:

Tu driver asigna y registra una estructura **if_t** (interfaz):

```c
if_t ifp;

ifp = if_alloc(IFT_ETHER);
if_setsoftc(ifp, sc);
if_initname(ifp, device_get_name(dev), device_get_unit(dev));
if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
if_setinitfn(ifp, mydriver_init);
if_setioctlfn(ifp, mydriver_ioctl);
if_settransmitfn(ifp, mydriver_transmit);
if_setqflushfn(ifp, mydriver_qflush);

ether_ifattach(ifp, sc->mac_addr);
```

**Tu driver debe gestionar**:

- **Transmisión**: El kernel te entrega paquetes (mbufs) para enviar
- **Recepción**: Recibes paquetes del hardware y los pasas hacia arriba en la pila
- **Inicialización**: Configura el hardware cuando la interfaz se activa
- **ioctl**: Gestiona cambios de configuración (dirección, MTU, etc.)

**Ejemplos**:

- `em` - Ethernet Intel (familia e1000)
- `igb` - Ethernet Gigabit Intel
- `bge` - Ethernet Gigabit Broadcom
- `if_tun` - Dispositivo de túnel

**Por qué esto es diferente**

Los drivers de red deben:

- Gestionar colas de paquetes y cadenas de mbufs
- Gestionar los cambios de estado del enlace
- Soportar filtrado multicast
- Implementar funciones de offload de hardware (sumas de verificación, TSO, etc.)

El Capítulo 28 cubre el desarrollo de drivers de red en profundidad.

### Pseudodispositivos y dispositivos clone (seguros, pequeños e instructivos)

Los **pseudodispositivos** son drivers únicamente software sin hardware subyacente. Son **perfectos para aprender** porque puedes centrarte por completo en la estructura del driver sin preocuparte por el comportamiento del hardware.

**Pseudodispositivos comunes**:

1. **null** (`/dev/null`) - Descarta las escrituras, devuelve EOF en las lecturas
2. **zero** (`/dev/zero`) - Devuelve ceros infinitos
3. **random** (`/dev/random`) - Generador de números aleatorios
4. **md** - Disco en memoria (disco RAM)
5. **tun/tap** - Dispositivos de túnel de red

**Por qué son valiosos para aprender**:

- Sin complejidad de hardware (sin registros, sin DMA, sin interrupciones)
- Permiten centrarse puramente en la estructura y el ciclo de vida del driver
- Fáciles de probar (basta con leer/escribir en `/dev`)
- Código fuente pequeño y legible

**Caso especial: dispositivos clone**

Algunos pseudodispositivos admiten **múltiples aperturas simultáneas** creando nuevos nodos de dispositivo bajo demanda. Ejemplo: `/dev/bpf` (Berkeley Packet Filter).

Cuando abres `/dev/bpf`, el driver asigna una nueva instancia (`/dev/bpf0`, `/dev/bpf1`, etc.) para tu sesión.

**Ejemplo: el dispositivo tun (híbrido)**

El dispositivo `tun` es interesante porque es **a la vez**:

- Un **dispositivo de caracteres** (`/dev/tun0`) para el control
- Una **interfaz de red** (`tun0` en `ifconfig`) para los datos

Los programas abren `/dev/tun0` para configurar el túnel, pero los paquetes fluyen a través de la interfaz de red. Este «modelo mixto» demuestra cómo los drivers pueden presentar múltiples superficies.

**Dónde encontrarlos en el código fuente**:

```bash
% ls /usr/src/sys/dev/null/
% ls /usr/src/sys/dev/md/
% ls /usr/src/sys/net/if_tuntap.c
```

La sección «Visita de solo lectura a drivers reales y pequeños» recorrerá estos drivers en detalle. Por ahora, basta con reconocer que los pseudodispositivos son tus **ruedas de entrenamiento**: lo suficientemente sencillos para entenderlos, lo suficientemente reales para ser útiles.

### Lista de comprobación para decidir: ¿qué forma encaja?

Usa esta lista de comprobación para identificar la familia de drivers adecuada para tu hardware:

**Elige dispositivo de caracteres si**:

- El hardware envía o recibe flujos de datos arbitrarios (no paquetes, no bloques)
- Los programas de usuario necesitan acceso directo similar a un archivo (`open`/`read`/`write`)
- Estás implementando una interfaz de control (GPIO, LED, sensor)
- Es un pseudodispositivo que ofrece funcionalidad software
- No encaja en los modelos de red ni de almacenamiento

**Elige interfaz de red si**:

- El hardware envía o recibe paquetes de red (tramas Ethernet, etc.)
- Debe integrarse con la pila de red (enrutamiento, cortafuegos, sockets)
- Aparece en `ifconfig`, no en `/dev`
- Necesita soportar protocolos (TCP/IP, etc.)

**Elige almacenamiento/GEOM si**:

- El hardware proporciona almacenamiento orientado a bloques
- Debe aparecer como un disco en el sistema
- Necesita soportar sistemas de archivos
- Requiere particionado o se sitúa en una pila de transformación de almacenamiento

**Modelos mixtos**:

- Algunos dispositivos (como `tun`) presentan tanto un plano de control (dispositivo de caracteres) como un plano de datos (interfaz de red o almacenamiento)
- Esto es menos común, pero útil cuando se necesita

**¿Todavía tienes dudas?**

- Mira drivers existentes similares
- Comprueba qué esperan los programas de usuario (¿abren archivos o usan sockets?)
- Pregúntate: «¿Con qué subsistema interactúa de forma natural mi hardware?»

### Miniejercicio: clasifica drivers reales

Vamos a practicar el reconocimiento de patrones en tu sistema FreeBSD en funcionamiento.

**Instrucciones**:

1. **Identifica un dispositivo de caracteres**:
   ```bash
   % ls -l /dev/null /dev/random /dev/cuau*
   ```
   Elige uno. ¿Qué lo convierte en un dispositivo de caracteres?

2. **Identifica una interfaz de red**:
   ```bash
   % ifconfig -l
   ```
   Elige una (por ejemplo, `em0`, `lo0`). Búscala:
   ```bash
   % man 4 em
   ```
   ¿Qué hardware gestiona?

3. **Identifica un participante en el almacenamiento**:
   ```bash
   % geom disk list
   ```
   Elige un disco (por ejemplo, `ada0` o `nvd0`). ¿Qué driver lo gestiona?

4. **Encuentra el código fuente del driver**:

   Para cada uno, intenta localizar su código fuente:

   ```bash
   % find /usr/src/sys -name "null.c"
   % find /usr/src/sys -name "if_em.c"
   % find /usr/src/sys -name "ahci.c"
   ```

5. **Anótalo en tu cuaderno de laboratorio**:
   ```html
   Character: /dev/random -> sys/dev/random/randomdev.c
   Network:   em0 -> sys/dev/e1000/if_em.c
   Storage:   ada0 (via CAM) -> sys/dev/ahci/ahci.c
   ```

**Qué estás aprendiendo**: Reconocimiento. Cuando hayas terminado esto, habrás conectado conceptos abstractos (caracteres, red, almacenamiento) con ejemplos reales y concretos de tu sistema.

**Resumen**

Los drivers pertenecen a familias con formas distintas:

- **Dispositivos de caracteres**: I/O de flujo a través de `/dev`, los más sencillos de aprender
- **Dispositivos de almacenamiento**: I/O de bloques a través de GEOM/CAM, avanzado
- **Interfaces de red**: I/O de paquetes a través de ifnet, sin presencia en `/dev`
- **Pseudodispositivos**: exclusivamente de software, perfectos para aprender la estructura

**Elegir la forma correcta**: asocia el propósito de tu hardware con el subsistema del kernel con el que integra de forma natural.

En la siguiente sección, examinaremos el **esqueleto mínimo de un driver**, el andamiaje universal que comparten todos los drivers, independientemente de la familia.

## El esqueleto mínimo del driver

Todo driver de FreeBSD, desde el pseudo-dispositivo más sencillo hasta el controlador PCI más complejo, comparte un **esqueleto** común, un andamiaje de componentes obligatorios que el kernel espera. Piensa en este esqueleto como el chasis de un coche: antes de poder añadir el motor, los asientos o el equipo de sonido, necesitas la estructura básica sobre la que se monta todo lo demás.

Esta sección presenta el patrón universal que encontrarás en cada driver. Lo mantendremos **mínimo**, lo justo para cargar, hacer el attach y descargar de forma limpia. Las secciones y capítulos posteriores añadirán los músculos, los órganos y las funcionalidades.

### Tipos fundamentales: `device_t` y el softc

Dos tipos fundamentales aparecen en todo driver: `device_t` y la estructura **softc** (contexto software) de tu driver.

#### `device_t` - el identificador del kernel para *este* dispositivo

`device_t` es un **identificador opaco** gestionado por el kernel. Nunca accedes a su interior directamente; le pides al kernel lo que necesitas mediante funciones de acceso.

```c
#include <sys/bus.h>

const char *name   = device_get_name(dev);   // e.g., "mydriver"
int         unit   = device_get_unit(dev);   // 0, 1, 2, ...
device_t    parent = device_get_parent(dev); // the parent bus (PCI, USB, etc.)
void       *cookie = device_get_softc(dev);  // pointer to your softc (explained below)
```

**¿Por qué opaco?**

Para que el kernel pueda evolucionar su representación interna sin romper tu código. Interactúas a través de una API estable en lugar de mediante campos de estructura.

**Dónde lo verás**

Cada callback del ciclo de vida (`probe`, `attach`, `detach`, ...) recibe un `device_t dev`. Ese parámetro es tu "sesión" con el kernel para esta instancia concreta del dispositivo.

#### El softc - el estado privado de tu driver

Cada instancia de dispositivo necesita un lugar donde guardar estado: recursos, locks, estadísticas y cualquier información específica del hardware. Eso es el **softc** que tú defines.

**Tú lo defines**

```c
struct mydriver_softc {
    device_t         dev;        // back-pointer to device_t (handy for prints, etc.)
    struct resource *mem_res;    // MMIO resource
    int              mem_rid;    // resource ID (e.g., PCIR_BAR(0))
    struct mtx       mtx;        // driver lock
    uint64_t         bytes_rx;   // example statistic
    /* ... your driver-specific state ... */
};
```

**El kernel lo asigna por ti**

Cuando registras el driver, le indicas a Newbus el tamaño de tu softc:

```c
static driver_t mydriver_driver = {
    "mydriver",
    mydriver_methods,
    sizeof(struct mydriver_softc) // Newbus allocates and zeroes this per instance
};
```

Newbus crea (y pone a cero) un softc **por instancia de dispositivo** durante la creación del dispositivo. No llamas a `malloc()` para ello ni tampoco a `free()`.

**Lo recuperas donde lo necesitas**

```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;

    sc = device_get_softc(dev);  // get your per-instance state
    sc->dev = dev;               // stash the handle for convenience

    /* initialize locks/resources, map registers, set up interrupts, etc. */
    return (0);
}
```

Esa línea

```c
struct mydriver_softc *sc = device_get_softc(dev);
```

aparece al comienzo de casi todo método del driver que necesita estado. Es la forma idiomática de entrar en el mundo de tu driver.

#### Modelo mental

- **`device_t`**: el "ticket" que el kernel te entrega para *este* dispositivo.
- **softc**: tu "mochila" de estado vinculada a ese ticket.
- **Patrón de acceso**: el kernel llama a tu método con `dev` -> tú llamas a `device_get_softc(dev)` -> operas a través de `sc->...`.

#### Antes de continuar

- **Tiempo de vida**: el softc existe desde que Newbus crea el objeto de dispositivo y dura hasta que el dispositivo se elimina. Aun así debes **destruir los locks y liberar los recursos** en `detach`; Newbus solo libera la memoria del softc.
- **probe frente a attach**: identifica en `probe`; **no** asignes recursos allí. Inicializa el hardware en `attach`.
- **Tipos**: `device_get_softc()` devuelve `void *`; asignarlo a `struct mydriver_softc *` es correcto en C (no se necesita conversión de tipo).

Con eso tienes todo lo necesario para el esqueleto. En las secciones dedicadas iremos añadiendo recursos, interrupciones y gestión de energía, usando siempre este modelo mental como punto de partida.

### Tablas de métodos y kobj: por qué los callbacks parecen "magia"

Los drivers de FreeBSD usan **tablas de métodos** para conectar tus funciones a Newbus. Al principio puede parecer algo mágico, pero en realidad es sencillo y elegante.

**La tabla de métodos:**

```c
static device_method_t mydriver_methods[] = {
    /* Device interface (device_if.m) */
    DEVMETHOD(device_probe,     mydriver_probe),
    DEVMETHOD(device_attach,    mydriver_attach),
    DEVMETHOD(device_detach,    mydriver_detach),

    DEVMETHOD_END
};
```

**Qué significa esta tabla (visión práctica)**

Es una tabla de enrutamiento que va de los "nombres de método" de Newbus a **tus** funciones:

- **`device_probe` -> `mydriver_probe`**
   Se ejecuta cuando el kernel pregunta "¿reconoce este driver este dispositivo?"
   *Haz:* comprueba IDs o cadenas de compatibilidad, establece una descripción si quieres, devuelve un resultado de probe.
   *No hagas:* asignar recursos ni tocar el hardware todavía.
- **`device_attach` -> `mydriver_attach`**
   Se ejecuta tras ganar el probe.
   *Haz:* asigna recursos (MMIO/IRQs), inicializa el hardware, configura las interrupciones, crea tu nodo `/dev` si procede. Gestiona los fallos de forma limpia.
   *No hagas:* dejar estado parcial; deshaz los cambios o falla con elegancia.
- **`device_detach` -> `mydriver_detach`**
   Se ejecuta cuando el dispositivo se retira o se descarga.
   *Haz:* detén el hardware, desmonta las interrupciones, destruye los nodos de dispositivo, libera los recursos, destruye los locks.
   *No hagas:* devolver éxito si el dispositivo sigue en uso; devuelve `EBUSY` cuando corresponda.

> **¿Por qué mantenerlo tan pequeño?**
>
> Este capítulo se centra en el *esqueleto del driver*. Añadiremos la gestión de energía y otros hooks más adelante, para que domines primero el ciclo de vida fundamental.

**La magia que hay detrás: kobj**

Internamente, FreeBSD usa **kobj** (objetos del kernel) para implementar el despacho de métodos:

1. Las interfaces (colecciones de métodos) se definen en archivos `.m` (por ejemplo, `device_if.m`, `bus_if.m`).
2. Las herramientas de construcción generan código C de enlace a partir de esos archivos `.m`.
3. En tiempo de ejecución, kobj usa tu tabla de métodos para localizar la función correcta a llamar.

**Ejemplo**

Cuando el kernel quiere hacer probe de un dispositivo, en esencia hace:

```c
DEVICE_PROBE(dev);  // The macro expands to a kobj lookup; kobj finds mydriver_probe here
```

**Por qué importa**

- El kernel puede llamar a los métodos de forma polimórfica (misma llamada, distintas implementaciones de driver).
- Solo sobreescribes lo que necesitas; los métodos no implementados recurren a valores predeterminados cuando es posible.
- Las interfaces son componibles: añadirás más (por ejemplo, métodos de bus o de gestión de energía) a medida que tu driver crezca.

**Qué añadirás más adelante (cuando estés listo)**

- **`device_shutdown` -> `mydriver_shutdown`**
   Se llama durante el reinicio o apagado para dejar el hardware en un estado seguro.
   *(Añádelo una vez que tu flujo básico de attach/detach esté afianzado.)*
- **`device_suspend` / `device_resume`**
   Para soporte de suspensión e hibernación: detén y restaura el hardware.
   *(Se trata cuando abordemos la gestión de energía en el Capítulo 22.)*

**Modelo mental**

Piensa en la tabla como un diccionario: las claves son nombres de método como `device_attach`; los valores son tus funciones. Los macros `DEVICE_*` le piden a kobj que "encuentre la función para este método en este objeto", y kobj consulta tu tabla para llamarla. No hay magia: solo código de despacho generado automáticamente.

### Los macros de registro que siempre encontrarás

Estos macros son la "tarjeta de presentación" del driver. Le dicen al kernel **quién eres**, **dónde haces el attach** y **de qué dependes**.

#### 1) `DRIVER_MODULE` - registra tu driver

```c
/* Minimal pattern: pick the correct parent bus for your hardware */
DRIVER_MODULE(mydriver, pci, mydriver_driver, NULL, NULL);

/*
 * Use the parent bus your device lives on: 'pci', 'usb', 'acpi', 'simplebus', etc.
 * 'nexus' is the machine-specific root bus and is rarely what you want for ordinary drivers.
 */
```

**Parámetros (el orden importa):**

- **`mydriver`** - el nombre del driver (aparece en los logs y como base del nombre de unidad, por ejemplo `mydriver0`).
- **`pci`** - el **bus padre** donde haces el attach (elige el que corresponda a tu hardware: `pci`, `usb`, `acpi`, `simplebus`, ...).
- **`mydriver_driver`** - tu `driver_t` (declara la tabla de métodos y el tamaño del softc).
- **`NULL`** - **manejador de eventos de módulo** opcional (se llama con `MOD_LOAD`/`MOD_UNLOAD`; usa `NULL` a menos que necesites inicialización a nivel de módulo).
- **`NULL`** - **argumento** opcional que se pasa a ese manejador de eventos (usa `NULL` cuando el manejador sea `NULL`).

> **Cuándo mantenerlo mínimo**
>
> Al principio de este capítulo nos centramos en el esqueleto. Pasar `NULL` tanto para el manejador de eventos como para su argumento mantiene las cosas simples.
> **Nota:** elige el bus padre real de tu dispositivo; `nexus` es el bus raíz y casi nunca es la opción correcta para drivers ordinarios.

> **Nota histórica (versiones anteriores a FreeBSD 13)**
>
> En código antiguo que puedas encontrar en internet aparece a veces una forma de seis argumentos como `DRIVER_MODULE(name, bus, driver, devclass, evh, arg)` junto con una variable `devclass_t` separada. FreeBSD moderno gestiona las devclasses de forma automática y el macro acepta ahora exactamente cinco argumentos tal como se muestra. Si copias un ejemplo heredado, elimina el argumento extra de devclass antes de construir.

**Qué hace realmente `DRIVER_MODULE`**

- Registra tu driver en Newbus bajo un bus padre.
- Expone tu tabla de métodos y el tamaño del softc a través de `driver_t`.
- Garantiza que el cargador sepa cómo **hacer coincidir** los dispositivos descubiertos en ese bus con tu driver.

#### 2) `MODULE_VERSION` - etiqueta tu módulo con una versión

```c
MODULE_VERSION(mydriver, 1);
```

Esto marca el módulo con un número de versión entero simple.

**Por qué importa**

- El kernel y otros módulos pueden comprobar tu versión para **evitar incompatibilidades**.
- Si realizas un cambio que rompe el ABI o los símbolos exportados del módulo, **incrementa** este número.

> **Convención:** comienza en `1` e incrementa solo cuando algo externo se rompería si se cargase una versión anterior.

#### 3) `MODULE_DEPEND` - declara dependencias (cuando las tengas)

```c
/* mydriver requires the USB stack to be present */
MODULE_DEPEND(mydriver, usb, 1, 1, 1);
```

**Parámetros:**

- **`mydriver`** - tu módulo.
- **`usb`** - el módulo del que dependes.
- **`1, 1, 1`** - versiones **mínima**, **preferida** y **máxima** de la dependencia (usar `1` para las tres es habitual cuando no hay versionado complejo que aplicar).

**Cuándo usarlo**

- Tu driver necesita que otro módulo esté cargado **primero** (por ejemplo, `usb`, `pci` o un módulo de biblioteca auxiliar).
- Exportas o consumes símbolos que requieren versiones coherentes entre módulos.

#### Modelo mental

- `DRIVER_MODULE` le dice a Newbus **quién eres** y **dónde te conectas**.
- `MODULE_VERSION` ayuda al cargador a mantener juntas piezas **compatibles**.
- `MODULE_DEPEND` garantiza que los módulos se carguen en el **orden correcto** para que tus símbolos y subsistemas estén listos cuando tu driver arranque.

> **Qué escribirás ahora frente a lo que vendrá después**
>
> Para el esqueleto mínimo del driver en este capítulo, incluirás casi siempre **`DRIVER_MODULE`** y **`MODULE_VERSION`**.
>
> Añade **`MODULE_DEPEND`** cuando realmente dependas de otro módulo; presentaremos las dependencias más habituales (y cuándo son necesarias) en capítulos posteriores dedicados a los buses PCI, USB, ACPI y SoC.

### Acceder al estado y comunicar con claridad

Dos patrones aparecen en casi todas las funciones de un driver: recuperar el softc y registrar mensajes.

**Recuperar el estado: device_get_softc()**

```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    
    sc = device_get_softc(dev);  // Get our private data
    
    // Now use sc-> for everything
    sc->dev = dev;
    sc->some_flag = 1;
}
```

Esta es tu **primera línea** en casi toda función del driver. Conecta el `device_t` que el kernel te ha dado con tu estado privado.

**Registrar mensajes: device_printf()**

Cuando tu driver necesita registrar información, usa `device_printf()`:

```c
device_printf(dev, "Driver attached successfully\n");
device_printf(dev, "Hardware version: %d.%d\n", major, minor);
```

**¿Por qué `device_printf` en lugar del `printf` habitual?**

- Añade como **prefijo** el nombre de tu dispositivo: `mydriver0: Driver attached successfully`
- Los usuarios saben de inmediato **qué dispositivo** está generando el mensaje
- Es imprescindible cuando existen varias instancias (mydriver0, mydriver1, ...)

**Ejemplo de salida**:

```html
em0: Intel PRO/1000 Network Connection 7.6.1-k
em0: Link is Up 1000 Mbps Full Duplex
```

**Buenas prácticas de registro** (ampliaremos esto en la sección "Registro, errores y comportamiento visible para el usuario"):

- **Attach**: registra una línea al hacer el attach con éxito
- **Errores**: registra siempre el motivo del fallo
- **Información detallada**: solo durante el boot o al depurar
- **Evita el spam**: no registres en cada paquete o interrupción (usa contadores en su lugar)

**Buen ejemplo**:

```c
if (error != 0) {
    device_printf(dev, "Could not allocate memory resource\n");
    return (error);
}
device_printf(dev, "Attached successfully\n");
```

**Mal ejemplo**:

```c
printf("Attaching...\n");  // No device name!
printf("Step 1\n");         // Too verbose
printf("Step 2\n");         // User doesn't care
```

### Construcción y carga de un stub de forma segura (vista previa)

Aún no construiremos un driver completo (eso llega en el Capítulo 7 y en el Laboratorio 2), pero echemos un vistazo previo al **ciclo de construcción y carga** para que sepas lo que se avecina.

**El Makefile mínimo**:

```makefile
# Makefile
KMOD=    mydriver
SRCS=    mydriver.c

.include <bsd.kmod.mk>
```

Eso es todo. El sistema de construcción de módulos del kernel de FreeBSD (`bsd.kmod.mk`) gestiona toda la complejidad.

**Construcción**:

```bash
% make clean
% make
```

Esto produce `mydriver.ko` (archivo objeto del kernel).

**Carga**:

```bash
% sudo kldload ./mydriver.ko
```

**Verificación**:

```bash
% kldstat | grep mydriver
% dmesg | tail
```

**Descarga**:

```bash
% sudo kldunload mydriver
```

**Qué ocurre entre bastidores**:

1. `kldload` lee tu archivo `.ko`
2. El kernel resuelve los símbolos y lo enlaza con el kernel
3. El kernel llama a tu manejador de eventos de módulo con `MOD_LOAD`
4. Si has registrado dispositivos o drivers, ahora están disponibles
5. Newbus puede hacer probe/attach inmediatamente si hay dispositivos presentes

**Al descargar**:

1. El kernel comprueba si es seguro descargar (sin dispositivos adjuntos, sin usuarios activos)
2. Llama a tu manejador de eventos de módulo con `MOD_UNLOAD`
3. Desvincula el código del kernel
4. Libera el módulo

**Nota de seguridad**: En tu VM de laboratorio, cargar y descargar es seguro. Si tu código hace caer el kernel, la VM se reinicia sin consecuencias. **Nunca pruebes drivers nuevos en sistemas en producción**.

**Vista previa del laboratorio**: En la sección "Laboratorios prácticos", el Laboratorio 2 te guiará paso a paso para construir y cargar un módulo mínimo que solo registra mensajes. Por ahora, basta con conocer este ciclo que seguirás.

**Resumen**

El esqueleto mínimo del driver incluye:

1. **device_t** - Manejador opaco para tu dispositivo
2. **Estructura softc** - Tus datos privados por dispositivo
3. **Tabla de métodos** - Asocia las llamadas a métodos del kernel con tus funciones
4. **DRIVER_MODULE** - Registra tu driver con el kernel
5. **MODULE_VERSION** - Declara tu versión
6. **device_get_softc()** - Recupera tu estado en cada función
7. **device_printf()** - Registra mensajes con el prefijo del nombre del dispositivo

**Este patrón aparece en todos los drivers de FreeBSD**. Domínalo y podrás leer cualquier código de driver con confianza.

A continuación, exploraremos el **ciclo de vida de Newbus**, cuándo y por qué se llama a cada uno de estos métodos.

## El ciclo de vida de Newbus: del descubrimiento al cierre

Ya has visto el esqueleto (las funciones probe, attach, detach). Ahora vamos a entender **cuándo** y **por qué** las llama el kernel. El ciclo de vida del dispositivo en Newbus es una secuencia cuidadosamente orquestada, y conocer este flujo es esencial para escribir código de inicialización y limpieza correcto.

Piensa en ello como el ciclo de vida de un restaurante: hay un orden específico para la apertura (inspeccionar el local, conectar los suministros, preparar la cocina), el funcionamiento (atender a los clientes) y el cierre (limpiar, apagar los equipos, desconectar los suministros). Los drivers siguen un ciclo de vida similar, y entender la secuencia te ayuda a escribir código robusto.

### De dónde viene la enumeración

Antes de que tu driver se ejecute, **el hardware debe ser descubierto**. Esto se denomina **enumeración**, y es tarea de los **drivers de bus**.

**Cómo los buses descubren dispositivos**

**Bus PCI**: Lee el espacio de configuración en cada dirección bus/dispositivo/función. Cuando encuentra un dispositivo que responde, lee el ID de fabricante, el ID de dispositivo, el código de clase y los requisitos de recursos (BARs de memoria, líneas IRQ).

**Bus USB**: Cuando conectas un dispositivo, el hub detecta cambios eléctricos, emite un reset USB y consulta el descriptor del dispositivo para identificarlo.

**Bus ACPI**: Analiza las tablas proporcionadas por la BIOS/UEFI que describen los dispositivos de la plataforma (UARTs, temporizadores, controladores integrados, etc.).

**Device tree (ARM/embebido)**: Lee un blob del árbol de dispositivos (DTB) que describe de forma estática la distribución del hardware.

**Idea clave**: **Tu driver no busca dispositivos**. Los dispositivos te llegan a través de los drivers de bus. Tú reaccionas a lo que el kernel te presenta.

**El resultado de la enumeración**

Para cada dispositivo descubierto, el bus crea una estructura `device_t` que contiene:

- Nombre del dispositivo (p. ej., `pci0:0:2:0`)
- Bus padre
- IDs de fabricante/dispositivo o cadenas de compatibilidad
- Requisitos de recursos

**Compruébalo tú mismo**:
```bash
% devinfo -v        # View device tree
% pciconf -lv       # PCI devices with vendor/device IDs
% sudo usbconfig dump_device_desc    # USB device descriptors
```

**Temporización**: La enumeración ocurre durante el boot para los dispositivos integrados, o de forma dinámica cuando conectas hardware con soporte hot-plug (USB, Thunderbolt, PCIe hot-plug, etc.).

### probe: «¿Soy tu driver?»

Una vez que el dispositivo existe, el kernel necesita encontrar el driver adecuado para él. Para ello, llama a la función **probe** de cada driver compatible.

**La signatura de probe**:
```c
static int
mydriver_probe(device_t dev)
{
    /* Examine the device and decide if we can handle it */
    
    /* If yes: */
    device_set_desc(dev, "My Awesome Hardware");
    return (BUS_PROBE_DEFAULT);
    
    /* If no: */
    return (ENXIO);
}
```

**Tu tarea en probe**:

1. **Examinar las propiedades del dispositivo** (ID de fabricante/dispositivo, cadena de compatibilidad, etc.)
2. **Decidir si puedes gestionarlo**
3. **Devolver un valor de prioridad** o un error

**Ejemplo: probe de un driver PCI**
```c
static int
mydriver_probe(device_t dev)
{
    uint16_t vendor = pci_get_vendor(dev);
    uint16_t device = pci_get_device(dev);
    
    if (vendor == MY_VENDOR_ID && device == MY_DEVICE_ID) {
        device_set_desc(dev, "My PCI Device");
        return (BUS_PROBE_DEFAULT);
    }
    
    return (ENXIO);  /* Not our device */
}
```

**Valores de retorno y prioridad de probe** (de `/usr/src/sys/sys/bus.h`):

| Valor de retorno         | Valor numérico  | Significado                                               |
|--------------------------|-----------------|-----------------------------------------------------------|
| `BUS_PROBE_SPECIFIC`     | 0               | Coincidencia exacta con esta variante del dispositivo     |
| `BUS_PROBE_VENDOR`       | -10             | Driver suministrado por el fabricante                     |
| `BUS_PROBE_DEFAULT`      | -20             | Driver estándar para esta clase de dispositivo            |
| `BUS_PROBE_LOW_PRIORITY` | -40             | Funciona, pero probablemente haya algo mejor              |
| `BUS_PROBE_GENERIC`      | -100            | Fallback genérico (p. ej., coincidencia a nivel de clase) |
| `BUS_PROBE_HOOVER`       | -1000000        | Captura todo para dispositivos sin driver real (`ugen`)   |
| `BUS_PROBE_NOWILDCARD`   | -2000000000     | Solo se asocia cuando el padre lo solicita por nombre     |
| `ENXIO`                  | 6 (positivo)    | No es nuestro dispositivo                                 |

**Gana el más cercano a cero.** Todas estas prioridades (excepto `ENXIO`) son cero o negativas, y Newbus elige el driver cuyo valor de retorno sea el **mayor**, es decir, el menos negativo, lo que equivale a la coincidencia más específica. `BUS_PROBE_SPECIFIC` (0) supera a todos; `BUS_PROBE_DEFAULT` (-20) supera a `BUS_PROBE_GENERIC` (-100); y cualquier valor no negativo se trata como un error.

**Por qué importa**: El esquema de prioridades permite que un driver especializado tenga precedencia sobre uno genérico sin que ninguno de los dos sepa nada del otro. Un driver optimizado por el fabricante que devuelva `BUS_PROBE_VENDOR` (-10) ganará a un driver del sistema base que devuelva `BUS_PROBE_DEFAULT` (-20) para el mismo dispositivo.

**Reglas para probe**:

- **Sí**: Examina las propiedades del dispositivo
- **Sí**: Establece una descripción del dispositivo con `device_set_desc()`
- **Sí**: Retorna rápidamente (sin inicializaciones largas)
- **No**: Modifiques el estado del hardware
- **No**: Asignes recursos (espera al attach)
- **No**: Des por hecho que ganarás (otro driver podría superarte)

**Ejemplo real** de `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
static int
uart_pci_probe(device_t dev)
{
        struct uart_softc *sc;
        const struct pci_id *id;
        struct pci_id cid = {
                .regshft = 0,
                .rclk = 0,
                .rid = 0x10 | PCI_NO_MSI,
                .desc = "Generic SimpleComm PCI device",
        };
        int result;

        sc = device_get_softc(dev);

        id = uart_pci_match(dev, pci_ns8250_ids);
        if (id != NULL) {
                sc->sc_class = &uart_ns8250_class;
                goto match;
        }
        if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
            pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
            pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
                /* XXX rclk what to do */
                id = &cid;
                sc->sc_class = &uart_ns8250_class;
                goto match;
        }
        /* Add checks for non-ns8250 IDs here. */
        return (ENXIO);

 match:
        result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
            id->rid & PCI_RID_MASK, 0, 0);
        /* Bail out on error. */
        if (result > 0)
                return (result);
        /*
         * If we haven't already matched this to a console, check if it's a
         * PCI device which is known to only exist once in any given system
         * and we can match it that way.
         */
        if (sc->sc_sysdev == NULL)
                uart_pci_unique_console_match(dev);
        /* Set/override the device description. */
        if (id->desc)
                device_set_desc(dev, id->desc);
        return (result);
}
```

**Qué ocurre después de probe**: El kernel recopila todos los resultados exitosos de probe, los ordena por prioridad y selecciona al ganador. A continuación, se llamará a la función `attach` de ese driver.

### attach: «Prepárate para operar»

Si tu función probe ganó, el kernel llama a tu función **attach**. Aquí es donde ocurre la **inicialización real**.

**La signatura de attach**:
```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    int error;
    
    sc = device_get_softc(dev);
    sc->dev = dev;
    
    /* Initialization steps go here */
    
    device_printf(dev, "Attached successfully\n");
    return (0);  /* Success */
}
```

**Flujo típico de attach**:

**Paso 1: Obtener tu softc**
```c
struct mydriver_softc *sc = device_get_softc(dev);
sc->dev = dev;  /* Store back-pointer */
```

**Paso 2: Asignar recursos de hardware**
```c
sc->mem_rid = PCIR_BAR(0);
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
    &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "Could not allocate memory\n");
    return (ENXIO);
}
```

**Paso 3: Inicializar el hardware**
```c
/* Reset hardware */
/* Configure registers */
/* Detect hardware capabilities */
```

**Paso 4: Configurar las interrupciones** (si es necesario)
```c
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
    &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
    
// Placeholder - interrupt handler implementation covered in Chapter 19
error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
    NULL, mydriver_intr, sc, &sc->irq_hand);
```

**Paso 5: Crear nodos de dispositivo o registrarse en los subsistemas**
```c
/* Character device: */
sc->cdev = make_dev(&mydriver_cdevsw, unit,
    UID_ROOT, GID_WHEEL, 0600, "mydriver%d", unit);
    
/* Network interface: */
ether_ifattach(ifp, sc->mac_addr);

/* Storage: */
/* Register with CAM or GEOM */
```

**Paso 6: Marcar el dispositivo como listo**
```c
device_printf(dev, "Successfully attached\n");
return (0);
```

**El manejo de errores es fundamental**: Si cualquier paso falla, debes limpiar **todo** lo que ya has hecho:

```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    int error;
    
    sc = device_get_softc(dev);
    
    /* Step 1 */
    sc->mem_res = bus_alloc_resource_any(...);
    if (sc->mem_res == NULL) {
        error = ENXIO;
        goto fail;
    }
    
    /* Step 2 */
    error = mydriver_hw_init(sc);
    if (error != 0)
        goto fail;
    
    /* Step 3 */
    sc->irq_res = bus_alloc_resource_any(...);
    if (sc->irq_res == NULL) {
        error = ENXIO;
        goto fail;
    }
    
    /* Success! */
    return (0);

fail:
    mydriver_detach(dev);  /* Clean up partial state */
    return (error);
}
```

**¿Por qué saltar a `fail` y llamar a detach?** Porque detach está diseñado para limpiar recursos. Al llamarlo ante un fallo, reutilizas la lógica de limpieza en lugar de duplicarla.

### detach y shutdown: «No dejes rastro»

Cuando tu driver se descarga o el dispositivo se retira, el kernel llama a tu función **detach** para realizar un cierre limpio.

**La signatura de detach**:
```c
static int
mydriver_detach(device_t dev)
{
    struct mydriver_softc *sc;
    
    sc = device_get_softc(dev);
    
    /* Cleanup steps in reverse order of attach */
    
    device_printf(dev, "Detached\n");
    return (0);
}
```

**Flujo típico de detach** (inverso al de attach):

**Paso 1: Verificar que es seguro hacer el detach**
```c
if (sc->open_count > 0) {
    return (EBUSY);  /* Device is in use, can't detach now */
}
```

**Paso 2: Detener el hardware**
```c
mydriver_hw_stop(sc);  /* Disable interrupts, stop DMA, reset chip */
```

**Paso 3: Desmontar las interrupciones**
```c
if (sc->irq_hand != NULL) {
    bus_teardown_intr(dev, sc->irq_res, sc->irq_hand);
    sc->irq_hand = NULL;
}
if (sc->irq_res != NULL) {
    bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
    sc->irq_res = NULL;
}
```

**Paso 4: Destruir los nodos de dispositivo o darse de baja**
```c
if (sc->cdev != NULL) {
    destroy_dev(sc->cdev);
    sc->cdev = NULL;
}
/* or */
ether_ifdetach(ifp);
```

**Paso 5: Liberar los recursos de hardware**
```c
if (sc->mem_res != NULL) {
    bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
    sc->mem_res = NULL;
}
```

**Paso 6: Liberar otras asignaciones de memoria**
```c
if (sc->buffer != NULL) {
    free(sc->buffer, M_DEVBUF);
    sc->buffer = NULL;
}
mtx_destroy(&sc->mtx);
```

**Reglas fundamentales**:

- **Sí**: Libera los recursos en orden inverso a su asignación
- **Sí**: Comprueba siempre los punteros antes de liberar (detach puede llamarse durante un attach parcial)
- **Sí**: Pon los punteros a NULL después de liberar
- **No**: Accedas al hardware después de detenerlo
- **No**: Liberes recursos que aún estén en uso

**El método shutdown**:

Algunos drivers también implementan un método `shutdown` para un apagado ordenado del sistema:

```c
static int
mydriver_shutdown(device_t dev)
{
    struct mydriver_softc *sc = device_get_softc(dev);
    
    /* Put hardware in a safe state for reboot */
    mydriver_hw_shutdown(sc);
    
    return (0);
}
```

Añádelo a la tabla de métodos:
```c
DEVMETHOD(device_shutdown,  mydriver_shutdown),
```

Se llama cuando el sistema se reinicia o se apaga, lo que permite al driver detener el hardware de forma ordenada.

### El patrón de desenrollado en caso de error

Ya hemos visto indicios de esto, pero vamos a hacerlo explícito. El **desenrollado en caso de error** es un patrón reutilizable para gestionar fallos parciales durante el attach.

**El patrón**:
```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    int error = 0;
    
    sc = device_get_softc(dev);
    sc->dev = dev;
    
    /* Initialize mutex */
    mtx_init(&sc->mtx, "mydriver", NULL, MTX_DEF);
    
    /* Allocate resource 1 */
    sc->mem_res = bus_alloc_resource_any(...);
    if (sc->mem_res == NULL) {
        error = ENXIO;
        goto fail_mtx;
    }
    
    /* Allocate resource 2 */
    sc->irq_res = bus_alloc_resource_any(...);
    if (sc->irq_res == NULL) {
        error = ENXIO;
        goto fail_mem;
    }
    
    /* Initialize hardware */
    error = mydriver_hw_init(sc);
    if (error != 0)
        goto fail_irq;
    
    /* Success! */
    device_printf(dev, "Attached\n");
    return (0);

/* Cleanup labels in reverse order */
fail_irq:
    bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
fail_mem:
    bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
fail_mtx:
    mtx_destroy(&sc->mtx);
    return (error);
}
```

**Por qué funciona**:

- Cada `goto` salta al nivel de limpieza correcto
- Los recursos se liberan en orden inverso
- Ningún recurso queda pendiente
- El código es legible y fácil de mantener

**Patrón alternativo**

Llamar a detach en caso de error:

```c
fail:
    mydriver_detach(dev);
    return (error);
}
```

Esto funciona si tu función detach comprueba los punteros antes de liberar (¡y debería hacerlo!).

### Observando el ciclo de vida en los registros

La mejor forma de entender el ciclo de vida es **verlo en acción**. El sistema de registro de FreeBSD lo facilita mucho.

**Observa en tiempo real**:

Terminal 1:
```bash
% tail -f /var/log/messages
```

Terminal 2:
```bash
% sudo kldload if_em
% sudo kldunload if_em
```

**Qué verás**:
```text
Oct 14 12:34:56 freebsd kernel: em0: <Intel(R) PRO/1000 Network Connection> port 0xc000-0xc01f mem 0xf0000000-0xf001ffff at device 2.0 on pci0
Oct 14 12:34:56 freebsd kernel: em0: Ethernet address: 00:0c:29:3a:4f:1e
Oct 14 12:34:56 freebsd kernel: em0: netmap queues/slots: TX 1/1024, RX 1/1024
```

La primera línea proviene de la función attach del driver. Puedes ver que detectó el dispositivo, asignó recursos y se inicializó.

**Al descargarlo**:
```text
Oct 14 12:35:10 freebsd kernel: em0: detached
```

**Usando dmesg**:
```bash
% dmesg | grep em0
```

Esto muestra todos los mensajes del kernel relacionados con `em0` desde el boot.

**Usando devmatch**:

La utilidad `devmatch` de FreeBSD muestra los dispositivos sin driver asignado y sugiere drivers:
```bash
% devmatch
```

Ejemplo de salida:
```text
pci0:0:2:0 needs if_em
```

**Ejercicio**: Carga y descarga un driver sencillo mientras observas los registros. Prueba con:
```bash
% sudo kldload null
% dmesg | tail
% kldstat | grep null
% sudo kldunload null
```

No verás gran cosa con `null` (es silencioso), pero el kernel confirma la carga y la descarga.

**Resumen**

El ciclo de vida de Newbus sigue una secuencia estricta:

1. **Enumeración**: Los drivers de bus descubren el hardware y crean estructuras device_t
2. **Probe**: El kernel pregunta a los drivers «¿Puedes gestionar esto?» mediante las funciones probe
3. **Selección de driver**: Gana la mejor coincidencia según los valores de prioridad devueltos
4. **Attach**: La función attach del ganador inicializa el hardware y los recursos
5. **Operación**: El dispositivo está listo para su uso (lectura/escritura, transmisión/recepción, etc.)
6. **Detach**: El driver realiza un cierre limpio y libera todos los recursos
7. **Destrucción**: El kernel libera el device_t tras un detach exitoso

**Patrones clave**:

- Probe: Solo examina, no modifiques
- Attach: Inicializa todo, gestiona los fallos con saltos de limpieza
- Detach: Orden inverso al de attach, comprueba todos los punteros y ponlos a NULL

**A continuación**, exploraremos los puntos de entrada de los dispositivos de caracteres, incluyendo cómo gestiona tu driver las operaciones open, read, write e ioctl.

## Puntos de entrada del dispositivo de caracteres: tu superficie de I/O

Ahora que entiendes cómo los drivers hacen attach y detach, veamos cómo realizan el trabajo real. Para los dispositivos de caracteres, esto significa implementar el **cdevsw** (conmutador de dispositivo de caracteres), una estructura que enruta las llamadas al sistema del espacio de usuario hacia las funciones de tu driver.

Piensa en cdevsw como un **menú de servicios** que ofrece tu driver. Cuando un programa abre `/dev/yourdevice` y llama a `read()`, el kernel busca la función `d_read` de tu driver y la llama. Esta sección te muestra cómo funciona ese enrutamiento.

### cdev y cdevsw: la tabla de enrutamiento

Dos estructuras relacionadas son el motor de las operaciones de los dispositivos de caracteres:

- **`struct cdev`** - Representa una instancia de dispositivo de caracteres
- **`struct cdevsw`** - Define las operaciones que soporta tu driver

**La estructura cdevsw** (de `/usr/src/sys/sys/conf.h`):

```c
struct cdevsw {
    int                 d_version;   /* Always D_VERSION */
    u_int               d_flags;     /* Device flags */
    const char         *d_name;      /* Base device name */
    
    d_open_t           *d_open;      /* Open handler */
    d_close_t          *d_close;     /* Close handler */
    d_read_t           *d_read;      /* Read handler */
    d_write_t          *d_write;     /* Write handler */
    d_ioctl_t          *d_ioctl;     /* Ioctl handler */
    d_poll_t           *d_poll;      /* Poll/select handler */
    d_mmap_t           *d_mmap;      /* Mmap handler */
    d_strategy_t       *d_strategy;  /* (Deprecated) */
    dumper_t           *d_dump;      /* Crash dump handler */
    d_kqfilter_t       *d_kqfilter;  /* Kqueue filter */
    d_purge_t          *d_purge;     /* Purge handler */
    /* ... additional fields for advanced features ... */
};
```

**Ejemplo mínimo** de `/usr/src/sys/dev/null/null.c`:

```c
static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};
```

Fíjate en lo que falta: no hay `d_open`, ni `d_close`, ni `d_poll`, ni `d_kqfilter`. Si no implementas un método, el kernel proporciona valores predeterminados razonables:

- Sin `d_open`  ->  Siempre tiene éxito
- Sin `d_close`  ->  Siempre tiene éxito
- Sin `d_read`  ->  Devuelve EOF (0 bytes)
- Sin `d_write`  ->  Devuelve el error ENODEV

**Por qué funciona**: La mayoría de los dispositivos sencillos no necesitan una lógica compleja de apertura y cierre. Implementa solo lo que necesitas.

### open/close: sesiones y estado por apertura

Cuando un programa de usuario abre tu dispositivo, el kernel llama a tu función `d_open`. Es tu oportunidad para inicializar el estado por apertura, comprobar permisos o rechazar la apertura si las condiciones no son las adecuadas.

**La signatura de d_open**:
```c
typedef int d_open_t(struct cdev *dev, int oflags, int devtype, struct thread *td);
```

**Parámetros**:

- `dev` - Tu estructura cdev
- `oflags` - Flags de apertura (O_RDONLY, O_RDWR, O_NONBLOCK, etc.)
- `devtype` - Tipo de dispositivo (habitualmente se ignora)
- `td` - Thread que realiza la apertura

**Función open típica**:
```c
static int
mydriver_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct mydriver_softc *sc;
    
    sc = dev->si_drv1;  /* Get softc from cdev back-pointer */
    
    /* Check if already open (if exclusive access needed) */
    if (sc->flags & MYDRV_OPEN) {
        return (EBUSY);
    }
    
    /* Mark as open */
    sc->flags |= MYDRV_OPEN;
    sc->open_count++;
    
    device_printf(sc->dev, "Device opened\n");
    return (0);
}
```

**La signatura de d_close**:
```c
typedef int d_close_t(struct cdev *dev, int fflag, int devtype, struct thread *td);
```

**Función close típica**:
```c
static int
mydriver_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
    struct mydriver_softc *sc;
    
    sc = dev->si_drv1;
    
    /* Clean up per-open state */
    sc->flags &= ~MYDRV_OPEN;
    sc->open_count--;
    
    device_printf(sc->dev, "Device closed\n");
    return (0);
}
```

**Cuándo usar open/close**:

- **Inicializar el estado por sesión** (buffers, cursores)
- **Garantizar acceso exclusivo** (un único abridor a la vez)
- **Restablecer el estado del hardware** en la apertura y el cierre
- **Registrar el uso** para depuración

**Cuándo puedes omitirlos**:

- El dispositivo no necesita configuración al abrirse
- El hardware siempre está listo (como /dev/null)

### read/write: transferencia segura de bytes

read y write son el núcleo de la transferencia de datos para los dispositivos de caracteres. El kernel proporciona una **estructura uio (user I/O)** para abstraer el buffer y gestionar la copia de forma segura entre el espacio del kernel y el espacio de usuario.

**La signatura de d_read**:
```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
```

**La signatura de d_write**:
```c
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

**Parámetros**:

- `dev` - Tu cdev
- `uio` - Estructura de I/O de usuario (describe el buffer, el desplazamiento y los bytes restantes)
- `ioflag` - Flags de I/O (IO_NDELAY para no bloqueante, etc.)

**Ejemplo simple de lectura**:
```c
static int
mydriver_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct mydriver_softc *sc;
    char data[128];
    int error, len;
    
    sc = dev->si_drv1;
    
    /* How much does user want? */
    len = MIN(uio->uio_resid, sizeof(data));
    if (len == 0)
        return (0);  /* EOF */
    
    /* Fill buffer with your data */
    snprintf(data, sizeof(data), "Hello from mydriver\\n");
    len = MIN(len, strlen(data));
    
    /* Copy to user space */
    error = uiomove(data, len, uio);
    
    return (error);
}
```

**Ejemplo simple de escritura**:
```c
static int
mydriver_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct mydriver_softc *sc;
    char buffer[128];
    int error, len;
    
    sc = dev->si_drv1;
    
    /* Get write size (bounded by our buffer) */
    len = MIN(uio->uio_resid, sizeof(buffer) - 1);
    if (len == 0)
        return (0);
    
    /* Copy from user space */
    error = uiomove(buffer, len, uio);
    if (error != 0)
        return (error);
    
    buffer[len] = '\\0';  /* Null terminate if treating as string */
    
    /* Do something with the data */
    device_printf(sc->dev, "User wrote: %s\\n", buffer);
    
    return (0);
}
```

**Funciones clave para I/O**:

**uiomove()** - Copia entre el buffer del kernel y el espacio de usuario

```c
int uiomove(void *cp, int n, struct uio *uio);
```

**uio_resid** - Bytes restantes por transferir
```c
if (uio->uio_resid == 0)
    return (0);  /* Nothing to do */
```

**Por qué existe uio**

 Se encarga de:

- Buffers multisegmento (scatter-gather)
- Transferencias parciales
- Seguimiento del desplazamiento
- Copia segura entre el espacio del kernel y el espacio de usuario

### ioctl: Rutas de control

Ioctl (control de I/O) es la **navaja suiza** de las operaciones de dispositivo. Gestiona todo aquello que no encaja en read/write: configuración, consulta de estado, activación de acciones, etc.

**La firma de d_ioctl**:
```c
typedef int d_ioctl_t(struct cdev *dev, u_long cmd, caddr_t data, 
                       int fflag, struct thread *td);
```

**Parámetros**:

- `dev` - Tu cdev
- `cmd` - Código de comando (constante definida por el usuario)
- `data` - Puntero a la estructura de datos (ya copiada desde el espacio de usuario por el kernel)
- `fflag` - Flags del archivo
- `td` - Thread

**Definición de comandos ioctl**

Usa las macros `_IO`, `_IOR`, `_IOW`, `_IOWR`:

```c
#include <sys/ioccom.h>

/* Command with no data */
#define MYDRV_RESET         _IO('M', 0)

/* Command that reads data (kernel -> user) */
#define MYDRV_GETSTATUS     _IOR('M', 1, struct mydrv_status)

/* Command that writes data (user -> kernel) */
#define MYDRV_SETCONFIG     _IOW('M', 2, struct mydrv_config)

/* Command that does both */
#define MYDRV_EXCHANGE      _IOWR('M', 3, struct mydrv_data)
```

**La `'M'` es tu «número mágico»** (letra única que identifica a tu driver). Elige una que no usen ya los ioctls del sistema.

**Implementación de ioctl**:
```c
static int
mydriver_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
               int fflag, struct thread *td)
{
    struct mydriver_softc *sc;
    struct mydrv_status *status;
    struct mydrv_config *config;
    
    sc = dev->si_drv1;
    
    switch (cmd) {
    case MYDRV_RESET:
        /* Reset hardware */
        mydriver_hw_reset(sc);
        return (0);
        
    case MYDRV_GETSTATUS:
        /* Return status to user */
        status = (struct mydrv_status *)data;
        status->flags = sc->flags;
        status->count = sc->packet_count;
        return (0);
        
    case MYDRV_SETCONFIG:
        /* Apply configuration */
        config = (struct mydrv_config *)data;
        if (config->speed > MAX_SPEED)
            return (EINVAL);
        sc->speed = config->speed;
        return (0);
        
    default:
        return (ENOTTY);  /* Invalid ioctl */
    }
}
```

**Buenas prácticas**:

- Devuelve siempre **ENOTTY** para comandos desconocidos
- **Valida toda la entrada** (rangos, punteros, etc.)
- Usa nombres significativos para los comandos
- Documenta tu interfaz ioctl (página de manual o comentarios en la cabecera)
- No des por supuesto que los punteros de datos son válidos (el kernel ya los ha validado)

**Ejemplo real** de `/usr/src/sys/dev/usb/misc/uled.c`:

```c
static int
uled_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
        struct uled_softc *sc;
        struct uled_color color;
        int error;

        sc = usb_fifo_softc(fifo);
        error = 0;

        mtx_lock(&sc->sc_mtx);

        switch(cmd) {
        case ULED_GET_COLOR:
                *(struct uled_color *)addr = sc->sc_color;
                break;
        case ULED_SET_COLOR:
                color = *(struct uled_color *)addr;
                uint8_t buf[8];

                sc->sc_color.red = color.red;
                sc->sc_color.green = color.green;
                sc->sc_color.blue = color.blue;

                if (sc->sc_flags & ULED_FLAG_BLINK1) {
                        buf[0] = 0x1;
                        buf[1] = 'n';
                        buf[2] = color.red;
                        buf[3] = color.green;
                        buf[4] = color.blue;
                        buf[5] = buf[6] = buf[7] = 0;
                } else {
                        buf[0] = color.red;
                        buf[1] = color.green;
                        buf[2] = color.blue;
                        buf[3] = buf[4] = buf[5] = 0;
                        buf[6] = 0x1a;
                        buf[7] = 0x05;
                }
                error = uled_ctrl_msg(sc, UT_WRITE_CLASS_INTERFACE,
                    UR_SET_REPORT, 0x200, 0, buf, sizeof(buf));
                break;
        default:
                error = ENOTTY;
                break;
        }

        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

### poll/kqfilter: Notificaciones de disponibilidad

Poll y kqfilter ofrecen soporte para **I/O orientada a eventos**, lo que permite a los programas esperar de forma eficiente a que tu dispositivo esté listo para leer o escribir.

**Cuándo los necesitas**:

- Tu dispositivo puede no estar listo de inmediato (buffer de hardware vacío o lleno)
- Quieres dar soporte a las llamadas al sistema `select()`, `poll()` o `kqueue()`
- La I/O no bloqueante tiene sentido para tu dispositivo

**La firma de d_poll**:
```c
typedef int d_poll_t(struct cdev *dev, int events, struct thread *td);
```

**Implementación básica**:
```c
static int
mydriver_poll(struct cdev *dev, int events, struct thread *td)
{
    struct mydriver_softc *sc = dev->si_drv1;
    int revents = 0;
    
    if (events & (POLLIN | POLLRDNORM)) {
        /* Check if data available for reading */
        if (sc->rx_ready)
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->rsel);  /* Register for notification */
    }
    
    if (events & (POLLOUT | POLLWRNORM)) {
        /* Check if ready for writing */
        if (sc->tx_ready)
            revents |= events & (POLLOUT | POLLWRNORM);
        else
            selrecord(td, &sc->wsel);
    }
    
    return (revents);
}
```

**Cuando el hardware esté listo**, despierta a los procesos que esperan:
```c
/* In your interrupt handler or completion routine: */
selwakeup(&sc->rsel);  /* Wake readers */
selwakeup(&sc->wsel);  /* Wake writers */
```

**La firma de d_kqfilter** (soporte para kqueue):
```c
typedef int d_kqfilter_t(struct cdev *dev, struct knote *kn);
```

Kqueue es más complejo. Para los principiantes, **implementar poll es suficiente**. Los detalles de Kqueue pertenecen a capítulos avanzados.

### mmap: Cuándo tiene sentido el mapeo

Mmap permite a los programas de usuario **mapear memoria de dispositivo directamente en su espacio de direcciones**. Es útil, pero avanzado.

**Cuándo dar soporte a mmap**:

- El hardware tiene una región de memoria grande (framebuffer, buffers DMA)
- El rendimiento es crítico (se evita la sobrecarga de la copia)
- El espacio de usuario necesita acceso directo a los registros de hardware (¡peligroso!)

**Cuándo NO dar soporte a mmap**:

- Preocupaciones de seguridad (exposición de memoria del kernel o del hardware)
- Complejidad de sincronización (coherencia de cache, ordenación DMA)
- Es excesivo para dispositivos sencillos

**La firma de d_mmap**:
```c
typedef int d_mmap_t(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
                     int nprot, vm_memattr_t *memattr);
```

**Implementación básica**:
```c
static int
mydriver_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
              int nprot, vm_memattr_t *memattr)
{
    struct mydriver_softc *sc = dev->si_drv1;
    
    /* Only allow mapping hardware memory region */
    if (offset >= sc->mem_size)
        return (EINVAL);
    
    *paddr = rman_get_start(sc->mem_res) + offset;
    *memattr = VM_MEMATTR_UNCACHEABLE;  /* Uncached device memory */
    
    return (0);
}
```

**Para principiantes**: Pospón la implementación de mmap hasta que realmente la necesites. La mayoría de los drivers no la necesitan.

### Punteros de retorno (si_drv1, etc.)

Has visto `dev->si_drv1` a lo largo de esta sección. Así es como **almacenas tu puntero softc** en el cdev para poder recuperarlo más adelante.

**Establecer el puntero de retorno** (en attach):
```c
sc->cdev = make_dev(&mydriver_cdevsw, unit, UID_ROOT, GID_WHEEL,
                    0600, "mydriver%d", unit);
sc->cdev->si_drv1 = sc;  /* Store our softc */
```

**Recuperarlo** (en cada punto de entrada):
```c
struct mydriver_softc *sc = dev->si_drv1;
```

**Punteros de retorno disponibles**:

- `si_drv1` - Datos primarios del driver (normalmente tu softc)
- `si_drv2` - Datos secundarios (si se necesitan)

**¿Por qué no usar simplemente device_get_softc()?**

Porque los puntos de entrada cdev reciben un `struct cdev *`, no un `device_t`. El campo `si_drv1` es el puente.

### Permisos y propiedad

Al crear nodos de dispositivo, establece los permisos adecuados para equilibrar la usabilidad y la seguridad.

**Parámetros de make_dev**:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
         int perms, const char *fmt, ...);
```

**Patrones de permisos habituales**:

**Dispositivo solo para root** (control de hardware, operaciones peligrosas):
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_WHEEL, 0600, "mydriver%d", unit);
```
Permisos: `rw-------` (propietario=root)

**Solo lectura accesible al usuario**:
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_WHEEL, 0444, "mysensor%d", unit);
```
Permisos: `r--r--r--` (todos pueden leer)

**Dispositivo accesible por grupo** (p. ej., audio):
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_OPERATOR, 0660, "myaudio%d", unit);
```
Permisos: `rw-rw----` (root y el grupo operator)

**Dispositivo público** (como `/dev/null`):
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_WHEEL, 0666, "mynull", unit);
```
Permisos: `rw-rw-rw-` (todos)

**Principio de seguridad**: Empieza con permisos restrictivos (0600) y amplíalos solo cuando sea necesario y seguro.

**Resumen**

Los puntos de entrada de los dispositivos de caracteres enrutan la I/O del espacio de usuario hacia tu driver:

- **cdevsw**: Tabla de enrutamiento que mapea las llamadas al sistema con tus funciones
- **open/close**: Inicializa y limpia el estado por sesión
- **read/write**: Transfiere datos usando uiomove() y struct uio
- **ioctl**: Comandos de configuración y control
- **poll/kqfilter**: Notificaciones de disponibilidad orientadas a eventos (avanzado)
- **mmap**: Mapeo directo de memoria (avanzado, sensible en cuanto a seguridad)
- **si_drv1**: Puntero de retorno para recuperar tu softc
- **Permisos**: Establece controles de acceso adecuados con make_dev()

**A continuación**, veremos las **superficies alternativas** para los drivers de red y almacenamiento, que presentan interfaces muy diferentes.

> **Si necesitas una pausa, este es un buen momento.** Acabas de cruzar el punto medio del capítulo. Todo lo visto hasta aquí (el panorama general, las familias de drivers, las tablas de métodos softc y kobj, el ciclo de vida de Newbus y la superficie completa de I/O de los dispositivos de caracteres) es una base suficiente para revisitar más adelante como una unidad. Las secciones que siguen cambian el enfoque: superficies alternativas para red y almacenamiento, una presentación segura de recursos y registros, creación y destrucción de nodos de dispositivo, empaquetado de módulos, registro de eventos y un recorrido guiado por drivers reales de pequeño tamaño. Si tu atención sigue fresca, continúa sin parar. Si estás cansado, cierra el libro, escribe una o dos frases en tu diario del laboratorio sobre lo que te ha quedado claro y vuelve mañana a este punto. Ninguna de las dos opciones es incorrecta.

## Superficies alternativas: red y almacenamiento (orientación rápida)

Los dispositivos de caracteres usan `/dev` y cdevsw. Pero no todos los drivers encajan en ese modelo. Los drivers de red y almacenamiento se integran con distintos subsistemas del kernel y presentan «superficies» alternativas al resto del sistema. Esta sección ofrece una **orientación rápida**, lo suficiente para reconocer estos patrones cuando los encuentres.

### Una primera mirada a ifnet

Los **drivers de red** no crean entradas en `/dev`. En su lugar, registran **interfaces de red** que aparecen en `ifconfig` y se integran con la pila de red.

**La estructura ifnet** (vista simplificada):
```c
struct ifnet {
    char      if_xname[IFNAMSIZ];  /* Interface name (e.g., "em0") */
    u_int     if_flags;             /* Flags (UP, RUNNING, etc.) */
    int       if_mtu;               /* Maximum transmission unit */
    uint64_t  if_baudrate;          /* Link speed */
    u_char    if_addr[ETHER_ADDR_LEN];  /* Hardware address */
    
    /* Driver-provided methods */
    if_init_fn_t    if_init;      /* Initialize interface */
    if_ioctl_fn_t   if_ioctl;     /* Handle ioctl commands */
    if_transmit_fn_t if_transmit; /* Transmit a packet */
    if_qflush_fn_t  if_qflush;    /* Flush transmit queue */
    /* ... many more fields ... */
};
```

**Registrar una interfaz de red** (en attach):
```c
if_t ifp;

/* Allocate interface structure */
ifp = if_alloc(IFT_ETHER);
if (ifp == NULL)
    return (ENOSPC);

/* Set driver data */
if_setsoftc(ifp, sc);
if_initname(ifp, device_get_name(dev), device_get_unit(dev));

/* Set capabilities and flags */
if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
if_setcapabilities(ifp, IFCAP_VLAN_MTU | IFCAP_HWCSUM);

/* Provide driver methods */
if_setinitfn(ifp, mydriver_init);
if_setioctlfn(ifp, mydriver_ioctl);
if_settransmitfn(ifp, mydriver_transmit);
if_setqflushfn(ifp, mydriver_qflush);

/* Attach as Ethernet interface */
ether_ifattach(ifp, sc->mac_addr);
```

**Lo que el driver debe implementar**:

**if_init** - Inicializa el hardware y activa la interfaz:
```c
static void
mydriver_init(void *arg)
{
    struct mydriver_softc *sc = arg;
    
    /* Reset hardware */
    /* Configure MAC address */
    /* Enable interrupts */
    /* Mark interface running */
    
    if_setdrvflagbits(sc->ifp, IFF_DRV_RUNNING, 0);
}
```

**if_transmit** - Transmite un paquete:
```c
static int
mydriver_transmit(if_t ifp, struct mbuf *m)
{
    struct mydriver_softc *sc = if_getsoftc(ifp);
    
    /* Queue packet for transmission */
    /* Program DMA descriptor */
    /* Notify hardware */
    
    return (0);
}
```

**if_ioctl** - Gestiona los cambios de configuración:
```c
static int
mydriver_ioctl(if_t ifp, u_long command, caddr_t data)
{
    struct mydriver_softc *sc = if_getsoftc(ifp);
    
    switch (command) {
    case SIOCSIFFLAGS:    /* Interface flags changed */
        /* Handle up/down, promisc, etc. */
        break;
    case SIOCSIFMEDIA:    /* Media selection changed */
        /* Handle speed/duplex changes */
        break;
    /* ... many more ... */
    }
    return (0);
}
```

**Recepción de paquetes** (normalmente en el manejador de interrupciones):
```c
/* In interrupt handler when packet arrives: */
struct mbuf *m;

m = mydriver_rx_packet(sc);  /* Get packet from hardware */
if (m != NULL) {
    (*ifp->if_input)(ifp, m);  /* Pass to network stack */
}
```

**Diferencia clave respecto a los dispositivos de caracteres**:

- Sin open/close/read/write
- Paquetes, no flujos de bytes
- Modelo asíncrono de transmisión/recepción
- Integración con enrutamiento, cortafuegos y protocolos

**Dónde aprender más**: El capítulo 28 cubre en profundidad la implementación de drivers de red.

### Una primera mirada a GEOM

Los **drivers de almacenamiento** se integran con la capa **GEOM (GEOmetry Management)** de FreeBSD, un marco modular para las transformaciones de almacenamiento.

**Modelo conceptual de GEOM**:

```html
File System (UFS/ZFS)
     -> 
GEOM Consumer
     -> 
GEOM Class (partition, mirror, encryption)
     -> 
GEOM Provider
     -> 
Disk Driver (CAM)
     -> 
Hardware (AHCI, NVMe)
```

**Proveedores y consumidores**:

- **Proveedor**: Suministra almacenamiento (p. ej., un disco: `ada0`)
- **Consumidor**: Usa el almacenamiento (p. ej., un sistema de archivos)
- **Clase GEOM**: Capa de transformación (particionado, RAID, cifrado)

**Crear un proveedor GEOM**:

```c
struct g_provider *pp;

pp = g_new_providerf(gp, "%s", name);
pp->mediasize = disk_size;
pp->sectorsize = 512;
g_error_provider(pp, 0);  /* Mark available */
```

**Gestión de solicitudes de I/O** (estructura bio):

```c
static void
mygeom_start(struct bio *bp)
{
    struct mygeom_softc *sc;
    
    sc = bp->bio_to->geom->softc;
    
    switch (bp->bio_cmd) {
    case BIO_READ:
        mygeom_read(sc, bp);
        break;
    case BIO_WRITE:
        mygeom_write(sc, bp);
        break;
    case BIO_DELETE:  /* TRIM command */
        mygeom_delete(sc, bp);
        break;
    default:
        g_io_deliver(bp, EOPNOTSUPP);
        return;
    }
}
```

**Completar la I/O**:

```c
bp->bio_completed = bp->bio_length;
bp->bio_resid = 0;
g_io_deliver(bp, 0);  /* Success */
```

**Diferencias clave respecto a los dispositivos de caracteres**:

- Orientado a bloques (no a flujos de bytes)
- Modelo de I/O asíncrono (solicitudes bio)
- Arquitectura en capas (pila de transformaciones)
- Integración con sistemas de archivos y la pila de almacenamiento

**Dónde aprender más**: El capítulo 27 cubre en profundidad GEOM y CAM.

### Modelos mixtos (tun como puente)

Algunos drivers exponen **tanto** un plano de control (dispositivo de caracteres) como un plano de datos (interfaz de red o almacenamiento). Este patrón de «puente» aporta flexibilidad.

**Ejemplo: dispositivo tun/tap**

El dispositivo tun (túnel de red) presenta:

1. **Dispositivo de caracteres** (`/dev/tun0`) para control e I/O de paquetes
2. **Interfaz de red** (`tun0` en ifconfig) para el enrutamiento del kernel

**Vista desde el espacio de usuario**:
```c
/* Open control interface */
int fd = open("/dev/tun0", O_RDWR);

/* Configure via ioctl */
struct tuninfo info = { ... };
ioctl(fd, TUNSIFINFO, &info);

/* Read packets from network stack */
char packet[2048];
read(fd, packet, sizeof(packet));

/* Write packets to network stack */
write(fd, packet, packet_len);
```

**Vista desde el kernel**

El driver tun:

- Crea un nodo `/dev/tunX` (cdevsw)
- Crea una interfaz de red `tunX` (ifnet)
- Enruta paquetes entre ambos

Cuando la pila de red tiene un paquete para `tun0`:

1. El paquete llega al `if_transmit` del driver tun
2. El driver lo encola
3. El `read()` del usuario sobre `/dev/tun0` lo recupera

Cuando el usuario escribe en `/dev/tun0`:

1. El driver recibe los datos en `d_write`
2. El driver lo envuelve en un mbuf
3. Llama a `(*ifp->if_input)()` para inyectarlo en la pila de red

**Por qué este patrón**

- **Plano de control**: Configuración, establecimiento y desmontaje
- **Plano de datos**: Transferencia de paquetes o bloques de alto rendimiento
- **Separación**: Límites de interfaz bien definidos

**Otros ejemplos**

- BPF (Berkeley Packet Filter): `/dev/bpf` para control, captura el tráfico de las interfaces de red
- TAP: Similar a TUN, pero opera en la capa Ethernet

### Qué viene después

Esta sección ha ofrecido una comprensión a **nivel de reconocimiento** de las superficies alternativas. La implementación completa se trata en capítulos dedicados:

**Drivers de red** - Capítulo 28

- Gestión de mbuf y colas de paquetes
- Anillos de descriptores DMA
- Moderación de interrupciones y polling similar a NAPI
- Offload de hardware (sumas de verificación, TSO, RSS)
- Gestión del estado del enlace
- Selección de medio (negociación de velocidad/dúplex)

**Drivers de almacenamiento** - Capítulo 27

- Arquitectura CAM (Common Access Method)
- Gestión de comandos SCSI/ATA
- DMA y scatter-gather para I/O de bloques
- Recuperación de errores y reintentos
- NCQ (Native Command Queuing)
- Implementación de clases GEOM

**Por ahora**: Reconoce simplemente que no todos los drivers usan cdevsw. Algunos se integran con subsistemas especializados del kernel (la pila de red, la capa de almacenamiento) y presentan interfaces específicas del dominio.

**Resumen**

**Superficies alternativas de driver**:

- **Interfaces de red (ifnet)**: Se integran con la pila de red y aparecen en ifconfig
- **Almacenamiento (GEOM)**: Orientado a bloques, transformaciones en capas, integración con sistemas de archivos
- **Modelos mixtos**: Combinan el plano de control del dispositivo de caracteres con el plano de datos de red o almacenamiento

**Conclusión clave**: La familia del driver (de caracteres, de red, de almacenamiento) determina con qué subsistema del kernel te integras. Todos siguen el mismo ciclo de vida Newbus (probe/attach/detach).

**A continuación**, veremos una vista previa de los **recursos y registros**, el vocabulario para el acceso al hardware.

## Recursos y registros: una presentación segura

Los drivers no solo gestionan estructuras de datos, también **se comunican con el hardware**. Esto implica reclamar recursos (regiones de memoria, IRQ), leer y escribir registros, configurar interrupciones y, posiblemente, usar DMA. Esta sección ofrece el vocabulario justo para reconocer estos patrones sin ahogarse en los detalles de implementación. Piénsalo como aprender a identificar las herramientas de un taller antes de aprender a usarlas.

### Reclamar recursos (bus_alloc_resource_*)

Los dispositivos de hardware usan **recursos**: regiones de I/O mapeadas en memoria, puertos de I/O, líneas IRQ y canales DMA. Antes de poder usarlos, debes **pedirle al bus que los asigne**.

**La función de asignación**:

```c
struct resource *
bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags);
```

**Tipos de recursos** (de `/usr/src/sys/amd64/include/resource.h`, `/usr/src/sys/arm64/include/resource.h`, etc.):

- `SYS_RES_MEMORY` - Región de I/O mapeada en memoria
- `SYS_RES_IOPORT` - Región de puerto de I/O (x86)
- `SYS_RES_IRQ` - Línea de interrupción
- `SYS_RES_DRQ` - Canal DMA (heredado)

**Ejemplo: asignación del PCI BAR 0 (región de memoria)**:

```c
sc->mem_rid = PCIR_BAR(0);  /* Base Address Register 0 */
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
                                      &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "Could not allocate memory resource\\n");
    return (ENXIO);
}
```

**Ejemplo: asignación de IRQ**:

```c
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
                                      &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
if (sc->irq_res == NULL) {
    device_printf(dev, "Could not allocate IRQ\\n");
    return (ENXIO);
}
```

**Liberar recursos** (en detach):

```c
if (sc->mem_res != NULL) {
    bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
    sc->mem_res = NULL;
}
```

**Lo que necesitas saber ahora**:

- Los recursos de hardware deben asignarse antes de usarse
- Libéralos siempre en detach
- La asignación puede fallar (comprueba siempre el valor de retorno)

**Detalles completos**: El capítulo 18 cubre la gestión de recursos, la configuración PCI y el mapeo de memoria.

### Comunicarse con el hardware con bus_space

Una vez que has asignado un recurso de memoria, necesitas **leer y escribir registros de hardware**. FreeBSD proporciona las funciones **bus_space** para el acceso portable a MMIO (Memory-Mapped I/O) y PIO (Port I/O).

**¿Por qué no simplemente desreferenciar punteros?**

El acceso directo a memoria como `*(uint32_t *)addr` no funciona de forma fiable porque:

- El endianness varía según la arquitectura
- Las barreras de memoria y el orden de acceso son importantes
- Algunas arquitecturas requieren instrucciones especiales

**Abstracciones de bus_space**:
```c
bus_space_tag_t    bst;   /* Bus space tag (method table) */
bus_space_handle_t bsh;   /* Bus space handle (mapped address) */
```

**Obtención de handles de bus_space a partir de un recurso**:
```c
sc->bst = rman_get_bustag(sc->mem_res);
sc->bsh = rman_get_bushandle(sc->mem_res);
```

**Lectura de registros**:
```c
uint32_t value;

value = bus_space_read_4(sc->bst, sc->bsh, offset);
/* _4 means 4 bytes (32 bits), offset is byte offset into region */
```

**Escritura de registros**:
```c
bus_space_write_4(sc->bst, sc->bsh, offset, value);
```

**Variantes de anchura comunes**:

- `bus_space_read_1` / `bus_space_write_1` - 8 bits (byte)
- `bus_space_read_2` / `bus_space_write_2` - 16 bits (word)
- `bus_space_read_4` / `bus_space_write_4` - 32 bits (dword)
- `bus_space_read_8` / `bus_space_write_8` - 64 bits (qword)

**Ejemplo: lectura del registro de estado del hardware**:
```c
#define MY_STATUS_REG  0x00
#define MY_CONTROL_REG 0x04

/* Read status */
uint32_t status = bus_space_read_4(sc->bst, sc->bsh, MY_STATUS_REG);

/* Check a flag */
if (status & STATUS_READY) {
    /* Hardware is ready */
}

/* Write control register */
bus_space_write_4(sc->bst, sc->bsh, MY_CONTROL_REG, CTRL_START);
```

**Lo que necesitas saber ahora**:

- Usa bus_space_read/write para acceder al hardware
- Nunca desreferencies direcciones de hardware directamente
- Los offsets están en bytes

**Detalles completos**: El capítulo 16 cubre los patrones de bus_space, las barreras de memoria y las estrategias de acceso a registros.

### Las interrupciones en dos frases

Cuando el hardware necesita atención (llegó un paquete, una transferencia se completó, ocurrió un error), genera una **interrupción**. Tu driver registra un **manejador de interrupciones** (interrupt handler) que el kernel invoca de forma asíncrona cuando la interrupción se dispara.

**Configurar un manejador de interrupciones** (detalles de implementación en el Capítulo 19):

```c
// Placeholder - full interrupt programming covered in Chapter 19
error = bus_setup_intr(dev, sc->irq_res,
                       INTR_TYPE_NET | INTR_MPSAFE,
                       NULL, mydriver_intr, sc, &sc->irq_hand);
```

**Tu manejador de interrupciones**:

```c
static void
mydriver_intr(void *arg)
{
    struct mydriver_softc *sc = arg;
    
    /* Read interrupt status */
    /* Handle the event */
    /* Acknowledge interrupt to hardware */
}
```

**Regla de oro**: Mantén los manejadores de interrupciones **cortos y rápidos**. Delega el trabajo pesado a un taskqueue o a un thread.

**Lo que necesitas saber ahora**:

- Las interrupciones son notificaciones asíncronas del hardware
- Registras una función manejadora
- El manejador se ejecuta en contexto de interrupción (con limitaciones sobre lo que puedes hacer)

**Detalles completos**: El Capítulo 19 cubre el manejo de interrupciones, los manejadores de tipo filter frente a los de tipo thread, la moderación de interrupciones y los taskqueues.

### DMA en dos frases

Para transferencias de datos de alto rendimiento, el hardware utiliza **DMA (Direct Memory Access)** para mover datos entre la memoria y el dispositivo sin intervención del CPU. FreeBSD proporciona **bus_dma** para una configuración de DMA segura y portable, incluyendo bounce buffers para arquitecturas con IOMMU o limitaciones de DMA.

**Patrón típico de DMA**:

1. Asigna memoria apta para DMA con `bus_dmamem_alloc`
2. Carga las direcciones del buffer en los descriptores del hardware
3. Indica al hardware que inicie el DMA
4. El hardware genera una interrupción cuando termina
5. Descarga y libera los recursos cuando el driver se desasocia

**Lo que necesitas saber ahora**:

- DMA = transferencia de datos sin copia (zero-copy)
- Requiere asignación de memoria especial
- Depende de la arquitectura (bus_dma gestiona la portabilidad)

**Detalles completos**: El Capítulo 21 cubre la arquitectura DMA, los anillos de descriptores, scatter-gather, la sincronización y los bounce buffers.

### Nota sobre la concurrencia

El kernel es **multithreaded** y **preemptible**. Tu driver puede ser invocado simultáneamente desde:

- Múltiples procesos de usuario (distintos threads que abren tu dispositivo)
- Contexto de interrupción (eventos del hardware)
- Threads del sistema (taskqueues, temporizadores)

**Esto significa que necesitas locks** para proteger el estado compartido:

```c
/* In softc: */
struct mtx mtx;

/* In attach: */
mtx_init(&sc->mtx, "mydriver", NULL, MTX_DEF);

/* In your functions: */
mtx_lock(&sc->mtx);
/* ... access shared state ... */
mtx_unlock(&sc->mtx);

/* In detach: */
mtx_destroy(&sc->mtx);
```

**Lo que necesitas saber ahora**:

- Los datos compartidos necesitan protección
- Usa mutexes (MTX_DEF en la mayoría de los casos)
- Adquiere el lock, realiza el trabajo, libera el lock
- Los manejadores de interrupciones pueden necesitar tipos especiales de lock

**Detalles completos**: El Capítulo 11 cubre las estrategias de locking, los tipos de lock (mutex, sx, rm), el orden de adquisición, la prevención de deadlocks y los algoritmos sin lock.

**Resumen**

Esta sección presentó el vocabulario para el acceso al hardware:

- **Recursos**: Asigna con `bus_alloc_resource_any()`, libera en detach
- **Registros**: Accede con `bus_space_read/write_N()`, nunca mediante punteros directos
- **Interrupciones**: Registra manejadores con `bus_setup_intr()`, mantenlos cortos
- **DMA**: Usa `bus_dma` para transferencias sin copia (complejo, se cubre más adelante)
- **Locking**: Protege el estado compartido con mutexes

**Recuerda**: Este capítulo trata sobre el **reconocimiento**, no el dominio. Cuando veas estos patrones en código de drivers, sabrás qué son. Los detalles de implementación llegan en capítulos dedicados.

**A continuación**, veremos cómo **crear y eliminar nodos de dispositivo** en `/dev`.

## Creación y eliminación de nodos de dispositivo

Los dispositivos de caracteres deben aparecer en `/dev` para que los programas de usuario puedan abrirlos. Esta sección te muestra la API mínima para crear y destruir nodos de dispositivo usando devfs de FreeBSD.

### make_dev/make_dev_s: creación de /dev/foo

La función principal para crear nodos de dispositivo es `make_dev()`:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
         int perms, const char *fmt, ...);
```

**Parámetros**:

- `devsw` - Tu tabla de operaciones del dispositivo de caracteres (cdevsw)
- `unit` - Número de unidad (número menor)
- `uid` - ID de usuario propietario (normalmente `UID_ROOT`)
- `gid` - ID de grupo propietario (normalmente `GID_WHEEL`)
- `perms` - Permisos (en octal, como `0600` o `0666`)
- `fmt, ...` - Nombre del dispositivo al estilo printf

**Ejemplo** (creando `/dev/mydriver0`):

```c
sc->cdev = make_dev(&mydriver_cdevsw, unit,
                    UID_ROOT, GID_WHEEL, 0600, "mydriver%d", unit);
if (sc->cdev == NULL) {
    device_printf(dev, "Failed to create device node\\n");
    return (ENOMEM);
}

/* Store softc pointer for retrieval in entry points */
sc->cdev->si_drv1 = sc;
```

**La variante más segura: make_dev_s()**

`make_dev_s()` gestiona mejor las condiciones de carrera y devuelve códigos de error:

```c
struct make_dev_args mda;
int error;

make_dev_args_init(&mda);
mda.mda_devsw = &mydriver_cdevsw;
mda.mda_uid = UID_ROOT;
mda.mda_gid = GID_WHEEL;
mda.mda_mode = 0600;
mda.mda_si_drv1 = sc;  /* Set back-pointer directly */

error = make_dev_s(&mda, &sc->cdev, "mydriver%d", unit);
if (error != 0) {
    device_printf(dev, "Failed to create device node: %d\\n", error);
    return (error);
}
```

**Cuándo crear nodos de dispositivo**: Normalmente en tu función **attach**, después de que la inicialización del hardware haya tenido éxito.

### Números menores y convenciones de nomenclatura

Los **números menores** identifican qué instancia de tu driver representa un nodo de dispositivo. El kernel los asigna automáticamente en función del parámetro `unit` que pasas a `make_dev()`.

**Convenciones de nomenclatura**:

- **Instancia única**: `mydriver` (sin número)
- **Múltiples instancias**: `mydriver0`, `mydriver1`, etc.
- **Sub-dispositivos**: `mydriver0.ctl`, `mydriver0a`, `mydriver0b`
- **Subdirectorios**: Usa `/` en el nombre: `"led/%s"` crea `/dev/led/foo`

**Ejemplos de FreeBSD**:

- `/dev/null`, `/dev/zero` - Únicos, sin número
- `/dev/cuau0`, `/dev/cuau1` - Puertos serie, numerados
- `/dev/ada0`, `/dev/ada1` - Discos, numerados
- `/dev/pts/0` - Pseudo-terminal en un subdirectorio

**Buenas prácticas**:

- Usa el número de dispositivo devuelto por `device_get_unit()` para mayor coherencia
- Sigue los patrones de nomenclatura establecidos (los usuarios los esperan)
- Usa nombres descriptivos (no solo `/dev/dev0`)

### destroy_dev: limpieza de recursos

Cuando tu driver se desasocia, debes eliminar los nodos de dispositivo para evitar entradas huérfanas en `/dev`.

**Limpieza sencilla**:

```c
if (sc->cdev != NULL) {
    destroy_dev(sc->cdev);
    sc->cdev = NULL;
}
```

**Qué hace realmente `destroy_dev()`**: Elimina el nodo de `/dev`, bloquea a los nuevos llamadores para que no puedan entrar en ninguno de tus métodos de `cdevsw` y luego **espera a que los threads que se están ejecutando dentro de tus métodos `d_open`, `d_read`, `d_write`, `d_ioctl` y similares terminen**. Puede que sigan existiendo descriptores de archivo abiertos tras su retorno, pero el kernel garantiza que ninguno de tus métodos está en ejecución ni volverá a ejecutarse jamás para ese `cdev`. Como puede bloquear la ejecución, `destroy_dev()` debe invocarse desde un contexto donde sea posible dormir y **nunca desde dentro de un manejador `d_close` ni mientras se mantiene un mutex**.

**Cuándo no puedes llamar a `destroy_dev()` directamente: destroy_dev_sched()**

Si necesitas destruir el nodo desde un contexto en el que no puedes bloquear, o desde dentro del propio método cdev, programa la destrucción en su lugar:

```c
if (sc->cdev != NULL) {
    destroy_dev_sched(sc->cdev);  /* Schedule for destruction in a safe context */
    sc->cdev = NULL;
}
```

`destroy_dev_sched()` retorna inmediatamente; el kernel llama a `destroy_dev()` en tu nombre desde un thread trabajador seguro. Para los caminos ordinarios de `DEVICE_DETACH`, la versión simple `destroy_dev()` es la opción correcta y la que usarás con más frecuencia.

**Cuándo destruir**: Siempre en tu función **detach**, antes de liberar otros recursos que los métodos cdev aún podrían estar usando.

**Patrón de ejemplo completo**:

```c
static int
mydriver_detach(device_t dev)
{
    struct mydriver_softc *sc = device_get_softc(dev);
    
    /* Destroy device node first: no new or in-flight cdev methods
     * can run after this returns. */
    if (sc->cdev != NULL) {
        destroy_dev(sc->cdev);
        sc->cdev = NULL;
    }
    
    /* Then release other resources */
    if (sc->irq_hand != NULL)
        bus_teardown_intr(dev, sc->irq_res, sc->irq_hand);
    if (sc->irq_res != NULL)
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
    if (sc->mem_res != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
    
    return (0);
}
```

### devctl/devmatch: eventos en tiempo de ejecución

FreeBSD proporciona **devctl** y **devmatch** para monitorizar eventos de dispositivo y asociar drivers al hardware.

**devctl**: sistema de notificación de eventos

Los programas pueden escuchar `/dev/devctl` para recibir eventos de dispositivo:

```bash
% sudo service devd stop
% cat /dev/devctl
!system=DEVFS subsystem=CDEV type=CREATE cdev=mydriver0
!system=DEVFS subsystem=CDEV type=DESTROY cdev=mydriver0
...
...
--- press CTRL+C to cancel / exit , remember to restart devd ---
% sudo service devd start
```

**Eventos que genera tu driver**:

- Creación de nodos de dispositivo (automáticamente cuando llamas a make_dev)
- Destrucción de nodos de dispositivo (automáticamente cuando llamas a destroy_dev)
- Attach/detach (a través de devctl_notify)

**Notificación manual** (opcional):

```c
#include <sys/devctl.h>

/* Notify that device attached */
devctl_notify("DEVICE", "ATTACH", device_get_name(dev), device_get_nameunit(dev));

/* Notify of custom event */
char buf[128];
snprintf(buf, sizeof(buf), "status=%d", sc->status);
devctl_notify("MYDRIVER", "STATUS", device_get_nameunit(dev), buf);
```

**devmatch**: carga automática de drivers

La utilidad `devmatch` examina los dispositivos sin asociar y sugiere (o carga) los drivers apropiados:

```bash
% devmatch
kldload -n if_em
kldload -n snd_hda
```

Tu driver participa automáticamente cuando usas `DRIVER_MODULE` correctamente. La base de datos de dispositivos del kernel (generada en tiempo de compilación) registra qué drivers corresponden a qué IDs de hardware.

**Resumen**

**Creación de nodos de dispositivo**:

- Usa `make_dev()` o `make_dev_s()` en attach
- Establece la propiedad y los permisos adecuadamente
- Almacena el puntero de retorno a softc en `si_drv1`

**Destrucción de nodos de dispositivo**:

- Usa `destroy_dev_sched()` en detach para mayor seguridad
- Destruye siempre antes de liberar otros recursos

**Eventos de dispositivo**:

- devctl monitoriza los eventos de creación/destrucción
- devmatch carga automáticamente los drivers para dispositivos sin asociar

**A continuación**, exploraremos el empaquetado de módulos y el ciclo de vida de carga/descarga.

## Empaquetado de módulos y ciclo de vida (carga, inicialización, descarga)

Tu driver no existe únicamente en forma de código fuente: se compila en un **módulo del kernel** (archivo `.ko`) que puede cargarse y descargarse dinámicamente. Esta sección explica qué es un módulo, cómo funciona el ciclo de vida y cómo gestionar los eventos de carga/descarga de forma correcta.

### Qué es un módulo del kernel (.ko)

Un **módulo del kernel** es código compilado y reubicable que el kernel puede cargar en tiempo de ejecución sin necesidad de reiniciar. Piensa en él como un plugin para el kernel.

**Extensión de archivo**: `.ko` (kernel object)

**Ejemplo**: `mydriver.ko`

**Qué contiene**:

- El código de tu driver (probe, attach, detach, puntos de entrada)
- Metadatos del módulo (nombre, versión, dependencias)
- Tabla de símbolos (para el enlace con los símbolos del kernel)
- Información de reubicación

**Cómo se construye**:

```bash
% cd mydriver
% make
```

El sistema de construcción de FreeBSD (`/usr/src/share/mk/bsd.kmod.mk`) compila tu código fuente y lo enlaza en un archivo `.ko`. Cuando ejecutas `make`, la copia instalada en `/usr/share/mk/bsd.kmod.mk` es la que se consulta realmente; ambos archivos se mantienen sincronizados por el proceso de construcción de FreeBSD.

**Por qué importan los módulos**:

- **Sin necesidad de reiniciar**: Carga y descarga drivers sin reiniciar
- **Kernel más pequeño**: Carga solo los drivers del hardware que tienes
- **Velocidad de desarrollo**: Prueba los cambios rápidamente
- **Modularidad**: Cada driver es independiente

**Integrado frente a módulo**: Los drivers pueden compilarse directamente en el kernel (monolítico) o como módulos. Para el desarrollo y el aprendizaje, **usa siempre módulos**.

### El manejador de eventos del módulo

Cuando se carga o descarga un módulo, el kernel llama a tu **manejador de eventos del módulo** para darte la oportunidad de inicializar o limpiar recursos.

**La firma del manejador de eventos del módulo**:

```c
typedef int (*modeventhand_t)(module_t mod, int /*modeventtype_t*/ type, void *data);
```

**Tipos de eventos**:

- `MOD_LOAD` - El módulo se está cargando
- `MOD_UNLOAD` - El módulo se está descargando
- `MOD_QUIESCE` - El kernel está comprobando si la descarga es segura
- `MOD_SHUTDOWN` - El sistema se está apagando

**Manejador de eventos del módulo típico**:

```c
static int
mydriver_modevent(module_t mod, int type, void *data)
{
    int error = 0;
    
    switch (type) {
    case MOD_LOAD:
        /* Module is being loaded */
        printf("mydriver: Module loaded\\n");
        /* Initialize global state if needed */
        break;
        
    case MOD_UNLOAD:
        /* Module is being unloaded */
        printf("mydriver: Module unloaded\\n");
        /* Clean up global state if needed */
        break;
        
    case MOD_QUIESCE:
        /* Check if it's safe to unload */
        if (driver_is_busy()) {
            error = EBUSY;
        }
        break;
        
    case MOD_SHUTDOWN:
        /* System is shutting down */
        break;
        
    default:
        error = EOPNOTSUPP;
        break;
    }
    
    return (error);
}
```

**Registro del manejador** (para pseudo-dispositivos sin Newbus):

```c
static moduledata_t mydriver_mod = {
    "mydriver",           /* Module name */
    mydriver_modevent,    /* Event handler */
    NULL                  /* Extra data */
};

DECLARE_MODULE(mydriver, mydriver_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(mydriver, 1);
```

`DECLARE_MODULE` es el nivel más bajo de estos macros y funciona para cualquier módulo del kernel. Para los pseudo-drivers de dispositivos de caracteres, el kernel también proporciona `DEV_MODULE`, una capa delgada que se expande en `DECLARE_MODULE` con el subsistema y el orden preestablecidos correctamente. Verás `DEV_MODULE(null, null_modevent, NULL);` en `/usr/src/sys/dev/null/null.c`, por ejemplo.

**Para drivers Newbus**: El macro `DRIVER_MODULE` gestiona la mayor parte de esto automáticamente. Normalmente no necesitas un manejador de eventos del módulo separado, salvo que tengas inicialización global más allá del estado por dispositivo.

**Ejemplo: pseudo-dispositivo con manejador de eventos del módulo**

De `/usr/src/sys/dev/null/null.c` (simplificado):

```c
static int
null_modevent(module_t mod __unused, int type, void *data __unused)
{
        switch(type) {
        case MOD_LOAD:
                if (bootverbose)
                        printf("null: <full device, null device, zero device>\n");
                full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
                    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
                null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
                    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
                zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
                    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
                break;

        case MOD_UNLOAD:
                destroy_dev(full_dev);
                destroy_dev(null_dev);
                destroy_dev(zero_dev);
                break;

        case MOD_SHUTDOWN:
                break;

        default:
                return (EOPNOTSUPP);
        }

        return (0);
}

...
...
    
DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

Esto crea `/dev/full`, `/dev/null` y `/dev/zero` al cargarse, y los destruye a los tres al descargarse.

### Declaración de dependencias y versiones

Si tu driver depende de otros módulos del kernel, declara esas dependencias explícitamente para que el kernel los cargue en el orden correcto.

**El macro MODULE_DEPEND**:

```c
MODULE_DEPEND(mydriver, usb, 1, 1, 1);
MODULE_DEPEND(mydriver, netgraph, 5, 7, 9);
```

**Parámetros**:

- `mydriver` - El nombre de tu módulo
- `usb` - El módulo del que dependes
- `1` - Versión mínima aceptable
- `1` - Versión preferida
- `1` - Versión máxima aceptable

**Por qué es importante**

Si intentas cargar `mydriver` sin que `usb` esté cargado, el kernel:

- Cargará `usb` automáticamente primero (si está disponible)
- Se negará a cargar `mydriver` con un error

**El macro MODULE_VERSION**:

```c
MODULE_VERSION(mydriver, 1);
```

Esto declara la versión de tu módulo. Increméntala cuando realices cambios que rompan la compatibilidad en interfaces de las que otros módulos podrían depender.

**Ejemplos de dependencias**:

```c
/* USB device driver */
MODULE_DEPEND(umass, usb, 1, 1, 1);
MODULE_DEPEND(umass, cam, 1, 1, 1);

/* Network driver using Netgraph */
MODULE_DEPEND(ng_ether, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
```

**Cuándo declarar dependencias**:

- Llamas a funciones de otro módulo
- Usas estructuras de datos definidas en otro módulo
- Tu driver no funcionará sin otro subsistema

**Dependencias comunes**:

- `usb` - Subsistema USB
- `pci` - Soporte del bus PCI
- `cam` - Subsistema de almacenamiento (CAM)
- `netgraph` - Framework de grafo de red
- `sound` - Subsistema de sonido

### Flujo y registros de kldload/kldunload

Vamos a seguir lo que ocurre cuando cargas y descargas un módulo.

**Carga de un módulo**:

```bash
% sudo kldload mydriver
```

**Flujo en el kernel**:

1. Lee `mydriver.ko` del sistema de archivos
2. Verifica el formato ELF y la firma
3. Resuelve las dependencias de símbolos
4. Enlaza el módulo en el kernel
5. Llama al manejador de eventos del módulo con `MOD_LOAD`
6. Para drivers Newbus: ejecuta probe de inmediato en busca de dispositivos coincidentes
7. Si hay dispositivos coincidentes: llama a attach para cada uno
8. El módulo está ahora activo

**Comprobar si está cargado**:

```bash
% kldstat
Id Refs Address                Size Name
 1   23 0xffffffff80200000  1c6e230 kernel
 2    1 0xffffffff81e6f000    5000 mydriver.ko
```

**Ver mensajes del kernel**:

```bash
% dmesg | tail -5
mydriver0: <My Awesome Driver> mem 0xf0000000-0xf0001fff irq 16 at device 2.0 on pci0
mydriver0: Hardware version 1.2
mydriver0: Attached successfully
```

**Descarga de un módulo**:

```bash
% sudo kldunload mydriver
```

**Flujo en el kernel**:

1. Llama al manejador de eventos del módulo con `MOD_QUIESCE` (comprobación opcional)
2. Si se devuelve EBUSY: rechaza la descarga
3. Para drivers Newbus: llama a detach para todos los dispositivos vinculados
4. Llama al manejador de eventos del módulo con `MOD_UNLOAD`
5. Desvincula el módulo del kernel
6. Libera la memoria del módulo

**Fallos comunes al descargar**:

```bash
% sudo kldunload mydriver
kldunload: can't unload file: Device busy
```

**Por qué**:

- Nodos de dispositivo aún abiertos
- Otro módulo depende de este módulo
- El driver devolvió EBUSY desde detach

**Descarga forzada** (peligrosa, solo para pruebas):

```bash
% sudo kldunload -f mydriver
```

Esto omite las comprobaciones de seguridad. ¡Úsalo solo en una VM durante las pruebas!

### Solución de problemas al cargar módulos

**Problema**: El módulo no carga

**Comprobación 1: Símbolos no resueltos**

```bash
% sudo kldload ./mydriver.ko
link_elf: symbol usb_ifconfig undefined
```
**Solución**: Añade `MODULE_DEPEND(mydriver, usb, 1, 1, 1)` y asegúrate de que el módulo USB esté cargado.

**Comprobación 2: Módulo no encontrado**

```bash
% sudo kldload mydriver
kldload: can't load mydriver: No such file or directory
```
**Solución**: Proporciona la ruta completa (`./mydriver.ko`) o copia el archivo a `/boot/modules/`.

**Comprobación 3: Permiso denegado**

```bash
% kldload mydriver.ko
kldload: Operation not permitted
```
**Solución**: Usa `sudo` o conviértete en root.

**Comprobación 4: Incompatibilidad de versión**

```bash
% sudo kldload mydriver.ko
kldload: can't load mydriver: Exec format error
```
**Solución**: El módulo fue compilado para una versión diferente de FreeBSD. Reconstruyelo contra tu kernel en ejecución.

**Comprobación 5: Símbolos duplicados**

```bash
% sudo kldload mydriver.ko
link_elf: symbol mydriver_probe defined in both mydriver.ko and olddriver.ko
```
**Solución**: Colisión de nombres. Descarga el módulo en conflicto o renombra tus funciones.

**Consejos de depuración**:

**1. Carga con salida detallada**:

```bash
% sudo kldload -v mydriver.ko
```

**2. Comprueba los metadatos del módulo**:

```bash
% kldstat -v | grep mydriver
```

**3. Examina los símbolos**:

```bash
% nm mydriver.ko | grep mydriver_probe
```

**4. Prueba en una VM**:

Prueba siempre los drivers nuevos en una VM, nunca en tu sistema principal. Los cuelgues son normales durante el desarrollo.

**5. Observa el log del kernel en tiempo real**:

```bash
% tail -f /var/log/messages
```

**Resumen**

**Módulos del kernel**:

- Archivos `.ko` que contienen el código del driver
- Se pueden cargar y descargar dinámicamente
- No requieren reinicio para las pruebas

**Manejador de eventos del módulo**:

- Gestiona los eventos MOD_LOAD y MOD_UNLOAD
- Inicializa y limpia el estado global
- Puede rechazar la descarga con EBUSY

**Dependencias**:

- Se declaran con MODULE_DEPEND
- Se versionan con MODULE_VERSION
- El kernel impone el orden de carga

**Solución de problemas**:

- Símbolos no resueltos: añade dependencias
- No se puede descargar: comprueba si hay dispositivos abiertos o dependencias
- Prueba siempre en una VM durante el desarrollo

**A continuación**, hablaremos sobre el logging, los errores y el comportamiento visible para el usuario.

## Logging, errores y comportamiento visible para el usuario

Tu driver no es solo código: es parte de la experiencia del usuario. Un logging claro, una notificación de errores coherente y unos diagnósticos útiles son lo que distingue a los drivers profesionales de los aficionados. Esta sección explica cómo ser un buen ciudadano del kernel de FreeBSD.

### Buenas prácticas de logging (`device_printf`, sugerencias de limitación de tasa)

**La regla fundamental**: registra lo suficiente para ser útil, pero no tanto como para saturar la consola o llenar los logs.

**Usa `device_printf()` para los mensajes relacionados con el dispositivo**:

```c
device_printf(dev, "Attached successfully\\n");
device_printf(dev, "Hardware error: status=0x%x\\n", status);
```

**Salida**:

```text
mydriver0: Attached successfully
mydriver0: Hardware error: status=0x42
```

**Cuándo registrar**:

**Al hacer attach**: UNA sola línea que resuma el attach satisfactorio

```c
device_printf(dev, "Attached (hw ver %d.%d)\\n", major, minor);
```

**Errores**: SIEMPRE registra los fallos con contexto

```c
if (error != 0) {
    device_printf(dev, "Could not allocate IRQ: error=%d\\n", error);
    return (error);
}
```

**Cambios de configuración**: registra los cambios de estado significativos

```c
device_printf(dev, "Link up: 1000 Mbps full-duplex\\n");
device_printf(dev, "Entering power-save mode\\n");
```

**Cuándo NO registrar**:

**Por paquete o por operación de I/O**: NUNCA registres en cada paquete o en cada lectura/escritura

```c
/* BAD: This will flood the log */
device_printf(dev, "Received packet, length=%d\\n", len);
```

**Información de depuración detallada**: no en código de producción

```c
/* BAD: Too verbose */
device_printf(dev, "Step 1\\n");
device_printf(dev, "Step 2\\n");
device_printf(dev, "Reading register 0x%x\\n", reg);
```

**Limitación de tasa para eventos repetitivos**:

Si un error puede producirse de forma repetida (timeout de hardware, desbordamiento), limita la tasa de registro:

```c
static struct timeval last_overflow_msg;

if (ppsratecheck(&last_overflow_msg, NULL, 1)) {
    /* Max once per second */
    device_printf(dev, "RX overflow (message rate-limited)\\n");
}
```

**Uso de `printf` frente a `device_printf`**:

- **`device_printf`**: para mensajes sobre un dispositivo concreto
- **`printf`**: para mensajes sobre el módulo o el subsistema

```c
/* On module load */
printf("mydriver: version 1.2 loaded\\n");

/* On device attach */
device_printf(dev, "Attached successfully\\n");
```

**Niveles de log** (para referencia futura)

El kernel de FreeBSD no dispone de niveles de log explícitos como los de syslog, pero existen convenciones establecidas:

- Errores críticos: registrar siempre
- Avisos: registrar con el prefijo "warning:"
- Información: registrar los cambios de estado principales
- Depuración: condicional en tiempo de compilación (MYDRV_DEBUG)

**Ejemplo de un driver real** (`/usr/src/sys/dev/uart/uart_core.c`):

```c
static void
uart_pps_print_mode(struct uart_softc *sc)
{

  device_printf(sc->sc_dev, "PPS capture mode: ");
  switch(sc->sc_pps_mode & UART_PPS_SIGNAL_MASK) {
  case UART_PPS_DISABLED:
    printf("disabled");
    break;
  case UART_PPS_CTS:
    printf("CTS");
    break;
  case UART_PPS_DCD:
    printf("DCD");
    break;
  default:
    printf("invalid");
    break;
  }
  if (sc->sc_pps_mode & UART_PPS_INVERT_PULSE)
    printf("-Inverted");
  if (sc->sc_pps_mode & UART_PPS_NARROW_PULSE)
    printf("-NarrowPulse");
  printf("\n");
}
```

### Códigos de retorno y convenciones

FreeBSD utiliza los códigos estándar de **errno** para notificar errores. Usarlos de forma coherente hace que tu driver sea predecible y fácil de depurar.

**Códigos errno comunes** (de `<sys/errno.h>`):

| Código | Valor | Significado | Cuándo usarlo |
|--------|-------|-------------|---------------|
| `0` | 0 | Éxito | La operación se completó correctamente |
| `ENOMEM` | 12 | Sin memoria | Falló malloc o `bus_alloc_resource` |
| `ENODEV` | 19 | Dispositivo no encontrado | Hardware no presente o sin respuesta |
| `EINVAL` | 22 | Argumento inválido | Parámetro incorrecto enviado por el usuario |
| `EIO` | 5 | Error de entrada/salida | Falló la comunicación con el hardware |
| `EBUSY` | 16 | Dispositivo ocupado | No se puede hacer detach; recurso en uso |
| `ETIMEDOUT` | 60 | Tiempo de espera agotado | El hardware no respondió |
| `ENOTTY` | 25 | No es un terminal | Comando ioctl inválido |
| `ENXIO` | 6 | Dispositivo o dirección no encontrados | El probe rechazó el dispositivo |

**En probe**:

```c
if (vendor_id == MY_VENDOR && device_id == MY_DEVICE)
    return (BUS_PROBE_DEFAULT);  /* Success, with priority */
else
    return (ENXIO);  /* Not my device */
```

**En attach**:

```c
sc->mem_res = bus_alloc_resource_any(...);
if (sc->mem_res == NULL)
    return (ENOMEM);  /* Resource allocation failed */

error = mydriver_hw_init(sc);
if (error != 0)
    return (EIO);  /* Hardware initialization failed */

return (0);  /* Success */
```

**En los puntos de entrada** (read, write, ioctl):

```c
/* Invalid parameter */
if (len > MAX_LEN)
    return (EINVAL);

/* Hardware not ready */
if (!(sc->flags & FLAG_READY))
    return (ENODEV);

/* I/O error */
if (timeout)
    return (ETIMEDOUT);

/* Success */
return (0);
```

**En ioctl**:

```c
switch (cmd) {
case MYDRV_SETSPEED:
    if (speed > MAX_SPEED)
        return (EINVAL);  /* Bad parameter */
    sc->speed = speed;
    return (0);

default:
    return (ENOTTY);  /* Unknown ioctl command */
}
```

**Resumen**:

- `0` = éxito (siempre)
- errno positivo = fallo
- Valores negativos = significados especiales en algunos contextos (como las prioridades de probe)

**El espacio de usuario ve estos códigos de la siguiente forma**:

```c
int fd = open("/dev/mydriver0", O_RDWR);
if (fd < 0) {
    perror("open");  /* Prints: "open: No such device" if ENODEV returned */
}
```

### Observabilidad ligera con sysctl

**sysctl** ofrece una forma de exponer el estado y las estadísticas del driver **sin necesidad de un depurador ni de herramientas especiales**. Es un recurso extraordinariamente valioso para la resolución de problemas y la monitorización.

**Por qué sysctl es útil**:

- Los usuarios pueden comprobar el estado del driver desde la shell
- Las herramientas de monitorización pueden leer los valores
- No es necesario abrir el dispositivo
- Sin coste de rendimiento cuando no se accede a él

**Ejemplo: exposición de estadísticas**

**En el softc**:

```c
struct mydriver_softc {
    /* ... */
    uint64_t stat_packets_rx;
    uint64_t stat_packets_tx;
    uint64_t stat_errors;
    uint32_t current_speed;
};
```

**En attach, crea los nodos sysctl**:

```c
struct sysctl_ctx_list *ctx;
struct sysctl_oid *tree;

/* Get device's sysctl context */
ctx = device_get_sysctl_ctx(dev);
tree = device_get_sysctl_tree(dev);

/* Add statistics */
SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "packets_rx", CTLFLAG_RD, &sc->stat_packets_rx, 0,
    "Packets received");

SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "packets_tx", CTLFLAG_RD, &sc->stat_packets_tx, 0,
    "Packets transmitted");

SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "errors", CTLFLAG_RD, &sc->stat_errors, 0,
    "Error count");

SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "speed", CTLFLAG_RD, &sc->current_speed, 0,
    "Current link speed (Mbps)");
```

**Acceso desde el espacio de usuario**:

```bash
% sysctl dev.mydriver.0
dev.mydriver.0.packets_rx: 1234567
dev.mydriver.0.packets_tx: 987654
dev.mydriver.0.errors: 5
dev.mydriver.0.speed: 1000
```

**sysctl de lectura y escritura** (para configuración):

```c
static int
mydriver_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
    struct mydriver_softc *sc = arg1;
    int error, value;
    
    value = sc->debug_level;
    error = sysctl_handle_int(oidp, &value, 0, req);
    if (error || !req->newptr)
        return (error);
    
    /* Validate new value */
    if (value < 0 || value > 9)
        return (EINVAL);
    
    sc->debug_level = value;
    device_printf(sc->dev, "Debug level set to %d\\n", value);
    
    return (0);
}

/* In attach: */
SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "debug", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
    mydriver_sysctl_debug, "I", "Debug level (0-9)");
```

**El usuario puede modificarlo**:

```bash
% sysctl dev.mydriver.0.debug=3
dev.mydriver.0.debug: 0 -> 3
```

**Buenas prácticas**:

- Expón contadores y estado (solo lectura)
- Usa nombres claros y descriptivos
- Añade cadenas de descripción
- Agrupa los sysctls relacionados bajo subárboles
- No expongas datos sensibles (claves, contraseñas)
- No crees sysctls para cada variable, solo para las que aporten información útil

**Limpieza**: los nodos sysctl se limpian automáticamente cuando el dispositivo hace detach (si usaste `device_get_sysctl_ctx()`).

**Resumen**

**Buenas prácticas de logging**:

- Una línea al hacer attach; registra siempre los errores
- Nunca registres por paquete ni por operación de I/O
- Limita la tasa de los mensajes repetitivos
- Usa `device_printf` para los mensajes de dispositivo

**Códigos de retorno**:

- 0 = éxito
- Códigos errno estándar (ENOMEM, EINVAL, EIO, etc.)
- Sé coherente y predecible

**Observabilidad con sysctl**:

- Expón estadísticas y estado para su monitorización
- Solo lectura para contadores, lectura y escritura para configuración
- Sin coste de rendimiento cuando no se usa
- Limpieza automática al hacer detach

**A continuación**, haremos un **recorrido de solo lectura por algunos drivers reales y pequeños** para ver estos patrones en la práctica.

## Recorrido de solo lectura por drivers reales pequeños (FreeBSD 14.3)

Ahora que entiendes la estructura de un driver a nivel conceptual, vamos a recorrer **drivers reales de FreeBSD** para ver estos patrones en la práctica. Examinaremos cuatro ejemplos pequeños y bien escritos, señalando exactamente dónde se encuentran probe, attach, los puntos de entrada y otras estructuras. Este recorrido es **de solo lectura**: implementarás el tuyo en el Capítulo 7. Por ahora, **reconoce y comprende**.

### Recorrido 1: el trío canónico de caracteres: `/dev/null`, `/dev/zero` y `/dev/full`

Abre el archivo:

```sh
% cd /usr/src/sys/dev/null
% less null.c
```

Recorreremos el código de arriba a abajo: cabeceras, variables globales, `cdevsw`, rutas de `write`/`read`/`ioctl` y el evento del módulo que crea y destruye los nodos de devfs.

#### 1) Includes y globales mínimos (crearemos nodos de devfs)

```c
32: #include <sys/cdefs.h>
33: #include <sys/param.h>
34: #include <sys/systm.h>
35: #include <sys/conf.h>
36: #include <sys/uio.h>
37: #include <sys/kernel.h>
38: #include <sys/malloc.h>
39: #include <sys/module.h>
40: #include <sys/disk.h>
41: #include <sys/bus.h>
42: #include <sys/filio.h>
43:
44: #include <machine/bus.h>
45: #include <machine/vmparam.h>
46:
47: /* For use with destroy_dev(9). */
48: static struct cdev *full_dev;
49: static struct cdev *null_dev;
50: static struct cdev *zero_dev;
51:
52: static d_write_t full_write;
53: static d_write_t null_write;
54: static d_ioctl_t null_ioctl;
55: static d_ioctl_t zero_ioctl;
56: static d_read_t zero_read;
57:
```

##### Cabeceras e identificadores globales de dispositivo

El driver null comienza con las cabeceras estándar del kernel y las declaraciones anticipadas que establecen la base para tres dispositivos de caracteres distintos pero relacionados.

##### Inclusión de cabeceras

```c
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/disk.h>
#include <sys/bus.h>
#include <sys/filio.h>

#include <machine/bus.h>
#include <machine/vmparam.h>
```

Estas cabeceras proporcionan la infraestructura del kernel necesaria para los drivers de dispositivos de caracteres:

**`<sys/cdefs.h>`** y **`<sys/param.h>`**: definiciones fundamentales del sistema, entre ellas directivas del compilador, tipos básicos y constantes globales. Todos los archivos fuente del kernel los incluyen en primer lugar.

**`<sys/systm.h>`**: funciones esenciales del kernel como `printf()`, `panic()` y `bzero()`. Es el equivalente en el kernel de `<stdio.h>` en el espacio de usuario.

**`<sys/conf.h>`**: estructuras de configuración de dispositivos de caracteres y bloques, en particular `cdevsw` (tabla de conmutación de dispositivos de caracteres) y los tipos relacionados. Esta cabecera define los tipos de puntero a función `d_open_t`, `d_read_t` y `d_write_t` que se usan a lo largo del driver.

**`<sys/uio.h>`**: operaciones de I/O de usuario. El tipo `struct uio` describe las transferencias de datos entre el kernel y el espacio de usuario, registrando la ubicación del buffer, su tamaño y la dirección de la transferencia. La función `uiomove()` declarada aquí realiza la copia efectiva de los datos.

**`<sys/kernel.h>`**: infraestructura de arranque del kernel y de módulos, incluidos los tipos de evento de módulo (`MOD_LOAD`, `MOD_UNLOAD`) y el framework `SYSINIT` para la ordenación de la inicialización.

**`<sys/malloc.h>`**: asignación dinámica de memoria en el kernel. Aunque este driver no asigna memoria dinámicamente, la cabecera se incluye por completitud.

**`<sys/module.h>`**: infraestructura de carga y descarga de módulos. Proporciona `DEV_MODULE` y macros relacionadas para registrar módulos del kernel cargables.

**`<sys/disk.h>`** y **`<sys/bus.h>`**: interfaces de los subsistemas de disco y bus. El driver null las incluye para dar soporte al ioctl de volcado del kernel (`DIOCSKERNELDUMP`).

**`<sys/filio.h>`**: comandos de control de I/O de archivos. Define los ioctls `FIONBIO` (activar I/O no bloqueante) y `FIOASYNC` (activar I/O asíncrono) que el driver debe gestionar.

**`<machine/bus.h>`** y **`<machine/vmparam.h>`**: definiciones específicas de la arquitectura. La cabecera `vmparam.h` proporciona `ZERO_REGION_SIZE` y `zero_region`, una región de memoria virtual del kernel preinicializada a ceros que `/dev/zero` utiliza para las lecturas eficientes.

##### Punteros a la estructura del dispositivo

```c
/* For use with destroy_dev(9). */
static struct cdev *full_dev;
static struct cdev *null_dev;
static struct cdev *zero_dev;
```

Estos tres punteros globales almacenan referencias a las estructuras de dispositivo de caracteres creadas durante la carga del módulo. Cada puntero representa un nodo de dispositivo en `/dev`:

**`full_dev`**: apunta a la estructura del dispositivo `/dev/full`. Este dispositivo simula un disco lleno: las lecturas tienen éxito, pero las escrituras siempre fallan con `ENOSPC` (no queda espacio en el dispositivo).

**`null_dev`**: apunta a la estructura del dispositivo `/dev/null`, el clásico "cubo de bits" que descarta todos los datos escritos y devuelve fin de archivo inmediatamente en las lecturas.

**`zero_dev`**: apunta a la estructura del dispositivo `/dev/zero`, que devuelve un flujo infinito de bytes a cero al leer y descarta las escrituras igual que `/dev/null`.

El comentario hace referencia a `destroy_dev(9)`, lo que indica que estos punteros son necesarios para la limpieza al descargar el módulo. La función `make_dev_credf()`, llamada durante `MOD_LOAD`, devuelve valores de tipo `struct cdev *` que se almacenan aquí, y `destroy_dev()`, llamada durante `MOD_UNLOAD`, usa estos punteros para eliminar los nodos de dispositivo.

El modificador de almacenamiento `static` limita estas variables a este archivo fuente: ningún otro código del kernel puede acceder a ellas directamente. Este encapsulamiento evita modificaciones externas no intencionadas.

##### Declaraciones anticipadas de funciones

```c
static d_write_t full_write;
static d_write_t null_write;
static d_ioctl_t null_ioctl;
static d_ioctl_t zero_ioctl;
static d_read_t zero_read;
```

Estas declaraciones anticipadas establecen las firmas de las funciones antes de que aparezcan las estructuras `cdevsw` que las referencian. Cada declaración utiliza un typedef de `<sys/conf.h>`:

**`d_write_t`**: firma de la operación de escritura: `int (*d_write)(struct cdev *dev, struct uio *uio, int ioflag)`

**`d_ioctl_t`**: firma de la operación ioctl: `int (*d_ioctl)(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)`

**`d_read_t`**: firma de la operación de lectura: `int (*d_read)(struct cdev *dev, struct uio *uio, int ioflag)`

Observa qué declaraciones se necesitan:

- Dos funciones de escritura (`full_write`, `null_write`), porque `/dev/full` y `/dev/null` se comportan de forma diferente al escribir
- Dos funciones ioctl (`null_ioctl`, `zero_ioctl`), porque gestionan comandos ioctl ligeramente distintos
- Una función de lectura (`zero_read`), compartida por `/dev/zero` y `/dev/full` (ambos devuelven ceros)

Notablemente ausentes: no hay declaraciones de `d_open_t` ni `d_close_t`. Estos dispositivos no necesitan manejadores de apertura o cierre, ya que no tienen estado por descriptor de archivo que inicializar o limpiar. Abrir `/dev/null` no requiere ninguna preparación; cerrarlo no requiere ninguna limpieza. Los manejadores predeterminados del kernel son suficientes.

También ausente: `/dev/null` no necesita función de lectura. El `cdevsw` de `/dev/null` utiliza `(d_read_t *)nullop`, una función proporcionada por el kernel que retorna inmediatamente con éxito y cero bytes leídos, señalando fin de archivo.

##### Sencillez de diseño

La simplicidad de esta sección de cabecera refleja la simplicidad conceptual de los dispositivos. Tres punteros a dispositivo y cinco declaraciones de función son suficientes porque estos dispositivos:

- No mantienen estado (no se necesitan estructuras de datos por dispositivo)
- Realizan operaciones triviales (las lecturas devuelven ceros, las escrituras tienen éxito o fallan de inmediato)
- No interactúan con subsistemas complejos del kernel

Esta complejidad mínima convierte a null.c en un punto de partida ideal para entender los drivers de dispositivos de caracteres: los conceptos son claros sin infraestructura excesiva.

#### 2) `cdevsw`: conectar las llamadas al sistema con las funciones de tu driver

```c
58: static struct cdevsw full_cdevsw = {
59: 	.d_version =	D_VERSION,
60: 	.d_read =	zero_read,
61: 	.d_write =	full_write,
62: 	.d_ioctl =	zero_ioctl,
63: 	.d_name =	"full",
64: };
66: static struct cdevsw null_cdevsw = {
67: 	.d_version =	D_VERSION,
68: 	.d_read =	(d_read_t *)nullop,
69: 	.d_write =	null_write,
70: 	.d_ioctl =	null_ioctl,
71: 	.d_name =	"null",
72: };
74: static struct cdevsw zero_cdevsw = {
75: 	.d_version =	D_VERSION,
76: 	.d_read =	zero_read,
77: 	.d_write =	null_write,
78: 	.d_ioctl =	zero_ioctl,
79: 	.d_name =	"zero",
80: 	.d_flags =	D_MMAP_ANON,
81: };
```

##### Tablas de despacho de dispositivos de caracteres

Las estructuras `cdevsw` (character device switch) son las tablas de despacho del kernel para las operaciones de dispositivos de caracteres. Cada estructura mapea una operación de llamada al sistema, `read(2)`, `write(2)` e `ioctl(2)`, a funciones específicas del driver. El driver null define tres estructuras `cdevsw` separadas, una por dispositivo, lo que permite compartir algunas implementaciones y diferir donde el comportamiento diverge.

##### La tabla de despacho de `/dev/full`

```c
static struct cdevsw full_cdevsw = {
    .d_version =    D_VERSION,
    .d_read =       zero_read,
    .d_write =      full_write,
    .d_ioctl =      zero_ioctl,
    .d_name =       "full",
};
```

El dispositivo `/dev/full` simula un sistema de archivos completamente lleno. Su `cdevsw` establece este comportamiento mediante asignaciones de punteros a función:

**`d_version = D_VERSION`**: Todo `cdevsw` debe especificar esta constante de versión, lo que garantiza la compatibilidad binaria entre el driver y el framework de dispositivos del kernel. El kernel comprueba este campo durante la creación del dispositivo y rechaza las versiones que no coinciden.

**`d_read = zero_read`**: Las operaciones de lectura devuelven un flujo infinito de bytes cero, idéntico al de `/dev/zero`. La misma función sirve a ambos dispositivos, ya que su comportamiento de lectura es idéntico.

**`d_write = full_write`**: Las operaciones de escritura siempre fallan con `ENOSPC` (no queda espacio en el dispositivo), simulando un disco lleno. Esta es la característica distintiva de `/dev/full`.

**`d_ioctl = zero_ioctl`**: El manejador de ioctl procesa operaciones de control como `FIONBIO` (modo no bloqueante) y `FIOASYNC` (I/O asíncrona).

**`d_name = "full"`**: La cadena con el nombre del dispositivo aparece en los mensajes del kernel e identifica al dispositivo en la contabilidad del sistema. Esta cadena determina el nombre del nodo de dispositivo que se crea en `/dev`.

Los campos no especificados (como `d_open`, `d_close` y `d_poll`) toman el valor NULL por defecto, lo que hace que el kernel utilice sus manejadores predeterminados integrados. Para dispositivos simples sin estado, estos valores predeterminados son suficientes.

##### La tabla de despacho de `/dev/null`

```c
static struct cdevsw null_cdevsw = {
    .d_version =    D_VERSION,
    .d_read =       (d_read_t *)nullop,
    .d_write =      null_write,
    .d_ioctl =      null_ioctl,
    .d_name =       "null",
};
```

El dispositivo `/dev/null` es el clásico cubo de bits de Unix que descarta las escrituras y señala inmediatamente el fin de archivo en las lecturas:

**`d_read = (d_read_t \*)nullop`**: La función `nullop` es una operación nula (no-op) proporcionada por el kernel que devuelve cero de inmediato, señalando el fin de archivo a la aplicación. Cualquier `read(2)` sobre `/dev/null` devuelve 0 bytes sin bloquearse. La conversión a `(d_read_t *)` satisface al verificador de tipos: `nullop` tiene una firma genérica que funciona para cualquier operación de dispositivo.

**`d_write = null_write`**: Las operaciones de escritura tienen éxito de inmediato, actualizando la estructura `uio` para indicar que todos los datos fueron consumidos, pero los datos se descartan. Las aplicaciones ven escrituras exitosas, aunque nada se almacena ni se transmite.

**`d_ioctl = null_ioctl`**: Un manejador de ioctl separado del de `/dev/full` y `/dev/zero`, porque `/dev/null` admite el ioctl `DIOCSKERNELDUMP` para la configuración del volcado de memoria del kernel. Este ioctl elimina todos los dispositivos de volcado del kernel, desactivando eficazmente los volcados de memoria.

##### La tabla de despacho de `/dev/zero`

```c
static struct cdevsw zero_cdevsw = {
    .d_version =    D_VERSION,
    .d_read =       zero_read,
    .d_write =      null_write,
    .d_ioctl =      zero_ioctl,
    .d_name =       "zero",
    .d_flags =      D_MMAP_ANON,
};
```

El dispositivo `/dev/zero` proporciona una fuente infinita de bytes cero y descarta las escrituras:

**`d_read = zero_read`**: Devuelve bytes cero tan rápido como la aplicación pueda leerlos. La implementación usa una región de memoria del kernel pre-inicializada a cero para mayor eficiencia, en lugar de inicializar un buffer en cada lectura.

**`d_write = null_write`**: Comparte la implementación de escritura con `/dev/null`; las escrituras se descartan, lo que permite a las aplicaciones medir el rendimiento de escritura o descartar salida no deseada.

**`d_ioctl = zero_ioctl`**: Gestiona los ioctls de terminal estándar como `FIONBIO` y `FIOASYNC`, rechazando los demás con `ENOIOCTL`.

**`d_flags = D_MMAP_ANON`**: Este indicador habilita una optimización importante para el mapeo de memoria. Cuando una aplicación llama a `mmap(2)` sobre `/dev/zero`, el kernel no mapea realmente el dispositivo; en cambio, crea memoria anónima (memoria que no está respaldada por ningún archivo ni dispositivo). Este comportamiento permite a las aplicaciones usar `/dev/zero` para la asignación portable de memoria anónima:

```c
void *mem = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, 
                 open("/dev/zero", O_RDWR), 0);
```

El indicador `D_MMAP_ANON` indica al kernel que sustituya la asignación de memoria anónima por el mapeo, proporcionando páginas inicializadas a cero sin involucrar al driver del dispositivo. Este patrón fue históricamente importante antes de que `MAP_ANON` se estandarizase, y sigue estando disponible por razones de compatibilidad.

##### Compartición y reutilización de funciones

Observa la compartición estratégica de implementaciones:

**`zero_read`**: Usada tanto por `/dev/full` como por `/dev/zero`, porque ambos dispositivos devuelven ceros cuando se leen.

**`null_write`**: Usada tanto por `/dev/null` como por `/dev/zero`, porque ambos descartan los datos escritos.

**`zero_ioctl`**: Usada tanto por `/dev/full` como por `/dev/zero`, porque ambos admiten las mismas operaciones ioctl básicas.

**`null_ioctl`**: Usada únicamente por `/dev/null`, porque es el único que admite la configuración de volcados del kernel.

**`full_write`**: Usada únicamente por `/dev/full`, porque es el único que falla las escrituras con `ENOSPC`.

Esta compartición elimina la duplicación de código al tiempo que preserva las diferencias de comportamiento. Los tres dispositivos requieren solo cinco funciones en total (dos de escritura, dos de ioctl y una de lectura) a pesar de tener tres estructuras `cdevsw` completas.

##### El `cdevsw` como contrato

Cada estructura `cdevsw` define un contrato entre el kernel y el driver. Cuando el espacio de usuario llama a `read(fd, buf, len)` sobre `/dev/zero`:

1. El kernel identifica el dispositivo asociado al descriptor de archivo
2. Busca el `cdevsw` para ese dispositivo (`zero_cdevsw`)
3. Llama al puntero a función almacenado en `d_read` (`zero_read`)
4. Devuelve el resultado al espacio de usuario

Esta indirección a través de punteros a función habilita el polimorfismo en C: la misma interfaz de llamada al sistema invoca diferentes implementaciones en función del dispositivo al que se accede. El kernel no necesita conocer los detalles de `/dev/zero`; simplemente llama a la función registrada en la tabla de despacho.

##### Almacenamiento estático y encapsulación

Las tres estructuras `cdevsw` usan la clase de almacenamiento `static`, lo que limita su visibilidad a este archivo fuente. Las estructuras se referencian por dirección durante la creación del dispositivo (`make_dev_credf(&full_cdevsw, ...)`), pero el código externo no puede modificarlas. Esta encapsulación garantiza la consistencia del comportamiento: ningún otro driver puede anular accidentalmente el comportamiento de escritura de `/dev/null`.

#### 3) Rutas de escritura: «descartar todo» frente a «no queda espacio»

```c
83: /* ARGSUSED */
84: static int
85: full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
86: {
87:
88: 	return (ENOSPC);
89: }
91: /* ARGSUSED */
92: static int
93: null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
94: {
95: 	uio->uio_resid = 0;
96:
97: 	return (0);
98: }
```

##### Implementaciones de las operaciones de escritura

Las funciones de escritura demuestran dos enfoques contrapuestos para gestionar la salida: fallo incondicional y éxito incondicional con descarte de datos. Estas implementaciones tan simples revelan patrones fundamentales en el diseño de drivers de dispositivo.

##### La escritura en `/dev/full`: simular la falta de espacio

```c
/* ARGSUSED */
static int
full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
{

    return (ENOSPC);
}
```

La función de escritura de `/dev/full` es deliberadamente trivial: devuelve inmediatamente `ENOSPC` (número de error 28, "No space left on device") sin examinar sus argumentos ni realizar ninguna operación.

**Firma de la función**: Todas las funciones de tipo `d_write_t` reciben tres parámetros:

- `struct cdev *dev`: el dispositivo sobre el que se escribe
- `struct uio *uio`: describe el buffer de escritura del usuario (ubicación, tamaño, desplazamiento)
- `int flags`: indicadores de I/O como `O_NONBLOCK` o `O_DIRECT`

**El atributo `__unused`**: Cada parámetro está marcado con `__unused`, una directiva del compilador que indica que el parámetro se ignora intencionadamente. Esto evita los avisos de "parámetro no utilizado" durante la compilación. La directiva documenta que el comportamiento de la función no depende de a qué instancia de dispositivo se accede, qué datos proporcionó el usuario ni qué indicadores se especificaron.

**El comentario `/\* ARGSUSED \*/`**: Esta directiva tradicional de lint es anterior a los atributos modernos del compilador y cumple el mismo propósito para herramientas de análisis estático más antiguas. Indica que los argumentos no se usan por diseño, no por error. El comentario y los atributos `__unused` son redundantes, pero mantienen la compatibilidad con múltiples herramientas de análisis de código.

**Valor de retorno `ENOSPC`**: Este valor errno indica al espacio de usuario que la escritura falló porque no queda espacio. Para la aplicación, `/dev/full` aparece como un dispositivo de almacenamiento completamente lleno. Este comportamiento es útil para probar cómo gestionan los programas los fallos de escritura: muchas aplicaciones no comprueban correctamente los valores de retorno de escritura, lo que conduce a pérdida silenciosa de datos cuando los discos se llenan. Las pruebas con `/dev/full` exponen estos errores.

**¿Por qué no procesar el `uio`?**: Los drivers de dispositivo normales llamarían a `uiomove()` para consumir datos del buffer del usuario y actualizar `uio->uio_resid` para reflejar los bytes escritos. El driver de `/dev/full` omite todo esto porque simula una condición de error en la que no se escribió ningún byte. Devolver un error sin tocar `uio` indica que se escribieron cero bytes y que la operación falló.

Las aplicaciones observan:

```c
ssize_t n = write(fd, buf, 100);
// n == -1, errno == ENOSPC
```

##### La escritura en `/dev/null` y `/dev/zero`: descartar datos

```c
/* ARGSUSED */
static int
null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
    uio->uio_resid = 0;

    return (0);
}
```

La función `null_write` (usada tanto por `/dev/null` como por `/dev/zero`) implementa el comportamiento clásico del cubo de bits: acepta todos los datos, lo descarta todo e informa de éxito.

**Marcar los datos como consumidos**: La única operación `uio->uio_resid = 0` es la clave del comportamiento de esta función. El campo `uio_resid` registra cuántos bytes quedan por transferir. Establecerlo a cero le indica al kernel que todos los bytes solicitados se escribieron correctamente, aunque el driver nunca accedió realmente al buffer del usuario.

**Por qué funciona esto**: La implementación de la llamada al sistema de escritura del kernel comprueba `uio_resid` para determinar cuántos bytes se escribieron. Si un driver establece `uio_resid` a cero y devuelve éxito (0), el kernel calcula:

```c
bytes_written = original_resid - current_resid
              = original_resid - 0
              = original_resid  // all bytes written
```

La llamada `write(2)` de la aplicación devuelve el número total de bytes solicitados, indicando éxito completo.

**Sin transferencia real de datos**: A diferencia de los drivers normales que llaman a `uiomove()` para copiar datos desde el espacio de usuario, `null_write` nunca accede al buffer del usuario. Los datos permanecen en el espacio de usuario, intactos y sin leer. El driver simplemente miente sobre haberlos consumido. Esto es seguro porque los datos se van a descartar de todas formas: no tiene sentido copiar datos en memoria del kernel solo para desecharlos.

**Valor de retorno cero**: Devolver 0 indica éxito. Combinado con `uio_resid = 0`, esto crea la ilusión de una operación de escritura perfectamente funcional que aceptó todos los datos.

**Por qué `uio` no está marcado con `__unused`**: La función modifica `uio->uio_resid`, por lo que el parámetro se usa activamente. Solo `dev` y `flags` se ignoran y se marcan con `__unused`.

Las aplicaciones observan:

```c
ssize_t n = write(fd, buf, 100);
// n == 100, all bytes "written"
```

##### Implicaciones de rendimiento

La optimización de `null_write` es significativa para las aplicaciones sensibles al rendimiento. Considera un programa que redirige gigabytes de salida no deseada a `/dev/null`:

```bash
% ./generate_logs > /dev/null
```

Si el driver realmente copiara datos desde el espacio de usuario (mediante `uiomove()`), estaría desperdiciando ciclos de CPU y ancho de banda de memoria copiando datos que se descartan de inmediato. Al establecer `uio_resid = 0` sin tocar el buffer, el driver elimina completamente esta sobrecarga. La aplicación llena su buffer en espacio de usuario, llama a `write(2)`, el kernel devuelve éxito de inmediato y la CPU nunca accede al contenido del buffer.

##### Contraste en la filosofía de gestión de errores

Estas dos funciones encarnan diferentes filosofías de diseño:

**`full_write`**: Simular una condición de error con fines de prueba. Error real, rechazo inmediato.

**`null_write`**: Maximizar el rendimiento no haciendo nada. Éxito falso, retorno inmediato.

Ambas son implementaciones correctas de la semántica de sus respectivos dispositivos. La simplicidad de estas funciones (cinco líneas en total) demuestra que los drivers de dispositivo no necesitan ser complejos para ser útiles. A veces la mejor implementación es la que realiza el mínimo trabajo necesario para satisfacer el contrato de la interfaz.

##### Cumplimiento del contrato de interfaz

Ambas funciones satisfacen el contrato `d_write_t`:

- Aceptan un puntero al dispositivo, un descriptor uio y unos flags
- Devuelven 0 si tienen éxito, o errno si falla
- Actualizan `uio_resid` para reflejar los bytes consumidos (o lo dejan sin cambios si no se consumió ninguno)

Los punteros de función de `cdevsw` hacen cumplir este contrato en tiempo de compilación. Cualquier función cuya firma no coincida con `d_write_t` provocaría un error de compilación al asignarla a `d_write` en la estructura `cdevsw`. Esta seguridad de tipos garantiza que todas las implementaciones de escritura sigan la misma convención de llamada, lo que permite al kernel invocarlas de manera uniforme.

#### 4) IOCTLs: acepta un subconjunto pequeño y sensato; rechaza el resto

```c
100: /* ARGSUSED */
101: static int
102: null_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
103:     int flags __unused, struct thread *td)
104: {
105: 	struct diocskerneldump_arg kda;
106: 	int error;
107:
108: 	error = 0;
109: 	switch (cmd) {
110: 	case DIOCSKERNELDUMP:
111: 		bzero(&kda, sizeof(kda));
112: 		kda.kda_index = KDA_REMOVE_ALL;
113: 		error = dumper_remove(NULL, &kda);
114: 		break;
115: 	case FIONBIO:
116: 		break;
117: 	case FIOASYNC:
118: 		if (*(int *)data != 0)
119: 			error = EINVAL;
120: 		break;
121: 	default:
122: 		error = ENOIOCTL;
123: 	}
124: 	return (error);
125: }
127: /* ARGSUSED */
128: static int
129: zero_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
130: 	   int flags __unused, struct thread *td)
131: {
132: 	int error;
133: 	error = 0;
134:
135: 	switch (cmd) {
136: 	case FIONBIO:
137: 		break;
138: 	case FIOASYNC:
139: 		if (*(int *)data != 0)
140: 			error = EINVAL;
141: 		break;
142: 	default:
143: 		error = ENOIOCTL;
144: 	}
145: 	return (error);
146: }
```

##### Implementaciones de las operaciones ioctl

Las funciones ioctl (I/O control) gestionan operaciones de control específicas del dispositivo más allá de la lectura y escritura estándar. Mientras que read y write transfieren datos, ioctl realiza configuración, consultas de estado y operaciones especiales. El driver null implementa dos manejadores ioctl que solo se diferencian en su soporte para la configuración de volcados de memoria del kernel.

##### El manejador ioctl de `/dev/null`

```c
/* ARGSUSED */
static int
null_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
    int flags __unused, struct thread *td)
{
    struct diocskerneldump_arg kda;
    int error;

    error = 0;
    switch (cmd) {
    case DIOCSKERNELDUMP:
        bzero(&kda, sizeof(kda));
        kda.kda_index = KDA_REMOVE_ALL;
        error = dumper_remove(NULL, &kda);
        break;
    case FIONBIO:
        break;
    case FIOASYNC:
        if (*(int *)data != 0)
            error = EINVAL;
        break;
    default:
        error = ENOIOCTL;
    }
    return (error);
}
```

**Firma de la función**: El tipo `d_ioctl_t` requiere cinco parámetros:

- `struct cdev *dev`: el dispositivo que se controla
- `u_long cmd`: el número de comando ioctl
- `caddr_t data`: puntero a los datos específicos del comando (parámetro de entrada y salida)
- `int flags`: flags del descriptor de archivo procedentes del `open(2)` original
- `struct thread *td`: el thread que realiza la llamada (para comprobaciones de credenciales y entrega de señales)

La mayoría de los parámetros están marcados como `__unused` porque este dispositivo tan sencillo no necesita estado por instancia (`dev`), no examina la mayoría de los datos del comando (`data` en algunos comandos) y no comprueba los flags ni las credenciales del thread.

**Despacho de comandos mediante switch**: La función utiliza una sentencia `switch` para gestionar los distintos comandos ioctl, cada uno identificado por una constante única. El patrón `switch (cmd)` seguido de etiquetas `case` es universal en los manejadores ioctl.

##### Configuración del volcado del kernel: `DIOCSKERNELDUMP`

```c
case DIOCSKERNELDUMP:
    bzero(&kda, sizeof(kda));
    kda.kda_index = KDA_REMOVE_ALL;
    error = dumper_remove(NULL, &kda);
    break;
```

Este caso gestiona la configuración del volcado de memoria del kernel. Cuando el sistema se bloquea, el kernel escribe información de diagnóstico (contenido de la memoria, estado de los registros, trazas de la pila) en un dispositivo de volcado designado, normalmente una partición de disco o el espacio de swap. El ioctl `DIOCSKERNELDUMP` configura dicho dispositivo.

**¿Por qué `/dev/null` para los volcados del kernel?**: El uso de `ioctl(fd, DIOCSKERNELDUMP, &args)` sobre `/dev/null` tiene un propósito concreto: deshabilitar todos los volcados del kernel. Al dirigir los volcados al agujero negro, los administradores pueden impedir completamente la recopilación de volcados (algo útil en sistemas con requisitos de seguridad estrictos o cuando el espacio en disco es limitado).

**Preparación de la estructura de argumentos**: `bzero(&kda, sizeof(kda))` pone a cero la estructura `diocskerneldump_arg`, garantizando que todos los campos comiencen en un estado conocido. Es una práctica defensiva, ya que la memoria de la pila sin inicializar puede contener valores aleatorios que podrían confundir al subsistema de volcado.

**Eliminar todos los dispositivos de volcado**: `kda.kda_index = KDA_REMOVE_ALL` establece el valor de índice especial que indica "eliminar todos los dispositivos de volcado configurados, sin añadir uno nuevo." La constante `KDA_REMOVE_ALL` indica una semántica especial, distinta de la de especificar un índice de dispositivo concreto.

**Llamada al subsistema de volcado**: `dumper_remove(NULL, &kda)` invoca la función de gestión de volcados del kernel. El primer parámetro (NULL) indica que no se está eliminando ningún dispositivo específico; el campo `kda_index` proporciona la directiva. La función devuelve 0 si tiene éxito o un código de error si falla.

##### I/O no bloqueante: `FIONBIO`

```c
case FIONBIO:
    break;
```

El ioctl `FIONBIO` activa o desactiva el modo no bloqueante en el descriptor de archivo. El parámetro `data` apunta a un entero: un valor distinto de cero activa el modo no bloqueante y cero lo desactiva.

**¿Por qué no hacer nada?**: El manejador simplemente ejecuta `break` sin realizar ninguna operación. Esto es correcto porque las operaciones sobre `/dev/null` nunca se bloquean:

- Las lecturas devuelven inmediatamente fin de archivo (0 bytes)
- Las escrituras tienen éxito inmediatamente (todos los bytes se consumen)

No existe ninguna condición bajo la cual una operación sobre `/dev/null` pudiera bloquearse, de modo que el modo no bloqueante carece de sentido. El ioctl tiene éxito (devuelve 0) pero no produce ningún efecto, manteniendo la compatibilidad con las aplicaciones que configuran el modo no bloqueante sin comprobar el tipo de dispositivo.

##### I/O asíncrona: `FIOASYNC`

```c
case FIOASYNC:
    if (*(int *)data != 0)
        error = EINVAL;
    break;
```

El ioctl `FIOASYNC` activa o desactiva la notificación de I/O asíncrona. Cuando está activa, el kernel envía señales `SIGIO` al proceso cuando el dispositivo pasa a estar disponible para lectura o escritura.

**Interpretación del parámetro**: El parámetro `data` apunta a un entero. El valor cero deshabilita la I/O asíncrona; cualquier valor distinto de cero la habilita.

**Rechazo de la I/O asíncrona**: El manejador comprueba si la aplicación intenta habilitar la I/O asíncrona (`*(int *)data != 0`). En ese caso, devuelve `EINVAL` (argumento no válido), rechazando la petición.

**¿Por qué rechazar la I/O asíncrona?**: La I/O asíncrona solo tiene sentido para dispositivos que pueden bloquearse. Las aplicaciones la habilitan para recibir notificación cuando una operación previamente bloqueada puede continuar. Como `/dev/null` nunca se bloquea, la I/O asíncrona carece de sentido y podría resultar confusa. En lugar de aceptar en silencio una configuración sin lógica, el driver devuelve un error, alertando a la aplicación del fallo lógico.

**Deshabilitar la I/O asíncrona tiene éxito**: Si `*(int *)data == 0`, la condición es falsa, `error` permanece en 0 y la función devuelve éxito. Deshabilitar una funcionalidad que nunca estuvo activa es inofensivo.

##### Comandos desconocidos: caso por defecto

```c
default:
    error = ENOIOCTL;
```

Cualquier comando ioctl que no esté gestionado explícitamente llega al caso `default`, que devuelve `ENOIOCTL`. Este código de error especial significa "este ioctl no está soportado por este dispositivo." Es distinto de `EINVAL` (argumento no válido para un ioctl soportado) y de `ENOTTY` (ioctl inapropiado para el tipo de dispositivo, utilizado para operaciones de terminal en dispositivos que no son terminales).

La infraestructura ioctl del kernel puede reintentar la operación a través de otras capas cuando recibe `ENOIOCTL`, permitiendo que manejadores genéricos procesen comandos comunes.

##### El manejador ioctl de `/dev/zero`

```c
/* ARGSUSED */
static int
zero_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
       int flags __unused, struct thread *td)
{
    int error;
    error = 0;

    switch (cmd) {
    case FIONBIO:
        break;
    case FIOASYNC:
        if (*(int *)data != 0)
            error = EINVAL;
        break;
    default:
        error = ENOIOCTL;
    }
    return (error);
}
```

La función `zero_ioctl` es casi idéntica a `null_ioctl`, con una diferencia fundamental: no gestiona `DIOCSKERNELDUMP`. El dispositivo `/dev/zero` no puede actuar como dispositivo de volcado del kernel (los volcados deben almacenarse, no descartarse), por lo que el ioctl no está soportado.

El tratamiento de `FIONBIO` y `FIOASYNC` es idéntico; se trata de ioctls estándar de descriptor de archivo que todos los dispositivos de caracteres deben gestionar de forma coherente, aunque las operaciones no tengan efecto.

##### Patrones de diseño en ioctl

De estas implementaciones se desprenden varios patrones:

**Gestión explícita de operaciones sin efecto**: En lugar de devolver errores para operaciones sin sentido como `FIONBIO` sobre `/dev/null`, los manejadores responden con éxito en silencio. Esto mantiene la compatibilidad con aplicaciones que configuran descriptores de archivo incondicionalmente sin comprobar el tipo de dispositivo.

**Rechazo de configuraciones sin sentido**: La I/O asíncrona no tiene ningún propósito en estos dispositivos, de modo que los manejadores devuelven errores cuando las aplicaciones intentan habilitarla. Es una decisión de diseño; los manejadores podrían responder con éxito en silencio, pero los errores explícitos ayudan a los desarrolladores a identificar errores lógicos.

**Códigos de error estándar**: `EINVAL` para argumentos no válidos, `ENOIOCTL` para comandos no soportados. Estas convenciones permiten al espacio de usuario distinguir distintos modos de fallo.

**Validación mínima de los datos**: Los manejadores hacen cast de los punteros `data` y los desreferencian sin una validación exhaustiva. Esto es seguro porque la infraestructura ioctl del kernel ya ha verificado que el puntero es accesible desde el espacio de usuario. Los drivers de dispositivo confían en la validación de argumentos que realiza el kernel.

##### ¿Por qué dos funciones ioctl?

El dispositivo `/dev/full` utiliza `zero_ioctl` (aunque no lo veamos asignado en su `cdevsw`, examinando las estructuras que analizamos anteriormente podemos comprobarlo). Solo `/dev/null` necesita la gestión especial del dispositivo de volcado, por lo que únicamente `null_ioctl` incluye el caso `DIOCSKERNELDUMP`. Esta separación evita contaminar el `zero_ioctl` más sencillo con funcionalidad que solo necesita un dispositivo.

La estrategia de reutilización de código consiste en escribir el manejador mínimo (`zero_ioctl`) y luego extenderlo para los casos especiales (`null_ioctl`). Así cada función permanece centrada en su responsabilidad y se evita lógica condicional del estilo "si este es `/dev/null`, gestiona los volcados."

#### 5) Camino de lectura: un bucle sencillo controlado por `uio->uio_resid`

```c
148: /* ARGSUSED */
149: static int
150: zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
151: {
152: 	void *zbuf;
153: 	ssize_t len;
154: 	int error = 0;
155:
156: 	KASSERT(uio->uio_rw == UIO_READ,
157: 	    ("Can't be in %s for write", __func__));
158: 	zbuf = __DECONST(void *, zero_region);
159: 	while (uio->uio_resid > 0 && error == 0) {
160: 		len = uio->uio_resid;
161: 		if (len > ZERO_REGION_SIZE)
162: 			len = ZERO_REGION_SIZE;
163: 		error = uiomove(zbuf, len, uio);
164: 	}
165:
166: 	return (error);
167: }
```

##### Operación de lectura: un flujo infinito de ceros

La función `zero_read` proporciona un flujo interminable de bytes cero y sirve tanto a `/dev/zero` como a `/dev/full`. Esta implementación muestra una transferencia de datos eficiente mediante un buffer del kernel preasignado y la función `uiomove()` para la copia del kernel al espacio de usuario.

##### Estructura de la función y aserción de seguridad

```c
/* ARGSUSED */
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
    void *zbuf;
    ssize_t len;
    int error = 0;

    KASSERT(uio->uio_rw == UIO_READ,
        ("Can't be in %s for write", __func__));
```

**Firma de la función**: El tipo `d_read_t` requiere los mismos parámetros que `d_write_t`:

- `struct cdev *dev`: el dispositivo del que se lee (sin uso, marcado como `__unused`)
- `struct uio *uio`: describe el buffer de lectura del usuario y lleva el seguimiento del progreso de la transferencia
- `int flags`: flags de I/O (sin uso en este dispositivo tan simple)

**Variables locales**: La función necesita un estado mínimo:

- `zbuf`: puntero al origen de los bytes cero
- `len`: número de bytes a transferir en cada iteración
- `error`: lleva el seguimiento del éxito o fracaso de las operaciones de transferencia

**Comprobación de seguridad con `KASSERT`**: La aserción verifica que `uio->uio_rw` sea igual a `UIO_READ`, confirmando que se trata de una operación de lectura real. La estructura `uio` sirve tanto para operaciones de lectura como de escritura, y el campo `uio_rw` indica la dirección.

Esta aserción detecta errores de programación durante el desarrollo. Si por algún motivo una operación de escritura llamase a esta función de lectura, la aserción provocaría un panic del kernel con el mensaje "Can't be in zero_read for write." La macro del preprocesador `__func__` se expande al nombre de la función actual, haciendo que el mensaje de error sea preciso.

En los kernels de producción compilados sin depuración, `KASSERT` no genera ningún código, eliminando cualquier coste en tiempo de ejecución. Este patrón de comprobaciones defensivas durante el desarrollo con coste cero en producción es habitual en todo el kernel de FreeBSD.

##### Acceso al buffer preinicializado a cero

```c
zbuf = __DECONST(void *, zero_region);
```

La variable `zero_region` (declarada en `<machine/vmparam.h>`) apunta a una región de la memoria virtual del kernel que está permanentemente rellena de ceros. El kernel asigna esta región durante el arranque y nunca la modifica, lo que proporciona una fuente eficiente de bytes cero sin tener que poner a cero buffers temporales repetidamente.

**La macro `__DECONST`**: `zero_region` está declarada como `const` para evitar modificaciones accidentales. Sin embargo, `uiomove()` espera un puntero no const porque es una función genérica que maneja tanto operaciones de lectura (del kernel al usuario) como de escritura (del usuario al kernel). La macro `__DECONST` elimina el calificador const, diciéndole al compilador: "Sé que esto es const, pero necesito pasarlo a una función que espera un puntero no const. Te aseguro que no se modificará."

Esto es seguro porque `uiomove()` con un `uio` en dirección de lectura solo copia datos desde el buffer del kernel al espacio de usuario; nunca escribe en el buffer. El cast de const es un recurso necesario para sortear las limitaciones del sistema de tipos de C.

##### El bucle de transferencia

```c
while (uio->uio_resid > 0 && error == 0) {
    len = uio->uio_resid;
    if (len > ZERO_REGION_SIZE)
        len = ZERO_REGION_SIZE;
    error = uiomove(zbuf, len, uio);
}

return (error);
```

El bucle continúa hasta que la petición de lectura completa se haya satisfecho (`uio->uio_resid == 0`) o se produzca un error (`error != 0`).

**Comprobación de los bytes restantes**: `uio->uio_resid` lleva el seguimiento de cuántos bytes solicitó la aplicación pero aún no se han transferido. Inicialmente es igual al tamaño de lectura original. Tras cada transferencia exitosa, `uiomove()` lo decrementa.

**Limitación del tamaño de transferencia**: El código calcula cuántos bytes se deben transferir en esta iteración:

```c
len = uio->uio_resid;
if (len > ZERO_REGION_SIZE)
    len = ZERO_REGION_SIZE;
```

Si la petición restante supera el tamaño de la región de ceros, la transferencia se limita a `ZERO_REGION_SIZE`. Esta limitación existe porque el kernel solo preasignó un buffer de ceros de tamaño finito. Los valores típicos de `ZERO_REGION_SIZE` son 64 KB o 256 KB: lo suficientemente grandes para ser eficientes, pero lo suficientemente pequeños como para no desperdiciar memoria del kernel.

**Por qué es importante**: Si una aplicación lee 1 MB desde `/dev/zero`, el bucle se ejecuta varias veces, y en cada iteración se transfieren hasta `ZERO_REGION_SIZE` bytes. El mismo buffer de ceros se reutiliza en cada iteración, lo que elimina la necesidad de asignar y poner a cero 1 MB de memoria del kernel.

**Realizando la transferencia**: `uiomove(zbuf, len, uio)` es la función de trabajo del kernel para mover datos entre el kernel y el espacio de usuario. Esta función:

1. Copia `len` bytes desde `zbuf` (memoria del kernel) al buffer del usuario (descrito por `uio`)
2. Actualiza `uio->uio_resid` restando `len` (quedan menos bytes por transferir)
3. Avanza `uio->uio_offset` en `len` (la posición en el archivo avanza, aunque esto no tiene significado para `/dev/zero`)
4. Devuelve 0 en caso de éxito o un código de error en caso de fallo (típicamente `EFAULT` si la dirección del buffer del usuario no es válida)

Si `uiomove()` devuelve un error, el bucle termina de inmediato y devuelve el error al llamador. La aplicación recibe los datos que se transfirieron correctamente antes de que ocurriera el error.

**Condición de salida del bucle**: El bucle termina cuando:

- **Éxito**: `uio->uio_resid` llega a cero, lo que significa que se transfirieron todos los bytes solicitados
- **Error**: `uiomove()` falló, normalmente porque el puntero al buffer del usuario no era válido o el proceso recibió una señal

##### Semántica de flujo infinito

Observa lo que falta en esta función: ninguna comprobación de fin de archivo. La mayoría de las lecturas de archivos acaban devolviendo 0 bytes, lo que indica EOF. La función de lectura de `/dev/zero` nunca hace esto: siempre transfiere la cantidad completa solicitada (o falla con un error).

Desde la perspectiva del espacio de usuario:

```c
char buf[4096];
ssize_t n = read(zero_fd, buf, sizeof(buf));
// n always equals 4096, never 0 (unless error)
```

Esta propiedad de flujo infinito hace que `/dev/zero` sea útil para:

- Asignar memoria inicializada a cero (antes de que existiera `MAP_ANON`)
- Generar cantidades arbitrarias de bytes cero para pruebas
- Sobreescribir bloques de disco con ceros para el saneamiento de datos

##### Optimización del rendimiento

El `zero_region` preasignado es una optimización significativa. Considera la implementación alternativa:

```c
// Inefficient approach
char zeros[4096];
bzero(zeros, sizeof(zeros));
while (uio->uio_resid > 0) {
    len = min(uio->uio_resid, sizeof(zeros));
    error = uiomove(zeros, len, uio);
}
```

Este enfoque pondría a cero un buffer en cada llamada a la función, desperdiciando ciclos de CPU. La implementación de producción pone a cero el buffer una sola vez al arrancar y lo reutiliza indefinidamente, eliminando el trabajo de puesta a cero repetida.

Para aplicaciones que leen gigabytes desde `/dev/zero`, esta optimización elimina miles de millones de instrucciones de escritura en memoria, haciendo que las lecturas sean esencialmente gratuitas (limitadas únicamente por la velocidad de copia de memoria).

##### Compartido entre dispositivos

Recuerda que en las estructuras `cdevsw` tanto `/dev/zero` como `/dev/full` usan `zero_read`. Este compartir es correcto, ya que ambos dispositivos deben devolver ceros al leerlos. El parámetro `dev` (identidad del dispositivo) se ignora porque el comportamiento es idéntico independientemente de a cuál de los dos dispositivos se acceda.

Esta implementación ilustra un principio clave: cuando varios dispositivos comparten un comportamiento, impleméntalo una sola vez y referéncialo desde múltiples tablas de despacho. La reutilización del código elimina la duplicación y garantiza un comportamiento coherente entre dispositivos relacionados.

##### Propagación de errores

Si `uiomove()` falla a mitad de una lectura grande, la función devuelve el error de inmediato. La llamada al sistema `read(2)` en el espacio de usuario ve una lectura corta seguida de un error en la siguiente llamada. Por ejemplo:

```c
// Reading 128KB when process receives signal after 64KB
char buf[128 * 1024];
ssize_t n = read(zero_fd, buf, sizeof(buf));
// n might equal 65536 (successful partial read)
// errno unset (partial success)

n = read(zero_fd, buf, sizeof(buf));
// n equals -1, errno equals EINTR (interrupted system call)
```

Este manejo de errores es automático: `uiomove()` detecta las señales y devuelve `EINTR`, que la función de lectura propaga al espacio de usuario. El driver no necesita lógica explícita para gestionar señales.

#### 6) Evento de módulo: crear nodos de dispositivo al cargar y destruirlos al descargar

```c
169: /* ARGSUSED */
170: static int
171: null_modevent(module_t mod __unused, int type, void *data __unused)
172: {
173: 	switch(type) {
174: 	case MOD_LOAD:
175: 		if (bootverbose)
176: 			printf("null: <full device, null device, zero device>\n");
177: 		full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
178: 		    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
179: 		null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
180: 		    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
181: 		zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
182: 		    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
183: 		break;
184:
185: 	case MOD_UNLOAD:
186: 		destroy_dev(full_dev);
187: 		destroy_dev(null_dev);
188: 		destroy_dev(zero_dev);
189: 		break;
190:
191: 	case MOD_SHUTDOWN:
192: 		break;
193:
194: 	default:
195: 		return (EOPNOTSUPP);
196: 	}
197:
198: 	return (0);
199: }
201: DEV_MODULE(null, null_modevent, NULL);
202: MODULE_VERSION(null, 1);
```

##### Ciclo de vida del módulo y registro

La sección final del driver null se encarga de la carga, la descarga y el registro del módulo en el sistema de módulos del kernel. Este código se ejecuta cuando el módulo se carga durante el boot o mediante `kldload`, y cuando se descarga mediante `kldunload`.

##### El manejador de eventos del módulo

```c
/* ARGSUSED */
static int
null_modevent(module_t mod __unused, int type, void *data __unused)
{
    switch(type) {
```

**Firma de la función**: Los manejadores de eventos de módulo reciben tres parámetros:

- `module_t mod`: un identificador del propio módulo (no se usa aquí)
- `int type`: el tipo de evento: `MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`, etc.
- `void *data`: datos específicos del evento (no se usan en este driver)

La función devuelve 0 en caso de éxito o un valor errno en caso de fallo. Si `MOD_LOAD` falla, el módulo no llega a cargarse; si falla `MOD_UNLOAD`, el módulo permanece cargado.

##### Carga del módulo: creación de dispositivos

```c
case MOD_LOAD:
    if (bootverbose)
        printf("null: <full device, null device, zero device>\n");
    full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
        NULL, UID_ROOT, GID_WHEEL, 0666, "full");
    null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
        NULL, UID_ROOT, GID_WHEEL, 0666, "null");
    zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
        NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
    break;
```

El caso `MOD_LOAD` se ejecuta cuando el módulo se carga por primera vez, ya sea durante el boot o cuando un administrador ejecuta `kldload null`.

**Mensaje de arranque**: La comprobación `if (bootverbose)` controla si aparece algún mensaje durante el boot. La variable `bootverbose` se activa cuando el sistema arranca con la salida detallada habilitada (mediante la configuración del cargador de arranque o una opción del kernel). Cuando es verdadera, el driver imprime un mensaje informativo que identifica los dispositivos que proporciona.

Esta condición evita ensuciar la salida del boot en operación normal, al tiempo que permite a los administradores ver la inicialización del driver durante arranques de diagnóstico. El formato del mensaje sigue la convención de FreeBSD: nombre del driver, dos puntos, lista de dispositivos entre corchetes angulares.

**Creación de dispositivos con `make_dev_credf`**: Esta función crea nodos de dispositivos de caracteres en `/dev`. Cada llamada requiere varios parámetros que controlan las propiedades del dispositivo:

**`MAKEDEV_ETERNAL_KLD`**: Un flag que indica que este dispositivo debe persistir hasta que se destruya explícitamente. La parte `ETERNAL` significa que el dispositivo no se eliminará automáticamente aunque se cierren todas las referencias, y `KLD` indica que forma parte de un módulo del kernel cargable (a diferencia de un driver compilado estáticamente). Esta combinación de flags garantiza que los nodos de dispositivo permanezcan disponibles mientras el módulo esté cargado, independientemente de si algún proceso los tiene abiertos.

**`&full_cdevsw`** (y de forma análoga para null y zero): Puntero a la tabla de despacho del dispositivo de caracteres que define el comportamiento del dispositivo. Esto conecta el nodo de dispositivo con las implementaciones de las funciones del driver.

**`0`**: El número de unidad del dispositivo. Como estos son dispositivos únicos (solo existe un `/dev/null` en todo el sistema), se usa la unidad 0. Los dispositivos con múltiples instancias, como `/dev/tty0` y `/dev/tty1`, usarían números de unidad distintos.

**`NULL`**: Puntero a las credenciales para comprobaciones de permisos. NULL indica que no se requieren credenciales especiales más allá de los permisos estándar del archivo.

**`UID_ROOT`**: El propietario del archivo de dispositivo (root, UID 0). Determina quién puede cambiar los permisos del dispositivo o eliminarlo.

**`GID_WHEEL`**: El grupo del archivo de dispositivo (wheel, GID 0). El grupo wheel tiene tradicionalmente privilegios administrativos.

**`0666`**: El modo de permisos en octal. Este valor (lectura y escritura para propietario, grupo y otros) permite que cualquier proceso abra estos dispositivos. Desglosándolo:

- Propietario (root): lectura (4) + escritura (2) = 6
- Grupo (wheel): lectura (4) + escritura (2) = 6
- Otros: lectura (4) + escritura (2) = 6

A diferencia de los archivos ordinarios, donde los permisos de escritura para todos pueden ser peligrosos, estos dispositivos están diseñados para un acceso universal: cualquier proceso debe poder escribir en `/dev/null` o leer desde `/dev/zero`.

**`"full"`** (y de forma análoga "null" y "zero"): La cadena con el nombre del dispositivo. Esto crea `/dev/full`, `/dev/null` y `/dev/zero` respectivamente. La función `make_dev_credf` antepone automáticamente `/dev/` al nombre.

**Almacenamiento del valor de retorno**: Cada llamada a `make_dev_credf` devuelve un puntero `struct cdev *` que se guarda en las variables globales (`full_dev`, `null_dev`, `zero_dev`). Estos punteros son imprescindibles para que el manejador de descarga pueda eliminar los dispositivos más adelante.

##### Descarga del módulo: destrucción de dispositivos

```c
case MOD_UNLOAD:
    destroy_dev(full_dev);
    destroy_dev(null_dev);
    destroy_dev(zero_dev);
    break;
```

El caso `MOD_UNLOAD` se ejecuta cuando un administrador ejecuta `kldunload null` para eliminar el módulo del kernel. El sistema de módulos solo invoca este manejador si el módulo puede descargarse sin problemas (es decir, ningún otro código lo referencia).

**Destrucción de dispositivos**: La función `destroy_dev` elimina un nodo de dispositivo de `/dev` y libera las estructuras del kernel asociadas. Cada llamada usa el puntero guardado durante `MOD_LOAD`.

La función se encarga automáticamente de varias tareas de limpieza:

- Elimina la entrada de `/dev`, de modo que los nuevos intentos de apertura fallan con `ENOENT`
- Espera a que se cierren las aperturas existentes (o las cierra de forma forzosa)
- Libera la memoria de `struct cdev` y estructuras relacionadas
- Cancela el registro del dispositivo en la contabilidad del kernel

El orden de destrucción no importa para estos dispositivos independientes. Si existieran dependencias entre ellos (por ejemplo, uno enrutando operaciones al otro), el orden sería crítico.

**¿Qué ocurre si hay dispositivos abiertos?**: Por defecto, `destroy_dev` se bloquea hasta que se cierran todos los descriptores de archivo que apuntan al dispositivo. Un administrador que intente ejecutar `kldunload null` mientras algún proceso tiene `/dev/null` abierto experimentará un retraso. En la práctica, `/dev/null` suele estar abierto con frecuencia (muchos demonios redirigen su salida allí), por lo que descargar este módulo es algo poco habitual.

##### Apagado del sistema: sin acción

```c
case MOD_SHUTDOWN:
    break;
```

El evento `MOD_SHUTDOWN` se dispara durante el apagado o el reinicio del sistema. El manejador no hace nada porque estos dispositivos no necesitan ningún tratamiento especial durante el apagado:

- No hay hardware que desactivar o poner en un estado seguro
- No hay buffers de datos que vaciar
- No hay conexiones de red que cerrar de forma ordenada

Simplemente ejecutar `break` (y caer al `return (0)`) indica que el apagado se manejó correctamente. Los dispositivos dejarán de existir cuando el kernel se detenga; no es necesaria ninguna limpieza explícita.

##### Eventos no soportados: devolución de error

```c
default:
    return (EOPNOTSUPP);
```

El caso `default` captura cualquier tipo de evento del módulo que no se haya manejado explícitamente. Devolver `EOPNOTSUPP` (operación no soportada) informa al sistema de módulos de que este evento no es aplicable a este driver.

Otros tipos de eventos posibles incluyen `MOD_QUIESCE` (prepararse para la descarga, utilizado para verificar si la descarga es segura) y eventos personalizados específicos del driver. Este driver no los soporta, por lo que el manejador por defecto los rechaza.

**¿Por qué no entrar en pánico?**: Un tipo de evento desconocido no es un error del driver: el kernel podría introducir nuevos tipos de eventos en versiones futuras. Devolver un error es más robusto que provocar una caída del sistema.

##### Retorno de éxito

```c
return (0);
```

Después de gestionar cualquier evento soportado (carga, descarga, apagado), la función devuelve 0 para indicar éxito. Esto permite que la operación del módulo se complete con normalidad.

##### Macros de registro del módulo

```c
DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

Estas macros registran el módulo en el sistema de módulos del kernel.

**`DEV_MODULE(null, null_modevent, NULL)`**: Declara un módulo de driver de dispositivo con tres argumentos:

- `null`: el nombre del módulo, que aparece en la salida de `kldstat` y se usa con los comandos `kldload` y `kldunload`
- `null_modevent`: puntero a la función manejadora de eventos
- `NULL`: datos adicionales opcionales que se pasan al manejador de eventos (no se usan aquí)

La macro se expande para generar estructuras de datos que el enlazador del kernel y el cargador de módulos reconocen. Cuando el módulo se carga, el kernel llama a `null_modevent` con `type = MOD_LOAD`. Al descargarlo, lo llama con `type = MOD_UNLOAD`.

**`MODULE_VERSION(null, 1)`**: Declara el número de versión del módulo. Los argumentos son:

- `null`: nombre del módulo (debe coincidir con el de `DEV_MODULE`)
- `1`: número de versión (entero)

Los números de versión permiten la comprobación de dependencias. Si otro módulo dependiera de este, podría especificar "requiere null versión >= 1" para garantizar la compatibilidad. Para este driver sencillo, el versionado es principalmente documentación: indica que esta es la primera (y probablemente única) versión de la interfaz.

##### Ciclo de vida completo del módulo

El ciclo de vida completo de este driver es el siguiente:

**Al arrancar o ejecutar `kldload null`**:

1. El kernel carga el módulo en memoria
2. Procesa el registro de `DEV_MODULE`
3. Llama a `null_modevent(mod, MOD_LOAD, NULL)`
4. El manejador crea `/dev/full`, `/dev/null` y `/dev/zero`
5. Los dispositivos quedan disponibles para el espacio de usuario

**Durante la operación normal**:

- Las aplicaciones abren, leen, escriben y ejecutan ioctl sobre los dispositivos
- Los punteros a funciones de `cdevsw` enrutan las operaciones al código del driver
- No se producen eventos del módulo durante la operación normal

**Al ejecutar `kldunload null`**:

1. El kernel comprueba si la descarga es segura (sin dependencias)
2. Llama a `null_modevent(mod, MOD_UNLOAD, NULL)`
3. El manejador destruye los tres dispositivos
4. El kernel elimina el módulo de la memoria
5. Los intentos de abrir `/dev/null` fallan ahora con `ENOENT`

**Durante el apagado del sistema**:

1. El kernel llama a `null_modevent(mod, MOD_SHUTDOWN, NULL)`
2. El handler no hace nada (devuelve éxito)
3. El sistema continúa la secuencia de apagado
4. El módulo deja de existir cuando el kernel se detiene

Esta gestión del ciclo de vida, con handlers explícitos de carga y descarga y macros de registro, es el patrón estándar de todos los módulos del kernel de FreeBSD. Los drivers de dispositivo, las implementaciones de sistemas de archivos, los protocolos de red y las ampliaciones de llamadas al sistema utilizan el mismo mecanismo de eventos de módulo.

#### Ejercicios interactivos: `/dev/null`, `/dev/zero` y `/dev/full`

**Objetivo:** Confirmar que eres capaz de leer un driver real, relacionar el comportamiento visible desde el espacio de usuario con el código del kernel y explicar el esqueleto mínimo de un dispositivo de caracteres.

##### A) Relación de llamadas al sistema con `cdevsw` (calentamiento)

1. ¿Qué función gestiona las escrituras en `/dev/full` y qué valor errno devuelve? Cita el nombre de la función y la sentencia return. ¿Qué significa este código de error para las aplicaciones del espacio de usuario? *Pista:* consulta `full_write`.

2. ¿Qué función gestiona las lecturas tanto de `/dev/zero` como de `/dev/full`? Cita las asignaciones `.d_read` correspondientes de ambas estructuras `cdevsw`. ¿Por qué es correcto que ambos dispositivos compartan el mismo handler de lectura, y qué comportamiento tienen en común? *Pista:* compara las estructuras `full_cdevsw` y `zero_cdevsw` y lee `zero_read`.

3. Crea una tabla que enumere el nombre de cada `cdevsw` y sus asignaciones de funciones de lectura y escritura:

| cdevsw             | .d_name | .d_read | .d_write |
| :---------------- | :------: | :----: | :----: | 
| full_cdevsw | ? | ? | ? |
| null_cdevsw | ? | ? | ? |
| zero_cdevsw | ? | ? | ? |

	Cita cada estructura. *Pista:* busca las tres definiciones `*_cdevsw` al principio del archivo.

##### B) Razonamiento sobre la ruta de lectura con `uiomove()`

1. Localiza el `KASSERT` que verifica que se trata de una operación de lectura. Cita la línea y explica qué ocurriría si esa aserción fallara. ¿Qué aporta la macro `__func__` al mensaje de error? *Pista:* consulta el principio de `zero_read`.

2. Explica el papel de `uio->uio_resid` en la condición del bucle while. ¿Qué representa este campo y cómo cambia durante el bucle? Cita la condición del while. *Pista:* dentro de `zero_read`.

3. ¿Por qué el código limita cada transferencia a `ZERO_REGION_SIZE` en lugar de copiar todos los bytes solicitados de una sola vez? ¿Qué problema supondría transferir 1 MB en una sola llamada a `uiomove()`? Cita la sentencia if que implementa este límite. *Pista:* la limitación es lo primero dentro del cuerpo del bucle de `zero_read`.

4. El código hace referencia a dos recursos del kernel preasignados: `zero_region` (un puntero) y `ZERO_REGION_SIZE` (una constante). Cita las líneas en que se usa cada uno. A continuación, utiliza grep para encontrar dónde se define `ZERO_REGION_SIZE`:

```bash
% grep -r "define.*ZERO_REGION_SIZE" /usr/src/sys/amd64/include/
```

	¿Cuál es el valor en tu sistema? *Pista:* `zero_region` se usa dentro de `zero_read`, y `ZERO_REGION_SIZE` es su límite de tamaño.

##### C) Contraste en la ruta de escritura

1. Compara las implementaciones de `null_write` y `full_write`. Para cada función, responde:

- ¿Qué hace con `uio->uio_resid`?
- ¿Qué valor devuelve?
- ¿Qué devolverá una llamada a `write(2)` en el espacio de usuario?

	Ahora verifica desde el espacio de usuario:

```bash
# This should succeed, reporting bytes written:
% dd if=/dev/zero of=/dev/null bs=64k count=8 2>&1 | grep copied

# This should fail with "No space left on device":
% dd if=/dev/zero of=/dev/full bs=1k count=1 2>&1 | grep -i "space"
```

	Para cada prueba, identifica qué handler de escritura se invocó y cita la línea concreta que provocó el comportamiento observado.

##### D) Forma mínima de `ioctl`

1. Crea una tabla comparativa del manejo de ioctl. Para `null_ioctl` y `zero_ioctl`, rellena:

```text
Commandnull_ioctl behaviorzero_ioctl behavior
DIOCSKERNELDUMP??
FIONBIO??
FIOASYNC??
Unknown command??
```

	Para cada entrada, cita la sentencia case correspondiente y explica el comportamiento.

2. El caso `FIOASYNC` tiene un manejo especial al activar el I/O asíncrono. Cita la comprobación condicional y explica por qué estos dispositivos rechazan el modo de I/O asíncrono. *Pista:* consulta el caso `FIOASYNC` tanto en `null_ioctl` como en `zero_ioctl`.

##### E) Ciclo de vida del nodo de dispositivo

1. Durante `MOD_LOAD`, se crean tres nodos de dispositivo mediante `make_dev_credf()`. Para cada llamada (en la rama `MOD_LOAD` de `null_modevent`), identifica:

- El nombre del dispositivo (lo que aparece en /dev/)
- El puntero cdevsw (qué tabla de funciones)
- El modo de permisos (¿qué significa 0666?)
- El propietario y el grupo (UID_ROOT, GID_WHEEL)

	Cita una llamada completa a `make_dev_credf()` e identifica cada parámetro.

2. Durante `MOD_UNLOAD`, se llama a `destroy_dev()` tres veces (en la rama `MOD_UNLOAD` de `null_modevent`). Cita estas llamadas y explica:

- ¿Por qué necesitamos los punteros globales (`full_dev`, `null_dev`, `zero_dev`)?
- ¿Qué ocurriría si olvidáramos llamar a `destroy_dev()` durante la descarga?
- ¿Por qué deben ser simétricas las operaciones `MOD_LOAD` y `MOD_UNLOAD`?

##### F) Rastreo desde el espacio de usuario

1. Verifica que `/dev/zero` produce ceros y que `/dev/null` consume datos:

```bash
% dd if=/dev/zero bs=1k count=1 2>/dev/null | hexdump -C | head -n 2
# Expected: all zeros (00 00 00 00...)

% printf 'test data' | dd of=/dev/null 2>/dev/null ; echo "Exit code: $?"
# Expected: Exit code: 0
```

	Explica estos resultados trazando el flujo a través de:

- `zero_read`: ¿Qué líneas producen los ceros? ¿Cómo funciona el bucle?
- `null_write`: ¿Qué línea hace que la escritura «tenga éxito»? ¿Qué ocurre con los datos?

	Cita las líneas concretas responsables de cada comportamiento.

2. Lee desde `/dev/full` y examina lo que obtienes:

```bash
% dd if=/dev/full bs=16 count=1 2>/dev/null | hexdump -C
```

	¿Qué salida observas? Consulta la estructura `full_cdevsw`: ¿qué función `.d_read` utiliza?

	¿Por qué `/dev/full` devuelve ceros en lugar de un error?

##### G) Ciclo de vida del módulo

1. Observa la sentencia switch de `null_modevent`. Enumera todas las etiquetas case y lo que hace cada una. ¿Qué casos realizan trabajo real frente a los que simplemente devuelven éxito?

2. Localiza las dos macros al final del archivo que registran este módulo. Cítalas y explica:

- ¿Qué hace `DEV_MODULE`?
- ¿Qué hace `MODULE_VERSION`?
- ¿Por qué ambas usan el nombre «null»?

3. El flag `MAKEDEV_ETERNAL_KLD` se usa en las tres llamadas a `make_dev_credf()`. ¿Qué significa este flag y por qué es apropiado para estos dispositivos? *Pista:* consulta las llamadas a `make_dev_credf()` dentro de `null_modevent` y piensa qué ocurre si un proceso tiene /dev/null abierto cuando intentas descargar el módulo.

#### Ampliación (experimento mental)

**Ampliación 1:** Examina `null_write`. La función hace dos cosas: establece `uio->uio_resid = 0` y devuelve 0.

Experimento mental: si cambiáramos el `return (0);` por `return (EIO);` pero mantuviéramos la asignación `uio->uio_resid = 0;` sin cambios, ¿qué ocurriría?

- ¿Qué pensaría el kernel sobre los bytes escritos?
- ¿Qué devolvería `write(2)` al espacio de usuario?
- ¿A qué se establecería errno?

	Cita las líneas implicadas y explica la interacción entre `uio_resid` y el valor de retorno.

**Ampliación 2:** En `zero_read`, el código limita cada transferencia a `ZERO_REGION_SIZE`. Cita la sentencia if donde se aplica este límite.

	Experimento mental: supón que eliminamos esta comprobación y hacemos siempre:

```c
len = uio->uio_resid;  // No limit!
error = uiomove(zbuf, len, uio);
```

Si un usuario solicita 10 MB a `/dev/zero`:

- ¿Qué invariante haría que esto «funcionara» (sin fallar)?
- ¿Qué restricción de recursos estaríamos ignorando?
- ¿Por qué el código actual utiliza un buffer preasignado de tamaño limitado?

**Pista:** `zero_region` solo tiene `ZERO_REGION_SIZE` bytes. ¿Qué ocurre si intentamos copiar más bytes de los que caben en este buffer de tamaño fijo?

#### Puente hacia la siguiente visita

Antes de continuar: si eres capaz de relacionar cada comportamiento visible desde el espacio de usuario con la función correcta en `null.c`, has interiorizado el **esqueleto de dispositivo de caracteres** que seguiremos encontrando. A continuación, examinaremos **`led(4)`**, que sigue siendo pequeño pero añade una **superficie de control** visible desde el espacio de usuario (escrituras que cambian el estado). Sigue prestando atención a tres aspectos: **cómo se crea el nodo de dispositivo**, **cómo se enrutan las operaciones** y **cómo el driver rechaza de forma limpia las acciones no soportadas**.

### Tour 2: una pequeña interfaz de control de solo escritura con temporizadores: `led(4)`

Abre el archivo:

```sh
% cd /usr/src/sys/dev/led
% less led.c
```

En un solo archivo encontramos un patrón práctico de **control de dispositivo orientado a escritura** respaldado por un **temporizador** y estado por dispositivo. Verás: un softc por LED, una gestión global de recursos, un **callout** periódico que avanza los patrones de parpadeo, un analizador que convierte comandos legibles por personas en secuencias compactas, un punto de entrada `write(2)`, y ayudantes mínimos de creación y destrucción.

#### 1.0) Includes

```c
12: #include <sys/cdefs.h>
13: #include <sys/param.h>
14: #include <sys/conf.h>
15: #include <sys/ctype.h>
16: #include <sys/kernel.h>
17: #include <sys/limits.h>
18: #include <sys/lock.h>
19: #include <sys/malloc.h>
20: #include <sys/mutex.h>
21: #include <sys/queue.h>
22: #include <sys/sbuf.h>
23: #include <sys/sx.h>
24: #include <sys/systm.h>
25: #include <sys/uio.h>
27: #include <dev/led/led.h>
```

##### Cabeceras e interfaz del subsistema

El driver LED comienza con las cabeceras del kernel y una cabecera del subsistema que establece su papel como componente de infraestructura utilizado por otros drivers. A diferencia del driver null, que funciona de forma autónoma, el driver LED ofrece servicios a los drivers de hardware que necesitan exponer indicadores de estado.

##### Cabeceras estándar del kernel

```c
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/uio.h>
```

Estas cabeceras proporcionan la infraestructura para un driver de dispositivo con estado, impulsado por temporizadores:

**`<sys/cdefs.h>`**, **`<sys/param.h>`**, **`<sys/systm.h>`**: Definiciones fundamentales del sistema idénticas a las de `null.c`. Todo archivo de código fuente del kernel comienza con estas.

**`<sys/conf.h>`**: Configuración de dispositivos de caracteres, que proporciona `cdevsw` y `make_dev()`. El driver LED las usa para crear nodos de dispositivo de forma dinámica a medida que los drivers de hardware registran LEDs.

**`<sys/ctype.h>`**: Funciones de clasificación de caracteres como `isdigit()`. El driver LED analiza las cadenas proporcionadas por el usuario para controlar los patrones de parpadeo, lo que requiere comprobación del tipo de carácter.

**`<sys/kernel.h>`**: Infraestructura de inicialización del kernel. Este driver usa `SYSINIT` para realizar una inicialización única durante el boot, configurando recursos globales antes de que se registre ningún LED.

**`<sys/limits.h>`**: Límites del sistema como `INT_MAX`. El driver LED lo usa para configurar su asignador de números de unidad con el rango máximo.

**`<sys/lock.h>`** y **`<sys/mutex.h>`**: Primitivas de lock para proteger estructuras de datos compartidas. El driver usa un mutex para proteger la lista de LEDs y el estado del parpadeo frente al acceso concurrente de callbacks de temporizadores y escrituras del usuario.

**`<sys/queue.h>`**: Macros de lista enlazada de BSD (`LIST_HEAD`, `LIST_FOREACH`, `LIST_INSERT_HEAD`, `LIST_REMOVE`). El driver mantiene una lista global de todos los LEDs registrados, lo que permite que los callbacks de temporizadores los recorran y actualicen.

**`<sys/sbuf.h>`**: Manipulación segura de buffers de cadenas. El driver usa `sbuf` para construir cadenas de patrones de parpadeo a partir de la entrada del usuario, evitando desbordamientos de buffer de tamaño fijo. Los buffers de cadenas crecen automáticamente según sea necesario y ofrecen comprobación de límites.

**`<sys/sx.h>`**: Locks compartidos/exclusivos (locks de lector/escritor). El driver usa un lock sx para proteger la creación y destrucción de dispositivos, lo que permite lecturas concurrentes de la lista de LEDs mientras serializa las modificaciones estructurales.

**`<sys/uio.h>`**: Operaciones de I/O del usuario. Al igual que `null.c`, este driver necesita `struct uio` y `uiomove()` para transferir datos entre el kernel y el espacio de usuario.

**`<sys/malloc.h>`**: Asignación de memoria del kernel. A diferencia de `null.c`, que no tenía memoria dinámica, el driver LED asigna estructuras de estado por LED y duplica cadenas para nombres de LED y patrones de parpadeo.

##### Cabecera de la interfaz del subsistema

```c
#include <dev/led/led.h>
```

Esta cabecera define la API pública del subsistema LED, la interfaz que otros drivers del kernel usan para registrar y controlar LEDs. Aunque el contenido específico no se muestra en este archivo de código fuente, las declaraciones típicas incluirían:

**`led_t` typedef**: Un tipo de puntero a función para los callbacks de control de LED. Los drivers de hardware proporcionan una función con esta firma que enciende o apaga su LED físico:

```c
typedef void led_t(void *priv, int onoff);
```

**Funciones públicas**: La API a la que llaman los drivers de hardware:

- `led_create()`: registra un nuevo LED y crea un nodo de dispositivo `/dev/led/name`.
- `led_create_state()`: registra un LED con un estado inicial.
- `led_destroy()`: cancela el registro de un LED cuando se elimina el hardware.
- `led_set()`: controla un LED de forma programática desde el código del kernel.

**Ejemplo de uso por parte de un driver de hardware**:

```c
// In a disk driver's attach function:
struct cdev *led_dev;
led_dev = led_create(disk_led_control, sc, "disk0");

// Later, in the LED control callback:
static void
disk_led_control(void *priv, int onoff)
{
    struct disk_softc *sc = priv;
    if (onoff)
        /* Turn on LED via hardware register write */
    else
        /* Turn off LED via hardware register write */
}
```

##### Rol arquitectónico

La organización de las cabeceras revela la naturaleza dual del driver LED:

**Como driver de dispositivo de caracteres**: Incluye las cabeceras estándar del driver de dispositivo (`<sys/conf.h>`, `<sys/uio.h>`) para crear nodos `/dev/led/*` a los que el espacio de usuario puede escribir.

**Como subsistema**: Incluye `<dev/led/led.h>` para exportar una API que otros drivers consumen. Los drivers de hardware no manipulan `/dev/led/*` directamente; llaman a `led_create()` y proporcionan callbacks.

Este patrón, un driver que tanto expone dispositivos orientados al usuario como proporciona APIs orientadas al kernel, aparece en toda la base de FreeBSD. Entre los ejemplos se incluyen:

- El driver `devctl`: crea `/dev/devctl` al tiempo que proporciona `devctl_notify()` para el reporte de eventos del kernel.
- El driver `random`: crea `/dev/random` al tiempo que proporciona `read_random()` para los consumidores del kernel.
- El driver `mem`: crea `/dev/mem` al tiempo que proporciona funciones de acceso directo a memoria.

El driver LED se sitúa entre los drivers específicos de hardware (que saben cómo controlar los LEDs físicos) y el espacio de usuario (que quiere controlar los patrones de parpadeo). Proporciona una capa de abstracción: los drivers de hardware implementan un control simple de encendido/apagado, mientras que el subsistema LED gestiona los patrones de parpadeo complejos, la temporización y la interfaz con el usuario.

#### 1.1) Estado por LED (softc)

```c
30: struct ledsc {
31: 	LIST_ENTRY(ledsc)	list;
32: 	char			*name;
33: 	void			*private;
34: 	int			unit;
35: 	led_t			*func;
36: 	struct cdev *dev;
37: 	struct sbuf		*spec;
38: 	char			*str;
39: 	char			*ptr;
40: 	int			count;
41: 	time_t			last_second;
42: };
```

##### Estructura de estado por LED

La estructura `ledsc` (LED softc, siguiendo la convención de nomenclatura de FreeBSD para "software context", es decir, contexto de software) contiene todo el estado por dispositivo de un LED registrado. A diferencia del driver null, que no tenía estado por dispositivo, el driver LED crea una de estas estructuras por cada LED registrado en el sistema, haciendo seguimiento tanto de la identidad del dispositivo como del estado de ejecución del patrón de parpadeo actual.

##### Definición de la estructura y sus campos

```c
struct ledsc {
    LIST_ENTRY(ledsc)   list;
    char                *name;
    void                *private;
    int                 unit;
    led_t               *func;
    struct cdev *dev;
    struct sbuf         *spec;
    char                *str;
    char                *ptr;
    int                 count;
    time_t              last_second;
};
```

**`LIST_ENTRY(ledsc) list`**: Enlace para la lista global de LEDs. El macro `LIST_ENTRY` (de `<sys/queue.h>`) incrusta punteros hacia adelante y hacia atrás directamente en la estructura, permitiendo que este LED forme parte de una lista doblemente enlazada sin asignación separada. La variable global `led_list` encadena todos los LEDs registrados, lo que permite que los callbacks de temporizadores los recorran y actualicen.

**`char *name`**: La cadena con el nombre del LED, duplicada a partir de la llamada de registro del driver de hardware. Este nombre aparece en la ruta del dispositivo `/dev/led/name` e identifica al LED en las llamadas a la API del kernel como `led_set()`. Ejemplos: "disk0", "power", "heartbeat". La cadena se asigna dinámicamente y debe liberarse cuando el LED se destruya.

**`void *private`**: Un puntero opaco que se devuelve a la función de control del driver de hardware. El driver de hardware lo proporciona durante `led_create()`, apuntando normalmente a su propia estructura de contexto de dispositivo. Cuando el subsistema LED necesita encender o apagar el LED, llama al callback del driver de hardware con este puntero, lo que permite al driver localizar los registros de hardware relevantes.

**`int unit`**: Un número de unidad único para este LED, que se usa para construir el número de dispositivo menor. Se asigna desde un pool de números de unidad para evitar conflictos cuando se registran múltiples LEDs. A diferencia de los números de unidad fijos del driver null (0 para todos los dispositivos), el driver LED asigna unidades de forma dinámica a medida que se crean los LEDs.

**`led_t *func`**: Puntero a función al callback de control de LED del driver de hardware. Esta función tiene la firma `void (*led_t)(void *priv, int onoff)`, donde `priv` es el puntero privado mencionado anteriormente y `onoff` es distinto de cero para "encendido" y cero para "apagado". Este callback es la parte específica del hardware: sabe cómo manipular pines GPIO, escribir en registros de hardware o enviar transferencias de control USB para encender o apagar físicamente el LED.

**`struct cdev *dev`**: Puntero a la estructura de dispositivo de caracteres que representa `/dev/led/name`. Es lo que devuelve `make_dev()` durante la creación del LED. El nodo de dispositivo permite al espacio de usuario escribir patrones de parpadeo en el LED. El puntero es necesario posteriormente para llamar a `destroy_dev()` cuando el LED sea eliminado.

##### Estado de ejecución del patrón de parpadeo

Los campos restantes realizan el seguimiento de la ejecución del patrón de parpadeo por parte del callback del temporizador:

**`struct sbuf *spec`**: El buffer de cadena de especificación de parpadeo ya analizado. Cuando un usuario escribe un patrón como "f" (flash) o "m...---..." (código Morse), el analizador lo convierte en una secuencia de códigos de temporización y lo almacena en este `sbuf`. La cadena persiste mientras el patrón esté activo, lo que permite que el temporizador la recorra repetidamente.

**`char *str`**: Puntero al comienzo de la cadena de patrones (extraído de `spec` mediante `sbuf_data()`). Aquí es donde comienza la ejecución del patrón y adonde vuelve tras llegar al final. Si es NULL, no hay ningún patrón activo y el LED se encuentra en estado estático de encendido/apagado.

**`char *ptr`**: La posición actual en la cadena de patrones. El callback del temporizador examina este carácter para determinar qué hacer a continuación (encender o apagar el LED, esperar N décimas de segundo). Tras procesar cada carácter, `ptr` avanza. Cuando llega al terminador de cadena, vuelve a `str` para repetirse de forma continua.

**`int count`**: Un temporizador regresivo para los caracteres de retardo. Los códigos de patrón de 'a' a 'j' significan "esperar entre 1 y 10 décimas de segundo". Cuando el temporizador encuentra dicho código, establece `count` al valor de retardo y lo decrementa en cada tick del temporizador. Mientras `count > 0`, el temporizador omite el avance del patrón, implementando así el retardo.

**`time_t last_second`**: Marca de tiempo que registra el último límite de segundo, utilizada para los códigos de patrón 'U'/'u' que conmutan el LED una vez por segundo (creando un patrón de latido a 1 Hz). El temporizador compara `time_second` (la hora actual del kernel) con este campo y solo actualiza el LED cuando cambia el segundo. Esto evita múltiples actualizaciones dentro del mismo segundo si el temporizador se dispara a más de 1 Hz.

##### Gestión de memoria y ciclo de vida

Varios campos apuntan a memoria asignada dinámicamente:

- `name`: asignado con `strdup(name, M_LED)` durante la creación.
- `spec`: creado con `sbuf_new_auto()` cuando se establece un patrón.
- La estructura en sí se asigna con `malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO)`.

Todos deben liberarse durante `led_destroy()` para evitar fugas de memoria. El tiempo de vida de la estructura abarca desde `led_create()` hasta `led_destroy()`, y puede durar todo el tiempo de actividad del sistema si el driver de hardware nunca cancela el registro del LED.

##### Relación con el nodo de dispositivo

La estructura `ledsc` y el nodo de dispositivo `/dev/led/name` están enlazados bidireccionalmente:

```text
struct cdev (device node)
     ->  si_drv1
struct ledsc
     ->  dev
struct cdev (same device node)
```

Este enlace bidireccional permite:

- Que el manejador de escritura encuentre el estado del LED: `sc = dev->si_drv1`
- Que la función de destrucción elimine el dispositivo: `destroy_dev(sc->dev)`

##### Contraste con `null.c`

El driver null no tenía ninguna estructura equivalente porque sus dispositivos no tenían estado. El driver LED necesita estado por dispositivo por los siguientes motivos:

**Identidad**: cada LED tiene un nombre único y un nodo de dispositivo.

**Callback**: cada LED tiene lógica de control específica del hardware.

**Estado del patrón**: cada LED puede estar ejecutando un patrón de parpadeo diferente en una posición diferente.

**Temporización**: los contadores de retardo y las marcas de tiempo de cada LED son independientes.

Esta estructura de estado por dispositivo es típica de los drivers que gestionan múltiples instancias de hardware similar. El patrón es universal: una estructura por entidad gestionada, que contiene identidad, configuración y estado operativo.

#### 1.2) Variables globales

```c
44: static struct unrhdr *led_unit;
45: static struct mtx led_mtx;
46: static struct sx led_sx;
47: static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
48: static struct callout led_ch;
49: static int blinkers = 0;
51: static MALLOC_DEFINE(M_LED, "LED", "LED driver");
```

##### Estado global y sincronización

El driver LED mantiene varias variables globales que coordinan todos los LEDs registrados. Estas variables globales proporcionan asignación de recursos, sincronización, gestión de temporizadores y un registro de LEDs activos, una infraestructura compartida entre todas las instancias de LED.

##### Asignador de recursos

```c
static struct unrhdr *led_unit;
```

El gestor de números de unidad asigna números de unidad únicos para los dispositivos LED. Cada LED registrado recibe un número de unidad distinto que se usa para construir su número de dispositivo menor, garantizando que `/dev/led/disk0` y `/dev/led/power` no colisionen aunque se creen simultáneamente.

El `unrhdr` (gestor de números de unidad) proporciona asignación y liberación thread-safe de enteros dentro de un rango. Durante la inicialización del driver, `new_unrhdr(0, INT_MAX, NULL)` crea un pool que abarca todo el rango de enteros positivos. Cuando los drivers de hardware llaman a `led_create()`, el código invoca `alloc_unr(led_unit)` para obtener la siguiente unidad disponible. Cuando se destruye un LED, `free_unr(led_unit, sc->unit)` devuelve la unidad al pool para su reutilización.

Esta asignación dinámica contrasta con las unidades fijas del driver null (siempre 0). El driver del LED debe gestionar un número arbitrario de LEDs que aparecen y desaparecen a medida que se añade y elimina hardware.

##### Primitivas de sincronización

```c
static struct mtx led_mtx;
static struct sx led_sx;
```

El driver utiliza dos locks con propósitos distintos:

**`led_mtx` (mutex)**: Protege la lista de LEDs y el estado de ejecución del patrón de parpadeo. Este lock protege:

- La lista enlazada `led_list` a medida que se añaden y eliminan LEDs
- El contador `blinkers` que lleva la cuenta de los patrones activos
- Los campos individuales de `ledsc` modificados por los callbacks del temporizador (`ptr`, `count`, `last_second`)

El mutex utiliza semántica `MTX_DEF` (por defecto; puede dormir mientras está retenido). Los callbacks del temporizador adquieren este mutex brevemente para examinar y actualizar los estados de los LEDs. Las operaciones de escritura lo adquieren para instalar nuevos patrones de parpadeo.

**`led_sx` (lock compartido/exclusivo)**: Protege la creación y destrucción de dispositivos. Este lock serializa:

- Las llamadas a `make_dev()` y `destroy_dev()`
- La asignación y liberación de números de unidad
- La duplicación de cadenas para los nombres de los LEDs

Los locks compartidos/exclusivos permiten que múltiples lectores (threads que examinan qué LEDs existen) procedan de forma concurrente, mientras que los escritores (threads que crean o destruyen LEDs) tienen acceso exclusivo. Para el driver del LED, la creación y destrucción son operaciones poco frecuentes que se benefician de estar completamente serializadas con un lock exclusivo.

**¿Por qué dos locks?**: La separación permite la concurrencia. Los callbacks del temporizador necesitan acceso rápido a los estados de los LEDs, protegidos por el mutex, mientras que la creación y destrucción de dispositivos requieren el lock sx, más pesado. Si un único lock protegiera todo, los callbacks del temporizador quedarían bloqueados esperando operaciones de dispositivo lentas. La división permite que los temporizadores funcionen libremente mientras la gestión de dispositivos avanza de forma independiente.

##### Registro de LEDs

```c
static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
```

La lista global de LEDs mantiene todos los LEDs registrados en una lista doblemente enlazada. La macro `LIST_HEAD` (de `<sys/queue.h>`) declara una estructura de cabeza de lista y `LIST_HEAD_INITIALIZER` establece su estado inicial vacío.

Esta lista cumple múltiples propósitos:

**Iteración del temporizador**: El callback del temporizador recorre la lista con `LIST_FOREACH(sc, &led_list, list)` para actualizar el patrón de parpadeo de cada LED activo. Sin este registro, el temporizador no sabría qué LEDs existen.

**Búsqueda por nombre**: La función `led_set()` busca en la lista para encontrar un LED por su nombre cuando el código del kernel quiere controlar un LED de forma programática.

**Verificación de limpieza**: Cuando se elimina el último LED (`LIST_EMPTY(&led_list)`), el driver puede detener el callback del temporizador, ahorrando ciclos de CPU cuando no hay LEDs que atender.

La lista está protegida por `led_mtx`, ya que tanto los callbacks del temporizador como las operaciones de dispositivo la modifican.

##### Infraestructura del callback del temporizador

```c
static struct callout led_ch;
static int blinkers = 0;
```

**`led_ch` (callout)**: Un temporizador del kernel que se activa periódicamente para avanzar los patrones de parpadeo. Cuando algún LED tiene un patrón activo, el temporizador se programa para dispararse 10 veces por segundo (`hz / 10`, donde `hz` es el número de ticks de temporizador por segundo, típicamente 1000). Cada disparo llama a `led_timeout()`, que recorre la lista de LEDs y actualiza los estados de los patrones.

El callout permanece inactivo (sin programar) cuando no hay LEDs parpadeando, ahorrando recursos. El primer LED que recibe un patrón de parpadeo programa el temporizador con `callout_reset(&led_ch, hz / 10, led_timeout, NULL)`. Los patrones siguientes no reprograman el temporizador; el temporizador único da servicio a todos los LEDs.

**Contador `blinkers`**: Lleva la cuenta de cuántos LEDs tienen actualmente patrones de parpadeo activos. Cuando se asigna un patrón, `blinkers++`. Cuando un patrón se completa o se reemplaza con un estado estático de encendido/apagado, `blinkers--`. Cuando el contador llega a cero (comprobado al final de la función), el callback del temporizador no se reprograma, deteniendo los disparos periódicos.

Este conteo de referencias es fundamental para el rendimiento. Sin él, el temporizador se dispararía continuamente incluso sin trabajo que realizar. El contador regula la actividad del temporizador: se programa al pasar de 0 a 1, y se detiene al pasar de 1 a 0.

##### Declaración del tipo de memoria

```c
static MALLOC_DEFINE(M_LED, "LED", "LED driver");
```

La macro `MALLOC_DEFINE` registra un tipo de asignación de memoria para el subsistema de LEDs. Todas las asignaciones relacionadas con los LEDs especifican `M_LED`:

- `malloc(sizeof *sc, M_LED, ...)` para las estructuras softc
- `strdup(name, M_LED)` para las cadenas de nombres de LEDs

Los tipos de memoria habilitan la contabilización y depuración del kernel:

- `vmstat -m` muestra el consumo de memoria por tipo
- Los desarrolladores pueden rastrear si el driver del LED está perdiendo memoria
- Los depuradores de memoria del kernel pueden filtrar asignaciones por tipo

Los tres argumentos son:

1. `M_LED`: el identificador C utilizado en las llamadas a `malloc()`
2. `"LED"`: nombre corto que aparece en la salida de contabilización
3. `"LED driver"`: texto descriptivo para la documentación

##### Coordinación de la inicialización

Estas variables globales se inicializan en una secuencia específica durante el boot:

1. **Inicialización estática**: `led_list` y `blinkers` reciben valores iniciales en tiempo de compilación
2. **`led_drvinit()` (a través de `SYSINIT`)**: Asigna `led_unit`, inicializa `led_mtx` y `led_sx`, y prepara el callout
3. **En tiempo de ejecución**: Los drivers de hardware llaman a `led_create()` para registrar LEDs, incrementando `blinkers` y poblando `led_list`

La clase de almacenamiento `static` en todas las variables globales limita su visibilidad a este archivo fuente. Ningún otro código del kernel puede acceder directamente a estas variables; todas las interacciones pasan por la API pública (`led_create()`, `led_destroy()`, `led_set()`). Esta encapsulación evita que el código externo corrompa el estado interno del subsistema de LEDs.

##### Contraste con null.c

El driver null tenía un estado global mínimo: tres punteros de dispositivo para sus dispositivos fijos. Las variables globales del driver del LED reflejan su naturaleza dinámica:

- **Asignación de recursos**: Números de unidad para un número arbitrario de dispositivos
- **Concurrencia**: Dos locks para diferentes patrones de acceso
- **Registro**: Una lista que registra todos los LEDs activos
- **Planificación**: Infraestructura de temporizadores para la ejecución de patrones
- **Contabilización**: Tipo de memoria para el seguimiento de asignaciones

Esta infraestructura global más rica respalda el papel del driver del LED como subsistema que gestiona múltiples dispositivos creados dinámicamente con comportamientos basados en el tiempo, en lugar de ser un simple driver que expone dispositivos fijos sin estado.

#### 2) El latido: `led_timeout()` avanza el patrón

Este **callout periódico** recorre todos los LEDs y avanza el patrón de cada uno. Los patrones están codificados en ASCII, por lo que el analizador y la máquina de estados se mantienen muy reducidos.

```c
54: static void
55: led_timeout(void *p)
56: {
57: 	struct ledsc	*sc;
58: 	LIST_FOREACH(sc, &led_list, list) {
59: 		if (sc->ptr == NULL)
60: 			continue;
61: 		if (sc->count > 0) {
62: 			sc->count--;
63: 			continue;
64: 		}
65: 		if (*sc->ptr == '.') {
66: 			sc->ptr = NULL;
67: 			blinkers--;
68: 			continue;
69: 		} else if (*sc->ptr == 'U' || *sc->ptr == 'u') {
70: 			if (sc->last_second == time_second)
71: 				continue;
72: 			sc->last_second = time_second;
73: 			sc->func(sc->private, *sc->ptr == 'U');
74: 		} else if (*sc->ptr >= 'a' && *sc->ptr <= 'j') {
75: 			sc->func(sc->private, 0);
76: 			sc->count = (*sc->ptr & 0xf) - 1;
77: 		} else if (*sc->ptr >= 'A' && *sc->ptr <= 'J') {
78: 			sc->func(sc->private, 1);
79: 			sc->count = (*sc->ptr & 0xf) - 1;
80: 		}
81: 		sc->ptr++;
82: 		if (*sc->ptr == '\0')
83: 			sc->ptr = sc->str;
84: 	}
85: 	if (blinkers > 0)
86: 		callout_reset(&led_ch, hz / 10, led_timeout, p);
87: }
```

##### Callback del temporizador: motor de ejecución de patrones

La función `led_timeout` es el corazón de la ejecución de patrones de parpadeo del subsistema de LEDs. Llamada por el subsistema de temporizadores del kernel aproximadamente 10 veces por segundo, recorre la lista global de LEDs y avanza cada patrón activo un paso, interpretando un sencillo lenguaje de patrones para controlar la temporización y el estado de los LEDs.

##### Entrada a la función e iteración de la lista

```c
static void
led_timeout(void *p)
{
    struct ledsc    *sc;
    LIST_FOREACH(sc, &led_list, list) {
```

**Firma de la función**: Los callbacks de temporizador reciben un único argumento `void *` que se pasa durante la programación del temporizador. Este driver no utiliza el argumento (que suele ser NULL) y se apoya en la lista global de LEDs para encontrar trabajo.

**Iteración de todos los LEDs**: La macro `LIST_FOREACH` recorre la lista doblemente enlazada `led_list`, visitando cada LED registrado. Esto permite que un único temporizador dé servicio a múltiples LEDs independientes, cada uno ejecutando potencialmente un patrón de parpadeo diferente en una posición distinta. La iteración es segura porque la lista está protegida por `led_mtx` (el callout se inicializó con este mutex mediante `callout_init_mtx()`).

##### Omisión de LEDs inactivos

```c
if (sc->ptr == NULL)
    continue;
```

El campo `ptr` indica si este LED tiene un patrón de parpadeo activo. Cuando es NULL, el LED está en estado estático de encendido/apagado y no necesita procesamiento por parte del temporizador. El callback salta al siguiente LED de inmediato.

Esta comprobación es el primer filtro: los LEDs sin patrones no consumen tiempo de CPU. Solo los LEDs que parpadean activamente requieren procesamiento en cada tick del temporizador.

##### Gestión de los estados de retardo

```c
if (sc->count > 0) {
    sc->count--;
    continue;
}
```

El campo `count` implementa los retardos en los patrones de parpadeo. Cuando el intérprete de patrones encuentra códigos de temporización como 'a' a 'j' (que significan «espera entre 1 y 10 décimas de segundo»), establece `count` con el valor del retardo. En los ticks siguientes del temporizador, el callback decrementa `count` sin avanzar en el patrón.

**Ejemplo**: El código de patrón 'c' (espera 3 décimas de segundo) establece `count = 2` (el valor es 1 menos que el retardo previsto). Los dos ticks siguientes del temporizador decrementan `count` a 1, y luego a 0. En el tercer tick, `count` ya es 0, por lo que esta comprobación no se cumple y la ejecución del patrón continúa.

Este mecanismo crea una temporización precisa: a 10 Hz, cada unidad de conteo representa 0,1 segundos. El patrón 'AcAc' produce: LED encendido, espera 0,3 s, LED encendido de nuevo, espera 0,3 s, repetir.

##### Terminación del patrón

```c
if (*sc->ptr == '.') {
    sc->ptr = NULL;
    blinkers--;
    continue;
}
```

El carácter punto '.' señala el fin del patrón. A diferencia de la mayoría de los patrones, que se repiten indefinidamente, algunas especificaciones de usuario incluyen un terminador explícito. Cuando se encuentra:

**Detener la ejecución del patrón**: Establecer `ptr = NULL` marca este LED como inactivo. Los ticks futuros del temporizador lo omitirán en la primera comprobación.

**Decrementar el contador de parpadeadores**: Reducir `blinkers` registra que un LED menos necesita atención. Cuando este contador llega a cero (comprobado al final de la función), el temporizador deja de programarse a sí mismo.

**Omitir el código restante**: El `continue` salta al siguiente LED de la lista. El código de avance del patrón y el de reinicio al final de `led_timeout` (el paso `sc->ptr++` y el reinicio con `*sc->ptr == '\0'`) no se ejecuta para los patrones terminados.

##### Patrón de latido: alternancia basada en segundos

```c
else if (*sc->ptr == 'U' || *sc->ptr == 'u') {
    if (sc->last_second == time_second)
        continue;
    sc->last_second = time_second;
    sc->func(sc->private, *sc->ptr == 'U');
}
```

Los códigos 'U' y 'u' crean alternaciones de una vez por segundo, útiles como indicadores de latido que muestran que el sistema está activo.

**Detección del límite de segundo**: La variable del kernel `time_second` contiene la marca de tiempo Unix actual. Compararla con `last_second` detecta cuándo ha pasado un límite de segundo. Si los valores coinciden, seguimos dentro del mismo segundo y el callback omite el procesamiento con `continue`.

**Registro de la transición**: `sc->last_second = time_second` recuerda este segundo, evitando múltiples actualizaciones si el temporizador se dispara varias veces por segundo (lo cual ocurre efectivamente, 10 veces por segundo).

**Actualización del LED**: El callback invoca la función de control del driver de hardware. El segundo parámetro determina el estado del LED:

- `*sc->ptr == 'U'`  ->  true (1)  ->  LED encendido
- `*sc->ptr == 'u'`  ->  false (0)  ->  LED apagado

El patrón "Uu" crea una alternancia de 1 Hz: encendido durante un segundo, apagado durante un segundo. El patrón "U" solo mantiene el LED encendido, pero únicamente se actualiza en los límites de segundo, lo que puede usarse para fines de sincronización.

##### Patrón de retardo de apagado

```c
else if (*sc->ptr >= 'a' && *sc->ptr <= 'j') {
    sc->func(sc->private, 0);
    sc->count = (*sc->ptr & 0xf) - 1;
}
```

Las letras minúsculas de 'a' a 'j' significan «apagar el LED y esperar». Esto combina dos operaciones: un cambio de estado inmediato más la configuración del retardo.

**Apagado del LED**: `sc->func(sc->private, 0)` llama a la función de control del driver de hardware con el comando de apagado (el segundo parámetro es 0).

**Cálculo del retardo**: La expresión `(*sc->ptr & 0xf) - 1` extrae la duración del retardo del código de carácter. En ASCII:

- 'a' es 0x61, `0x61 & 0x0f = 1`, menos 1 = 0 (espera 0,1 segundos)
- 'b' es 0x62, `0x62 & 0x0f = 2`, menos 1 = 1 (espera 0,2 segundos)
- 'c' es 0x63, `0x63 & 0x0f = 3`, menos 1 = 2 (espera 0,3 segundos)
- ...
- 'j' es 0x6A, `0x6A & 0x0f = 10`, menos 1 = 9 (espera 1,0 segundos)

La máscara `& 0xf` aísla los 4 bits de menor peso, que convenientemente asignan los valores del 1 al 10 a los caracteres 'a' a 'j'. Restar 1 convierte al formato de cuenta regresiva (ticks del temporizador restantes menos uno).

##### Patrón de retardo de encendido

```c
else if (*sc->ptr >= 'A' && *sc->ptr <= 'J') {
    sc->func(sc->private, 1);
    sc->count = (*sc->ptr & 0xf) - 1;
}
```

Las letras mayúsculas de 'A' a 'J' funcionan de forma idéntica a las minúsculas, salvo que el LED se enciende en lugar de apagarse. El cálculo del retardo es el mismo:

- 'A'  ->  encendido durante 0,1 segundos
- 'B'  ->  encendido durante 0,2 segundos
- ...
- 'J'  ->  encendido durante 1,0 segundos

El patrón "AaBb" genera: encendido 0.1s, apagado 0.1s, encendido 0.2s, apagado 0.2s, se repite. El patrón "Aa" es un parpadeo rápido estándar a ~2.5Hz.

##### Avance del patrón y bucle

```c
sc->ptr++;
if (*sc->ptr == '\0')
    sc->ptr = sc->str;
```

Tras procesar el carácter de patrón actual (ya sea un código de latido o un código de retardo), el puntero avanza al siguiente carácter.

**Detección del fin del patrón**: si la nueva posición es el terminador null, el patrón se ha ejecutado completamente una vez. En lugar de detenerse (como hace el terminador '.'), la mayoría de los patrones se repiten indefinidamente.

**Volver al inicio del bucle**: `sc->ptr = sc->str` restablece la posición al comienzo del patrón. El siguiente tick del temporizador comenzará de nuevo desde el primer carácter, creando un ciclo repetitivo.

**Ejemplo**: el patrón "AjBj" equivale a encendido 1s, encendido 1s, y se repite de forma continua. El patrón no se detiene nunca a menos que se sustituya por una nueva escritura o se destruya el LED.

##### Reprogramación del temporizador

```c
if (blinkers > 0)
    callout_reset(&led_ch, hz / 10, led_timeout, p);
}
```

Tras procesar todos los LED, el callback decide si reprogramarse a sí mismo. Si algún LED tiene todavía un patrón activo (`blinkers > 0`), el temporizador se restablece para dispararse de nuevo en `hz / 10` ticks (0.1 segundos).

**Temporizador autoperpetuado**: cada invocación programa la siguiente, creando un bucle continuo mientras haya trabajo pendiente. Esto es diferente de un temporizador periódico que se dispara incondicionalmente; el temporizador del LED se activa según el trabajo pendiente.

**Apagado automático**: cuando el último patrón activo termina (ya sea mediante '.' o al ser sustituido por un estado estático), `blinkers` cae a 0 y el temporizador no se reprograma. El callback finaliza y no vuelve a ejecutarse hasta que un nuevo patrón lo active, conservando CPU cuando todos los LED están en estado estático.

**La variable `hz`**: la constante del kernel `hz` representa los ticks del temporizador por segundo (normalmente 1000 en sistemas modernos). Dividir entre 10 da el retardo en ticks para una décima de segundo, lo que coincide con la resolución del lenguaje de patrones.

##### Resumen del lenguaje de patrones

El temporizador interpreta un lenguaje sencillo integrado en las cadenas de patrón:

| Código  | Significado            | Duración                   |
| ------- | ---------------------- | -------------------------- |
| 'a'-'j' | LED apagado            | 0.1-1.0 segundos           |
| 'A'-'J' | LED encendido          | 0.1-1.0 segundos           |
| 'U'     | LED encendido          | En el límite del segundo   |
| 'u'     | LED apagado            | En el límite del segundo   |
| '.'     | Fin del patrón         | -                          |

Patrones de ejemplo y sus efectos:

- "Aa"  ->  parpadeo a ~2.5Hz (0.1s encendido, 0.1s apagado)
- "AjAj"  ->  parpadeo lento a 0.5Hz (1s encendido, 1s apagado)
- "AaAaBjBj"  ->  doble parpadeo rápido, pausa larga
- "U"  ->  encendido continuo, sincronizado con los segundos
- "Uu"  ->  conmutación a 1Hz

Esta codificación compacta permite comportamientos de parpadeo complejos a partir de cadenas cortas, todo interpretado por este único callback de temporizador que sirve a todos los LED del sistema.

#### 3) Aplicar un nuevo estado/patrón: `led_state()`

Dado un patrón compilado (sbuf) o un indicador simple de encendido/apagado, esta función actualiza el softc e inicia o detiene el temporizador periódico.

```c
88: static int
89: led_state(struct ledsc *sc, struct sbuf **sb, int state)
90: {
91: 	struct sbuf *sb2 = NULL;
93: 	sb2 = sc->spec;
94: 	sc->spec = *sb;
95: 	if (*sb != NULL) {
96: 		if (sc->str != NULL)
97: 			free(sc->str, M_LED);
98: 		sc->str = strdup(sbuf_data(*sb), M_LED);
99: 		if (sc->ptr == NULL)
100: 			blinkers++;
101: 		sc->ptr = sc->str;
102: 	} else {
103: 		sc->str = NULL;
104: 		if (sc->ptr != NULL)
105: 			blinkers--;
106: 		sc->ptr = NULL;
107: 		sc->func(sc->private, state);
108: 	}
109: 	sc->count = 0;
110: 	*sb = sb2;
111: 	return(0);
112: }
```

##### Gestión del estado del LED: instalación de patrones

La función `led_state` instala un nuevo patrón de parpadeo o un estado estático para un LED. Gestiona la transición entre los distintos modos del LED, administra la memoria de las cadenas de patrón, actualiza el contador de parpadeo para el control del temporizador e invoca los callbacks de hardware cuando es necesario. Esta función es el coordinador central de cambios de estado, llamado tanto por el gestor de escritura como por la API del kernel.

##### Firma de la función e intercambio de patrones

```c
static int
led_state(struct ledsc *sc, struct sbuf **sb, int state)
{
    struct sbuf *sb2 = NULL;

    sb2 = sc->spec;
    sc->spec = *sb;
```

**Parámetros**: la función recibe tres valores:

- `sc` - el LED cuyo estado se está cambiando
- `sb` - puntero a un puntero a un buffer de cadena que contiene el nuevo patrón (o NULL para estado estático)
- `state` - el estado estático deseado (0 o 1) si no se proporciona ningún patrón

**El patrón de doble puntero**: el parámetro `sb` es `struct sbuf **`, lo que permite a la función intercambiar buffers con el caller. La función toma posesión del buffer del caller y devuelve el buffer anterior para su limpieza. Este intercambio evita copiar cadenas de patrón y garantiza una gestión correcta de la memoria.

**Conservar el patrón anterior**: `sb2 = sc->spec` guarda el buffer del patrón actual antes de instalar el nuevo. Al final de la función, este buffer antiguo se devuelve al caller a través de `*sb = sb2`. El caller pasa a ser responsable de liberarlo con `sbuf_delete()`.

##### Instalar un patrón de parpadeo

```c
if (*sb != NULL) {
    if (sc->str != NULL)
        free(sc->str, M_LED);
    sc->str = strdup(sbuf_data(*sb), M_LED);
    if (sc->ptr == NULL)
        blinkers++;
    sc->ptr = sc->str;
```

Cuando el caller proporciona un patrón (`sb` no es NULL), la función activa el modo de patrón.

**Liberar la cadena anterior**: si `sc->str` no es NULL, existe la cadena de un patrón anterior y debe liberarse. La llamada `free(sc->str, M_LED)` devuelve esta memoria al heap del kernel. La etiqueta `M_LED` coincide con el tipo de asignación utilizado durante `strdup()`, lo que mantiene la coherencia de la contabilidad.

**Duplicar el nuevo patrón**: `sbuf_data(*sb)` extrae la cadena terminada en null del buffer de cadena, y `strdup(name, M_LED)` asigna memoria y la copia. La cadena del patrón debe persistir porque el callback del temporizador la recorrerá repetidamente; el propio buffer de cadena puede ser eliminado por el caller, por lo que se necesita una copia independiente.

**Activar el temporizador**: la comprobación `if (sc->ptr == NULL)` detecta si este LED estaba previamente inactivo. En ese caso, incrementar `blinkers++` registra que un LED más necesita ahora atención del temporizador. El callback del temporizador comprueba este contador al final de cada ejecución; la transición de 0 a 1 hace que el temporizador se reprograme.

**Iniciar la ejecución del patrón**: `sc->ptr = sc->str` establece la posición del patrón al principio. En el siguiente tick del temporizador, `led_timeout` procesará el primer carácter del patrón de este LED.

**¿Por qué no iniciar el temporizador aquí?**: el temporizador puede estar ya en marcha si otros LED tienen patrones activos. El contador `blinkers` controla esto: si ya no era cero, el temporizador ya está programado y procesará este LED en su siguiente tick. Solo cuando `blinkers` pasa de 0 a 1 (detectado en el gestor de escritura o en `led_set()`) necesita el temporizador una programación explícita.

##### Instalar un estado estático

```c
} else {
    sc->str = NULL;
    if (sc->ptr != NULL)
        blinkers--;
    sc->ptr = NULL;
    sc->func(sc->private, state);
}
```

Cuando el caller pasa NULL para `sb`, el LED debe establecerse en un estado estático de encendido/apagado sin parpadeo.

**Limpiar el estado del patrón**: establecer `sc->str = NULL` indica que no existe ninguna cadena de patrón. Este campo se comprueba durante la limpieza para determinar si hay que liberar memoria.

**Desactivar el temporizador**: la comprobación `if (sc->ptr != NULL)` detecta si este LED estaba ejecutando previamente un patrón. En ese caso, decrementar `blinkers--` registra que un LED menos necesita atención del temporizador. Si era el último LED activo, `blinkers` cae a cero y el callback del temporizador no se reprogramará, deteniendo las activaciones del temporizador.

**Establecer a NULL**: `sc->ptr = NULL` marca este LED como inactivo. La primera comprobación del callback del temporizador (`if (sc->ptr == NULL) continue;`) ignorará este LED en todos los ticks futuros.

**Actualización inmediata del hardware**: `sc->func(sc->private, state)` invoca el callback de control del driver de hardware para establecer el LED en el estado solicitado (0 para apagado, 1 para encendido). A diferencia del modo de patrón, donde el temporizador controla los cambios del LED, el modo estático requiere una actualización inmediata del hardware, ya que no hay ningún temporizador implicado.

##### Restablecer el contador de retardo

```c
sc->count = 0;
```

El contador de retardo se pone a cero independientemente del camino tomado. Si se está instalando un patrón, empezar con `count = 0` garantiza que el primer carácter del patrón se ejecute de inmediato sin retardo heredado. Si se está estableciendo un estado estático, ponerlo a cero es inocuo, ya que el campo no se utiliza cuando `ptr` es NULL.

##### Devolver el patrón anterior

```c
*sb = sb2;
return(0);
```

La función devuelve el buffer del patrón anterior a través del doble puntero. El caller recibe:

- NULL si no existía ningún patrón anterior
- El `sbuf` antiguo si se está sustituyendo un patrón

El caller debe comprobar este valor devuelto y llamar a `sbuf_delete()` si no es NULL para liberar la memoria del buffer. Este patrón de transferencia de propiedad previene las fugas de memoria evitando las copias innecesarias.

El valor de retorno 0 indica éxito. Esta función actualmente no puede fallar, pero devolver un código de error proporciona extensibilidad futura si se añadiesen validación o asignación de recursos.

##### Ejemplos de transición de estado

**Establecer patrón inicial en un LED inactivo**:

```text
Before: sc->ptr = NULL, sc->spec = NULL, blinkers = 0
Call:   led_state(sc, &pattern_sb, 0)
After:  sc->ptr = sc->str, sc->spec = pattern_sb, blinkers = 1
        Old NULL returned to caller
```

**Sustituir un patrón por otro**:

```text
Before: sc->ptr = old_str, sc->spec = old_sb, blinkers = 3
Call:   led_state(sc, &new_sb, 0)
After:  sc->ptr = new_str, sc->spec = new_sb, blinkers = 3
        Old old_sb returned to caller for deletion
```

**Cambiar de patrón a estado estático**:

```text
Before: sc->ptr = pattern_str, sc->spec = pattern_sb, blinkers = 1
Call:   led_state(sc, &NULL_ptr, 1)
After:  sc->ptr = NULL, sc->spec = NULL, blinkers = 0
        Hardware callback invoked with state=1 (on)
        Old pattern_sb returned to caller for deletion
```

**Establecer estado estático en un LED ya estático**:

```text
Before: sc->ptr = NULL, sc->spec = NULL, blinkers = 0
Call:   led_state(sc, &NULL_ptr, 0)
After:  sc->ptr = NULL, sc->spec = NULL, blinkers = 0
        Hardware callback invoked with state=0 (off)
        Old NULL returned to caller
```

##### Consideraciones sobre la seguridad de threads

Esta función opera bajo la protección de `led_mtx`, adquirido por el caller (el gestor de escritura o `led_set()`). El mutex serializa los cambios de estado y protege:

- El contador `blinkers` frente a condiciones de carrera cuando múltiples LED cambian de estado simultáneamente
- Los campos individuales del LED (`ptr`, `str`, `spec`, `count`) frente a la corrupción
- La relación entre el recuento de `blinkers` y los patrones realmente activos

Sin el mutex, dos escrituras simultáneas podrían incrementar `blinkers` ambas, generando un recuento incorrecto. O un thread podría liberar `sc->str` mientras el callback del temporizador lo recorre, provocando un fallo por uso después de liberar (use-after-free).

##### Disciplina de gestión de memoria

La función demuestra una cuidadosa gestión de la memoria:

**Transferencia de propiedad**: el caller cede el nuevo `sbuf` y recibe el antiguo, estableciendo una propiedad clara en todo momento.

**Asignación y liberación emparejadas**: cada `strdup()` tiene su correspondiente `free()`, lo que evita fugas incluso cuando los patrones se sustituyen repetidamente.

**Tolerancia a NULL**: todas las comprobaciones gestionan los punteros NULL de forma segura, permitiendo transiciones hacia y desde el estado no inicializado sin casos especiales.

Esta disciplina previene el error habitual de sustitución de patrones, en el que actualizar el estado provoca una fuga de la memoria del patrón anterior.

#### 4) Analizar comandos de usuario en patrones: `led_parse()`

```c
116: static int
117: led_parse(const char *s, struct sbuf **sb, int *state)
118: {
119: 	int i, error;
121: 	/* '0' or '1' means immediate steady off/on (no pattern). */
124: 	if (*s == '0' || *s == '1') {
125: 		*state = *s & 1;
126: 		return (0);
127: 	}
129: 	*state = 0;
130: 	*sb = sbuf_new_auto();
131: 	if (*sb == NULL)
132: 		return (ENOMEM);
133: 	switch(s[0]) {
135: 	case 'f': /* blink (default 100/100ms); 'f2' => 200/200ms */
136: 		if (s[1] >= '1' && s[1] <= '9') i = s[1] - '1'; else i = 0;
137: 		sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i);
138: 		break;
149: 	case 'd': /* "digits": flash out numbers 0..9 */
150: 		for(s++; *s; s++) {
151: 			if (!isdigit(*s)) continue;
152: 			i = *s - '0'; if (i == 0) i = 10;
156: 			for (; i > 1; i--) sbuf_cat(*sb, "Aa");
158: 			sbuf_cat(*sb, "Aj");
159: 		}
160: 		sbuf_cat(*sb, "jj");
161: 		break;
162: 	/* other small patterns elided for brevity in this excerpt ... */
187: 	case 'm': /* Morse: '.' -> short, '-' -> long, ' ' -> space */
188: 		for(s++; *s; s++) {
189: 			if (*s == '.') sbuf_cat(*sb, "aA");
190: 			else if (*s == '-') sbuf_cat(*sb, "aC");
191: 			else if (*s == ' ') sbuf_cat(*sb, "b");
192: 			else if (*s == '\n') sbuf_cat(*sb, "d");
193: 		}
198: 		sbuf_cat(*sb, "j");
199: 		break;
200: 	default:
201: 		sbuf_delete(*sb);
202: 		return (EINVAL);
203: 	}
204: 	error = sbuf_finish(*sb);
205: 	if (error != 0 || sbuf_len(*sb) == 0) {
206: 		*sb = NULL;
207: 		return (error);
208: 	}
209: 	return (0);
210: }
```

##### Analizador de patrones: comandos de usuario a códigos internos

La función `led_parse` traduce las especificaciones de patrones legibles por las personas procedentes del espacio de usuario al lenguaje de códigos de temporización interno que interpreta el callback del temporizador. Este analizador permite a los usuarios escribir comandos simples como "f" para el parpadeo o "m...---..." para el código Morse, que se expanden en secuencias de códigos de temporización como "AaAa" o "aAaAaCaCaC".

##### Firma de la función y ruta estática rápida

```c
static int
led_parse(const char *s, struct sbuf **sb, int *state)
{
    int i, error;

    /* '0' or '1' means immediate steady off/on (no pattern). */
    if (*s == '0' || *s == '1') {
        *state = *s & 1;
        return (0);
    }
```

**Parámetros**: el analizador recibe tres valores:

- `s` - la cadena de entrada del usuario procedente de la operación de escritura
- `sb` - puntero a un puntero donde se devolverá el buffer de cadena asignado
- `state` - puntero donde se devuelve el estado estático (0 o 1) para comandos sin patrón

**Ruta rápida para estado estático**: los comandos "0" y "1" solicitan el apagado y el encendido estáticos, respectivamente. La expresión `*s & 1` extrae el bit bajo del carácter ASCII: '0' (0x30) & 1 = 0, '1' (0x31) & 1 = 1. Este valor se escribe en `*state` y la función retorna de inmediato sin asignar un buffer de cadena. El caller recibe `*sb = NULL` (nunca asignado) y sabe que debe usar `led_state()` con el modo estático.

Esta ruta rápida gestiona el caso más habitual de forma eficiente, activando o desactivando LED sin temporización compleja.

##### Asignación del buffer de cadena

```c
*state = 0;
*sb = sbuf_new_auto();
if (*sb == NULL)
    return (ENOMEM);
```

Para los comandos de patrón, se necesita un buffer de cadena para construir la secuencia de códigos internos.

**Estado por defecto**: establecer `*state = 0` proporciona un valor predeterminado en caso de que se use el patrón, aunque este valor se ignora cuando `*sb` no es NULL.

**Crear un buffer de tamaño automático**: `sbuf_new_auto()` asigna un buffer de cadena que crece automáticamente a medida que se añaden datos. Esto elimina la necesidad de precalcular la longitud del patrón. El código Morse de un mensaje largo puede producir una secuencia de códigos muy larga, pero el buffer se expande según sea necesario.

**Gestión del fallo de asignación**: si la memoria se agota, la función devuelve `ENOMEM` de inmediato. El caller comprueba este error y lo propaga al espacio de usuario, donde la operación de escritura falla con el mensaje "Cannot allocate memory".

##### Despacho de patrones

```c
switch(s[0]) {
```

El primer carácter determina el tipo de patrón. Cada caso implementa un lenguaje de patrones diferente, expandiendo la entrada del usuario en códigos de temporización.

##### Patrón flash: parpadeo simple

```c
case 'f': /* blink (default 100/100ms); 'f2' => 200/200ms */
    if (s[1] >= '1' && s[1] <= '9') i = s[1] - '1'; else i = 0;
    sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i);
    break;
```

El comando 'f' crea un patrón de parpadeo simétrico, con tiempos de encendido y apagado iguales.

**Modificador de velocidad**: Si un dígito sigue a 'f', especifica la velocidad de parpadeo:

- "f" o "f1"  ->  `i = 0`  ->  patrón "Aa"  ->  0.1s on, 0.1s off (~2.5Hz)
- "f2"  ->  `i = 1`  ->  patrón "Bb"  ->  0.2s on, 0.2s off (~1.25Hz)
- "f3"  ->  `i = 2`  ->  patrón "Cc"  ->  0.3s on, 0.3s off (~0.83Hz)
- ...
- "f9"  ->  `i = 8`  ->  patrón "Ii"  ->  0.9s on, 0.9s off (~0.56Hz)

**Construcción del patrón**: `sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i)` genera dos caracteres: una letra mayúscula (estado on) seguida de la letra minúscula correspondiente (estado off). Ambos usan la misma duración, creando un parpadeo simétrico.

Este sencillo patrón de dos caracteres se repite indefinidamente, proporcionando el efecto clásico de indicador de parpadeo.

##### Patrón de destellos por dígito: conteo de parpadeos

```c
case 'd': /* "digits": flash out numbers 0..9 */
    for(s++; *s; s++) {
        if (!isdigit(*s)) continue;
        i = *s - '0'; if (i == 0) i = 10;
        for (; i > 1; i--) sbuf_cat(*sb, "Aa");
        sbuf_cat(*sb, "Aj");
    }
    sbuf_cat(*sb, "jj");
    break;
```

El comando 'd' seguido de dígitos crea patrones que «cuentan» visualmente mediante destellos del LED.

**Análisis de dígitos**: El bucle avanza más allá del carácter de comando 'd' (`s++`) y examina cada carácter subsiguiente. Los no dígitos se omiten silenciosamente con `continue`, lo que permite que "d1x2y3" se interprete como "d123".

**Conversión de dígitos**: `i = *s - '0'` convierte el dígito ASCII a valor numérico. El caso especial `if (i == 0) i = 10` trata el cero como diez destellos en lugar de ninguno, haciéndolo distinguible de la pausa entre dígitos.

**Generación de destellos**: Para el valor de dígito `i`:

- Generar `i-1` destellos rápidos: `for (; i > 1; i--) sbuf_cat(*sb, "Aa")`
- Añadir un destello más largo: `sbuf_cat(*sb, "Aj")`

Ejemplo para el dígito 3: dos destellos rápidos "AaAa" más un destello de 1 segundo "Aj".

**Separación de dígitos**: Una vez procesados todos los dígitos, `sbuf_cat(*sb, "jj")` añade una pausa de 2 segundos antes de que el patrón se repita, separando claramente las repeticiones.

**Resultado**: El comando "d12" genera el patrón "AjAjAaAjjj", que significa: destello de 1 segundo (dígito 1), pausa, destello rápido seguido de destello de 1 segundo (dígito 2), pausa larga, repetición. Esto permite leer números mediante los parpadeos del LED, lo que resulta útil para códigos de diagnóstico.

##### Patrón de código Morse

```c
case 'm': /* Morse: '.' -> short, '-' -> long, ' ' -> space */
    for(s++; *s; s++) {
        if (*s == '.') sbuf_cat(*sb, "aA");
        else if (*s == '-') sbuf_cat(*sb, "aC");
        else if (*s == ' ') sbuf_cat(*sb, "b");
        else if (*s == '\n') sbuf_cat(*sb, "d");
    }
    sbuf_cat(*sb, "j");
    break;
```

El comando 'm' interpreta los caracteres siguientes como elementos de código Morse.

**Correspondencia de elementos Morse**:

- '.' (punto)  ->  "aA"  ->  0.1s off, 0.1s on (destello corto)
- '-' (guion)  ->  "aC"  ->  0.1s off, 0.3s on (destello largo)
- ' ' (espacio)  ->  "b"  ->  0.2s off (separador de palabras)
- '\\n' (nueva línea)  ->  "d"  ->  0.4s off (pausa larga entre mensajes)

**Temporización estándar del Morse**: El código Morse internacional especifica:

- Punto: 1 unidad
- Guion: 3 unidades
- Separación entre elementos: 1 unidad
- Separación entre letras: 3 unidades (aproximada mediante la pausa final de cada letra)
- Separación entre palabras: 7 unidades (carácter espacio)

El patrón "aA" representa el punto (1 unidad off, 1 unidad on), y "aC" representa el guion (1 unidad off, 3 unidades on), siendo cada unidad 0.1 segundos.

**Finalización del patrón**: `sbuf_cat(*sb, "j")` añade una pausa de 1 segundo antes de que el mensaje se repita, separando transmisiones consecutivas.

**Ejemplo**: El comando "m... ---" (SOS) genera "aAaAaAaCaCaC", que significa: punto-punto-punto, guion-guion-guion, repetición.

##### Gestión de errores para comandos desconocidos

```c
default:
    sbuf_delete(*sb);
    return (EINVAL);
}
```

Si el primer carácter no coincide con ningún tipo de patrón conocido, la función rechaza el comando. El buffer de cadena asignado se libera con `sbuf_delete()` para evitar fugas de memoria, y se devuelve `EINVAL` (argumento no válido) para indicar que la entrada del usuario es incorrecta.

La operación de escritura fallará y devolverá -1 al espacio de usuario con `errno = EINVAL`, informando al usuario de que la sintaxis del comando es incorrecta.

##### Finalización de la cadena de patrón

```c
error = sbuf_finish(*sb);
if (error != 0 || sbuf_len(*sb) == 0) {
    *sb = NULL;
    return (error);
}
return (0);
```

**Cierre del buffer**: `sbuf_finish()` finaliza el buffer de cadena, añadiendo el terminador nulo y marcándolo como de solo lectura. Tras esta llamada, el contenido del buffer puede extraerse con `sbuf_data()`, pero no se permiten más adiciones.

**Validación**: Se comprueban dos condiciones de error:

- `error != 0`: `sbuf_finish()` falló, habitualmente por agotamiento de memoria durante un redimensionado del buffer
- `sbuf_len(*sb) == 0`: el patrón está vacío, lo que no debería ocurrir pero se comprueba de forma defensiva

Si se cumple alguna de estas condiciones, el buffer no es utilizable. Asignar `*sb = NULL` señala al llamador que no se generó ningún patrón, y se devuelve el código de error. El llamador no debe intentar usar ni liberar el buffer, ya que `sbuf_finish()` lo liberó en caso de error.

**Éxito**: Devolver 0 con `*sb` apuntando a un buffer válido indica que el análisis se completó correctamente. El llamador es ahora responsable del buffer y debe liberarlo en algún momento con `sbuf_delete()`.

##### Resumen del lenguaje de patrones

El analizador admite varios lenguajes de patrones, cada uno optimizado para diferentes casos de uso:

| Comando   | Propósito            | Ejemplo | Resultado                |
| --------- | -------------------- | ------- | ------------------------ |
| 0, 1      | Estado estático      | "1"     | LED encendido fijo       |
| f[1-9]    | Parpadeo simétrico   | "f"     | Parpadeo rápido          |
| d[digits] | Conteo por destellos | "d42"   | 4 destellos, 2 destellos |
| m[morse]  | Código Morse         | "msos"  | ... --- ...              |

Esta variedad permite a los usuarios expresar su intención de forma natural sin tener que memorizar la sintaxis de los códigos de temporización. El manejador de escritura acepta comandos sencillos, el analizador los expande a secuencias de temporización precisas y el temporizador ejecuta dichas secuencias.

#### 5.1) El punto de entrada de escritura: `echo "cmd" > /dev/led/<name>`

El espacio de usuario **escribe una cadena de comando** en el dispositivo. El driver la analiza y actualiza el estado del LED. La **estructura** es exactamente la que escribirás más adelante: pasar el buffer de usuario con `uiomove()`, analizarlo y actualizar el softc bajo un lock.

```c
212: static int
213: led_write(struct cdev *dev, struct uio *uio, int ioflag)
214: {
215: 	struct ledsc	*sc;
216: 	char *s;
217: 	struct sbuf *sb = NULL;
218: 	int error, state = 0;
220: 	if (uio->uio_resid > 512)
221: 		return (EINVAL);
222: 	s = malloc(uio->uio_resid + 1, M_DEVBUF, M_WAITOK);
223: 	s[uio->uio_resid] = '\0';
224: 	error = uiomove(s, uio->uio_resid, uio);
225: 	if (error) { free(s, M_DEVBUF); return (error); }
226: 	/* parse  ->  (sb pattern) or (state only) */
227: 	error = led_parse(s, &sb, &state);
228: 	free(s, M_DEVBUF);
229: 	if (error) return (error);
230: 	mtx_lock(&led_mtx);
231: 	sc = dev->si_drv1;
232: 	if (sc != NULL)
233: 		error = led_state(sc, &sb, state);
234: 	mtx_unlock(&led_mtx);
235: 	if (sb != NULL) sbuf_delete(sb);
236: 	return (error);
237: }
```

##### Manejador de escritura: interfaz de comando de usuario

La función `led_write` implementa la operación de escritura del dispositivo de caracteres para los dispositivos `/dev/led/*`. Cuando un usuario escribe un comando de patrón como "f" o "m...---..." en un nodo de dispositivo LED, esta función copia los datos desde el espacio de usuario, los analiza en un formato interno e instala el nuevo patrón del LED.

##### Validación de tamaño y asignación de buffer

```c
static int
led_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct ledsc    *sc;
    char *s;
    struct sbuf *sb = NULL;
    int error, state = 0;

    if (uio->uio_resid > 512)
        return (EINVAL);
    s = malloc(uio->uio_resid + 1, M_DEVBUF, M_WAITOK);
    s[uio->uio_resid] = '\0';
```

**Aplicación del límite de tamaño**: La comprobación `uio->uio_resid > 512` rechaza escrituras mayores de 512 bytes. Los patrones de LED son comandos de texto breves; incluso los mensajes de código Morse más complejos raramente superan unas pocas decenas de caracteres. Este límite evita el agotamiento de memoria provocado por programas malintencionados o defectuosos que intenten escribir varios megabytes.

Devolver `EINVAL` señala al espacio de usuario que el argumento no es válido. La escritura falla de inmediato sin asignar memoria ni modificar el estado del LED.

**Asignación de buffer temporal**: A diferencia de `null_write` del null driver, que nunca accede a los datos del usuario, el driver del LED debe examinar los bytes escritos para analizar los comandos. La asignación reserva `uio->uio_resid + 1` bytes, el tamaño exacto de la escritura más un byte para el terminador nulo.

El tipo de asignación `M_DEVBUF` es genérico para buffers temporales de drivers de dispositivo. El flag `M_WAITOK` permite que la asignación duerma si la memoria no está disponible temporalmente, lo cual es aceptable ya que se trata de una operación de escritura bloqueante sin requisitos estrictos de latencia.

**Terminador nulo**: Asignar `s[uio->uio_resid] = '\0'` garantiza que el buffer sea una cadena C válida. La llamada a `uiomove` rellenará los primeros `uio->uio_resid` bytes con los datos del usuario, y esta asignación añade el terminador justo después. Las funciones de cadena utilizadas en el análisis requieren cadenas terminadas en nulo.

##### Copia de datos desde el espacio de usuario

```c
error = uiomove(s, uio->uio_resid, uio);
if (error) { free(s, M_DEVBUF); return (error); }
```

La función `uiomove` transfiere `uio->uio_resid` bytes del buffer del usuario (descrito por `uio`) al buffer del kernel `s`. Es la misma función utilizada en los drivers null y zero para la transferencia de datos entre espacios de direcciones.

**Gestión de errores**: Si `uiomove` falla (habitualmente con `EFAULT` por un puntero de usuario no válido), el buffer asignado se libera de inmediato con `free(s, M_DEVBUF)` y el error se propaga al espacio de usuario. La escritura falla sin modificar el estado del LED, y el buffer temporal no genera una fuga de memoria.

Esta disciplina de limpieza es fundamental: el código del kernel debe liberar la memoria asignada en todos los caminos de error, no solo en el de éxito.

##### Análisis del comando

```c
/* parse  ->  (sb pattern) or (state only) */
error = led_parse(s, &sb, &state);
free(s, M_DEVBUF);
if (error) return (error);
```

**Conversión al formato interno**: La función `led_parse` interpreta la cadena de comando del usuario, produciendo:

- Un buffer de cadena (`sb`) que contiene los códigos de temporización para el modo de patrón
- Un valor de estado (0 o 1) para el modo estático on/off

El analizador determina el modo según el primer carácter del comando. Los comandos como "f", "d" o "m" generan patrones; los comandos "0" y "1" establecen un estado estático.

**Limpieza inmediata**: El buffer temporal `s` ya no es necesario tras el análisis, independientemente de si este tuvo éxito o falló; la cadena de comando original ya no se necesita. Liberarlo de inmediato en lugar de esperar al final de la función reduce el consumo de memoria en el caso habitual en que el análisis tiene éxito y continúa el procesamiento.

**Propagación de errores**: Si el análisis falla (comando no reconocido, agotamiento de memoria o patrón vacío), el error se devuelve al espacio de usuario. La operación de escritura falla antes de adquirir locks o modificar el estado del LED. El usuario verá que la escritura falla con `errno` establecido al código de error del analizador (habitualmente `EINVAL` por sintaxis incorrecta o `ENOMEM` por agotamiento de recursos).

##### Instalación del nuevo estado

```c
mtx_lock(&led_mtx);
sc = dev->si_drv1;
if (sc != NULL)
    error = led_state(sc, &sb, state);
mtx_unlock(&led_mtx);
```

**Adquisición del lock**: El mutex `led_mtx` protege la lista de LEDs y el estado por LED frente a modificaciones concurrentes. Múltiples threads podrían escribir en distintos LEDs simultáneamente, o una escritura podría competir con los callbacks del temporizador que actualizan los patrones de parpadeo. El mutex serializa estas operaciones.

**Obtención del contexto del LED**: `dev->si_drv1` proporciona la estructura `ledsc` para este dispositivo, establecida durante `led_create()`. Este puntero vincula el nodo del dispositivo de caracteres con su estado de LED.

**Comprobación defensiva de NULL**: La condición `if (sc != NULL)` protege frente a una condición de carrera en la que el LED se destruye mientras hay una escritura en curso. Si `led_destroy()` ha borrado `si_drv1` pero el manejador de escritura sigue ejecutándose, esta comprobación evita desreferenciar NULL. En la práctica, un conteo de referencias adecuado hace que esto sea poco probable, pero las comprobaciones defensivas previenen los kernel panics.

**Instalación del estado**: `led_state(sc, &sb, state)` instala el nuevo patrón o estado estático. Esta función:

- Intercambia el nuevo buffer de patrón con el anterior
- Actualiza el contador `blinkers` si el LED cambia entre activo e inactivo
- Llama al callback del driver de hardware para los cambios de estado estático
- Devuelve el buffer de patrón anterior mediante el puntero `sb`

**Liberación del lock**: Una vez completada la instalación del estado, se libera el mutex. Los demás threads bloqueados en operaciones de LED pueden continuar ahora. El tiempo de retención del lock es mínimo: únicamente el intercambio de estado y la actualización del contador, no el análisis potencialmente lento que se realizó antes.

##### Limpieza y retorno

```c
if (sb != NULL) sbuf_delete(sb);
return (error);
```

**Liberación del patrón anterior**: Después de que `led_state` retorne, `sb` apunta al buffer del patrón anterior (o NULL si no existía ningún patrón previo). El código debe liberar este buffer para evitar fugas de memoria. Cada instalación de patrón genera un buffer del patrón anterior que debe liberarse.

La comprobación `if (sb != NULL)` cubre tanto la instalación inicial del patrón (sin patrón previo) como los comandos de estado estático (el analizador nunca asignó un buffer). Solo los buffers de patrón reales deben eliminarse.

**Retorno de éxito**: Devolver `error` (habitualmente 0 en caso de éxito) completa la operación de escritura. La llamada `write(2)` en el espacio de usuario devuelve el número de bytes escritos (el valor original de `uio->uio_resid`), indicando que la operación se completó con éxito.

##### Flujo completo de escritura

La secuencia completa desde la escritura en el espacio de usuario hasta el cambio de estado del LED (el flujo siguiente usa un dispositivo teórico a modo ilustrativo):

```text
User: echo "f" > /dev/led/disk0
     -> 
led_write() called by kernel
     -> 
Validate size (< 512 bytes)
     -> 
Allocate temporary buffer
     -> 
Copy "f\n" from userspace
     -> 
Parse "f"  ->  timing code "Aa"
     -> 
Free temporary buffer
     -> 
Lock led_mtx
     -> 
Find LED via dev->si_drv1
     -> 
Install new pattern "Aa"
     -> 
Increment blinkers (0 -> 1)
     -> 
Schedule timer if needed
     -> 
Unlock led_mtx
     -> 
Free old pattern (NULL)
     -> 
Return success
     -> 
User: write() returns 2 bytes
```

En el siguiente tick del temporizador (0.1 segundos después), el LED comienza a parpadear a ~2.5Hz, alternando entre on y off cada 0.1 segundos.

##### Caminos de gestión de errores

La función tiene múltiples salidas de error, cada una con la limpieza adecuada:

**Fallo en la validación de tamaño**:

```text
Check uio_resid > 512  ->  return EINVAL
(nothing allocated yet, no cleanup needed)
```

**Fallo en la asignación**:

```text
malloc() returns NULL  ->  kernel panics (M_WAITOK)
(M_WAITOK means "wait for memory, never fail")
```

**Fallo en la copia de entrada**:

```text
uiomove() fails  ->  free(s)  ->  return EFAULT
(temporary buffer freed, no other resources allocated)
```

**Fallo en el análisis**:

```text
led_parse() fails  ->  free(s)  ->  return EINVAL
(temporary buffer freed, no string buffer created)
```

**Instalación de estado correcta**:

```text
led_state() succeeds  ->  sbuf_delete(old)  ->  return 0
(old pattern freed, new pattern installed)
```

Cada ruta de error libera todos los recursos asignados, evitando fugas de memoria independientemente del punto donde se produzca el fallo.

##### Contraste con null.c

El manejador `null_write` del driver null era trivial: establecer `uio_resid = 0` y retornar. El manejador de escritura del driver LED es sustancialmente más complejo por los siguientes motivos:

**La entrada del usuario requiere análisis sintáctico**: Comandos como "f" y "m..." deben interpretarse, no simplemente descartarse.

**El estado debe modificarse**: Los nuevos patrones afectan al comportamiento del LED, lo que exige coordinación con los callbacks del temporizador.

**La memoria debe gestionarse**: Los buffers se asignan, intercambian y liberan a través de los límites de las funciones.

**Se requiere sincronización**: Múltiples escritores y callbacks del temporizador deben coordinarse mediante mutexes.

Esta mayor complejidad refleja el papel del driver LED como infraestructura que soporta una interacción rica del usuario con el hardware físico, y no como un simple sumidero de datos.

#### 5.2) API del kernel: control programático de LEDs

```c
240: int
241: led_set(char const *name, char const *cmd)
...
247: 	error = led_parse(cmd, &sb, &state);
...
251: 	LIST_FOREACH(sc, &led_list, list) {
252: 		if (strcmp(sc->name, name) == 0) break;
253: 	}
254: 	if (sc != NULL) error = led_state(sc, &sb, state);
255: 	else error = ENOENT;
```

La función `led_set` proporciona una API orientada al kernel que permite a otro código del kernel controlar los LEDs sin pasar por la interfaz del dispositivo de caracteres. Esto permite que drivers, subsistemas del kernel y manejadores de eventos del sistema manipulen los LEDs directamente usando el mismo lenguaje de patrones disponible para el espacio de usuario.

##### Firma y propósito de la función

```c
int
led_set(char const *name, char const *cmd)
```

**Parámetros**: La función recibe dos cadenas:

- `name`: el identificador del LED, que coincide con el nombre empleado al llamar a `led_create()` (por ejemplo, "disk0", "power")
- `cmd`: la cadena de comandos de patrón, con la misma sintaxis que las escrituras desde el espacio de usuario (por ejemplo, "f", "1", "m...---...")

**Valor de retorno**: Cero en caso de éxito, o un valor errno en caso de fallo (`EINVAL` para errores de análisis sintáctico, `ENOENT` para nombres de LED desconocidos, `ENOMEM` para fallos de asignación de memoria).

**Casos de uso**: El código del kernel puede llamar a esta función para:

- Indicar actividad en disco: `led_set("disk0", "f")` para parpadear durante operaciones de I/O
- Mostrar el estado del sistema: `led_set("power", "1")` para encender el LED de alimentación al finalizar el boot
- Señalizar condiciones de error: `led_set("status", "m...---...")` para emitir el patrón SOS
- Implementar un latido del sistema: `led_set("heartbeat", "Uu")` para alternar a 1 Hz y demostrar que el sistema está activo

##### Análisis sintáctico del comando

```c
error = led_parse(cmd, &sb, &state);
```

La función reutiliza el mismo analizador sintáctico que el manejador de escritura. Las cadenas de patrón se interpretan de forma idéntica tanto si provienen del espacio de usuario mediante `write(2)` como si proceden de código del kernel a través de `led_set()`.

Esta reutilización de código garantiza la coherencia: un comando que funciona en un contexto funciona en el otro. El analizador se encarga de toda la complejidad de expandir "f" a "Aa" o "m..." a "aA", de modo que el código del kernel que invoca la función no necesita comprender el formato interno de codificación de tiempos.

Si el análisis falla (sintaxis de comando incorrecta, agotamiento de memoria), el error se registra en la variable `error` y se comprueba más adelante. La función continúa adquiriendo el lock incluso en caso de fallo en el análisis, porque debe mantener el lock para retornar de forma segura sin provocar fugas del buffer.

##### Localización del LED por nombre

```c
LIST_FOREACH(sc, &led_list, list) {
    if (strcmp(sc->name, name) == 0) break;
}
if (sc != NULL) error = led_state(sc, &sb, state);
else error = ENOENT;
```

**Búsqueda lineal**: La macro `LIST_FOREACH` recorre la lista global de LEDs, comparando el nombre de cada LED con el nombre solicitado mediante `strcmp()`. El bucle termina anticipadamente con `break` cuando se encuentra una coincidencia, dejando `sc` apuntando al LED correspondiente.

**¿Por qué búsqueda lineal?**: Para listas pequeñas (generalmente de 5 a 20 LEDs por sistema), la búsqueda lineal es más rápida que el coste adicional de una tabla hash. La simplicidad del código y el acceso secuencial favorable para la caché superan la complejidad O(n). Los sistemas con cientos de LEDs se beneficiarían de una tabla hash, pero tales sistemas son raros.

**Gestión del caso no encontrado**: Si el bucle termina sin haberse interrumpido, significa que ningún LED coincide con el nombre y `sc` permanece NULL (según la inicialización de `LIST_FOREACH`). Asignar `error = ENOENT` (no existe ese archivo o directorio) indica que el LED solicitado no existe.

**Instalación del estado**: Cuando se encuentra una coincidencia (`sc != NULL`), se llama a `led_state()` para instalar el nuevo patrón o estado estático, usando la misma función de instalación de estado que el manejador de escritura. El valor de retorno sobreescribe cualquier error de análisis previo: si el análisis tuvo éxito pero la instalación del estado falla, el error de instalación tiene prioridad.

##### Código crítico omitido en el fragmento

El fragmento proporcionado omite varias líneas críticas visibles en la función completa:

**Adquisición del lock** (antes del bucle `LIST_FOREACH` en `led_set`):

```c
mtx_lock(&led_mtx);
```

La lista de LEDs debe protegerse con el lock antes de recorrerla para evitar modificaciones concurrentes. Si un thread busca en la lista mientras otro destruye un LED, la búsqueda podría acceder a memoria ya liberada. El mutex serializa el acceso a la lista.

**Liberación del lock y limpieza** (después de la llamada de instalación de estado en `led_set`):

```c
mtx_unlock(&led_mtx);
if (sb != NULL)
    sbuf_delete(sb);
return (error);
```

Tras el intento de instalación del estado, el mutex se libera y el antiguo buffer de patrón (devuelto a través de `sb` por `led_state()`) se libera. Esta limpieza sigue el mismo patrón de gestión de buffers que el manejador de escritura.

##### Comparación con el manejador de escritura

Tanto `led_write` como `led_set` siguen el mismo patrón:

```text
Parse command  ->  Acquire lock  ->  Find LED  ->  Install state  ->  Release lock  ->  Cleanup
```

Las diferencias clave son:

| Aspecto                  | led_write                         | led_set                                              |
| ------------------------ | --------------------------------- | ---------------------------------------------------- |
| Invocado por             | Espacio de usuario vía `write(2)` | Código del kernel                                    |
| Origen de la entrada     | Estructura uio                    | Punteros de cadena directos                          |
| Identificación del LED   | `dev->si_drv1`                    | Búsqueda por nombre                                  |
| Validación de tamaño     | Límite de 512 bytes               | Sin límite explícito (responsabilidad del invocador) |
| Notificación de errores  | errno al espacio de usuario       | Valor de retorno al invocador                        |

El manejador de escritura utiliza el puntero del dispositivo para encontrar el LED directamente (un dispositivo, un LED). La API del kernel emplea búsqueda por nombre para permitir la selección arbitraria de cualquier LED desde cualquier contexto del kernel.

##### Ejemplos de patrones de uso

**Driver de disco indicando actividad**:

```c
void
disk_start_io(struct disk_softc *sc)
{
    /* Begin I/O operation */
    led_set(sc->led_name, "f");  // Start blinking
}

void
disk_complete_io(struct disk_softc *sc)
{
    /* I/O completed */
    led_set(sc->led_name, "0");  // Turn off
}
```

**Secuencia de inicialización del sistema**:

```c
void
system_boot_complete(void)
{
    led_set("power", "1");      // Solid on: system ready
    led_set("status", "0");     // Off: no errors
    led_set("heartbeat", "Uu"); // 1Hz toggle: alive
}
```

**Indicación de error**:

```c
void
critical_error_handler(int error_code)
{
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "d%d", error_code);
    led_set("status", pattern);  // Flash error code
}
```

##### Seguridad con threads

La función es thread-safe gracias a la protección mediante mutex. Múltiples threads pueden llamar a `led_set()` de forma concurrente:

**Escenario**: El thread A establece "disk0" a "f" mientras el thread B establece "power" a "1".

```text
Thread A                    Thread B
Parse "f"  ->  "Aa"            Parse "1"  ->  state=1
Lock led_mtx                (blocks on lock)
Find "disk0"                ...
Install pattern             ...
Unlock led_mtx              Acquire lock
Delete old buffer           Find "power"
Return                      Install state
                            Unlock led_mtx
                            Delete old buffer
                            Return
```

El mutex serializa el recorrido de la lista y la modificación del estado, evitando la corrupción. Ambas operaciones se completan con éxito sin interferencias.

##### Gestión de errores

La función puede fallar de varias formas:

**Error de análisis sintáctico**:

```c
led_set("disk0", "invalid")  // Returns EINVAL
```

**LED no encontrado**:

```c
led_set("nonexistent", "f")  // Returns ENOENT
```

**Agotamiento de memoria**:

```c
led_set("disk0", "m..." /* very long morse */)  // Returns ENOMEM
```

El código del kernel que invoque esta función debe comprobar el valor de retorno y gestionar los errores de forma adecuada, aunque en la práctica los fallos en el control de LEDs raramente son fatales: el sistema continúa funcionando, simplemente sin indicadores visuales.

##### Por qué existen ambas APIs

La interfaz dual (dispositivo de caracteres + API del kernel) atiende necesidades diferentes:

**Dispositivo de caracteres** (`/dev/led/*`):

- Scripts y programas de usuario
- Administradores del sistema
- Pruebas y depuración
- Control interactivo

**API del kernel** (`led_set()`):

- Respuestas automáticas a eventos
- Indicadores integrados en drivers
- Visualización del estado del sistema
- Rutas de rendimiento crítico (sin el coste adicional de una llamada al sistema)

Este patrón de exponer funcionalidad tanto a través de dispositivos en el espacio de usuario como de APIs del kernel está presente en todo FreeBSD. El subsistema de LEDs ofrece un ejemplo claro de cómo estructurar dichos servicios con doble interfaz.

#### 6) Integración con devfs y exportación del método de escritura

```c
272: static struct cdevsw led_cdevsw = {
273: 	.d_version =	D_VERSION,
274: 	.d_write =	led_write,
275: 	.d_name =	"LED",
276: };
```

##### Tabla de operaciones del dispositivo de caracteres

La estructura `led_cdevsw` define las operaciones del dispositivo de caracteres para todos los nodos de dispositivo LED. A diferencia del driver null, que tenía tres estructuras `cdevsw` separadas para tres dispositivos, el driver LED utiliza una única `cdevsw` compartida por todos los dispositivos `/dev/led/*` creados dinámicamente.

##### Definición de la estructura

```c
static struct cdevsw led_cdevsw = {
    .d_version =    D_VERSION,
    .d_write =      led_write,
    .d_name =       "LED",
};
```

**`d_version = D_VERSION`**: El campo de versión obligatorio garantiza la compatibilidad binaria entre el driver y el framework de dispositivos del kernel. Todas las estructuras `cdevsw` deben incluir este campo.

**`d_write = led_write`**: La única operación definida explícitamente. Cuando el espacio de usuario llama a `write(2)` en cualquier dispositivo `/dev/led/*`, el kernel invoca esta función. El manejador `led_write` analiza los comandos de patrón y actualiza el estado del LED.

**`d_name = "LED"`**: El nombre de clase del dispositivo que aparece en los mensajes del kernel y en la contabilización. Esta cadena identifica el tipo de driver, aunque los dispositivos individuales tienen sus propios nombres específicos (como "disk0" o "power").

##### Conjunto mínimo de operaciones

Observa lo que **no** está definido:

**Sin `d_read`**: Los LEDs son dispositivos solo de salida. Leer de `/dev/led/disk0` carece de sentido: no hay estado que consultar ni datos que recuperar. Omitir `d_read` hace que los intentos de lectura fallen con `ENODEV` (operación no soportada por el dispositivo).

**Sin `d_open` / `d_close`**: Los dispositivos LED no requieren inicialización ni limpieza por apertura. Múltiples procesos pueden escribir en el mismo LED simultáneamente (serializados por el mutex), y cerrar el dispositivo no requiere desmontaje de estado. Los manejadores por defecto del kernel son suficientes.

**Sin `d_ioctl`**: A diferencia del driver null, que soportaba ioctls de terminal, los dispositivos LED no tienen operaciones de control más allá de escribir patrones. Toda la configuración se realiza a través de la interfaz de escritura.

**Sin `d_poll` / `d_kqfilter`**: Los LEDs son solo de escritura, por lo que no hay ninguna condición que esperar. Consultar la disponibilidad para escritura siempre devolvería "listo", ya que las escrituras nunca se bloquean (más allá de la adquisición del mutex), lo que hace inútil el soporte de poll.

Este minimalismo contrasta con la interfaz más completa del driver null (que incluía manejadores de ioctl) y demuestra que las estructuras `cdevsw` solo necesitan proporcionar las operaciones que tienen sentido para el tipo de dispositivo.

##### Compartida entre dispositivos

Una diferencia clave respecto al driver null: esta **única** `cdevsw` atiende a **todos** los dispositivos LED. Cuando el sistema tiene tres LEDs registrados:

```text
/dev/led/disk0   ->  led_cdevsw
/dev/led/power   ->  led_cdevsw
/dev/led/status  ->  led_cdevsw
```

Los tres nodos de dispositivo comparten la misma tabla de punteros de función. La función `led_write` determina en qué LED se está escribiendo examinando `dev->si_drv1`, que apunta a la estructura `ledsc` del LED específico.

Este uso compartido es posible porque:

- Todos los LEDs soportan las mismas operaciones (comandos de escritura de patrones)
- El estado por dispositivo se accede a través de `si_drv1`, no mediante funciones diferentes
- La misma lógica de análisis e instalación de estado se aplica a todos los LEDs

##### Contraste con null.c

El driver null definía tres estructuras `cdevsw` separadas:

```c
static struct cdevsw full_cdevsw = { ... };
static struct cdevsw null_cdevsw = { ... };
static struct cdevsw zero_cdevsw = { ... };
```

Cada una tenía asignaciones de función diferentes porque los dispositivos tenían comportamientos distintos (`full_write` frente a `null_write`, `nullop` frente a `zero_read`). Los dispositivos eran tipos fundamentalmente diferentes.

Los dispositivos del driver LED son todos del mismo tipo: son LEDs que aceptan comandos de patrón. Las únicas diferencias son:

- El nombre del dispositivo ("disk0" frente a "power")
- El callback de control de hardware (diferente para cada LED físico)
- El estado de patrón actual (independiente por LED)

Estas diferencias se almacenan en estructuras `ledsc` por dispositivo, no se codifican en tablas de funciones separadas. Este diseño escala de forma elegante: registrar 100 LEDs no requiere 100 estructuras `cdevsw`, sino solo 100 instancias de `ledsc` que comparten una única `cdevsw`.

##### Uso en la creación de dispositivos

Cuando un driver de hardware llama a `led_create()`, el código crea un nodo de dispositivo:

```c
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
```

El parámetro `&led_cdevsw` proporciona la tabla de despacho de funciones. Todos los dispositivos creados referencian la misma estructura: `make_dev()` no la copia, solo almacena el puntero. Esto significa que:

- El coste de memoria por dispositivo para la tabla de funciones es nulo
- Los cambios en `led_write` (durante el desarrollo) afectan automáticamente a todos los dispositivos
- La `cdevsw` debe permanecer válida durante toda la vida del sistema (de ahí el almacenamiento `static`)

##### Identificación del dispositivo

Si todos los dispositivos comparten una única `cdevsw`, ¿cómo distingue `led_write` en qué LED se está escribiendo? El enlace del dispositivo:

```c
// In led_create():
sc->dev = make_dev(&led_cdevsw, ...);
sc->dev->si_drv1 = sc;  // Link device to its ledsc

// In led_write():
sc = dev->si_drv1;       // Retrieve the ledsc
```

El campo `si_drv1` (establecido durante `led_create()`) crea un puntero por dispositivo hacia la estructura `ledsc` única. Aunque todos los dispositivos comparten el mismo `cdevsw` y, por tanto, la misma función `led_write`, cada invocación recibe un parámetro `dev` distinto, que proporciona acceso al estado específico del dispositivo a través de `si_drv1`.

Este patrón (tabla de funciones compartida, puntero de estado por dispositivo) es el enfoque estándar para los drivers que gestionan múltiples dispositivos similares. Combina eficiencia (una sola tabla de funciones) con flexibilidad (comportamiento específico por dispositivo a través del puntero de estado).

#### 7) Crear los nodos de dispositivo por LED

```c
278: struct cdev *
279: led_create(led_t *func, void *priv, char const *name)
280: {
282: 	return (led_create_state(func, priv, name, 0));
283: }
285: struct cdev *
286: led_create_state(led_t *func, void *priv, char const *name, int state)
287: {
288: 	struct ledsc	*sc;
290: 	sc = malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);
292: 	sx_xlock(&led_sx);
293: 	sc->name = strdup(name, M_LED);
294: 	sc->unit = alloc_unr(led_unit);
295: 	sc->private = priv;
296: 	sc->func = func;
297: 	sc->dev = make_dev(&led_cdevsw, sc->unit,
298: 	    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
299: 	sx_xunlock(&led_sx);
301: 	mtx_lock(&led_mtx);
302: 	sc->dev->si_drv1 = sc;
303: 	LIST_INSERT_HEAD(&led_list, sc, list);
304: 	if (state != -1)
305: 		sc->func(sc->private, state != 0);
306: 	mtx_unlock(&led_mtx);
308: 	return (sc->dev);
309: }
```

##### Registro de LED: creación de dispositivos dinámicos

Las funciones `led_create` y `led_create_state` forman la API pública que los drivers de hardware utilizan para registrar LEDs en el subsistema. Estas funciones asignan recursos, crean nodos de dispositivo e integran el LED en el registro global, haciéndolo accesible tanto para el código en espacio de usuario como para el código del kernel.

##### Envoltorio de registro simplificado

```c
struct cdev *
led_create(led_t *func, void *priv, char const *name)
{
    return (led_create_state(func, priv, name, 0));
}
```

La función `led_create` proporciona una interfaz simplificada para el caso habitual en que el estado inicial del LED no es relevante. Delega en `led_create_state` con un estado inicial de 0 (apagado), lo que permite a los drivers de hardware registrar LEDs con el mínimo de código:

```c
struct cdev *led;
led = led_create(my_led_callback, my_softc, "disk0");
```

Este envoltorio de conveniencia sigue el patrón de FreeBSD de ofrecer versiones simples y versiones completas de la misma API.

##### Función de registro completa

```c
struct cdev *
led_create_state(led_t *func, void *priv, char const *name, int state)
{
    struct ledsc    *sc;
```

**Parámetros**: La función recibe cuatro valores:

- `func` - función de callback que controla el hardware físico del LED
- `priv` - puntero opaco que se pasa al callback, normalmente el softc del driver
- `name` - cadena que identifica el LED; se convierte en parte de `/dev/led/name`
- `state` - estado inicial del LED: 0 (apagado), 1 (encendido) o -1 (no inicializar)

**Valor de retorno**: Puntero a la estructura `struct cdev` creada, que el driver de hardware debe guardar para su posterior uso con `led_destroy()`. Si la creación falla, se produce un pánico en el kernel (debido a la asignación con `M_WAITOK`) en lugar de devolver NULL.

##### Asignación del estado del LED

```c
sc = malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);
```

La estructura softc se asigna para hacer seguimiento del estado de este LED. El flag `M_ZERO` pone a cero todos los campos, proporcionando valores seguros por defecto:

- Los campos de tipo puntero (name, dev, spec, str, ptr) son NULL
- Los campos numéricos (unit, count) son cero
- La entrada `list` se pone a cero (será inicializada por `LIST_INSERT_HEAD`)

El flag `M_WAITOK` significa que la asignación puede dormir esperando memoria, lo cual es aceptable ya que el registro del LED ocurre durante el attach del driver (un contexto bloqueante). Si la memoria está realmente agotada, el kernel entra en pánico: el registro del LED se considera lo suficientemente esencial como para que un fallo no sea recuperable.

##### Creación del dispositivo bajo lock exclusivo

```c
sx_xlock(&led_sx);
sc->name = strdup(name, M_LED);
sc->unit = alloc_unr(led_unit);
sc->private = priv;
sc->func = func;
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
sx_xunlock(&led_sx);
```

**Adquisición del lock exclusivo**: La llamada a `sx_xlock` adquiere el lock compartido/exclusivo en modo exclusivo (escritura). Esto serializa todas las operaciones de creación y destrucción de dispositivos, evitando condiciones de carrera en las que dos threads podrían crear dispositivos con el mismo nombre o asignar el mismo número de unidad simultáneamente.

**Duplicación del nombre**: `strdup(name, M_LED)` asigna una copia de la cadena del nombre. La cadena del invocador puede ser temporal (un buffer en la pila o un literal de cadena), por lo que se necesita una copia persistente durante toda la vida del LED. Esta copia se liberará en `led_destroy()`.

**Asignación del número de unidad**: `alloc_unr(led_unit)` obtiene un número de unidad único del pool global. Este número se convierte en el número menor del dispositivo, garantizando que `/dev/led/disk0` y `/dev/led/power` tengan identificadores de dispositivo distintos aunque compartan el mismo número mayor.

**Registro del callback**: Los campos `private` y `func` se copian de los parámetros, estableciendo la conexión con la función de control del driver de hardware. Cuando el estado del LED cambia (mediante la ejecución de un patrón o un comando de estado estático), se llamará a `sc->func(sc->private, onoff)` para manipular el hardware físico.

**Creación del nodo de dispositivo**: `make_dev` crea `/dev/led/name` con las siguientes propiedades:

- `&led_cdevsw` - operaciones compartidas del dispositivo de caracteres (manejador de escritura)
- `sc->unit` - número menor único para este LED
- `UID_ROOT, GID_WHEEL` - propiedad de root:wheel
- `0600` - lectura/escritura solo para el propietario (root), sin acceso para otros
- `"led/%s", name` - ruta del dispositivo; el sistema antepone `/dev/` automáticamente

Los permisos restrictivos (`0600`) impiden que usuarios sin privilegios controlen los LEDs, lo que podría suponer un problema de seguridad (filtración de información a través de patrones de LED) o una molestia (hacer parpadear el LED de alimentación rápidamente).

**Liberación del lock**: Tras completar la creación del dispositivo, se libera el lock exclusivo. Otros threads pueden ahora crear o destruir LEDs. El tiempo de retención del lock es mínimo (solo la asignación y el registro esenciales), sin incluir la asignación previa del softc, que no necesitaba protección.

##### Integración bajo mutex

```c
mtx_lock(&led_mtx);
sc->dev->si_drv1 = sc;
LIST_INSERT_HEAD(&led_list, sc, list);
if (state != -1)
    sc->func(sc->private, state != 0);
mtx_unlock(&led_mtx);
```

**Adquisición del mutex**: El mutex `led_mtx` protege la lista de LEDs y el estado relacionado con el temporizador. Se adquiere después de la creación del dispositivo porque el uso de múltiples locks con diferentes propósitos reduce la contención: los threads que crean dispositivos no bloquean a los threads que modifican estados de LEDs.

**Enlace bidireccional**: Establecer `sc->dev->si_drv1 = sc` crea el vínculo fundamental entre el nodo de dispositivo y el softc. Cuando se llama a `led_write` con este dispositivo, puede recuperar el softc mediante `dev->si_drv1`. Este vínculo debe establecerse antes de que el dispositivo sea utilizable.

**Inserción en la lista**: `LIST_INSERT_HEAD(&led_list, sc, list)` añade el LED al registro global en la cabeza de la lista. El campo `list` del softc se puso a cero durante la asignación, y esta macro lo inicializa correctamente al enlazarlo con la lista existente.

El uso de `LIST_INSERT_HEAD` en lugar de `LIST_INSERT_TAIL` es arbitrario; el orden no importa para la iteración de la lista de LEDs. La inserción por la cabeza es ligeramente más rápida (no es necesario encontrar el final), pero la diferencia de rendimiento es despreciable.

**Estado inicial opcional**: Si `state != -1`, el callback de hardware se invoca de inmediato para establecer el estado inicial del LED:

- `state != 0` convierte cualquier valor no nulo en verdadero booleano (LED encendido)
- `state == 0` significa LED apagado

El valor especial -1 significa «no inicializar», dejando el LED en el estado que establezca el hardware por defecto. Esto es útil cuando el driver de hardware ya ha configurado el LED antes del registro.

**Liberación del lock**: Tras la inserción en la lista y la inicialización opcional, se libera el mutex. El LED está ahora completamente operativo: el espacio de usuario puede escribir en su nodo de dispositivo, el código del kernel puede llamar a `led_set()` con su nombre, y los callbacks del temporizador procesarán cualquier patrón.

##### Valor de retorno y propiedad

```c
return (sc->dev);
}
```

La función devuelve el puntero `cdev`, que el driver de hardware debe guardar:

```c
struct my_driver_softc {
    struct cdev *led_dev;
    /* other fields */
};

void
my_driver_attach(device_t dev)
{
    struct my_driver_softc *sc = device_get_softc(dev);
    /* other initialization */
    sc->led_dev = led_create(my_led_callback, sc, "disk0");
}
```

El driver de hardware necesita este puntero para llamar a `led_destroy()` durante el detach. Sin guardarlo, el LED quedaría sin liberar: su nodo de dispositivo y sus recursos persistirían incluso después de que el driver de hardware se descargue.

##### Resumen de la asignación de recursos

Un registro de LED exitoso asigna:

- Estructura softc (liberada en `led_destroy`)
- Copia de la cadena del nombre (liberada en `led_destroy`)
- Número de unidad (devuelto al pool en `led_destroy`)
- Nodo de dispositivo (destruido en `led_destroy`)

Todos los recursos se limpian de forma simétrica durante la destrucción, evitando fugas cuando el hardware es extraído.

##### Seguridad entre threads

El diseño de dos locks permite operaciones concurrentes seguras:

**Escenario**: El thread A crea «disk0» mientras el thread B crea «power».

```text
Thread A                    Thread B
Allocate sc1                Allocate sc2
Lock led_sx (exclusive)     (blocks on led_sx)
Create /dev/led/disk0       ...
Unlock led_sx               Acquire led_sx
Lock led_mtx                Create /dev/led/power
Insert sc1 to list          Unlock led_sx
Unlock led_mtx              Lock led_mtx
                            Insert sc2 to list
                            Unlock led_mtx
```

El lock exclusivo serializa la creación de dispositivos (evitando conflictos de nombre y número de unidad), mientras que el mutex serializa la modificación de la lista (evitando la corrupción de la lista). Ambos threads completan con éxito, con dos LEDs en funcionamiento.

##### Contraste con null.c

La creación del dispositivo en el driver null ocurrió en `null_modevent` durante la carga del módulo:

```c
// null.c: static devices created once
full_dev = make_dev_credf(..., "full");
null_dev = make_dev_credf(..., "null");
zero_dev = make_dev_credf(..., "zero");
```

La creación del dispositivo en el driver de LED ocurre de forma dinámica bajo demanda:

```c
// led.c: devices created whenever hardware drivers request
led_create(func, priv, "disk0");   // called by disk driver
led_create(func, priv, "power");   // called by power driver
led_create(func, priv, "status");  // called by GPIO driver
```

Este enfoque dinámico escala de forma natural: el sistema puede tener cualquier número de LEDs (de cero a cientos), con dispositivos que aparecen y desaparecen a medida que el hardware se añade y se elimina. El subsistema proporciona la infraestructura, pero no dicta qué LEDs existen: eso lo determinan los drivers de hardware que estén cargados y el hardware presente.


#### 8) Destruir los nodos de dispositivo por LED

```c
306: void
307: led_destroy(struct cdev *dev)
308: {
309: 	struct ledsc *sc;
311: 	mtx_lock(&led_mtx);
312: 	sc = dev->si_drv1;
313: 	dev->si_drv1 = NULL;
314: 	if (sc->ptr != NULL)
315: 		blinkers--;
316: 	LIST_REMOVE(sc, list);
317: 	if (LIST_EMPTY(&led_list))
318: 		callout_stop(&led_ch);
319: 	mtx_unlock(&led_mtx);
321: 	sx_xlock(&led_sx);
322: 	free_unr(led_unit, sc->unit);
323: 	destroy_dev(dev);
324: 	if (sc->spec != NULL)
325: 		sbuf_delete(sc->spec);
326: 	free(sc->name, M_LED);
327: 	free(sc, M_LED);
328: 	sx_xunlock(&led_sx);
329: }
```

##### Cancelación de registro del LED: limpieza y liberación de recursos

La función `led_destroy` cancela el registro de un LED en el subsistema, invirtiendo todas las operaciones realizadas durante `led_create`. Los drivers de hardware llaman a esta función durante el detach para eliminar los LEDs de forma limpia antes de que el hardware subyacente desaparezca, garantizando que no queden referencias colgantes ni fugas de recursos.

##### Entrada a la función y recuperación del softc

```c
void
led_destroy(struct cdev *dev)
{
    struct ledsc *sc;

    mtx_lock(&led_mtx);
    sc = dev->si_drv1;
    dev->si_drv1 = NULL;
```

**Parámetro**: La función recibe el puntero `cdev` devuelto por `led_create`. Los drivers de hardware suelen guardar este puntero en su propio softc y pasarlo durante la limpieza:

```c
void
my_driver_detach(device_t dev)
{
    struct my_driver_softc *sc = device_get_softc(dev);
    led_destroy(sc->led_dev);
    /* other cleanup */
}
```

**Adquisición del mutex**: El mutex `led_mtx` se adquiere primero para proteger la lista de LEDs y el estado del temporizador. Esto serializa la destrucción con los callbacks del temporizador en curso y las operaciones de escritura.

**Ruptura del enlace**: Establecer `dev->si_drv1 = NULL` interrumpe de inmediato la conexión entre el nodo de dispositivo y el softc. Cualquier operación de escritura que haya comenzado antes de que se llamara a esta función pero que todavía no haya adquirido el mutex verá NULL al comprobar `dev->si_drv1` y fallará de forma segura en lugar de acceder a memoria liberada. Esta programación defensiva evita errores de uso tras liberación (use-after-free) durante operaciones concurrentes.

##### Desactivación de la ejecución de patrones

```c
if (sc->ptr != NULL)
    blinkers--;
```

Si este LED tiene un patrón de parpadeo activo (`ptr != NULL`), el contador global `blinkers` debe decrementarse. Este contador lleva la cuenta de cuántos LEDs necesitan servicio del temporizador, y eliminar un LED activo reduce ese recuento.

**Lógica de parada del temporizador**: Cuando el contador llega a cero (este era el último LED parpadeante), el callback del temporizador lo detectará y dejará de reprogramarse. Sin embargo, no hay una parada explícita del temporizador aquí; la actualización del contador es suficiente. El callback del temporizador comprueba `blinkers > 0` antes de cada reprogramación.

##### Eliminación del registro global

```c
LIST_REMOVE(sc, list);
if (LIST_EMPTY(&led_list))
    callout_stop(&led_ch);
```

**Eliminación de la lista**: `LIST_REMOVE(sc, list)` desvincula este LED de la lista global. La macro actualiza las entradas vecinas de la lista para saltarse este nodo, y los futuros callbacks del temporizador no verán este LED al iterar.

**Parada explícita del temporizador**: Si la lista queda vacía tras la eliminación, `callout_stop(&led_ch)` detiene el temporizador de forma explícita. Esto es una optimización: esperar a que el temporizador detecte `blinkers == 0` funcionaría, pero detenerlo de inmediato cuando todos los LEDs desaparecen es más eficiente.

La función `callout_stop` es segura de llamar sobre un temporizador ya detenido (no hace nada), así que la comprobación de lista vacía es solo una optimización para evitar la llamada a la función cuando no es necesaria.

**Liberación del lock**: Tras la modificación de la lista y la gestión del temporizador, se libera el mutex:

```c
mtx_unlock(&led_mtx);
```

La limpieza restante no requiere protección con mutex, ya que este LED es ahora invisible para los callbacks del temporizador y las operaciones de escritura.

##### Liberación de recursos bajo lock exclusivo

```c
sx_xlock(&led_sx);
free_unr(led_unit, sc->unit);
destroy_dev(dev);
if (sc->spec != NULL)
    sbuf_delete(sc->spec);
free(sc->name, M_LED);
free(sc, M_LED);
sx_xunlock(&led_sx);
```

**Adquisición del lock exclusivo**: El lock `led_sx` serializa la creación y destrucción de dispositivos. Adquirirlo de forma exclusiva impide que se creen nuevos dispositivos mientras este está siendo destruido, evitando condiciones de carrera en las que el número de unidad o el nombre liberado podrían reutilizarse de inmediato.

**Devolución del número de unidad**: `free_unr(led_unit, sc->unit)` devuelve el número de unidad al pool, haciéndolo disponible para futuros registros de LED. Sin esto, los números de unidad se filtrarían y eventualmente agotarían el rango disponible.

**Destrucción del nodo de dispositivo**: `destroy_dev(dev)` elimina `/dev/led/name` del sistema de archivos y libera la estructura `cdev`. Esta función bloquea hasta que todos los descriptores de archivo abiertos del dispositivo se hayan cerrado, garantizando que no haya operaciones de escritura en curso.

Después de que `destroy_dev` retorne, el dispositivo ya no existe en `/dev`, y cualquier intento futuro de abrirlo fallará con `ENOENT` (no existe el archivo o directorio).

**Limpieza del buffer de patrón**: Si existe un patrón activo (`sc->spec != NULL`), su buffer de cadena se libera con `sbuf_delete`. Esto gestiona el caso en que un LED se destruye mientras se está ejecutando un patrón de parpadeo.

**Limpieza de la cadena de nombre**: `free(sc->name, M_LED)` libera la cadena de nombre duplicada que se asignó durante `led_create`. La etiqueta de tipo `M_LED` coincide con la de la asignación, lo que mantiene la coherencia del seguimiento de memoria.

**Liberación del softc**: `free(sc, M_LED)` libera la propia estructura de estado del LED. Después de esta llamada, el puntero `sc` es inválido y no debe volver a accederse a él.

**Liberación del lock**: Se libera el lock exclusivo, lo que permite que continúen otras operaciones de dispositivo. Todos los recursos asociados a este LED han sido liberados.

##### Limpieza simétrica

La secuencia de destrucción invierte con precisión la de creación:

| Paso en la creación                       | Paso en la destrucción                    |
| ----------------------------------------- | ----------------------------------------- |
| Asignar softc                             | Liberar softc                             |
| Duplicar nombre                           | Liberar nombre                            |
| Asignar unidad                            | Liberar unidad                            |
| Crear nodo de dispositivo                 | Destruir nodo de dispositivo              |
| Insertar en la lista                      | Eliminar de la lista                      |
| Incrementar blinkers (si hay patrón)      | Decrementar blinkers (si hay patrón)      |

Esta simetría garantiza una limpieza completa sin fugas de recursos. Cada asignación tiene su correspondiente liberación, cada inserción en la lista tiene su eliminación y cada incremento tiene su decremento.

##### Gestión de LEDs activos

Si un LED se destruye mientras está parpadeando activamente, la función lo gestiona de forma limpia:

**Antes de la destrucción**:

```text
LED state: ptr = "AaAa", spec = sbuf, blinkers = 1
Timer: scheduled, will fire in 0.1s
```

**Durante la destrucción**:

```text
Mutex locked
dev->si_drv1 = NULL (breaks write path)
blinkers--  (now 0)
LIST_REMOVE (invisible to timer)
Mutex unlocked
Timer fires, sees empty list, doesn't reschedule
sbuf_delete (frees pattern)
```

**Después de la destrucción**:

```text
LED state: freed
Timer: stopped
Device: removed from /dev
```

El patrón del LED se interrumpe a mitad de ejecución, pero no se producen ni fallos ni fugas. El LED hardware queda en el estado en que estaba en el momento de la destrucción; apagarlo explícitamente es responsabilidad del driver de hardware si así se desea.

##### Consideraciones sobre la seguridad en threads

El locking en dos fases (primero el mutex, luego el lock exclusivo) previene varias condiciones de carrera:

**Condición de carrera 1: escritura frente a destrucción**

```text
Thread A (write)                Thread B (destroy)
Begin led_write()               Begin led_destroy()
                                Lock led_mtx
                                dev->si_drv1 = NULL
                                Remove from list
                                Unlock led_mtx
Lock led_mtx                    Lock led_sx
sc = dev->si_drv1 (NULL)        destroy_dev() blocks
if (sc != NULL) ... (skipped)   ...
Unlock led_mtx                  [write returns]
Return error                    destroy_dev() completes
```

La operación de escritura detecta de forma segura el LED destruido mediante la comprobación de NULL y devuelve un error sin acceder a la memoria liberada.

**Condición de carrera 2: temporizador frente a destrucción**

```text
Timer callback running          led_destroy() called
Iterating LED list              Lock led_mtx (blocks)
Process this LED                ...
                                Acquire lock
                                Remove from list
                                Unlock
Move to next LED                [timer continues]
                                Free softc
```

El temporizador termina de procesar el LED antes de que se elimine de la lista. El mutex garantiza que el LED no se libera mientras el temporizador está accediendo a él.

##### Contraste con null.c

La limpieza del driver null en `MOD_UNLOAD` era sencilla:

```c
destroy_dev(full_dev);
destroy_dev(null_dev);
destroy_dev(zero_dev);
```

Tres dispositivos fijos, tres llamadas a destroy, y listo. La limpieza del driver del LED es más compleja porque:

**Ciclo de vida dinámico**: Los LEDs se crean y destruyen individualmente a medida que el hardware aparece y desaparece, no todos a la vez durante la descarga del módulo.

**Estado activo**: Los LEDs pueden tener temporizadores en ejecución y patrones asignados que necesitan limpieza.

**Conteo de referencias**: El contador `blinkers` debe mantenerse correctamente para la gestión del temporizador.

**Gestión de lista**: La eliminación del registro global requiere una manipulación adecuada de la lista.

Esta complejidad adicional es el precio de admitir la creación dinámica de dispositivos: el subsistema debe gestionar secuencias arbitrarias de operaciones de creación y destrucción sin causar fugas de recursos ni corromper el estado.

##### Ejemplo de uso

Un ciclo de vida completo del driver de hardware:

```c
// During attach
sc->led_dev = led_create(my_led_control, sc, "disk0");

// During normal operation
// LED blinks, patterns execute, writes succeed

// During detach
led_destroy(sc->led_dev);
// LED is gone, /dev/led/disk0 removed
// All resources freed
```

Una vez que `led_destroy` retorna, el driver de hardware puede descargarse de forma segura sin dejar estado del LED huérfano en el kernel.

#### 9) Inicialización del driver: configuración del registro de estado y el callout

```c
331: static void
332: led_drvinit(void *unused)
333: {
335: 	led_unit = new_unrhdr(0, INT_MAX, NULL);
336: 	mtx_init(&led_mtx, "LED mtx", NULL, MTX_DEF);
337: 	sx_init(&led_sx, "LED sx");
338: 	callout_init_mtx(&led_ch, &led_mtx, 0);
339: }
341: SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL);
```

##### Inicialización y registro del driver

La sección final del driver del LED gestiona la inicialización única durante el arranque del sistema. Este código configura la infraestructura global necesaria antes de que pueda registrarse ningún LED, estableciendo los cimientos de los que dependen todas las operaciones posteriores.

##### Función de inicialización

```c
static void
led_drvinit(void *unused)
{
    led_unit = new_unrhdr(0, INT_MAX, NULL);
    mtx_init(&led_mtx, "LED mtx", NULL, MTX_DEF);
    sx_init(&led_sx, "LED sx");
    callout_init_mtx(&led_ch, &led_mtx, 0);
}
```

**Firma de la función**: Las funciones de inicialización registradas con `SYSINIT` reciben un único argumento `void *` para datos opcionales. El driver del LED no necesita ningún parámetro de inicialización, por lo que el argumento no se utiliza y se nombra en consecuencia.

**Creación del asignador de números de unidad**: `new_unrhdr(0, INT_MAX, NULL)` crea un pool de números de unidad capaz de asignar enteros del 0 al `INT_MAX` (típicamente 2.147.483.647). Cada LED registrado recibirá un número único de ese rango, que se usa como número menor del dispositivo. El parámetro NULL indica que ningún mutex protege este asignador; en su lugar, el lock externo (mediante `led_sx`) se encargará de serializar el acceso.

**Inicialización del mutex**: `mtx_init(&led_mtx, "LED mtx", NULL, MTX_DEF)` inicializa el mutex que protege:

- La lista de LEDs durante inserciones, eliminaciones y recorridos
- El contador `blinkers`
- El estado de ejecución de patrón por LED

Los parámetros especifican:

- `&led_mtx` - la estructura del mutex que se inicializa
- `"LED mtx"` - nombre que aparece en las herramientas de depuración y análisis de locks
- `NULL` - sin datos de witness (no se necesita comprobación avanzada del orden de locks)
- `MTX_DEF` - tipo de mutex por defecto (puede dormir mientras se mantiene, reglas de recursión estándar)

**Inicialización del lock compartido/exclusivo**: `sx_init(&led_sx, "LED sx")` inicializa el lock que protege la creación y destrucción de dispositivos. La lista de parámetros más sencilla refleja que los locks sx tienen menos opciones que los mutex; siempre pueden dormir y no son recursivos.

**Inicialización del temporizador**: `callout_init_mtx(&led_ch, &led_mtx, 0)` prepara la infraestructura de callbacks del temporizador. Los parámetros especifican:

- `&led_ch` - la estructura callout que se inicializa
- `&led_mtx` - el mutex que se mantiene cuando se ejecutan los callbacks del temporizador
- `0` - flags (ninguno necesario)

Esta inicialización asocia el temporizador con el mutex, de modo que los callbacks del temporizador mantienen `led_mtx` automáticamente durante su ejecución. Esto simplifica el locking en `led_timeout`, que no necesita adquirir el mutex de forma explícita porque la infraestructura del callout lo hace automáticamente.

##### Registro en tiempo de arranque

```c
SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL);
```

El macro `SYSINIT` registra la función de inicialización en la secuencia de arranque del kernel. El kernel llama a las funciones registradas en orden durante el arranque, garantizando que se cumplen las dependencias.

**Parámetros del macro**:

**`leddev`**: Un identificador único para esta inicialización. Debe ser único en todo el kernel para evitar colisiones. El nombre no afecta al comportamiento; es puramente identificativo durante la depuración.

**`SI_SUB_DRIVERS`**: El nivel del subsistema. La inicialización del kernel ocurre en fases (veremos una lista simplificada; los `...` de la lista a continuación indican que se han omitido algunas fases):

- `SI_SUB_TUNABLES` - sintonizables del sistema
- `SI_SUB_COPYRIGHT` - mostrar copyright
- `SI_SUB_VM` - memoria virtual
- `SI_SUB_KMEM` - asignador de memoria del kernel
- ...
- `SI_SUB_DRIVERS` - drivers de dispositivo
- ...
- `SI_SUB_RUN_SCHEDULER` - arrancar el planificador

El driver del LED se inicializa durante la fase de drivers, cuando ya están disponibles los servicios básicos del kernel (asignación de memoria, primitivas de locking), pero antes de que los dispositivos empiecen a conectarse (attach).

**`SI_ORDER_MIDDLE`**: El orden dentro del subsistema. Varios inicializadores en el mismo subsistema se ejecutan en orden, desde `SI_ORDER_FIRST` hasta `SI_ORDER_LAST`, pasando por `SI_ORDER_ANY`. Usar `MIDDLE` sitúa al driver del LED en el punto medio de la fase de inicialización de drivers: no es crítico que vaya el primero, pero tampoco depende de que todo lo demás ya esté inicializado.

**`led_drvinit`**: Puntero a la función de inicialización.

**`NULL`**: Sin datos de argumento que pasar a la función.

##### Orden de inicialización

El mecanismo `SYSINIT` garantiza el orden correcto de inicialización:

**Antes de la inicialización del LED**:

```text
Memory allocator running (malloc works)
Lock primitives available (mtx_init, sx_init work)
Timer subsystem operational (callout_init works)
Device filesystem ready (make_dev will work later)
```

**Durante la inicialización del LED**:

```text
led_drvinit() called
 -> 
Create unit allocator
Initialize locks
Prepare timer infrastructure
```

**Después de la inicialización del LED**:

```text
Hardware drivers attach
 -> 
Call led_create()
 -> 
Use the already-initialized infrastructure
```

Sin `SYSINIT`, los drivers de hardware que llaman a `led_create()` durante sus funciones attach fallarían al intentar usar locks no inicializados o asignar memoria desde un pool de números de unidad con valor NULL.

##### Contraste con la carga de módulo de null.c

El driver null usaba manejadores de eventos de módulo:

```c
static int
null_modevent(module_t mod, int type, void *data)
{
    switch(type) {
    case MOD_LOAD:
        /* Create devices */
        break;
    case MOD_UNLOAD:
        /* Destroy devices */
        break;
    }
}

DEV_MODULE(null, null_modevent, NULL);
```

Los eventos de módulo se disparan cuando se cargan o descargan módulos del kernel. El driver del LED usa `SYSINIT` en su lugar por las siguientes razones:

**Siempre necesario**: El subsistema del LED es infraestructura de la que dependen otros drivers. Debe inicializarse pronto durante el arranque, sin esperar a que se cargue un módulo de forma explícita.

**Sin descarga**: El subsistema del LED no proporciona un manejador de descarga de módulo. Una vez inicializado, permanece disponible durante toda la vida del sistema. La descarga sería compleja: habría que destruir todos los LEDs registrados, lo que requeriría coordinar con potencialmente muchos drivers de hardware.

**Separación de responsabilidades**: `SYSINIT` se encarga de la inicialización, mientras que los LEDs individuales se crean y destruyen dinámicamente a medida que el hardware aparece y desaparece. El driver null mezclaba la inicialización con la creación de dispositivos (ambas ocurrían en `MOD_LOAD`), mientras que el driver del LED las separa.

##### Lo que no se inicializa

Observa lo que esta función **no** hace:

**Sin creación de LEDs**: A diferencia del driver null, que creaba sus tres dispositivos durante la inicialización, el driver del LED no crea ningún dispositivo aquí. La creación de dispositivos se realiza bajo demanda, a través de llamadas a `led_create()` procedentes de los drivers de hardware.

**Sin inicialización de lista**: La variable global `led_list` se inicializó de forma estática:

```c
static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
```

La inicialización estática es suficiente para las cabeceras de lista, que no son más que estructuras de punteros que comienzan vacías.

**Sin inicialización de blinkers**: El contador `blinkers` se declaró como `static int`, lo que le asigna automáticamente un valor inicial de 0. No se necesita ninguna inicialización explícita.

**Sin programación del temporizador**: El callback del temporizador comienza inactivo. Solo se programa cuando el primer LED recibe un patrón de parpadeo, no durante la inicialización del driver.

Esta inicialización mínima refleja un buen diseño: hacer el mínimo trabajo necesario en el arranque y diferir todo lo demás hasta que sea realmente necesario.

##### Secuencia completa de arranque

La secuencia completa desde el encendido hasta los LEDs en funcionamiento:

```text
1. Kernel starts
2. Early boot (memory, interrupts, etc.)
3. SYSINIT runs:
   - led_drvinit() initializes LED infrastructure
4. Device enumeration and driver attachment:
   - Disk driver attaches
   - Calls led_create(..., "disk0")
   - /dev/led/disk0 appears
   - GPIO driver attaches
   - Calls led_create(..., "power")
   - /dev/led/power appears
5. System running:
   - User scripts write patterns
   - Drivers call led_set()
   - LEDs blink and indicate status
```

El subsistema del LED está listo antes de que los drivers de hardware lo necesiten, y dichos drivers pueden registrar LEDs en cualquier momento durante o después del arranque sin preocuparse por el orden de inicialización.

##### Por qué esto es importante

Este patrón de inicialización (configuración temprana de infraestructura mediante `SYSINIT`, creación tardía de dispositivos bajo demanda) es fundamental para la arquitectura modular de FreeBSD. Permite:

**Flexibilidad**: Los drivers de hardware no necesitan coordinar el orden de inicialización. El subsistema del LED siempre está listo cuando lo necesitan.

**Escalabilidad**: El subsistema no preasigna recursos para dispositivos que puede que no existan. El uso de memoria escala con el hardware real.

**Modularidad**: Los drivers de hardware dependen solo de la API del LED, no de los detalles de implementación. El subsistema puede cambiar internamente sin afectar a los drivers.

**Fiabilidad**: Los fallos de inicialización (como el agotamiento de memoria durante `new_unrhdr`) provocan panics fatales en lugar de fallos oscuros posteriores, lo que hace que los problemas sean inmediatamente visibles durante el arranque.

Esta filosofía de diseño (inicializar la infraestructura pronto, crear instancias de forma diferida) aparece en todo el kernel de FreeBSD y merece la pena comprenderla para quien implementa subsistemas o drivers.

#### Ejercicios interactivos para `led(4)`

**Objetivo:** Comprender la creación dinámica de dispositivos, las máquinas de estado basadas en temporizadores y el análisis de patrones. Este driver se apoya en los conceptos del driver null, pero añade ejecución de patrones con estado y diseño de API del kernel.

##### A) Estructura y estado global

1. Examina la definición de `struct ledsc` cerca del inicio de `led.c`. Esta estructura contiene tanto la identidad del dispositivo como el estado de ejecución del patrón. Crea una tabla que clasifique los campos:

| Campo | Propósito | Categoría           |
| ----- | --------- | ------------------- |
| list  | ?         | Enlace              |
| name  | ?         | Identidad           |
| ptr   | ?         | Ejecución de patrón |
| ...   | ...       | ...                 |

	Cita los campos relacionados con la ejecución del patrón (`str`, `ptr`, `count`, `last_second`) y explica el papel de cada uno en una sola frase.

2. Localiza las variables estáticas de ámbito de archivo que siguen a `struct ledsc` (`led_unit`, `led_mtx`, `led_sx`, `led_list`, `led_ch`, `blinkers` y el `MALLOC_DEFINE` de `M_LED`). Para cada una, explica su propósito:

- `led_unit` - ¿qué asigna?
- `led_mtx` vs. `led_sx` - ¿por qué dos locks? ¿Qué protege cada uno?
- `led_list` - ¿quién la recorre y cuándo?
- `led_ch` - ¿qué lo activa?
- `blinkers` - ¿qué ocurre cuando llega a 0?

Cita las líneas de declaración.

3. Examina la estructura `led_cdevsw`. ¿Qué operación está definida? ¿Qué operaciones están notablemente ausentes (compara con null.c)? ¿Qué aparece bajo `/dev` cuando se crean los LEDs?

##### B) Ruta de escritura a parpadeo

1. Traza el flujo de datos en `led_write()`:

- Encuentra la comprobación de tamaño: ¿cuál es el límite y por qué?
- Encuentra la asignación del buffer: ¿por qué `uio_resid + 1`?
- Encuentra la llamada a `uiomove()`: ¿qué se está copiando?
- Encuentra la llamada al parseador: ¿qué produce?
- Encuentra la actualización de estado: ¿qué lock se mantiene?

Cita cada paso y escribe una oración que explique su propósito.

2. En `led_state()`, traza dos caminos:

**Camino 1** - Instalación de un patrón (sb != NULL):

- ¿Qué campos cambian en el softc?
- ¿Cuándo se incrementa `blinkers`?
- ¿Qué consigue `sc->ptr = sc->str`?

**Camino 2** - Establecimiento de estado estático (sb == NULL):

- ¿Qué campos cambian?
- ¿Cuándo se decrementa `blinkers`?
- ¿Por qué se llama a `sc->func()` aquí pero no en el Camino 1?

Cita las líneas clave de cada camino.

3. Explica la conexión entre el temporizador y el patrón:

- Cuando `blinkers` pasa de 0 a 1, ¿qué debe ocurrir? (Pista: ¿quién programa el temporizador?)
- Cuando `blinkers` pasa de 1 a 0, ¿qué debe ocurrir? (Pista: busca `LIST_EMPTY(&led_list)` y la llamada adyacente a `callout_stop(&led_ch)` en `led_destroy`.)
- ¿Por qué `led_state()` no programa el temporizador directamente?

##### C) Máquina de estados del callback del temporizador

1. En `led_timeout()`, explica el intérprete de patrones:

Crea una tabla que muestre qué hace cada código:

| Código  | Acción del LED | Configuración de duración | Ejemplo             |
| ------- | -------------- | ------------------------- | ------------------- |
| 'A'-'J' | ?              | count = ?                 | ¿Qué significa 'C'? |
| 'a'-'j' | ?              | count = ?                 | ¿Qué significa 'c'? |
| 'U'/'u' | ?              | Temporización especial    | ¿Qué se comprueba?  |
| '.'     | ?              | N/A                       | ¿Qué ocurre?        |

Cita las líneas que implementan cada caso.

2. El campo `count` implementa retardos:

- ¿Cuándo se asigna a `count` un valor distinto de cero? Cita la línea.
- ¿Cuándo se decrementa `count`? Cita la línea.
- ¿Por qué se omite el avance del patrón cuando `count > 0`?

Traza el patrón "Ac" (encendido 0.1s, apagado 0.3s) a través de tres ticks del temporizador:

- Tick 1: ¿Qué ocurre?
- Tick 2: ¿Qué ocurre?
- Tick 3: ¿Qué ocurre?

3. Encuentra la lógica de reprogramación del temporizador al final de `led_timeout` (el guard `if (blinkers > 0)` seguido de `callout_reset(&led_ch, hz / 10, led_timeout, p)`):

- ¿Qué condición debe cumplirse para la reprogramación?
- ¿Cuál es el retardo (`hz / 10` significa qué en segundos)?
- ¿Por qué el temporizador no se reprograma cuando `blinkers == 0`?

##### D) DSL de análisis de patrones

1. Para el comando de destellos "f2" (el brazo `case 'f':` dentro de `led_parse`):

- ¿A qué se mapea el dígito '2' (i = ?)?
- ¿Qué cadena de dos caracteres se genera?
- ¿Cuánto dura cada fase en ticks del temporizador?
- ¿Qué frecuencia produce esto?

Cita las líneas y calcula la frecuencia de parpadeo.

2. Para el comando Morse "m...---..." (el brazo `case 'm':` dentro de `led_parse`):

- ¿Qué cadena se genera para '.' (punto)?
- ¿Qué cadena se genera para '-' (raya)?
- ¿Qué cadena se genera para ' ' (espacio)?
- ¿Qué cadena se genera para '\\n' (salto de línea)?

Cita las llamadas a `sbuf_cat()` y explica cómo esto implementa la temporización estándar del código Morse (punto = 1 unidad, raya = 3 unidades).

3. Para el comando de dígitos "d12" (el brazo `case 'd':` dentro de `led_parse`):

- ¿Cómo se representa el dígito '1' en destellos?
- ¿Cómo se representa el dígito '2' en destellos?
- ¿Por qué '0' se trata como 10 en lugar de 0?
- ¿Qué separa las repeticiones del patrón?

Cita el bucle y explica la fórmula para el recuento de destellos.

##### E) Ciclo de vida dinámico del dispositivo

1. En `led_create_state()`, identifica la secuencia de inicialización:

- ¿Qué se asigna primero y con qué flags?
- ¿Qué lock se adquiere para la creación del dispositivo? ¿Por qué exclusivo?
- ¿Qué parámetros recibe `make_dev()`? ¿Qué ruta se crea?
- ¿Qué lock protege la inserción en la lista? ¿Por qué es diferente del de la creación del dispositivo?
- ¿Cuándo se invoca el callback de hardware y qué significa `state != -1`?

Cita cada fase y explica la separación de locks.

2. En `led_destroy()`, traza la limpieza:

- ¿Por qué se asigna NULL a `dev->si_drv1` inmediatamente?
- ¿Cuándo se decrementa `blinkers`?
- ¿Por qué llamar a `callout_stop()` solo cuando la lista queda vacía?
- ¿Qué recursos se liberan bajo qué lock?

Crea una tabla que relacione cada asignación de `led_create()` con su correspondiente liberación en `led_destroy()`.

3. Explica el bloqueo en dos fases:

- ¿Por qué adquirir primero `led_mtx` y luego liberarlo antes de adquirir `led_sx`?
- ¿Qué ocurriría si mantuviéramos `led_mtx` durante `destroy_dev()`?
- ¿Podríamos usar un solo lock para todo? ¿Qué inconvenientes tendría?

##### F) API del kernel frente a escritura en dispositivo

1. Compara `led_write()` y `led_set()`:

- Ambas llaman a `led_parse()` y `led_state()`: ¿en qué se diferencia la forma en que localizan el LED?
- `led_write()` tiene límites de tamaño: ¿los necesita `led_set()`? ¿Por qué sí o por qué no?
- ¿Quién llama habitualmente a cada función? Da ejemplos.

Cita la lógica de búsqueda del LED en ambas funciones.

2. Encuentra la declaración de `led_cdevsw` y explica por qué se comparte:

- ¿Cuántas estructuras `cdevsw` existen para N LEDs?
- ¿Cómo sabe `led_write()` a qué LED está escribiendo?
- Compara esto con null.c, que tenía tres estructuras `cdevsw` separadas.

##### G) Integración en el sistema

1. Examina la inicialización (`led_drvinit` y su registro con `SYSINIT`):

- ¿Qué hace `SYSINIT` y cuándo se ejecuta?
- ¿Cuáles son los cuatro recursos que se inicializan en `led_drvinit()`?
- ¿Por qué el callout está asociado con `led_mtx`?
- ¿Qué NO se inicializa aquí (compara con el `MOD_LOAD` de null.c)?

2. Encuentra dónde se registra el driver con `SYSINIT`:

- ¿Cuál es el nivel del subsistema (`SI_SUB_DRIVERS`)?
- ¿Por qué no usar `DEV_MODULE` como hizo null.c?
- ¿Se puede descargar este driver? ¿Por qué sí o por qué no?

##### H) Experimentos seguros (opcional, solo si dispones de un sistema con LEDs físicos)

1. Si tu sistema tiene LEDs en `/dev/led`, prueba lo siguiente (como root en una VM):

```bash
# List available LEDs
ls -l /dev/led/

# Fast blink
echo "f" > /dev/led/SOME_LED_NAME

# Slow blink
echo "f5" > /dev/led/SOME_LED_NAME

# Morse code SOS
echo "m...---..." > /dev/led/SOME_LED_NAME

# Static on
echo "1" > /dev/led/SOME_LED_NAME

# Static off
echo "0" > /dev/led/SOME_LED_NAME
```

Para cada prueba:

- ¿Qué caso del parseador gestiona el comando?
- ¿Qué cadena de patrón interna se genera?
- Estima la temporización que observas y verifícala con el código.

2. Prueba comandos inválidos y explica los errores:

```bash
# Too long
perl -e 'print "f" x 600' > /dev/led/SOME_LED_NAME
# What error? Which line checks this?

# Invalid syntax
echo "xyz" > /dev/led/SOME_LED_NAME
# What error? Which case handles this?
```

#### Desafío adicional (experimentos mentales)

1. La lógica de auto-reprogramación del temporizador (el guard `if (blinkers > 0)` más `callout_reset(&led_ch, hz / 10, led_timeout, p)` al final de `led_timeout`):

Supón que eliminamos la comprobación `if (blinkers > 0)` y siempre llamáramos a:

```c
callout_reset(&led_ch, hz / 10, led_timeout, p);
```

Traza lo que ocurre cuando:

- El usuario escribe "f" en un LED (el temporizador arranca)
- El patrón se ejecuta durante 5 segundos
- El usuario escribe "0" para detener el parpadeo (blinkers  ->  0)

¿Cuál es el síntoma? ¿Dónde está el recurso desperdiciado? ¿Por qué la comprobación actual lo evita?

2. El límite de tamaño de escritura (la comprobación `if (uio->uio_resid > 512) return (EINVAL);` en `led_write`):

El código rechaza escrituras de más de 512 bytes. Considera eliminar esta comprobación:

- ¿Cuál es el riesgo inmediato con `malloc(uio->uio_resid, ...)`?
- El parseador asigna entonces un `sbuf`: ¿cuál es el riesgo aquí?
- ¿Podría un atacante causar una denegación de servicio? ¿Cómo?
- ¿Por qué son suficientes 512 bytes para cualquier patrón de LED legítimo?

Señala el guard actual y explica el principio de defensa en profundidad.

3. El diseño de dos locks:

Supón que reemplazamos tanto `led_mtx` como `led_sx` por un único mutex. ¿Qué se rompería?

Escenario 1: `led_create()` llama a `make_dev()` mientras mantiene el lock, y `make_dev()` se bloquea. ¿Qué ocurre con los callbacks del temporizador durante ese tiempo?

Escenario 2: Una operación de escritura mantiene el lock mientras parsea un patrón complejo. ¿Qué ocurre con las actualizaciones del temporizador de los demás LEDs?

Explica por qué separar las operaciones de estructura de dispositivo (`led_sx`) de las operaciones de estado (`led_mtx`) mejora la concurrencia.

**Nota:** Si tu sistema no tiene LEDs físicos, puedes igualmente recorrer el código y entender los patrones. El modelo mental de "el temporizador recorre la lista  ->  interpreta los códigos  ->  llama a los callbacks" es la lección fundamental, no ver luces reales parpadear.

#### Puente hacia el próximo recorrido

Si puedes recorrer el camino desde el **`write()` de usuario** hasta una **máquina de estados dirigida por temporizador** y de vuelta al **desmontaje del dispositivo**, has interiorizado la forma del dispositivo de caracteres centrada en escritura, con temporizadores y análisis basado en sbuf. A continuación, examinaremos una forma ligeramente distinta: un **pseudo-dispositivo de interfaz de red** que se enlaza con la pila **ifnet** (`if_tuntap.c`). Mantén la atención en tres aspectos: cómo se **registra** el driver con un subsistema mayor, cómo se **enruta la I/O** a través de los callbacks de ese subsistema, y en qué se diferencia el **ciclo de vida de apertura/cierre** de los pequeños patrones de `/dev` que acabas de dominar.

> **Punto de control.** Ya has recorrido la forma completa de un driver sencillo: el ciclo de vida Newbus, los puntos de entrada de `cdevsw`, `make_dev()` y devctl, el empaquetado de módulos con `bsd.kmod.mk`, y dos drivers de caracteres reales: el trío null/zero/full y `led(4)`. El resto del capítulo se vuelca hacia los drivers que se conectan a subsistemas más grandes: el pseudo-NIC `tun(4)/tap(4)` que se enlaza con la pila ifnet, el driver glue de `uart(4)` respaldado por PCI, la síntesis que integra los cuatro recorridos en un único modelo mental, y los planos y laboratorios que convierten la lectura en práctica. Si quieres cerrar el libro y retomarlo después, este es un punto de pausa natural.

### Tour 3: una pseudo-NIC que también es un dispositivo de caracteres: `tun(4)/tap(4)`

Abre el archivo:

```console
% cd /usr/src/sys/net
% less if_tuntap.c
```

Este driver es un ejemplo perfecto de «pequeño pero real» para ver cómo se integra un dispositivo de caracteres sencillo con un **subsistema** mayor del kernel (la pila de red). Expone los dispositivos de caracteres `/dev/tunN`, `/dev/tapN` y `/dev/vmnetN`, al tiempo que registra interfaces **ifnet** que puedes configurar con `ifconfig`.

Mientras lees, ten presentes estos «anclajes»:

- **Superficie de dispositivo de caracteres**: `cdevsw` + `open/read/write/ioctl/poll/kqueue`;
- **Superficie de red**: `ifnet` + `if_attach` + `bpfattach;`
- **Clonación**: creación bajo demanda de `/dev/tunN` y el correspondiente `ifnet`;

- cómo un **`cdevsw`** mapea `open/read/write/ioctl` en el código del driver para tres nombres de dispositivo relacionados;
- cómo la apertura de `/dev/tun0` y similares se alinea con la creación y configuración de un **`ifnet`**;
- cómo fluyen los datos en ambos sentidos: paquetes del kernel al usuario a través de `read(2)`, y del usuario al kernel a través de `write(2)`.

> **Nota**
>
> Para mantenerlo manejable, los ejemplos de código que siguen son extractos del archivo fuente de 2071 líneas. Las líneas marcadas con `...` han sido omitidas.

#### 1) Dónde se declara la superficie de dispositivo de caracteres (el `cdevsw`)

```c
 270: static struct tuntap_driver {
 271: 	struct cdevsw		 cdevsw;
 272: 	int			 ident_flags;
 273: 	struct unrhdr		*unrhdr;
 274: 	struct clonedevs	*clones;
 275: 	ifc_match_f		*clone_match_fn;
 276: 	ifc_create_f		*clone_create_fn;
 277: 	ifc_destroy_f		*clone_destroy_fn;
 278: } tuntap_drivers[] = {
 279: 	{
 280: 		.ident_flags =	0,
 281: 		.cdevsw =	{
 282: 		    .d_version =	D_VERSION,
 283: 		    .d_flags =		D_NEEDMINOR,
 284: 		    .d_open =		tunopen,
 285: 		    .d_read =		tunread,
 286: 		    .d_write =		tunwrite,
 287: 		    .d_ioctl =		tunioctl,
 288: 		    .d_poll =		tunpoll,
 289: 		    .d_kqfilter =	tunkqfilter,
 290: 		    .d_name =		tunname,
 291: 		},
 292: 		.clone_match_fn =	tun_clone_match,
 293: 		.clone_create_fn =	tun_clone_create,
 294: 		.clone_destroy_fn =	tun_clone_destroy,
 295: 	},
 296: 	{
 297: 		.ident_flags =	TUN_L2,
 298: 		.cdevsw =	{
 299: 		    .d_version =	D_VERSION,
 300: 		    .d_flags =		D_NEEDMINOR,
 301: 		    .d_open =		tunopen,
 302: 		    .d_read =		tunread,
 303: 		    .d_write =		tunwrite,
 304: 		    .d_ioctl =		tunioctl,
 305: 		    .d_poll =		tunpoll,
 306: 		    .d_kqfilter =	tunkqfilter,
 307: 		    .d_name =		tapname,
 308: 		},
 309: 		.clone_match_fn =	tap_clone_match,
 310: 		.clone_create_fn =	tun_clone_create,
 311: 		.clone_destroy_fn =	tun_clone_destroy,
 312: 	},
 313: 	{
 314: 		.ident_flags =	TUN_L2 | TUN_VMNET,
 315: 		.cdevsw =	{
 316: 		    .d_version =	D_VERSION,
 317: 		    .d_flags =		D_NEEDMINOR,
 318: 		    .d_open =		tunopen,
 319: 		    .d_read =		tunread,
 320: 		    .d_write =		tunwrite,
 321: 		    .d_ioctl =		tunioctl,
 322: 		    .d_poll =		tunpoll,
 323: 		    .d_kqfilter =	tunkqfilter,
 324: 		    .d_name =		vmnetname,
 325: 		},
 326: 		.clone_match_fn =	vmnet_clone_match,
 327: 		.clone_create_fn =	tun_clone_create,
 328: 		.clone_destroy_fn =	tun_clone_destroy,
 329: 	},
 330: };

```

Este fragmento inicial muestra un patrón de diseño ingenioso: **una única implementación de driver que sirve a tres tipos de dispositivo relacionados pero distintos** (tun, tap y vmnet).

Veamos cómo funciona:

##### La estructura `tuntap_driver`

```c
struct tuntap_driver {
    struct cdevsw         cdevsw;           // Character device switch table
    int                   ident_flags;      // Identity flags (TUN_L2, TUN_VMNET)
    struct unrhdr        *unrhdr;           // Unit number allocator
    struct clonedevs     *clones;           // Cloning infrastructure
    ifc_match_f          *clone_match_fn;   // Network interface clone matching
    ifc_create_f         *clone_create_fn;  // Network interface creation
    ifc_destroy_f        *clone_destroy_fn; // Network interface destruction
};
```

Esta estructura combina **dos subsistemas del kernel**:

1. **Operaciones de dispositivo de caracteres** (`cdevsw`): cómo interactúa el espacio de usuario con `/dev/tunN`, `/dev/tapN`, `/dev/vmnetN`
2. **Clonación de interfaces de red** (`clone_*_fn`): cómo se crean las estructuras `ifnet` correspondientes

##### La estructura `cdevsw` clave

El `cdevsw` (character device switch) es la **tabla de despacho de funciones** de FreeBSD para dispositivos de caracteres. Piensa en él como una vtable o interfaz:

```c
.d_version   = D_VERSION      // ABI version check
.d_flags     = D_NEEDMINOR    // Device needs minor number tracking
.d_open      = tunopen        // Called on open(2)
.d_read      = tunread        // Called on read(2)
.d_write     = tunwrite       // Called on write(2)
.d_ioctl     = tunioctl       // Called on ioctl(2)
.d_poll      = tunpoll        // Called on poll(2)/select(2)
.d_kqfilter  = tunkqfilter    // Called for kqueue event registration
.d_name      = tunname        // Device name ("tun", "tap", "vmnet")
```

**Idea clave**: los tres tipos de dispositivo comparten las **mismas implementaciones de función** (`tunopen`, `tunread`, etc.), pero se comportan de forma diferente según `ident_flags`.

##### Las tres instancias del driver

##### 1. **TUN**: túnel de capa 3 (IP)

```c
.ident_flags = 0              // No flags = plain TUN device
.d_name = tunname             // "tun"  ->  /dev/tun0, /dev/tun1, ...
```

- Túnel IP punto a punto
- Los paquetes son IP en bruto (sin cabeceras Ethernet)
- Lo usan las VPN como OpenVPN en modo TUN

##### 2. **TAP**: túnel de capa 2 (Ethernet)

```c
.ident_flags = TUN_L2         // Layer 2 flag
.d_name = tapname             // "tap"  ->  /dev/tap0, /dev/tap1, ...
```

- Túnel a nivel Ethernet
- Los paquetes incluyen tramas Ethernet completas
- Lo usan las VM, bridges y OpenVPN en modo TAP

##### 3. **VMNET**: compatibilidad con VMware

```c
.ident_flags = TUN_L2 | TUN_VMNET  // Layer 2 + VMware semantics
.d_name = vmnetname                 // "vmnet"  ->  /dev/vmnet0, ...
```

- Similar a TAP pero con comportamiento específico de VMware
- Distintas reglas de ciclo de vida (sobrevive a la bajada de la interfaz)

##### Cómo se consigue la reutilización del código

Observa que **las tres entradas usan punteros de función idénticos**:

- `tunopen` gestiona la apertura de los tres tipos de dispositivo
- `tunread`/`tunwrite` gestionan las E/S de los tres
- Las funciones comprueban `tp->tun_flags` (derivado de `ident_flags`) para determinar el comportamiento

Por ejemplo, en `tunopen` verás:

```c
if ((tp->tun_flags & TUN_L2) != 0) {
    // TAP/VMNET-specific setup
} else {
    // TUN-specific setup
}
```

##### Las funciones de clonación

Cada driver tiene **distintas funciones de coincidencia de clonación**, pero comparte la creación y destrucción:

- `tun_clone_match`: coincide con «tun» o «tunN»
- `tap_clone_match`: coincide con «tap» o «tapN»
- `vmnet_clone_match`: coincide con «vmnet» o «vmnetN»
- Todas usan `tun_clone_create`: lógica de creación compartida
- Todas usan `tun_clone_destroy`: lógica de destrucción compartida

Esto permite al kernel crear automáticamente `/dev/tun0` cuando alguien lo abre, aunque todavía no exista.

#### 2) Del clon solicitado a la creación del `cdev` y la conexión del `ifnet`

#### 2.1 Creación del clon (`tun_clone_create`): elegir nombre y unidad, asegurar el `cdev` y delegar en `tuncreate`

```c
 520: tun_clone_create(struct if_clone *ifc, char *name, size_t len,
 521:     struct ifc_data *ifd, struct ifnet **ifpp)
 522: {
 523: 	struct tuntap_driver *drv;
 524: 	struct cdev *dev;
 525: 	int err, i, tunflags, unit;
 526: 
 527: 	tunflags = 0;
 528: 	/* The name here tells us exactly what we're creating */
 529: 	err = tuntap_name2info(name, &unit, &tunflags);
 530: 	if (err != 0)
 531: 		return (err);
 532: 
 533: 	drv = tuntap_driver_from_flags(tunflags);
 534: 	if (drv == NULL)
 535: 		return (ENXIO);
 536: 
 537: 	if (unit != -1) {
 538: 		/* If this unit number is still available that's okay. */
 539: 		if (alloc_unr_specific(drv->unrhdr, unit) == -1)
 540: 			return (EEXIST);
 541: 	} else {
 542: 		unit = alloc_unr(drv->unrhdr);
 543: 	}
 544: 
 545: 	snprintf(name, IFNAMSIZ, "%s%d", drv->cdevsw.d_name, unit);
 546: 
 547: 	/* find any existing device, or allocate new unit number */
 548: 	dev = NULL;
 549: 	i = clone_create(&drv->clones, &drv->cdevsw, &unit, &dev, 0);
 550: 	/* No preexisting struct cdev *, create one */
 551: 	if (i != 0)
 552: 		i = tun_create_device(drv, unit, NULL, &dev, name);
 553: 	if (i == 0) {
 554: 		dev_ref(dev);
 555: 		tuncreate(dev);
 556: 		struct tuntap_softc *tp = dev->si_drv1;
 557: 		*ifpp = tp->tun_ifp;
 558: 	}
 559: 	return (i);
 560: }
```

La función `tun_clone_create` actúa como puente entre el subsistema de clonación de interfaces de red de FreeBSD y la creación de dispositivos de caracteres. Esta función se invoca cuando un usuario ejecuta comandos como `ifconfig tun0 create` o `ifconfig tap1 create`, y su responsabilidad es crear tanto un dispositivo de caracteres (`/dev/tun0`) como su interfaz de red correspondiente.

##### Firma de la función y propósito

```c
static int
tun_clone_create(struct if_clone *ifc, char *name, size_t len,
    struct ifc_data *ifd, struct ifnet **ifpp)
```

La función recibe un nombre de interfaz (como «tun0» o «tap3») y debe devolver un puntero a una estructura `ifnet` recién creada a través del parámetro `ifpp`. El éxito devuelve 0; los errores devuelven los valores errno apropiados, como `EEXIST` o `ENXIO`.

##### Análisis del nombre de la interfaz

El primer paso extrae información del nombre de la interfaz:

```c
tunflags = 0;
err = tuntap_name2info(name, &unit, &tunflags);
if (err != 0)
    return (err);
```

La función auxiliar `tuntap_name2info` analiza cadenas como «tap3» o «vmnet1» para extraer:

- El **número de unidad** (3, 1, etc.)
- Los **indicadores de tipo** que determinan el comportamiento del dispositivo (0 para tun, TUN_L2 para tap, TUN_L2|TUN_VMNET para vmnet)

Si el nombre no contiene número de unidad (p. ej., simplemente «tun»), la función devuelve `-1` para la unidad, indicando que debe asignarse cualquier unidad disponible.

##### Localización del driver apropiado

```c
drv = tuntap_driver_from_flags(tunflags);
if (drv == NULL)
    return (ENXIO);
```

Los indicadores extraídos determinan qué entrada del array `tuntap_drivers[]` gestionará este dispositivo. Esta búsqueda devuelve la estructura `tuntap_driver` que contiene el `cdevsw` correcto y el nombre del dispositivo («tun», «tap» o «vmnet»).

##### Asignación del número de unidad

El driver mantiene un asignador de números de unidad (`unrhdr`) para evitar conflictos:

```c
if (unit != -1) {
    /* User requested specific unit number */
    if (alloc_unr_specific(drv->unrhdr, unit) == -1)
        return (EEXIST);
} else {
    /* Allocate any available unit */
    unit = alloc_unr(drv->unrhdr);
}
```

El `unrhdr` (gestor de números de unidad) garantiza la asignación segura para threads de los números de dispositivo menores. Cuando un usuario solicita una unidad específica (p. ej., «tun3»), `alloc_unr_specific` reserva ese número o devuelve error si ya está asignado. Cuando no se solicita ninguna unidad concreta, `alloc_unr` selecciona el siguiente número disponible.

Este mecanismo previene las condiciones de carrera en las que varios procesos intentan crear simultáneamente la misma unidad de dispositivo, ya que la asignación está serializada por el mutex global `tunmtx`.

##### Normalización del nombre

Tras la asignación de la unidad, la función normaliza el nombre de la interfaz:

```c
snprintf(name, IFNAMSIZ, "%s%d", drv->cdevsw.d_name, unit);
```

Si el usuario especificó `ifconfig tun create` sin número de unidad, esto da formato al nombre con la unidad recién asignada, produciendo cadenas como «tun0» o «tun1». El parámetro `name` sirve tanto de entrada como de salida: el buffer del llamador recibe el nombre definitivo.

##### Creación del dispositivo de caracteres

```c
dev = NULL;
i = clone_create(&drv->clones, &drv->cdevsw, &unit, &dev, 0);
if (i != 0)
    i = tun_create_device(drv, unit, NULL, &dev, name);
```

Esta sección gestiona una sutileza importante: el dispositivo de caracteres puede ya existir. La llamada a `clone_create` busca un nodo de dispositivo `/dev/tun0` existente, que podría haberse creado antes a través de la clonación de devfs cuando un proceso abrió la ruta del dispositivo.

Cuando `clone_create` devuelve un valor distinto de cero (dispositivo no encontrado), el código llama a `tun_create_device` para construir un nuevo `struct cdev`. Este enfoque de doble camino contempla dos escenarios de creación:

1. Un proceso abre `/dev/tun0` antes de cualquier configuración de red, lo que activa la clonación de devfs
2. Un usuario ejecuta `ifconfig tun0 create`, solicitando explícitamente la creación de la interfaz

##### Instanciación de la interfaz de red

El paso final conecta el dispositivo de caracteres con el subsistema de red:

```c
if (i == 0) {
    dev_ref(dev);
    tuncreate(dev);
    struct tuntap_softc *tp = dev->si_drv1;
    *ifpp = tp->tun_ifp;
}
```

Tras la creación o búsqueda exitosa del dispositivo:

- `dev_ref(dev)` incrementa el contador de referencias del dispositivo, evitando su destrucción prematura durante la inicialización
- `tuncreate(dev)` asigna e inicializa la estructura `ifnet`, registrándola en la pila de red
- `dev->si_drv1` proporciona el vínculo crítico: este campo apunta a la estructura `tuntap_softc`, que contiene tanto el estado del dispositivo de caracteres como el puntero al `ifnet`
- `*ifpp = tp->tun_ifp` devuelve la interfaz de red recién creada al subsistema if_clone

##### Arquitectura de coordinación

La función `tun_clone_create` ejemplifica un patrón de coordinación habitual en los drivers del kernel. No realiza trabajo pesado por sí misma; en cambio, orquesta varios subsistemas:

1. El análisis del nombre determina el tipo y la unidad del dispositivo
2. La búsqueda del driver selecciona la tabla de despacho `cdevsw` apropiada
3. La asignación de la unidad garantiza la unicidad
4. La búsqueda o creación del dispositivo establece la presencia del dispositivo de caracteres
5. La creación de la interfaz la registra en la pila de red

Esta separación permite que dos caminos de creación independientes (el acceso al dispositivo de caracteres y la configuración de red) converjan correctamente independientemente del orden de invocación.

El campo `si_drv1` actúa como la clave de bóveda arquitectónica, vinculando el mundo del dispositivo de caracteres (`struct cdev`, operaciones de archivo, espacio de nombres `/dev`) con el mundo de la red (`struct ifnet`, procesamiento de paquetes, visibilidad en `ifconfig`). Toda operación posterior, ya sea una llamada al sistema `read(2)` o la transmisión de un paquete, recorrerá este vínculo para acceder al estado compartido de `tuntap_softc`.

#### 2.2 Crear el `cdev` y conectar `si_drv1` (`tun_create_device`)

```c
 807: static int
 808: tun_create_device(struct tuntap_driver *drv, int unit, struct ucred *cr,
 809:     struct cdev **dev, const char *name)
 810: {
 811: 	struct make_dev_args args;
 812: 	struct tuntap_softc *tp;
 813: 	int error;
 814: 
 815: 	tp = malloc(sizeof(*tp), M_TUN, M_WAITOK | M_ZERO);
 816: 	mtx_init(&tp->tun_mtx, "tun_mtx", NULL, MTX_DEF);
 817: 	cv_init(&tp->tun_cv, "tun_condvar");
 818: 	tp->tun_flags = drv->ident_flags;
 819: 	tp->tun_drv = drv;
 820: 
 821: 	make_dev_args_init(&args);
 822: 	if (cr != NULL)
 823: 		args.mda_flags = MAKEDEV_REF | MAKEDEV_CHECKNAME;
 824: 	args.mda_devsw = &drv->cdevsw;
 825: 	args.mda_cr = cr;
 826: 	args.mda_uid = UID_UUCP;
 827: 	args.mda_gid = GID_DIALER;
 828: 	args.mda_mode = 0600;
 829: 	args.mda_unit = unit;
 830: 	args.mda_si_drv1 = tp;
 831: 	error = make_dev_s(&args, dev, "%s", name);
 832: 	if (error != 0) {
 833: 		free(tp, M_TUN);
 834: 		return (error);
 835: 	}
 836: 
 837: 	KASSERT((*dev)->si_drv1 != NULL,
 838: 	    ("Failed to set si_drv1 at %s creation", name));
 839: 	tp->tun_dev = *dev;
 840: 	knlist_init_mtx(&tp->tun_rsel.si_note, &tp->tun_mtx);
 841: 	mtx_lock(&tunmtx);
 842: 	TAILQ_INSERT_TAIL(&tunhead, tp, tun_list);
 843: 	mtx_unlock(&tunmtx);
 844: 	return (0);
 845: }
```

La función `tun_create_device` construye el nodo de dispositivo de caracteres y el estado del driver asociado. Es aquí donde `/dev/tun0`, `/dev/tap0` o `/dev/vmnet0` cobran existencia realmente en el sistema de archivos de dispositivos.

##### Parámetros de la función

```c
static int
tun_create_device(struct tuntap_driver *drv, int unit, struct ucred *cr,
    struct cdev **dev, const char *name)
```

La función acepta:

- `drv`: puntero a la entrada apropiada en `tuntap_drivers[]`
- `unit`: el número de unidad de dispositivo asignado (0, 1, 2, etc.)
- `cr`: contexto de credenciales (NULL para creación iniciada por el kernel, distinto de NULL para creación iniciada por el usuario)
- `dev`: parámetro de salida que recibe el puntero al `struct cdev` creado
- `name`: la cadena con el nombre completo del dispositivo («tun0», «tap3», etc.)

##### Asignación de la estructura softc

```c
tp = malloc(sizeof(*tp), M_TUN, M_WAITOK | M_ZERO);
mtx_init(&tp->tun_mtx, "tun_mtx", NULL, MTX_DEF);
cv_init(&tp->tun_cv, "tun_condvar");
tp->tun_flags = drv->ident_flags;
tp->tun_drv = drv;
```

Cada instancia de dispositivo tun/tap/vmnet requiere una estructura `tuntap_softc` para mantener su estado. Esta estructura contiene todo lo necesario para operar el dispositivo: indicadores, el puntero a la interfaz de red asociada, primitivas de sincronización de E/S y referencias al driver.

La asignación usa `M_WAITOK`, lo que permite a la función dormir si la memoria no está disponible temporalmente. El indicador `M_ZERO` garantiza que todos los campos se inicialicen a cero, proporcionando valores predeterminados seguros para punteros y contadores.

Se inicializan dos primitivas de sincronización:

- `tun_mtx`: un mutex que protege los campos mutables del softc
- `tun_cv`: una variable de condición utilizada durante la destrucción del dispositivo para esperar a que todas las operaciones finalicen

El campo `tun_flags` recibe los indicadores de identidad del driver (0, TUN_L2 o TUN_L2|TUN_VMNET), estableciendo si esta instancia se comporta como un dispositivo tun, tap o vmnet. El puntero inverso `tun_drv` permite al softc acceder a los recursos del driver padre, como el asignador de números de unidad.

##### Preparación de los argumentos de creación del dispositivo

La API moderna de creación de dispositivos de FreeBSD usa una estructura para pasar parámetros en lugar de una larga lista de argumentos:

```c
make_dev_args_init(&args);
if (cr != NULL)
    args.mda_flags = MAKEDEV_REF | MAKEDEV_CHECKNAME;
args.mda_devsw = &drv->cdevsw;
args.mda_cr = cr;
args.mda_uid = UID_UUCP;
args.mda_gid = GID_DIALER;
args.mda_mode = 0600;
args.mda_unit = unit;
args.mda_si_drv1 = tp;
```

La estructura `make_dev_args` configura cada aspecto del nodo de dispositivo:

**Indicadores**: cuando `cr` es distinto de NULL (creación iniciada por el usuario), se establecen dos indicadores:

- `MAKEDEV_REF`: añade automáticamente una referencia para evitar la destrucción inmediata
- `MAKEDEV_CHECKNAME`: valida que el nombre no entre en conflicto con dispositivos existentes

**Tabla de despacho**: `mda_devsw` apunta al `cdevsw` que contiene los punteros de función para `open`, `read`, `write`, `ioctl`, etc. Así es como el kernel sabe qué funciones invocar cuando el espacio de usuario realiza operaciones sobre este dispositivo.

**Credenciales**: `mda_cr` asocia las credenciales del usuario creador con el dispositivo, utilizadas para las comprobaciones de permisos.

**Propiedad y permisos**: el nodo de dispositivo será propiedad del usuario `uucp` y del grupo `dialer` con modo `0600` (lectura/escritura solo para el propietario). Estas convenciones históricas de Unix reflejan el uso original de los dispositivos serie para las conexiones de acceso telefónico. En la práctica, los administradores suelen ajustar estos permisos mediante `devfs.rules` o haciendo que los demonios con privilegios abran los dispositivos.

**Número de unidad**: `mda_unit` incrusta el número de unidad en el número de dispositivo menor, lo que permite al kernel distinguir `/dev/tun0` de `/dev/tun1`.

**Datos privados**: `mda_si_drv1` es clave aquí: este campo se convertirá en el miembro `si_drv1` del `struct cdev` creado, estableciendo el vínculo del dispositivo de caracteres al estado del driver. Toda operación posterior sobre el dispositivo recuperará el softc a través de este campo.

##### Creación del nodo de dispositivo

```c
error = make_dev_s(&args, dev, "%s", name);
if (error != 0) {
    free(tp, M_TUN);
    return (error);
}
```

La llamada a `make_dev_s` crea el `struct cdev` y lo registra en devfs. Si tiene éxito, `*dev` recibe un puntero a la nueva estructura de dispositivo. La cadena de formato `"%s"` y el argumento `name` especifican la ruta del nodo de dispositivo dentro de `/dev`.

Los modos de fallo más habituales son:

- Conflictos de nombre (ya existe un dispositivo con ese nombre)
- Agotamiento de recursos (memoria del kernel insuficiente)
- Errores del subsistema devfs

Si falla, la función desasigna inmediatamente el softc y devuelve el error al llamador. Esto evita fugas de recursos.

##### Finalización del estado del dispositivo

```c
KASSERT((*dev)->si_drv1 != NULL,
    ("Failed to set si_drv1 at %s creation", name));
tp->tun_dev = *dev;
knlist_init_mtx(&tp->tun_rsel.si_note, &tp->tun_mtx);
```

El `KASSERT` es una comprobación de integridad en tiempo de desarrollo que verifica que `make_dev_s` rellenó correctamente `si_drv1` a partir de `mda_si_drv1`. Esta aserción se dispararía durante el desarrollo del kernel si la lógica de creación del dispositivo fallase, pero se elimina en las compilaciones de producción.

La asignación `tp->tun_dev` crea el enlace inverso: mientras que `si_drv1` apunta del cdev al softc, `tun_dev` apunta del softc al cdev. Esta vinculación bidireccional permite que el código recorra el vínculo en cualquier dirección.

La llamada a `knlist_init_mtx` inicializa la lista de notificaciones kqueue protegida por el mutex del softc. Esta infraestructura da soporte a la monitorización de eventos mediante `kqueue(2)`, lo que permite que las aplicaciones en espacio de usuario esperen eficientemente condiciones de lectura/escritura sobre el dispositivo.

##### Registro global

```c
mtx_lock(&tunmtx); 
TAILQ_INSERT_TAIL(&tunhead, tp, tun_list); 
mtx_unlock(&tunmtx); 
return (0);
```

Por último, el nuevo dispositivo se registra en la lista global `tunhead`. Esta lista permite al driver enumerar todas las instancias activas de tun/tap/vmnet, algo necesario durante la descarga del módulo o en operaciones a escala de todo el sistema.

El mutex `tunmtx` protege la lista de modificaciones concurrentes. Varios threads podrían crear dispositivos simultáneamente, por lo que este lock garantiza la consistencia de la lista.

##### El estado del dispositivo creado

Al terminar la función, existen varios objetos del kernel correctamente enlazados:

```html
/dev/tun0 (struct cdev)
     ->  si_drv1
tuntap_softc
     ->  tun_dev
/dev/tun0 (struct cdev)
     ->  tun_drv
tuntap_drivers[0]
```

El softc está registrado en la lista global de dispositivos y listo tanto para operaciones de dispositivo de caracteres como para la vinculación con la interfaz de red. Sin embargo, la interfaz de red (`ifnet`) todavía no existe; la creará la función `tuncreate`.

Esta separación de responsabilidades, entre la creación del dispositivo de caracteres y la creación de la interfaz de red, permite que los dos subsistemas se inicialicen de forma independiente y en el orden que resulte conveniente.

#### 2.3 Construir y vincular el `ifnet` (`tuncreate`): L2 (tap) frente a L3 (tun)

```c
 950: static void
 951: tuncreate(struct cdev *dev)
 952: {
 953: 	struct tuntap_driver *drv;
 954: 	struct tuntap_softc *tp;
 955: 	struct ifnet *ifp;
 956: 	struct ether_addr eaddr;
 957: 	int iflags;
 958: 	u_char type;
 959: 
 960: 	tp = dev->si_drv1;
 961: 	KASSERT(tp != NULL,
 962: 	    ("si_drv1 should have been initialized at creation"));
 963: 
 964: 	drv = tp->tun_drv;
 965: 	iflags = IFF_MULTICAST;
 966: 	if ((tp->tun_flags & TUN_L2) != 0) {
 967: 		type = IFT_ETHER;
 968: 		iflags |= IFF_BROADCAST | IFF_SIMPLEX;
 969: 	} else {
 970: 		type = IFT_PPP;
 971: 		iflags |= IFF_POINTOPOINT;
 972: 	}
 973: 	ifp = tp->tun_ifp = if_alloc(type);
 974: 	ifp->if_softc = tp;
 975: 	if_initname(ifp, drv->cdevsw.d_name, dev2unit(dev));
 976: 	ifp->if_ioctl = tunifioctl;
 977: 	ifp->if_flags = iflags;
 978: 	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
 979: 	ifp->if_capabilities |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
 980: 	if ((tp->tun_flags & TUN_L2) != 0)
 981: 		ifp->if_capabilities |=
 982: 		    IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 | IFCAP_LRO;
 983: 	ifp->if_capenable |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
 984: 
 985: 	if ((tp->tun_flags & TUN_L2) != 0) {
 986: 		ifp->if_init = tunifinit;
 987: 		ifp->if_start = tunstart_l2;
 988: 		ifp->if_transmit = tap_transmit;
 989: 		ifp->if_qflush = if_qflush;
 990: 
 991: 		ether_gen_addr(ifp, &eaddr);
 992: 		ether_ifattach(ifp, eaddr.octet);
 993: 	} else {
 994: 		ifp->if_mtu = TUNMTU;
 995: 		ifp->if_start = tunstart;
 996: 		ifp->if_output = tunoutput;
 997: 
 998: 		ifp->if_snd.ifq_drv_maxlen = 0;
 999: 		IFQ_SET_READY(&ifp->if_snd);
1000: 
1001: 		if_attach(ifp);
1002: 		bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
1003: 	}
1004: 
1005: 	TUN_LOCK(tp);
1006: 	tp->tun_flags |= TUN_INITED;
1007: 	TUN_UNLOCK(tp);
1008: 
1009: 	TUNDEBUG(ifp, "interface %s is created, minor = %#x\n",
1010: 	    ifp->if_xname, dev2unit(dev));
1011: }
```

La función `tuncreate` construye y registra la interfaz de red (`ifnet`) correspondiente a un dispositivo de caracteres. Una vez que esta función termina, el dispositivo aparece en la salida de `ifconfig` y puede participar en operaciones de red. Aquí es donde convergen el mundo del dispositivo de caracteres y la pila de red.

##### Recuperación del contexto del driver

```c
tp = dev->si_drv1;
KASSERT(tp != NULL,
    ("si_drv1 should have been initialized at creation"));

drv = tp->tun_drv;
```

La función comienza recorriendo el enlace desde `struct cdev` hasta `tuntap_softc` establecido durante la creación del dispositivo. La aserción verifica este invariante fundamental: todo dispositivo debe tener un softc asociado. El campo `tun_drv` proporciona acceso a los recursos y la configuración a nivel de driver.

##### Determinación del tipo de interfaz y los flags

```c
iflags = IFF_MULTICAST;
if ((tp->tun_flags & TUN_L2) != 0) {
    type = IFT_ETHER;
    iflags |= IFF_BROADCAST | IFF_SIMPLEX;
} else {
    type = IFT_PPP;
    iflags |= IFF_POINTOPOINT;
}
```

El tipo de interfaz y los flags de comportamiento dependen de si se trata de un túnel de capa 2 (Ethernet) o de capa 3 (IP):

**Dispositivos de capa 2** (tap/vmnet con `TUN_L2` activado):

- `IFT_ETHER` - declara la interfaz como Ethernet
- `IFF_BROADCAST` - admite transmisión en broadcast
- `IFF_SIMPLEX` - no puede recibir sus propias transmisiones (estándar en Ethernet)
- `IFF_MULTICAST` - admite grupos multicast

**Dispositivos de capa 3** (tun sin `TUN_L2`):

- `IFT_PPP` - declara la interfaz como punto a punto (PPP)
- `IFF_POINTOPOINT` - tiene exactamente un par extremo (sin dominio de broadcast)
- `IFF_MULTICAST` - admite multicast (aunque tiene menos relevancia en punto a punto)

Estos flags controlan cómo trata la pila de red a la interfaz. Por ejemplo, el código de enrutamiento usa `IFF_POINTOPOINT` para determinar si una ruta necesita una dirección de pasarela o solo una dirección de destino.

##### Asignación e inicialización de la interfaz

```c
ifp = tp->tun_ifp = if_alloc(type);
ifp->if_softc = tp;
if_initname(ifp, drv->cdevsw.d_name, dev2unit(dev));
```

La función `if_alloc` asigna un `struct ifnet` del tipo especificado. Esta estructura es la representación que la pila de red tiene de la interfaz, y contiene colas de paquetes, contadores de estadísticas, flags de capacidades y punteros a funciones.

Se establecen tres vínculos críticos:

1. `tp->tun_ifp = if_alloc(type)` - el softc apunta al ifnet
2. `ifp->if_softc = tp` - el ifnet apunta de vuelta al softc
3. `if_initname(ifp, drv->cdevsw.d_name, dev2unit(dev))` - asocia el nombre de la interfaz ("tun0") con el ifnet

La vinculación bidireccional permite que el código que trabaja con cualquiera de las dos representaciones acceda a la otra. El código de red que recibe un paquete puede encontrar el estado del dispositivo de caracteres; las operaciones sobre el dispositivo de caracteres pueden acceder a las estadísticas de red.

##### Configuración de las operaciones de la interfaz

```c
ifp->if_ioctl = tunifioctl;
ifp->if_flags = iflags;
IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
```

El puntero a función `if_ioctl` gestiona las solicitudes de configuración de la interfaz, como `SIOCSIFADDR` (establecer dirección), `SIOCSIFMTU` (establecer MTU) y `SIOCSIFFLAGS` (establecer flags). Esto es distinto del manejador `ioctl` del dispositivo de caracteres, que procesa comandos específicos del dispositivo.

Los flags de la interfaz se copian del valor `iflags` determinado anteriormente. La longitud máxima de la cola de envío se establece en `ifqmaxlen` (normalmente 50), lo que limita cuántos paquetes pueden estar pendientes de transmisión hacia el espacio de usuario.

##### Configuración de las capacidades de la interfaz

```c
ifp->if_capabilities |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
if ((tp->tun_flags & TUN_L2) != 0)
    ifp->if_capabilities |=
        IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 | IFCAP_LRO;
ifp->if_capenable |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
```

Las capacidades de la interfaz declaran qué funciones de descarga de hardware admite el dispositivo. Existen dos conjuntos de flags:

- `if_capabilities` - funciones que la interfaz puede soportar
- `if_capenable` - funciones actualmente habilitadas

Todas las interfaces admiten:

- `IFCAP_LINKSTATE` - puede notificar cambios de estado del enlace (activo/inactivo)
- `IFCAP_MEXTPG` - admite mbufs externos multipágina (optimización de copia cero)

Las interfaces de capa 2 admiten además:

- `IFCAP_RXCSUM` - descarga de suma de verificación en recepción para IPv4
- `IFCAP_RXCSUM_IPV6` - descarga de suma de verificación en recepción para IPv6
- `IFCAP_LRO` - Large Receive Offload (coalescencia de segmentos TCP)

Estas capacidades están deshabilitadas inicialmente en los dispositivos tap/vmnet. Cuando el espacio de usuario activa el modo de cabecera virtio-net mediante el ioctl `TAPSVNETHDR`, se habilitan capacidades de transmisión adicionales y el código actualiza estos flags en consecuencia.

##### Registro de la interfaz de capa 2

```c
if ((tp->tun_flags & TUN_L2) != 0) {
    ifp->if_init = tunifinit;
    ifp->if_start = tunstart_l2;
    ifp->if_transmit = tap_transmit;
    ifp->if_qflush = if_qflush;

    ether_gen_addr(ifp, &eaddr);
    ether_ifattach(ifp, eaddr.octet);
```

Para las interfaces Ethernet, cuatro punteros a funciones configuran el procesamiento de paquetes:

- `if_init` - se invoca cuando la interfaz pasa al estado activo
- `if_start` - transmisión de paquetes legada (invocada por la cola de envío)
- `if_transmit` - transmisión de paquetes moderna (omite la cola de envío cuando es posible)
- `if_qflush` - descarta los paquetes en cola

La función `ether_gen_addr` genera una dirección MAC aleatoria para el extremo local del túnel. La dirección utiliza el patrón de bits de administración local, lo que garantiza que no entre en conflicto con direcciones de hardware reales.

`ether_ifattach` realiza el registro específico de Ethernet:

- Registra la interfaz en la pila de red
- Asocia BPF (Berkeley Packet Filter) con el tipo de enlace `DLT_EN10MB` (Ethernet)
- Inicializa la estructura de dirección de capa de enlace de la interfaz
- Configura la gestión del filtro multicast

Tras `ether_ifattach`, la interfaz está completamente operativa y visible para las herramientas del espacio de usuario.

##### Registro de la interfaz de capa 3

```c
} else {
    ifp->if_mtu = TUNMTU;
    ifp->if_start = tunstart;
    ifp->if_output = tunoutput;

    ifp->if_snd.ifq_drv_maxlen = 0;
    IFQ_SET_READY(&ifp->if_snd);

    if_attach(ifp);
    bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
}
```

Las interfaces punto a punto siguen un camino más sencillo:

El MTU se establece en `TUNMTU` (normalmente 1500) y se instalan dos funciones de transmisión de paquetes:

- `if_start` - gestiona los paquetes de la cola de envío
- `if_output` - invocada directamente por el código de enrutamiento

El ajuste `if_snd.ifq_drv_maxlen = 0` es significativo: impide que la cola de envío legada retenga paquetes, ya que el camino moderno usa la semántica de `if_transmit` aunque el puntero a función no esté definido. `IFQ_SET_READY` marca la cola como operativa.

`if_attach` registra la interfaz en la pila de red, haciéndola visible para las herramientas de enrutamiento y configuración.

`bpfattach` habilita la captura de paquetes con el tipo de enlace `DLT_NULL`. Este tipo de enlace antepone un campo de familia de direcciones de 4 bytes (AF_INET o AF_INET6) a cada paquete, lo que permite que herramientas como `tcpdump` distingan el tráfico IPv4 del IPv6 sin examinar el contenido de los paquetes.

##### Marca de inicialización completa

```c
TUN_LOCK(tp);
tp->tun_flags |= TUN_INITED;
TUN_UNLOCK(tp);
```

El flag `TUN_INITED` señala que la interfaz está completamente construida. Otros caminos de código comprueban este flag antes de realizar operaciones. Por ejemplo, la función `open` del dispositivo verifica que tanto `TUN_INITED` como `TUN_OPEN` estén activos antes de permitir I/O.

El mutex protege este flag de condiciones de carrera en las que un thread comprueba el estado mientras otro sigue inicializando.

##### La interfaz completada

Tras el retorno de `tuncreate`, tanto el dispositivo de caracteres como la interfaz de red existen y están enlazados bidireccionalmente:

```html
/dev/tun0 (struct cdev)
     <->  si_drv1 / tun_dev
tuntap_softc
     <->  if_softc / tun_ifp
tun0 (struct ifnet)
```

Abrir `/dev/tun0` con `open(2)` permite al espacio de usuario leer y escribir paquetes. Los paquetes enviados a la interfaz `tun0` mediante `sendto(2)` o a través del código de enrutamiento quedan en cola para que el espacio de usuario los lea. Esta conexión bidireccional permite que el software VPN y de virtualización en espacio de usuario implemente protocolos de red personalizados mientras se integra en la pila de red del kernel.

#### 3) `open(2)`: contexto vnet, marcar como abierto, activar enlace

```c
1064: static int
1065: tunopen(struct cdev *dev, int flag, int mode, struct thread *td)
1066: {
1067: 	struct ifnet	*ifp;
1068: 	struct tuntap_softc *tp;
1069: 	int error __diagused, tunflags;
1070: 
1071: 	tunflags = 0;
1072: 	CURVNET_SET(TD_TO_VNET(td));
1073: 	error = tuntap_name2info(dev->si_name, NULL, &tunflags);
1074: 	if (error != 0) {
1075: 		CURVNET_RESTORE();
1076: 		return (error);	/* Shouldn't happen */
1077: 	}
1078: 
1079: 	tp = dev->si_drv1;
1080: 	KASSERT(tp != NULL,
1081: 	    ("si_drv1 should have been initialized at creation"));
1082: 
1083: 	TUN_LOCK(tp);
1084: 	if ((tp->tun_flags & TUN_INITED) == 0) {
1085: 		TUN_UNLOCK(tp);
1086: 		CURVNET_RESTORE();
1087: 		return (ENXIO);
1088: 	}
1089: 	if ((tp->tun_flags & (TUN_OPEN | TUN_DYING)) != 0) {
1090: 		TUN_UNLOCK(tp);
1091: 		CURVNET_RESTORE();
1092: 		return (EBUSY);
1093: 	}
1094: 
1095: 	error = tun_busy_locked(tp);
1096: 	KASSERT(error == 0, ("Must be able to busy an unopen tunnel"));
1097: 	ifp = TUN2IFP(tp);
1098: 
1099: 	if ((tp->tun_flags & TUN_L2) != 0) {
1100: 		bcopy(IF_LLADDR(ifp), tp->tun_ether.octet,
1101: 		    sizeof(tp->tun_ether.octet));
1102: 
1103: 		ifp->if_drv_flags |= IFF_DRV_RUNNING;
1104: 		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
1105: 
1106: 		if (tapuponopen)
1107: 			ifp->if_flags |= IFF_UP;
1108: 	}
1109: 
1110: 	tp->tun_pid = td->td_proc->p_pid;
1111: 	tp->tun_flags |= TUN_OPEN;
1112: 
1113: 	if_link_state_change(ifp, LINK_STATE_UP);
1114: 	TUNDEBUG(ifp, "open\n");
1115: 	TUN_UNLOCK(tp);
1116: 	/* ... cdevpriv setup ... */
1117: 	(void)devfs_set_cdevpriv(tp, tundtor);
1118: 	CURVNET_RESTORE();
1119: 	return (0);
1120: }
```

La función `tunopen` gestiona la llamada al sistema `open(2)` sobre dispositivos de caracteres tun/tap/vmnet. Este es el punto de entrada donde aplicaciones del espacio de usuario como demonios VPN o monitores de máquinas virtuales toman el control de una interfaz de red. Abrir el dispositivo lo hace pasar de un estado inicializado pero inactivo a un estado operativo listo para I/O de paquetes.

##### Firma de la función y contexto de red virtual

```c
static int
tunopen(struct cdev *dev, int flag, int mode, struct thread *td)
{
    CURVNET_SET(TD_TO_VNET(td));
```

La función recibe los parámetros estándar de `open` para un dispositivo de caracteres: el dispositivo que se abre, los flags de la llamada a `open(2)`, los bits de modo y el thread que realiza la operación.

La macro `CURVNET_SET` es fundamental para el soporte VNET (pila de red virtual) de FreeBSD. En sistemas que utilizan jails o virtualización, pueden existir múltiples pilas de red independientes. Esta macro conmuta al contexto de red asociado con el jail o vnet del thread que realiza la apertura, asegurando que todas las operaciones de red subsiguientes afecten a la pila correcta. Toda función que acceda a interfaces de red o tablas de enrutamiento debe encuadrar su trabajo entre `CURVNET_SET` y `CURVNET_RESTORE`.

##### Validación del tipo de dispositivo

```c
tunflags = 0;
error = tuntap_name2info(dev->si_name, NULL, &tunflags);
if (error != 0) {
    CURVNET_RESTORE();
    return (error);
}
```

Aunque el dispositivo ya debería existir y tener el tipo correcto, este código valida que el nombre del dispositivo sigue correspondiendo a una variante conocida de tun/tap/vmnet. La comprobación debería tener siempre éxito, tal como indica el comentario "Shouldn't happen". La validación protege frente a estado del kernel corrompido o condiciones de carrera durante la destrucción del dispositivo.

##### Recuperación y validación del estado del dispositivo

```c
tp = dev->si_drv1;
KASSERT(tp != NULL,
    ("si_drv1 should have been initialized at creation"));

TUN_LOCK(tp);
if ((tp->tun_flags & TUN_INITED) == 0) {
    TUN_UNLOCK(tp);
    CURVNET_RESTORE();
    return (ENXIO);
}
```

El softc se recupera a través del enlace `si_drv1` establecido durante la creación del dispositivo. La aserción verifica este invariante fundamental.

El mutex del softc se adquiere antes de comprobar los flags de estado, lo que evita condiciones de carrera. La comprobación del flag `TUN_INITED` garantiza que la interfaz de red se creó correctamente. Si la inicialización falló o aún no ha terminado, la apertura falla con `ENXIO` (dispositivo no configurado).

##### Aplicación del acceso exclusivo

```c
if ((tp->tun_flags & (TUN_OPEN | TUN_DYING)) != 0) {
    TUN_UNLOCK(tp);
    CURVNET_RESTORE();
    return (EBUSY);
}
```

Los dispositivos tun/tap aplican el acceso exclusivo: solo un proceso puede tener el dispositivo abierto en un momento dado. Este diseño simplifica el enrutamiento de paquetes, ya que siempre hay exactamente un consumidor en el espacio de usuario para los paquetes que llegan a la interfaz.

La comprobación examina dos flags:

- `TUN_OPEN` - el dispositivo ya está abierto por otro proceso
- `TUN_DYING` - el dispositivo está siendo destruido

Cualquiera de las dos condiciones devuelve `EBUSY`, informando al espacio de usuario de que el dispositivo no está disponible. Esto evita situaciones en las que varios demonios VPN compiten por el mismo túnel o en las que un proceso abre un dispositivo mientras está siendo destruido.

##### Marcar el dispositivo como ocupado

```c
error = tun_busy_locked(tp);
KASSERT(error == 0, ("Must be able to busy an unopen tunnel"));
ifp = TUN2IFP(tp);
```

El mecanismo de ocupación impide la destrucción del dispositivo mientras hay operaciones en curso. La función `tun_busy_locked` incrementa el contador `tun_busy` y falla si `TUN_DYING` está activo.

La aserción verifica que marcar el dispositivo como ocupado debe tener éxito, ya que poseemos el lock y ya hemos comprobado que ni `TUN_OPEN` ni `TUN_DYING` están activados, por lo que no puede estar produciéndose ninguna destrucción concurrente.

La macro `TUN2IFP` extrae el puntero `ifnet` del softc, proporcionando acceso a la interfaz de red para la configuración posterior.

##### Activación de la interfaz de capa 2

```c
if ((tp->tun_flags & TUN_L2) != 0) {
    bcopy(IF_LLADDR(ifp), tp->tun_ether.octet,
        sizeof(tp->tun_ether.octet));

    ifp->if_drv_flags |= IFF_DRV_RUNNING;
    ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

    if (tapuponopen)
        ifp->if_flags |= IFF_UP;
}
```

En las interfaces Ethernet (tap/vmnet), abrir el dispositivo activa varias funcionalidades:

La dirección MAC se copia desde la interfaz a `tp->tun_ether`. Esta instantánea conserva la dirección MAC "remota" que el espacio de usuario podría necesitar. Aunque la propia interfaz conoce su dirección MAC local, el softc almacena esta copia para facilitar patrones de acceso simétricos.

Se actualizan dos flags del driver:

- `IFF_DRV_RUNNING`: indica que el driver está listo para transmitir y recibir.
- `IFF_DRV_OACTIVE`: se borra para indicar que la salida no está bloqueada.

Estos "flags del driver" (`if_drv_flags`) son distintos de los flags de interfaz (`if_flags`). Los flags del driver reflejan el estado interno del driver de dispositivo, mientras que los flags de interfaz reflejan propiedades configuradas administrativamente.

El sysctl `tapuponopen` controla si al abrir el dispositivo se marca automáticamente la interfaz como activa desde el punto de vista administrativo. Cuando está habilitado, `ifp->if_flags |= IFF_UP` levanta la interfaz sin necesidad de ejecutar el comando `ifconfig tap0 up` por separado. Esta funcionalidad de conveniencia está deshabilitada por defecto para mantener la semántica tradicional de Unix, en la que la disponibilidad del dispositivo y el estado de la interfaz son ortogonales.

##### Registro de la propiedad

```c
tp->tun_pid = td->td_proc->p_pid;
tp->tun_flags |= TUN_OPEN;
```

El PID del proceso controlador se registra en `tun_pid`. Esta información aparece en la salida de `ifconfig` y ayuda a los administradores a identificar qué proceso es propietario de cada túnel. Aunque no se utiliza para el control de acceso (que lo proporciona el descriptor de archivo), resulta muy útil para la depuración y la monitorización.

Se activa el flag `TUN_OPEN`, lo que hace transicionar el dispositivo al estado abierto. Los intentos de apertura posteriores fallarán con `EBUSY` hasta que este proceso cierre el dispositivo.

##### Señalización del estado del enlace

```c
if_link_state_change(ifp, LINK_STATE_UP);
TUNDEBUG(ifp, "open\n");
TUN_UNLOCK(tp);
```

La llamada a `if_link_state_change` notifica a la pila de red que el enlace de la interfaz está activo. Esto genera mensajes del socket de enrutamiento que demonios como `devd` pueden monitorizar, y actualiza el estado del enlace de la interfaz visible en la salida de `ifconfig`.

En las interfaces Ethernet físicas, el estado del enlace refleja el estado de la conexión del cable. En los dispositivos tun/tap, el estado del enlace refleja si el espacio de usuario tiene abierto el dispositivo. Este mapeo semántico permite que los protocolos de enrutamiento y las herramientas de gestión traten las interfaces virtuales de forma coherente con las físicas.

El mensaje de depuración registra el evento de apertura, y el mutex se libera antes del paso de configuración final.

##### Establecimiento de la notificación de cierre

```c
(void)devfs_set_cdevpriv(tp, tundtor);
CURVNET_RESTORE();
return (0);
```

La llamada a `devfs_set_cdevpriv` asocia el softc con este descriptor de archivo y registra `tundtor` (destructor del túnel) como función de limpieza. Cuando el descriptor de archivo se cierra, ya sea explícitamente mediante `close(2)` o implícitamente al terminar el proceso, el kernel invoca automáticamente a `tundtor` para desmantelar el estado del dispositivo.

Este mecanismo proporciona semánticas de limpieza robustas. Incluso si un proceso se bloquea o es eliminado, el kernel garantiza el apagado correcto del dispositivo. La asociación entre el puntero a función y los datos es por descriptor de archivo, lo que permite que el mismo dispositivo se abra varias veces de forma sucesiva (aunque no de forma concurrente) con una limpieza correcta para cada instancia.

El valor de retorno 0 indica que la apertura ha sido exitosa. En este punto, el espacio de usuario puede comenzar a leer los paquetes transmitidos a la interfaz y a escribir paquetes para inyectarlos en la pila de red.

##### Transiciones de estado

La operación de apertura hace que el dispositivo pase por varios estados:

```html
Device created  ->  TUN_INITED set
     -> 
tunopen() called
     -> 
Check exclusive access
     -> 
Mark busy (prevent destruction)
     -> 
Configure interface (L2: set RUNNING, optionally set UP)
     -> 
Record owner PID
     -> 
Set TUN_OPEN flag
     -> 
Signal link state UP
     -> 
Register close handler
     -> 
Device ready for I/O
```

Tras una apertura exitosa, el dispositivo existe en tres representaciones interrelacionadas:

- Nodo de dispositivo de caracteres (`/dev/tun0`) con un descriptor de archivo abierto.
- Interfaz de red (`tun0`) con estado del enlace UP.
- Estructura softc que las une con `TUN_OPEN` activado.

Los paquetes pueden fluir ahora de forma bidireccional: la pila de red encola los paquetes salientes para que el espacio de usuario los lea, y el espacio de usuario escribe los paquetes entrantes para que la pila de red los procese.

#### 4) `read(2)`: el espacio de usuario **recibe** un paquete completo (o EWOULDBLOCK)

```c
1706: /*
1707:  * The cdevsw read interface - reads a packet at a time, or at
1708:  * least as much of a packet as can be read.
1709:  */
1710: static	int
1711: tunread(struct cdev *dev, struct uio *uio, int flag)
1712: {
1713: 	struct tuntap_softc *tp = dev->si_drv1;
1714: 	struct ifnet	*ifp = TUN2IFP(tp);
1715: 	struct mbuf	*m;
1716: 	size_t		len;
1717: 	int		error = 0;
1718: 
1719: 	TUNDEBUG (ifp, "read\n");
1720: 	TUN_LOCK(tp);
1721: 	if ((tp->tun_flags & TUN_READY) != TUN_READY) {
1722: 		TUN_UNLOCK(tp);
1723: 		TUNDEBUG (ifp, "not ready 0%o\n", tp->tun_flags);
1724: 		return (EHOSTDOWN);
1725: 	}
1726: 
1727: 	tp->tun_flags &= ~TUN_RWAIT;
1728: 
1729: 	for (;;) {
1730: 		IFQ_DEQUEUE(&ifp->if_snd, m);
1731: 		if (m != NULL)
1732: 			break;
1733: 		if (flag & O_NONBLOCK) {
1734: 			TUN_UNLOCK(tp);
1735: 			return (EWOULDBLOCK);
1736: 		}
1737: 		tp->tun_flags |= TUN_RWAIT;
1738: 		error = mtx_sleep(tp, &tp->tun_mtx, PCATCH | (PZERO + 1),
1739: 		    "tunread", 0);
1740: 		if (error != 0) {
1741: 			TUN_UNLOCK(tp);
1742: 			return (error);
1743: 		}
1744: 	}
1745: 	TUN_UNLOCK(tp);
1746: 
1747: 	len = min(tp->tun_vhdrlen, uio->uio_resid);
1748: 	if (len > 0) {
1749: 		struct virtio_net_hdr_mrg_rxbuf vhdr;
1750: 
1751: 		bzero(&vhdr, sizeof(vhdr));
1752: 		if (m->m_pkthdr.csum_flags & TAP_ALL_OFFLOAD) {
1753: 			m = virtio_net_tx_offload(ifp, m, false, &vhdr.hdr);
1754: 		}
1755: 
1756: 		TUNDEBUG(ifp, "txvhdr: f %u, gt %u, hl %u, "
1757: 		    "gs %u, cs %u, co %u\n", vhdr.hdr.flags,
1758: 		    vhdr.hdr.gso_type, vhdr.hdr.hdr_len,
1759: 		    vhdr.hdr.gso_size, vhdr.hdr.csum_start,
1760: 		    vhdr.hdr.csum_offset);
1761: 		error = uiomove(&vhdr, len, uio);
1762: 	}
1763: 	if (error == 0)
1764: 		error = m_mbuftouio(uio, m, 0);
1765: 	m_freem(m);
1766: 	return (error);
1767: }
```

La función `tunread` implementa la llamada al sistema `read(2)` para los dispositivos tun/tap, transfiriendo paquetes desde la pila de red del kernel al espacio de usuario. Este es el camino crítico donde los paquetes destinados a transmitirse por la interfaz de red virtual quedan disponibles para los demonios VPN, los monitores de máquinas virtuales u otras aplicaciones de red en espacio de usuario.

##### Visión general de la función y obtención del contexto

```c
static int
tunread(struct cdev *dev, struct uio *uio, int flag)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet *ifp = TUN2IFP(tp);
    struct mbuf *m;
    size_t len;
    int error = 0;
```

La función recibe los parámetros estándar de `read(2)`: el dispositivo que se lee, una estructura `uio` (user I/O) que describe el buffer del espacio de usuario, y los flags de la llamada `open(2)` (en particular `O_NONBLOCK`).

Los punteros al softc y a la interfaz se obtienen a través de los enlaces establecidos. El puntero `mbuf` `m` contendrá el paquete que se está transfiriendo, mientras que `len` registra cuántos datos se deben copiar.

##### Comprobación de la disponibilidad del dispositivo

```c
TUNDEBUG(ifp, "read\n");
TUN_LOCK(tp);
if ((tp->tun_flags & TUN_READY) != TUN_READY) {
    TUN_UNLOCK(tp);
    TUNDEBUG(ifp, "not ready 0%o\n", tp->tun_flags);
    return (EHOSTDOWN);
}
```

La macro `TUN_READY` combina dos flags: `TUN_OPEN | TUN_INITED`. Ambos deben estar activados para que pueda continuar la I/O:

- `TUN_INITED`: la interfaz de red se creó correctamente.
- `TUN_OPEN`: un proceso ha abierto el dispositivo.

Si alguna de las condiciones falla, la lectura devuelve `EHOSTDOWN`, lo que indica que el camino de red no está disponible. Este código de error es semánticamente apropiado: desde la perspectiva del kernel, los paquetes se están enviando a un "host" (el espacio de usuario), pero ese host está caído.

##### Preparación para la recuperación del paquete

```c
tp->tun_flags &= ~TUN_RWAIT;
```

El flag `TUN_RWAIT` indica si hay un lector bloqueado esperando paquetes. Borrarlo antes de entrar en el bucle garantiza el estado correcto independientemente de cómo se completó la lectura anterior, ya sea que haya recuperado un paquete, agotado el tiempo de espera o sido interrumpida.

##### El bucle de extracción de paquetes

```c
for (;;) {
    IFQ_DEQUEUE(&ifp->if_snd, m);
    if (m != NULL)
        break;
    if (flag & O_NONBLOCK) {
        TUN_UNLOCK(tp);
        return (EWOULDBLOCK);
    }
    tp->tun_flags |= TUN_RWAIT;
    error = mtx_sleep(tp, &tp->tun_mtx, PCATCH | (PZERO + 1),
        "tunread", 0);
    if (error != 0) {
        TUN_UNLOCK(tp);
        return (error);
    }
}
TUN_UNLOCK(tp);
```

Este bucle implementa el patrón estándar del kernel para I/O bloqueante con soporte de modo no bloqueante.

**Recuperación de paquetes**: `IFQ_DEQUEUE` elimina atómicamente el paquete cabecera de la cola de envío de la interfaz. Esta macro gestiona el bloqueo de la cola internamente y devuelve NULL si la cola está vacía.

**Camino de éxito**: cuando `m != NULL`, se ha extraído correctamente un paquete y el bucle termina.

**Camino no bloqueante**: si la cola está vacía y se especificó `O_NONBLOCK` durante `open(2)`, la lectura devuelve inmediatamente `EWOULDBLOCK` (también conocido como `EAGAIN`). Esto permite al espacio de usuario usar `poll(2)`, `select(2)` o `kqueue(2)` para esperar eficientemente condiciones de lectura sin bloquear el thread.

**Camino bloqueante**: para lecturas bloqueantes, el código:

1. Activa `TUN_RWAIT` para indicar que hay un lector esperando.
2. Llama a `mtx_sleep` para bloquear el thread de forma atómica.

La función `mtx_sleep` libera atómicamente `tp->tun_mtx` y pone el thread a dormir. Cuando se despierta (por `tunstart` o `tunstart_l2` al llegar paquetes), vuelve a adquirir el mutex antes de retornar.

Los parámetros del sleep especifican:

- `tp`: el canal de espera (puntero único arbitrario que usa el softc).
- `&tp->tun_mtx`: mutex que se libera y vuelve a adquirir atómicamente.
- `PCATCH | (PZERO + 1)`: permite la interrupción por señales, con prioridad justo por encima de la normal.
- `"tunread"`: nombre para depuración (aparece en `ps` o `top`).
- `0`: sin tiempo de espera (duerme indefinidamente).

**Gestión de señales**: si se interrumpe por una señal (como `SIGINT`), `mtx_sleep` devuelve un error (normalmente `EINTR` o `ERESTART`) y la función lo propaga al espacio de usuario. Esto permite que `Ctrl+C` interrumpa una lectura bloqueada.

Tras extraer correctamente un paquete, el mutex se libera. El resto de la función opera sobre el mbuf sin mantener locks, evitando la contención con los threads de transmisión de paquetes.

##### Procesamiento del encabezado virtio-net

```c
len = min(tp->tun_vhdrlen, uio->uio_resid);
if (len > 0) {
    struct virtio_net_hdr_mrg_rxbuf vhdr;

    bzero(&vhdr, sizeof(vhdr));
    if (m->m_pkthdr.csum_flags & TAP_ALL_OFFLOAD) {
        m = virtio_net_tx_offload(ifp, m, false, &vhdr.hdr);
    }
    /* ... debug output ... */
    error = uiomove(&vhdr, len, uio);
}
```

En los dispositivos tap configurados con el modo de encabezado virtio-net (mediante el ioctl `TAPSVNETHDR`), los paquetes van precedidos de un encabezado de metadatos que describe las funcionalidades de descarga. Esta optimización permite al espacio de usuario (especialmente a QEMU/KVM) hacer uso de las capacidades de descarga de hardware:

El campo `tun_vhdrlen` es cero en el modo estándar y distinto de cero (normalmente 10 o 12 bytes) cuando los encabezados virtio están habilitados. El código solo procesa los encabezados si tanto el encabezado está habilitado (`len > 0`) como el buffer del espacio de usuario tiene espacio (`uio->uio_resid`).

La estructura `vhdr` se inicializa a cero para proporcionar valores predeterminados seguros. Si el mbuf tiene activados los flags de descarga (`TAP_ALL_OFFLOAD` incluye la descarga de suma de verificación TCP/UDP y TSO), `virtio_net_tx_offload` rellena el encabezado con:

- Parámetros de cálculo de la suma de verificación (dónde empezar, dónde insertar).
- Parámetros de segmentación (MSS, longitud del encabezado).
- Flags genéricos (si el encabezado es válido).

La llamada `uiomove(&vhdr, len, uio)` copia el encabezado al espacio de usuario. Esta función gestiona la transferencia de memoria del kernel al espacio de usuario, actualizando `uio` para reflejar el espacio de buffer consumido. Si esta copia falla (normalmente por un puntero del espacio de usuario no válido), el error se registra pero el procesamiento continúa para liberar el mbuf.

##### Transferencia de datos del paquete

```c
if (error == 0)
    error = m_mbuftouio(uio, m, 0);
m_freem(m);
return (error);
```

Suponiendo que la transferencia del encabezado se realizó correctamente (o que no se requería ningún encabezado), `m_mbuftouio` copia los datos del paquete desde la cadena de mbufs al buffer del espacio de usuario. Esta función:

- Recorre la cadena de mbufs (los paquetes pueden estar fragmentados en varios mbufs).
- Copia cada segmento al espacio de usuario mediante `uiomove`.
- Actualiza `uio->uio_resid` para reflejar el espacio de buffer restante.
- Devuelve un error si el buffer es demasiado pequeño o los punteros no son válidos.

La llamada a `m_freem` libera el mbuf de vuelta al pool de memoria del kernel. Esto debe ejecutarse siempre, incluso si las operaciones anteriores fallaron, para evitar fugas de memoria. El mbuf se libera independientemente de si la copia tuvo éxito: una vez extraído de la cola de envío, el destino del paquete está sellado.

##### Resumen del flujo de datos

El camino completo desde la transmisión por red hasta la lectura en el espacio de usuario:

```text
Application calls send()/sendto()
     -> 
Kernel routing selects tun0 interface
     -> 
tunoutput() or tap_transmit() enqueues mbuf
     -> 
tunstart()/tunstart_l2() wakes blocked reader
     -> 
tunread() dequeues mbuf from if_snd
     -> 
Optional: Generate virtio-net header
     -> 
Copy header to userspace (if enabled)
     -> 
Copy packet data to userspace
     -> 
Free mbuf
     -> 
Userspace receives packet data
```

##### Semántica del manejo de errores

La función devuelve varios códigos de error distintos con significados específicos:

- `EHOSTDOWN`: dispositivo no listo (no abierto o no inicializado).
- `EWOULDBLOCK`: lectura no bloqueante, no hay paquetes disponibles.
- `EINTR`/`ERESTART`: interrumpido por una señal mientras esperaba.
- `EFAULT`: puntero del buffer del espacio de usuario no válido.
- `0`: éxito, paquete transferido.

Estos códigos de error permiten al espacio de usuario distinguir entre condiciones transitorias (como `EWOULDBLOCK`, que requiere reintento) y fallos permanentes (como `EHOSTDOWN`, que requiere reabrir el dispositivo).

##### Coordinación de bloqueo y despertar

El flag `TUN_RWAIT` y la coordinación con `mtx_sleep` garantizan un uso eficiente de los recursos. Cuando no hay paquetes disponibles:

1. El lector se bloquea en `mtx_sleep`, sin consumir CPU.
2. Cuando la pila de red transmite un paquete, se ejecuta `tunstart` o `tunstart_l2`.
3. Esas funciones comprueban `TUN_RWAIT` y llaman a `wakeup(tp)` si está activado.
4. El thread dormido despierta, itera y extrae el paquete.

Este patrón evita los bucles de sondeo a la vez que garantiza la entrega puntual de paquetes. El mutex protege frente a condiciones de carrera en las que los paquetes llegan entre la comprobación de cola vacía y la llamada al sleep.

#### 5) `write(2)`: el espacio de usuario **inyecta** un paquete (camino L2 frente a L3)

#### 5.1 Despachador principal de escritura (`tunwrite`)

```c
1896: /*
1897:  * the cdevsw write interface - an atomic write is a packet - or else!
1898:  */
1899: static	int
1900: tunwrite(struct cdev *dev, struct uio *uio, int flag)
1901: {
1902: 	struct virtio_net_hdr_mrg_rxbuf vhdr;
1903: 	struct tuntap_softc *tp;
1904: 	struct ifnet	*ifp;
1905: 	struct mbuf	*m;
1906: 	uint32_t	mru;
1907: 	int		align, vhdrlen, error;
1908: 	bool		l2tun;
1909: 
1910: 	tp = dev->si_drv1;
1911: 	ifp = TUN2IFP(tp);
1912: 	TUNDEBUG(ifp, "tunwrite\n");
1913: 	if ((ifp->if_flags & IFF_UP) != IFF_UP)
1914: 		/* ignore silently */
1915: 		return (0);
1916: 
1917: 	if (uio->uio_resid == 0)
1918: 		return (0);
1919: 
1920: 	l2tun = (tp->tun_flags & TUN_L2) != 0;
1921: 	mru = l2tun ? TAPMRU : TUNMRU;
1922: 	vhdrlen = tp->tun_vhdrlen;
1923: 	align = 0;
1924: 	if (l2tun) {
1925: 		align = ETHER_ALIGN;
1926: 		mru += vhdrlen;
1927: 	} else if ((tp->tun_flags & TUN_IFHEAD) != 0)
1928: 		mru += sizeof(uint32_t);	/* family */
1929: 	if (uio->uio_resid < 0 || uio->uio_resid > mru) {
1930: 		TUNDEBUG(ifp, "len=%zd!\n", uio->uio_resid);
1931: 		return (EIO);
1932: 	}
1933: 
1934: 	if (vhdrlen > 0) {
1935: 		error = uiomove(&vhdr, vhdrlen, uio);
1936: 		if (error != 0)
1937: 			return (error);
1938: 		TUNDEBUG(ifp, "txvhdr: f %u, gt %u, hl %u, "
1939: 		    "gs %u, cs %u, co %u\n", vhdr.hdr.flags,
1940: 		    vhdr.hdr.gso_type, vhdr.hdr.hdr_len,
1941: 		    vhdr.hdr.gso_size, vhdr.hdr.csum_start,
1942: 		    vhdr.hdr.csum_offset);
1943: 	}
1944: 
1945: 	if ((m = m_uiotombuf(uio, M_NOWAIT, 0, align, M_PKTHDR)) == NULL) {
1946: 		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
1947: 		return (ENOBUFS);
1948: 	}
1949: 
1950: 	m->m_pkthdr.rcvif = ifp;
1951: #ifdef MAC
1952: 	mac_ifnet_create_mbuf(ifp, m);
1953: #endif
1954: 
1955: 	if (l2tun)
1956: 		return (tunwrite_l2(tp, m, vhdrlen > 0 ? &vhdr : NULL));
1957: 
1958: 	return (tunwrite_l3(tp, m));
1959: }
```

La función `tunwrite` implementa la llamada al sistema `write(2)` para los dispositivos tun/tap, inyectando paquetes desde el espacio de usuario en la pila de red del kernel. Esta es la operación complementaria a `tunread`: mientras que `tunread` entrega los paquetes originados en el kernel al espacio de usuario, `tunwrite` acepta los paquetes del espacio de usuario para su procesamiento por el kernel. El comentario "an atomic write is a packet - or else!" subraya un principio de diseño crítico: cada llamada a `write(2)` debe contener exactamente un paquete completo.

##### Inicialización de la función y obtención del contexto

```c
static int
tunwrite(struct cdev *dev, struct uio *uio, int flag)
{
    struct virtio_net_hdr_mrg_rxbuf vhdr;
    struct tuntap_softc *tp;
    struct ifnet *ifp;
    struct mbuf *m;
    uint32_t mru;
    int align, vhdrlen, error;
    bool l2tun;

    tp = dev->si_drv1;
    ifp = TUN2IFP(tp);
```

La función obtiene el contexto del dispositivo a través del enlace estándar `si_drv1`. Las variables locales registran la unidad máxima de recepción, los requisitos de alineamiento, la longitud del encabezado virtio y si se trata de una interfaz de capa 2.

##### Validación del estado de la interfaz

```c
TUNDEBUG(ifp, "tunwrite\n");
if ((ifp->if_flags & IFF_UP) != IFF_UP)
    /* ignore silently */
    return (0);

if (uio->uio_resid == 0)
    return (0);
```

Dos comprobaciones tempranas filtran las operaciones no válidas:

**Comprobación de interfaz caída**: Si la interfaz está administrativamente desactivada (no marcada como `IFF_UP`), la escritura tiene éxito inmediatamente sin procesar el paquete. Este comportamiento de descarte silencioso difiere del camino de lectura, que devuelve `EHOSTDOWN` cuando no está lista. La asimetría tiene sentido: las aplicaciones que escriben paquetes no deberían fallar cuando la interfaz está temporalmente caída; los paquetes simplemente se descartan, imitando lo que ocurriría en una interfaz de red real sin portadora.

**Escritura de longitud cero**: Escribir cero bytes se trata como un éxito sin efecto. Esto gestiona casos límite como `write(fd, buf, 0)` sin error.

##### Determinación de los límites de tamaño del paquete

```c
l2tun = (tp->tun_flags & TUN_L2) != 0;
mru = l2tun ? TAPMRU : TUNMRU;
vhdrlen = tp->tun_vhdrlen;
align = 0;
if (l2tun) {
    align = ETHER_ALIGN;
    mru += vhdrlen;
} else if ((tp->tun_flags & TUN_IFHEAD) != 0)
    mru += sizeof(uint32_t);
```

La Unidad Máxima de Recepción (MRU) depende del tipo de interfaz:

- Capa 3 (tun): `TUNMRU` (habitualmente 1500 bytes, MTU estándar de IPv4)
- Capa 2 (tap/vmnet): `TAPMRU` (habitualmente 1518 bytes, tamaño de trama Ethernet)

**Requisitos de alineación**: Los dispositivos de capa 2 establecen `align = ETHER_ALIGN` (habitualmente 2 bytes). Esto garantiza que la cabecera IP que sigue a la cabecera Ethernet de 14 bytes quede alineada en un límite de 4 bytes, lo que mejora el rendimiento en arquitecturas con restricciones de alineación o preocupaciones de eficiencia de línea de caché.

**Ajustes de cabecera**: La MRU aumenta para dar cabida a:

- Cabeceras virtio-net para dispositivos tap (`vhdrlen` bytes)
- Indicador de familia de direcciones para dispositivos tun en modo IFHEAD (4 bytes)

Estas cabeceras preceden a los datos reales del paquete en el buffer del espacio de usuario, pero no forman parte del formato del paquete en el cable.

##### Validación del tamaño de escritura

```c
if (uio->uio_resid < 0 || uio->uio_resid > mru) {
    TUNDEBUG(ifp, "len=%zd!\n", uio->uio_resid);
    return (EIO);
}
```

El tamaño de escritura (`uio->uio_resid`) debe estar dentro de límites válidos. Los tamaños negativos son imposibles en un funcionamiento correcto, pero se comprueban por seguridad. Las escrituras de tamaño excesivo indican alguna de estas situaciones:

- Errores en la aplicación (intentar escribir tramas jumbo sin configuración)
- Violaciones de protocolo (encuadre incorrecto del paquete)
- Comportamiento malicioso

El retorno `EIO` señala un error de I/O genérico, apropiado para datos que no se pueden procesar.

##### Procesamiento de cabeceras virtio-net

```c
if (vhdrlen > 0) {
    error = uiomove(&vhdr, vhdrlen, uio);
    if (error != 0)
        return (error);
    TUNDEBUG(ifp, "txvhdr: f %u, gt %u, hl %u, "
        "gs %u, cs %u, co %u\n", vhdr.hdr.flags,
        vhdr.hdr.gso_type, vhdr.hdr.hdr_len,
        vhdr.hdr.gso_size, vhdr.hdr.csum_start,
        vhdr.hdr.csum_offset);
}
```

Cuando el modo de cabecera virtio-net está habilitado (habitual en redes de VM), el espacio de usuario antepone a cada paquete una pequeña cabecera que describe las operaciones de offload:

- **Checksum offload**: indica al kernel dónde calcular e insertar los checksums
- **Segmentation offload**: para paquetes grandes (TSO/GSO), describe cómo segmentarlos en fragmentos del tamaño del MTU
- **Receive offload hints**: indica los checksums ya validados por el guest de la VM

La llamada `uiomove` copia la cabecera desde el espacio de usuario, consumiendo `vhdrlen` bytes del buffer de usuario y avanzando `uio`. Si la copia falla (puntero inválido), el error se propaga inmediatamente; una cabecera corrupta no puede procesarse de forma segura.

La salida de depuración registra los campos de la cabecera para resolver problemas de offload. En builds de producción con `tundebug = 0`, estas sentencias se eliminan durante la compilación.

##### Construcción del mbuf

```c
if ((m = m_uiotombuf(uio, M_NOWAIT, 0, align, M_PKTHDR)) == NULL) {
    if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
    return (ENOBUFS);
}
```

La función `m_uiotombuf` es la utilidad del kernel para convertir datos del espacio de usuario al formato nativo de paquetes de la pila de red (cadenas de mbuf). Sus parámetros especifican:

- `uio`: datos de origen del espacio de usuario
- `M_NOWAIT`: no bloquear para esperar memoria (devuelve NULL inmediatamente si la asignación falla)
- `0`: sin longitud máxima (usa todos los bytes restantes de `uio_resid`)
- `align`: inicia los datos del paquete este número de bytes dentro del primer mbuf
- `M_PKTHDR`: asigna un mbuf con cabecera de paquete (obligatorio para paquetes de red)

**Fallo en la asignación de memoria**: Si `m_uiotombuf` devuelve NULL, el sistema se ha quedado sin memoria de mbuf. El contador `IFCOUNTER_IERRORS` se incrementa (visible en `netstat -i`), y `ENOBUFS` informa al espacio de usuario del agotamiento temporal de recursos. Las aplicaciones normalmente deberían reintentarlo tras una breve pausa.

**La política M_NOWAIT**: Usar `M_NOWAIT` en lugar de `M_WAITOK` evita que las escrituras del espacio de usuario se bloqueen indefinidamente cuando la memoria es escasa. Esto es apropiado para el camino de escritura; si la memoria no está disponible en ese momento, fallar rápidamente permite a la aplicación gestionar la contrapresión.

##### Establecimiento de los metadatos del paquete

```c
m->m_pkthdr.rcvif = ifp;
#ifdef MAC
mac_ifnet_create_mbuf(ifp, m);
#endif
```

Se adjuntan dos fragmentos de metadatos al paquete:

**Interfaz de recepción**: `m_pkthdr.rcvif` registra qué interfaz recibió el paquete. Esto puede parecer contradictorio; estamos inyectando un paquete, no recibiéndolo, pero desde la perspectiva del kernel, los paquetes escritos en `/dev/tun0` son "recibidos" por la interfaz `tun0`. Este campo se usa para:

- Reglas de firewall (ipfw, pf) que filtran en función de la interfaz de entrada
- Decisiones de enrutamiento que tienen en cuenta el origen del paquete
- Contabilidad que atribuye el tráfico a interfaces específicas

**Etiquetado del marco MAC**: Si el marco de Control de Acceso Obligatorio (Mandatory Access Control) está habilitado, `mac_ifnet_create_mbuf` aplica etiquetas de seguridad al paquete basándose en la política de la interfaz. Esto es útil en sistemas que utilizan TrustedBSD MAC para seguridad de red de grano fino.

##### Despacho por capa

```c
if (l2tun)
    return (tunwrite_l2(tp, m, vhdrlen > 0 ? &vhdr : NULL));

return (tunwrite_l3(tp, m));
```

El último paso delega en funciones de procesamiento específicas de cada capa:

**Camino de capa 2** (`tunwrite_l2`): Para dispositivos tap/vmnet, el mbuf contiene una trama Ethernet completa. La función:
- Valida la cabecera Ethernet
- Aplica las indicaciones de offload de virtio-net si están presentes
- Inyecta la trama en el camino de procesamiento Ethernet
- Procesa potencialmente a través de LRO (Large Receive Offload)

**Camino de capa 3** (`tunwrite_l3`): Para dispositivos tun, el mbuf contiene un paquete IP sin procesar (posiblemente precedido por un indicador de familia de direcciones en modo IFHEAD). La función:
- Extrae la familia de protocolo (IPv4 frente a IPv6)
- Despacha al manejador de protocolo de capa de red apropiado
- Omite completamente el procesamiento de la capa de enlace

Ambas funciones asumen la propiedad del mbuf; lo inyectarán con éxito en la pila de red o lo liberarán en caso de error. El llamador no debe acceder al mbuf después de que estas llamadas retornen.

##### Resumen del flujo de datos

El camino completo desde la escritura en espacio de usuario hasta el procesamiento de red en el kernel:

```html
Application calls write(fd, packet, len)
     -> 
tunwrite() validates interface state and size
     -> 
Extract virtio-net header (if enabled)
     -> 
Copy packet data from userspace to mbuf
     -> 
Set mbuf metadata (rcvif, MAC labels)
     -> 
Layer 2: tunwrite_l2()           Layer 3: tunwrite_l3()
     ->                                   -> 
Validate Ethernet header          Extract address family
     ->                                   -> 
Apply offload hints               Dispatch to IP/IPv6
     ->                                   -> 
ether_input() / LRO               netisr_dispatch()
     ->                                   -> 
Network stack processes packet
     -> 
Routing, firewall, socket delivery
```

##### Semántica de escritura atómica

El comentario inicial, "an atomic write is a packet - or else!", destaca un contrato fundamental: el espacio de usuario debe escribir paquetes completos en una sola llamada a `write(2)`. El driver no proporciona buffering ni ensamblado de paquetes:

- Escribir 1000 bytes y luego 500 bytes crea **dos** paquetes (de 1000 bytes y de 500 bytes)
- No "un paquete de 1500 bytes ensamblado a partir de dos escrituras"

Este diseño simplifica el driver y encaja con la semántica de las interfaces de red reales, que reciben tramas completas. Las aplicaciones que necesiten construir paquetes trozo a trozo deben hacer el buffering en espacio de usuario antes de escribir.

##### Gestión de errores y de recursos

La gestión de errores de la función demuestra patrones de programación defensiva:

- **Validación temprana**: impide la asignación de recursos para peticiones inválidas
- **Limpieza inmediata** ante un fallo de `m_uiotombuf` (incrementar el contador de errores, devolver ENOBUFS)
- **Transferencia de propiedad** a las funciones específicas de capa, que elimina los riesgos de doble liberación

El único recurso asignado (el mbuf) tiene una semántica clara de transferencia de propiedad. Después de llamar a `tunwrite_l2` o `tunwrite_l3`, la función de escritura no lo vuelve a tocar.

#### 5.2 Despacho L3 (`tun`) a la pila de red (netisr)

```c
1845: static int
1846: tunwrite_l3(struct tuntap_softc *tp, struct mbuf *m)
1847: {
1848: 	struct epoch_tracker et;
1849: 	struct ifnet *ifp;
1850: 	int family, isr;
1851: 
1852: 	ifp = TUN2IFP(tp);
1853: 	/* Could be unlocked read? */
1854: 	TUN_LOCK(tp);
1855: 	if (tp->tun_flags & TUN_IFHEAD) {
1856: 		TUN_UNLOCK(tp);
1857: 		if (m->m_len < sizeof(family) &&
1858: 		(m = m_pullup(m, sizeof(family))) == NULL)
1859: 			return (ENOBUFS);
1860: 		family = ntohl(*mtod(m, u_int32_t *));
1861: 		m_adj(m, sizeof(family));
1862: 	} else {
1863: 		TUN_UNLOCK(tp);
1864: 		family = AF_INET;
1865: 	}
1866: 
1867: 	BPF_MTAP2(ifp, &family, sizeof(family), m);
1868: 
1869: 	switch (family) {
1870: #ifdef INET
1871: 	case AF_INET:
1872: 		isr = NETISR_IP;
1873: 		break;
1874: #endif
1875: #ifdef INET6
1876: 	case AF_INET6:
1877: 		isr = NETISR_IPV6;
1878: 		break;
1879: #endif
1880: 	default:
1881: 		m_freem(m);
1882: 		return (EAFNOSUPPORT);
1883: 	}
1884: 	random_harvest_queue(m, sizeof(*m), RANDOM_NET_TUN);
1885: 	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
1886: 	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
1887: 	CURVNET_SET(ifp->if_vnet);
1888: 	M_SETFIB(m, ifp->if_fib);
1889: 	NET_EPOCH_ENTER(et);
1890: 	netisr_dispatch(isr, m);
1891: 	NET_EPOCH_EXIT(et);
1892: 	CURVNET_RESTORE();
1893: 	return (0);
1894: }
```

La función `tunwrite_l3` gestiona los paquetes escritos en dispositivos de capa 3 (tun), inyectando paquetes IP sin procesar directamente en los manejadores de protocolo de red del kernel. A diferencia de los dispositivos de capa 2 (tap), que procesan tramas Ethernet completas, los dispositivos tun trabajan con paquetes IP que no tienen cabeceras de capa de enlace, lo que los hace ideales para implementaciones de VPN y protocolos de tunelización IP.

##### Contexto de la función y extracción de la familia de protocolo

```c
static int
tunwrite_l3(struct tuntap_softc *tp, struct mbuf *m)
{
    struct epoch_tracker et;
    struct ifnet *ifp;
    int family, isr;

    ifp = TUN2IFP(tp);
```

La función recibe el softc y un mbuf que contiene el paquete. El `epoch_tracker` se usará más adelante para garantizar un acceso concurrente seguro a las estructuras de enrutamiento. La variable `family` contendrá la familia de protocolo (AF_INET o AF_INET6), e `isr` identificará la rutina de servicio de interrupción de red apropiada.

##### Determinación de la familia de protocolo

```c
TUN_LOCK(tp);
if (tp->tun_flags & TUN_IFHEAD) {
    TUN_UNLOCK(tp);
    if (m->m_len < sizeof(family) &&
    (m = m_pullup(m, sizeof(family))) == NULL)
        return (ENOBUFS);
    family = ntohl(*mtod(m, u_int32_t *));
    m_adj(m, sizeof(family));
} else {
    TUN_UNLOCK(tp);
    family = AF_INET;
}
```

Los dispositivos tun admiten dos modos para indicar el protocolo del paquete:

**Modo IFHEAD** (flag `TUN_IFHEAD` establecido): Cada paquete comienza con un indicador de familia de direcciones de 4 bytes en orden de bytes de red. Este modo, habilitado mediante el ioctl `TUNSIFHEAD`, permite que un único dispositivo tun transporte tráfico IPv4 e IPv6 simultáneamente. El código:

1. Comprueba si el primer mbuf contiene al menos 4 bytes usando `m->m_len`
2. Si no, llama a `m_pullup` para consolidar la cabecera en el primer mbuf
3. Extrae la familia usando `mtod` (puntero de mbuf a datos) y convierte del orden de bytes de red al del host con `ntohl`
4. Elimina el indicador de familia con `m_adj`, que avanza el puntero de datos 4 bytes

La llamada a `m_pullup` puede fallar si la memoria se agota, devolviendo NULL. En ese caso, el mbuf original ya ha sido liberado por `m_pullup`, por lo que la función simplemente devuelve `ENOBUFS` sin llamar a `m_freem`.

**Modo no-IFHEAD** (por defecto): Se asume que todos los paquetes son IPv4. Este modo heredado simplifica las aplicaciones que solo gestionan IPv4, pero impide multiplexar protocolos sobre un mismo dispositivo.

El mutex se mantiene solo durante la lectura de `tun_flags`, minimizando la contención del lock. El comentario "Could be unlocked read?" cuestiona si el lock es siquiera necesario, ya que los flags rara vez cambian tras la inicialización y una lectura sin bloqueo probablemente sería segura. Sin embargo, el enfoque conservador evita condiciones de carrera teóricas.

##### Tap de Berkeley Packet Filter

```c
BPF_MTAP2(ifp, &family, sizeof(family), m);
```

La macro `BPF_MTAP2` pasa el paquete a cualquier oyente BPF (Berkeley Packet Filter) conectado, habitualmente herramientas de captura de paquetes como `tcpdump`. El nombre de la macro se descompone así:

- **BPF**: subsistema Berkeley Packet Filter
- **MTAP**: tap en el flujo de paquetes desde un mbuf
- **2**: variante de dos argumentos que antepone metadatos

La llamada antepone el valor `family` de 4 bytes antes de los datos del paquete, lo que permite a las herramientas de captura distinguir IPv4 de IPv6 sin inspeccionar el contenido del paquete. Esto encaja con el tipo de capa de enlace `DLT_NULL` configurado durante la creación de la interfaz; los paquetes capturados tienen una cabecera de familia de direcciones de 4 bytes aunque el formato en el cable no la tenga.

BPF funciona de manera eficiente: si no hay oyentes conectados, la macro se expande a una simple comprobación condicional que cuesta solo unas pocas instrucciones. Este diseño permite puntos de instrumentación ubicuos en toda la pila de red sin impacto en el rendimiento cuando no se está depurando activamente.

##### Validación de protocolo y configuración del despacho

```c
switch (family) {
#ifdef INET
case AF_INET:
    isr = NETISR_IP;
    break;
#endif
#ifdef INET6
case AF_INET6:
    isr = NETISR_IPV6;
    break;
#endif
default:
    m_freem(m);
    return (EAFNOSUPPORT);
}
```

La familia de protocolo determina qué rutina de servicio de interrupción (netisr) de la capa de red procesará el paquete:

- **AF_INET** -> `NETISR_IP`: procesamiento IPv4
- **AF_INET6** -> `NETISR_IPV6`: procesamiento IPv6

Los guardas `#ifdef` son necesarios: si el kernel se compiló sin soporte de IPv4 o IPv6, esos casos no existen, y el intento de inyectar tales paquetes resulta en `EAFNOSUPPORT` (familia de direcciones no soportada).

Las familias de protocolo no soportadas desencadenan la liberación inmediata del mbuf mediante `m_freem` y devuelven un error. Esto evita que los paquetes se filtren a la pila de red con metadatos incorrectos que podrían provocar fallos del sistema o problemas de seguridad.

##### Recolección de entropía

```c
random_harvest_queue(m, sizeof(*m), RANDOM_NET_TUN);
```

Esta llamada contribuye entropía al generador de números aleatorios del kernel. Los tiempos de llegada de los paquetes de red son impredecibles y difíciles de manipular por parte de los atacantes, lo que los convierte en una valiosa fuente de entropía. La función muestrea metadatos sobre la estructura del mbuf (no el contenido del paquete) para inicializar el pool de aleatoriedad.

El flag `RANDOM_NET_TUN` etiqueta la fuente de entropía, lo que permite al subsistema de aleatoriedad hacer un seguimiento de la diversidad de entropía. Los sistemas que dependen de `/dev/random` para operaciones criptográficas se benefician de acumular entropía procedente de múltiples fuentes independientes.

##### Estadísticas de la interfaz

```c
if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
```

Estas llamadas actualizan las estadísticas de la interfaz visibles mediante `netstat -i` o `ifconfig`:

- `IFCOUNTER_IBYTES`: total de bytes recibidos
- `IFCOUNTER_IPACKETS`: total de paquetes recibidos

Desde la perspectiva del kernel, los paquetes escritos por el espacio de usuario son "entradas" de la interfaz, de ahí el uso de contadores de entrada en lugar de contadores de salida. Esto encaja con la semántica establecida al asignar `m_pkthdr.rcvif` anteriormente; el paquete está siendo recibido desde el espacio de usuario.

La función `if_inc_counter` gestiona las actualizaciones atómicas, garantizando recuentos precisos incluso con procesamiento de paquetes concurrente en sistemas multiprocesador.

##### Configuración del contexto del stack de red

```c
CURVNET_SET(ifp->if_vnet);
M_SETFIB(m, ifp->if_fib);
```

Se establecen dos elementos de contexto antes de inyectar el paquete:

**Stack de red virtual**: `CURVNET_SET` cambia al contexto de red (vnet) asociado con la interfaz. En sistemas que usan jails o virtualización del stack de red, coexisten múltiples stacks de red independientes. Esta macro garantiza que las tablas de enrutamiento, las reglas de firewall y las búsquedas de sockets operen en el espacio de nombres correcto.

**Forwarding Information Base (FIB)**: `M_SETFIB` etiqueta el paquete con el número de FIB de la interfaz. FreeBSD admite múltiples tablas de enrutamiento (FIBs), lo que permite el enrutamiento basado en políticas, donde distintas aplicaciones o interfaces utilizan políticas de enrutamiento diferenciadas. El paquete hereda el FIB de la interfaz, garantizando que las rutas se busquen en la tabla adecuada.

Estos ajustes afectan a todo el procesamiento posterior del paquete: reglas de firewall, decisiones de enrutamiento y entrega a sockets.

##### Despacho protegido por epoch

```c
NET_EPOCH_ENTER(et);
netisr_dispatch(isr, m);
NET_EPOCH_EXIT(et);
CURVNET_RESTORE();
return (0);
```

La inyección crítica del paquete ocurre dentro de una sección de epoch:

**Epoch de red**: El stack de red de FreeBSD utiliza recuperación basada en epoch (una forma de read-copy-update) para proteger las estructuras de datos del acceso concurrente sin necesidad de locks pesados. `NET_EPOCH_ENTER` registra este thread como activo en el epoch de red, impidiendo que las entradas de enrutamiento, las estructuras de interfaz y otros objetos de red sean liberados hasta que se ejecute `NET_EPOCH_EXIT`.

Este mecanismo permite lecturas sin locks de las tablas de enrutamiento y las listas de interfaces, mejorando drásticamente la escalabilidad en sistemas multinúcleo. El rastreador de epoch `et` mantiene el contexto necesario para salir de forma limpia.

**Despacho netisr**: `netisr_dispatch(isr, m)` entrega el paquete al subsistema de rutinas de servicio de interrupciones de red. Este modelo de despacho asíncrono desacopla la inyección de paquetes del procesamiento de protocolos:

1. El paquete se encola en el thread netisr correspondiente (normalmente uno por núcleo de CPU)
2. El thread que realiza la llamada (gestionando el `write(2)`) regresa de inmediato
3. El thread netisr desencola y procesa el paquete de forma asíncrona

Este diseño evita que las escrituras del espacio de usuario bloqueen el procesamiento complejo de protocolos (reenvío IP, evaluación del firewall, reensamblaje TCP). El thread netisr:
- Validará las cabeceras IP (checksum, longitud, versión)
- Procesará las opciones IP
- Consultará las tablas de enrutamiento
- Aplicará las reglas de firewall
- Entregará los datos a sockets locales o los reenviará a otras interfaces

**Restauración del contexto**: `CURVNET_RESTORE` vuelve al contexto de red original del thread que realiza la llamada. Esto es esencial para la corrección del programa: sin la restauración, las operaciones posteriores en el thread se ejecutarían en el espacio de nombres de red equivocado.

##### Propiedad y ciclo de vida

Tras la llamada a `netisr_dispatch`, la función retorna con éxito pero ya no es propietaria del mbuf. El subsistema netisr asume la responsabilidad de:
- Entregar el paquete a su destino y liberar el mbuf
- Descartar el paquete (por razones de política, enrutamiento o validación) y liberar el mbuf

La función nunca necesita llamar a `m_freem` en el camino de éxito, ya que la propiedad se ha transferido al stack de red.

##### Flujo de datos a través del stack de red

El camino completo tras el despacho:
```html
tunwrite_l3() injects packet
     -> 
netisr_dispatch() queues to NETISR_IP/NETISR_IPV6
     -> 
Netisr thread dequeues packet
     -> 
ip_input() / ip6_input() processes
     -> 
Routing table lookup
     -> 
Firewall evaluation (ipfw, pf)
     -> 
    | ->  Local delivery: socket input queue
    | ->  Forward: ip_forward()  ->  output interface
    | ->  Drop: m_freem()
```

##### Caminos de error y gestión de recursos

La función tiene tres posibles resultados:

1. **Éxito** (retorna 0): El paquete se ha despachado al stack de red y la propiedad del mbuf se ha transferido
2. **Fallo de pullup** (retorna ENOBUFS): `m_pullup` liberó el mbuf, no se requiere limpieza adicional
3. **Protocolo no soportado** (retorna EAFNOSUPPORT): El mbuf se libera explícitamente con `m_freem`

Todos los caminos gestionan correctamente la propiedad del mbuf, evitando tanto las fugas de memoria como las dobles liberaciones. Esta cuidadosa gestión de recursos es característica del código del kernel bien diseñado.

#### 6) Disponibilidad: `poll(2)` y kqueue

```c
1965:  */
1966: static	int
1967: tunpoll(struct cdev *dev, int events, struct thread *td)
1968: {
1969: 	struct tuntap_softc *tp = dev->si_drv1;
1970: 	struct ifnet	*ifp = TUN2IFP(tp);
1971: 	int		revents = 0;
1972: 
1973: 	TUNDEBUG(ifp, "tunpoll\n");
1974: 
1975: 	if (events & (POLLIN | POLLRDNORM)) {
1976: 		IFQ_LOCK(&ifp->if_snd);
1977: 		if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
1978: 			TUNDEBUG(ifp, "tunpoll q=%d\n", ifp->if_snd.ifq_len);
1979: 			revents |= events & (POLLIN | POLLRDNORM);
1980: 		} else {
1981: 			TUNDEBUG(ifp, "tunpoll waiting\n");
1982: 			selrecord(td, &tp->tun_rsel);
1983: 		}
1984: 		IFQ_UNLOCK(&ifp->if_snd);
1985: 	}
1986: 	revents |= events & (POLLOUT | POLLWRNORM);
1987: 
1988: 	return (revents);
1989: }
1990: 
1991: /*
1992:  * tunkqfilter - support for the kevent() system call.
1993:  */
1994: static int
1995: tunkqfilter(struct cdev *dev, struct knote *kn)
1996: {
1997: 	struct tuntap_softc	*tp = dev->si_drv1;
1998: 	struct ifnet	*ifp = TUN2IFP(tp);
1999: 
2000: 	switch(kn->kn_filter) {
2001: 	case EVFILT_READ:
2002: 		TUNDEBUG(ifp, "%s kqfilter: EVFILT_READ, minor = %#x\n",
2003: 		    ifp->if_xname, dev2unit(dev));
2004: 		kn->kn_fop = &tun_read_filterops;
2005: 		break;
2006: 
2007: 	case EVFILT_WRITE:
2008: 		TUNDEBUG(ifp, "%s kqfilter: EVFILT_WRITE, minor = %#x\n",
2009: 		    ifp->if_xname, dev2unit(dev));
2010: 		kn->kn_fop = &tun_write_filterops;
2011: 		break;
2012: 
2013: 	default:
2014: 		return (EINVAL);
2015: 	}
2016: 
2017: 	kn->kn_hook = tp;
2018: 	knlist_add(&tp->tun_rsel.si_note, kn, 0);
2019: 
2020: 	return (0);
2021: }
```

La función `tunpoll` implementa el soporte para `poll(2)` y `select(2)`, que permiten a las aplicaciones monitorizar múltiples descriptores de archivo para detectar disponibilidad de E/S:

```c
static int
tunpoll(struct cdev *dev, int events, struct thread *td)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet *ifp = TUN2IFP(tp);
    int revents = 0;
```

La función recibe:

- `dev` - el dispositivo de caracteres que se está sondeando
- `events` - máscara de bits de los eventos que la aplicación quiere monitorizar
- `td` - el contexto del thread que realiza la llamada

El valor de retorno `revents` indica qué eventos solicitados están actualmente disponibles. La función construye esta máscara de bits comprobando las condiciones reales del dispositivo.

##### Mecanismos de notificación de eventos: `tunpoll` y `tunkqfilter`

La multiplexación eficiente de E/S es esencial para las aplicaciones que gestionan múltiples dispositivos tun/tap o que integran la E/S de túneles con otras fuentes de eventos. FreeBSD ofrece dos interfaces para esto: las llamadas al sistema tradicionales `poll(2)`/`select(2)` y el mecanismo más escalable de `kqueue(2)`. Las funciones `tunpoll` y `tunkqfilter` implementan estas interfaces, permitiendo a las aplicaciones esperar de forma eficiente condiciones de lectura o escritura sin recurrir al sondeo activo.

##### Disponibilidad de lectura

```c
if (events & (POLLIN | POLLRDNORM)) {
    IFQ_LOCK(&ifp->if_snd);
    if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
        TUNDEBUG(ifp, "tunpoll q=%d\n", ifp->if_snd.ifq_len);
        revents |= events & (POLLIN | POLLRDNORM);
    } else {
        TUNDEBUG(ifp, "tunpoll waiting\n");
        selrecord(td, &tp->tun_rsel);
    }
    IFQ_UNLOCK(&ifp->if_snd);
}
```

Cuando la aplicación solicita eventos de lectura (`POLLIN` o `POLLRDNORM`, que son sinónimos para dispositivos):

**Comprobación de la cola**: Se adquiere el lock de la cola de envío y `IFQ_IS_EMPTY` comprueba si hay paquetes pendientes de lectura. Si hay paquetes presentes:

- Los eventos de lectura solicitados se añaden a `revents`
- Se notifica a la aplicación de que `read(2)` puede proceder sin bloquearse

**Registro para notificación**: Si la cola está vacía:

- `selrecord` registra el interés de este thread en que el dispositivo se vuelva legible
- El contexto del thread se añade a `tp->tun_rsel`, una lista de selección por dispositivo
- Cuando lleguen paquetes más adelante (en `tunstart` o `tunstart_l2`), el código llama a `selwakeup(&tp->tun_rsel)` para notificar a todos los threads registrados

El mecanismo `selrecord` es la clave para la espera eficiente. En lugar de que la aplicación sondee repetidamente, el kernel mantiene una lista de threads interesados y los despierta cuando las condiciones cambian. Este patrón aparece en todo el kernel de FreeBSD para cualquier dispositivo que soporte `poll(2)`.

El lock de la cola de envío protege contra condiciones de carrera en las que paquetes llegan entre la comprobación de la cola y el registro del interés. El lock garantiza la atomicidad: si la cola está vacía durante la comprobación, el registro se completa antes de que cualquier llegada de paquete pueda llamar a `selwakeup`.

##### Disponibilidad de escritura

```c
revents |= events & (POLLOUT | POLLWRNORM);
```

Las escrituras están siempre disponibles para los dispositivos tun/tap. El dispositivo no tiene buffers internos que puedan llenarse: `write(2)` tiene éxito de inmediato (asignando un mbuf y despachando al stack de red) o falla de inmediato (si la asignación del mbuf falla). No existe ninguna condición en la que la escritura se bloquee esperando que haya espacio en el buffer.

Esta disponibilidad de escritura incondicional es habitual en los dispositivos de red. A diferencia de las tuberías o los sockets con espacio de buffer limitado, los dispositivos tun/tap aceptan escrituras tan rápido como la aplicación pueda generarlas, apoyándose en la gestión de memoria dinámica del asignador de mbufs.

##### Interfaz kqueue: `tunkqfilter`

La función `tunkqfilter` implementa el soporte para `kqueue(2)`, el mecanismo de notificación de eventos escalable de FreeBSD. Kqueue ofrece varias ventajas sobre `poll(2)`:

- Semántica de activación por flanco (notificaciones solo ante cambios de estado)
- Mejor rendimiento con miles de descriptores de archivo
- Se pueden adjuntar datos de usuario a los eventos
- Tipos de eventos más flexibles (no solo lectura/escritura)

```c
static int
tunkqfilter(struct cdev *dev, struct knote *kn)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet *ifp = TUN2IFP(tp);
```

La función recibe una estructura `knote` (nota del kernel) que representa el registro del evento. El `knote` persiste entre múltiples entregas de eventos, a diferencia de `poll(2)`, que requiere un nuevo registro en cada llamada.

##### Validación del tipo de filtro

```c
switch(kn->kn_filter) {
case EVFILT_READ:
    TUNDEBUG(ifp, "%s kqfilter: EVFILT_READ, minor = %#x\n",
        ifp->if_xname, dev2unit(dev));
    kn->kn_fop = &tun_read_filterops;
    break;

case EVFILT_WRITE:
    TUNDEBUG(ifp, "%s kqfilter: EVFILT_WRITE, minor = %#x\n",
        ifp->if_xname, dev2unit(dev));
    kn->kn_fop = &tun_write_filterops;
    break;

default:
    return (EINVAL);
}
```

La aplicación especifica qué tipo de evento monitorizar mediante `kn->kn_filter`:

- `EVFILT_READ` - monitoriza la condición de lectura disponible
- `EVFILT_WRITE` - monitoriza la condición de escritura disponible

Para cada tipo de filtro, el código asigna una tabla de funciones (`kn_fop`) que implementa la semántica del filtro. Estas tablas se definieron anteriormente en el código fuente:

```c
static const struct filterops tun_read_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = tunkqdetach,
    .f_event = tunkqread,
};

static const struct filterops tun_write_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = tunkqdetach,
    .f_event = tunkqwrite,
};
```

La estructura `filterops` define los callbacks:

- `f_isfd` - indicador de que este filtro opera sobre descriptores de archivo
- `f_attach` - se llama cuando se registra el filtro (NULL aquí, no se necesita configuración especial)
- `f_detach` - se llama cuando se elimina el filtro (limpieza mediante `tunkqdetach`)
- `f_event` - se llama para comprobar la condición del evento (`tunkqread` o `tunkqwrite`)

Los tipos de filtro no soportados (como `EVFILT_SIGNAL` o `EVFILT_TIMER`) retornan `EINVAL`, ya que no tienen sentido para dispositivos tun/tap.

##### Registro del evento

```c
kn->kn_hook = tp;
knlist_add(&tp->tun_rsel.si_note, kn, 0);

return (0);
}
```

Dos pasos completan el registro:

**Vinculación del contexto**: `kn->kn_hook` almacena el puntero al softc. Esto permite a las funciones de operación del filtro (`tunkqread`, `tunkqwrite`) acceder al estado del dispositivo sin búsquedas globales. Cuando el evento se dispara, el callback recibe el `knote`, extrae `kn_hook` y lo reinterpreta como `tuntap_softc *`.

**Añadir a la lista de notificación**: `knlist_add` inserta el `knote` en la lista de notas del kernel del dispositivo (`tp->tun_rsel.si_note`). Esta lista es compartida por la infraestructura de `poll(2)` y `kqueue(2)`: el campo `si_note` dentro de `tun_rsel` gestiona los eventos de kqueue, mientras que los demás campos de `tun_rsel` gestionan los eventos de poll/select.

Cuando llegan paquetes (en `tunstart` o `tunstart_l2`), el código llama a `KNOTE_LOCKED(&tp->tun_rsel.si_note, 0)`, que itera la lista de knotes e invoca el callback `f_event` de cada filtro. Si el callback retorna verdadero (condición de lectura/escritura cumplida), el subsistema kqueue entrega el evento al espacio de usuario.

El tercer argumento de `knlist_add` (0) indica que no hay indicadores especiales: el knote se añade incondicionalmente sin requerir un estado de lock específico.

##### Callbacks de las operaciones del filtro

Aunque no se muestran en este fragmento, merece la pena comprender las operaciones del filtro:

**`tunkqread`**: Se llama para comprobar la disponibilidad de lectura

```c
static int
tunkqread(struct knote *kn, long hint)
{
    struct tuntap_softc *tp = kn->kn_hook;
    struct ifnet *ifp = TUN2IFP(tp);

    if ((kn->kn_data = ifp->if_snd.ifq_len) > 0) {
        return (1);  // Readable
    }
    return (0);  // Not readable
}
```

El callback comprueba la longitud de la cola de envío y la almacena en `kn->kn_data`, poniendo el recuento a disposición del espacio de usuario a través de la estructura `kevent`. Retornar 1 indica que el evento debe dispararse; retornar 0 significa que la condición aún no se ha cumplido.

**`tunkqwrite`**: Se llama para comprobar la disponibilidad de escritura

```c
static int
tunkqwrite(struct knote *kn, long hint)
{
    struct tuntap_softc *tp = kn->kn_hook;
    struct ifnet *ifp = TUN2IFP(tp);

    kn->kn_data = ifp->if_mtu;
    return (1);  // Always writable
}
```

Como las escrituras siempre son posibles, esta función siempre retorna 1. El campo `kn_data` se establece con el MTU de la interfaz, proporcionando al espacio de usuario información sobre el tamaño máximo de escritura.

**`tunkqdetach`**: Se llama al eliminar el evento

```c
static void
tunkqdetach(struct knote *kn)
{
    struct tuntap_softc *tp = kn->kn_hook;

    knlist_remove(&tp->tun_rsel.si_note, kn, 0);
}
```

Esto elimina el knote de la lista de notificación del dispositivo, garantizando que no se entreguen más eventos para este registro.

##### Comparación: Poll frente a Kqueue

Los dos mecanismos cumplen propósitos similares, pero con características distintas:

**Poll/Select**:
- Activación por nivel: informa del estado de disponibilidad en cada llamada
- Requiere que el kernel explore todos los descriptores de archivo en cada llamada
- API sencillo, ampliamente portable
- Complejidad O(n) en función del número de descriptores de archivo

**Kqueue**:
- Activación por flanco: informa de los cambios en el estado de disponibilidad
- El kernel mantiene una lista activa de eventos y solo notifica los cambios
- API más complejo, específico de FreeBSD/macOS
- Complejidad O(1) para la entrega de eventos

Para las aplicaciones que monitorizan un único dispositivo tun/tap, la diferencia es despreciable. Para concentradores VPN o simuladores de red que gestionan cientos de interfaces virtuales, las ventajas de escalabilidad de kqueue se vuelven significativas.

##### Flujo de notificación

Cuando llega un paquete para su transmisión, la secuencia de notificación completa es:
```html
Network stack routes packet to tun0
     -> 
tunoutput() / tap_transmit() enqueues mbuf
     -> 
tunstart() / tunstart_l2() wakes waiters:
    | ->  wakeup(tp) - wakes blocked read()
    | ->  selwakeup(&tp->tun_rsel) - wakes poll()/select()
    | ->  KNOTE_LOCKED(&tp->tun_rsel.si_note, 0) - delivers kqueue events
     -> 
Application receives notification
     -> 
Application calls read() to retrieve packet
```

Esta notificación multi-mecanismo garantiza que las aplicaciones que emplean cualquier estrategia de espera (lecturas bloqueantes, bucles de poll/select o bucles de eventos kqueue) reciban una notificación puntual de la llegada del paquete.

#### Ejercicios interactivos para `tun(4)/tap(4)`

**Objetivo:** Rastrear ambas direcciones del flujo de datos y relacionar las operaciones del espacio de usuario con las líneas exactas del kernel.

##### A) Personalidades del dispositivo y clonación (calentamiento)

1. En el array `tuntap_drivers[]`, lista los tres valores `.d_name` e identifica qué punteros a función (`.d_open`, `.d_read`, `.d_write`, etc.) se asignan a cada uno. Nota: ¿son las mismas funciones o distintas? Cita las líneas del inicializador que hayas utilizado. (Pista: examina las líneas en torno a 280-291 y las entradas siguientes para tap/vmnet.)

2. En `tun_clone_create()`, localiza dónde el driver:

	- calcula el nombre final con la unidad,
	- llama a `clone_create()`,
	- recurre a `tun_create_device()`, y
	- llama a `tuncreate()` para conectar el ifnet.

	Cita esas líneas y explica la secuencia.

3. En `tun_create_device()`, anota el modo utilizado para el `cdev` y qué campo apunta `si_drv1` al softc. Cita las líneas. (Pista: busca `mda_mode` y `mda_si_drv1`.)

##### B) Ruta de activación de la interfaz

1. En `tuncreate()`, señala las llamadas a `if_alloc()`, `if_initname()` y `if_attach()`. ¿Por qué se llama a `bpfattach()` en modo L3 **con `DLT_NULL`** en lugar de `DLT_EN10MB`? Cita las líneas que uses.

2. En `tunopen()`, identifica dónde se marca el estado del enlace como UP al abrir. Cita la(s) línea(s).

3. En `tunopen()`, ¿qué impide que dos procesos abran el mismo dispositivo simultáneamente? Cita la comprobación y explica los flags implicados. (Pista: busca `TUN_OPEN` y `EBUSY`.)

##### C) Leer un paquete desde el espacio de usuario (kernel -> usuario)

1. En `tunread()`, explica los comportamientos bloqueante y no bloqueante. ¿Qué flag fuerza `EWOULDBLOCK`? ¿Dónde se realiza el sleep? Cita las líneas.

2. ¿Dónde se copia la cabecera virtio opcional al espacio de usuario, y cómo se entrega después el payload? Cita esas líneas.

3. ¿Dónde se despiertan los lectores cuando llegan paquetes de salida desde la pila? Traza los wakeups en `tunstart_l2()` (o la ruta de inicio L3): `wakeup`, `selwakeuppri` y `KNOTE`. Cita las líneas.

##### D) Escribir un paquete desde el espacio de usuario (usuario -> kernel)

1. En `tunwrite()`, localiza la guarda que ignora silenciosamente las escrituras si la interfaz está caída, y la comprobación que limita el tamaño máximo de escritura (MRU + cabeceras). Cita las líneas.

2. Todavía en `tunwrite()`, ¿dónde se convierte el buffer del usuario en un mbuf? Cita la llamada y explica el parámetro `align` para L2.

3. Sigue la ruta L3 hacia `tunwrite_l3()`: ¿dónde se lee la familia de direcciones (cuando `TUN_IFHEAD` está activo), dónde se engancha BPF, y dónde se llama al despacho de netisr? Cita esas líneas.

4. Sigue la ruta L2 hacia `tunwrite_l2()`: ¿dónde se descartan las tramas cuya dirección MAC de destino no coincide con la MAC de la interfaz (salvo que esté activo el modo promiscuo)? Esto simula lo que el hardware Ethernet real no entregaría. Cita esas líneas.

##### E) Validaciones rápidas en espacio de usuario (experimentos seguros)

Estas comprobaciones asumen que has creado un `tun0` (L3) o `tap0` (L2) y lo has activado en una VM privada.

```bash
# L3: read a packet the kernel queued for us
% ifconfig tun0 10.0.0.1/24 up
% ( ping -c1 10.0.0.2 >/dev/null & ) &
% dd if=/dev/tun0 bs=4096 count=1 2>/dev/null | hexdump -C | head -n2
# Expected: You should see an ICMP echo request (type 8)
# with destination IP 10.0.0.2 starting around offset 0x14

# L3: inject an IPv4 echo request (requires crafting a full frame)
# (later in the book we'll show a tiny C sender using write())
```

Por cada comando que ejecutes, señala las líneas exactas en `tunread()` o `tunwrite_l3()` que explican el comportamiento que observas.

#### Ampliación (experimentos mentales)

1. Si `tunwrite()` devolviera `EIO` cuando la interfaz está caída, en lugar de ignorar las escrituras, ¿cómo se comportarían las herramientas que dependen de escrituras a ciegas? Señala la línea actual de «ignorar si está caída» y explica la decisión de diseño.

2. Supón que `tunstart_l2()` llamara a `wakeup(tp)` pero **no** a `selwakeuppri(&tp->tun_rsel, ...)`. ¿Qué le ocurriría a una aplicación que usa `poll(2)` para esperar paquetes? ¿Seguiría funcionando el `read(2)` bloqueante? Señala ambos mecanismos de notificación y explica por qué cada uno es necesario.

#### Puente hacia el siguiente recorrido

El driver `if_tuntap` demuestra cómo se integran los dispositivos de caracteres y las interfaces de red, con el userland actuando como el punto final «hardware». Nuestro próximo driver explora un territorio fundamentalmente diferente: **uart_bus_pci** muestra cómo se descubren los dispositivos de hardware real y se vinculan a los drivers del kernel a través de la arquitectura de bus por capas de FreeBSD.

Este salto de las operaciones de dispositivos de caracteres a la vinculación al bus representa un patrón arquitectónico fundamental: la separación entre el **código de pegamento específico del bus** y la **funcionalidad central independiente del dispositivo**. El driver uart_bus_pci es deliberadamente minimalista, con menos de 300 líneas de código, y se centra exclusivamente en la identificación del dispositivo (correspondencia de IDs de fabricante/dispositivo PCI), la negociación de recursos (reclamando puertos I/O e interrupciones) y la entrega al subsistema UART genérico a través de `uart_bus_probe()` y `uart_bus_attach()`.

### Tour 4 - El pegamento de PCI: `uart(4)`

Abre el archivo:

```console
% cd /usr/src/sys/dev/uart
% less uart_bus_pci.c
```

Este archivo es el **«bus glue» de PCI** (el pegamento de bus) para el núcleo genérico de UART. Relaciona hardware mediante una tabla de IDs de PCI, elige una **clase** de UART, llama al **probe/attach compartido del bus uart** y añade un poco de lógica específica del bus (preferencia de MSI, coincidencia de consola única). La manipulación real de los registros UART vive en el código UART común; este archivo trata del **emparejamiento y la conexión**.

#### 1) Tabla de métodos + objeto driver (lo que llama Newbus)

```c
 52: static device_method_t uart_pci_methods[] = {
 53: 	/* Device interface */
 54: 	DEVMETHOD(device_probe,		uart_pci_probe),
 55: 	DEVMETHOD(device_attach,	uart_pci_attach),
 56: 	DEVMETHOD(device_detach,	uart_pci_detach),
 57: 	DEVMETHOD(device_resume,	uart_bus_resume),
 58: 	DEVMETHOD_END
 59: };
 61: static driver_t uart_pci_driver = {
 62: 	uart_driver_name,
 63: 	uart_pci_methods,
 64: 	sizeof(struct uart_softc),
 65: };
```

*Relaciona esto mentalmente con el ciclo de vida de Newbus: `probe` -> `attach` -> `detach` (+ `resume`).*

##### Métodos de dispositivo y estructura del driver

El framework de drivers de dispositivo de FreeBSD utiliza un enfoque orientado a objetos en el que los drivers declaran qué operaciones soportan mediante tablas de métodos. El array `uart_pci_methods` y la estructura `uart_pci_driver` establecen la interfaz de este driver con el subsistema de gestión de dispositivos del kernel.

##### La tabla de métodos de dispositivo

```c
static device_method_t uart_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,     uart_pci_probe),
    DEVMETHOD(device_attach,    uart_pci_attach),
    DEVMETHOD(device_detach,    uart_pci_detach),
    DEVMETHOD(device_resume,    uart_bus_resume),
    DEVMETHOD_END
};
```

El array `device_method_t` asocia operaciones genéricas de dispositivo con implementaciones específicas del driver. Cada entrada `DEVMETHOD` enlaza un identificador de método con un puntero a función:

**`device_probe`** -> `uart_pci_probe`: Llamado por el driver del bus PCI durante la enumeración de dispositivos para preguntar «¿puedes manejar este dispositivo?». La función examina los IDs de vendor y dispositivo PCI del dispositivo y devuelve un valor de prioridad que indica la calidad de la coincidencia. Los valores más bajos indican mejores coincidencias; devolver `ENXIO` significa «no es mi dispositivo».

**`device_attach`** -> `uart_pci_attach`: Llamado tras un probe exitoso para inicializar el dispositivo. Esta función asigna recursos (puertos de I/O, interrupciones), configura el hardware y pone el dispositivo en funcionamiento. Si el attach falla, el driver debe liberar todos los recursos asignados.

**`device_detach`** -> `uart_pci_detach`: Llamado cuando el dispositivo va a ser retirado del sistema (extracción en caliente, descarga del driver o apagado del sistema). Debe liberar todos los recursos solicitados durante el attach y asegurarse de que el hardware queda en un estado seguro.

**`device_resume`** -> `uart_bus_resume`: Llamado cuando el sistema se reanuda tras un estado de suspensión. Observa que apunta a `uart_bus_resume`, no a una función específica de PCI; la capa genérica de UART gestiona el control de energía de forma uniforme en todos los tipos de bus.

**`DEVMETHOD_END`**: Un centinela que marca el final del array. El kernel itera esta tabla hasta encontrar este terminador.

##### La declaración del driver

```c
static driver_t uart_pci_driver = {
    uart_driver_name,
    uart_pci_methods,
    sizeof(struct uart_softc),
};
```

La estructura `driver_t` empaqueta la tabla de métodos junto con metadatos:

**`uart_driver_name`**: Una cadena que identifica este driver, normalmente «uart». Este nombre aparece en los mensajes del kernel, en la salida del árbol de dispositivos y en las herramientas de administración. El nombre está definido en el código genérico de uart y se comparte entre todos los attach de bus (PCI, ISA, ACPI), lo que garantiza un nombre de dispositivo coherente independientemente de cómo se descubrió el UART.

**`uart_pci_methods`**: Puntero a la tabla de métodos definida arriba. Cuando el kernel necesita realizar una operación sobre un dispositivo uart_pci, busca el método apropiado en esta tabla y llama a la función correspondiente.

**`sizeof(struct uart_softc)`**: El tamaño de la estructura de estado por dispositivo del driver. El kernel asigna esta cantidad de memoria al crear una instancia de dispositivo, accesible mediante `device_get_softc()`. Es importante destacar que se usa `uart_softc` de la capa genérica de UART, no una estructura específica de PCI; el estado central del UART es agnóstico al bus.

##### Importancia arquitectónica

Esta sencilla estructura encarna el modelo de driver en capas de FreeBSD. La tabla de métodos contiene cuatro funciones:

- Dos son específicas de PCI (`uart_pci_probe`, `uart_pci_attach`, `uart_pci_detach`)
- Una es agnóstica al bus (`uart_bus_resume`)

Las funciones específicas de PCI se ocupan únicamente de cuestiones relacionadas con el bus: emparejar IDs de dispositivo, reclamar recursos PCI y gestionar interrupciones MSI. Toda la lógica específica de UART (configuración de la tasa de baudios, gestión de FIFO, E/S de caracteres) vive en el código genérico `uart_bus.c` al que llaman estas funciones.

Esta separación significa que la misma lógica de hardware UART funciona tanto si el dispositivo aparece en el bus PCI como en el bus ISA o como un dispositivo enumerado por ACPI. Solo cambia el pegamento de probe/attach. Este patrón (envolturas finas específicas del bus sobre núcleos genéricos sustanciales) reduce la duplicación de código y simplifica la adaptación a nuevos tipos de bus o arquitecturas.

El mecanismo de tabla de métodos también permite el polimorfismo en tiempo de ejecución. Si un UART aparece en diferentes buses (un 16550 tanto en PCI como en ISA, por ejemplo), el kernel carga módulos de driver diferentes (`uart_pci`, `uart_isa`), cada uno con su propia tabla de métodos, pero ambos comparten la estructura subyacente `uart_softc` y llaman a las mismas funciones genéricas para la operación real del dispositivo.

#### 2) Estructuras locales + flags que usaremos

```c
 67: struct pci_id {
 68: 	uint16_t	vendor;
 69: 	uint16_t	device;
 70: 	uint16_t	subven;
 71: 	uint16_t	subdev;
 72: 	const char	*desc;
 73: 	int		rid;
 74: 	int		rclk;
 75: 	int		regshft;
 76: };
 78: struct pci_unique_id {
 79: 	uint16_t	vendor;
 80: 	uint16_t	device;
 81: };
 83: #define PCI_NO_MSI	0x40000000
 84: #define PCI_RID_MASK	0x0000ffff
```

*Lo que importa más adelante:* `rid` (qué BAR/IRQ usar), `rclk` y `regshft` opcionales, y el hint `PCI_NO_MSI`.

##### Estructuras de identificación de dispositivo

Los drivers de hardware deben identificar qué dispositivos concretos pueden gestionar. Para los dispositivos PCI, esta identificación se basa en códigos de ID de vendor y dispositivo grabados en el espacio de configuración del hardware. Las estructuras `pci_id` y `pci_unique_id` codifican esta lógica de emparejamiento junto con parámetros de configuración específicos del dispositivo.

##### La estructura de identificación principal

```c
struct pci_id {
    uint16_t    vendor;
    uint16_t    device;
    uint16_t    subven;
    uint16_t    subdev;
    const char  *desc;
    int         rid;
    int         rclk;
    int         regshft;
};
```

Cada entrada `pci_id` describe una variante de UART y cómo configurarla:

**`vendor` y `device`**: El par de identificación principal. Cada dispositivo PCI tiene un ID de vendor de 16 bits (asignado por el PCI Special Interest Group) y un ID de dispositivo de 16 bits (asignado por el vendor). Por ejemplo, Intel es el vendor `0x8086` y su controlador AMT Serial-over-LAN es el dispositivo `0x108f`. Estos IDs se leen del espacio de configuración del dispositivo durante la enumeración del bus.

**`subven` y `subdev`**: Identificación secundaria para la personalización OEM. Muchos fabricantes crean tarjetas utilizando diseños de referencia de vendors de chipset y luego asignan sus propios IDs de vendor y dispositivo de subsistema. Un valor de `0xffff` en estos campos actúa como comodín, lo que significa «coincidir con cualquier ID de subsistema». Esto permite emparejar tanto variantes OEM específicas como familias completas de chipset.

La jerarquía de emparejamiento de cuatro niveles permite una identificación precisa:

1. Coincidir solo con tarjetas OEM específicas: los cuatro IDs deben coincidir exactamente
2. Coincidir con todas las tarjetas que usan un chipset: `vendor`/`device` coinciden, `subven`/`subdev` son `0xffff`
3. Coincidir con una personalización OEM específica: `vendor`/`device` más `subven`/`subdev` exactos

**`desc`**: Descripción del dispositivo legible por humanos que se muestra en los mensajes de arranque y en la salida de `dmesg`. Ejemplos: «Intel AMT - SOL» u «Oxford Semiconductor OXCB950 Cardbus 16950 UART». Esta cadena ayuda a los administradores a identificar qué dispositivo físico corresponde a qué entrada `/dev/cuaU*`.

**`rid`**: ID de recurso que especifica qué registro de dirección base (BAR) de PCI contiene los registros del UART. Los dispositivos PCI pueden tener hasta seis BARs (numerados 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24). La mayoría de los UARTs utilizan el BAR 0 (`0x10`), pero algunas tarjetas multifunción sitúan el UART en BARs alternativos. Este campo también puede codificar flags mediante los bits altos.

**`rclk`**: Frecuencia del reloj de referencia en Hz. El generador de tasa de baudios del UART divide este reloj para producir la temporización de bits en serie. Los UARTs estándar de PC usan 1843200 Hz (1,8432 MHz), pero los UARTs embebidos y las tarjetas especializadas suelen usar frecuencias diferentes. Algunos dispositivos Intel usan 24 veces el reloj estándar para operación de alta velocidad. Un `rclk` incorrecto provoca comunicación serie corrupta por desajuste en la tasa de baudios.

**`regshft`**: Valor de desplazamiento de dirección de registro. La mayoría de los UARTs colocan registros consecutivos en direcciones de byte consecutivas (desplazamiento = 0), pero algunos integran el UART en espacios de registro más grandes con registros cada 4 bytes (desplazamiento = 2) u otros intervalos. El driver desplaza los offsets de registro por esta cantidad al acceder al hardware. Esto permite acomodar diseños de SoC en los que el UART comparte el espacio de direcciones con otros periféricos.

##### La estructura de identificación simplificada

```c
struct pci_unique_id {
    uint16_t    vendor;
    uint16_t    device;
};
```

Esta estructura más pequeña identifica dispositivos que tienen garantizado existir como mucho una vez por sistema. Cierto hardware (en particular, los controladores de gestión de servidores y los UARTs embebidos en SoC) está diseñado como dispositivos singleton. Para estos, los IDs de vendor y dispositivo son suficientes para el emparejamiento contra consolas del sistema, sin necesidad de IDs de subsistema ni parámetros de configuración.

La distinción importa para el emparejamiento de consola: si un UART sirve como consola del sistema (configurado en el firmware o en el cargador de arranque), el kernel debe identificar qué dispositivo enumerado corresponde a la consola preconfigurada. Para dispositivos únicos, una simple coincidencia de vendor/dispositivo proporciona certeza.

##### Codificación del ID de recurso

```c
#define PCI_NO_MSI      0x40000000
#define PCI_RID_MASK    0x0000ffff
```

El campo `rid` tiene doble función mediante empaquetado de bits:

**`PCI_RID_MASK` (0x0000ffff)**: Los 16 bits inferiores contienen el número real de BAR (0x10, 0x14, etc.). Aplicar la máscara con este valor extrae el ID de recurso para las funciones de asignación de bus.

**`PCI_NO_MSI` (0x40000000)**: El bit alto marca dispositivos con soporte de Message Signaled Interrupt (MSI) defectuoso o poco fiable. Algunas implementaciones de UART no implementan correctamente MSI, lo que provoca fallos en la entrega de interrupciones o cuelgues del sistema. Este flag indica a la función attach que use interrupciones de línea tradicionales en lugar de intentar la asignación de MSI.

Este esquema de codificación evita ampliar la estructura `pci_id` con un campo booleano adicional. Como los números de BAR solo usan el byte bajo, los bits altos quedan disponibles para flags. El driver extrae el RID real con `id->rid & PCI_RID_MASK` y comprueba la compatibilidad con MSI mediante `(id->rid & PCI_NO_MSI) == 0`.

##### Propósito en el emparejamiento de dispositivos

Estas estructuras rellenan un array estático de gran tamaño (que se examina en el siguiente fragmento) que la función probe busca durante la enumeración de dispositivos. Cuando el driver del bus PCI descubre un dispositivo de clase «Simple Communications» (módems y UARTs), llama a la función probe de este driver. La función probe recorre el array comparando los IDs del dispositivo con cada entrada, buscando una coincidencia. Al encontrarla, usa los valores `desc`, `rid`, `rclk` y `regshft` asociados para configurar el dispositivo correctamente.

Este enfoque basado en tablas simplifica la incorporación de nuevo hardware: la mayoría de las variantes nuevas de UART requieren solo añadir una entrada en la tabla con los IDs y la frecuencia de reloj correctos, sin modificar código.

#### 3) La **tabla de IDs** de PCI (partes ns8250 y compatibles)

A continuación figura la tabla **contigua** utilizada para emparejar vendor/dispositivo(/subvendor/subdispositivo), junto con hints por dispositivo (RID, reloj de referencia, desplazamiento de registro). La fila `0xffff` termina la lista.

```c
 86: static const struct pci_id pci_ns8250_ids[] = {
 87: { 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
 88: 	128 * DEFAULT_RCLK },
 89: { 0x1028, 0x0012, 0xffff, 0, "Dell RAC 4 Daughter Card Virtual UART", 0x14,
 90: 	128 * DEFAULT_RCLK },
 91: { 0x1033, 0x0074, 0x1033, 0x8014, "NEC RCV56ACF 56k Voice Modem", 0x10 },
 92: { 0x1033, 0x007d, 0x1033, 0x8012, "NEC RS232C", 0x10 },
 93: { 0x103c, 0x1048, 0x103c, 0x1227, "HP Diva Serial [GSP] UART - Powerbar SP2",
 94: 	0x10 },
 95: { 0x103c, 0x1048, 0x103c, 0x1301, "HP Diva RMP3", 0x14 },
 96: { 0x103c, 0x1290, 0xffff, 0, "HP Auxiliary Diva Serial Port", 0x18 },
 97: { 0x103c, 0x3301, 0xffff, 0, "HP iLO serial port", 0x10 },
 98: { 0x11c1, 0x0480, 0xffff, 0, "Agere Systems Venus Modem (V90, 56KFlex)", 0x14 },
 99: { 0x115d, 0x0103, 0xffff, 0, "Xircom Cardbus Ethernet + 56k Modem", 0x10 },
100: { 0x125b, 0x9100, 0xa000, 0x1000,
101: 	"ASIX AX99100 PCIe 1/2/3/4-port RS-232/422/485", 0x10 },
102: { 0x1282, 0x6585, 0xffff, 0, "Davicom 56PDV PCI Modem", 0x10 },
103: { 0x12b9, 0x1008, 0xffff, 0, "3Com 56K FaxModem Model 5610", 0x10 },
104: { 0x131f, 0x1000, 0xffff, 0, "Siig CyberSerial (1-port) 16550", 0x18 },
105: { 0x131f, 0x1001, 0xffff, 0, "Siig CyberSerial (1-port) 16650", 0x18 },
106: { 0x131f, 0x1002, 0xffff, 0, "Siig CyberSerial (1-port) 16850", 0x18 },
107: { 0x131f, 0x2000, 0xffff, 0, "Siig CyberSerial (1-port) 16550", 0x10 },
108: { 0x131f, 0x2001, 0xffff, 0, "Siig CyberSerial (1-port) 16650", 0x10 },
109: { 0x131f, 0x2002, 0xffff, 0, "Siig CyberSerial (1-port) 16850", 0x10 },
110: { 0x135a, 0x0a61, 0xffff, 0, "Brainboxes UC-324", 0x18 },
111: { 0x135a, 0x0aa1, 0xffff, 0, "Brainboxes UC-246", 0x18 },
112: { 0x135a, 0x0aa2, 0xffff, 0, "Brainboxes UC-246", 0x18 },
113: { 0x135a, 0x0d60, 0xffff, 0, "Intashield IS-100", 0x18 },
114: { 0x135a, 0x0da0, 0xffff, 0, "Intashield IS-300", 0x18 },
115: { 0x135a, 0x4000, 0xffff, 0, "Brainboxes PX-420", 0x10 },
116: { 0x135a, 0x4001, 0xffff, 0, "Brainboxes PX-431", 0x10 },
117: { 0x135a, 0x4002, 0xffff, 0, "Brainboxes PX-820", 0x10 },
118: { 0x135a, 0x4003, 0xffff, 0, "Brainboxes PX-831", 0x10 },
119: { 0x135a, 0x4004, 0xffff, 0, "Brainboxes PX-246", 0x10 },
120: { 0x135a, 0x4005, 0xffff, 0, "Brainboxes PX-101", 0x10 },
121: { 0x135a, 0x4006, 0xffff, 0, "Brainboxes PX-257", 0x10 },
122: { 0x135a, 0x4008, 0xffff, 0, "Brainboxes PX-846", 0x10 },
123: { 0x135a, 0x4009, 0xffff, 0, "Brainboxes PX-857", 0x10 },
124: { 0x135c, 0x0190, 0xffff, 0, "Quatech SSCLP-100", 0x18 },
125: { 0x135c, 0x01c0, 0xffff, 0, "Quatech SSCLP-200/300", 0x18 },
126: { 0x135e, 0x7101, 0xffff, 0, "Sealevel Systems Single Port RS-232/422/485/530",
127: 	0x18 },
128: { 0x1407, 0x0110, 0xffff, 0, "Lava Computer mfg DSerial-PCI Port A", 0x10 },
129: { 0x1407, 0x0111, 0xffff, 0, "Lava Computer mfg DSerial-PCI Port B", 0x10 },
130: { 0x1407, 0x0510, 0xffff, 0, "Lava SP Serial 550 PCI", 0x10 },
131: { 0x1409, 0x7168, 0x1409, 0x4025, "Timedia Technology Serial Port", 0x10,
132: 	8 * DEFAULT_RCLK },
133: { 0x1409, 0x7168, 0x1409, 0x4027, "Timedia Technology Serial Port", 0x10,
134: 	8 * DEFAULT_RCLK },
135: { 0x1409, 0x7168, 0x1409, 0x4028, "Timedia Technology Serial Port", 0x10,
136: 	8 * DEFAULT_RCLK },
137: { 0x1409, 0x7168, 0x1409, 0x5025, "Timedia Technology Serial Port", 0x10,
138: 	8 * DEFAULT_RCLK },
139: { 0x1409, 0x7168, 0x1409, 0x5027, "Timedia Technology Serial Port", 0x10,
140: 	8 * DEFAULT_RCLK },
141: { 0x1415, 0x950b, 0xffff, 0, "Oxford Semiconductor OXCB950 Cardbus 16950 UART",
142: 	0x10, 16384000 },
143: { 0x1415, 0xc120, 0xffff, 0, "Oxford Semiconductor OXPCIe952 PCIe 16950 UART",
144: 	0x10 },
145: { 0x14e4, 0x160a, 0xffff, 0, "Broadcom TruManage UART", 0x10,
146: 	128 * DEFAULT_RCLK, 2},
147: { 0x14e4, 0x4344, 0xffff, 0, "Sony Ericsson GC89 PC Card", 0x10},
148: { 0x151f, 0x0000, 0xffff, 0, "TOPIC Semiconductor TP560 56k modem", 0x10 },
149: { 0x1d0f, 0x8250, 0x0000, 0, "Amazon PCI serial device", 0x10 },
150: { 0x1d0f, 0x8250, 0x1d0f, 0, "Amazon PCI serial device", 0x10 },
151: { 0x1fd4, 0x1999, 0x1fd4, 0x0001, "Sunix SER5xxxx Serial Port", 0x10,
152: 	8 * DEFAULT_RCLK },
153: { 0x8086, 0x0c5f, 0xffff, 0, "Atom Processor S1200 UART",
154: 	0x10 | PCI_NO_MSI },
155: { 0x8086, 0x0f0a, 0xffff, 0, "Intel ValleyView LPIO1 HSUART#1", 0x10,
156: 	24 * DEFAULT_RCLK, 2 },
157: { 0x8086, 0x0f0c, 0xffff, 0, "Intel ValleyView LPIO1 HSUART#2", 0x10,
158: 	24 * DEFAULT_RCLK, 2 },
159: { 0x8086, 0x108f, 0xffff, 0, "Intel AMT - SOL", 0x10 },
160: { 0x8086, 0x19d8, 0xffff, 0, "Intel Denverton UART", 0x10 },
161: { 0x8086, 0x1c3d, 0xffff, 0, "Intel AMT - KT Controller", 0x10 },
162: { 0x8086, 0x1d3d, 0xffff, 0, "Intel C600/X79 Series Chipset KT Controller",
163: 	0x10 },
164: { 0x8086, 0x1e3d, 0xffff, 0, "Intel Panther Point KT Controller", 0x10 },
165: { 0x8086, 0x228a, 0xffff, 0, "Intel Cherryview SIO HSUART#1", 0x10,
166: 	24 * DEFAULT_RCLK, 2 },
167: { 0x8086, 0x228c, 0xffff, 0, "Intel Cherryview SIO HSUART#2", 0x10,
168: 	24 * DEFAULT_RCLK, 2 },
169: { 0x8086, 0x2a07, 0xffff, 0, "Intel AMT - PM965/GM965 KT Controller", 0x10 },
170: { 0x8086, 0x2a47, 0xffff, 0, "Mobile 4 Series Chipset KT Controller", 0x10 },
171: { 0x8086, 0x2e17, 0xffff, 0, "4 Series Chipset Serial KT Controller", 0x10 },
172: { 0x8086, 0x31bc, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 0", 0x10,
173: 	24 * DEFAULT_RCLK, 2 },
174: { 0x8086, 0x31be, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 1", 0x10,
175: 	24 * DEFAULT_RCLK, 2 },
176: { 0x8086, 0x31c0, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 2", 0x10,
177: 	24 * DEFAULT_RCLK, 2 },
178: { 0x8086, 0x31ee, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 3", 0x10,
179: 	24 * DEFAULT_RCLK, 2 },
180: { 0x8086, 0x3b67, 0xffff, 0, "5 Series/3400 Series Chipset KT Controller",
181: 	0x10 },
182: { 0x8086, 0x5abc, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 0", 0x10,
183: 	24 * DEFAULT_RCLK, 2 },
184: { 0x8086, 0x5abe, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 1", 0x10,
185: 	24 * DEFAULT_RCLK, 2 },
186: { 0x8086, 0x5ac0, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 2", 0x10,
187: 	24 * DEFAULT_RCLK, 2 },
188: { 0x8086, 0x5aee, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 3", 0x10,
189: 	24 * DEFAULT_RCLK, 2 },
190: { 0x8086, 0x8811, 0xffff, 0, "Intel EG20T Serial Port 0", 0x10 },
191: { 0x8086, 0x8812, 0xffff, 0, "Intel EG20T Serial Port 1", 0x10 },
192: { 0x8086, 0x8813, 0xffff, 0, "Intel EG20T Serial Port 2", 0x10 },
193: { 0x8086, 0x8814, 0xffff, 0, "Intel EG20T Serial Port 3", 0x10 },
194: { 0x8086, 0x8c3d, 0xffff, 0, "Intel Lynx Point KT Controller", 0x10 },
195: { 0x8086, 0x8cbd, 0xffff, 0, "Intel Wildcat Point KT Controller", 0x10 },
196: { 0x8086, 0x8d3d, 0xffff, 0,
197: 	"Intel Corporation C610/X99 series chipset KT Controller", 0x10 },
198: { 0x8086, 0x9c3d, 0xffff, 0, "Intel Lynx Point-LP HECI KT", 0x10 },
199: { 0x8086, 0xa13d, 0xffff, 0,
200: 	"100 Series/C230 Series Chipset Family KT Redirection",
201: 	0x10 | PCI_NO_MSI },
202: { 0x9710, 0x9820, 0x1000, 1, "NetMos NM9820 Serial Port", 0x10 },
203: { 0x9710, 0x9835, 0x1000, 1, "NetMos NM9835 Serial Port", 0x10 },
204: { 0x9710, 0x9865, 0xa000, 0x1000, "NetMos NM9865 Serial Port", 0x10 },
205: { 0x9710, 0x9900, 0xa000, 0x1000,
206: 	"MosChip MCS9900 PCIe to Peripheral Controller", 0x10 },
207: { 0x9710, 0x9901, 0xa000, 0x1000,
208: 	"MosChip MCS9901 PCIe to Peripheral Controller", 0x10 },
209: { 0x9710, 0x9904, 0xa000, 0x1000,
210: 	"MosChip MCS9904 PCIe to Peripheral Controller", 0x10 },
211: { 0x9710, 0x9922, 0xa000, 0x1000,
212: 	"MosChip MCS9922 PCIe to Peripheral Controller", 0x10 },
213: { 0xdeaf, 0x9051, 0xffff, 0, "Middle Digital PC Weasel Serial Port", 0x10 },
214: { 0xffff, 0, 0xffff, 0, NULL, 0, 0}
215: };
```

*Observa el **RID** por dispositivo (qué BAR/IRQ), los hints de frecuencia (`rclk` como `24 \* DEFAULT_RCLK`) y el `regshft` opcional.*

##### La tabla de identificación de dispositivos

El array `pci_ns8250_ids` es el núcleo de la lógica de reconocimiento de dispositivos del driver. Esta tabla enumera todas las variantes conocidas de UART PCI compatibles con la interfaz de registros NS8250/16550, junto con los parámetros de configuración necesarios para operar cada una correctamente. Durante el arranque del sistema, el driver del bus PCI recorre todos los dispositivos descubiertos y llama a la función probe de este driver para posibles coincidencias; la función probe busca en esta tabla para determinar la compatibilidad.

##### Estructura y propósito de la tabla

```c
static const struct pci_id pci_ns8250_ids[] = {
```

El nombre del array, `pci_ns8250_ids`, refleja que todos los dispositivos listados implementan la interfaz de registros de National Semiconductor 8250 (o compatible 16450/16550/16650/16750/16850/16950). A pesar de provenir de docenas de fabricantes, estos UARTs comparten un modelo de programación común que se remonta al diseño del puerto serie del IBM PC original. Esta compatibilidad permite que un único driver gestione hardware heterogéneo mediante una abstracción de registros unificada.

Los calificadores `static const` indican que estos datos son de solo lectura e internos a esta unidad de compilación. La tabla reside en memoria de solo lectura, lo que evita modificaciones accidentales y permite que el kernel comparta una única copia entre todos los núcleos de CPU.

##### Análisis de entradas: comprendiendo los patrones

El examen de entradas representativas revela la jerarquía de emparejamiento y la diversidad de configuración:

**Coincidencia simple con comodín** (entrada de Intel AMT SOL en `pci_ns8250_ids`):

```c
{ 0x8086, 0x108f, 0xffff, 0, "Intel AMT - SOL", 0x10 },
```

- Proveedor 0x8086 (Intel), dispositivo 0x108f (AMT Serial-over-LAN)
- Los IDs de subsistema 0xffff (comodín) coinciden con todas las variantes de fabricante
- Descripción para los mensajes de arranque y los listados de dispositivos
- RID 0x10 (BAR0), frecuencia de reloj estándar (DEFAULT_RCLK implícito), sin desplazamiento de registros

Este patrón coincide con el controlador AMT SOL de Intel independientemente del fabricante de la placa base que lo haya integrado.

**Coincidencia específica por fabricante** (entradas adyacentes de HP Diva en `pci_ns8250_ids`):

```c
{ 0x103c, 0x1048, 0x103c, 0x1227, "HP Diva Serial [GSP] UART - Powerbar SP2", 0x10 },
{ 0x103c, 0x1048, 0x103c, 0x1301, "HP Diva RMP3", 0x14 },
```

- El mismo chipset (proveedor HP 0x103c, dispositivo 0x1048) se usa en varios productos
- Los IDs de dispositivo de subsistema distintos (0x1227, 0x1301) diferencian las variantes
- Los diferentes BAR (0x10 y 0x14) indican que el UART aparece en direcciones distintas dentro del espacio de configuración de cada tarjeta

Esto ilustra cómo un mismo chipset genera múltiples entradas en la tabla cuando los fabricantes lo configuran de forma diferente en cada línea de producto.

**Frecuencia de reloj no estándar** (entrada de Dell Remote Access Card III en `pci_ns8250_ids`):

```c
{ 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
    128 * DEFAULT_RCLK },
```

- El RAC III de Dell (0x1028) usa 128 veces el reloj estándar de 1,8432 MHz, lo que da 235,9296 MHz
- Esta frecuencia extremadamente alta permite velocidades en baudios muy superiores a las de los puertos serie convencionales
- Sin el valor correcto de `rclk`, todos los cálculos de velocidad en baudios estarían equivocados por un factor de 128, produciendo datos ilegibles

Las tarjetas de gestión de servidores suelen emplear relojes rápidos para admitir la redirección de consola a alta velocidad sobre enlaces de red.

**Desplazamiento de dirección de registros** (entrada de Intel ValleyView LPIO1 HSUART en `pci_ns8250_ids`):

```c
{ 0x8086, 0x0f0a, 0xffff, 0, "Intel ValleyView LPIO1 HSUART#1", 0x10,
    24 * DEFAULT_RCLK, 2 },
```

- UART de SoC Intel con reloj 24 veces mayor que el estándar para operación a alta velocidad
- `regshft = 2` significa que los registros aparecen a intervalos de 4 bytes (direcciones 0, 4, 8, 12, ...)
- El código genérico del UART desplaza todos los desplazamientos de registro hacia la izquierda 2 bits: `address << 2`

Esto permite adaptarse a diseños de SoC donde el UART comparte una gran región de memoria con otros periféricos, normalmente con registros alineados a límites de 32 bits por eficiencia en el bus.

**Incompatibilidad con MSI** (entrada Atom Processor S1200 en `pci_ns8250_ids`, combinada con el tratamiento de `PCI_NO_MSI` en `uart_pci_attach`):

```c
{ 0x8086, 0x0c5f, 0xffff, 0, "Atom Processor S1200 UART",
    0x10 | PCI_NO_MSI },
```

- La bandera `PCI_NO_MSI` en el campo RID indica soporte MSI defectuoso
- La función attach detectará esta bandera y usará en su lugar interrupciones por línea clásicas
- Estos dispositivos declaran capacidad MSI en su espacio de configuración PCI pero no entregan las interrupciones correctamente

Este tipo de peculiaridades suelen surgir de erratas de silicio o de implementaciones incompletas de MSI en periféricos integrados.

**Múltiples variantes de subsistema** (entradas de Timedia Technology en `pci_ns8250_ids`):

```c
{ 0x1409, 0x7168, 0x1409, 0x4025, "Timedia Technology Serial Port", 0x10,
    8 * DEFAULT_RCLK },
{ 0x1409, 0x7168, 0x1409, 0x4027, "Timedia Technology Serial Port", 0x10,
    8 * DEFAULT_RCLK },
```

- El mismo chipset base (proveedor 0x1409, dispositivo 0x7168) se usa en toda una familia de productos
- Cada ID de dispositivo de subsistema representa un modelo de tarjeta o una variante con distinto número de puertos
- Todas comparten el mismo reloj (8 veces el estándar) y la misma configuración de BAR
- La función probe selecciona la primera entrada con IDs de subsistema compatibles

Esta repetición es inevitable cuando un fabricante usa un mismo chipset en muchas referencias, cada una con su propia identificación de subsistema.

##### La entrada centinela

```c
{ 0xffff, 0, 0xffff, 0, NULL, 0, 0}
```

La última entrada marca el final de la tabla. La función de coincidencia recorre las entradas hasta encontrar `vendor == 0xffff`, lo que indica que no hay más dispositivos que comprobar. El uso de 0xffff (un ID de proveedor inválido; ningún proveedor real tiene ese valor) garantiza que el centinela no pueda coincidir accidentalmente con hardware real.

##### Mantenimiento y evolución de la tabla

Esta tabla crece continuamente a medida que aparece nuevo hardware UART. Añadir soporte para un dispositivo nuevo suele requerir:

1. Determinar los IDs de proveedor, dispositivo y subsistema (mediante `pciconf -lv` en FreeBSD)
2. Encontrar el BAR correcto donde residen los registros del UART (a menudo documentado, a veces descubierto por prueba y error)
3. Identificar la frecuencia del reloj (a partir de datasheets o experimentación)
4. Verificar que el acceso estándar a los registros NS8250 funciona correctamente

La mayoría de las entradas usan valores por defecto (reloj estándar, sin desplazamiento, BAR0) y solo requieren los IDs y una descripción. Las entradas más complejas, como las de relojes inusuales o peculiaridades MSI, suelen surgir de informes de errores o donaciones de hardware a los desarrolladores.

El enfoque basado en tablas mantiene el código fácil de mantener: añadir un nuevo UART raramente requiere cambios en el código, solo una nueva entrada en la tabla. Esto es fundamental para un subsistema que da soporte a decenas de fabricantes y cientos de variantes de producto acumuladas a lo largo de décadas de evolución del hardware PC.

##### Nota arquitectónica

Esta tabla documenta únicamente los UART compatibles con NS8250. Los controladores serie no compatibles (como los adaptadores serie USB, los serial IEEE 1394 o los diseños propietarios) usan drivers distintos. La función probe verifica la compatibilidad NS8250 antes de aceptar un dispositivo, lo que garantiza que las suposiciones de esta tabla se cumplan para todo el hardware reconocido.

#### 4) Función de coincidencia: de los IDs PCI a un resultado

```c
218: const static struct pci_id *
219: uart_pci_match(device_t dev, const struct pci_id *id)
220: {
221: 	uint16_t device, subdev, subven, vendor;
222: 
223: 	vendor = pci_get_vendor(dev);
224: 	device = pci_get_device(dev);
225: 	while (id->vendor != 0xffff &&
226: 	    (id->vendor != vendor || id->device != device))
227: 		id++;
228: 	if (id->vendor == 0xffff)
229: 		return (NULL);
230: 	if (id->subven == 0xffff)
231: 		return (id);
232: 	subven = pci_get_subvendor(dev);
233: 	subdev = pci_get_subdevice(dev);
234: 	while (id->vendor == vendor && id->device == device &&
235: 	    (id->subven != subven || id->subdev != subdev))
236: 		id++;
237: 	return ((id->vendor == vendor && id->device == device) ? id : NULL);
```

*Primero se hace coincidir el proveedor y el dispositivo; si la entrada tiene IDs de subsistema específicos, también se comprueban; de lo contrario se acepta el comodín.*

##### Lógica de coincidencia de dispositivos: `uart_pci_match`

La función `uart_pci_match` implementa un algoritmo de búsqueda en dos fases que hace coincidir dispositivos PCI con la tabla de identificación de forma eficiente, respetando la jerarquía proveedor/dispositivo/subsistema. Esta función es el núcleo del reconocimiento de dispositivos y se llama durante el probe para determinar si un dispositivo PCI descubierto es un UART compatible.

##### Firma y contexto de la función

```c
const static struct pci_id *
uart_pci_match(device_t dev, const struct pci_id *id)
{
    uint16_t device, subdev, subven, vendor;
```

La función recibe un `device_t` que representa el dispositivo PCI que se está sondeando y un puntero al inicio de la tabla de identificación. Devuelve un puntero a la entrada `pci_id` coincidente (que contiene los parámetros de configuración) o NULL si no existe ninguna coincidencia.

El tipo de retorno es `const struct pci_id *` porque la función devuelve un puntero a la tabla de solo lectura; el llamador no debe modificar la entrada devuelta.

##### Fase uno: coincidencia de IDs primarios

```c
vendor = pci_get_vendor(dev);
device = pci_get_device(dev);
while (id->vendor != 0xffff &&
    (id->vendor != vendor || id->device != device))
    id++;
if (id->vendor == 0xffff)
    return (NULL);
```

La función comienza leyendo la identificación primaria del dispositivo desde el espacio de configuración PCI. Las funciones `pci_get_vendor()` y `pci_get_device()` acceden a los registros de configuración 0x00 y 0x02, que todo dispositivo PCI debe implementar.

**El bucle de búsqueda**: la condición del `while` tiene dos criterios de terminación:

1. `id->vendor != 0xffff`: no se ha llegado al centinela
2. `(id->vendor != vendor || id->device != device)`: la entrada actual no coincide

El bucle avanza por la tabla hasta encontrar un par proveedor/dispositivo coincidente o el centinela. Esta búsqueda lineal es aceptable porque:

- La tabla tiene menos de 100 entradas (rápido incluso con búsqueda lineal)
- El probe ocurre una sola vez por dispositivo durante el arranque (no es un camino crítico en rendimiento)
- La tabla reside en memoria secuencial, favorable para la caché

**Detección del centinela**: si el bucle termina con `id->vendor == 0xffff`, ninguna entrada coincidió con los IDs primarios del dispositivo. Devolver NULL señala "no es mi dispositivo" a la función probe, que a su vez devolverá `ENXIO` para dar oportunidad a otros drivers.

##### Tratamiento de subsistema con comodín

```c
if (id->subven == 0xffff)
    return (id);
```

Este es el camino rápido para las entradas con IDs de subsistema comodín. Cuando `subven == 0xffff`, la entrada coincide con todas las variantes de este chipset independientemente de la personalización del fabricante. La función retorna inmediatamente sin leer los IDs de subsistema del espacio de configuración.

Esta optimización evita lecturas innecesarias del espacio de configuración PCI en el caso habitual en que el driver acepta todas las variantes OEM de un chipset (por ejemplo, "Intel AMT - SOL" coincide con el chipset de Intel en cualquier placa base).

##### Fase dos: coincidencia de IDs de subsistema

```c
subven = pci_get_subvendor(dev);
subdev = pci_get_subdevice(dev);
while (id->vendor == vendor && id->device == device &&
    (id->subven != subven || id->subdev != subdev))
    id++;
```

Para las entradas que requieren coincidencias de subsistema específicas, la función lee los IDs de proveedor y dispositivo de subsistema desde los registros de configuración PCI 0x2C y 0x2E.

**El bucle de refinamiento**: esta segunda búsqueda avanza por las entradas consecutivas de la tabla con los mismos IDs primarios, buscando una coincidencia de subsistema. El bucle continúa mientras:

1. `id->vendor == vendor && id->device == device`: se siguen examinando entradas de este chipset
2. `(id->subven != subven || id->subdev != subdev)`: los IDs de subsistema no coinciden

Esto permite gestionar tablas con múltiples entradas para un mismo chipset, cada una especificando distintas variantes OEM:

c

```c
{ 0x103c, 0x1048, 0x103c, 0x1227, "HP Diva Serial - Powerbar SP2", 0x10 },
{ 0x103c, 0x1048, 0x103c, 0x1301, "HP Diva RMP3", 0x14 },
```

Ambas entradas tienen el proveedor 0x103c y el dispositivo 0x1048, pero IDs de dispositivo de subsistema distintos. El bucle examina cada una hasta encontrar la variante correcta.

##### Validación final

```c
return ((id->vendor == vendor && id->device == device) ? id : NULL);
```

Cuando el bucle de refinamiento termina, se cumple una de estas dos condiciones:

1. El bucle encontró una entrada coincidente (los cuatro IDs coinciden): se devuelve esa entrada.
2. El bucle agotó las entradas de este chipset sin encontrar coincidencia de subsistema: se devuelve NULL.

La expresión ternaria realiza una comprobación final: aunque la condición del bucle garantiza que `id` apunta a una entrada con IDs primarios coincidentes (o más allá de la última de ese tipo), la verificación explícita asegura un comportamiento correcto si el bucle avanzó más allá de todas las entradas de ese dispositivo sin encontrar coincidencia de subsistema.

Esto cubre el caso en que:

- Los IDs primarios coinciden (la fase uno tuvo éxito)
- La tabla tiene entradas con requisitos de subsistema específicos
- Ninguna de esas entradas de subsistema coincide con el dispositivo
- El bucle avanzó hasta encontrar un ID primario diferente o el centinela

##### Ejemplos de coincidencia

**Ejemplo 1: coincidencia simple con comodín**

- Dispositivo: Intel AMT SOL (proveedor 0x8086, dispositivo 0x108f)
- Fase uno: encuentra `{ 0x8086, 0x108f, 0xffff, 0, ... }`
- Comprobación de comodín: `subven == 0xffff`, retorno inmediato
- Resultado: coincidencia sin leer los IDs de subsistema

**Ejemplo 2: coincidencia específica por fabricante**

- Dispositivo: HP Diva RMP3 (proveedor 0x103c, dispositivo 0x1048, subven 0x103c, subdev 0x1301)
- Fase uno: encuentra la primera entrada con proveedor 0x103c, dispositivo 0x1048
- Comprobación de comodín: `subven != 0xffff`, se leen los IDs de subsistema
- Fase dos: la primera entrada tiene subdev 0x1227 (no coincide), se avanza
- Fase dos: la segunda entrada tiene subdev 0x1301 (¡coincide!), se devuelve
- Resultado: devuelve la segunda entrada con BAR 0x14 y la descripción correcta

**Ejemplo 3: sin coincidencia**

- Dispositivo: UART desconocido (proveedor 0x1234, dispositivo 0x5678)
- Fase uno: recorre toda la tabla sin encontrar IDs primarios coincidentes
- Detección del centinela: devuelve NULL
- Resultado: la función probe devuelve `ENXIO`

##### Consideraciones de eficiencia

El enfoque en dos fases optimiza el caso habitual:

- La mayoría de las entradas de la tabla usan subsistemas comodín (solo requieren coincidencia de IDs primarios)
- Leer el espacio de configuración PCI es más lento que acceder a memoria
- Posponer la lectura de los IDs de subsistema hasta que sea necesario reduce la latencia del probe

Para los dispositivos con entradas comodín, la función realiza dos lecturas del espacio de configuración (proveedor y dispositivo) y retorna. Solo los dispositivos que requieren coincidencia de subsistema incurren en cuatro lecturas.

La búsqueda lineal está justificada porque:

- El tamaño de la tabla está acotado y es pequeño (< 100 entradas)
- Los procesadores modernos hacen prefetch de memoria secuencial de forma eficiente
- El probe ocurre una vez en la vida del dispositivo, no en caminos de I/O
- La simplicidad del código supera la mejora marginal de una búsqueda binaria o tablas hash

##### Integración con la función probe

La función probe llama a `uart_pci_match` con el puntero base de la tabla:

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if (id != NULL) {
    sc->sc_class = &uart_ns8250_class;
    goto match;
}
```

Un retorno no NULL confirma que el dispositivo es compatible y proporciona acceso a sus parámetros de configuración (`id->rid`, `id->rclk`, `id->regshft`). La función probe usa estos valores para inicializar correctamente la capa UART genérica según las particularidades de este hardware.

#### 5) Función auxiliar de unicidad de consola (infrecuente, pero instructiva)

```c
239: extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;
242: static const struct pci_unique_id pci_unique_devices[] = {
243: { 0x1d0f, 0x8250 }	/* Amazon PCI serial device */
244: };
248: static void
249: uart_pci_unique_console_match(device_t dev)
250: {
251: 	struct uart_softc *sc;
252: 	struct uart_devinfo * sysdev;
253: 	const struct pci_unique_id * id;
254: 	uint16_t vendor, device;
255: 
256: 	sc = device_get_softc(dev);
257: 	vendor = pci_get_vendor(dev);
258: 	device = pci_get_device(dev);
259: 
260: 	/* Is this a device known to exist only once in a system? */
261: 	for (id = pci_unique_devices; ; id++) {
262: 		if (id == &pci_unique_devices[nitems(pci_unique_devices)])
263: 			return;
264: 		if (id->vendor == vendor && id->device == device)
265: 			break;
266: 	}
267: 
268: 	/* If it matches a console, it must be the same device. */
269: 	SLIST_FOREACH(sysdev, &uart_sysdevs, next) {
270: 		if (sysdev->pci_info.vendor == vendor &&
271: 		    sysdev->pci_info.device == device) {
272: 			sc->sc_sysdev = sysdev;
273: 			sysdev->bas.rclk = sc->sc_bas.rclk;
274: 		}
275: 	}
```

*Si se sabe que un UART PCI es **único** en el sistema, se lo asocia automáticamente con la instancia de consola.*

##### Coincidencia de dispositivo de consola: `uart_pci_unique_console_match`

FreeBSD necesita identificar qué UART actúa como consola del sistema, es decir, el dispositivo donde aparecen los mensajes de arranque y donde se produce el inicio de sesión en modo monousuario. En la mayoría de los sistemas, el firmware o el bootloader configura la consola antes de que el kernel arranque, pero el kernel debe posteriormente asociar esa consola preconfigurada con la instancia correcta del driver durante la enumeración PCI. La función `uart_pci_unique_console_match` resuelve este problema de asociación para los dispositivos que se garantiza que existen como mucho una vez por sistema.

##### El problema de la asociación de la consola

Cuando el kernel arranca, la salida temprana por consola puede utilizar un UART inicializado por el firmware (BIOS/UEFI) o por el boot loader. Este «dispositivo de sistema» (`sysdev`) tiene direcciones de registro y configuración básica, pero no está asociado con ninguna entrada del árbol de dispositivos PCI. Más adelante, durante la enumeración normal de dispositivos, el driver del bus PCI descubre los UARTs y conecta instancias del driver. El kernel debe determinar qué dispositivo enumerado corresponde a la consola preconfigurada.

El desafío está en que el orden de enumeración PCI no está garantizado. El dispositivo en la dirección PCI `0:1f:3` (bus 0, dispositivo 31, función 3) puede enumerarse como `uart0` en un arranque y como `uart1` después de añadir una tarjeta. Hacer la correspondencia por posición en el árbol de dispositivos sería poco fiable.

##### El enfoque del dispositivo único

```c
extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static const struct pci_unique_id pci_unique_devices[] = {
{ 0x1d0f, 0x8250 }  /* Amazon PCI serial device */
};
```

La solución para cierto hardware: algunos dispositivos tienen garantía arquitectónica de existir una sola vez. Los controladores de gestión de servidores, los UARTs integrados en SoC y los puertos serie de instancias en la nube entran en esta categoría. Para estos dispositivos, los IDs de vendedor y dispositivo son suficientes para establecer la correspondencia.

La lista `uart_sysdevs` contiene los dispositivos de consola preconfigurados registrados durante el arranque temprano. Cada estructura `uart_devinfo` captura la dirección base de registro de la consola, la velocidad de transmisión y, si se conoce, la identificación PCI.

El array `pci_unique_devices` lista los dispositivos que cumplen el criterio de unicidad. Actualmente solo contiene el dispositivo serie EC2 de Amazon (vendedor 0x1d0f, dispositivo 0x8250), que existe exactamente una vez en las instancias EC2 y sirve como consola para el acceso por consola serie.

##### Entrada de la función e identificación del dispositivo

```c
static void
uart_pci_unique_console_match(device_t dev)
{
    struct uart_softc *sc;
    struct uart_devinfo * sysdev;
    const struct pci_unique_id * id;
    uint16_t vendor, device;

    sc = device_get_softc(dev);
    vendor = pci_get_vendor(dev);
    device = pci_get_device(dev);
```

La función se invoca desde `uart_pci_probe` tras la identificación satisfactoria del dispositivo, pero antes de que el probe finalice. Recibe el dispositivo que se está sondeando y obtiene:

- El softc (estado de la instancia del driver) mediante `device_get_softc()`
- Los IDs de vendedor y dispositivo del espacio de configuración PCI

En este punto, el softc ha sido inicializado parcialmente por `uart_bus_probe()` con los métodos de acceso a registros y las frecuencias de reloj, pero `sc->sc_sysdev` es NULL a menos que la correspondencia con la consola tenga éxito.

##### Verificación de unicidad

```c
/* Is this a device known to exist only once in a system? */
for (id = pci_unique_devices; ; id++) {
    if (id == &pci_unique_devices[nitems(pci_unique_devices)])
        return;
    if (id->vendor == vendor && id->device == device)
        break;
}
```

El bucle busca una coincidencia en la tabla de dispositivos únicos. Hay dos condiciones de salida:

**No único**: si el bucle recorre todas las entradas sin encontrar coincidencia, no hay garantía de que este dispositivo sea único. La función retorna inmediatamente; la correspondencia con la consola requiere una identificación más estricta (que probablemente incluya IDs de subsistema o comparación de direcciones base), algo que esta función no intenta.

**Es único**: si los IDs de vendedor y dispositivo coinciden con una entrada, el dispositivo tiene garantía de ser único en el sistema. El bucle se interrumpe y el proceso de correspondencia continúa.

La comprobación de límites del array utiliza `nitems(pci_unique_devices)`, una macro que calcula el número de elementos del array. Esta comparación de punteros detecta cuándo `id` ha avanzado más allá del final del array:

```c
if (id == &pci_unique_devices[nitems(pci_unique_devices)])
```

Esto equivale a `id == pci_unique_devices + array_length`, que comprueba si el puntero es igual a la dirección justo después del último elemento válido.

##### Correspondencia del dispositivo de consola

```c
/* If it matches a console, it must be the same device. */
SLIST_FOREACH(sysdev, &uart_sysdevs, next) {
    if (sysdev->pci_info.vendor == vendor &&
        sysdev->pci_info.device == device) {
        sc->sc_sysdev = sysdev;
        sysdev->bas.rclk = sc->sc_bas.rclk;
    }
}
```

La macro `SLIST_FOREACH` itera la lista de dispositivos del sistema, comprobando cada consola preconfigurada en busca de IDs PCI coincidentes. La lista normalmente contiene cero o una entrada (sistemas sin consola serie o con una sola consola), pero el código gestiona correctamente múltiples consolas.

**Confirmación de la coincidencia**: cuando `sysdev->pci_info` coincide con los IDs de vendedor y dispositivo, la garantía de unicidad asegura que este dispositivo enumerado es el mismo hardware físico que el firmware configuró como consola. No existe ambigüedad; solo hay un dispositivo con esos IDs en el sistema.

**Enlace de instancias**: `sc->sc_sysdev = sysdev` crea una asociación bidireccional:

- La instancia del driver (`sc`) sabe ahora que gestiona un dispositivo de consola
- Se activan los comportamientos específicos de consola: gestión de caracteres especiales, salida de mensajes del kernel y entrada al depurador

**Sincronización del reloj**: `sysdev->bas.rclk = sc->sc_bas.rclk` actualiza la frecuencia de reloj del dispositivo de sistema para que coincida con el valor de la tabla de identificación. La inicialización en el arranque temprano puede no conocer la frecuencia de reloj exacta, y utiliza un valor predeterminado o detectado durante el probe. El driver PCI, al haber emparejado el dispositivo con la tabla, conoce la frecuencia correcta y actualiza el registro del dispositivo de sistema.

Esta actualización del reloj es fundamental: si el arranque temprano utilizó un reloj incorrecto, los cálculos de la velocidad de transmisión serían erróneos. La consola podría haber funcionado por casualidad (si el firmware configuró directamente el divisor del UART), pero fallaría cuando el driver la reconfigurara. Sincronizar `rclk` garantiza que las operaciones posteriores utilicen valores correctos.

##### Por qué existe esta función

La correspondencia tradicional de consola compara direcciones base: la dirección de registro físico del dispositivo de sistema coincide con el BAR PCI de uno de los dispositivos enumerados. Esto funciona de forma fiable, pero requiere leer los BARs de todos los UARTs y gestionar complicaciones como la distinción entre puertos I/O y registros mapeados en memoria.

Para los dispositivos únicos, la correspondencia por ID de vendedor y dispositivo es más sencilla e igual de fiable. La garantía de unicidad elimina toda ambigüedad: si un dispositivo único existe como consola y ese dispositivo se enumera, tienen que ser el mismo.

##### Limitaciones y alcance

Esta función solo gestiona los dispositivos presentes en `pci_unique_devices`. La mayoría de los UARTs no cumplen los requisitos:

- Las tarjetas multipuerto tienen IDs de vendedor y dispositivo idénticos para todos los puertos
- Los chipsets genéricos aparecen en múltiples productos
- Los UARTs de placa base de un mismo fabricante pueden usar el mismo chipset en toda su gama de productos

Para los dispositivos no únicos, la función probe recurre a otros métodos de correspondencia (normalmente la comparación de direcciones base en `uart_bus_probe`), o bien la asociación con la consola puede establecerse mediante hints o propiedades del árbol de dispositivos.

La función se invoca de forma oportunista: intenta establecer la correspondencia para todos los dispositivos sondeados, pero solo tiene éxito con los dispositivos únicos que además son consolas. El fallo no es un error; simplemente significa que el dispositivo no es único o no es una consola.

##### Contexto de integración

La función probe la invoca tras la identificación inicial del dispositivo:

```c
result = uart_bus_probe(dev, ...);
if (sc->sc_sysdev == NULL)
    uart_pci_unique_console_match(dev);
```

La comprobación `sc->sc_sysdev == NULL` garantiza que esta función se ejecute solo si `uart_bus_probe` no estableció ya una asociación con la consola por otros medios. Este orden proporciona una alternativa de respaldo: primero se intenta la correspondencia precisa (comparación de direcciones base) y, a continuación, la correspondencia por dispositivo único.

Si la correspondencia tiene éxito, las operaciones posteriores del driver reconocen el estado de consola y habilitan un tratamiento especial: salida síncrona para mensajes de pánico, detección del carácter de interrupción del depurador y enrutamiento de mensajes del kernel.

#### 6) `probe`: elegir la clase y llamar al probe **compartido** del bus

```c
277: static int
278: uart_pci_probe(device_t dev)
279: {
280: 	struct uart_softc *sc;
281: 	const struct pci_id *id;
282: 	struct pci_id cid = {
283: 		.regshft = 0,
284: 		.rclk = 0,
285: 		.rid = 0x10 | PCI_NO_MSI,
286: 		.desc = "Generic SimpleComm PCI device",
287: 	};
288: 	int result;
289: 
290: 	sc = device_get_softc(dev);
291: 
292: 	id = uart_pci_match(dev, pci_ns8250_ids);
293: 	if (id != NULL) {
294: 		sc->sc_class = &uart_ns8250_class;
295: 		goto match;
296: 	}
297: 	if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
298: 	    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
299: 	    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
300: 		/* XXX rclk what to do */
301: 		id = &cid;
302: 		sc->sc_class = &uart_ns8250_class;
303: 		goto match;
304: 	}
305: 	/* Add checks for non-ns8250 IDs here. */
306: 	return (ENXIO);
307: 
308:  match:
309: 	result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
310: 	    id->rid & PCI_RID_MASK, 0, 0);
311: 	/* Bail out on error. */
312: 	if (result > 0)
313: 		return (result);
314: 	/*
315: 	 * If we haven't already matched this to a console, check if it's a
316: 	 * PCI device which is known to only exist once in any given system
317: 	 * and we can match it that way.
318: 	 */
319: 	if (sc->sc_sysdev == NULL)
320: 		uart_pci_unique_console_match(dev);
321: 	/* Set/override the device description. */
322: 	if (id->desc)
323: 		device_set_desc(dev, id->desc);
324: 	return (result);
325: }
```

*Dos rutas hacia una coincidencia: una entrada explícita en la tabla o la alternativa por clase/subclase. Después se invoca el **probe del bus UART** con `regshft`, `rclk` y `rid`.*

##### La función probe del dispositivo: `uart_pci_probe`

La función probe es la primera interacción del kernel con un posible dispositivo durante la enumeración. Cuando el driver del bus PCI descubre un dispositivo, invoca la función probe de cada driver registrado, preguntando «¿puedes gestionar este dispositivo?». La función probe examina la identificación y la configuración del hardware, y devuelve un valor de prioridad que indica la calidad de la coincidencia o un error que significa «este dispositivo no es mío».

##### Propósito y contrato de la función

```c
static int
uart_pci_probe(device_t dev)
{
    struct uart_softc *sc;
    const struct pci_id *id;
    int result;

    sc = device_get_softc(dev);
```

La función probe recibe un `device_t` que representa el hardware que se está examinando. Debe determinar la compatibilidad sin modificar el estado del dispositivo ni asignar recursos; esas operaciones corresponden a la función attach.

El valor de retorno codifica los resultados del probe:

- Los valores negativos o cero indican éxito, y los valores más bajos representan coincidencias de mayor calidad
- Los valores positivos (en especial `ENXIO`) indican «este driver no puede gestionar este dispositivo»
- El kernel selecciona el driver que devuelve el valor más bajo (el mejor)

El softc se obtiene mediante `device_get_softc()`, que devuelve una estructura inicializada a cero del tamaño especificado en la declaración del driver (`sizeof(struct uart_softc)`). La función probe inicializa campos críticos como `sc_class` antes de delegar en el código genérico.

##### Correspondencia explícita en la tabla de dispositivos

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if (id != NULL) {
    sc->sc_class = &uart_ns8250_class;
    goto match;
}
```

La ruta de correspondencia principal busca en la tabla explícita de dispositivos. Si `uart_pci_match` devuelve un valor distinto de NULL, el dispositivo está explícitamente soportado con parámetros de configuración conocidos.

**Asignación de la clase UART**: `sc->sc_class = &uart_ns8250_class` asigna la tabla de funciones para el acceso a registros compatible con NS8250. La estructura `uart_class` (definida en la capa genérica UART) contiene punteros a funciones para operaciones como:

- Lectura y escritura de registros
- Configuración de velocidades de transmisión
- Gestión de FIFOs y control de flujo
- Gestión de interrupciones

Las distintas familias de UART (NS8250/16550, SAB82532, Z8530) asignarían punteros de clase diferentes. Este driver solo gestiona variantes NS8250, por lo que la asignación de clase es incondicional.

El `goto match` omite las comprobaciones posteriores; una vez identificado de forma explícita, no se necesitan más heurísticas.

##### Alternativa genérica para dispositivos SimpleComm

```c
if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
    /* XXX rclk what to do */
    id = &cid;
    sc->sc_class = &uart_ns8250_class;
    goto match;
}
```

Esta alternativa gestiona los dispositivos que no están en la tabla explícita pero que se anuncian como UARTs genéricos mediante códigos de clase PCI. La especificación PCI define una jerarquía de clase, subclase e interfaz de programación para categorizar los dispositivos:

**Comprobación de clase**: `PCIC_SIMPLECOMM` (0x07) identifica los «controladores de comunicación simple», que incluyen puertos serie, puertos paralelo y módems.

**Comprobación de subclase**: `PCIS_SIMPLECOMM_UART` (0x00) lo acota específicamente a los controladores serie.

**Comprobación de interfaz de programación**: `pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A` acepta los dispositivos que declaran interfaces de programación compatibles con 8250 (ProgIF 0x00) o con 16450 (ProgIF 0x01), pero rechaza los que declaran compatibilidad con 16550A (ProgIF 0x02) o superior.

Esta lógica aparentemente inversa existe porque las primeras implementaciones del 16550A tenían FIFOs defectuosas. La especificación PCI permitía que los dispositivos se declarasen «compatibles con 16550» sin especificar si las FIFOs funcionaban. Rechazar los valores ProgIF de 16550A o superior obliga a estos dispositivos a pasar por la correspondencia en la tabla explícita, donde se pueden documentar sus particularidades. Solo se confía en las declaraciones conservadoras de compatibilidad 8250/16450.

**Configuración de alternativa**: la estructura `cid` (declarada al inicio de la función) proporciona los parámetros predeterminados:

```c
struct pci_id cid = {
    .regshft = 0,        /* Standard register spacing */
    .rclk = 0,           /* Use default clock */
    .rid = 0x10 | PCI_NO_MSI,  /* BAR0, no MSI */
    .desc = "Generic SimpleComm PCI device",
};
```

El comentario `/* XXX rclk what to do */` pone de manifiesto la incertidumbre: sin una entrada explícita en la tabla, la frecuencia de reloj correcta es desconocida. El código genérico usa por defecto 1,8432 MHz (la frecuencia estándar del reloj UART en PC), que funciona para la mayoría del hardware pero falla en dispositivos con relojes no estándar.

La marca `PCI_NO_MSI` en el RID predeterminado deshabilita MSI para los dispositivos genéricos. Como no se conocen las particularidades del dispositivo, una gestión conservadora de las interrupciones previene posibles bloqueos o tormentas de interrupciones relacionadas con MSI.

Asignar `id = &cid` hace que esta estructura local sea visible para la ruta de correspondencia siguiente, tratando la configuración genérica como si procediera de la tabla.

##### Salida sin coincidencia

```c
/* Add checks for non-ns8250 IDs here. */
return (ENXIO);
```

Si ni la correspondencia explícita ni la genérica por clase tienen éxito, el dispositivo no es un UART soportado. Devolver `ENXIO` («Device not configured») indica al kernel que pruebe otros drivers.

El comentario indica un punto de extensión: los drivers para otras familias de UART (Exar, Oxford, Sunix con registros propietarios) añadirían sus comprobaciones aquí antes del `ENXIO` final.

##### Delegación en la lógica de probe genérica

```c
match:
result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
    id->rid & PCI_RID_MASK, 0, 0);
/* Bail out on error. */
if (result > 0)
    return (result);
```

La etiqueta `match` unifica ambas rutas de identificación (tabla explícita y clase genérica). Todo el código posterior opera sobre `id`, que apunta bien a una entrada de la tabla o bien a la estructura `cid`.

**Llamada a la capa genérica**: `uart_bus_probe()` reside en `uart_bus.c` y gestiona la inicialización independiente del bus:

- Asigna y mapea el recurso I/O (el BAR indicado por `id->rid`)
- Configura el acceso a registros usando `id->regshft`
- Establece el reloj de referencia a `id->rclk` (o el valor predeterminado si es cero)
- Sondea el hardware para verificar la presencia del UART e identificar la profundidad de la FIFO
- Establece la dirección base de registro

Los parámetros adicionales (tres ceros) especifican:

- Indicadores que controlan el comportamiento del probe
- Pista del número de unidad del dispositivo (0 = asignación automática)
- Reservado para uso futuro

**Gestión de errores**: Si `uart_bus_probe` devuelve un valor positivo (error), ese valor se propaga al llamador. Los errores habituales son:

- `ENOMEM` - no se pudieron asignar recursos
- `ENXIO` - los registros no responden correctamente (no es un UART o está deshabilitado)
- `EIO` - fallos de acceso al hardware

Un probe exitoso devuelve cero o un valor de prioridad negativo.

##### Asociación con el dispositivo de consola

```c
if (sc->sc_sysdev == NULL)
    uart_pci_unique_console_match(dev);
```

Tras un probe genérico exitoso, el driver intenta la asociación con la consola. La comprobación `sc->sc_sysdev == NULL` garantiza que esto solo se ejecuta si `uart_bus_probe` no identificó ya el dispositivo como consola (lo que podría haber hecho mediante la comparación de la dirección base).

La asociación con la consola es oportunista; si falla, el dispositivo sigue pudiendo conectarse al sistema, simplemente este UART no recibirá mensajes del kernel ni actuará como prompt de inicio de sesión.

##### Establecimiento de la descripción del dispositivo

```c
/* Set/override the device description. */
if (id->desc)
    device_set_desc(dev, id->desc);
return (result);
```

La descripción del dispositivo aparece en los mensajes de arranque, en la salida de `dmesg` y en la de `pciconf -lv`. Ayuda a los administradores a identificar el hardware: «Intel AMT - SOL» resulta más significativo que «PCI device 8086:108f».

Para los dispositivos identificados de forma explícita, `id->desc` contiene la cadena especificada en la tabla. Para los dispositivos genéricos, es «Generic SimpleComm PCI device». La descripción se establece incondicionalmente si existe; incluso si un probe genérico ya había establecido una, el driver específico de PCI la sobrescribe con información más precisa.

Por último, la función devuelve el resultado de `uart_bus_probe`, que el kernel utiliza para seleccionar entre los drivers que compiten por el dispositivo. Para los UART, suele ser `BUS_PROBE_DEFAULT` (-20), la prioridad estándar para los drivers del sistema operativo base, ya que los drivers NS8250 son los únicos que reclaman estos dispositivos.

##### Prioridad del probe y selección del driver

El mecanismo de prioridad del probe gestiona el hardware reclamado por varios drivers. Considera una tarjeta multifunción con puertos serie e interfaces de red:
- `uart_pci` podría probarlo (coincide con la clase PCI, devolviendo `BUS_PROBE_DEFAULT` = -20)
- Un driver específico del fabricante también podría probarlo (coincidiendo exactamente con el ID de fabricante/dispositivo)

El driver del fabricante debería devolver un valor más alto (más cercano a cero), como `BUS_PROBE_VENDOR` (-10) o `BUS_PROBE_SPECIFIC` (0), y Newbus lo seleccionará porque su prioridad es **mayor** que `BUS_PROBE_DEFAULT`. Recuerda: gana el que está más cerca de cero.

Para la mayoría del hardware serie, solo `uart_pci` realiza el probe con éxito, por lo que la prioridad carece de relevancia. Pero el mecanismo permite la coexistencia armoniosa con drivers especializados.

##### El flujo completo del probe

```html
PCI bus discovers device
     -> 
Calls uart_pci_probe(dev)
     -> 
Check explicit table  ->  uart_pci_match()
     ->  (if matched)
Set NS8250 class, jump to match label
     -> 
Check PCI class codes
     ->  (if generic UART)
Use default config, jump to match label
     ->  (if neither)
Return ENXIO (not my device)

match:
     -> 
Call uart_bus_probe() for generic init
     ->  (on error)
Return error code
     ->  (on success)
Attempt console matching (if needed)
     -> 
Set device description
     -> 
Return success (0 or priority)
```

Tras un probe exitoso, el kernel registra este driver como gestor del dispositivo y, posteriormente, llamará a `uart_pci_attach` para completar la inicialización.

#### 7) `attach`: preferir **MSI de vector único** y delegar en el núcleo UART

```c
327: static int
328: uart_pci_attach(device_t dev)
329: {
330: 	struct uart_softc *sc;
331: 	const struct pci_id *id;
332: 	int count;
333: 
334: 	sc = device_get_softc(dev);
335: 
336: 	/*
337: 	 * Use MSI in preference to legacy IRQ if available. However, experience
338: 	 * suggests this is only reliable when one MSI vector is advertised.
339: 	 */
340: 	id = uart_pci_match(dev, pci_ns8250_ids);
341: 	if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
342: 	    pci_msi_count(dev) == 1) {
343: 		count = 1;
344: 		if (pci_alloc_msi(dev, &count) == 0) {
345: 			sc->sc_irid = 1;
346: 			device_printf(dev, "Using %d MSI message\n", count);
347: 		}
348: 	}
349: 
350: 	return (uart_bus_attach(dev));
351: }
```

*Política específica del bus de menor envergadura (preferir MSI de 1 vector) y, a continuación, **delegar** en `uart_bus_attach()`.*

##### Función de attach del dispositivo: `uart_pci_attach`

La función attach se invoca tras un probe exitoso para poner el dispositivo en funcionamiento. Mientras que probe simplemente identifica el dispositivo y verifica la compatibilidad, attach asigna recursos, configura el hardware e integra el dispositivo en el sistema. En el caso de uart_pci, attach se centra en un aspecto específico de PCI, la configuración de interrupciones, antes de delegar en el código de inicialización UART genérico.

##### Entrada a la función y contexto

```c
static int
uart_pci_attach(device_t dev)
{
    struct uart_softc *sc;
    const struct pci_id *id;
    int count;

    sc = device_get_softc(dev);
```

La función attach recibe el mismo `device_t` que se pasó al probe. El softc recuperado aquí contiene la inicialización realizada durante el probe: la asignación de clase UART, la configuración de la dirección base y cualquier asociación con la consola.

A diferencia del probe (que debe ser idempotente y no destructivo), attach puede modificar el estado del dispositivo, asignar recursos y fallar de forma destructiva. Si attach falla, el dispositivo queda inaccesible y normalmente se necesita un reinicio o intervención manual para recuperarlo.

##### Interrupciones señalizadas por mensaje: contexto

Las interrupciones PCI tradicionales utilizan líneas de señal físicas dedicadas (INTx: INTA#, INTB#, INTC#, INTD#) compartidas entre varios dispositivos. Este uso compartido provoca varios problemas:

- Tormentas de interrupciones cuando los dispositivos no reconocen correctamente las interrupciones
- Latencia al iterar por los gestores hasta encontrar el dispositivo que generó la interrupción
- Flexibilidad de enrutamiento limitada en sistemas complejos

Las interrupciones señalizadas por mensaje (MSI) reemplazan las señales físicas por escrituras en memoria en direcciones especiales. Cuando un dispositivo necesita ser atendido, escribe en una dirección específica de la CPU, lo que dispara una interrupción en ese procesador. Ventajas de MSI:

- Sin uso compartido, cada dispositivo obtiene vectores de interrupción dedicados
- Menor latencia, direccionamiento directo a la CPU
- Mejor escalabilidad, miles de vectores disponibles frente a cuatro líneas INTx

Sin embargo, la calidad de implementación de MSI varía, en particular en los UART (dispositivos sencillos que a menudo reciben una validación mínima). Algunas implementaciones MSI de UART sufren pérdidas de interrupciones, interrupciones espurias o bloqueos del sistema.

##### Verificación de elegibilidad para MSI

```c
/*
 * Use MSI in preference to legacy IRQ if available. However, experience
 * suggests this is only reliable when one MSI vector is advertised.
 */
id = uart_pci_match(dev, pci_ns8250_ids);
if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
    pci_msi_count(dev) == 1) {
```

El driver intenta la asignación de MSI solo cuando se cumplen tres condiciones:

**Dispositivo no en la tabla O MSI no deshabilitado explícitamente**: La condición `(id == NULL || (id->rid & PCI_NO_MSI) == 0)` se evalúa como verdadera en dos casos:

1. `id == NULL`: el dispositivo coincidió mediante códigos de clase genéricos, no mediante una entrada explícita en la tabla (sin peculiaridades conocidas)
2. `(id->rid & PCI_NO_MSI) == 0`: el dispositivo está en la tabla, pero el indicador de MSI está a cero (se sabe que MSI funciona)

Si el dispositivo tiene `PCI_NO_MSI` establecido en su entrada de tabla, esta condición falla y se omite por completo la asignación de MSI. En su lugar, se utilizarán las interrupciones heredadas basadas en línea.

**Un único vector MSI anunciado**: `pci_msi_count(dev) == 1` consulta la estructura de capacidades MSI del dispositivo para determinar cuántos vectores de interrupción admite. Los UART solo necesitan una interrupción (eventos serie: carácter recibido, buffer de transmisión vacío, cambio de estado del módem), por lo que el soporte de múltiples vectores no es necesario.

El comentario recoge la experiencia acumulada con dificultad: los dispositivos que anuncian múltiples vectores MSI (aunque solo utilicen uno) suelen tener implementaciones defectuosas. Restringir la asignación a dispositivos de vector único evita estos problemas. Un dispositivo que anuncia ocho vectores para un UART sencillo probablemente recibió pruebas de MSI mínimas.

##### Asignación de MSI

```c
count = 1;
if (pci_alloc_msi(dev, &count) == 0) {
    sc->sc_irid = 1;
    device_printf(dev, "Using %d MSI message\n", count);
}
```

**Solicitud de asignación**: `pci_alloc_msi(dev, &count)` solicita al subsistema PCI que asigne vectores MSI para este dispositivo. El parámetro `count` actúa como entrada y como salida:
- Entrada: número de vectores solicitados (1)
- Salida: número de vectores asignados realmente (puede ser menor si los recursos están agotados)

La función devuelve cero en caso de éxito y un valor distinto de cero en caso de fallo. Los motivos de fallo incluyen:
- El sistema no admite MSI (chipsets antiguos, deshabilitado en la BIOS)
- Recursos MSI agotados (demasiados dispositivos ya usan MSI)
- La estructura de capacidades MSI del dispositivo está malformada

**Registro del ID de recurso de interrupción**: Tras una asignación exitosa, `sc->sc_irid = 1` registra que se usará el ID de recurso de interrupción 1. El significado:
- RID 0 representa habitualmente la interrupción INTx heredada
- RID 1 y superiores representan vectores MSI
- El código de attach UART genérico asignará el recurso de interrupción usando este RID

Sin esta asignación, se utilizaría el RID por defecto (0), lo que provocaría que el driver asignase la interrupción heredada en lugar del vector MSI recién asignado.

**Notificación al usuario**: `device_printf` registra la asignación de MSI en la consola y en el buffer de mensajes del sistema. Esta información ayuda a los administradores a depurar problemas relacionados con las interrupciones. La salida tiene esta forma:

```yaml
uart0: <Intel AMT - SOL> port 0xf0e0-0xf0e7 mem 0xfebff000-0xfebff0ff irq 16 at device 22.0 on pci0
uart0: Using 1 MSI message
```

**Retroceso silencioso**: Si `pci_alloc_msi` falla, el cuerpo condicional no se ejecuta. El campo `sc->sc_irid` permanece en su valor por defecto (0) y no se imprime ningún mensaje. La función attach continúa con la inicialización genérica, que asignará la interrupción heredada. Este retroceso silencioso garantiza el funcionamiento del dispositivo incluso cuando MSI no está disponible, ya que las interrupciones heredadas funcionan de forma universal.

##### Delegación en el attach genérico

```c
return (uart_bus_attach(dev));
```

Tras la configuración de interrupciones específica de PCI, la función llama a `uart_bus_attach()` para completar la inicialización. Esta función genérica (compartida entre todos los tipos de bus: PCI, ISA, ACPI, USB) realiza:

**Asignación de recursos**:
- Puertos de I/O o registros mapeados en memoria (ya mapeados durante el probe)
- Recurso de interrupción (usando `sc->sc_irid` para seleccionar MSI o heredado)
- Posiblemente recursos DMA (no utilizados por la mayoría de los UART)

**Inicialización del hardware**:
- Resetear el UART
- Configurar los parámetros por defecto (8 bits de datos, sin paridad, 1 bit de parada)
- Habilitar el FIFO y ajustar su tamaño
- Configurar las señales de control del módem

**Creación del dispositivo de caracteres**:
- Asignar estructuras TTY
- Crear nodos de dispositivo (`/dev/cuaU0`, `/dev/ttyU0`)
- Registrarse en la capa TTY para el soporte de disciplina de línea

**Integración con la consola**:
- Si `sc->sc_sysdev` está establecido, configurar como consola del sistema
- Habilitar la salida de consola a través de este UART
- Gestionar la entrada al depurador del kernel mediante señales de break

**Propagación del valor de retorno**: El valor de retorno de `uart_bus_attach()` se pasa directamente al kernel. El éxito (0) indica que el dispositivo está operativo; los errores (valores errno positivos) indican fallo.

##### Gestión del fallo en attach

Si `uart_bus_attach()` falla, el dispositivo permanece inutilizable. El subsistema PCI registra el fallo y no llamará a los métodos del dispositivo (read, write, ioctl) en esta instancia. Sin embargo, los recursos ya asignados por attach (como los vectores MSI) pueden perderse si no se llama a la función detach del driver.

La gestión adecuada de errores en el código de attach genérico garantiza:
- Una asignación de interrupción fallida desencadena la limpieza de recursos
- La inicialización parcial se revierte
- El dispositivo permanece en un estado seguro para reintentarlo o eliminarlo

##### El flujo completo del attach

```html
Kernel calls uart_pci_attach(dev)
     -> 
Check MSI eligibility
    | ->  Device has PCI_NO_MSI flag  ->  skip MSI
    | ->  Device advertises multiple vectors  ->  skip MSI
    | ->  Device advertises one vector  ->  attempt MSI
         -> 
    Allocate MSI vector via pci_alloc_msi()
        | ->  Success: set sc->sc_irid = 1, log message
        | ->  Failure: silent, sc->sc_irid remains 0
     -> 
Call uart_bus_attach(dev)
     -> 
Generic code allocates interrupt using sc->sc_irid
    | ->  RID 1: MSI vector
    | ->  RID 0: legacy INTx
     -> 
Complete UART initialization
     -> 
Create device nodes (/dev/cuaU*, /dev/ttyU*)
     -> 
Return success/failure
```

Tras un attach exitoso, el UART está completamente operativo. Las aplicaciones pueden abrir `/dev/cuaU0` para la comunicación serie, los mensajes del kernel fluyen hacia la consola (si está configurado) y las operaciones de I/O basadas en interrupciones gestionan la transmisión y recepción de caracteres.

##### Simplicidad arquitectónica

La brevedad de la función attach, veintitrés líneas incluidos los comentarios, demuestra la potencia de la arquitectura por capas. Los aspectos específicos de PCI (la asignación de MSI) se gestionan aquí con un código mínimo, mientras que la compleja inicialización del UART reside en la capa genérica, donde se comparte entre todos los tipos de bus.

Esta separación implica:

- Los UART conectados por ISA omiten la lógica de MSI pero reutilizan toda la inicialización de UART
- Los UART conectados por ACPI pueden gestionar la administración de energía de forma diferente, pero comparten la creación del dispositivo de caracteres
- Los adaptadores serie USB utilizan un mecanismo de entrega de interrupciones completamente diferente, pero comparten la integración TTY

El driver uart_pci es un fino enlace que conecta la gestión de recursos PCI con la funcionalidad UART genérica, exactamente como se diseñó.

#### 8) `detach` y registro del módulo

```c
353: static int
354: uart_pci_detach(device_t dev)
355: {
356: 	struct uart_softc *sc;
357: 
358: 	sc = device_get_softc(dev);
359: 
360: 	if (sc->sc_irid != 0)
361: 		pci_release_msi(dev);
362: 
363: 	return (uart_bus_detach(dev));
364: }
366: DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

*Liberar MSI si se asignó, y dejar que el núcleo UART deshaga los cambios. Por último, registrar este driver en el bus **`pci`**.*

##### Función de detach del dispositivo y registro del driver

La función detach se invoca cuando un dispositivo debe retirarse del sistema, ya sea por desconexión en caliente, descarga del driver o apagado del sistema. Debe revertir todas las operaciones realizadas durante attach, liberando recursos y asegurando que el hardware quede en un estado seguro. El macro `DRIVER_MODULE` final registra el driver en el marco de dispositivos del kernel.

##### Función de detach del dispositivo: `uart_pci_detach`

```c
static int
uart_pci_detach(device_t dev)
{
    struct uart_softc *sc;

    sc = device_get_softc(dev);
```

detach recibe el dispositivo que se va a retirar y recupera su softc, que contiene la configuración actual. La función debe estar preparada para gestionar estados de inicialización parcial: si attach falló a medias, detach podría invocarse para limpiar lo que sí se completó.

##### Liberación del recurso MSI

```c
if (sc->sc_irid != 0)
    pci_release_msi(dev);
```

La condición comprueba si se asignó MSI durante attach. Recuerda que `sc->sc_irid = 1` indica una asignación de MSI exitosa; el valor por defecto (0) indica que se usaron interrupciones heredadas.

**Liberación de vectores MSI**: `pci_release_msi(dev)` devuelve el vector de interrupción MSI al pool del sistema, poniéndolo a disposición de otros dispositivos. Esta llamada debe realizarse antes del detach genérico, que desasignará el propio recurso de interrupción. El orden importa:

1. Libera la asignación de MSI (devuelve el vector al sistema)
2. El detach genérico desasigna el recurso de interrupción (libera las estructuras del kernel)

Invertir este orden provocaría una fuga de vectores MSI; el kernel los consideraría asignados incluso después de que el dispositivo haya desaparecido.

**¿Por qué comprobar `sc_irid`?**: Llamar a `pci_release_msi` cuando MSI no estaba asignado es inofensivo, pero consume ciclos innecesarios. Lo más importante es que documenta la intención del código: "si asignamos MSI durante el attach, liberémoslo durante el detach". Esta simetría facilita la comprensión.

La ausencia de manejo de errores es intencional: `pci_release_msi` no puede fallar de forma significativa durante el detach. El dispositivo se está eliminando de todas formas; si la liberación de MSI fallase (debido a un estado corrupto del kernel), continuar con el detach sigue siendo lo correcto.

##### Delegación al detach genérico

```c
return (uart_bus_detach(dev));
```

Tras la limpieza de recursos específica de PCI, la función llama a `uart_bus_attach()` para gestionar el desmontaje genérico del UART. Esto refleja la secuencia del attach: el código específico de PCI envuelve al código genérico.

**Operaciones del detach genérico**:

**Eliminación del dispositivo de caracteres**: Cierra los descriptores de archivo abiertos, destruye los nodos `/dev/cuaU*` y `/dev/ttyU*`, y cancela el registro en la capa TTY.

**Apagado del hardware**: Deshabilita las interrupciones en el UART, vacía las FIFOs y desactiva las señales de control del módem. Esto evita que el hardware genere interrupciones espurias o afirme líneas de control después de que el driver haya desaparecido.

**Desasignación de recursos**: Libera el recurso de interrupción (la estructura del kernel, no el vector MSI, que ya se liberó antes), desmapea los puertos de I/O o las regiones de memoria, y libera la memoria del kernel asignada.

**Desconexión de la consola**: Si este dispositivo era la consola del sistema, redirige la salida de consola a un dispositivo alternativo o deshabilita completamente la salida de consola. El sistema debe poder arrancar incluso si se elimina el UART de consola.

**Valor de retorno**: `uart_bus_detach()` devuelve cero en caso de éxito o un código de error en caso de fallo. En la práctica, el detach raramente falla; el dispositivo se elimina tanto si la limpieza de software concluye con éxito como si no.

##### Consecuencias de un fallo en el detach

Si el detach devuelve un error, la respuesta del kernel depende del contexto:

**Descarga del driver**: Si se intenta descargar el módulo del driver (`kldunload uart_pci`), la operación falla y el módulo permanece cargado. El dispositivo sigue enlazado, evitando fugas de recursos.

**Extracción en caliente del dispositivo**: Si la extracción física activó el detach (hot-unplug de PCIe), el hardware ya ha desaparecido. El fallo del detach se registra en el log, pero la entrada del árbol de dispositivos se elimina igualmente. Pueden producirse fugas de recursos, pero se preserva la estabilidad del sistema.

**Apagado del sistema**: Durante el apagado, los fallos de detach se ignoran. El sistema se está deteniendo de todas formas, por lo que las fugas de recursos son irrelevantes.

Las funciones de detach bien diseñadas nunca deberían fallar. La implementación de `uart_pci` lo consigue mediante:

- Realizar únicamente operaciones que no pueden fallar (liberación de recursos)
- Delegar la lógica compleja al código genérico, que gestiona los casos extremos
- No requerir respuestas del hardware (que podría estar ya desconectado)

##### Registro del driver: `DRIVER_MODULE`

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

Esta macro registra el driver en el framework de dispositivos de FreeBSD, poniéndolo a disposición para la coincidencia de dispositivos durante el boot y la carga de módulos. La macro se expande en una considerable cantidad de código de infraestructura, pero sus parámetros son sencillos:

**`uart`**: El nombre del driver, que coincide con la cadena en `uart_driver_name`. Este nombre aparece en los mensajes del kernel, en las rutas del árbol de dispositivos y en los comandos de administración. Varios drivers pueden compartir el mismo nombre si se enlazan a buses diferentes: `uart_pci`, `uart_isa` y `uart_acpi` usan todos "uart", diferenciándose por el bus al que se enlazan.

**`pci`**: El nombre del bus padre. Este driver se enlaza al bus PCI, por lo que especifica "pci". El framework de bus del kernel usa esto para determinar cuándo llamar a la función probe del driver; solo se ofrecen dispositivos PCI a `uart_pci`.

**`uart_pci_driver`**: Puntero a la estructura `driver_t` definida anteriormente, que contiene la tabla de métodos y el tamaño del softc. El kernel la usa para invocar los métodos del driver y asignar el estado por dispositivo.

**`NULL, NULL`**: Dos parámetros reservados para hooks de inicialización del módulo. La mayoría de los drivers no los necesitan y pasan NULL en ambos. Los hooks permiten ejecutar código cuando el módulo se carga (antes de cualquier attach de dispositivo) o se descarga (después de que todos los dispositivos hayan hecho el detach). Sus usos incluyen:

- Asignación de recursos globales (pools de memoria, threads de trabajo)
- Registro en subsistemas (como la pila de red)
- Inicialización del hardware que se realiza una sola vez

En el caso de `uart_pci`, no se necesita inicialización a nivel de módulo; todo el trabajo ocurre en probe/attach por dispositivo.

##### El ciclo de vida del módulo

La macro `DRIVER_MODULE` hace que el driver participe en la arquitectura modular del kernel de FreeBSD:

**Compilación estática**: Si se compila dentro del kernel (`options UART` en la configuración del kernel), el driver está disponible en el boot. El enlazador incluye `uart_pci_driver` en la tabla de drivers del kernel, y la enumeración de PCI durante el boot llama a su función probe.

**Carga dinámica**: Si se compila como módulo (`kldload uart_pci.ko`), el cargador de módulos procesa el registro de `DRIVER_MODULE`, añadiendo el driver a la tabla activa. Se vuelve a hacer probe de los dispositivos existentes; las nuevas coincidencias activan el attach.

**Descarga dinámica**: `kldunload uart_pci` intenta hacer el detach de todos los dispositivos gestionados por este driver. Si algún detach falla o los dispositivos están en uso (descriptores de archivo abiertos), la descarga falla y el módulo permanece. Una descarga exitosa elimina el driver de la tabla activa.

##### Relación con otros drivers UART

El subsistema UART de FreeBSD incluye varios drivers específicos de bus que comparten código genérico:

- `uart_pci.c` - UARTs conectados por PCI (este driver)
- `uart_isa.c` - UARTs de bus ISA (puertos COM heredados)
- `uart_acpi.c` - UARTs enumerados por ACPI (portátiles y servidores modernos)
- `uart_fdt.c` - UARTs de Flattened Device Tree (sistemas embebidos, ARM)

Cada uno usa `DRIVER_MODULE` para registrarse en su bus correspondiente:

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);   // PCI bus
DRIVER_MODULE(uart, isa, uart_isa_driver, NULL, NULL);   // ISA bus
DRIVER_MODULE(uart, acpi, uart_acpi_driver, NULL, NULL); // ACPI bus
```

Todos comparten el nombre "uart" pero se enlazan a buses distintos. Un sistema podría cargar los cuatro módulos simultáneamente, con cada uno gestionando los UARTs descubiertos en su bus. Un equipo de escritorio podría tener:
- Dos puertos ISA COM (COM1/COM2 mediante `uart_isa`)
- Un controlador de gestión PCI (IPMI mediante `uart_pci`)
- Cero UARTs ACPI (no presentes)

Cada dispositivo obtiene una instancia de driver independiente, todas compartiendo el código UART genérico en `uart_bus.c` y `uart_core.c`.

##### Estructura completa del driver

Con todas las piezas explicadas, la estructura completa del driver es:

```text
uart_pci_methods[] ->  Method table (probe/attach/detach/resume)
      
uart_pci_driver ->  Driver declaration (name, methods, softc size)
      
DRIVER_MODULE() ->  Registration (uart, pci, uart_pci_driver)
```

En tiempo de ejecución, el driver del bus PCI descubre los dispositivos y consulta la tabla de drivers registrados. Para cada dispositivo, llama a las funciones probe de los drivers que coinciden. La función probe de `uart_pci` examina los IDs del dispositivo comparándolos con su tabla y devuelve éxito para las coincidencias. El kernel llama entonces a attach para inicializar el dispositivo. Más tarde, detach realiza la limpieza cuando el dispositivo se elimina.

Esta arquitectura, tablas de métodos, inicialización en capas, lógica del núcleo independiente del bus, se repite en todo el framework de drivers de dispositivos de FreeBSD. Comprenderla en el contexto de `uart_pci` te prepara para drivers más complejos: tarjetas de red, controladores de almacenamiento y adaptadores gráficos siguen patrones similares a mayor escala.

#### Ejercicios interactivos para `uart(4)`

**Objetivo:** Consolidar el patrón del driver PCI: tablas de identificación de dispositivos -> probe -> attach -> núcleo genérico, con MSI como variación específica del bus.

##### A) Esqueleto del driver y registro

1. Señala el array `device_method_t` y la estructura `driver_t`. Para cada uno, identifica qué declara y cómo se relacionan entre sí. Cita las líneas relevantes. ¿Qué campo de `driver_t` apunta a la tabla de métodos? *Pista:* busca `uart_pci_methods[]` y la definición de `uart_pci_driver` cerca del inicio del archivo.

2. ¿Dónde está la macro `DRIVER_MODULE` y a qué bus apunta? ¿Cuáles son los cinco parámetros que recibe? Cítala y explica cada parámetro. *Pista:* `DRIVER_MODULE(uart, pci, ...)` está al final del archivo.

##### B) Identificación y coincidencia de dispositivos

1. En la tabla `pci_ns8250_ids[]`, encuentra al menos dos entradas de Intel (vendedor 0x8086) que demuestren un manejo especial: una con la marca `PCI_NO_MSI` y otra con una frecuencia de reloj no estándar (`rclk`). Cita ambas entradas completas y explica qué significa cada parámetro especial para el hardware. *Pista:* busca `0x8086` en la tabla y fíjate en las filas cercanas a los HSUART de Atom y ValleyView.

2. En `uart_pci_match()`, sigue la lógica de coincidencia en dos fases. ¿Dónde coincide el primer bucle con los IDs primarios (vendedor/dispositivo)? ¿Dónde lo hace el segundo con los IDs de subsistema? ¿Qué ocurre si una entrada tiene `subven == 0xffff`? Cita las líneas relevantes (3-5 líneas en total). *Pista:* trabaja con los dos bucles `for` de `uart_pci_match` y presta atención a la comprobación del comodín `subven == 0xffff`.

3. Encuentra un ejemplo en `pci_ns8250_ids[]` en el que el mismo par vendedor/dispositivo aparezca varias veces con distintos IDs de subsistema. Cita 2-3 entradas consecutivas y explica por qué existe esta duplicación. *Pista:* el bloque de HP Diva (vendedor 0x103c, dispositivo 0x1048) y el bloque Timedia 0x1409/0x7168 en `pci_ns8250_ids`.

##### C) Flujo de probe

1. En `uart_pci_probe()`, muestra dónde el código asigna `sc->sc_class` a `&uart_ns8250_class` tras una coincidencia exitosa en la tabla, y dónde llama a continuación a `uart_bus_probe()`. Cita ambos fragmentos (2-3 líneas cada uno). *Pista:* la asignación de la clase está en la ruta de éxito, después de `uart_pci_match`, y la llamada a `uart_bus_probe` es el paso final antes de que `uart_pci_probe` devuelva el resultado.

2. ¿Qué hace `uart_pci_unique_console_match()` cuando encuentra un dispositivo único que coincide con una consola? Cita la asignación a `sc->sc_sysdev` y la línea de sincronización de `rclk`. ¿Por qué es necesaria la sincronización del reloj? *Pista:* céntrate en el final de `uart_pci_unique_console_match`, donde se asigna `sc->sc_sysdev` y `sc->sc_sysdev->bas.rclk` se copia en `sc->sc_bas.rclk`.

3. En `uart_pci_probe()`, explica la ruta de reserva para dispositivos "Generic SimpleComm". ¿Qué valores de clase, subclase y progif de PCI activan esta ruta? ¿Por qué el comentario dice "XXX rclk what to do"? Cita la comprobación condicional e indica qué configuración se usa. *Pista:* busca la estructura local `cid` al inicio de `uart_pci_probe` y la comprobación de `pci_get_class/subclass/progif` más adelante.

##### D) Attach y detach

1. En `uart_pci_attach()`, ¿por qué la función vuelve a hacer coincidir el dispositivo con la tabla de IDs si probe ya lo hizo? Cita la línea. *Pista:* busca la llamada a `uart_pci_match` cerca del inicio de `uart_pci_attach`.

2. Cita la condicional exacta que comprueba la elegibilidad para MSI (debe preferir MSI de vector único) y la llamada que lo asigna. ¿Qué ocurre si la asignación de MSI falla? Cita 5-7 líneas. *Pista:* el bloque `pci_msi_count`/`pci_alloc_msi` está justo después de la llamada a `uart_pci_match` en `uart_pci_attach`.

3. En `uart_pci_detach()`, cita las dos operaciones críticas: la liberación de MSI y la delegación al detach genérico. ¿Por qué debe liberarse MSI antes de llamar a `uart_bus_detach()`? Explica la dependencia de orden. *Pista:* tanto la llamada a `pci_release_msi` como la llamada a `uart_bus_detach` aparecen en secuencia dentro de `uart_pci_detach`.

##### E) Integración: trazando el flujo completo

1. Partiendo del boot, traza cómo un Dell RAC 4 (vendedor 0x1028, dispositivo 0x0012) se convierte en `/dev/cuaU0`. Para cada paso, cita la línea relevante:

- ¿Qué entrada de la tabla coincide?
- ¿Qué frecuencia de reloj especifica?
- ¿Qué ocurre en probe? (¿qué clase se asigna? ¿qué función se llama?)
- ¿Qué ocurre en attach? (¿usará MSI?)
- ¿Qué función genérica crea el nodo del dispositivo?

2. Un dispositivo tiene vendedor 0x8086, dispositivo 0xa13d (100 Series Chipset KT). ¿Usará MSI? Sigue la lógica:

- Encuentra y cita la entrada de la tabla
- Comprueba el campo `rid`: ¿qué marca está presente?
- Cita el condicional en `uart_pci_attach()` que comprueba esta marca
- ¿Qué mecanismo de interrupción se usará en su lugar?

##### F) Arquitectura y patrones de diseño

1. Compara `if_tuntap.c` (de la sección anterior) con `uart_bus_pci.c`:

- if_tuntap tenía ~2200 líneas; uart_bus_pci tiene ~370. ¿Por qué tanta diferencia de tamaño?
- if_tuntap contenía la lógica completa del dispositivo; uart_bus_pci es principalmente código de pegamento. ¿Dónde ocurre el acceso real a los registros UART, la configuración de la tasa de baudios y la integración con TTY? (Pista: ¿a qué función llama attach?)
- ¿Qué enfoque de diseño, monolítico como if_tuntap o en capas como uart_bus_pci, facilita más el soporte del mismo hardware en múltiples buses (PCI, ISA, USB)?

2. Imagina que necesitas añadir soporte para:

- Un nuevo UART PCI: fabricante 0xABCD, dispositivo 0x1234, reloj estándar, BAR 0x10
- Una versión conectada por ISA del mismo chipset UART

	Para la variante PCI, ¿qué modificarías en `uart_bus_pci.c`? (Cita la estructura y la ubicación)
	Para la variante ISA, ¿modificarías `uart_bus_pci.c` o trabajarías en un archivo diferente?
	¿Cuántas líneas de código de acceso a registros UART necesitarías escribir/duplicar?

#### Ejercicios de extensión (experimentos mentales)

Examina la lógica de asignación de MSI en `uart_pci_attach()`.

El comentario dice «la experiencia sugiere que esto solo es fiable cuando se anuncia un único vector MSI».

1. ¿Por qué razón un UART simple (que solo necesita una interrupción) podría anunciar múltiples vectores MSI?
2. ¿Qué problemas podrían surgir con MSI multivector que el driver evita comprobando `pci_msi_count(dev) == 1`?
3. Si la asignación de MSI falla silenciosamente (la condición `if` es falsa), el driver continúa. ¿Dónde en el código genérico de attach se asignará entonces el recurso de interrupción? ¿Qué tipo de interrupción se usará?

#### Por qué esto importa en tu capítulo de «anatomía»

Acabas de recorrer un driver de **pegamento PCI mínimo** de principio a fin. **Identifica** dispositivos, elige una **clase** UART, llama a una **probe/attach compartidos** en el núcleo del subsistema y añade algo de política PCI ligera (MSI/consola). Esta es la misma estructura que reutilizarás para otros buses: **match  ->  probe  ->  attach  ->  core**, más **recursos/IRQs** y un **detach limpio**. Ten este patrón en mente cuando pases de pseudo-dispositivos a **hardware real** en capítulos posteriores.

## De cuatro drivers a un modelo mental único

Ya has recorrido cuatro drivers completos, cada uno ilustrando distintos aspectos de la arquitectura de drivers de dispositivo de FreeBSD. No eran ejemplos arbitrarios; forman una progresión deliberada que revela los patrones subyacentes a todos los drivers del kernel.

### La progresión que has completado

**Recorrido 1: `/dev/null`, `/dev/zero`, `/dev/full`** (null.c)

- Los dispositivos de caracteres más sencillos posibles
- Creación estática del dispositivo durante la carga del módulo
- Operaciones triviales: descartar escrituras, devolver ceros, simular errores
- Sin estado por dispositivo, sin temporizadores, sin complejidad
- **Lección clave**: La tabla de despacho de funciones `cdevsw` y la E/S básica con `uiomove()`

**Recorrido 2: subsistema LED** (led.c)

- Creación dinámica de dispositivos bajo demanda
- Subsistema que proporciona tanto interfaz al espacio de usuario como API del kernel
- Máquina de estados controlada por temporizador para la ejecución de patrones
- DSL de análisis sintáctico de patrones que convierte comandos de usuario en códigos internos
- **Lección clave**: Dispositivos con estado, drivers de infraestructura, separación de locks (mtx frente a sx)

**Recorrido 3: túneles de red TUN/TAP** (if_tuntap.c)

- Dispositivo de caracteres e interfaz de red combinados
- Flujo de datos bidireccional: intercambio de paquetes kernel <-> espacio de usuario
- Integración con la pila de red (ifnet, BPF, enrutamiento)
- E/S bloqueante con wakeups adecuados (soporte para poll/select/kqueue)
- **Lección clave**: Integración compleja que une dos subsistemas del kernel

**Recorrido 4: driver PCI UART** (uart_bus_pci.c)

- Conexión al bus hardware (enumeración PCI)
- Arquitectura en capas: pegamento de bus delgado + núcleo genérico robusto
- Identificación del dispositivo mediante tablas de IDs de fabricante y dispositivo
- Gestión de recursos (BARs, interrupciones, MSI)
- **Lección clave**: El ciclo de vida probe-attach-detach, reutilización de código a través del diseño en capas

### Patrones que han emergido

A medida que has avanzado por estos cuatro drivers, ciertos patrones han aparecido de forma recurrente:

#### 1. El patrón del dispositivo de caracteres

Todo dispositivo de caracteres sigue la misma estructura, ya sea `/dev/null` o `/dev/tun0`:

- Una estructura `cdevsw` que mapea las llamadas al sistema a funciones
- `make_dev()` crea la entrada en `/dev`
- `si_drv1` enlaza el nodo de dispositivo con el estado por dispositivo
- `destroy_dev()` limpia todo al retirar el dispositivo

La complejidad varía: null.c no tiene estado, led.c rastrea patrones, tuntap gestiona la interfaz de red, pero el esqueleto es idéntico.

#### 2. El patrón de dispositivo dinámico frente al estático

null.c crea tres dispositivos fijos en el momento de carga del módulo. led.c y tuntap crean dispositivos bajo demanda a medida que el hardware se registra o los usuarios abren nodos de dispositivo. Esta flexibilidad conlleva mayor complejidad:

- Asignación de números de unidad (unrhdr)
- Registros globales (listas enlazadas)
- Locking más sofisticado

#### 3. El patrón de la API de subsistema

led.c demuestra el diseño de infraestructura: es a la vez un driver de dispositivo (que expone `/dev/led/*`) y un proveedor de servicios (que exporta `led_create()` para otros drivers). Este doble papel aparece en todos los drivers de FreeBSD que actúan como bibliotecas para otros drivers.

#### 4. El patrón de la arquitectura en capas

uart_bus_pci.c es mínimo porque la mayor parte de la lógica reside en uart_bus.c. El patrón:

- El código específico del bus se ocupa de: identificación del dispositivo, reclamación de recursos, configuración de interrupciones
- El código genérico se ocupa de: inicialización del dispositivo, implementación del protocolo, interfaz de usuario

Esta separación significa que la misma lógica UART funciona en plataformas PCI, ISA, USB y device-tree.

#### 5. Los patrones de movimiento de datos

Has visto tres enfoques para transferir datos:

- **Simple**: null_write establece `uio_resid = 0` y retorna (descarta los datos)
- **Con buffer**: zero_read itera llamando a `uiomove()` desde un buffer del kernel
- **Zero-copy**: tuntap usa mbufs para el manejo eficiente de paquetes

#### 6. Los patrones de sincronización

El locking de cada driver refleja sus necesidades:

- null.c: ninguno (dispositivos sin estado)
- led.c: dos locks (mtx para el estado rápido, sx para los cambios de estructura lentos)
- tuntap: mutex por dispositivo que protege las colas y el estado de ifnet
- uart_pci: mínimo (la mayor parte del locking está en la capa genérica uart_bus)

#### 7. Los patrones de ciclo de vida

Todos los drivers siguen el esquema crear-operar-destruir, aunque con variaciones:

- **Ciclo de vida del módulo**: los eventos `MOD_LOAD`/`MOD_UNLOAD` de null.c
- **Ciclo de vida dinámico**: la API `led_create()`/`led_destroy()` de led.c
- **Ciclo de vida por clonación**: la creación bajo demanda de dispositivos en tuntap
- **Ciclo de vida hardware**: la secuencia probe-attach-detach de uart_pci

### Lo que ya eres capaz de reconocer

Tras estos cuatro recorridos, cuando te encuentres con cualquier driver de FreeBSD, deberías identificar de inmediato:

**¿Qué tipo de driver es este?**

- ¿Solo dispositivo de caracteres? (como null.c)
- ¿Infraestructura/subsistema? (como led.c)
- ¿Dispositivo y red combinados? (como tuntap)
- ¿Conexión a bus hardware? (como uart_pci)

**¿Dónde está el estado?**

- ¿Solo global? (lista global y temporizador de led.c)
- ¿Por dispositivo? (softc de tuntap con colas e ifnet)
- ¿Dividido? (estado mínimo de uart_pci + estado rico de uart_bus)

**¿Cómo está protegido por locks?**

- ¿Un mutex para todo?
- ¿Múltiples locks para distintos datos o patrones de acceso?
- ¿Delegado al código genérico?

**¿Cuál es el camino de los datos?**

- ¿Copia con `uiomove()`?
- ¿Uso de mbufs?
- ¿Técnicas de zero-copy?

**¿Cuál es el ciclo de vida?**

- ¿Fijo (creado una sola vez al cargarse)?
- ¿Dinámico (creado bajo demanda)?
- ¿Controlado por hardware (aparece y desaparece con los dispositivos físicos)?

### El esquema por delante

El documento que sigue destila estos patrones en una guía de referencia rápida: una colección de listas de comprobación y plantillas que puedes usar al escribir o analizar drivers. Está organizado por punto de integración (dispositivo de caracteres, interfaz de red, conexión a bus) y captura las decisiones críticas y las invariantes que debes mantener.

Piensa en los cuatro drivers que has estudiado como ejemplos resueltos, y en el esquema como los principios extraídos de ellos. Juntos, forman tu base para entender la arquitectura de drivers de FreeBSD. Los drivers te mostraron *cómo* funcionan las cosas en contexto; el esquema te recuerda *qué* debes hacer para que tus propios drivers funcionen correctamente.

Cuando estés listo para escribir tu propio driver o modificar uno existente, empieza por las preguntas de autocomprobación del esquema. Luego vuelve al recorrido apropiado (null.c para dispositivos básicos, led.c para temporizadores y APIs, tuntap para redes, uart_pci para hardware) para ver esos patrones en implementaciones completas.

Ahora estás equipado para moverte por los drivers de dispositivo del kernel, no como cajas negras intimidantes, sino como variaciones sobre patrones que has interiorizado mediante el estudio práctico.

## Esquema de anatomía de un driver (FreeBSD 14.3)

Este es tu mapa de referencia rápida para los drivers de FreeBSD. Captura la forma (las partes móviles y dónde viven), el contrato (lo que el kernel espera de ti) y los escollos (lo que falla bajo carga). Úsalo como lista de comprobación antes y después de programar.

### Esqueleto básico: lo que todo driver necesita

**Identifica tu punto de integración:**

**Dispositivo de caracteres (devfs)**  ->  `struct cdevsw` + `make_dev*()`/`destroy_dev()`

- Puntos de entrada: open/read/write/ioctl/poll/kqfilter
- Ejemplo: null.c, led.c

**Interfaz de red (ifnet)**  ->  `if_alloc()`/`if_attach()`/`if_free()` + cdev opcional

- Callbacks: `if_transmit` o `if_start`, entrada mediante `netisr_dispatch()`
- Ejemplo: if_tuntap.c

**Conexión a bus (p. ej., PCI)**  ->  `device_method_t[]` + `driver_t` + `DRIVER_MODULE()`

- Ciclo de vida: probe/attach/detach (+ suspend/resume si es necesario)
- Ejemplo: uart_bus_pci.c

**Invariantes mínimas (memoriza estas):**

- Todo objeto que crees (cdev, ifnet, callout, taskqueue, recurso) tiene una destrucción o liberación simétrica en los caminos de error y durante detach/unload
- La concurrencia es explícita: si accedes al estado desde múltiples contextos (camino de syscall, timeout, rx/tx, interrupción), mantienes el lock adecuado o diseñas sin locks con reglas estrictas
- La limpieza de recursos debe ocurrir en orden inverso a la asignación

### Esquema del dispositivo de caracteres

**Forma:**

- `static struct cdevsw` solo con lo que implementas; deja el resto como `nullop` u omítelo
- El módulo o el hook de inicialización crea los nodos: `make_dev_credf()`/`make_dev_s()`
- Guarda un `struct cdev *` para destruirlo después

**Puntos de entrada:**

**read**: Itera mientras `uio->uio_resid > 0`; mueve bytes con `uiomove()`; retorna antes si hay error

- Ejemplo: zero_read itera copiando desde un buffer del kernel preinicializado a cero

**write**: O bien consume (`uio_resid = 0; return 0;`) o falla (`return ENOSPC/EIO/...`)

- Sin escrituras parciales a menos que lo desees explícitamente
- Ejemplo: null_write consume todo; full_write siempre falla

**ioctl**: Un `switch(cmd)` pequeño; devuelve 0, un errno específico o `ENOIOCTL`

- Maneja los ioctls estándar de terminal (`FIONBIO`, `FIOASYNC`) aunque sean no-ops
- Ejemplo: null_ioctl gestiona la configuración de volcado del kernel

**poll/kqueue (opcional)**: Conecta la disponibilidad y las notificaciones si el espacio de usuario bloquea

- Ejemplo: el poll de tuntap comprueba la cola y se registra mediante `selrecord()`

**Concurrencia y temporizadores:**

- Si tienes trabajo periódico (p. ej., parpadeo de LED), usa un callout ligado al mutex adecuado
- Arma y rearma con cuidado; deténlo en el teardown cuando el último usuario desaparezca
- Ejemplo: `callout_init_mtx(&led_ch, &led_mtx, 0)` de led.c

**Teardown:**

- `destroy_dev()`, detén callouts y taskqueues, libera buffers
- Limpia los punteros (p. ej., `si_drv1 = NULL`) bajo lock antes de liberar
- Ejemplo: la limpieza en dos fases de led_destroy (primero mtx, luego sx)

**Comprueba antes del laboratorio:**

- ¿Puedes relacionar cada comportamiento visible para el usuario con el punto de entrada exacto?
- ¿Están todas las asignaciones emparejadas con sus liberaciones en todos los caminos de error?

### Esquema de la pseudointerfaz de red

**Dos caras:**

- Lado del dispositivo de caracteres (`/dev/tunN`, `/dev/tapN`) con open/read/write/ioctl/poll
- Lado de ifnet (`ifconfig tun0 ...`) con attach, flags, estado del enlace y hooks de BPF

**Flujo de datos:**

**Kernel  ->  usuario (read)**:

- Saca el paquete (mbuf) de la cola
- Bloquea hasta que haya uno disponible, salvo que se use `O_NONBLOCK` (en ese caso, `EWOULDBLOCK`)
- Copia primero las cabeceras opcionales (virtio/ifhead) y luego el payload mediante `m_mbuftouio()`
- Libera el mbuf con `m_freem()`
- Ejemplo: el bucle de tunread con `mtx_sleep()` para el bloqueo

**Usuario  ->  kernel (write)**:

- Construye un mbuf con `m_uiotombuf()`
- Decide el camino L2 o L3
- Para L3: elige el AF y ejecuta `netisr_dispatch()`
- Para L2: valida el destino (descarta tramas que una NIC real no recibiría salvo en modo promiscuo)
- Ejemplo: tunwrite_l3 despacha mediante NETISR_IP/NETISR_IPV6

**Ciclo de vida:**

- La clonación o la primera apertura crea el cdev y el softc
- Luego `if_alloc()`/`if_attach()` y `bpfattach()`
- La apertura puede activar el enlace; el cierre puede desactivarlo
- Ejemplo: tuncreate construye ifnet, tunopen marca el enlace como UP

**Notifica a los lectores:**

- `wakeup()`, `selwakeuppri()`, `KNOTE()` cuando llegan paquetes
- Ejemplo: la triple notificación de tunstart cuando se encola un paquete

**Comprueba antes del laboratorio:**

- ¿Sabes qué caminos bloquean y cuáles retornan de inmediato?
- ¿Está acotado el tamaño máximo de E/S (MRU + cabeceras)?
- ¿Se disparan los wakeups con cada encolado de paquete?

### Esquema del pegamento PCI

**Match  ->  Probe  ->  Attach  ->  Detach:**

**Match**: Tabla de fabricante/dispositivo(/subfabricante/subdispositivo); cae en clase/subclase cuando es necesario

- Ejemplo: la búsqueda en dos fases de uart_pci_match (IDs primarios y luego de subsistema)

**Probe**: Elige la clase de driver, calcula parámetros (desplazamiento de registro, rclk, RID de BAR) y luego llama al probe compartido del bus

- Ejemplo: uart_pci_probe establece `sc->sc_class = &uart_ns8250_class`

**Attach**: Asigna interrupciones (prefiere MSI de vector único si se soporta) y luego delega al subsistema

- Ejemplo: la asignación condicional de MSI en uart_pci_attach

**Detach**: Libera MSI/IRQ y luego delega al detach del subsistema

- Ejemplo: uart_pci_detach comprueba `sc_irid` y libera MSI si fue asignado

**Recursos:**

- Mapea BARs, asigna IRQs y entrega los recursos al núcleo
- Rastrea los IDs para poder liberarlos de forma simétrica
- Ejemplo: `id->rid & PCI_RID_MASK` extrae el número de BAR

**Comprueba antes del laboratorio:**

- ¿Gestionas el camino "sin coincidencia" de forma limpia (`ENXIO`)?
- ¿No hay fugas en ningún fallo a mitad del attach?
- ¿Compruebas los quirks (como el flag `PCI_NO_MSI`)?

### Referencia rápida de locking y concurrencia

**Camino rápido de movimiento de datos** (read/write, rx/tx):

- Protege las colas y el estado con un mutex
- Minimiza el tiempo de retención; nunca duermas mientras lo mantienes si es evitable
- Ejemplo: `tun_mtx` de tuntap protegiendo la cola de envío

**Configuración / topología** (crear/destruir, enlace activo/inactivo):

- Normalmente un sx lock o una serialización de nivel superior
- Ejemplo: el `led_sx` de led.c para la creación/destrucción de dispositivos

**Temporizador/callout**:

- Usa `callout_init_mtx(&callout, &mtx, flags)` para que el timeout se ejecute con tu mutex adquirido
- Ejemplo: el temporizador de led.c adquiere automáticamente `led_mtx`

**Notificaciones hacia el espacio de usuario**:

- Después de encolar: `wakeup(tp)`, `selwakeuppri(&sel, PRIO)`, `KNOTE(&klist, NOTE_*)`
- Ejemplo: el patrón de triple notificación de tunstart

**Reglas de orden de locks:**

- Nunca adquieras locks en un orden inconsistente
- Documenta tu jerarquía de locks
- Ejemplo: led.c adquiere `led_mtx` y lo libera antes de adquirir `led_sx`

### Patrones de movimiento de datos

**Bucle `uiomove()` para read/write de cdev:**

- Limita el tamaño del fragmento a un buffer seguro (evita copias enormes)
- Comprueba y gestiona los errores en cada iteración
- Ejemplo: zero_read limita las transferencias a `ZERO_REGION_SIZE` por iteración

**Ruta mbuf para redes:**

**User -> kernel**:

```c
m = m_uiotombuf(uio, M_NOWAIT, 0, align, M_PKTHDR);
// set metadata (AF/virtio)
netisr_dispatch(isr, m);
```

**Kernel -> user**:

```c
// optional header to user (uiomove())
m_mbuftouio(uio, m, 0);
m_freem(m);
```

Ejemplo: tunwrite construye el mbuf; tunread extrae los datos al espacio de usuario

### Patrones comunes de los recorridos

**Patrón: `cdevsw` compartido, estado por dispositivo mediante `si_drv1`**

- Una tabla de funciones, múltiples instancias de dispositivo
- Ejemplo: led.c comparte `led_cdevsw` entre todos los LEDs
- Estado accesible mediante `sc = dev->si_drv1`

**Patrón: Subsistema que ofrece ambas APIs**

- Interfaz en espacio de usuario (dispositivo de caracteres)
- API del kernel (llamadas a función)
- Ejemplo: `led_write()` frente a `led_set()` en led.c

**Patrón: Máquina de estados dirigida por temporizador**

- El contador de referencias realiza el seguimiento de los elementos activos
- El temporizador se reprograma solo cuando queda trabajo pendiente
- Ejemplo: el contador `blinkers` de led.c controla la activación del temporizador

**Patrón: Limpieza en dos fases**

- Fase 1: hacer invisible (limpiar punteros, eliminar de las listas)
- Fase 2: liberar recursos
- Ejemplo: led_destroy borra `si_drv1` antes de destruir el dispositivo

**Patrón: Asignación de números de unidad**

- Usa `unrhdr` para la asignación dinámica
- Evita conflictos en dispositivos con múltiples instancias
- Ejemplo: el pool `led_unit` de led.c

### Errores, casos límite y experiencia de usuario

**Gestión de errores:**

- Prefiere un errno claro frente al comportamiento silencioso, salvo que el silencio sea parte del contrato
- Ejemplo: tunwrite ignora silenciosamente las escrituras cuando la interfaz está caída (comportamiento esperado)
- Ejemplo: led_write devuelve `EINVAL` ante comandos incorrectos (condición de error)

**Validación de entradas:**

- Valida siempre tamaños, conteos e índices
- Ejemplo: led_write rechaza comandos de más de 512 bytes
- Ejemplo: tuntap comprueba contra MRU + cabeceras

**Falla rápido por defecto:**

- ioctl no soportado -> `ENOIOCTL`
- Flags inválidos -> `EINVAL`
- Tramas malformadas -> descartar e incrementar el contador de errores

**Descarga del módulo:**

- Piensa en el impacto sobre los usuarios activos
- No retires dispositivos fundamentales de sistemas en uso
- Ejemplo: null.c puede descargarse; led.c no puede (no tiene manejador de descarga)

### Plantillas mínimas

#### Dispositivo de caracteres (solo Read/Write/Ioctl)

```c
static d_read_t  foo_read;
static d_write_t foo_write;
static d_ioctl_t foo_ioctl;

static struct cdevsw foo_cdevsw = {
    .d_version = D_VERSION,
    .d_read    = foo_read,
    .d_write   = foo_write,
    .d_ioctl   = foo_ioctl,
    .d_name    = "foo",
};

static struct cdev *foo_dev;

static int
foo_read(struct cdev *dev, struct uio *uio, int flags)
{
    while (uio->uio_resid > 0) {
        size_t n = MIN(uio->uio_resid, CHUNK);
        int err = uiomove(srcbuf, n, uio);
        if (err) return err;
    }
    return 0;
}

static int
foo_write(struct cdev *dev, struct uio *uio, int flags)
{
    /* Consume all (bit bucket pattern) */
    uio->uio_resid = 0;
    return 0;
}

static int
foo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, 
          int fflag, struct thread *td)
{
    switch (cmd) {
    case FIONBIO:
        return 0;  /* Non-blocking always OK */
    default:
        return ENOIOCTL;
    }
}
```

#### Registro dinámico de dispositivos

```c
static struct unrhdr *foo_units;
static struct mtx foo_mtx;
static LIST_HEAD(, foo_softc) foo_list;

struct cdev *
foo_create(void *priv, const char *name)
{
    struct foo_softc *sc;
    
    sc = malloc(sizeof(*sc), M_FOO, M_WAITOK | M_ZERO);
    sc->unit = alloc_unr(foo_units);
    sc->private = priv;
    
    sc->dev = make_dev(&foo_cdevsw, sc->unit,
        UID_ROOT, GID_WHEEL, 0600, "foo/%s", name);
    sc->dev->si_drv1 = sc;
    
    mtx_lock(&foo_mtx);
    LIST_INSERT_HEAD(&foo_list, sc, list);
    mtx_unlock(&foo_mtx);
    
    return sc->dev;
}

void
foo_destroy(struct cdev *dev)
{
    struct foo_softc *sc;
    
    mtx_lock(&foo_mtx);
    sc = dev->si_drv1;
    dev->si_drv1 = NULL;
    LIST_REMOVE(sc, list);
    mtx_unlock(&foo_mtx);
    
    free_unr(foo_units, sc->unit);
    destroy_dev(dev);
    free(sc, M_FOO);
}
```

#### Plantilla PCI (Probe/Attach/Detach)

```c
static int foo_probe(device_t dev)
{
    /* Table match  ->  pick class */
    id = foo_pci_match(dev, foo_ids);
    if (id == NULL)
        return ENXIO;
    
    sc->sc_class = &foo_device_class;
    return foo_bus_probe(dev, id->regshft, id->rclk, 
                         id->rid & RID_MASK);
}

static int foo_attach(device_t dev)
{
    /* Maybe allocate single-vector MSI */
    if (pci_msi_count(dev) == 1) {
        count = 1;
        if (pci_alloc_msi(dev, &count) == 0)
            sc->sc_irid = 1;
    }
    return foo_bus_attach(dev);
}

static int foo_detach(device_t dev)
{
    /* Release MSI if used */
    if (sc->sc_irid != 0)
        pci_release_msi(dev);
    
    return foo_bus_detach(dev);
}

static device_method_t foo_methods[] = {
    DEVMETHOD(device_probe,  foo_probe),
    DEVMETHOD(device_attach, foo_attach),
    DEVMETHOD(device_detach, foo_detach),
    DEVMETHOD_END
};

static driver_t foo_driver = {
    "foo",
    foo_methods,
    sizeof(struct foo_softc)
};

DRIVER_MODULE(foo, pci, foo_driver, NULL, NULL);
```

### Autocomprobación previa al laboratorio (2 minutos)

Hazte estas preguntas antes de escribir código:

1. ¿Qué punto de integración tengo como objetivo (devfs, ifnet, PCI)?
2. ¿Conozco mis puntos de entrada y lo que cada uno debe devolver en caso de éxito o fallo?
3. ¿Cuáles son mis locks y qué contextos acceden a cada campo?
4. ¿Puedo enumerar cada recurso que asigno y dónde lo libero en:

	- La ruta de éxito
	- Un fallo a mitad del attach
	- El detach o la descarga

5. ¿He estudiado un driver similar de los recorridos?

	- null.c para dispositivos de caracteres simples
	- led.c para dispositivos dinámicos y temporizadores
	- tuntap para la integración en red
	- uart_pci para la conexión con hardware

### Reflexión posterior al laboratorio (5 minutos)

Después de escribir o modificar código, verifica:

1. ¿Hay alguna fuga de recursos en un retorno anticipado?
2. ¿He bloqueado en un contexto que no debería dormir?
3. ¿He notificado al espacio de usuario o a los pares del kernel tras encolar trabajo?
4. ¿Puedo rastrear un comportamiento visible para el usuario hasta las líneas de código fuente concretas?
5. ¿Siguen mis locks una jerarquía coherente?
6. ¿Son mis mensajes de error útiles para la depuración?

### Errores frecuentes y cómo evitarlos

Esta sección cataloga los errores que más problemas causan en el desarrollo de drivers: corrupción silenciosa, deadlocks, pánicos del kernel y fugas de recursos. Cada trampa incluye el síntoma, la causa raíz y el patrón correcto a seguir.

#### Errores en el movimiento de datos

##### **Trampa: Olvidar actualizar `uio_resid`**

**Síntoma**: Bucles infinitos en los manejadores de read/write, o espacio de usuario recibiendo conteos de bytes incorrectos.

**Causa raíz**: El kernel usa `uio_resid` para llevar la cuenta de los bytes restantes. Si no lo decrementas, el kernel considera que no se ha realizado ningún progreso.

**Incorrecto**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    /* Data is discarded but uio_resid never changes! */
    return 0;  /* Kernel sees 0 bytes written, retries infinitely */
}
```

**Correcto**:

```c
static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    uio->uio_resid = 0;  /* Mark all bytes consumed */
    return 0;
}
```

**Cómo evitarlo**: Pregúntate siempre "¿cuántos bytes he procesado realmente?" y actualiza `uio_resid` en consecuencia. Aunque descartes datos (como hace `/dev/null`), debes marcarlos como consumidos.

**Relacionado**: Las transferencias parciales son peligrosas. Si procesas algunos bytes y luego fallas, debes actualizar `uio_resid` para reflejar lo que se transfirió realmente antes de devolver el error; de lo contrario, el espacio de usuario reintentará con el desplazamiento incorrecto.

##### **Trampa: No limitar el tamaño de los fragmentos en los bucles `uiomove()`**

**Síntoma**: Desbordamiento de pila si se copia a un buffer en la pila, pánico del kernel ante asignaciones enormes.

**Causa raíz**: Las peticiones del usuario pueden ser arbitrariamente grandes. Copiar transferencias de varios megabytes de una sola vez agota los recursos.

**Incorrecto**:

```c
static int
bad_read(struct cdev *dev, struct uio *uio, int flags)
{
    char buf[uio->uio_resid];  /* Stack overflow if user requests 1MB! */
    memset(buf, 0, sizeof(buf));
    return uiomove(buf, uio->uio_resid, uio);
}
```

**Correcto**:

```c
#define CHUNK_SIZE 4096

static int
good_read(struct cdev *dev, struct uio *uio, int flags)
{
    char buf[CHUNK_SIZE];
    int error;
    
    memset(buf, 0, sizeof(buf));
    
    while (uio->uio_resid > 0) {
        size_t len = MIN(uio->uio_resid, CHUNK_SIZE);
        error = uiomove(buf, len, uio);
        if (error)
            return error;
    }
    return 0;
}
```

**Cómo evitarlo**: Itera siempre con un tamaño de fragmento razonable (normalmente entre 4 KB y 64 KB). Estudia `zero_read` en null.c: limita las transferencias a `ZERO_REGION_SIZE` por iteración.

##### **Trampa: Acceder directamente a la memoria de usuario desde el kernel**

**Síntoma**: Vulnerabilidades de seguridad, caídas del kernel ante punteros inválidos.

**Causa raíz**: Los espacios de memoria del kernel y del usuario son independientes. Desreferenciar punteros de usuario directamente elude los mecanismos de protección.

**Incorrecto**:

```c
static int
bad_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    char *user_ptr = *(char **)data;
    strcpy(kernel_buf, user_ptr);  /* DANGER: user_ptr not validated! */
}
```

**Correcto**:

```c
static int
good_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    char *user_ptr = *(char **)data;
    char kernel_buf[256];
    int error;
    
    error = copyinstr(user_ptr, kernel_buf, sizeof(kernel_buf), NULL);
    if (error)
        return error;
    /* Now safe to use kernel_buf */
}
```

**Cómo evitarlo**: Nunca desreferencíes punteros recibidos del espacio de usuario. Usa `copyin()`, `copyout()`, `copyinstr()` o `uiomove()` para todas las transferencias entre usuario y kernel. Estas funciones validan las direcciones y gestionan los fallos de página de forma segura.

#### Desastres por locking

##### **Trampa: Mantener locks durante `uiomove()`**

**Síntoma**: Deadlock del sistema cuando la memoria de usuario está paginada.

**Causa raíz**: `uiomove()` puede provocar un fallo de página, que a su vez puede necesitar adquirir locks de VM. Si mantienes otro lock durante el fallo, y ese lock es necesario en la ruta de paginación, se produce un deadlock.

**Incorrecto**:

```c
static int
bad_read(struct cdev *dev, struct uio *uio, int flags)
{
    mtx_lock(&my_mtx);
    /* Build response in kernel buffer */
    uiomove(kernel_buf, len, uio);  /* DEADLOCK RISK: uiomove while locked */
    mtx_unlock(&my_mtx);
    return 0;
}
```

**Correcto**:

```c
static int
good_read(struct cdev *dev, struct uio *uio, int flags)
{
    char *local_buf;
    size_t len;
    
    mtx_lock(&my_mtx);
    /* Copy data to private buffer while locked */
    len = MIN(uio->uio_resid, bufsize);
    local_buf = malloc(len, M_TEMP, M_WAITOK);
    memcpy(local_buf, sc->data, len);
    mtx_unlock(&my_mtx);
    
    /* Transfer to user without holding lock */
    error = uiomove(local_buf, len, uio);
    free(local_buf, M_TEMP);
    return error;
}
```

**Cómo evitarlo**: Libera siempre los locks antes de llamar a `uiomove()`, `copyin()` o `copyout()`. Toma una instantánea de los datos que necesitas con el lock adquirido y, luego, transfiere esos datos al espacio de usuario sin el lock.

**Excepción**: Algunos locks que permiten dormir (sx locks con `SX_DUPOK`) pueden mantenerse durante el acceso a memoria de usuario si el diseño lo contempla con cuidado, pero los mutex nunca pueden.

##### **Trampa: Orden de adquisición de locks inconsistente**

**Síntoma**: Deadlock cuando dos threads adquieren los mismos locks en orden inverso.

**Causa raíz**: Las violaciones del orden de adquisición de locks crean condiciones de espera circular.

**Incorrecto**:

```c
/* Thread A */
mtx_lock(&lock_a);
mtx_lock(&lock_b);  /* Order: A then B */

/* Thread B */
mtx_lock(&lock_b);
mtx_lock(&lock_a);  /* Order: B then A - DEADLOCK! */
```

**Correcto**:

```c
/* Establish hierarchy: always lock_a before lock_b */

/* Thread A */
mtx_lock(&lock_a);
mtx_lock(&lock_b);

/* Thread B */
mtx_lock(&lock_a);  /* Same order everywhere */
mtx_lock(&lock_b);
```

**Cómo evitarlo**:

1. Documenta la jerarquía de locks en comentarios al principio del archivo
2. Adquiere los locks siempre en el mismo orden en todo el driver
3. Usa la opción de kernel `WITNESS` durante el desarrollo para detectar violaciones
4. Estudia led.c: adquiere `led_mtx` primero, lo libera y luego adquiere `led_sx`, sin mantener ambos simultáneamente

##### **Trampa: Olvidar inicializar los locks**

**Síntoma**: Pánico del kernel con "lock not initialized" o bloqueo inmediato en la primera adquisición del lock.

**Causa raíz**: Las estructuras de lock deben inicializarse explícitamente antes de usarse.

**Incorrecto**:

```c
static struct mtx my_lock;  /* Declared but not initialized */

static int
foo_attach(device_t dev)
{
    mtx_lock(&my_lock);  /* PANIC: uninitialized lock */
}
```

**Correcto**:

```c
static struct mtx my_lock;

static void
foo_init(void)
{
    mtx_init(&my_lock, "my lock", NULL, MTX_DEF);
}

SYSINIT(foo, SI_SUB_DRIVERS, SI_ORDER_FIRST, foo_init, NULL);
```

**Cómo evitarlo**:

- Inicializa los locks en el manejador de carga del módulo, en `SYSINIT` o en la función attach
- Usa `mtx_init()`, `sx_init()` o `rw_init()` según corresponda
- Para callouts: `callout_init_mtx()` asocia el temporizador con el lock
- Estudia `led_drvinit()` en led.c: inicializa todos los locks antes de crear cualquier dispositivo

##### **Trampa: Destruir locks mientras threads los mantienen**

**Síntoma**: Pánico del kernel durante la descarga del módulo o el detach del dispositivo.

**Causa raíz**: Las estructuras de lock deben permanecer válidas hasta que todos sus usuarios hayan terminado.

**Incorrecto**:

```c
static int
bad_detach(device_t dev)
{
    mtx_destroy(&sc->mtx);     /* Destroy lock */
    destroy_dev(sc->dev);       /* But device write handler may still run! */
    return 0;
}
```

**Correcto**:

```c
static int
good_detach(device_t dev)
{
    destroy_dev(sc->dev);       /* Wait for all users to finish */
    /* Now safe - no threads can be in device operations */
    mtx_destroy(&sc->mtx);
    return 0;
}
```

**Cómo evitarlo**:

- `destroy_dev()` bloquea hasta que todos los descriptores de archivo abiertos se cierran y las operaciones en curso finalizan
- Destruye los locks solo después de que los dispositivos o recursos hayan desaparecido
- Para locks globales: destrúyelos en la descarga del módulo, o nunca (si el módulo no puede descargarse)

#### Fallos en la gestión de recursos

##### **Trampa: Fugas de recursos en las rutas de error**

**Síntoma**: Fugas de memoria, fugas de nodos de dispositivo, agotamiento eventual de recursos.

**Causa raíz**: Los retornos anticipados omiten el código de limpieza.

**Incorrecto**:

```c
static int
bad_attach(device_t dev)
{
    sc = malloc(sizeof(*sc), M_DEV, M_WAITOK);
    
    sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (sc->res == NULL)
        return ENXIO;  /* LEAK: sc not freed! */
    
    error = setup_irq(dev);
    if (error)
        return error;  /* LEAK: sc and sc->res not freed! */
    
    return 0;
}
```

**Correcto**:

```c
static int
good_attach(device_t dev)
{
    sc = malloc(sizeof(*sc), M_DEV, M_WAITOK | M_ZERO);
    
    sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (sc->res == NULL) {
        error = ENXIO;
        goto fail;
    }
    
    error = setup_irq(dev);
    if (error)
        goto fail;
    
    return 0;

fail:
    if (sc->res != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res);
    free(sc, M_DEV);
    return error;
}
```

**Cómo evitarlo**:

- Usa una única etiqueta `fail:` al final de la función
- Comprueba qué recursos se han asignado y libera solo esos
- Inicializa los punteros a NULL para poder comprobarlos
- Ten en cuenta que cada `malloc()` necesita un `free()`, y cada `make_dev()` necesita un `destroy_dev()`

##### **Trampa: Use-after-free en la limpieza concurrente**

**Síntoma**: Pánico del kernel con "page fault in kernel mode", a menudo intermitente.

**Causa raíz**: Un thread libera memoria mientras otro thread todavía accede a ella.

**Incorrecto**:

```c
void
bad_destroy(struct cdev *dev)
{
    struct foo_softc *sc = dev->si_drv1;
    
    free(sc, M_FOO);            /* Free immediately */
    /* Another thread's foo_write may still be using sc! */
}
```

**Correcto**:

```c
void
good_destroy(struct cdev *dev)
{
    struct foo_softc *sc;
    
    mtx_lock(&foo_mtx);
    sc = dev->si_drv1;
    dev->si_drv1 = NULL;        /* Break link first */
    LIST_REMOVE(sc, list);      /* Remove from searchable lists */
    mtx_unlock(&foo_mtx);
    
    destroy_dev(dev);           /* Wait for operations to drain */
    
    /* Now safe - no threads can find or access sc */
    free(sc, M_FOO);
}
```

**Cómo evitarlo**:

- Haz los objetos invisibles antes de liberarlos (limpia los punteros, elimínalos de las listas)
- Usa `destroy_dev()`, que espera a que finalicen las operaciones en curso
- Estudia led_destroy: borra `si_drv1` primero, luego lo elimina de la lista y finalmente libera la memoria

##### **Trampa: No comprobar los fallos de asignación con `M_NOWAIT`**

**Síntoma**: Pánico del kernel al desreferenciar un puntero NULL.

**Causa raíz**: Las asignaciones con `M_NOWAIT` pueden fallar, pero el código da por supuesto que tienen éxito.

**Incorrecto**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf = malloc(uio->uio_resid, M_TEMP, M_NOWAIT);
    /* PANIC if malloc returns NULL and we dereference buf! */
    uiomove(buf, uio->uio_resid, uio);
    free(buf, M_TEMP);
}
```

**Correcto**:

```c
static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf = malloc(uio->uio_resid, M_TEMP, M_NOWAIT);
    if (buf == NULL)
        return ENOMEM;
    
    error = uiomove(buf, uio->uio_resid, uio);
    free(buf, M_TEMP);
    return error;
}
```

**Mejor opción**: Usa `M_WAITOK` cuando sea seguro:

```c
static int
better_write(struct cdev *dev, struct uio *uio, int flags)
{
    /* M_WAITOK can sleep but never returns NULL */
    char *buf = malloc(uio->uio_resid, M_TEMP, M_WAITOK);
    error = uiomove(buf, uio->uio_resid, uio);
    free(buf, M_TEMP);
    return error;
}
```

**Cómo evitarlo**:

- Usa `M_WAITOK` salvo que estés en contexto de interrupción o mantengas spinlocks
- Comprueba siempre si las asignaciones con `M_NOWAIT` devuelven NULL
- Estudia led_write: usa `M_WAITOK` porque las operaciones de escritura pueden dormir

#### Errores en temporizadores y operaciones asíncronas

##### **Trampa: Callback de temporizador accediendo a memoria liberada**

**Síntoma**: Pánico en el callback del temporizador, corrupción de memoria.

**Causa raíz**: El dispositivo se ha destruido pero el temporizador sigue programado.

**Incorrecto**:

```c
void
bad_destroy(struct cdev *dev)
{
    struct foo_softc *sc = dev->si_drv1;
    
    destroy_dev(dev);
    free(sc, M_FOO);            /* Free softc */
    /* Timer may fire and access sc! */
}
```

**Correcto**:

```c
void
good_destroy(struct cdev *dev)
{
    struct foo_softc *sc = dev->si_drv1;
    
    callout_drain(&sc->callout);  /* Wait for callback to complete */
    destroy_dev(dev);
    free(sc, M_FOO);              /* Now safe */
}
```

**Cómo evitarlo**:

- Usa `callout_drain()` antes de liberar las estructuras a las que accede el callback
- O usa `callout_stop()` y asegúrate de que no hay ningún callback en ejecución
- Inicializa los callouts con `callout_init_mtx()` para que el lock se adquiera automáticamente
- Estudia led_destroy: detiene el temporizador cuando la lista queda vacía

##### **Trampa: Reprogramar el temporizador de forma incondicional**

**Síntoma**: Desperdicio de CPU, ralentización del sistema, despertares innecesarios.

**Causa raíz**: El temporizador se dispara aunque no haya trabajo pendiente.

**Incorrecto**:

```c
static void
bad_timeout(void *arg)
{
    /* Process items */
    LIST_FOREACH(item, &list, entries) {
        if (item->active)
            process_item(item);
    }
    
    /* Always reschedule - wastes CPU even when list empty! */
    callout_reset(&timer, hz / 10, bad_timeout, arg);
}
```

**Correcto**:

```c
static void
good_timeout(void *arg)
{
    int active_count = 0;
    
    LIST_FOREACH(item, &list, entries) {
        if (item->active) {
            process_item(item);
            active_count++;
        }
    }
    
    /* Only reschedule if there's work */
    if (active_count > 0)
        callout_reset(&timer, hz / 10, good_timeout, arg);
}
```

**Cómo evitarlo**:

- Mantén un contador de elementos que necesiten atención
- Programa el temporizador solo cuando el contador sea mayor que 0
- Estudia led.c: el contador `blinkers` controla la reprogramación del temporizador

#### Problemas específicos de los drivers de red

##### **Trampa: No liberar los mbufs en las rutas de error**

**Síntoma**: Agotamiento de mbufs, mensajes de "network buffers exhausted".

**Causa raíz**: Los mbufs son un recurso limitado que debe liberarse explícitamente.

**Incorrecto**:

```c
static int
bad_transmit(struct ifnet *ifp, struct mbuf *m)
{
    if (validate_packet(m) < 0)
        return EINVAL;  /* LEAK: m not freed! */
    
    if (queue_full())
        return ENOBUFS; /* LEAK: m not freed! */
    
    enqueue_packet(m);
    return 0;
}
```

**Correcto**:

```c
static int
good_transmit(struct ifnet *ifp, struct mbuf *m)
{
    if (validate_packet(m) < 0) {
        m_freem(m);
        return EINVAL;
    }
    
    if (queue_full()) {
        m_freem(m);
        return ENOBUFS;
    }
    
    enqueue_packet(m);  /* Queue now owns mbuf */
    return 0;
}
```

**Cómo evitarlo**:

- Quien posea el puntero al mbuf es responsable de liberarlo
- En caso de error: llama a `m_freem(m)` antes de retornar
- En caso de éxito: asegúrate de que otro código tomó la propiedad del mbuf (encolar, transmitir, etc.)

##### **Trampa: Olvidar notificar a lectores/escritores bloqueados**

**Síntoma**: Los procesos se quedan colgados en read/write/poll aunque haya datos disponibles.

**Causa raíz**: Los datos llegan pero los waiters no reciben la señal de despertar.

**Incorrecto**:

```c
static void
bad_rx_handler(struct foo_softc *sc, struct mbuf *m)
{
    TAILQ_INSERT_TAIL(&sc->rxq, m, list);
    /* Reader blocked in read() never wakes up! */
}
```

**Correcto**:

```c
static void
good_rx_handler(struct foo_softc *sc, struct mbuf *m)
{
    TAILQ_INSERT_TAIL(&sc->rxq, m, list);
    
    /* Triple notification pattern */
    wakeup(sc);                              /* Wake sleeping threads */
    selwakeuppri(&sc->rsel, PZERO + 1);      /* Wake poll/select */
    KNOTE_LOCKED(&sc->rsel.si_note, 0);      /* Wake kqueue */
}
```

**Cómo evitarlo**:

- Tras encolar datos: llama a `wakeup()`, `selwakeuppri()` o `KNOTE()`
- Estudia tunstart en if_tuntap.c: el patrón de triple notificación
- Para escritura: notifica tras desencolar (cuando haya espacio disponible)

#### Fallos en la validación de entradas

##### **Trampa: No limitar el tamaño de las entradas**

**Síntoma**: Denegación de servicio, agotamiento de la memoria del kernel.

**Causa raíz**: Un atacante puede solicitar asignaciones enormes o provocar copias masivas.

**Incorrecto**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf = malloc(uio->uio_resid, M_TEMP, M_WAITOK);
    /* Attacker writes 1GB, kernel allocates 1GB! */
    uiomove(buf, uio->uio_resid, uio);
    process(buf);
    free(buf, M_TEMP);
}
```

**Correcto**:

```c
#define MAX_CMD_SIZE 4096

static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf;
    
    if (uio->uio_resid > MAX_CMD_SIZE)
        return EINVAL;  /* Reject excessive requests */
    
    buf = malloc(uio->uio_resid, M_TEMP, M_WAITOK);
    uiomove(buf, uio->uio_resid, uio);
    process(buf);
    free(buf, M_TEMP);
}
```

**Cómo evitarlo**:

- Define tamaños máximos para todas las entradas (comandos, paquetes, buffers)
- Comprueba los límites antes de la asignación
- Estudia `led_write`: rechaza comandos de más de 512 bytes

##### **Trampa: confiar en las longitudes y desplazamientos proporcionados por el usuario**

**Síntoma**: desbordamientos de buffer, lectura de memoria no inicializada, filtraciones de información.

**Causa raíz**: el usuario controla los campos de longitud en las estructuras de ioctl.

**Incorrecto**:

```c
struct user_request {
    void *buf;
    size_t len;
};

static int
bad_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    struct user_request *req = (struct user_request *)data;
    char kernel_buf[256];
    
    /* User can set len > 256! */
    copyin(req->buf, kernel_buf, req->len);  /* Buffer overrun! */
}
```

**Correcto**:

```c
static int
good_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    struct user_request *req = (struct user_request *)data;
    char kernel_buf[256];
    
    if (req->len > sizeof(kernel_buf))
        return EINVAL;
    
    return copyin(req->buf, kernel_buf, req->len);
}
```

**Cómo evitarlo**:

- Valida todos los campos de longitud frente a los tamaños de los buffers
- Valida que los desplazamientos estén dentro de rangos válidos
- Usa `MIN()` para acotar longitudes: `len = MIN(user_len, MAX_LEN)`

#### Condiciones de carrera y problemas de sincronización

##### **Trampa: condiciones de carrera de tipo comprobar-y-usar**

**Síntoma**: cuelgues intermitentes, vulnerabilidades de seguridad (bugs TOCTOU).

**Causa raíz**: el estado cambia entre la comprobación y el uso.

**Incorrecto**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct foo_softc *sc = dev->si_drv1;
    
    if (sc == NULL)          /* Check */
        return ENXIO;
    
    /* Another thread destroys device here! */
    
    process_data(sc->buf);   /* Use - sc may be freed! */
}
```

**Correcto**:

```c
static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct foo_softc *sc;
    int error;
    
    mtx_lock(&foo_mtx);
    sc = dev->si_drv1;
    if (sc == NULL) {
        mtx_unlock(&foo_mtx);
        return ENXIO;
    }
    
    /* Process while holding lock */
    error = process_data_locked(sc->buf);
    mtx_unlock(&foo_mtx);
    return error;
}
```

**Cómo evitarlo**:

- Mantén el lock adecuado desde la comprobación hasta el uso
- Haz que las comprobaciones y los usos sean atómicos entre sí
- O utiliza conteo de referencias para mantener vivos los objetos

##### **Trampa: barreras de memoria ausentes en código sin locks**

**Síntoma**: corrupción esporádica en sistemas multinúcleo, funciona bien en un único núcleo.

**Causa raíz**: reordenación de operaciones de memoria por parte de la CPU.

**Incorrecto**:

```c
/* Producer */
sc->data = new_value;    /* Write data */
sc->ready = 1;           /* Set flag - may be reordered before data write! */

/* Consumer */
if (sc->ready)           /* Check flag */
    use(sc->data);       /* May see old data! */
```

**Correcto con barreras explícitas**:

```c
/* Producer */
sc->data = new_value;
atomic_store_rel_int(&sc->ready, 1);  /* Release barrier */

/* Consumer */
if (atomic_load_acq_int(&sc->ready))  /* Acquire barrier */
    use(sc->data);
```

**Mejor: usa simplemente locks**:

```c
/* Much simpler and correct */
mtx_lock(&sc->mtx);
sc->data = new_value;
sc->ready = 1;
mtx_unlock(&sc->mtx);
```

**Cómo evitarlo**:

- Evita la programación sin locks a menos que seas un experto
- Usa locks para garantizar la corrección; optimiza solo si el profiling lo justifica
- Si debes prescindir de locks: utiliza operaciones atómicas con barreras explícitas

#### Problemas en el ciclo de vida del módulo

##### **Trampa: operaciones sobre el dispositivo compitiendo con la descarga del módulo**

**Síntoma**: cuelgue durante `kldunload`, saltos a memoria inválida.

**Causa raíz**: funciones descargadas mientras aún están en uso.

**Incorrecto**:

```c
static int
bad_unload(module_t mod, int type, void *data)
{
    switch(type) {
    case MOD_UNLOAD:
        destroy_dev(my_dev);
        return 0;  /* Module text may be unloaded while write() in progress! */
    }
}
```

**Correcto**:

```c
static int
good_unload(module_t mod, int type, void *data)
{
    switch(type) {
    case MOD_UNLOAD:
        /* destroy_dev() waits for all operations to complete */
        destroy_dev(my_dev);
        /* Now safe - no code paths reference module functions */
        return 0;
    }
}
```

**Cómo evitarlo**:

- `destroy_dev()` evita esto automáticamente esperando a que no haya usuarios activos
- Para módulos de infraestructura (como `led.c`): no proporciones un manejador de descarga
- Prueba la descarga bajo carga: `while true; do cat /dev/foo; done & sleep 1; kldunload foo`

##### **Trampa: la descarga deja referencias colgantes**

**Síntoma**: cuelgues en código aparentemente no relacionado tras descargar el módulo.

**Causa raíz**: otro código conserva punteros a datos o funciones del módulo descargado.

**Incorrecto**:

```c
/* Your module */
void my_callback(void *arg) { /* ... */ }

static int
bad_load(module_t mod, int type, void *data)
{
    register_callback(my_callback);  /* Register with another subsystem */
    return 0;
}

static int
bad_unload(module_t mod, int type, void *data)
{
    return 0;  /* Forgot to unregister - subsystem will call invalid function! */
}
```

**Correcto**:

```c
static int
good_unload(module_t mod, int type, void *data)
{
    unregister_callback(my_callback);  /* Clean up registrations */
    /* Wait for any in-progress callbacks to complete */
    return 0;
}
```

**Cómo evitarlo**:

- Cada registro necesita su correspondiente deregistro
- Cada instalación de callback necesita su eliminación
- Cada "registrar en subsistema" necesita su "dar de baja del subsistema"

### Patrones de fallos de depuración habituales

#### **Cómo detectar estos errores:**

**Para problemas de locking**:

```console
# In kernel config or loader.conf
options WITNESS
options WITNESS_SKIPSPIN
options INVARIANTS
options INVARIANT_SUPPORT
```

WITNESS detecta violaciones en el orden de adquisición de locks y las notifica en dmesg.

**Para problemas de memoria**:

```console
# Track allocations
vmstat -m | grep M_YOURTYPE

# Enable kernel malloc debugging
options MALLOC_DEBUG_MAXZONES=8
```

**Para condiciones de carrera**:

- Ejecuta pruebas de estrés en sistemas multinúcleo
- Usa la suite de pruebas `stress2`
- Operaciones concurrentes: múltiples threads abriendo, cerrando, leyendo y escribiendo

**Para detectar fugas**:

- Antes de cargar: apunta los recuentos de recursos (`vmstat -m`, `devfs`, `ifconfig -a`)
- Carga el módulo y ejercítalo intensamente
- Descarga el módulo
- Comprueba los recuentos de recursos: deben volver a los valores iniciales

### Lista de verificación preventiva

Antes de hacer commit del código, verifica:

**Movimiento de datos**

- Todas las llamadas a `uiomove()` actualizan correctamente `uio_resid`
- Los tamaños de bloque están acotados a límites razonables
- No hay desreferenciado directo de punteros de usuario

**Locking**

- Ningún lock se mantiene durante `uiomove()`, `copyin()` o `copyout()`
- El orden de adquisición de locks está documentado y se respeta de forma coherente
- Todos los locks se inicializan antes de usarlos
- Los locks se destruyen solo después de que el último usuario haya terminado

**Recursos**

- Cada asignación tiene su liberación correspondiente en todos los caminos de ejecución
- Los caminos de error están probados y no producen fugas
- Los objetos se vuelven invisibles antes de ser liberados
- Comprobaciones de NULL después de asignaciones con `M_NOWAIT`

**Temporizadores**

- Se llama a `callout_drain()` antes de liberar las estructuras
- La reprogramación del temporizador está controlada por un contador de trabajo
- El callout se inicializa con su mutex asociado

**Red (si aplica)**

- Los mbufs se liberan en todos los caminos de error
- Triple notificación tras poner en la cola
- Los tamaños de entrada se validan frente al MRU

**Validación de entrada**

- Los tamaños máximos están definidos y se aplican
- Las longitudes proporcionadas por el usuario se comprueban
- Los desplazamientos se validan antes de usarlos

**Condiciones de carrera**

- No hay patrones de comprobación seguida de uso sin lock
- Las secciones críticas están correctamente protegidas
- Se evita el código sin locks salvo cuando sea imprescindible

**Ciclo de vida**

- Se llama a `destroy_dev()` antes de liberar el softc
- Toda operación de registro tiene su deregistro correspondiente
- La descarga se prueba bajo uso concurrente

### Cuando las cosas van mal

**Si ves "sleeping with lock held"**:

- Probablemente mantienes un mutex durante `uiomove()` o una asignación con `M_WAITOK`
- Solución: suelta el lock antes de la operación bloqueante

**Si ves "lock order reversal"**:

- Dos locks se adquieren en órdenes distintos en diferentes caminos de código
- Solución: establece y documenta la jerarquía, y corrige el código que la viola

**Si ves "page fault in kernel mode"**:

- Normalmente es un uso tras liberación o una desreferencia de NULL
- Comprueba: ¿estás accediendo a memoria después de liberarla? ¿Se borra `si_drv1` primero?

**Si los procesos se quedan bloqueados indefinidamente**:

- Falta un `wakeup()` o una notificación
- Comprueba: ¿llama cada operación de encolado a wakeup, selwakeup o KNOTE?

**Si hay fugas de recursos**:

- Falta limpieza en algún camino de error
- Comprueba: ¿libera cada return anticipado lo que se asignó?

### Ya estás listo: de los patrones a la práctica

Al estudiar estos fallos y sus soluciones en el contexto de los cuatro recorridos de drivers, desarrollas los instintos necesarios para evitarlos. Los patrones se repiten: comprueba antes de usar, aplica locking adecuado, libera lo que asignas, notifica cuando encolas, valida la entrada del usuario. Domina estos principios y tus drivers serán robustos.

Ahora tienes un modelo mental compacto: los mismos pocos patrones se repiten con distintas aplicaciones. Mantén este esquema a mano mientras afrontas los laboratorios prácticos; es el camino más corto entre "creo que lo entiendo" y "puedo entregar un driver que se comporta correctamente".

Cuando tengas dudas, vuelve a los cuatro recorridos de drivers. Son tus ejemplos resueltos que muestran estos patrones en código completo y funcional.

**A continuación**, llega el momento de ponerse manos a la obra con cuatro laboratorios prácticos.

## Laboratorios prácticos: del estudio a la construcción (aptos para principiantes)

Has leído sobre la estructura de los drivers; ahora **experiméntala**. Estos cuatro laboratorios, diseñados con cuidado, te llevan desde la lectura del código hasta la construcción de módulos del kernel funcionales, y cada uno valida tu comprensión antes de avanzar.

### Filosofía de diseño de los laboratorios

Estos laboratorios son:

- **Seguros**: se ejecutan en tu VM de laboratorio, aislada de tu sistema principal
- **Incrementales**: cada uno se apoya en el anterior con puntos de control claros
- **Autovalidables**: sabrás de inmediato si has tenido éxito
- **Explicativos**: el código incluye comentarios que explican el "por qué" detrás del "qué"
- **Completos**: todo el código está probado en FreeBSD 14.3 y listo para usar

### Requisitos previos para todos los laboratorios

Antes de comenzar, asegúrate de tener:

1. **FreeBSD 14.3** en ejecución (VM o máquina física)

2. **Código fuente instalado**: debe existir `/usr/src`

   ```bash
   # If /usr/src is missing, install it:
   % sudo pkg install git
   % sudo git clone --branch releng/14.3 --depth 1 https://git.FreeBSD.org/src.git src /usr/src
   ```
   
3. **Herramientas de build instaladas**:

   ```bash
   % sudo pkg install llvm
   ```

4. **Acceso como root** mediante `sudo` o `su`

5. **Tu cuaderno de laboratorio** para notas y observaciones

### Tiempo necesario

- **Lab 1** (búsqueda del tesoro): 30-40 minutos
- **Lab 2** (módulo Hello): 40-50 minutos
- **Lab 3** (nodo de dispositivo): 60-75 minutos
- **Lab 4** (manejo de errores): 30-40 minutos

**Total**: entre 2,5 y 3,5 horas para todos los laboratorios

**Recomendación**: completa el Lab 1 y el Lab 2 en una misma sesión, descansa, y luego afronta el Lab 3 y el Lab 4 en una segunda sesión.

## Lab 1: Explora el mapa de drivers (búsqueda del tesoro, solo lectura)

### Objetivo

Localizar e identificar las estructuras clave de los drivers en el código fuente real de FreeBSD. Ganar confianza en la navegación y habilidad para reconocer patrones.

### Qué aprenderás

- Cómo encontrar y leer los archivos fuente de los drivers de FreeBSD
- Cómo reconocer patrones comunes (cdevsw, probe/attach, DRIVER_MODULE)
- Dónde viven los distintos tipos de drivers en el árbol de código fuente
- Cómo usar `less` y grep de forma efectiva para explorar drivers

### Requisitos previos

- FreeBSD 14.3 con `/usr/src` instalado
- Editor de texto o `less` para visualizar archivos
- Terminal con tu shell favorita

### Tiempo estimado

30-40 minutos (solo las preguntas)  
+10 minutos si quieres explorar más allá de las preguntas

### Instrucciones

#### Parte 1: Driver de dispositivo de caracteres: el driver null

**Paso 1**: Navega hasta el driver null

```bash
% cd /usr/src/sys/dev/null
% ls -l
total 8
-rw-r--r--  1 root  wheel  4127 Oct 14 10:15 null.c
```

**Paso 2**: Abre el archivo con `less`

```bash
% less null.c
```

**Consejos de navegación en `less`**:

- Pulsa `/` para buscar (ejemplo: `/cdevsw` para encontrar la estructura cdevsw)
- Pulsa `n` para ir a la siguiente ocurrencia
- Pulsa `q` para salir
- Pulsa `g` para ir al principio y `G` para ir al final

**Paso 3**: Responde a estas preguntas (escríbelas en tu cuaderno de laboratorio):

**P1**: ¿En qué número de línea se define la estructura `null_cdevsw`?  
*Pista*: busca `/cdevsw` en less

**P2**: ¿Qué función gestiona las escrituras en `/dev/null`?  
*Pista*: observa la línea `.d_write =` en la estructura cdevsw

**P3**: ¿Qué devuelve la función de escritura?  
*Pista*: mira la implementación de la función

**P4**: ¿Dónde está el manejador de eventos del módulo? ¿Cómo se llama?  
*Pista*: busca `modevent`

**P5**: ¿Qué macro registra el módulo en el kernel?  
*Pista*: busca cerca del final del archivo, buscando `DECLARE_MODULE`

**P6**: ¿Cuántos nodos de dispositivo crea este módulo en `/dev`?  
*Pista*: cuenta las llamadas a `make_dev_credf` en el manejador de carga

**P7**: ¿Cómo se llaman los nodos de dispositivo?  
*Pista*: mira el último parámetro de cada llamada a `make_dev_credf`

#### Parte 2: Driver de infraestructura: el driver LED

**Paso 4**: Navega hasta el driver LED

```bash
% cd /usr/src/sys/dev/led
% less led.c
```

**Paso 5**: Responde a estas preguntas:

**P8**: Encuentra la estructura softc. ¿Cómo se llama?  
*Pista*: busca `_softc {` para encontrar definiciones de estructuras

**P9**: ¿Dónde está definida `led_create()`?  
*Pista*: busca `^led_create` (^ significa inicio de línea)

**P10**: ¿En qué subdirectorio de `/dev` aparecen los nodos de dispositivo LED?  
*Pista*: mira la llamada a `make_dev` en `led_create()` y comprueba la ruta

**P11**: Encuentra la función `led_write`. ¿Qué hace con la entrada del usuario?  
*Pista*: busca la definición de la función y lee el código

**P12**: ¿Hay un par probe/attach, o usa un manejador de eventos del módulo?  
*Pista*: busca `probe` y `attach` frente a `modevent`

**P13**: ¿Puedes encontrar dónde el driver asigna memoria para el softc?  
*Pista*: busca llamadas a `malloc` en `led_create()`

#### Parte 3: Driver de red: el driver tun/tap

**Paso 6**: Navega hasta el driver tun/tap

```bash
% cd /usr/src/sys/net
% less if_tuntap.c
```

**Nota**: Este es un driver más grande y complejo. No intentes entenderlo todo; limítate a encontrar los patrones concretos.

**Paso 7**: Responde a estas preguntas:

**P14**: Encuentra la estructura softc de tun. ¿Cómo se llama?  
*Pista*: busca `tun_softc {`

**P15**: ¿Contiene el softc tanto un `struct cdev *` como un puntero a interfaz de red?  
*Pista*: mira los miembros de la estructura softc

**P16**: ¿Dónde está definida la estructura `tun_cdevsw`?  
*Pista*: busca `tun_cdevsw =`

**P17**: ¿Qué función se llama cuando abres `/dev/tun`?  
*Pista*: mira la línea `.d_open =` en el cdevsw

**P18**: ¿Dónde crea el driver la interfaz de red?  
*Pista*: busca `if_alloc` en el código fuente

#### Parte 4: Driver conectado al bus: un UART por PCI

**Paso 8**: Navega hasta un driver PCI

```bash
% cd /usr/src/sys/dev/uart
% less uart_bus_pci.c
```

**Paso 9**: Responde a estas preguntas:

**P19**: Encuentra la función probe. ¿Cómo se llama?  
*Pista*: busca una función que termine en `_probe`

**P20**: ¿Qué comprueba la función probe para identificar hardware compatible?  
*Pista*: mira dentro de la función probe, busca comparaciones de ID

**P21**: ¿Dónde se declara `DRIVER_MODULE`?  
*Pista*: busca `DRIVER_MODULE`, debería estar cerca del final del archivo

**P22**: ¿A qué bus se conecta este driver?  
*Pista*: mira el segundo parámetro de la macro `DRIVER_MODULE`

**P23**: Encuentra la tabla de métodos del dispositivo. ¿Cómo se llama?  
*Pista*: busca `device_method_t`, debería ser un array

**P24**: ¿Cuántos métodos están definidos en la tabla de métodos?  
*Pista*: cuenta las entradas entre la declaración y `DEVMETHOD_END`

### Comprueba tus respuestas

Cuando hayas completado todas las preguntas, compáralas con el solucionario que encontrarás a continuación. ¡No mires antes de intentarlo!

#### Parte 1: Driver null

**R1**: La definición de `null_cdevsw` (la tabla de conmutación de dispositivo de caracteres para `/dev/null`)

**R2**: La función `null_write`

**R3**: Pone `uio->uio_resid = 0` para marcar todos los bytes como consumidos y devuelve `0` (éxito). Los datos se descartan.

**R4**: `null_modevent()`, definida cerca del final de `null.c`, justo antes del registro con `DEV_MODULE`

**R5**: `DEV_MODULE(null, null_modevent, NULL);` seguido de `MODULE_VERSION(null, 1);`

**R6**: Tres nodos de dispositivo: `/dev/null`, `/dev/zero` y `/dev/full`

**R7**: "null", "zero", "full"

#### Parte 2: Driver LED

**R8**: `struct ledsc` (nota el nombre compacto "LED softc"; no `led_softc`)

**R9**: `led_create()` es un envoltorio delgado sobre `led_create_state()`; ambas conviven en `led.c`, justo después de la definición de `led_cdevsw`

**R10**: `/dev/led/` (los LEDs aparecen como `/dev/led/nombre`, creados con `make_dev(..., "led/%s", name)`)

**R11**: `led_write()` lee el buffer del usuario mediante `uiomove()`, lo pasa por `led_parse()` para convertir una cadena legible como `"f3"` o `"m-.-"` en un patrón compacto, y luego instala el patrón con `led_state()`.

**R12**: Ninguno de los dos. `led.c` es un subsistema de infraestructura (sin `probe`/`attach`, sin manejador de eventos del módulo). Se inicializa al arranque mediante `SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL)` cerca del final del archivo y no tiene un manejador de carga y descarga independiente; los drivers de hardware llaman a `led_create()`/`led_destroy()` para registrar sus LEDs en tiempo de ejecución.

**R13**: Sí, en `led_create_state()`: `sc = malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);`

#### Parte 3: Driver tun/tap

**R14**: `struct tuntap_softc`

**R15**: Sí. El softc embebe un puntero `ifnet` (`tun_ifp`) y está vinculado a un `cdev` mediante `dev->si_drv1` y el puntero de retorno del softc.

**R16**: No existe una única variable `tun_cdevsw`. Hay tres definiciones de `struct cdevsw` dentro del array `tuntap_drivers[]` (una para `tun`, otra para `tap` y otra para `vmnet`). Comparten los mismos manejadores (`tunopen`, `tunread`, `tunwrite`, `tunioctl`, `tunpoll`, `tunkqfilter`) y solo difieren en `.d_name` y las flags.

**R17**: `tunopen()` está asignada a `.d_open` en cada `cdevsw` dentro de `tuntap_drivers[]`.

**R18**: En `tuncreate()`, la interfaz se crea con `if_alloc(type)`, donde `type` es `IFT_ETHER` para `tap` y `IFT_PPP` para `tun`.

#### Parte 4: Driver UART por PCI

**R19**: `uart_pci_probe()`

**A20**: Llama a `uart_pci_match()` contra la tabla `pci_ns8250_ids` para identificar IDs de fabricante/dispositivo PCI conocidos, y recurre al código de clase PCI (`PCIC_SIMPLECOMM` con subclase `PCIS_SIMPLECOMM_UART`) para dispositivos genéricos de clase 16550.

**A21**: Al final del archivo: `DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);`

**A22**: `pci` (el segundo argumento de `DRIVER_MODULE`).

**A23**: `uart_pci_methods[]`

**A24**: Cuatro entradas más `DEVMETHOD_END`: `device_probe`, `device_attach`, `device_detach` y `device_resume`.

**Si tus respuestas difieren de forma significativa**:

1. ¡No te preocupes! El código de FreeBSD evoluciona entre versiones.
2. Lo importante es **encontrar** las estructuras, no los números de línea exactos.
3. Si encontraste patrones similares en ubicaciones distintas, eso también es un éxito.

### Criterios de éxito

- Has encontrado las principales estructuras en cada driver
- Comprendes el patrón: puntos de entrada (cdevsw/ifnet), ciclo de vida (probe/attach/detach), registro (DRIVER_MODULE/DECLARE_MODULE)
- Puedes navegar el código fuente de un driver con confianza
- Reconoces las diferencias entre tipos de driver (de caracteres frente a de red frente a conectados al bus)

### Lo que has aprendido

- Los **dispositivos de caracteres** utilizan estructuras `cdevsw` con funciones de punto de entrada
- Los **dispositivos de red** combinan dispositivos de caracteres (`cdev`) con interfaces de red (`ifnet`)
- Los **drivers conectados al bus** utilizan newbus (probe/attach/detach) y tablas de métodos
- Los **módulos de infraestructura** pueden omitir probe/attach si no son drivers de hardware
- Las **estructuras softc** almacenan el estado por dispositivo
- El **registro de módulos** varía (DECLARE_MODULE vs DRIVER_MODULE) según el tipo de driver

### Plantilla de entrada en el diario del laboratorio

```text
Lab 1 Complete: [Date]

Time taken: ___ minutes
Questions answered: 24/24

Most interesting discovery: 
[What surprised you most about real driver code?]

Challenging aspects:
[What was hard to find? Any patterns you didn't expect?]

Key insight:
[What "clicked" for you during this exploration?]

Next steps:
[Ready for Lab 2 where you'll build your first module]
```

## Laboratorio 2: módulo mínimo solo con mensajes de log

### Objetivo

Construye, carga y descarga tu primer módulo del kernel. Confirma que tu cadena de herramientas funciona correctamente y comprende el ciclo de vida del módulo mediante observación directa.

### Lo que aprenderás

- Cómo escribir un módulo del kernel mínimo
- Cómo crear un Makefile para construir módulos del kernel
- Cómo cargar y descargar módulos de forma segura
- Cómo observar mensajes del kernel en dmesg
- El ciclo de vida del event handler del módulo (load/unload)
- Cómo solucionar errores comunes de compilación

### Requisitos previos

- FreeBSD 14.3 con /usr/src instalado
- Herramientas de compilación instaladas (clang, make)
- Acceso sudo/root
- Haber completado el Laboratorio 1 (recomendado pero no obligatorio)

### Tiempo estimado

40-50 minutos (incluyendo compilación, pruebas y documentación)

### Instrucciones

#### Paso 1: crear el directorio de trabajo

```bash
% mkdir -p ~/drivers/hello
% cd ~/drivers/hello
```

**¿Por qué esta ubicación?**: Tu directorio home mantiene los experimentos con drivers separados de los archivos del sistema y sobrevive a los reinicios.

#### Paso 2: crear el driver mínimo

Crea un archivo llamado `hello.c`:

```bash
% vi hello.c   # or nano, emacs, your choice
```

Introduce el siguiente código (la explicación viene a continuación):

```c
/*
 * hello.c - Minimal FreeBSD kernel module for testing
 * 
 * This is the simplest possible kernel module: it does nothing except
 * print messages when loaded and unloaded. Perfect for verifying that
 * your build environment works correctly.
 *
 * FreeBSD 14.3 compatible
 */

#include <sys/param.h>      /* System parameter definitions */
#include <sys/module.h>     /* Kernel module definitions */
#include <sys/kernel.h>     /* Kernel types and macros */
#include <sys/systm.h>      /* System functions (printf) */

/*
 * Module event handler
 * 
 * This function is called whenever something happens to the module:
 * - MOD_LOAD: Module is being loaded into the kernel
 * - MOD_UNLOAD: Module is being removed from the kernel
 * - MOD_SHUTDOWN: System is shutting down (rare, usually not implemented)
 * - MOD_QUIESCE: Module should prepare for unload (advanced, not shown here)
 *
 * Parameters:
 *   mod: Module identifier (handle to this module)
 *   event: What's happening (MOD_LOAD, MOD_UNLOAD, etc.)
 *   arg: Extra data (usually NULL, not used here)
 *
 * Returns:
 *   0 on success
 *   Error code (like EOPNOTSUPP) on failure
 */
static int
hello_modevent(module_t mod __unused, int event, void *arg __unused)
{
    int error = 0;
    
    /*
     * The __unused attribute tells the compiler "I know these parameters
     * aren't used, don't warn me about it." It's good practice to mark
     * intentionally unused parameters.
     */
    
    switch (event) {
    case MOD_LOAD:
        /*
         * This runs when someone does 'kldload hello.ko'
         * 
         * printf() in kernel code goes to the kernel message buffer,
         * which you can see with 'dmesg' or in /var/log/messages.
         * 
         * Notice we say "Hello:" at the start - this helps identify
         * which module printed the message when reading logs.
         */
        printf("Hello: Module loaded successfully!\n");
        printf("Hello: This message appears in dmesg\n");
        printf("Hello: Module address: %p\n", (void *)&hello_modevent);
        break;
        
    case MOD_UNLOAD:
        /*
         * This runs when someone does 'kldunload hello'
         * 
         * This is where you'd clean up resources if this module
         * had allocated anything. Our minimal module has nothing
         * to clean up.
         */
        printf("Hello: Module unloaded. Goodbye!\n");
        break;
        
    default:
        /*
         * We don't handle other events (like MOD_SHUTDOWN).
         * Return EOPNOTSUPP ("operation not supported").
         */
        error = EOPNOTSUPP;
        break;
    }
    
    return (error);
}

/*
 * Module declaration structure
 * 
 * This tells the kernel about our module:
 * - name: "hello" (how it appears in kldstat)
 * - evhand: pointer to our event handler
 * - priv: private data (NULL for us, we have none)
 */
static moduledata_t hello_mod = {
    "hello",            /* module name */
    hello_modevent,     /* event handler function */
    NULL                /* extra data (not used) */
};

/*
 * DECLARE_MODULE macro
 * 
 * This is the magic that registers our module with FreeBSD.
 * 
 * Parameters:
 *   1. hello: Unique module identifier (matches name in moduledata_t)
 *   2. hello_mod: Our moduledata_t structure
 *   3. SI_SUB_DRIVERS: Subsystem order (we're a "driver" subsystem)
 *   4. SI_ORDER_MIDDLE: Load order within subsystem (middle of the pack)
 *
 * Load order matters when modules depend on each other. SI_SUB_DRIVERS
 * and SI_ORDER_MIDDLE are safe defaults for simple modules.
 */
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

/*
 * MODULE_VERSION macro
 * 
 * Declares the version of this module. Version numbers help the kernel
 * manage module dependencies and compatibility.
 * 
 * Format: MODULE_VERSION(name, version_number)
 * Version 1 is fine for new modules.
 */
MODULE_VERSION(hello, 1);
```

**Resumen de la explicación del código**:

- **Includes**: incluyen las cabeceras del kernel (a diferencia del espacio de usuario, no podemos usar `<stdio.h>`)
- **Event handler**: función que se llama cuando el módulo se carga o descarga
- **moduledata_t**: conecta el nombre del módulo con su event handler
- **DECLARE_MODULE**: registra todo con el kernel
- **MODULE_VERSION**: declara la versión para el seguimiento de dependencias

#### Paso 3: crear el Makefile

Crea un archivo llamado `Makefile` (nombre exacto, con M mayúscula):

```bash
% vi Makefile
```

Introduce este contenido:

```makefile
# Makefile for hello kernel module
#
# This Makefile uses FreeBSD's kernel module build infrastructure.
# The .include at the end does all the heavy lifting.

# KMOD: Kernel module name (will produce hello.ko)
KMOD=    hello

# SRCS: Source files to compile (just hello.c)
SRCS=    hello.c

# Include FreeBSD's kernel module build rules
# This single line gives you:
#   - 'make' or 'make all': Build the module
#   - 'make clean': Remove build artifacts
#   - 'make install': Install to /boot/modules (don't use in lab!)
#   - 'make load': Load the module (requires root)
#   - 'make unload': Unload the module (requires root)
.include <bsd.kmod.mk>
```

**Notas sobre el Makefile**:

- **Debe llamarse "Makefile"** (o "makefile", pero "Makefile" es la convención)
- **Los tabuladores importan**: si obtienes errores, comprueba que la indentación usa TABULADORES, no espacios
- **KMOD** determina el nombre del archivo de salida (`hello.ko`)
- **bsd.kmod.mk** es la infraestructura de construcción de módulos del kernel de FreeBSD (se encarga de lo complejo)

#### Paso 4: compilar el módulo

```bash
% make clean
rm -f hello.ko hello.o ... [various cleanup]

% make
cc -O2 -pipe -fno-strict-aliasing  -Werror -D_KERNEL -DKLD_MODULE ... -c hello.c
ld -d -warn-common -r -d -o hello.ko hello.o
```

**Qué está ocurriendo**:

1. **make clean**: elimina los artefactos de compilación anteriores (siempre es seguro ejecutarlo)
2. **make**: compila hello.c en hello.o y luego lo enlaza para crear hello.ko
3. Los flags del compilador (`-D_KERNEL -DKLD_MODULE`) indican al código que está en modo kernel

**Salida esperada**: deberías ver los comandos de compilación pero **ningún error**.

**Mensajes de error comunes**:

```text
Error: "implicit declaration of function 'printf'"
Fix: Check your includes - you need <sys/systm.h>

Error: "expected ';' before '}'"
Fix: Check for missing semicolons in your code

Error: "undefined reference to __something"
Fix: Usually means wrong includes or typo in function name
```

#### Paso 5: verificar que la compilación fue correcta

```bash
% ls -lh hello.ko
-rwxr-xr-x  1 youruser  youruser   14K Nov 14 15:30 hello.ko
```

**Qué buscar**:

- **El archivo existe**: `hello.ko` está presente
- **El tamaño es razonable**: entre 10 y 20 KB es típico para módulos mínimos
- **Bit de ejecución activo**: `-rwxr-xr-x` (la 'x' significa ejecutable)

#### Paso 6: cargar el módulo

```bash
% sudo kldload ./hello.ko
```

**Notas importantes**:

- **Debes usar sudo** (o ser root): solo root puede cargar módulos del kernel
- **Usa ./hello.ko**: el `./` indica a kldload que use el archivo local en lugar de buscar en las rutas del sistema
- **No tener salida es normal**: si se carga correctamente, kldload no imprime nada

**Si obtienes un error**:

```text
kldload: can't load ./hello.ko: module already loaded or in kernel
Solution: The module is already loaded. Unload it first: sudo kldunload hello

kldload: can't load ./hello.ko: Exec format error
Solution: Module was built for different FreeBSD version. Rebuild on target system.

kldload: an error occurred. Please check dmesg(8) for more details.
Solution: Run 'dmesg | tail' to see what went wrong
```

#### Paso 7: verificar que el módulo está cargado

```bash
% kldstat | grep hello
 5    1 0xffffffff82500000     3000 hello.ko
```

**Significado de las columnas**:

- **5**: ID del módulo (tu número puede ser diferente)
- **1**: recuento de referencias (cuántas cosas dependen de él)
- **0xffffffff82500000**: dirección de memoria del kernel donde está cargado el módulo
- **3000**: tamaño en hexadecimal (0x3000 = 12288 bytes = 12 KB)
- **hello.ko**: nombre del archivo del módulo

#### Paso 8: ver los mensajes del kernel

```bash
% dmesg | tail -5
Hello: Module loaded successfully!
Hello: This message appears in dmesg
Hello: Module address: 0xffffffff82500000
```

**¿Qué es dmesg?**: El buffer de mensajes del kernel. Todo lo que se imprime con `printf()` en el código del kernel va aquí.

**Formas alternativas de verlo**:

```bash
% dmesg | grep Hello
% tail -f /var/log/messages   # Watch in real-time (Ctrl+C to stop)
```

#### Paso 9: descargar el módulo

```bash
% sudo kldunload hello
```

**Qué ocurre**:

1. El kernel llama a tu `hello_modevent()` con `MOD_UNLOAD`
2. Tu manejador imprime "Goodbye!" y devuelve 0 (éxito)
3. El kernel elimina el módulo de la memoria

#### Paso 10: verificar los mensajes de descarga

```bash
% dmesg | tail -3
Hello: This message appears in dmesg
Hello: Module address: 0xffffffff82500000
Hello: Module unloaded. Goodbye!
```

#### Paso 11: confirmar que el módulo ha desaparecido

```bash
% kldstat | grep hello
[no output - module is unloaded]

% ls -l /dev/ | grep hello
[no output - this module doesn't create devices]
```

### Entre bastidores: ¿qué acaba de ocurrir?

Vamos a recorrer el **ciclo de vida completo** de tu módulo:

#### Cuando ejecutaste `kldload ./hello.ko`:

1. **El kernel carga el archivo**: lee hello.ko del disco en la memoria del kernel
2. **Reubicación**: ajusta las direcciones de memoria del código para que funcionen en la dirección de carga
3. **Resolución de símbolos**: conecta las llamadas a funciones con sus implementaciones
4. **Inicialización**: llama a tu `hello_modevent()` con `MOD_LOAD`
5. **Registro**: añade "hello" a la lista de módulos del kernel
6. **Completado**: kldload devuelve éxito (código de salida 0)

Las llamadas a `printf()` en `MOD_LOAD` ocurrieron durante el paso 4.

#### Cuando ejecutaste `kldunload hello`:

1. **Búsqueda**: encuentra el módulo "hello" en la lista de módulos del kernel
2. **Comprobación de referencias**: verifica que nada está usando el módulo (ref count = 1)
3. **Apagado**: llama a tu `hello_modevent()` con `MOD_UNLOAD`
4. **Limpieza**: lo elimina de la lista de módulos
5. **Desmapeo**: libera la memoria del kernel que contenía el código del módulo
6. **Completado**: kldunload devuelve éxito

Tu `printf()` en `MOD_UNLOAD` ocurrió durante el paso 3.

#### Por qué importan DECLARE_MODULE y MODULE_VERSION:

```c
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
```

Esta macro se expande en código que crea una estructura de datos especial en una sección ELF especial (la sección `.set`) del archivo hello.ko. Cuando el kernel carga el módulo, busca estas estructuras y así sabe:

- **Nombre**: "hello"
- **Manejador**: `hello_modevent`
- **Cuándo inicializar**: fase SI_SUB_DRIVERS, posición SI_ORDER_MIDDLE

¡Sin esta macro, el kernel no sabría que tu módulo existe!

### Guía de resolución de problemas

#### Problema: el módulo no compila

**Síntoma**: `make` muestra errores

**Causas comunes**:

1. **Error tipográfico en el código**: compara cuidadosamente con el ejemplo anterior
2. **Includes incorrectos**: comprueba que las cuatro líneas #include están presentes
3. **Tabuladores frente a espacios en el Makefile**: los Makefiles requieren TABULADORES para la indentación
4. **Falta /usr/src**: la compilación necesita las cabeceras del kernel de /usr/src

**Pasos de depuración**:

```bash
# Check if /usr/src exists
% ls /usr/src/sys/sys/param.h
[should exist]

# Try compiling manually to see better errors
% cc -c -D_KERNEL -I/usr/src/sys hello.c
```

#### Problema: "Operation not permitted" al cargar

**Síntoma**: `kldload: can't load ./hello.ko: Operation not permitted`

**Causa**: no se está ejecutando como root

**Solución**:

```bash
% sudo kldload ./hello.ko
# OR
% su
# kldload ./hello.ko
```

#### Problema: "module already loaded"

**Síntoma**: `kldload: can't load ./hello.ko: module already loaded`

**Causa**: el módulo ya está en el kernel

**Solución**:

```bash
% sudo kldunload hello
% sudo kldload ./hello.ko
```

#### Problema: no hay mensajes en dmesg

**Síntoma**: `kldload` tiene éxito pero `dmesg` no muestra nada

**Posibles causas**:

1. **Los mensajes han desaparecido por scroll**: usa `dmesg | tail -20` para ver los mensajes recientes
2. **Módulo incorrecto cargado**: comprueba `kldstat` para verificar que tu módulo está ahí
3. **Event handler no llamado**: comprueba que DECLARE_MODULE coincide con el nombre de moduledata_t

#### Problema: kernel panic

**Síntoma**: el sistema falla y muestra un mensaje de pánico

**Poco probable con este módulo mínimo**, pero si ocurre:

1. **No entres en pánico** (sin juego de palabras): tu VM puede reiniciarse
2. **Revisa el código**: probablemente hay un error tipográfico en la macro DECLARE_MODULE
3. **Empieza de cero**: reinicia la VM y compara tu código carácter a carácter con el ejemplo

### Criterios de éxito

- El módulo compila sin errores ni advertencias
- El archivo `hello.ko` se crea (entre 10 y 20 KB)
- El módulo se carga sin errores
- Aparecen mensajes en dmesg que muestran la carga
- El módulo aparece en la salida de `kldstat`
- El módulo se descarga correctamente
- El mensaje de descarga aparece en dmesg
- No hay kernel panics ni bloqueos

### Lo que has aprendido

**Habilidades técnicas**:

- Escribir la estructura de un módulo del kernel mínimo
- Usar el sistema de construcción de módulos del kernel de FreeBSD
- Cargar y descargar módulos del kernel de forma segura
- Observar mensajes del kernel con dmesg

**Conceptos**:

- Event handlers de módulo (ciclo de vida MOD_LOAD/MOD_UNLOAD)
- Las macros DECLARE_MODULE y MODULE_VERSION
- `printf` del kernel frente a `printf` del espacio de usuario
- Por qué se requiere acceso root para las operaciones con módulos

**Confianza adquirida**:

- Tu entorno de compilación funciona correctamente
- Puedes compilar y cargar código del kernel
- Comprendes el ciclo de vida básico del módulo
- Estás listo para añadir funcionalidad real (Laboratorio 3)

### Plantilla de entrada en el diario del laboratorio

```text
Lab 2 Complete: [Date]

Time taken: ___ minutes

Build results:
- First attempt: [ ] Success  [ ] Errors (describe: ___)
- After fixes: [ ] Success

Module operations:
- Load: [ ] Success  [ ] Errors
- Visible in kldstat: [ ] Yes  [ ] No
- Messages in dmesg: [ ] Yes  [ ] No
- Unload: [ ] Success  [ ] Errors

Key insight:
[What did you learn about the kernel module lifecycle?]

Challenges faced:
[What went wrong? How did you fix it?]

Next steps:
[Ready for Lab 3: adding real functionality with device nodes]
```

### Experimento opcional: orden de carga de módulos

¿Quieres ver por qué importan SI_SUB y SI_ORDER?

1. **Comprueba el orden de arranque actual**:

```bash
% kldstat -v | less
```

2. **Prueba diferentes órdenes de subsistema**:
   Edita hello.c y cambia:

```c
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
```

a:

```c
DECLARE_MODULE(hello, hello_mod, SI_SUB_PSEUDO, SI_ORDER_FIRST);
```

Vuelve a compilar y recarga. ¡El módulo sigue funcionando! El orden solo importa cuando los módulos dependen entre sí.

## Laboratorio 3: crear y eliminar un nodo de dispositivo

### Objetivo

Extiende el módulo mínimo para crear una entrada en `/dev` con la que los usuarios puedan interactuar. Implementa operaciones básicas de lectura y escritura.

### Lo que aprenderás

- Cómo crear un nodo de dispositivo de caracteres en `/dev`
- Cómo implementar los puntos de entrada de cdevsw (character device switch)
- Cómo copiar datos de forma segura entre el espacio de usuario y el espacio del kernel con `uiomove()`
- Cómo las syscalls open/close/read/write se conectan a las funciones de tu driver
- La relación entre struct cdev, cdevsw y las operaciones del dispositivo
- Limpieza adecuada de recursos y seguridad ante punteros NULL

### Requisitos previos

- Haber completado el Laboratorio 2 (módulo Hello)
- Comprensión de las operaciones de archivo (open, read, write, close)
- Conocimientos básicos de manejo de cadenas en C

### Tiempo estimado

60-75 minutos (incluyendo la comprensión del código, la compilación y las pruebas exhaustivas)

### Instrucciones

#### Paso 1: crear un nuevo directorio de trabajo

```bash
% mkdir -p ~/drivers/demo
% cd ~/drivers/demo
```

**¿Por qué un nuevo directorio?**: mantén cada laboratorio autocontenido para poder consultarlo fácilmente después.

#### Paso 2: crear el código fuente del driver

Crea `demo.c` con el siguiente código completo:

```c
/*
 * demo.c - Simple character device with /dev node
 * 
 * This driver demonstrates:
 * - Creating a device node in /dev
 * - Implementing open/close/read/write operations
 * - Safe data transfer between kernel and user space
 * - Proper resource management and cleanup
 *
 * Compatible with FreeBSD 14.3
 */

#include <sys/param.h>      /* System parameters and limits */
#include <sys/module.h>     /* Kernel module support */
#include <sys/kernel.h>     /* Kernel types */
#include <sys/systm.h>      /* System functions like printf */
#include <sys/conf.h>       /* Character device configuration */
#include <sys/uio.h>        /* User I/O structures and uiomove() */
#include <sys/malloc.h>     /* Kernel memory allocation */

/*
 * Global device node pointer
 * 
 * This holds the handle to our /dev/demo entry. We need to keep this
 * so we can destroy the device when the module unloads.
 * 
 * NULL when module is not loaded.
 */
static struct cdev *demo_dev = NULL;

/*
 * Open handler - called when someone opens /dev/demo
 * 
 * This is called every time a process opens the device file:
 *   open("/dev/demo", O_RDWR);
 *   cat /dev/demo
 *   echo "hello" > /dev/demo
 * 
 * Parameters:
 *   dev: Device being opened (our cdev structure)
 *   oflags: Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_NONBLOCK, etc.)
 *   devtype: Device type (usually S_IFCHR for character devices)
 *   td: Thread opening the device (process context)
 * 
 * Returns:
 *   0 on success
 *   Error code (like EBUSY, ENOMEM) on failure
 * 
 * Note: The __unused attribute marks parameters we don't use, avoiding
 *       compiler warnings.
 */
static int
demo_open(struct cdev *dev __unused, int oflags __unused,
          int devtype __unused, struct thread *td __unused)
{
    /*
     * In a real driver, you might:
     * - Check if exclusive access is required
     * - Allocate per-open state
     * - Initialize hardware
     * - Check device readiness
     * 
     * Our simple demo just logs that open happened.
     */
    printf("demo: Device opened (pid=%d, comm=%s)\n", 
           td->td_proc->p_pid, td->td_proc->p_comm);
    
    return (0);  /* Success */
}

/*
 * Close handler - called when last reference is closed
 * 
 * Important: This is called when the LAST file descriptor referring to
 * this device is closed. If a process opens /dev/demo twice, close is
 * called only after both fds are closed.
 * 
 * Parameters:
 *   dev: Device being closed
 *   fflag: File flags from the open call
 *   devtype: Device type
 *   td: Thread closing the device
 * 
 * Returns:
 *   0 on success
 *   Error code on failure
 */
static int
demo_close(struct cdev *dev __unused, int fflag __unused,
           int devtype __unused, struct thread *td __unused)
{
    /*
     * In a real driver, you might:
     * - Free per-open state
     * - Flush buffers
     * - Update hardware state
     * - Cancel pending operations
     */
    printf("demo: Device closed (pid=%d)\n", td->td_proc->p_pid);
    
    return (0);  /* Success */
}

/*
 * Read handler - transfer data from kernel to user space
 * 
 * This is called when someone reads from the device:
 *   cat /dev/demo
 *   dd if=/dev/demo of=output.txt bs=1024 count=1
 *   read(fd, buffer, size);
 * 
 * Parameters:
 *   dev: Device being read from
 *   uio: User I/O structure describing the read request
 *   ioflag: I/O flags (IO_NDELAY for non-blocking, etc.)
 * 
 * The 'uio' structure contains:
 *   uio_resid: Bytes remaining to transfer (initially = read size)
 *   uio_offset: Current position in the "file" (we ignore this)
 *   uio_rw: Direction (UIO_READ for read operations)
 *   uio_td: Thread performing the I/O
 *   [internal]: Scatter-gather list describing user buffer(s)
 * 
 * Returns:
 *   0 on success
 *   Error code (like EFAULT if user buffer is invalid)
 */
static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
    /*
     * Our message to return to user space.
     * Could be device data, sensor readings, status info, etc.
     */
    char message[] = "Hello from demo driver!\n";
    size_t len;
    int error;
    
    /*
     * Log the read request details.
     * uio_resid tells us how many bytes the user wants to read.
     */
    printf("demo: Read called, uio_resid=%zd bytes requested\n", 
           uio->uio_resid);
    
    /*
     * Calculate how many bytes to actually transfer.
     * 
     * We use MIN() to transfer the smaller of:
     * 1. What the user requested (uio_resid)
     * 2. What we have available (sizeof(message)-1, excluding null terminator)
     * 
     * Why -1? The null terminator '\0' is for C string handling in the
     * kernel, but we don't send it to user space. Text files don't have
     * null terminators between lines.
     */
    len = MIN(uio->uio_resid, sizeof(message) - 1);
    
    /*
     * uiomove() - The safe way to copy data to user space
     * 
     * This function:
     * 1. Verifies the user's buffer is valid and writable
     * 2. Copies 'len' bytes from 'message' to the user's buffer
     * 3. Automatically updates uio->uio_resid (subtracts len)
     * 4. Handles scatter-gather buffers (if user buffer is non-contiguous)
     * 5. Returns error if user buffer is invalid (EFAULT)
     * 
     * CRITICAL SAFETY RULE:
     * Never use memcpy(), bcopy(), or direct pointer access for user data!
     * User pointers are in user space, not accessible in kernel space.
     * uiomove() safely bridges this gap.
     * 
     * Parameters:
     *   message: Source data (kernel space)
     *   len: Bytes to copy
     *   uio: Destination description (user space)
     * 
     * After uiomove() succeeds:
     *   uio->uio_resid is decreased by len
     *   uio->uio_offset is increased by len (for seekable devices)
     */
    error = uiomove(message, len, uio);
    
    if (error != 0) {
        printf("demo: Read failed, error=%d\n", error);
        return (error);
    }
    
    printf("demo: Read completed, transferred %zu bytes\n", len);
    
    /*
     * Return 0 for success.
     * The caller knows how much we transferred by checking how much
     * uio_resid decreased.
     */
    return (0);
}

/*
 * Write handler - receive data from user space
 * 
 * This is called when someone writes to the device:
 *   echo "hello" > /dev/demo
 *   dd if=input.txt of=/dev/demo bs=1024
 *   write(fd, buffer, size);
 * 
 * Parameters:
 *   dev: Device being written to
 *   uio: User I/O structure describing the write request
 *   ioflag: I/O flags
 * 
 * Returns:
 *   0 on success (usually - see note below)
 *   Error code on failure
 * 
 * IMPORTANT WRITE SEMANTICS:
 * Unlike read(), write() is expected to consume ALL the data.
 * If you don't consume everything (uio_resid > 0 after return),
 * the kernel will call write() again with the remaining data.
 * This can cause infinite loops if you always return 0 with resid > 0!
 */
static int
demo_write(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
    char buffer[128];  /* Temporary buffer for incoming data */
    size_t len;
    int error;
    
    /*
     * Limit transfer size to our buffer size.
     * 
     * We use sizeof(buffer)-1 to reserve space for null terminator
     * (so we can safely print the string).
     * 
     * Note: Real drivers might:
     * - Accept unlimited data (loop calling uiomove)
     * - Have larger buffers
     * - Queue data for processing
     * - Return EFBIG if data exceeds device capacity
     */
    len = MIN(uio->uio_resid, sizeof(buffer) - 1);
    
    /*
     * uiomove() for write: Copy FROM user space TO kernel buffer
     * 
     * Same function, but now we're the destination.
     * The direction is determined by uio->uio_rw internally.
     */
    error = uiomove(buffer, len, uio);
    if (error != 0) {
        printf("demo: Write failed during uiomove, error=%d\n", error);
        return (error);
    }
    
    /*
     * Add null terminator so we can safely use printf.
     * 
     * SECURITY NOTE: In a real driver, you must validate data!
     * - Check for null bytes if expecting text
     * - Validate ranges for numeric data
     * - Sanitize before using in format strings
     * - Never trust user input
     */
    buffer[len] = '\0';
    
    /*
     * Do something with the data.
     * 
     * Real drivers might:
     * - Send to hardware (network packet, disk write, etc.)
     * - Process commands (like LED control strings)
     * - Update device state
     * - Queue for async processing
     * 
     * We just log it.
     */
    printf("demo: User wrote %zu bytes: \"%s\"\n", len, buffer);
    
    /*
     * Return success.
     * 
     * At this point, uio->uio_resid should be 0 (we consumed everything).
     * If not, the kernel will call us again with the remainder.
     */
    return (0);
}

/*
 * Character device switch (cdevsw) structure
 * 
 * This is the "method table" that connects system calls to your functions.
 * When a user process calls open(), read(), write(), etc. on /dev/demo,
 * the kernel looks up this table to find which function to call.
 * 
 * Think of it as a virtual function table (vtable) in OOP terms.
 */
static struct cdevsw demo_cdevsw = {
    .d_version =    D_VERSION,      /* ABI version - always required */
    .d_open =       demo_open,      /* open() syscall handler */
    .d_close =      demo_close,     /* close() syscall handler */
    .d_read =       demo_read,      /* read() syscall handler */
    .d_write =      demo_write,     /* write() syscall handler */
    .d_name =       "demo",         /* Device name for identification */
    
    /*
     * Other possible entries (not used here):
     * 
     * .d_ioctl =   demo_ioctl,   // ioctl() for configuration/control
     * .d_poll =    demo_poll,    // poll()/select() for readiness
     * .d_mmap =    demo_mmap,    // mmap() for direct memory access
     * .d_strategy= demo_strategy,// For block devices (legacy)
     * .d_kqfilter= demo_kqfilter,// kqueue event notification
     * 
     * Unimplemented entries default to NULL and return ENODEV.
     */
};

/*
 * Module event handler
 * 
 * This is called on module load and unload.
 * We create our device node on load, destroy it on unload.
 */
static int
demo_modevent(module_t mod __unused, int event, void *arg __unused)
{
    int error = 0;
    
    switch (event) {
    case MOD_LOAD:
        /*
         * make_dev() - Create a device node in /dev
         * 
         * This is the key function that makes your driver visible
         * to user space. It creates an entry in the devfs filesystem.
         * 
         * Parameters:
         *   &demo_cdevsw: Pointer to our method table
         *   0: Unit number (minor number) - use 0 for single-instance devices
         *   UID_ROOT: Owner user ID (0 = root)
         *   GID_WHEEL: Owner group ID (0 = wheel group)
         *   0666: Permissions (rw-rw-rw- = world read/write)
         *   "demo": Device name (appears as /dev/demo)
         * 
         * Returns:
         *   Pointer to cdev structure on success
         *   NULL on failure (rare - usually only if name collision)
         * 
         * The returned cdev is an opaque handle representing the device.
         */
        demo_dev = make_dev(&demo_cdevsw, 
                           0,              /* unit number */
                           UID_ROOT,       /* owner UID */
                           GID_WHEEL,      /* owner GID */
                           0666,           /* permissions: rw-rw-rw- */
                           "demo");        /* device name */
        
        /*
         * Always check if make_dev() succeeded.
         * Failure is rare but possible.
         */
        if (demo_dev == NULL) {
            printf("demo: Failed to create device node\n");
            return (ENXIO);  /* "Device not configured" */
        }
        
        printf("demo: Device /dev/demo created successfully\n");
        printf("demo: Permissions: 0666 (world readable/writable)\n");
        printf("demo: Try: cat /dev/demo\n");
        printf("demo: Try: echo \"test\" > /dev/demo\n");
        break;
        
    case MOD_UNLOAD:
        /*
         * Cleanup on module unload.
         * 
         * CRITICAL ORDERING:
         * 1. Make device invisible (destroy_dev)
         * 2. Wait for all operations to complete
         * 3. Free resources
         * 
         * destroy_dev() does steps 1 and 2 automatically!
         */
        
        /*
         * Always check for NULL before destroying.
         * This protects against:
         * - MOD_LOAD failure (demo_dev never created)
         * - Double-unload attempts
         * - Corrupted state
         */
        if (demo_dev != NULL) {
            /*
             * destroy_dev() - Remove device node and clean up
             * 
             * This function:
             * 1. Removes /dev/demo from the filesystem
             * 2. Marks device as "going away"
             * 3. WAITS for all in-progress operations to complete
             * 4. Ensures no new operations can start
             * 5. Frees associated kernel resources
             * 
             * SYNCHRONIZATION GUARANTEE:
             * After destroy_dev() returns, no threads are executing
             * your open/close/read/write functions. This makes cleanup
             * safe - no race conditions with active I/O.
             * 
             * This is why you can safely unload modules while they're
             * in use (e.g., someone has the device open). The unload
             * will wait until they close it.
             */
            destroy_dev(demo_dev);
            
            /*
             * Set pointer to NULL for safety.
             * 
             * Defense in depth: If something accidentally tries to
             * use demo_dev after unload, NULL pointer dereference
             * is much easier to debug than use-after-free.
             */
            demo_dev = NULL;
            
            printf("demo: Device /dev/demo destroyed\n");
        }
        break;
        
    default:
        /*
         * We don't handle MOD_SHUTDOWN or other events.
         */
        error = EOPNOTSUPP;
        break;
    }
    
    return (error);
}

/*
 * Module declaration - connects everything together
 */
static moduledata_t demo_mod = {
    "demo",           /* Module name */
    demo_modevent,    /* Event handler */
    NULL              /* Extra data */
};

/*
 * Register module with kernel
 */
DECLARE_MODULE(demo, demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

/*
 * Declare module version
 */
MODULE_VERSION(demo, 1);
```

**Conceptos clave en este código**:

1. **Estructura cdevsw**: la tabla de despacho que conecta las syscalls con tus funciones
2. **uiomove()**: transferencia segura de datos entre el kernel y el espacio de usuario (¡nunca uses memcpy!)
3. **make_dev()**: crea una entrada visible en /dev
4. **destroy_dev()**: elimina el dispositivo y espera a que las operaciones en curso terminen
5. **Seguridad ante NULL**: comprueba siempre los punteros antes de usarlos y ponlos a NULL tras liberarlos

#### Paso 3: crear el Makefile

Crea el `Makefile`:

```makefile
# Makefile for demo character device driver

KMOD=    demo
SRCS=    demo.c

.include <bsd.kmod.mk>
```

#### Paso 4: compilar el driver

```bash
% make clean
rm -f demo.ko demo.o ...

% make
cc -O2 -pipe -fno-strict-aliasing -Werror -D_KERNEL ... -c demo.c
ld -d -warn-common -r -d -o demo.ko demo.o
```

**Esperado**: compilación limpia sin errores.

**Si ves advertencias sobre parámetros no utilizados**: es normal; los hemos marcado con `__unused` pero algunas versiones del compilador siguen avisando.

#### Paso 5: cargar el driver

```bash
% sudo kldload ./demo.ko

% dmesg | tail -5
demo: Device /dev/demo created successfully
demo: Permissions: 0666 (world readable/writable)
demo: Try: cat /dev/demo
demo: Try: echo "test" > /dev/demo
```

#### Paso 6: verificar la creación del nodo de dispositivo

```bash
% ls -l /dev/demo
crw-rw-rw-  1 root  wheel  0x5e Nov 14 16:00 /dev/demo
```

**Qué estás viendo**:

- **c**: dispositivo de caracteres (no un dispositivo de bloques ni un archivo normal)
- **rw-rw-rw-**: permisos 0666 (cualquiera puede leer y escribir)
- **root wheel**: propiedad de root, grupo wheel
- **0x5e**: número de dispositivo (major/minor combinados; tu valor puede ser diferente)
- **/dev/demo**: la ruta del dispositivo

#### Paso 7: probar la lectura

```bash
% cat /dev/demo
Hello from demo driver!
```

**Qué ha ocurrido**:

1. `cat` abrió /dev/demo -> se llamó a `demo_open()`
2. `cat` llamó a `read()` -> se llamó a `demo_read()`
3. El driver copió "Hello from demo driver!\\n" al buffer de cat mediante `uiomove()`
4. `cat` imprimió los datos recibidos en stdout
5. `cat` cerró el archivo -> se llamó a `demo_close()`

**Comprueba el log del kernel**:

```bash
% dmesg | tail -5
demo: Device opened (pid=1234, comm=cat)
demo: Read called, uio_resid=65536 bytes requested
demo: Read completed, transferred 25 bytes
demo: Device closed (pid=1234)
```

**Nota**: `uio_resid=65536` significa que cat solicitó 64 KB (su buffer por defecto). Solo enviamos 25 bytes, lo que es perfectamente correcto: read() devuelve cuántos bytes se transfirieron realmente.

#### Paso 8: probar la escritura

```bash
% echo "Test message" > /dev/demo

% dmesg | tail -4
demo: Device opened (pid=1235, comm=sh)
demo: User wrote 13 bytes: "Test message
"
demo: Device closed (pid=1235)
```

**Qué ocurrió**:

1. El shell abrió `/dev/demo` para escritura
2. `echo` escribió "Test message\\n" (13 bytes incluyendo el salto de línea)
3. El driver lo recibió a través de `uiomove()` y lo registró
4. El shell cerró el dispositivo

#### Paso 9: Probar múltiples operaciones

```bash
% (cat /dev/demo; echo "Another test" > /dev/demo; cat /dev/demo)
Hello from demo driver!
Hello from demo driver!
```

**Observa dmesg en otro terminal**:

```bash
% dmesg -w    # Watch mode - updates in real-time
...
demo: Device opened (pid=1236, comm=sh)
demo: Read called, uio_resid=65536 bytes requested
demo: Read completed, transferred 25 bytes
demo: Device closed (pid=1236)
demo: Device opened (pid=1237, comm=sh)
demo: User wrote 13 bytes: "Another test
"
demo: Device closed (pid=1237)
demo: Device opened (pid=1238, comm=sh)
demo: Read called, uio_resid=65536 bytes requested
demo: Read completed, transferred 25 bytes
demo: Device closed (pid=1238)
```

#### Paso 10: Probar con dd (I/O controlada)

```bash
% dd if=/dev/demo bs=10 count=1 2>/dev/null
Hello from

% dd if=/dev/demo bs=100 count=1 2>/dev/null
Hello from demo driver!
```

**Qué muestra esto**:

- Primer dd: solicitó 10 bytes, recibió 10 bytes ("Hello from")
- Segundo dd: solicitó 100 bytes, recibió 25 bytes (nuestro mensaje completo)
- El driver respeta el tamaño solicitado a través de `uio_resid`

#### Paso 11: Verificar la protección contra descarga

**Abre el dispositivo y mantenlo abierto**:

```bash
% (sleep 30; echo "Done") > /dev/demo &
[1] 1240
```

**Ahora intenta descargarlo** (dentro de la misma ventana de 30 segundos):

```bash
% sudo kldunload demo
[hangs... waiting...]
```

**Pasados 30 segundos**:

```text
Done
demo: Device closed (pid=1240)
demo: Device /dev/demo destroyed
[kldunload completes]
```

**Qué ocurrió**: `destroy_dev()` esperó a que la operación de escritura se completara antes de permitir la descarga. Se trata de una característica de seguridad CRÍTICA que evita cuelgues al descargar código que todavía se está ejecutando.

#### Paso 12: Limpieza final

```bash
% sudo kldunload demo    # If still loaded
% ls -l /dev/demo
ls: /dev/demo: No such file or directory  # Good - it's gone
```

### Entre bastidores: el recorrido completo

Sigamos el camino de `cat /dev/demo` desde la shell hasta el driver y de vuelta:

#### 1. La shell ejecuta cat

```text
User space:
  Shell forks, execs /bin/cat with argument "/dev/demo"
```

#### 2. cat abre el archivo

```text
User space:
  cat: fd = open("/dev/demo", O_RDONLY);

Kernel:
   ->  VFS layer: Lookup "/dev/demo" in devfs
   ->  devfs: Find cdev structure (created by make_dev)
   ->  devfs: Allocate file descriptor, file structure
   ->  devfs: Call cdev->si_devsw->d_open (demo_open)
  
Kernel (in demo_open):
   ->  printf("Device opened...")
   ->  return 0 (success)
  
Kernel:
   ->  Return file descriptor to cat
  
User space:
  cat: fd = 3 (success)
```

#### 3. cat lee datos

```text
User space:
  cat: n = read(fd, buffer, 65536);

Kernel:
   ->  VFS: Lookup file descriptor 3
   ->  VFS: Find associated cdev
   ->  VFS: Allocate and initialize uio structure:
      uio_rw = UIO_READ
      uio_resid = 65536 (requested size)
      uio_offset = 0
      [iovec array pointing to cat's buffer]
   ->  VFS: Call cdev->si_devsw->d_read (demo_read)
  
Kernel (in demo_read):
   ->  printf("Read called, uio_resid=65536...")
   ->  len = MIN(65536, 24)  # We have 25 bytes (24 + null)
   ->  uiomove("Hello from demo driver!\n", 24, uio)
       ->  Copy 24 bytes from kernel message[] to cat's buffer
       ->  Update uio_resid: 65536 - 24 = 65512
   ->  printf("Read completed, transferred 24 bytes")
   ->  return 0
  
Kernel:
   ->  Calculate transferred = (original resid - final resid) = 24
   ->  Return 24 to cat
  
User space:
  cat: n = 24 (got 24 bytes)
```

#### 4. cat procesa los datos

```text
User space:
  cat: write(STDOUT_FILENO, buffer, 24);
  [Your terminal shows: Hello from demo driver!]
```

#### 5. cat intenta leer más

```text
User space:
  cat: n = read(fd, buffer, 65536);  # Try to read more
  
Kernel:
   ->  Call demo_read again
   ->  uiomove returns 24 bytes again (we always return same message)
  
User space:
  cat: n = 24
  cat: write(STDOUT_FILENO, buffer, 24);
  [Would print again, but cat knows this is a device not a file]
```

En realidad, `cat` seguirá leyendo hasta que reciba 0 bytes (EOF). Nuestro driver nunca devuelve 0, por lo que `cat` se quedaría bloqueado. Sin embargo, en la práctica cat agota el tiempo de espera o tú pulsas Ctrl+C.

**Implementación de read() más adecuada** para un comportamiento similar al de un archivo:

```c
static size_t bytes_sent = 0;  /* Track position */

static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
    char message[] = "Hello from demo driver!\n";
    size_t len;
    
    /* If we already sent the message, return 0 (EOF) */
    if (bytes_sent >= sizeof(message) - 1) {
        bytes_sent = 0;  /* Reset for next open */
        return (0);  /* EOF */
    }
    
    len = MIN(uio->uio_resid, sizeof(message) - 1 - bytes_sent);
    uiomove(message + bytes_sent, len, uio);
    bytes_sent += len;
    
    return (0);
}
```

Para nuestro demo, la versión sencilla es suficiente.

#### 6. cat cierra el archivo

```text
User space:
  cat: close(fd);
  
Kernel:
   ->  VFS: Decrement file reference count
   ->  VFS: If last reference, call cdev->si_devsw->d_close (demo_close)
  
Kernel (in demo_close):
   ->  printf("Device closed...")
   ->  return 0
  
Kernel:
   ->  Free file descriptor
   ->  Return to cat
  
User space:
  cat: exit(0)
```

### Análisis en profundidad: ¿por qué uiomove()?

**Pregunta**: ¿Por qué no podemos usar simplemente `memcpy()` o acceso directo por puntero?

**Respuesta**: El espacio de usuario y el espacio del kernel tienen **espacios de direcciones separados**.

#### Separación de espacios de direcciones:

```text
User space (cat process):
  Address 0x1000: cat's buffer[0]
  Address 0x1001: cat's buffer[1]
  ...
  
Kernel space:
  Address 0x1000: DIFFERENT memory (maybe page tables)
  Address 0x1001: DIFFERENT memory
```

Un puntero válido en espacio de usuario (como el buffer de cat en `0x1000`) **no tiene ningún significado** en el espacio del kernel. Si intentas:

```c
/* WRONG - WILL CRASH */
char *user_buf = (char *)0x1000;  /* User's buffer address */
strcpy(user_buf, "data");  /* KERNEL PANIC! */
```

El kernel intentará escribir en la dirección `0x1000` del *espacio de direcciones del kernel*, que es una memoria completamente distinta. En el mejor de los casos, corrompes datos del kernel. En el peor, provocas un pánico inmediato.

#### Qué hace uiomove():

1. **Valida**: Comprueba que las direcciones de usuario pertenecen realmente al espacio de usuario.
2. **Mapea**: Mapea temporalmente las páginas del usuario en el espacio de direcciones del kernel.
3. **Copia**: Realiza la copia usando direcciones del kernel válidas.
4. **Desmapea**: Limpia el mapeo temporal.
5. **Gestiona fallos**: Si el buffer del usuario no es válido, devuelve EFAULT.

Por eso **todo driver debe usar `uiomove()`, `copyin()` o `copyout()`** para transferir datos con el usuario. El acceso directo siempre es incorrecto y peligroso.

### Criterios de éxito

- El driver compila sin errores.
- El módulo se carga correctamente.
- El nodo de dispositivo `/dev/demo` aparece con los permisos correctos.
- Se puede leer del dispositivo (se recibe el mensaje).
- Se puede escribir en el dispositivo (el mensaje se registra en dmesg).
- Las operaciones aparecen en dmesg con los PID correctos.
- El módulo se puede descargar limpiamente.
- El nodo de dispositivo desaparece tras la descarga.
- La descarga espera a que las operaciones en curso finalicen (comprobado con el experimento de sleep).
- No se producen pánicos del kernel ni cuelgues.

### Lo que has aprendido

**Habilidades técnicas**:

- Crear nodos de dispositivo de caracteres con `make_dev()`.
- Implementar la tabla de métodos cdevsw.
- Transferencia segura de datos entre usuario y kernel con `uiomove()`.
- Limpieza correcta de recursos con `destroy_dev()`.
- Depuración con `printf()` y dmesg.

**Conceptos**:

- Cómo se traducen las syscalls (open/read/write/close) en funciones del driver.
- El papel de cdevsw como tabla de despacho.
- Por qué es necesario `uiomove()` (separación de espacios de direcciones).
- Cómo `destroy_dev()` proporciona sincronización.
- La relación entre cdev, devfs y las entradas de `/dev`.

**Buenas prácticas**:

- Comprobar siempre el valor de retorno de `make_dev()`.
- Comprobar siempre si el puntero es NULL antes de llamar a `destroy_dev()`.
- Asignar NULL a los punteros después de liberar.
- Usar `MIN()` para evitar desbordamientos de buffer.
- Registrar las operaciones en el log para facilitar la depuración.

### Errores frecuentes y cómo evitarlos

#### Error 1: usar memcpy() en lugar de uiomove()

**Incorrecto**:

```c
memcpy(user_buffer, kernel_data, size);  /* CRASH! */
```

**Correcto**:

```c
uiomove(kernel_data, size, uio);  /* Safe */
```

#### Error 2: no consumir todos los datos de escritura

**Incorrecto**:

```c
demo_write(...) {
    /* Only process part of the data */
    uiomove(buffer, 10, uio);
    return (0);  /* BUG: uio_resid is not 0! */
}
```

**Consecuencia**: El kernel vuelve a llamar a `demo_write()` con los datos restantes, lo que provoca un bucle infinito.

**Correcto**:

```c
demo_write(...) {
    /* Process ALL data */
    len = MIN(uio->uio_resid, buffer_size);
    uiomove(buffer, len, uio);
    /* Now uio_resid = 0, or we return EFBIG if too much */
    return (0);
}
```

#### Error 3: olvidar la comprobación de NULL antes de destroy_dev()

**Incorrecto**:

```c
MOD_UNLOAD:
    destroy_dev(demo_dev);  /* What if make_dev failed? */
```

**Correcto**:

```c
MOD_UNLOAD:
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);
        demo_dev = NULL;
    }
```

#### Error 4: permisos incorrectos en el nodo de dispositivo

Si usas permisos `0600`:

```c
make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "demo");
```

Los usuarios normales no podrán acceder:

```bash
% cat /dev/demo
cat: /dev/demo: Permission denied
```

Usa `0666` para dispositivos accesibles por todos (apropiado para aprendizaje y pruebas).

### Plantilla para el cuaderno de laboratorio

```text
Lab 3 Complete: [Date]

Time taken: ___ minutes

Build results:
- Compilation: [ ] Success  [ ] Errors
- Module size: ___ KB

Testing results:
- Device node created: [ ] Yes  [ ] No
- Permissions correct: [ ] Yes  [ ] No (expected: crw-rw-rw-)
- Read test: [ ] Success  [ ] Failed
- Write test: [ ] Success  [ ] Failed
- Multiple operations: [ ] Success  [ ] Failed
- Unload protection: [ ] Tested  [ ] Not tested

Key insight:
[What did you learn about user-kernel data transfer?]

Most interesting discovery:
[What surprised you? Maybe how destroy_dev waits?]

Challenges faced:
[Any build errors? Runtime issues? How did you resolve them?]

Code understanding:
- uiomove() purpose: [Explain in your own words]
- cdevsw role: [Explain in your own words]
- Why NULL checks matter: [Explain in your own words]

Next steps:
[Ready for Lab 4: deliberate bugs and error handling]
```

## Laboratorio 4: gestión de errores y programación defensiva

### Objetivo

Aprender a gestionar errores introduciendo deliberadamente bugs, observando sus síntomas y corrigiéndolos correctamente. Desarrollar instintos de programación defensiva para el desarrollo de drivers.

### Lo que aprenderás

- Qué ocurre cuando la limpieza es incompleta.
- Cómo detectar fugas de recursos.
- La importancia del orden de limpieza.
- Cómo gestionar fallos de asignación de memoria.
- Técnicas de programación defensiva (comprobaciones de NULL, limpieza de punteros).
- Cómo depurar problemas en drivers usando logs del kernel y herramientas del sistema.

### Requisitos previos

- Haber completado el Laboratorio 3 (dispositivo demo).
- Comprensión de la estructura del código de demo.c.
- Capacidad de editar código C y recompilar.

### Tiempo estimado

30-40 minutos (introducir bugs deliberadamente, observar síntomas y corregirlos).

### Nota de seguridad importante

Estos experimentos implican **hacer que tu driver falle deliberadamente** (no el kernel, solo el driver). Esto es seguro en tu VM de laboratorio, pero ilustra bugs reales que debes evitar en código de producción.

**Siempre**:

- Usa tu VM de laboratorio, nunca tu sistema anfitrión.
- Toma una instantánea de la VM antes de comenzar.
- Estate preparado para reiniciar si algo se queda bloqueado.

### Parte 1: el bug de fuga de recursos

#### Experimento 1A: olvidar destroy_dev()

**Objetivo**: Ver qué ocurre cuando no limpias los nodos de dispositivo.

**Paso 1**: Edita demo.c y comenta la llamada a `destroy_dev()`:

```c
case MOD_UNLOAD:
    if (demo_dev != NULL) {
        /* destroy_dev(demo_dev);  */  /* COMMENTED OUT - BUG! */
        demo_dev = NULL;
        printf("demo: Device /dev/demo destroyed\n");  /* LIE! */
    }
    break;
```

**Paso 2**: Recompila y carga:

```bash
% make clean && make
% sudo kldload ./demo.ko
% ls -l /dev/demo
crw-rw-rw-  1 root  wheel  0x5e Nov 14 17:00 /dev/demo
```

**Paso 3**: Descarga el módulo:

```bash
% sudo kldunload demo
% dmesg | tail -1
demo: Device /dev/demo destroyed  # Lied!
```

**Paso 4**: Comprueba si el dispositivo sigue existiendo:

```bash
% ls -l /dev/demo
crw-rw-rw-  1 root  wheel  0x5e Nov 14 17:00 /dev/demo  # STILL THERE!
```

**Paso 5**: Intenta usar el dispositivo huérfano:

```bash
% cat /dev/demo
```

**Síntomas que podrías observar**:

- Bloqueo (cat se queda esperando indefinidamente).
- Pánico del kernel (salto a memoria no mapeada).
- Mensaje de error sobre dispositivo inválido.

**Paso 6**: Comprueba si hay fugas:

```bash
% vmstat -m | grep cdev
    cdev     10    15K     -    1442     16,32,64
```

El contador puede ser mayor que antes de empezar.

**Paso 7**: Reinicia para limpiar:

```bash
% sudo reboot
```

**Lo que has aprendido**:

- Los **nodos de dispositivo huérfanos** permanecen en `/dev` aunque el driver se descargue.
- Intentar usar dispositivos huérfanos provoca **comportamiento indefinido** (cuelgue, pánico o errores).
- Se trata de una **fuga de recursos**: la estructura cdev y el nodo de dispositivo nunca se liberan.
- **Llama siempre a `destroy_dev()`** en la ruta de limpieza.

#### Experimento 1B: corregirlo correctamente

**Paso 1**: Restaura la llamada a `destroy_dev()`:

```c
case MOD_UNLOAD:
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);  /* RESTORED */
        demo_dev = NULL;
        printf("demo: Device /dev/demo destroyed\n");
    }
    break;
```

**Paso 2**: Recompila, carga, prueba y descarga:

```bash
% make clean && make
% sudo kldload ./demo.ko
% ls -l /dev/demo        # Exists
% cat /dev/demo          # Works
% sudo kldunload demo
% ls -l /dev/demo        # GONE - correct!
```

**Éxito**: El nodo de dispositivo se limpia correctamente.

### Parte 2: el bug del orden incorrecto

#### Experimento 2A: liberar antes de destruir

**Objetivo**: Ver por qué el orden de limpieza importa.

**Paso 1**: Añade un buffer asignado con malloc a demo.c:

Tras `static struct cdev *demo_dev = NULL;`, añade:

```c
static char *demo_buffer = NULL;
```

**Paso 2**: Asigna memoria en MOD_LOAD:

```c
case MOD_LOAD:
    /* Allocate a buffer */
    demo_buffer = malloc(128, M_TEMP, M_WAITOK | M_ZERO);
    printf("demo: Allocated buffer at %p\n", demo_buffer);
    
    demo_dev = make_dev(...);
    /* ... rest of load code ... */
    break;
```

**Paso 3**: **LIMPIEZA INCORRECTA**: liberar antes de destruir:

```c
case MOD_UNLOAD:
    /* BUG: Free while device is still accessible! */
    if (demo_buffer != NULL) {
        free(demo_buffer, M_TEMP);
        demo_buffer = NULL;
        printf("demo: Freed buffer\n");
    }
    
    /* Device is still alive and can be opened! */
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);
        demo_dev = NULL;
    }
    break;
```

**Paso 4**: Recompila y prueba:

```bash
% make clean && make
% sudo kldload ./demo.ko
```

**Paso 5**: **Con el módulo cargado**, en otro terminal:

```bash
% ( sleep 2; cat /dev/demo ) &  # Start delayed cat
% sudo kldunload demo           # Try to unload
```

**Condición de carrera**:

1. `kldunload` comienza.
2. Tu código libera `demo_buffer`.
3. Se llama a `destroy_dev()`.
4. Mientras tanto, cat ya ha abierto `/dev/demo` (¡el dispositivo todavía existía!).
5. `demo_read()` intenta usar el `demo_buffer` ya liberado.
6. **Uso tras liberación (use-after-free)**: pánico o datos corruptos.

**Síntomas**:

- Pánico del kernel: "page fault in kernel mode".
- Salida de datos corrompida.
- Bloqueo del sistema.

**Paso 6**: Reinicia para recuperarte.

#### Experimento 2B: corregir el orden

**Orden correcto**: hacer el dispositivo invisible PRIMERO y liberar los recursos después.

```c
case MOD_UNLOAD:
    /* CORRECT: Destroy device first */
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);  /* Waits for all operations */
        demo_dev = NULL;
    }
    
    /* Now safe - no one can call our functions */
    if (demo_buffer != NULL) {
        free(demo_buffer, M_TEMP);
        demo_buffer = NULL;
        printf("demo: Freed buffer\n");
    }
    break;
```

**Por qué funciona**:

1. `destroy_dev()` elimina `/dev/demo` del sistema de archivos.
2. `destroy_dev()` **espera** a que terminen las operaciones en curso (como lecturas activas).
3. Cuando `destroy_dev()` retorna, **no puede iniciarse ninguna operación nueva**.
4. **Ahora** es seguro liberar `demo_buffer`: nada puede acceder a él.

**Paso 7**: Recompila y prueba:

```bash
% make clean && make
% sudo kldload ./demo.ko
% ( sleep 2; cat /dev/demo ) &
% sudo kldunload demo
# Works safely - no crash
```

**Lo que has aprendido**:

- **El orden de limpieza es crítico**: dispositivo invisible, esperar a las operaciones y liberar recursos.
- `destroy_dev()` proporciona sincronización (espera a que finalicen las operaciones).
- **Orden inverso** al de la inicialización: lo último en asignarse es lo primero en liberarse.

### Parte 3: el bug del puntero NULL

#### Experimento 3A: comprobación de NULL ausente

**Objetivo**: Ver por qué son necesarias las comprobaciones de NULL.

**Paso 1**: Haz que `make_dev()` falle usando un nombre ya existente:

Carga el módulo demo y luego intenta cargarlo de nuevo en MOD_LOAD:

```c
case MOD_LOAD:
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    
    /* BUG: Don't check for NULL! */
    printf("demo: Device created at %p\n", demo_dev);  /* Might print NULL! */
    /* Continuing even though make_dev failed... */
    break;
```

O simula el fallo:

```c
case MOD_LOAD:
    demo_dev = NULL;  /* Simulate make_dev failure */
    /* BUG: No check! */
    printf("demo: Device created at %p\n", demo_dev);
    break;
```

**Paso 2**: Intenta descargar sin comprobar NULL:

```c
case MOD_UNLOAD:
    /* BUG: No NULL check! */
    destroy_dev(demo_dev);  /* Passing NULL to destroy_dev! */
    break;
```

**Paso 3**: Prueba:

```bash
% make clean && make
% sudo kldload ./demo.ko
# Module "loads" but device wasn't created
% sudo kldunload demo
# Might panic or crash
```

**Síntomas**:

- Pánico del kernel en `destroy_dev`.
- "panic: bad address".
- Bloqueo del sistema.

#### Experimento 3B: comprobación correcta de NULL

```c
case MOD_LOAD:
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    
    /* ALWAYS check return value! */
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        return (ENXIO);  /* Abort load */
    }
    
    printf("demo: Device /dev/demo created successfully\n");
    break;

case MOD_UNLOAD:
    /* ALWAYS check for NULL before using pointer! */
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);
        demo_dev = NULL;  /* Clear pointer for safety */
    }
    break;
```

**Reglas de programación defensiva**:

1. **Comprueba cada asignación**: `if (ptr == NULL) handle_error();`
2. **Comprueba antes de liberar**: `if (ptr != NULL) free(ptr);`
3. **Limpia tras liberar**: `ptr = NULL;` (defensa contra use-after-free).

### Parte 4: el bug de fallo de asignación

#### Experimento 4: gestionar fallos de malloc

**Objetivo**: Aprender a manejar fallos de asignación con M_NOWAIT.

**Paso 1**: Añade una asignación al attach:

```c
case MOD_LOAD:
    /* Allocate with M_NOWAIT - can fail! */
    demo_buffer = malloc(128, M_TEMP, M_NOWAIT | M_ZERO);
    
    /* BUG: Don't check for NULL */
    strcpy(demo_buffer, "Hello");  /* CRASH if malloc failed! */
    
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    /* ... */
    break;
```

**Si malloc falla** (poco frecuente, pero posible):

```text
panic: page fault while in kernel mode
fault virtual address = 0x0
fault code = supervisor write data
instruction pointer = 0x8:0xffffffff12345678
current process = 1234 (kldload)
```

**Paso 2**: Corrige con gestión adecuada de errores:

```c
case MOD_LOAD:
    /* Allocate with M_NOWAIT */
    demo_buffer = malloc(128, M_TEMP, M_NOWAIT | M_ZERO);
    if (demo_buffer == NULL) {
        printf("demo: Failed to allocate buffer\n");
        return (ENOMEM);  /* Out of memory */
    }
    
    /* Now safe to use */
    strcpy(demo_buffer, "Hello");
    
    /* Create device */
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        /* BUG: Forgot to free demo_buffer! */
        return (ENXIO);
    }
    
    printf("demo: Device created successfully\n");
    break;
```

**¡Espera, todavía hay un bug!** Si `make_dev()` falla, retornamos sin liberar `demo_buffer`.

**Paso 3**: Corrige con desenrollado completo del error:

```c
case MOD_LOAD:
    int error = 0;
    
    /* Allocate buffer */
    demo_buffer = malloc(128, M_TEMP, M_NOWAIT | M_ZERO);
    if (demo_buffer == NULL) {
        printf("demo: Failed to allocate buffer\n");
        return (ENOMEM);
    }
    
    strcpy(demo_buffer, "Hello");
    
    /* Create device */
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        error = ENXIO;
        goto fail;
    }
    
    printf("demo: Device created successfully\n");
    return (0);  /* Success */
    
fail:
    /* Error cleanup - undo everything we did */
    if (demo_buffer != NULL) {
        free(demo_buffer, M_TEMP);
        demo_buffer = NULL;
    }
    return (error);
```

**Patrón de desenrollado de errores**:

1. Cada paso de asignación puede fallar.
2. En caso de fallo, **deshaz todo lo hecho hasta ese punto**.
3. Patrón habitual: usa `goto fail` para centralizar la limpieza.
4. Libera en orden inverso al de asignación.

### Parte 5: ejemplo completo con gestión total de errores

Aquí tienes una plantilla que muestra todas las buenas prácticas:

```c
case MOD_LOAD:
    int error = 0;
    
    /* Step 1: Allocate buffer */
    demo_buffer = malloc(128, M_TEMP, M_WAITOK | M_ZERO);
    if (demo_buffer == NULL) {  /* Paranoid - M_WAITOK shouldn't fail */
        error = ENOMEM;
        goto fail_0;  /* Nothing to clean up yet */
    }
    
    strcpy(demo_buffer, "Initialized");
    
    /* Step 2: Create device node */
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        error = ENXIO;
        goto fail_1;  /* Need to free buffer */
    }
    
    /* Success! */
    printf("demo: Module loaded successfully\n");
    return (0);

/* Error unwinding - labels in reverse order of operations */
fail_1:
    /* Failed after allocating buffer */
    free(demo_buffer, M_TEMP);
    demo_buffer = NULL;
fail_0:
    /* Failed before allocating anything */
    return (error);
```

**Por qué funciona este patrón**:

- Cada etiqueta `fail_N` sabe exactamente qué se ha asignado hasta ese punto.
- La limpieza se produce en orden inverso (lo último en asignarse es lo primero en liberarse).
- Un único punto de retorno para los errores facilita la depuración.
- Todas las rutas de error limpian correctamente.

### Lista de comprobación para depurar bugs en drivers

Cuando tu driver se comporte de forma incorrecta, comprueba estos puntos de forma sistemática:

#### 1. Consulta dmesg para ver los mensajes del kernel

```bash
% dmesg | tail -20
% dmesg | grep -i panic
% dmesg | grep -i "page fault"
```

Busca:

- Mensajes de pánico.
- "sleeping with lock held".
- "lock order reversal".
- Los mensajes `printf` de tu driver.

#### 2. Comprueba si hay fugas de recursos

**Antes de cargar el módulo**:

```bash
% vmstat -m | grep cdev > before.txt
```

**Tras cargar y descargar**:

```bash
% vmstat -m | grep cdev > after.txt
% diff before.txt after.txt
```

Si los contadores han aumentado, tienes una fuga.

#### 3. Comprueba si hay dispositivos huérfanos

```bash
% ls -l /dev/ | grep demo
```

Si `/dev/demo` existe tras la descarga, has olvidado llamar a `destroy_dev()`.

#### 4. Prueba la descarga bajo carga

```bash
% ( sleep 10; cat /dev/demo ) &
% sudo kldunload demo
```

Debería esperar a que cat finalice. Si provoca un cuelgue o un pánico, tienes una condición de carrera.

#### 5. Comprueba el estado del módulo

```bash
% kldstat -v | grep demo
```

Muestra dependencias y referencias.

### Criterios de éxito

- Has observado un nodo de dispositivo huérfano (Experimento 1A).
- Lo has corregido con la llamada correcta a `destroy_dev()` (Experimento 1B).
- Has observado un cuelgue por use-after-free (Experimento 2A).
- Lo has corregido con el orden de limpieza correcto (Experimento 2B).
- Has comprendido los peligros de los punteros NULL (Experimento 3).
- Has implementado comprobaciones de NULL correctas (Experimento 3B).
- Has aprendido el patrón de desenrollado de errores (Experimento 4).
- Puedes identificar fugas de recursos con vmstat.
- Puedes depurar con dmesg.

### Lo que has aprendido

**Tipos de bugs**:

- Fugas de recursos (olvidar `destroy_dev`).
- Use-after-free (orden de limpieza incorrecto).
- Desreferencia de puntero NULL (comprobaciones ausentes).
- Fugas de memoria (desenrollado de errores incompleto).

**Programación defensiva**:

- Comprobar siempre los valores de retorno.
- Comprobar siempre si un puntero es NULL antes de usarlo.
- Limpiar en orden inverso al de inicialización.
- Limpiar los punteros tras liberar (`ptr = NULL`).
- Usar goto para centralizar el desenrollado de errores.

**Técnicas de depuración**:

- Usar dmesg para seguir las operaciones.
- Usar vmstat para detectar fugas.
- Probar la descarga bajo carga.
- Introducir bugs deliberadamente para entender sus síntomas.

**Patrones a seguir**:

```c
/* Allocation */
ptr = malloc(size, type, M_WAITOK);
if (ptr == NULL) {
    error = ENOMEM;
    goto fail;
}

/* Device creation */
dev = make_dev(...);
if (dev == NULL) {
    error = ENXIO;
    goto fail_after_malloc;
}

/* Success */
return (0);

/* Error cleanup */
fail_after_malloc:
    free(ptr, type);
    ptr = NULL;
fail:
    return (error);
```

### Plantilla para el cuaderno de laboratorio

```text
Lab 4 Complete: [Date]

Time taken: ___ minutes

Experiments conducted:
- Orphaned device: [ ] Observed  [ ] Fixed
- Wrong cleanup order: [ ] Observed crash  [ ] Fixed
- NULL pointer bug: [ ] Observed  [ ] Fixed
- Error unwinding: [ ] Implemented  [ ] Tested

Most valuable insight:
[What "clicked" about error handling?]

Bugs I've seen before:
[Have you made similar mistakes in userspace code?]

Defensive programming rules I'll remember:
1. [e.g., "Always check malloc return"]
2. [e.g., "Cleanup in reverse order"]
3. [e.g., "Set pointers to NULL after free"]

Debugging techniques learned:
[Which debugging method was most useful?]

Ready for Chapter 7:
[ ] Yes - I understand error handling
[ ] Need more practice - I'll review the error patterns again
```

## Resumen de los laboratorios y próximos pasos

¡Enhorabuena! Has completado los cuatro laboratorios. Esto es lo que has conseguido:

### Resumen de la progresión de los laboratorios

| Lab   | Lo que construiste     | Habilidad clave                              |
| ----- | ---------------------- | -------------------------------------------- |
| Lab 1 | Habilidades de navegación | Leer y comprender código de driver        |
| Lab 2 | Módulo mínimo          | Compilar y cargar módulos del kernel         |
| Lab 3 | Dispositivo de caracteres | Crear nodos en `/dev`, implementar I/O    |
| Lab 4 | Gestión de errores     | Programación defensiva, depuración           |

### Conceptos clave dominados

**Ciclo de vida del módulo**:

- MOD_LOAD  ->  inicialización
- MOD_UNLOAD  ->  limpieza
- Registro con DECLARE_MODULE

**Framework de dispositivos**:

- cdevsw como tabla de despacho de métodos
- make_dev() para crear entradas en `/dev`
- destroy_dev() para limpieza + sincronización

**Transferencia de datos**:

- uiomove() para copia segura entre usuario y kernel
- La estructura uio para peticiones de I/O
- Seguimiento de uio_resid

**Manejo de errores**:

- Comprobación de NULL en todas las asignaciones
- Limpieza en orden inverso
- Desenrollado de errores con goto
- Prevención de fugas de recursos

**Depuración**:

- Uso de dmesg para logs del kernel
- vmstat para seguimiento de recursos
- Pruebas bajo carga

### Tu kit de herramientas para el desarrollo de drivers

Ahora tienes una base sólida formada por:

1. **Reconocimiento de patrones**: puedes examinar cualquier driver de FreeBSD e identificar su estructura
2. **Habilidades prácticas**: puedes construir, cargar, probar y depurar módulos del kernel
3. **Conocimiento de seguridad**: entiendes los errores comunes y cómo evitarlos
4. **Capacidad de depuración**: puedes diagnosticar problemas con las herramientas del sistema

### ¡Celebra tu logro!

Has completado laboratorios prácticos que muchos desarrolladores se saltan. No solo leíste sobre drivers, los **construiste**, los **rompiste** y los **arreglaste**. Este aprendizaje experiencial es invaluable.

## Cerrando

¡Enhorabuena! Has completado un recorrido completo por la anatomía de los drivers de FreeBSD. Recapitulemos lo que has aprendido y hacia dónde nos dirigimos.

### Lo que sabes ahora

**Vocabulario**. Ya puedes hablar el idioma de los drivers de FreeBSD:

- **newbus**: el framework de dispositivos (probe/attach/detach)
- **devclass**: agrupación de dispositivos relacionados
- **softc**: estructura de datos privada por dispositivo
- **cdevsw**: character device switch (tabla de puntos de entrada)
- **ifnet**: estructura de interfaz de red
- **GEOM**: arquitectura de la capa de almacenamiento
- **devfs**: sistema de archivos de dispositivos dinámico

**Estructura**. Reconoces los patrones de drivers al instante:

- Las funciones probe comprueban los IDs de dispositivo y devuelven una prioridad
- Las funciones attach inicializan el hardware y crean nodos de dispositivo
- Las funciones detach realizan la limpieza en orden inverso
- Las tablas de métodos mapean las llamadas del kernel a tus funciones
- Las declaraciones de módulo se registran con el kernel

**Ciclo de vida**. Entiendes el flujo:

1. La enumeración del bus descubre el hardware
2. Las funciones probe compiten por los dispositivos
3. Las funciones attach inicializan los ganadores
4. Los dispositivos operan (lectura/escritura, transmisión/recepción)
5. Las funciones detach realizan la limpieza al descargar

**Puntos de entrada**. Sabes cómo los programas de usuario llegan a tu driver:

- Dispositivos de caracteres: open/close/read/write/ioctl a través de `/dev`
- Interfaces de red: transmisión/recepción a través de la pila de red
- Dispositivos de almacenamiento: peticiones bio a través de GEOM/CAM

### Lo que puedes hacer ahora

- Navegar el árbol de código fuente del kernel de FreeBSD con confianza
- Reconocer patrones comunes de drivers (probe/attach/detach, cdevsw)
- Entender el ciclo de vida probe/attach/detach
- Construir módulos del kernel con Makefiles correctos
- Cargar y descargar módulos de forma segura
- Crear nodos de dispositivos de caracteres con los permisos apropiados
- Implementar operaciones básicas de I/O (open/close/read/write)
- Usar uiomove() correctamente para la transferencia de datos entre usuario y kernel
- Manejar errores y limpiar recursos correctamente
- Depurar con dmesg y las herramientas del sistema
- Evitar errores comunes (fugas de recursos, orden de limpieza incorrecto, punteros NULL)

### Cambio de mentalidad

Observa el cambio en este capítulo:

- **Capítulos 1-5**: Fundamentos (UNIX, C, C para el kernel)
- **Capítulo 6** (este): Estructura y patrones (reconocimiento)
- **Capítulo 7+**: Implementación (construcción)

Has cruzado un umbral. Ya no estás solo aprendiendo conceptos, estás listo para escribir código real del kernel. Esto es emocionante y un poco intimidante, y eso es exactamente lo correcto.

### Reflexión final

El desarrollo de drivers es como aprender un instrumento musical. Al principio, los patrones se sienten extraños y complejos. Pero con la práctica, se vuelven algo natural. Empezarás a ver probe/attach/detach en todas partes. Reconocerás cdevsw al instante. Sabrás qué significa «asignar recursos, comprobar errores, limpiar en caso de fallo» sin ni siquiera pensarlo.

**Confía en el proceso**. Los laboratorios fueron solo el comienzo. En el Capítulo 7, escribirás más código, cometerás errores, los depurarás y ganarás confianza. Para el Capítulo 8, la estructura de los drivers te resultará natural.

### Antes de continuar

Tómate un momento para:

- **Revisar tu cuaderno de laboratorio**: ¿Qué te sorprendió? ¿Qué encajó?
- **Repasar las secciones confusas**: ahora que has hecho los laboratorios, releer tiene más sentido
- **Explorar un driver más**: elige cualquiera de `/usr/src/sys/dev` y observa cuánto reconoces

### Lo que viene

El Capítulo 6 fue el último capítulo de fundamentos de la Parte 1. Ahora tienes un modelo mental completo de cómo se forma un driver de FreeBSD, desde el momento en que el bus enumera un dispositivo, pasando por probe, attach, operación y detach, hasta llegar a `/dev` e `ifconfig`.

El siguiente capítulo, **Capítulo 7: Escribiendo tu primer driver**, pone ese modelo en práctica. Construirás un pseudo-dispositivo llamado `myfirst`, lo conectarás limpiamente a través de Newbus, crearás un nodo `/dev/myfirst0`, expondrás un sysctl de solo lectura, registrarás eventos del ciclo de vida y desconectarás sin fugas. El objetivo no es un driver sofisticado, sino uno disciplinado: el tipo de esqueleto del que crece cada driver de producción.

Todo lo que practicaste en este capítulo (la forma del cdevsw, el ritmo de probe/attach/detach, el patrón de desenrollado, la regla de liberar siempre los recursos en orden inverso) aparecerá de nuevo en el Capítulo 7 como código que escribirás tú mismo. Mantén tu cuaderno de laboratorio a mano, marca `/usr/src/sys/dev/null/null.c` como esqueleto de referencia y, cuando pases la página, ya conocerás la mayor parte de lo que estás a punto de construir.

## Punto de control de la Parte 1

La Parte 1 te ha llevado desde «¿qué es UNIX siquiera?» hasta «puedo leer un driver pequeño y nombrar sus piezas». Antes de que el Capítulo 7 te pida escribir y cargar un módulo real, detente y confirma que los cimientos se sienten sólidos bajo tus pies. La Parte 2 se construye directamente sobre cada habilidad que los primeros seis capítulos han reunido.

Al final de la Parte 1 deberías ser capaz de instalar, configurar y hacer un snapshot de un laboratorio de trabajo de FreeBSD, rastrear su árbol de código fuente bajo control de versiones y llevar un cuaderno disciplinado de lo que cambiaste y por qué. Deberías poder manejar la línea de comandos de FreeBSD para el trabajo habitual de desarrollo, lo que significa moverte por el sistema de archivos, inspeccionar procesos, leer y ajustar permisos, instalar paquetes, seguir registros y escribir scripts de shell cortos que sobrevivan a nombres de archivos inusuales. También deberías poder leer y escribir C con estilo del kernel sin inmutarte ante su dialecto, incluyendo tipos y calificadores, flags de bits, el preprocesador, punteros y arrays, punteros a función, cadenas acotadas, y los asignadores de memoria y helpers de registro del lado del kernel que reemplazan a `malloc(3)` y `printf(3)`. Y deberías poder mirar cualquier driver en `/usr/src/sys/dev` y nombrar sus piezas: qué función es el probe, cuál es el attach, cuál es el detach, dónde vive el softc, qué puntos de entrada proporciona el character switch, y qué recursos está adquiriendo la ruta de attach.

Si alguna de esas cosas todavía se siente como una búsqueda en lugar de un hábito, los laboratorios que las anclan merecen una segunda pasada:

- Disciplina de laboratorio y navegación del código fuente: los laboratorios prácticos del Capítulo 2 (shell, archivos, procesos, scripting) y el recorrido de instalación y snapshot del Capítulo 3.
- C para el kernel: el Lab 4 del Capítulo 4 (Despacho mediante punteros a función, un mini devsw) y el Lab 5 (Buffer circular de tamaño fijo), ambos de los cuales anticipan patrones que volverás a encontrar en cada driver.
- Dialecto C del kernel: el Lab 1 del Capítulo 5 (Asignación segura de memoria y limpieza) y el Lab 2 (Intercambio de datos entre usuario y kernel), que enseñan las dos fronteras que todo driver cruza.
- Anatomía de drivers: el Lab 1 del Capítulo 6 (Explora el mapa de drivers), el Lab 2 (Módulo mínimo solo con logs) y el Lab 3 (Crear y eliminar un nodo de dispositivo).

La Parte 2 esperará un laboratorio de FreeBSD funcionando con `/usr/src` instalado, un kernel que puedas construir y arrancar, y el hábito de revertir a un snapshot limpio después de cada experimento. Esperará suficiente comodidad con el C del kernel como para que un `struct cdevsw`, una firma de handler `d_read`, o un patrón de limpieza con goto etiquetado no te detenga. También esperará que el ritmo de probe/attach/detach esté firmemente en mente, para que el Capítulo 7 pueda convertir ese ritmo en código que escribas tú mismo. Si esas tres cosas se sostienen, estás listo para cruzar del reconocimiento a la autoría. Si alguna tambalea, la hora tranquila invertida ahora te ahorra una tarde desconcertante más adelante.

## Ejercicios de desafío (opcionales)

Estos ejercicios opcionales profundizan tu comprensión y generan confianza. Son más abiertos que los laboratorios pero siguen siendo seguros para principiantes. Completa los que quieras antes de pasar al Capítulo 7.

### Desafío 1: Traza un ciclo de vida en dmesg

**Objetivo**: Capturar y anotar mensajes reales del ciclo de vida de un driver.

**Instrucciones**:

1. Elige un driver que se pueda cargar como módulo (p. ej., `if_em`, `snd_hda`, `usb`)
2. Configura el registro:
   ```bash
   % tail -f /var/log/messages > ~/driver_lifecycle.log &
   ```
3. Carga el driver:
   ```bash
   % sudo kldload if_em
   ```
4. Observa la secuencia de attach en tiempo real
5. Descarga el driver:
   ```bash
   % sudo kldunload if_em
   ```
6. Detén el registro (termina el proceso tail)
7. Anota el archivo de log:
   - Marca dónde se llamó a probe
   - Marca dónde ocurrió el attach
   - Marca las asignaciones de recursos
   - Marca dónde limpió el detach
8. Escribe un resumen de una página explicando el ciclo de vida que observaste

**Criterios de éxito**: Tu log anotado muestra una comprensión clara de cuándo ocurrió cada fase del ciclo de vida.

### Desafío 2: Mapea los puntos de entrada

**Objetivo**: Documentar completamente la estructura cdevsw de un driver.

**Instrucciones**:

1. Abre `/usr/src/sys/dev/null/null.c`
2. Crea una tabla:

| Punto de entrada | Nombre de función | ¿Presente? | Qué hace |
|------------------|-------------------|------------|----------|
| d_open | ? | ? | ? |
| d_close | ? | ? | ? |
| d_read | ? | ? | ? |
| d_write | ? | ? | ? |
| d_ioctl | ? | ? | ? |
| d_poll | ? | ? | ? |
| d_mmap | ? | ? | ? |

3. Rellena la tabla
4. Para los puntos de entrada ausentes, explica por qué no son necesarios
5. Para los puntos de entrada presentes, describe qué hacen en 1-2 frases
6. Repite para `/usr/src/sys/dev/led/led.c`
7. Compara las dos tablas: ¿Qué es similar? ¿Qué es diferente? ¿Por qué?

**Criterios de éxito**: Tus tablas son precisas y tus explicaciones demuestran comprensión.

### Desafío 3: Ejercicio de clasificación

**Objetivo**: Practicar la identificación de familias de drivers examinando el código fuente.

**Instrucciones**:

1. Elige **cinco drivers al azar** de `/usr/src/sys/dev/`
   ```bash
   % ls /usr/src/sys/dev | shuf | head -5
   ```
2. Para cada driver, crea una entrada en tu cuaderno de laboratorio:
   - Nombre del driver
   - Archivo fuente principal
   - Clasificación (de caracteres, de red, de almacenamiento, de bus, o mixto)
   - Evidencia (¿cómo determinaste la clasificación?)
   - Propósito (¿qué hace este driver?)

3. Verificación: usa `man 4 <drivername>` para confirmar tu clasificación

**Ejemplo de entrada**:
```text
Driver: led
File: sys/dev/led/led.c
Classification: Character device
Evidence: Has cdevsw structure, creates /dev/led/* nodes, no ifnet or GEOM
Purpose: Control system LEDs (keyboard lights, chassis indicators)
Man page: man 4 led (confirmed)
```

**Criterios de éxito**: Clasificaste correctamente los cinco, con evidencia clara para cada uno.

### Desafío 4: Auditoría de códigos de error

**Objetivo**: Entender los patrones de manejo de errores en drivers reales.

**Instrucciones**:

1. Abre `/usr/src/sys/dev/uart/uart_core.c`
2. Encuentra la función `uart_bus_attach()`
3. Lista cada código de error devuelto (ENOMEM, ENXIO, EIO, etc.)
4. Para cada uno, anota:
   - Qué condición lo desencadenó
   - Qué recursos se liberaron antes de retornar
   - Si la limpieza fue completa

5. Repite para `/usr/src/sys/dev/ahci/ahci.c` (función ahci_attach)

6. Escribe un ensayo breve (1-2 páginas):
   - Patrones comunes de manejo de errores que observaste
   - Cómo los drivers aseguran que no haya fugas de recursos
   - Buenas prácticas que puedes aplicar a tu propio código

**Criterios de éxito**: Tu ensayo demuestra comprensión del correcto desenrollado de errores.

### Desafío 5: Detective de dependencias

**Objetivo**: Entender las dependencias de módulos y el orden de carga.

**Instrucciones**:

1. Encuentra un driver que declare `MODULE_DEPEND`
   ```bash
   % grep -r "MODULE_DEPEND" /usr/src/sys/dev/usb | head -5
   ```
2. Elige un ejemplo (p. ej., un driver USB)
3. Abre el archivo fuente y localiza todas las declaraciones `MODULE_DEPEND`
4. Para cada dependencia:
   - ¿De qué módulo depende?
   - ¿Por qué es necesaria esta dependencia? (¿Qué funciones o tipos de ese módulo se utilizan?)
   - ¿Qué ocurriría si intentaras cargarlo sin la dependencia?
5. Pruébalo:
   ```bash
   % sudo kldload <dependency_module>
   % sudo kldload <your_driver>
   % kldstat
   ```
6. Intenta descargar la dependencia mientras tu driver está cargado:
   ```bash
   % sudo kldunload <dependency_module>
   ```
   ¿Qué ocurre? ¿Por qué?

7. Documenta tus hallazgos: dibuja un grafo de dependencias que muestre las relaciones.

**Criterio de éxito**: Eres capaz de explicar por qué existe cada dependencia y de predecir el orden de carga.

**Resumen**

Estos desafíos desarrollan:

- **Desafío 1**: Observación del ciclo de vida en situaciones reales
- **Desafío 2**: Dominio de los puntos de entrada
- **Desafío 3**: Reconocimiento de patrones entre distintos drivers
- **Desafío 4**: Disciplina en el manejo de errores
- **Desafío 5**: Comprensión de las dependencias

**Opcional**: Comparte tus resultados en los foros o listas de correo de FreeBSD. A la comunidad le encanta ver a los recién llegados afrontar problemas más exigentes.

## Tabla de referencia resumida: los elementos fundamentales del driver de un vistazo

Esta hoja de referencia rápida relaciona conceptos con implementaciones. Guárdala como marcador para consultarla mientras trabajas en el Capítulo 7 y los siguientes.

| Concepto | Qué es | API/Estructura típica | Dónde en el árbol | Cuándo lo usarás |
|---------|------------|----------------------|---------------|-------------------|
| **device_t** | Handle opaco de dispositivo | `device_t dev` | `<sys/bus.h>` | En cada función del driver (probe/attach/detach) |
| **softc** | Datos privados por dispositivo | `struct mydriver_softc` | Tú lo defines | Almacenar estado, recursos, locks |
| **devclass** | Agrupación de clase de dispositivo | `devclass_t` | `<sys/bus.h>` | Gestionado automáticamente por DRIVER_MODULE |
| **cdevsw** | Switch de dispositivo de caracteres | `struct cdevsw` | `<sys/conf.h>` | Puntos de entrada del dispositivo de caracteres |
| **d_open** | Manejador de apertura | `d_open_t` | En tu cdevsw | Inicializar estado por sesión |
| **d_close** | Manejador de cierre | `d_close_t` | En tu cdevsw | Limpiar estado por sesión |
| **d_read** | Manejador de lectura | `d_read_t` | En tu cdevsw | Transferir datos al usuario |
| **d_write** | Manejador de escritura | `d_write_t` | En tu cdevsw | Aceptar datos del usuario |
| **d_ioctl** | Manejador de ioctl | `d_ioctl_t` | En tu cdevsw | Configuración y control |
| **uiomove** | Copia hacia/desde el usuario | `int uiomove(...)` | `<sys/uio.h>` | En los manejadores de lectura/escritura |
| **make_dev** | Crear nodo de dispositivo | `struct cdev *make_dev(...)` | `<sys/conf.h>` | En attach (dispositivos de caracteres) |
| **destroy_dev** | Eliminar nodo de dispositivo | `void destroy_dev(...)` | `<sys/conf.h>` | En detach |
| **ifnet (if_t)** | Interfaz de red | `if_t` | `<net/if_var.h>` | Drivers de red |
| **ether_ifattach** | Registrar interfaz Ethernet | `void ether_ifattach(...)` | `<net/ethernet.h>` | Attach del driver de red |
| **ether_ifdetach** | Dar de baja la interfaz Ethernet | `void ether_ifdetach(...)` | `<net/ethernet.h>` | Detach del driver de red |
| **GEOM provider** | Proveedor de almacenamiento | `struct g_provider` | `<geom/geom.h>` | Drivers de almacenamiento |
| **bio** | Solicitud de I/O de bloques | `struct bio` | `<sys/bio.h>` | Gestión de I/O en almacenamiento |
| **bus_alloc_resource** | Asignar recurso | `struct resource *` | `<sys/bus.h>` | Attach (memoria, IRQ, etc.) |
| **bus_release_resource** | Liberar recurso | `void` | `<sys/bus.h>` | Limpieza en detach |
| **bus_space_read_N** | Leer registro | `uint32_t bus_space_read_4(...)` | `<machine/bus.h>` | Acceso a registros de hardware |
| **bus_space_write_N** | Escribir registro | `void bus_space_write_4(...)` | `<machine/bus.h>` | Acceso a registros de hardware |
| **bus_setup_intr** | Registrar interrupción | `int bus_setup_intr(...)` | `<sys/bus.h>` | Attach (configuración de la interrupción) |
| **bus_teardown_intr** | Dar de baja la interrupción | `int bus_teardown_intr(...)` | `<sys/bus.h>` | Limpieza en detach |
| **device_printf** | Registro específico del dispositivo | `void device_printf(...)` | `<sys/bus.h>` | En todas las funciones del driver |
| **device_get_softc** | Obtener el softc | `void *device_get_softc(device_t)` | `<sys/bus.h>` | Primera línea de la mayoría de funciones |
| **device_set_desc** | Establecer la descripción del dispositivo | `void device_set_desc(...)` | `<sys/bus.h>` | En la función probe |
| **DRIVER_MODULE** | Registrar el driver | Macro | `<sys/module.h>` | Una vez por driver (al final del archivo) |
| **MODULE_VERSION** | Declarar la versión | Macro | `<sys/module.h>` | Una vez por driver |
| **MODULE_DEPEND** | Declarar dependencia | Macro | `<sys/module.h>` | Si dependes de otros módulos |
| **DEVMETHOD** | Mapear método a función | Macro | `<sys/bus.h>` | En la tabla de métodos |
| **DEVMETHOD_END** | Finalizar la tabla de métodos | Macro | `<sys/bus.h>` | Última entrada en la tabla de métodos |
| **mtx** | Mutex lock | `struct mtx` | `<sys/mutex.h>` | Proteger el estado compartido |
| **mtx_init** | Inicializar el mutex | `void mtx_init(...)` | `<sys/mutex.h>` | En attach |
| **mtx_destroy** | Destruir el mutex | `void mtx_destroy(...)` | `<sys/mutex.h>` | En detach |
| **mtx_lock** | Adquirir el lock | `void mtx_lock(...)` | `<sys/mutex.h>` | Antes de acceder a datos compartidos |
| **mtx_unlock** | Liberar el lock | `void mtx_unlock(...)` | `<sys/mutex.h>` | Después de acceder a datos compartidos |
| **malloc** | Asignar memoria | `void *malloc(...)` | `<sys/malloc.h>` | Asignación dinámica |
| **free** | Liberar memoria | `void free(...)` | `<sys/malloc.h>` | Limpieza |
| **M_WAITOK** | Esperar por memoria | Flag | `<sys/malloc.h>` | Flag de malloc (puede dormir) |
| **M_NOWAIT** | No esperar | Flag | `<sys/malloc.h>` | Flag de malloc (devuelve NULL si no hay memoria disponible) |

### Búsqueda rápida por tarea

**Necesitas...** | **Usa esto** | **Página man**
---|---|---
Crear un dispositivo de caracteres | `make_dev()` | `make_dev(9)`
Leer/escribir registros de hardware | `bus_space_read/write_N()` | `bus_space(9)`
Asignar recursos de hardware | `bus_alloc_resource()` | `bus_alloc_resource(9)`
Configurar interrupciones | `bus_setup_intr()` | `bus_setup_intr(9)`
Copiar datos hacia/desde el usuario | `uiomove()` | `uio(9)`
Registrar un mensaje | `device_printf()` | `device(9)`
Proteger datos compartidos | `mtx_lock()` / `mtx_unlock()` | `mutex(9)`
Registrar un driver | `DRIVER_MODULE()` | `DRIVER_MODULE(9)`

### Referencia rápida de Probe/Attach/Detach

```c
/* Probe - Check if we can handle this device */
static int mydrv_probe(device_t dev) {
    /* Check IDs, return BUS_PROBE_DEFAULT or ENXIO */
}

/* Attach - Initialize device */
static int mydrv_attach(device_t dev) {
    sc = device_get_softc(dev);
    /* Allocate resources */
    /* Initialize hardware */
    /* Create device node or register interface */
    return (0);  /* or error code */
}

/* Detach - Clean up */
static int mydrv_detach(device_t dev) {
    sc = device_get_softc(dev);
    /* Reverse order of attach */
    /* Check pointers before freeing */
    /* Set pointers to NULL after freeing */
    return (0);  /* or EBUSY if can't detach */
}
```
