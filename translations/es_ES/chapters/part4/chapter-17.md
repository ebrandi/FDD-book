---
title: "Simulación de hardware"
description: "El capítulo 17 toma el bloque de registros estático presentado en el capítulo 16 y lo hace comportarse como un dispositivo real: un callout impulsa cambios de estado autónomos, un protocolo disparado por escritura planifica eventos diferidos, la semántica de lectura para borrado refleja el hardware real, y una ruta de inyección de fallos enseña el manejo de errores sin arriesgar silicio real. El driver pasa de la versión 0.9-mmio a la 1.0-simulated, incorpora un nuevo archivo de simulación, y llega a la Parte 4 listo para encontrarse con dispositivos PCI reales en el capítulo 18."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 17
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Simulando hardware

## Orientación para el lector y objetivos

El capítulo 16 terminó con un driver que tenía en su poder un bloque de registros. El módulo `myfirst` en la versión `0.9-mmio` contiene una región de memoria del kernel de 64 bytes diseñada para parecerse a un dispositivo: diez registros de 32 bits con nombre, un pequeño conjunto de máscaras de bits, un ID de dispositivo que devuelve `0x4D594649`, una revisión de firmware que codifica `1.0`, y un registro `STATUS` cuyo bit `READY` se activa al hacer el attach y se desactiva al hacer el detach. Cada acceso pasa por `bus_space_read_4` y `bus_space_write_4`, envueltos por los ya conocidos macros `CSR_READ_4`, `CSR_WRITE_4` y `CSR_UPDATE_4`. El mutex del driver (`sc->mtx`) protege cada acceso a los registros. Un registro de accesos guarda en memoria los últimos sesenta y cuatro accesos, una tarea ticker voltea `SCRATCH_A` bajo demanda, y un pequeño `HARDWARE.md` documenta toda la superficie del dispositivo.

Ese driver ya es capaz de muchas cosas. Puede leer registros. Puede escribir registros. Puede observar sus propios accesos, detectar offsets fuera de rango en kernels de depuración, y aplicar la disciplina de locking de la que dependerán todos los capítulos posteriores. Lo que aún no puede hacer es comportarse como un dispositivo real. Sus bits de `STATUS` nunca cambian por sí solos. Su registro `DATA_IN` no inicia una operación de varios ciclos. Su `INTR_STATUS` no acumula eventos. Escribir `CTRL.GO` no planifica nada. Nada en la simulación tiene pulso propio; cada cambio en el bloque de registros es consecuencia directa de la última escritura desde el espacio de usuario.

Los dispositivos reales no son así. Un sensor de temperatura refresca su valor cada pocos milisegundos sin que nadie lo pida. Un controlador serie genera una interrupción cuando llega un byte al FIFO de recepción, mucho después de la última escritura del driver. El anillo de descriptores de una tarjeta de red se llena de paquetes mientras el driver duerme. Las partes más interesantes de un driver de dispositivo consisten en reaccionar a los cambios, no en producirlos. Enseñar al lector a escribir esas rutas reactivas exige un dispositivo simulado que produzca cambios, y el capítulo 17 es donde la simulación aprende a hacerlo.

### De qué trata el capítulo 17

El alcance del capítulo 17 es reducido pero profundo. Toma el bloque de registros estático del capítulo 16 y le añade cuatro propiedades nuevas:

- **Comportamiento autónomo.** Un callout se dispara con una cadencia que el driver controla y actualiza bits de `STATUS`, bits de `INTR_STATUS` o registros de datos como si la máquina de estados interna de un dispositivo real estuviera ejecutándose en silicio. El lector verá cambiar valores entre dos lecturas consecutivas de sysctl, aunque no haya habido ninguna escritura desde el espacio de usuario entre ellas.

- **Protocolo disparado por comandos.** Escribir un bit determinado en `CTRL` planifica un cambio de estado diferido. Tras un retardo configurable, `STATUS.DATA_AV` se activa y, opcionalmente, `INTR_STATUS.DATA_AV` queda latched. Este es el patrón que sigue todo dispositivo de tipo comando-respuesta: una escritura inicia algo, y un cambio de estado posterior señala que ese algo ha terminado.

- **Temporización realista.** Los retardos del orden de microsegundos utilizan `DELAY(9)` donde corresponde y `pause_sbt(9)` donde es seguro dormir un hilo. Los retardos del orden de milisegundos usan `callout_reset_sbt(9)` para que ningún thread quede bloqueado mientras el dispositivo simulado trabaja. Cada herramienta tiene su lugar; cada herramienta tiene su coste; el capítulo 17 enseña ambas cosas.

- **Inyección de fallos.** Un sysctl permite al lector solicitar que la simulación falle. Los fallos adoptan varias formas: la siguiente lectura devuelve un valor de error fijo, la siguiente escritura nunca activa `DATA_AV`, una fracción aleatoria de operaciones activa `STATUS.ERROR`, un bit `STATUS.BUSY` permanece activo indefinidamente. Cada modo ejercita una ruta de tratamiento de errores diferente en el driver, y el driver aprende a detectarlos, recuperarse o informar de ellos.

El capítulo construye estas propiedades sobre el driver del capítulo 16 sin reemplazarlo. El mismo mapa de registros, los mismos accesores, el mismo `sc->mtx`, el mismo registro de accesos. Lo que crece es el backend de simulación. El archivo `myfirst_hw.c` del capítulo 16 gana un archivo hermano, `myfirst_sim.c`, que contiene los callouts de la simulación, los hooks de inyección de fallos y los helpers que hacen que el bloque de registros respire. Al final del capítulo el driver estará en la versión `1.0-simulated`, y la superficie de hardware que muestra al driver será suficientemente rica como para ejercitar casi todas las lecciones de la Parte 3.

### Por qué el comportamiento autónomo merece un capítulo propio

Una pregunta natural en este punto es si el comportamiento impulsado por callouts y la inyección de fallos merecen realmente un capítulo completo. El capítulo 16 ya colocó un bloque de registros frente al driver, y el capítulo 18 reemplazará ese bloque con hardware PCI real. ¿No es la simulación simplemente un escalón que deberíamos abandonar cuanto antes?

Tres respuestas, cada una de las cuales merece tomarse en serio.

La primera: la simulación del capítulo 16 era estática a propósito. Existía para que el lector practicara el acceso a registros sin tener que gestionar al mismo tiempo el comportamiento del dispositivo. El capítulo 17 es donde ese comportamiento entra en escena, y el lugar adecuado para introducirlo no es en mitad del capítulo sobre PCI real, donde el lector ya está manejando la asignación de BAR, los IDs de vendedor y dispositivo y el pegamento de newbus. Un capítulo dedicado a la simulación da al lector espacio para razonar sobre cómo se comporta un dispositivo, cómo la temporización da forma al protocolo y cómo se propagan los fallos, sin las distracciones que trae el hardware real.

La segunda: la simulación no es un andamio que se descarta tras el capítulo 18. Es una parte permanente del desarrollo de drivers. Los drivers reales se prueban contra dispositivos simulados mucho tiempo después de haber funcionado por primera vez en hardware real, porque la simulación es la única forma de producir fallos deterministas, temporización reproducible y condiciones de fallo a demanda. Todos los subsistemas serios de FreeBSD tienen alguna forma de simulación o banco de pruebas, y todo autor de drivers con experiencia aprende a construirlo. El capítulo 17 enseña la técnica a pequeña escala, donde el lector puede ver cada pieza en movimiento.

La tercera: la simulación del capítulo 17 es en sí misma un dispositivo pedagógico. Al escribir un dispositivo falso, el lector se ve obligado a pensar en lo que hace un dispositivo real: cuándo activa un bit, cuándo lo desactiva, cuándo falla, cuándo hace latch, cuándo olvida. Un lector que ha escrito la simulación comprende el protocolo de una manera que no lo hace quien solo la ha usado. El capítulo trata tanto de la disciplina de pensar como hardware como del código que lo implementa.

### En qué punto dejó el capítulo 16 al driver

Un breve resumen de dónde deberías estar. El capítulo 17 extiende el driver producido al final del Stage 4 del capítulo 16, etiquetado como versión `0.9-mmio`. Si alguno de los puntos siguientes no te resulta del todo claro, vuelve al capítulo 16 antes de empezar este.

- Tu driver compila sin errores y se identifica como `0.9-mmio` en `kldstat -v`.
- El softc contiene un puntero `sc->hw` a un `struct myfirst_hw` que almacena `regs_buf`, `regs_size`, `regs_tag`, `regs_handle` y el registro de accesos del capítulo 16.
- Todos los accesos a registros en el driver pasan por `CSR_READ_4(sc, off)`, `CSR_WRITE_4(sc, off, val)` o `CSR_UPDATE_4(sc, off, clear, set)`.
- Los accesores verifican mediante `MYFIRST_ASSERT` que `sc->mtx` está adquirido, en kernels de depuración.
- `sysctl dev.myfirst.0` muestra los sysctls `reg_*` para cada registro, el sysctl escribible `reg_ctrl_set`, el toggle `access_log_enabled` y el volcador `access_log`.
- `HARDWARE.md` documenta el mapa de registros y la API `CSR_*`.
- `LOCKING.md` documenta el orden de detach, incluido el nuevo paso `myfirst_hw_detach`.

Ese driver es lo que el capítulo 17 extiende. Las adiciones son de nuevo moderadas en número de líneas: un nuevo archivo `myfirst_sim.c`, cuatro nuevos callouts, dos nuevos grupos de sysctls, una pequeña estructura de estado para la inyección de fallos y un puñado de helpers de protocolo. El cambio en el modelo mental es mayor de lo que sugiere el número de líneas.

### Lo que aprenderás

Al terminar este capítulo, serás capaz de:

- Explicar qué significa "simular hardware" en el espacio del kernel, por qué la simulación es una parte permanente del desarrollo de drivers y no un recurso temporal, y qué hace buena a una simulación frente a una que resulta engañosa.
- Diseñar un mapa de registros para un dispositivo simulado, con secciones separadas para control, estado, datos, interrupciones y metadatos, y justificar cada elección frente al protocolo que el dispositivo pretende implementar.
- Elegir anchos de registro, offsets, distribuciones de campos de bits y semánticas de acceso (solo lectura, solo escritura, lectura que limpia, escritura de uno que limpia, lectura/escritura) que reflejen los patrones utilizados en drivers reales de FreeBSD.
- Implementar un backend de hardware simulado en memoria del kernel que se comporte de forma autónoma, con un callout que actualiza registros con una cadencia y una tarea que reacciona a las escrituras que emite el driver.
- Exponer el dispositivo falso al driver usando la misma abstracción `bus_space(9)` que utilizan los drivers reales, de modo que el código del driver no tenga conocimiento de si está hablando con silicio o con un struct en memoria del kernel.
- Probar el comportamiento del dispositivo desde el lado del driver, escribiendo comandos, haciendo polling de estado, leyendo datos y comprobando invariantes bajo diversas cargas.
- Añadir temporización y retardos a la simulación usando `DELAY(9)`, `pause_sbt(9)` y `callout_reset_sbt(9)`, y explicar por qué cada herramienta es apropiada para la escala de tiempo en que se usa.
- Simular errores y condiciones de fallo de forma segura, con una ruta de inyección de fallos controlada por sysctl que puede reproducir timeouts, errores de datos, estados de busy permanente y fallos aleatorios, y usar esos fallos inyectados para ejercitar las rutas de error del driver.
- Refactorizar la simulación para que resida en su propio archivo (`myfirst_sim.c`), documentar su interfaz en un `SIMULATION.md`, versionar el driver como `1.0-simulated` y ejecutar una pasada de regresión completa.
- Reconocer los límites de la simulación y los escenarios en los que solo el hardware real (o hardware virtual respaldado por un hipervisor) producirá resultados fiables.

La lista es larga; cada elemento es concreto. El objetivo del capítulo es la composición.

### Lo que no cubre este capítulo

Varios temas adyacentes se aplazan explícitamente para que el capítulo 17 se mantenga enfocado.

- **Dispositivos PCI reales.** El subsistema `pci(4)`, la concordancia por ID de vendedor y dispositivo, la asignación de BAR mediante `bus_alloc_resource_any`, `pci_enable_busmaster` y el pegamento que une un driver a un bus real corresponden al capítulo 18. El capítulo 17 se mantiene en simulación y utiliza el atajo del capítulo 16 para el tag y el handle.
- **Interrupciones.** La simulación produce cambios de estado que imitan lo que vería un manejador de interrupciones. Aún no registra un manejador de interrupciones real mediante `bus_setup_intr(9)`, ni divide el trabajo entre un manejador de filtro y un thread de interrupción. Eso lo cubre el capítulo 19. El capítulo 17 hace polling a través de callouts y syscalls desde el espacio de usuario; los callouts actúan como fuente de interrupción.
- **DMA.** Los anillos de descriptores, las listas scatter-gather, los tags de `bus_dma(9)`, los bounce buffers y los flushes de caché alrededor del DMA son materia de los capítulos 20 y 21. El registro de datos del capítulo 17 sigue siendo un único slot de 32 bits.
- **Simuladores de sistema completo.** QEMU, bhyve, los dispositivos virtio y los tipos de simulación que ofrecen los hipervisores se mencionan de pasada como puente hacia el capítulo 18. El capítulo 17 permanece en el kernel; la simulación se ejecuta dentro del mismo kernel que el driver.
- **Verificación de protocolos y métodos formales.** La verificación de hardware real utiliza herramientas formales para demostrar que el tratamiento del protocolo de un driver es correcto. Esas herramientas están fuera del ámbito de este libro. El capítulo 17 usa `INVARIANTS`, `WITNESS`, pruebas de estrés e inyección deliberada de fallos para generar confianza.
- **Emulación de dispositivos respaldada por hipervisor.** La simulación del capítulo 17 se ejecuta en el espacio de direcciones del propio kernel. Los dispositivos virtio en una VM, las NICs emuladas en QEMU y la emulación de dispositivos de bhyve se ejecutan en un hipervisor; el driver los ve a través de PCI real. Esa ruta es responsabilidad del capítulo 18.

Mantenerse dentro de esos límites hace del capítulo 17 un capítulo sobre cómo lograr que un dispositivo falso se comporte como uno real. El vocabulario es lo que se transfiere; los subsistemas concretos son lo que los capítulos 18 al 22 aplican a ese vocabulario.

### Tiempo estimado de dedicación

- **Solo lectura**: tres o cuatro horas. Los conceptos de simulación son pequeños pero se acumulan; cada sección introduce un nuevo patrón de comportamiento y es la composición la que hace el capítulo rico.
- **Lectura más escritura de los ejemplos resueltos**: ocho o diez horas en dos sesiones. El driver evoluciona en cinco etapas; cada etapa es una pequeña refactorización que se integra en el `myfirst_hw.c` existente o en el nuevo `myfirst_sim.c`.
- **Lectura más todos los laboratorios y desafíos**: trece a dieciséis horas en tres o cuatro sesiones, incluyendo pruebas de estrés con el kernel de depuración, el trabajo con los escenarios de inyección de fallos y la lectura de un par de drivers reales basados en callout.

Las secciones 3, 6 y 7 son las más densas. Si la interacción entre un callout, un mutex y un cambio de estado resulta opaca en la primera lectura, es algo esperable: tres primitivas de la Parte 3 se están combinando de una manera nueva. Detente, vuelve a leer la tabla de tiempos de la Sección 6 y continúa cuando la composición haya quedado clara.

### Requisitos previos

Antes de comenzar este capítulo, confirma:

- El código fuente de tu driver coincide con la etapa 4 del Capítulo 16 (`0.9-mmio`). El punto de partida asume todos los accesores de registros del Capítulo 16, la división entre `myfirst.c` y `myfirst_hw.c`, las macros `CSR_*` y el registro de accesos.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con `/usr/src` en disco y coincidente con el kernel en ejecución.
- Un kernel de depuración con `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` y `KDB_UNATTENDED` está construido, instalado y arrancando correctamente.
- Tienes soltura con `callout_init_mtx`, `callout_reset_sbt` y `callout_drain` del Capítulo 13, y con `taskqueue` del Capítulo 14.
- Entiendes `arc4random(9)` a nivel de lectura. Si te resulta nuevo, repasa la página del manual antes de la Sección 7.

Si alguno de los puntos anteriores no está claro, corrígelo ahora en lugar de avanzar por el Capítulo 17 intentando razonar sobre una base inestable. El código de simulación produce bugs sensibles al tiempo, y un kernel de depuración junto con unas bases sólidas de la Parte 3 detecta la mayoría de ellos en el primer contacto.

### Cómo sacar el máximo partido de este capítulo

Tres hábitos darán sus frutos rápidamente.

En primer lugar, mantén abierto un segundo terminal en la máquina en la que estás probando. La simulación hace que los registros cambien entre dos lecturas de sysctl, y la única forma de verlo es leer el mismo sysctl dos veces seguidas. Un shell que ejecute en bucle `sysctl dev.myfirst.0.reg_status` cada 200 milisegundos es la forma en que los ejemplos del capítulo cobran vida. Si no puedes tener dos terminales en paralelo, un bucle `watch -n 0.2 sysctl ...` en un panel de tmux funciona igual de bien.

En segundo lugar, lee el registro de accesos después de cada experimento. El registro captura cada acceso a registros con una marca de tiempo, un desplazamiento, un valor y una etiqueta de contexto. Para una simulación estática el registro es aburrido: lecturas desde espacio de usuario, escrituras desde espacio de usuario, nada más. Para la simulación del Capítulo 17 el registro es denso: callouts disparándose, eventos retardados activándose, fallos inyectándose, timeouts expirando. Todo comportamiento interesante deja un rastro, y ese rastro es de donde proviene la intuición del capítulo.

En tercer lugar, rompe la simulación a propósito. Establece la tasa de inyección de fallos al 100%. Establece el intervalo del callout a cero. Haz que el dispositivo simulado esté siempre ocupado. Observa cómo reacciona el driver. Las lecciones de gestión de errores del capítulo son mucho más claras cuando has visto desarrollarse los modos de fallo. La simulación es segura: puedes inyectar tantos fallos como quieras sin arriesgar el hardware real, y en el peor caso obtendrás un kernel panic que un kernel de depuración detecta limpiamente.

### Hoja de ruta del capítulo

Las secciones en orden son:

1. **¿Por qué simular hardware?** El argumento a favor de la simulación como parte permanente del desarrollo de drivers, la diferencia entre una buena simulación y una engañosa, y los objetivos que persigue la simulación del Capítulo 17.
2. **Diseño del mapa de registros.** Cómo elegir registros, anchos, distribuciones y semántica de acceso para un dispositivo simulado, con un ejemplo resuelto de las incorporaciones que hace el Capítulo 17 sobre el mapa del Capítulo 16.
3. **Implementación del backend de hardware simulado.** El primer callout que actualiza los registros de forma autónoma, un helper `myfirst_sim_tick` que lleva la máquina de estados de la simulación, y la primera etapa del driver del Capítulo 17 (`1.0-sim-stage1`).
4. **Exposición del dispositivo falso al driver.** El tag, el handle, la ruta de acceso y cómo la abstracción del Capítulo 16 se mantiene sin modificaciones. Una sección breve; el punto es que el trabajo del Capítulo 16 ya hizo la mayor parte.
5. **Prueba del comportamiento del dispositivo desde el driver.** Escribir un comando, hacer polling del estado, leer datos y gestionar la condición de carrera entre la observación del driver y las propias actualizaciones de estado de la simulación. Etapa 2 del driver del Capítulo 17.
6. **Incorporación de temporización y retardo.** `DELAY(9)` en escala de microsegundos, `pause_sbt(9)` en escala de milisegundos y `callout_reset_sbt(9)` en escala de segundos; cuándo es seguro cada uno; cuándo no lo es; qué latencia puede esperar el driver de cada uno. Etapa 3.
7. **Simulación de errores y condiciones de fallo.** Un framework de inyección de fallos construido sobre la simulación, con modos que cubren timeouts, errores de datos, bloqueo en estado busy y fallos aleatorios. Enseñar al driver a detectar, recuperar o reportar. Etapa 4.
8. **Refactorización y versionado del driver de hardware simulado.** La división final en `myfirst_sim.c`, un nuevo `SIMULATION.md`, el incremento de versión a `1.0-simulated` y la pasada de regresión. Etapa 5.

Tras las ocho secciones vienen los laboratorios prácticos, los ejercicios de desafío, una referencia de resolución de problemas, un apartado de cierre que concluye la historia del Capítulo 17 y abre la del Capítulo 18, y un puente hacia el Capítulo 18. El material de referencia al final del capítulo está pensado para tenerlo a mano mientras trabajas en los laboratorios.

Si es tu primera lectura, lee de forma lineal y haz los laboratorios en orden. Si estás revisitando el capítulo, las Secciones 3, 6 y 7 funcionan razonablemente bien de forma independiente y son buenas lecturas para una sola sesión.



## Sección 1: ¿Por qué simular hardware?

El Capítulo 16 ya argumentó a favor de la simulación como rampa de acceso. Un lector que no tenga el dispositivo exacto al que el libro hace referencia puede igualmente practicar el acceso a registros contra un bloque simulado. Ese argumento es válido y sigue siendo aplicable en el Capítulo 17, pero no es la única razón por la que la simulación merece un capítulo propio. La simulación forma parte de cómo se hace el desarrollo serio de drivers, y comprender por qué es de lo que trata esta sección.

### El kit de herramientas del autor de drivers en activo

Toma cualquier driver real de FreeBSD y recorre su historial de commits hacia atrás. La mayoría de los drivers en desarrollo activo comparten un patrón común: los primeros commits construyen el driver contra hardware real. Los commits posteriores, a menudo muchos commits posteriores, añaden tests, arneses, scaffolding e instrumentación que permiten ejercitar el driver sin que el hardware original esté en el escritorio del desarrollador. Parte de esta instrumentación se ejecuta en espacio de usuario. Parte de ella es un dispositivo falso respaldado por un hipervisor que habla el mismo protocolo que el real. Parte de ella es un conjunto de stubs en el lado del kernel que simulan ser el hardware con suficiente fidelidad para ejecutar las máquinas de estados del driver.

¿Por qué invierten en esto los autores de drivers en activo? Porque el hardware real no es reproducible. El comportamiento de un dispositivo real depende de la revisión de su firmware, su temperatura, su dispositivo par en el enlace, la integridad de la señal de su conexión física, y los caprichos del azar. Un driver que pasa los tests contra una instancia de un dispositivo puede fallar contra una instancia ligeramente diferente. Un driver que pasa los tests hoy puede fallar mañana porque la actualización automática del firmware del dispositivo se aplicó durante la noche. Un driver cuyo fallo depende del tiempo puede pasar cien ejecuciones y fallar en la número ciento uno. El hardware real es un objetivo en movimiento, y un objetivo en movimiento es una mala base para las pruebas de regresión.

La simulación fija el objetivo. Un dispositivo simulado se comporta de la misma manera cada vez que llegan las mismas entradas. Las condiciones de fallo de un dispositivo simulado están bajo el control del autor. Un dispositivo simulado puede ejecutarse en un bucle cerrado sin desgastarse. Un dispositivo simulado no requiere hardware de laboratorio, cableado, una fuente de alimentación ni la ayuda de un compañero. Cada propiedad que hace que la simulación sea conveniente para el aprendizaje la hace valiosa para las pruebas en producción.

La conclusión es que la simulación del Capítulo 17 no es scaffolding desechable. Los patrones que aprendes aquí son los mismos que los committers de FreeBSD usan para probar drivers a los que ya nadie tiene acceso porque el silicio tiene veinte años. Las mismas técnicas detectan regresiones antes de que lleguen a la máquina de un cliente. La misma disciplina, aplicada al dispositivo PCI real del Capítulo 18, permite ejercitar el driver en una estación de trabajo sin red en absoluto diciéndole a un backend virtio-net que descarte paquetes, corrompa tramas o duplique descriptores a una tasa controlada.

### Objetivos de la simulación: comportamiento, temporización y efectos secundarios

Cuando decimos «simular un dispositivo», generalmente nos referimos a una de tres cosas, y vale la pena ser explícitos sobre cuál de ellas pretendemos.

El primer tipo de simulación es **de comportamiento**. La simulación expone una interfaz de registros, y las escrituras y lecturas producen los mismos efectos que produciría un dispositivo real. La máquina de estados es correcta: escribir `CTRL.GO` lleva al dispositivo de inactivo a ocupado a terminado, estableciendo `STATUS.DATA_AV` en el momento adecuado. La ruta de datos es correcta: los datos escritos en `DATA_IN` aparecen en `DATA_OUT` tras algún procesamiento. La simulación no tiene que coincidir con la temporización del dispositivo real; solo tiene que seguir las mismas reglas.

El segundo tipo de simulación es **consciente de la temporización**. La simulación impone los retardos que impondría el dispositivo real. Una escritura que el dispositivo real necesita 500 microsegundos para procesar tarda 500 microsegundos en la simulación, más o menos, sujeta a la resolución temporal del propio CPU. Un bit de estado que el dispositivo real activa 2 milisegundos después de un comando se activa 2 milisegundos después del comando en la simulación. Un driver que pasa la simulación consciente de la temporización es probable que funcione en el hardware real; un driver que solo pasa la simulación de comportamiento puede fallar cuando la latencia del dispositivo real produce una condición de carrera que el driver no ha contemplado.

El tercer tipo de simulación es **consciente de los efectos secundarios**. La simulación refleja las partes sutiles del protocolo: lecturas que borran el estado, escrituras que no tienen efecto, registros que devuelven todo unos cuando el dispositivo está en un determinado modo, bits de escritura-uno-para-borrar frente a bits de lectura-para-borrar. Un driver que pasa este tipo de simulación tiene confianza en los detalles del protocolo de bajo nivel, no solo en el flujo de estados de alto nivel.

Los tres tipos se combinan. Una simulación realista es los tres a la vez: comportamiento, temporización y efectos secundarios. El Capítulo 17 construye una simulación de los tres tipos, aunque la precisión temporal está limitada por lo que puede ofrecer la maquinaria de planificación del kernel. El objetivo no es igualar al silicio microsegundo a microsegundo; el objetivo es hacer la simulación suficientemente realista como para que las rutas de protocolo importantes del driver sean ejercitadas y las condiciones de carrera que esas rutas contienen tengan la oportunidad de manifestarse.

### Lo que una buena simulación enseña y lo que no

Una buena simulación enseña protocolo. Ejercita cada rama de la máquina de estados del driver, cada ruta de error, cada comprobación de estado, cada tolerancia a retardos. Un driver escrito contra una buena simulación es robusto frente a los escenarios que la simulación cubre.

Una buena simulación no enseña silicio. La jerarquía de memoria del CPU, los pipelines internos del dispositivo, la integridad de la señal de las pistas del PCB, la interacción con el resto del tráfico del sistema, el comportamiento térmico del chip bajo carga: nada de esto está en la simulación. Un driver que pasa la simulación puede fallar igualmente en hardware real si el fallo está enraizado en la física.

Esta distinción importa. La simulación del Capítulo 17 es una herramienta de enseñanza de protocolos, y el driver que produce es un vehículo de aprendizaje, no un driver de red en producción. Los Capítulos 20 al 22 introducen DMA, interrupciones y consideraciones de rendimiento del dispositivo real, todos los cuales extienden la simulación en direcciones que necesitan hardware real para validar completamente. El Capítulo 17 es el primer paso de una progresión más larga: que el protocolo sea correcto y, luego, que los detalles del hardware real también lo sean.

Hay una segunda sutileza que merece mención. Una simulación puede enseñar el protocolo *equivocado* si es inconsistente con el dispositivo real. Un lector que aprende a esperar el comportamiento de la simulación y luego se encuentra con hardware real cuyo comportamiento difiere se sentirá confundido. La simulación del Capítulo 17 está diseñada deliberadamente para coincidir con los patrones comunes en los drivers reales de FreeBSD: `STATUS.READY` se borra mientras el dispositivo está ocupado y se activa cuando está listo, `STATUS.DATA_AV` permanece activo hasta que el driver lee los datos, `INTR_STATUS` es de lectura-para-borrar, `CTRL.RESET` es de escritura-uno-para-auto-borrar. Estos patrones no son universales; diferentes dispositivos eligen diferentes convenciones. Pero son comunes, y un driver que los gestiona bien ha adquirido una habilidad transferible.

### Sin dependencia de hardware: el beneficio práctico

Para este libro, la ventaja más inmediata de la simulación es que el lector no necesita hardware específico para seguir el material. Un lector que trabaje el Capítulo 16 en un portátil sin dispositivos externos, en una máquina virtual sin PCI passthrough, o en un ordenador de placa única ARM con solo los periféricos integrados, puede igualmente compilar el driver y observar su comportamiento. El Capítulo 17 conserva esa propiedad. La simulación vive íntegramente en la memoria del kernel; el driver no necesita nada más que un sistema FreeBSD 14.3 en ejecución.

Esto importa en un libro de enseñanza. Un lector que tiene que detenerse, pedir hardware, instalarlo y esperar la entrega antes de poder continuar es un lector que quizás no continúe. Un lector que puede compilar y ejecutar el driver en la misma sesión en la que lee el capítulo es un lector que está aprendiendo. Cada laboratorio y ejercicio de desafío del Capítulo 17 está diseñado para ejecutarse en cualquier sistema FreeBSD x86 sin hardware externo.

Una ventaja secundaria es que la simulación permite al libro adelantarse a lo que el hardware del lector puede hacer. El driver del Capítulo 17 ejercita rutas de error que un dispositivo real casi nunca dispararía en condiciones normales de funcionamiento. A un lector que posea una tarjeta de red de producción le costaría forzar la tarjeta en los tipos de modos de fallo que enseña la inyección de fallos del Capítulo 7. Un dispositivo simulado puede configurarse para fallar a petición, tantas veces como sea necesario, en cualquier condición que el lector quiera poner a prueba. La superficie de aprendizaje es más amplia.

### Control total: el beneficio pedagógico

La simulación te da más control del que el hardware real puede ofrecerte jamás. Puedes congelar el tiempo en la simulación, avanzar un número conocido de ticks y observar cada estado intermedio. Puedes desactivar el comportamiento autónomo de la simulación, realizar una única manipulación y volver a activarlo. Puedes inyectar un fallo concreto, observar cómo reacciona el driver y reparar después el fallo cambiando el valor de un sysctl. Nada de esto es posible con silicio real.

Este nivel de control tiene un valor pedagógico enorme. Un capítulo que explica "cuando el dispositivo está ocupado, el driver debe esperar a `STATUS.READY`" puede demostrar el escenario de forma concreta. El capítulo 17 hace exactamente eso: un sysctl le indica a la simulación que entre en un estado de falsa ocupación, y tú puedes escribir comandos mientras el driver espera pacientemente a que se limpie el bit de ocupado. En hardware real, reproducir este escenario de forma fiable requeriría una carga de trabajo específica que el dispositivo rechazaría, un banco de pruebas artificial o un depurador. En la simulación, basta con cambiar un sysctl en una sola línea.

El control también significa que puedes experimentar sin consecuencias. Asignar un valor ilegal a un registro, enviar un comando que el dispositivo no espera, pedirle al driver que acceda a un registro fuera del rango mapeado: todo esto es seguro en la simulación. El peor caso es que salte un KASSERT y se produzca un pánico que un kernel de depuración captura limpiamente. Puedes probar diez ideas erróneas en una hora, aprender de cada una y salir fortalecido de la experiencia. Experimentar con hardware real conlleva riesgos reales, y los lectores (con razón) actúan con más cautela.

### Experimentos seguros: el beneficio de reducción de riesgos

La palabra "seguro" merece una explicación más detallada. Un pánico del kernel provocado por un dispositivo simulado es una molestia; un pánico provocado por un dispositivo real puede ser algo peor. Un driver que escribe un bit incorrecto en el registro `CTRL` de un dispositivo real puede dejarlo inutilizable. Un driver que corrompe la configuración DMA de un dispositivo real puede escribir basura en posiciones arbitrarias de RAM. Un driver que gestiona mal una interrupción real puede dejar el sistema en un estado en el que las interrupciones legítimas se pierden indefinidamente. Cada uno de estos modos de fallo ha aparecido en versiones de producción de FreeBSD a lo largo de los años, y cada uno ha requerido una actualización del kernel para solucionarlo.

La simulación elimina la mayor parte de este riesgo. El dispositivo simulado es una struct en memoria del kernel; un bit incorrecto va a parar a memoria que no está conectada a nada más. El DMA simulado no existe todavía (llega en el capítulo 20). Las interrupciones simuladas son callouts que el driver puede detener en cualquier momento. Un principiante que escribe su primer driver puede cometer todos los errores que contiene el ciclo de vida del desarrollo de drivers sin causar ningún problema físico.

Merece la pena interiorizar esto. La simulación del capítulo 17 te da permiso para experimentar de forma agresiva, que es exactamente lo que necesita hacer un principiante para ganar confianza. Un lector que nunca ha roto un driver es un lector que todavía no ha aprendido cómo se rompen los drivers. El capítulo 17 es el capítulo donde aprendes tanto a romper un driver adrede como a reconocerlo cuando se rompe por sí solo.

### Qué simularemos en el capítulo 17

Una lista breve de los comportamientos concretos que el capítulo presentará. Cada uno se añade en la sección que lo trata; la lista que aparece aquí es una hoja de ruta.

- **Actualizaciones autónomas de `STATUS`.** Un callout se dispara cada 100 milisegundos y actualiza los campos de `STATUS` como si un monitor hardware en segundo plano estuviera muestreando el estado interno del dispositivo. El driver puede observar las actualizaciones; la suite de pruebas puede verificarlas.
- **Eventos retardados desencadenados por comandos.** Escribir `CTRL.GO` programa un callout que, tras un retardo configurable (500 milisegundos por defecto), activa `STATUS.DATA_AV` y copia `DATA_IN` en `DATA_OUT`. Este es el patrón canónico del "dispositivo asíncrono".
- **`INTR_STATUS` con limpieza en lectura.** Leer `INTR_STATUS` devuelve los bits de interrupción pendientes actuales y los borra de forma atómica. Escribir en `INTR_STATUS` no tiene efecto (los dispositivos reales varían; algunos permiten escribir un uno para borrar, que es una variante común que discutiremos).
- **Datos de sensor simulados.** Un registro `SENSOR` contiene un valor que la simulación actualiza de forma periódica, imitando una lectura de temperatura o presión. El driver puede consultarlo mediante polling; una herramienta en espacio de usuario puede representarlo gráficamente.
- **Tolerancia a timeouts.** La ruta de comandos del driver espera hasta un número configurable de milisegundos a que `STATUS.DATA_AV` se active. Si la simulación está configurada para retardar más que el timeout, el driver informa del timeout y se recupera.
- **Inyección aleatoria de fallos.** Un sysctl controla la probabilidad de que cualquier operación inyecte un fallo. Los tipos de fallo incluyen: `STATUS.ERROR` activado tras un comando, el siguiente comando con timeout completo, una lectura que devuelve `0xFFFFFFFF`, el dispositivo reportando ocupación falsa indefinidamente. Cada fallo ejercita una ruta diferente del driver.

Todos estos comportamientos se implementan en la sección 8. El capítulo los construye uno a uno para que puedas interiorizar cada uno antes de seguir adelante.

### El lugar de la simulación en el libro

El capítulo 17 se sitúa entre el capítulo 16 (acceso estático a registros) y el capítulo 18 (hardware PCI real). Es el puente entre el vocabulario abstracto y el dispositivo concreto. Un lector que completa el capítulo 17 tiene un driver que ha ejercitado casi todos los patrones de protocolo que el libro discutirá, contra un dispositivo simulado que se comporta como uno real. Ese lector llega al capítulo 18 con un driver que ya tiene una estructura cercana a la calidad de producción y que solo necesita el pegamento PCI real para ejecutarse en silicio real.

La simulación es también una referencia. Cuando el capítulo 18 reemplaza el bloque de registros simulado por un BAR PCI real, puedes comparar el comportamiento de la simulación con el del dispositivo real y observar las diferencias. La simulación es el punto de referencia. Cuando el capítulo 19 añade interrupciones, los cambios de estado impulsados por callouts de la simulación se convierten en el modelo de aquello a lo que reacciona un manejador de interrupciones. Cuando el capítulo 20 añade DMA, los registros de datos de la simulación se convierten en el modelo de lo que el motor DMA lee y escribe. La simulación es el vocabulario docente que todos los capítulos posteriores amplían.

### Cerrando la sección 1

La simulación es una parte permanente del desarrollo de drivers, no un andamiaje temporal. Proporciona a los autores de drivers pruebas reproducibles, inyección controlada de fallos y la capacidad de ejercitar rutas de error que el hardware real raramente genera. El capítulo 17 construye una simulación de tres tipos (de comportamiento, con conciencia del tiempo, con conciencia de los efectos secundarios) sobre el driver del capítulo 16. La simulación está deliberadamente diseñada para coincidir con los patrones comunes de los drivers reales de FreeBSD, de modo que tu intuición se transfiera al hardware real más adelante.

Los objetivos concretos del capítulo incluyen actualizaciones autónomas de `STATUS`, eventos retardados desencadenados por comandos, semántica de limpieza en lectura, datos de sensor simulados, tolerancia a timeouts e inyección aleatoria de fallos. Cada uno se presenta en la sección que lo necesita; juntos producen un driver lo suficientemente rico como para ejercitar casi todas las lecciones de la parte 3.

La sección 2 da el siguiente paso. Antes de simular el comportamiento del dispositivo, necesitamos un mapa de registros sobre el que opere ese comportamiento. El capítulo 16 nos proporcionó un mapa estático; el capítulo 17 lo extiende para el comportamiento dinámico.



## Sección 2: diseño del mapa de registros

El capítulo 16 dotó al dispositivo simulado de un mapa de registros. El trabajo del capítulo 17 extiende ese mapa, pero las extensiones no son automáticas. Añadir un registro a un dispositivo real requiere un equipo de ingenieros de hardware, una nueva iteración del silicio y una hoja de datos actualizada. Añadir un registro a un dispositivo simulado es sencillo en términos mecánicos, pero el trabajo de diseño (¿qué debe hacer el registro? ¿quién lo escribe? ¿quién lo lee? ¿cuáles son sus efectos secundarios?) es el mismo, y la disciplina necesaria para hacerlo bien también lo es.

Esta sección recorre el diseño, no solo el resultado. Al final de la sección 2 tendrás un mapa de registros que soporta todos los comportamientos enumerados en la sección 1, documentado con suficiente detalle como para que la implementación en las secciones 3 a 7 sea casi mecánica. El ejercicio es transferible: cada vez que leas una hoja de datos real, reconocerás las decisiones que tomaron los diseñadores de hardware y tendrás un vocabulario para evaluar si esas decisiones fueron acertadas.

### ¿Qué es un mapa de registros?

Un mapa de registros es el catálogo completo de la interfaz de registros de un dispositivo. Enumera cada registro, cada desplazamiento, cada anchura, cada campo, cada bit, cada tipo de acceso, cada valor de reset y cada efecto secundario. El mapa de registros de un dispositivo real puede ocupar cientos de páginas; el de un dispositivo sencillo puede caber en una sola página. La forma es similar en ambos casos: una tabla, o un conjunto de tablas, con encabezados que responden a las preguntas que necesita formular el autor de un driver.

Un mapa de registros no es prosa. Es un documento de referencia. El autor del driver lo consulta al escribir código, y el autor de las pruebas lo consulta al escribir los tests. Un mapa de registros vago, incompleto o ambiguo produce drivers que fallan de maneras difíciles de diagnosticar. Un mapa de registros preciso produce drivers que se ajustan a lo que el dispositivo espera.

Para un dispositivo simulado, el mapa de registros desempeña un papel adicional. Es también la especificación frente a la cual se comprueba el comportamiento de la simulación. Si la simulación no concuerda con el mapa de registros, la simulación está mal y debe corregirse. Si el driver no concuerda con el mapa de registros, el driver está mal y debe corregirse. El mapa es el contrato. Los cambios en el mapa requieren cambios tanto en la simulación como en el driver. Así es exactamente como funciona el desarrollo de dispositivos reales: la hoja de datos es el contrato, y el equipo de hardware, el equipo de firmware y el equipo de drivers trabajan todos conforme a ella.

### El mapa del capítulo 16 como punto de partida

Recuerda el mapa de registros del capítulo 16:

| Desplazamiento | Anchura | Nombre          | Acceso    | Comportamiento en el capítulo 16               |
|----------------|---------|-----------------|-----------|------------------------------------------------|
| 0x00           | 32 bit  | `CTRL`          | R/W       | Memoria de lectura-escritura simple.           |
| 0x04           | 32 bit  | `STATUS`        | R/W       | Memoria de lectura-escritura simple.           |
| 0x08           | 32 bit  | `DATA_IN`       | R/W       | Memoria de lectura-escritura simple.           |
| 0x0c           | 32 bit  | `DATA_OUT`      | R/W       | Memoria de lectura-escritura simple.           |
| 0x10           | 32 bit  | `INTR_MASK`     | R/W       | Memoria de lectura-escritura simple.           |
| 0x14           | 32 bit  | `INTR_STATUS`   | R/W       | Memoria de lectura-escritura simple.           |
| 0x18           | 32 bit  | `DEVICE_ID`     | R-only    | Fijo a `0x4D594649`.                           |
| 0x1c           | 32 bit  | `FIRMWARE_REV`  | R-only    | Fijo a `0x00010000`.                           |
| 0x20           | 32 bit  | `SCRATCH_A`     | R/W       | Memoria de lectura-escritura simple.           |
| 0x24           | 32 bit  | `SCRATCH_B`     | R/W       | Memoria de lectura-escritura simple.           |

Diez registros, 40 bytes de espacio definido, 64 bytes asignados para dejar margen de crecimiento. Cada acceso es directo: las lecturas devuelven lo que la última escritura almacenó, las escrituras van directamente a memoria. Sin efectos secundarios en las lecturas. Sin efectos secundarios en las escrituras. Sin máquina de estados de hardware detrás de los registros.

El capítulo 17 cambia dos cosas en este mapa. En primer lugar, los registros existentes adquieren semántica de comportamiento: `STATUS.DATA_AV` será establecido por la simulación, no solo por las escrituras del driver; `INTR_STATUS` pasa a tener limpieza en lectura. En segundo lugar, el mapa añade un pequeño número de registros nuevos que necesita la simulación del capítulo 17: un registro `SENSOR`, un registro `DELAY_MS`, un registro `FAULT_MASK` y algunos más. La asignación de 64 bytes del capítulo 16 ya tiene espacio suficiente; no es necesario ningún cambio en el asignador.

### Semántica de acceso a registros

Antes de escribir ningún registro, debemos nombrar la semántica de acceso que nos importa. La semántica de acceso de un registro son las reglas que determinan qué ocurre cuando el driver lo lee o lo escribe. Las categorías principales de uso común son:

**Solo lectura (RO).** El registro devuelve un valor que el dispositivo o la simulación ha producido. Las escrituras se ignoran o generan un error. `DEVICE_ID` y `FIRMWARE_REV` son ejemplos.

**Lectura/escritura (RW).** El registro almacena lo que el driver escriba. Las lecturas devuelven el último valor escrito. `CTRL`, `SCRATCH_A` y `SCRATCH_B` son ejemplos.

**Solo escritura (WO).** El registro acepta escrituras, pero las lecturas devuelven un valor fijo (habitualmente cero, a veces basura y en ocasiones el último valor leído de otra fuente). `DATA_IN` es frecuentemente de solo escritura en dispositivos reales.

**Lectura con borrado automático (RC).** Leer el registro devuelve su valor actual y lo borra a continuación. Las escrituras se ignoran habitualmente. `INTR_STATUS` es el ejemplo clásico.

**Escritura de uno para borrar (W1C).** Escribir un 1 en un bit lo borra; escribir un 0 no tiene ningún efecto. Las lecturas devuelven el valor actual. `INTR_STATUS` en algunos dispositivos utiliza W1C en lugar de RC.

**Escritura de uno para activar (W1S).** Escribir un 1 activa el bit; escribir un 0 no tiene ningún efecto. Es menos habitual que W1C, pero se utiliza en cierto hardware para registros de control donde el driver no debería tener que hacer una lectura-modificación-escritura.

**Lectura/escritura con efecto secundario (RWSE).** El registro se puede leer y escribir, pero la escritura (o la lectura) desencadena algo más allá de una simple actualización en memoria. `CTRL` cae con frecuencia en esta categoría: escribir `CTRL.RESET` provoca un reinicio del dispositivo, que es un efecto secundario.

**Sticky (con latch).** Un bit que el hardware ha activado permanece activado hasta que el driver lo borra explícitamente. Los bits de error en `STATUS` son habitualmente sticky. `STATUS.ERROR` será sticky en el mapa del Capítulo 17.

**Reservado.** El bit no tiene ningún significado definido. Las escrituras deben preservar el valor existente o escribir cero; las lecturas pueden devolver cualquier valor. Un driver que ignora los bits reservados es robusto ante futuras revisiones del hardware; un driver que depende del comportamiento de los bits reservados es frágil.

Cada tipo de acceso tiene implicaciones para el código del driver. Un driver que lee un registro RC debe hacerlo exactamente cuando el protocolo lo exige, porque cada lectura extra consume un evento. Un driver que lee un registro WO está leyendo basura, lo cual es un bug que tarde o temprano aflorará si alguna vez se actúa sobre ese valor. Un driver que realiza una lectura-modificación-escritura en un registro con bits sticky debe tener cuidado de no borrar un bit que no pretendía tocar. Un driver que escribe en un registro W1C utiliza `CSR_WRITE_4(sc, REG, mask)` y no `CSR_UPDATE_4(sc, REG, mask, 0)`; los dos tienen un aspecto similar, pero producen un comportamiento diferente en el hardware.

La simulación del Capítulo 17 utiliza la semántica RO, RW, RC, sticky y RWSE. No introduce W1C ni W1S (aunque los ejercicios de desafío invitan al lector a añadirlos). El objetivo de la simulación es cubrir los casos habituales de forma exhaustiva; los casos menos comunes se convierten en extensiones naturales una vez que se han comprendido los más habituales.

### Adiciones del capítulo 17

El mapa de registros del capítulo 17 añade los siguientes registros:

| Desplazamiento | Ancho   | Nombre          | Acceso    | Comportamiento en el capítulo 17                                           |
|----------------|---------|-----------------|-----------|----------------------------------------------------------------------------|
| 0x28           | 32 bits | `SENSOR`        | RO        | Valor del sensor simulado. Actualizado por un callout cada 100 ms.         |
| 0x2c           | 32 bits | `SENSOR_CONFIG` | RW        | Configuración del intervalo de actualización y la amplitud del sensor.     |
| 0x30           | 32 bits | `DELAY_MS`      | RW        | Número de milisegundos que tarda el dispositivo por comando.               |
| 0x34           | 32 bits | `FAULT_MASK`    | RW        | Qué fallos simulados están habilitados. Campo de bits.                     |
| 0x38           | 32 bits | `FAULT_PROB`    | RW        | Probabilidad de fallo aleatorio por operación (0..10000).                  |
| 0x3c           | 32 bits | `OP_COUNTER`    | RO        | Recuento de comandos que el dispositivo simulado ha procesado.             |

Seis nuevos registros, con desplazamientos de `0x28` a `0x3c`, que caben íntegramente dentro de la región de 64 bytes que el capítulo 16 reservó. No hay cambios en el softc, en el asignador ni en `bus_space_map`.

Cada registro tiene una función específica:

- **`SENSOR`** es la temperatura, presión o voltaje simulados. La interpretación exacta no importa para la simulación; lo que importa es que el valor cambia por sí solo, enseñando al lector cómo gestionar registros cuyo valor se produce de forma autónoma. El valor oscila en torno a un valor de referencia mediante una fórmula sencilla que el callout implementará.

- **`SENSOR_CONFIG`** permite que el driver (o el usuario a través de un sysctl) controle el comportamiento del sensor. Los 16 bits bajos son el intervalo de actualización en milisegundos; los 16 bits altos son la amplitud de la oscilación. Un valor de `0x0064_0040` significa «intervalo de 100 ms, amplitud 64». Al cambiar el registro se modifica la siguiente actualización de la simulación.

- **`DELAY_MS`** es el tiempo que tarda un comando en la simulación. Escribir `CTRL.GO` programa que `STATUS.DATA_AV` se active `DELAY_MS` milisegundos después. El valor por defecto es 500. Si se establece en 0, los comandos se completan en el siguiente tick del callout; si se establece en un valor elevado, el lector puede ejercitar la ruta de timeout del driver.

- **`FAULT_MASK`** es un campo de bits que selecciona qué modos de fallo están activos. El bit 0 activa el fallo «la operación agota el tiempo de espera»; el bit 1 activa el fallo «la lectura devuelve todos unos»; el bit 2 activa el fallo «el bit de error se activa tras cada comando»; el bit 3 activa el fallo «el dispositivo siempre está ocupado». Se pueden activar varios bits simultáneamente.

- **`FAULT_PROB`** controla la probabilidad de que una operación individual inyecte un fallo, en décimas de punto básico. Un valor de 10000 equivale al 100% (fallo siempre); 5000 equivale al 50%; 0 deshabilita por completo los fallos aleatorios. Esto le da al lector un control preciso sobre la agresividad con que se inyectan los fallos.

- **`OP_COUNTER`** cuenta cada comando que ha procesado la simulación. Es de solo lectura; las escrituras se ignoran. El driver puede usarlo para verificar que un comando llegó realmente a la simulación, y el usuario puede leerlo desde el espacio de usuario para comprobar la actividad.

### Adiciones de encabezado para los nuevos registros

El encabezado para las adiciones reside en `myfirst_hw.h`, extendiendo las definiciones del capítulo 16:

```c
/* Chapter 17 additions to the simulated device's register map. */

#define MYFIRST_REG_SENSOR        0x28
#define MYFIRST_REG_SENSOR_CONFIG 0x2c
#define MYFIRST_REG_DELAY_MS      0x30
#define MYFIRST_REG_FAULT_MASK    0x34
#define MYFIRST_REG_FAULT_PROB    0x38
#define MYFIRST_REG_OP_COUNTER    0x3c

/* SENSOR_CONFIG register fields. */
#define MYFIRST_SCFG_INTERVAL_MASK  0x0000ffffu  /* interval in ms */
#define MYFIRST_SCFG_INTERVAL_SHIFT 0
#define MYFIRST_SCFG_AMPLITUDE_MASK 0xffff0000u  /* oscillation range */
#define MYFIRST_SCFG_AMPLITUDE_SHIFT 16

/* FAULT_MASK register bits. */
#define MYFIRST_FAULT_TIMEOUT    0x00000001u  /* next op times out        */
#define MYFIRST_FAULT_READ_1S    0x00000002u  /* reads return 0xFFFFFFFF  */
#define MYFIRST_FAULT_ERROR      0x00000004u  /* STATUS.ERROR after op    */
#define MYFIRST_FAULT_STUCK_BUSY 0x00000008u  /* STATUS.BUSY latched on   */

/* CTRL register: GO bit (new for Chapter 17). */
#define MYFIRST_CTRL_GO          0x00000200u  /* bit 9: start command     */

/* STATUS register: BUSY and DATA_AV are now dynamic (set by simulation). */
/* (The mask constants were already defined in Chapter 16.)               */
```

El nuevo bit de CTRL `MYFIRST_CTRL_GO` no se solapa con ningún bit existente. `MYFIRST_CTRL_ENABLE` es el bit 0, `RESET` es el bit 1, `MODE_MASK` cubre los bits 4 a 7 y `LOOPBACK` es el bit 8. El nuevo bit `GO` en el bit 9 encaja sin problemas en el hueco.

Los comentarios en el encabezado son deliberadamente escuetos. En un driver real, los comentarios de este nivel señalarían la sección del datasheet correspondiente (por ejemplo, `/* See datasheet section 4.2.1 */`). Para la simulación, el datasheet es este capítulo; un lector que busque el comportamiento definitivo de `MYFIRST_FAULT_ERROR` deberá consultar la sección 7 del capítulo 17.

### Cómo escribir una tabla de mapa de registros para tu propio dispositivo

El ejercicio de escribir un mapa de registros es una de las cosas más valiosas que puedes hacer como autor de drivers principiante. Incluso si nunca escribes la simulación ni el driver, el simple hecho de sentarte a pensar «¿cuáles serían los registros de este dispositivo?» te obliga a pensar en el dispositivo como una entidad con un protocolo, en lugar de como una caja mágica.

Una plantilla útil:

1. Decide qué hace el dispositivo a alto nivel. Una frase.
2. Identifica los comandos que debe aceptar el dispositivo. Cada comando puede convertirse en un bit de un registro `CTRL`, en un valor de un registro `COMMAND` o en su propio registro.
3. Identifica los estados que informa el dispositivo. Cada estado puede convertirse en un bit de un registro `STATUS`, en un valor de un registro `STATE` o en su propio registro de solo lectura.
4. Identifica los datos que el dispositivo produce o consume. Los datos de entrada van en un registro `DATA_IN` o en una FIFO. Los datos de salida provienen de un registro `DATA_OUT` o de una FIFO.
5. Identifica las interrupciones que el dispositivo puede generar. Cada fuente de interrupción se convierte en un bit de `INTR_STATUS` (o un equivalente).
6. Identifica cualquier metadato que el driver necesite en el arranque. ID del dispositivo, revisión de firmware, indicadores de características, bits de capacidad.
7. Deja espacio para registros de configuración que el driver pueda querer ajustar. En el capítulo 17, `DELAY_MS`, `FAULT_MASK` y `FAULT_PROB` son registros de configuración; un dispositivo real podría tener umbrales de timeout, opciones de recuperación ante errores o configuraciones de gestión de energía.
8. Decide el ancho de cada registro. 32 bits es el valor por defecto habitual en los dispositivos modernos. Anchos más pequeños existen para dispositivos heredados o para dispositivos con un número de registros muy elevado.
9. Decide el desplazamiento de cada registro. Mantén los registros relacionados contiguos cuando sea posible. Alinea los desplazamientos al ancho del registro (los registros de 32 bits en desplazamientos divisibles por 4).
10. Decide la semántica de acceso (RO, RW, RC, W1C, ...). Sé explícito; la ambigüedad aquí produce bugs.
11. Decide el valor de reset de cada registro. ¿Qué debe contener el registro en el encendido? ¿En el reset? ¿Al hacer el attach del módulo?
12. Documenta los efectos secundarios de cada registro no trivial.

Esta lista es larga, pero cada paso es pequeño. Un equipo de dispositivo real podría tardar semanas en producir un mapa de registros tan detallado. El mapa de un dispositivo simulado puede producirse en una tarde. El mapa del capítulo 17 es el resultado de aplicar este ejercicio a un dispositivo de enseñanza.

### Ejemplo de diseño: disposición y decisiones

Para hacer que la disciplina sea concreta, veamos las decisiones de diseño que hay detrás de las adiciones del capítulo 17.

**Decisión 1: Agrupar los registros relacionados.** Los nuevos registros `SENSOR` (0x28) y `SENSOR_CONFIG` (0x2c) son adyacentes. Un driver que lea el código del sensor puede acceder a ambos con una sola lectura de región si es necesario, y un lector que examine el volcado de registros los ve juntos. La misma lógica hace que `FAULT_MASK` (0x34) y `FAULT_PROB` (0x38) sean adyacentes.

**Decisión 2: Colocar la configuración después de los registros de diagnóstico.** `DEVICE_ID` (0x18) y `FIRMWARE_REV` (0x1c) son registros de diagnóstico de solo lectura; van primero en la sección de desplazamientos altos. `SENSOR` y su configuración vienen a continuación. `DELAY_MS`, `FAULT_MASK` y `FAULT_PROB` son de configuración; van después. `OP_COUNTER` es otro registro de diagnóstico; va al final.

**Decisión 3: Usar el registro CTRL existente para el nuevo bit GO.** En lugar de añadir un registro `COMMAND` dedicado, el bit `GO` en `CTRL` es el mecanismo con el que el driver inicia un comando. Esto sigue el patrón del capítulo 16 (los bits `ENABLE` y `RESET` están ambos en `CTRL`) y ahorra un registro.

**Decisión 4: Hacer `OP_COUNTER` de solo lectura.** Un contador escribible invitaría a que hubiera bugs en los que el driver resetease accidentalmente el contador durante una operación. Un contador de solo lectura es siempre monótono (módulo el desbordamiento de 32 bits) y refleja siempre la perspectiva de la simulación.

**Decisión 5: Codificar dos campos en `SENSOR_CONFIG`.** En lugar de dos registros separados para el intervalo y la amplitud, un único registro con dos campos de 16 bits ahorra una ranura y sigue un patrón hardware habitual. Un dispositivo real podría tener un ID de comando de 8 bits en el byte alto y un argumento de 24 bits en los tres bytes bajos de un registro; el principio es el mismo.

**Decisión 6: Usar `FAULT_PROB` en décimas de punto básico (rango de 0 a 10000).** Usar 10000 como escala completa en lugar de 100 proporciona dos decimales adicionales de precisión. Probabilidades de fallo del 0,5%, el 0,75% o el 1,25% pueden expresarse como enteros sin aritmética fraccionaria.

Cada decisión es pequeña; la composición es lo que hace que el mapa sea utilizable. Un mapa de registros diseñado con descuido produce un driver lleno de parches incómodos; un mapa diseñado con cuidado produce un driver que casi se escribe solo.

### Convenciones de nomenclatura para las constantes

Una nota sobre las convenciones de nomenclatura que usa el capítulo 17 para las máscaras de bits, que son un reflejo de lo que hacen los drivers reales de FreeBSD.

El prefijo `MYFIRST_` identifica todo como perteneciente al driver. Dentro de ese prefijo, `REG_` indica un desplazamiento de registro, `CTRL_` indica un bit o campo en `CTRL`, `STATUS_` indica un bit en `STATUS`, y así sucesivamente. El patrón es `<driver>_<registro>_<campo>`. Por ejemplo, `MYFIRST_STATUS_READY` es el bit `READY` del registro `STATUS`. El nombre es largo, pero es inequívoco, y los editores modernos lo completan rápidamente.

Las máscaras que cubren varios bits terminan en `_MASK`; los desplazamientos para alinear la máscara terminan en `_SHIFT`. `MYFIRST_CTRL_MODE_MASK` es la máscara para el campo `MODE` de 4 bits; `MYFIRST_CTRL_MODE_SHIFT` es el número de bits que hay que desplazar a la derecha para llevar el campo al bit cero. Extraer el campo se lee como `(ctrl & MYFIRST_CTRL_MODE_MASK) >> MYFIRST_CTRL_MODE_SHIFT`; establecerlo se lee como `(mode << MYFIRST_CTRL_MODE_SHIFT) & MYFIRST_CTRL_MODE_MASK`.

Los valores fijos terminan en `_VALUE`. `MYFIRST_DEVICE_ID_VALUE` es el valor constante del registro `DEVICE_ID`. Esta convención hace que los valores sean distinguibles de las máscaras de un vistazo.

Los drivers reales a veces se apartan de esta convención, a menudo por razones históricas o porque los nombres de sus registros son largos. El driver `if_em` usa prefijos más cortos (`EM_` en lugar de `E1000_` en algunos lugares) para ajustarse a longitudes de línea razonables. El driver `if_ale` usa `CSR_READ_4(sc, ALE_REG_NAME)` con `ALE_` como prefijo. El principio es el mismo en todos los drivers: cada constante pertenece visiblemente al espacio de nombres de un driver, y el patrón dentro del espacio de nombres es consistente.

En el capítulo 17, la consistencia importa más que la brevedad. El lector todavía está aprendiendo los patrones; un nombre largo y explícito es más educativo que uno corto y ambiguo.

### Validar el mapa antes de escribir código

Antes de implementar la simulación, vale la pena validar el mapa con algunas comprobaciones básicas.

**Comprobación 1: Los desplazamientos caben dentro de la región reservada.** La región del capítulo 16 tiene 64 bytes. El desplazamiento más alto definido en el mapa del capítulo 17 es `0x3c`, con un ancho de 4, por lo que el último byte usado está en el desplazamiento `0x3f`. `0x40` queda justo fuera de la región reservada. El mapa encaja exactamente; no hay espacio para otro registro sin ampliar la asignación. Es una restricción de diseño que conviene conocer.

**Comprobación 2: Todos los desplazamientos están alineados a 4 bytes.** Todos los registros tienen 32 bits de ancho y todos los desplazamientos son múltiplos de 4. `bus_space_read_4` y `bus_space_write_4` requieren alineación a 4 bytes en la mayoría de las plataformas; un acceso no alineado provocaría un fallo en algunas arquitecturas y un comportamiento incorrecto silencioso en otras. El mapa sigue esta regla.

**Comprobación 3: Ningún registro se solapa con otro.** Cada registro de 32 bits ocupa 4 bytes consecutivos. Comprueba que los desplazamientos adyacentes difieren en al menos 4. Así es: 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c.

**Comprobación 4: Ningún bit reservado se usa para varios propósitos.** Un bit definido para un campo no debe aparecer en otro campo. `MYFIRST_CTRL_ENABLE` es el bit 0, `RESET` es el bit 1, `MODE` cubre los bits 4 a 7, `LOOPBACK` es el bit 8 y `GO` es el bit 9. No hay solapamiento.

**Comprobación 5: La semántica de acceso es coherente con la intención de la simulación.** `DEVICE_ID` y `FIRMWARE_REV` son RO, lo que significa que la simulación se negará a cambiarlos tras el attach. `INTR_STATUS` será RC, lo que significa que la simulación lo borrará cuando el driver lo lea. `OP_COUNTER` es RO, lo que significa que las escrituras no tienen efecto. Todo lo demás es RW (posiblemente con efectos secundarios). Esto es coherente con lo que describió la sección 1.

**Comprobación 6: Los valores de reset son razonables.** En el attach, `DEVICE_ID` vale `0x4D594649`, `FIRMWARE_REV` vale `0x00010000`, `STATUS` tiene `READY` activado y todo lo demás está a cero. `DELAY_MS` toma por defecto el valor 500 (500 ms por comando). `SENSOR_CONFIG` toma por defecto el valor 0x0064_0040 (intervalo de 100 ms, amplitud 64). `FAULT_MASK` toma por defecto 0 (ningún fallo habilitado). `FAULT_PROB` toma por defecto 0 (sin fallos aleatorios). Los valores por defecto describen una simulación que se comporta como un dispositivo fiable hasta que se le indique lo contrario.

Estas comprobaciones son el tipo de cosas que un autor de drivers disciplinado repasa antes de escribir ningún código. Llevan pocos minutos y permiten detectar varios bugs habituales antes de que aparezcan.

### Documentar el mapa

El capítulo 16 introdujo `HARDWARE.md`. El capítulo 17 lo amplía. Los títulos de sección en `HARDWARE.md` al final del capítulo 17 serán:

1. Versión y alcance
2. Tabla resumen de registros (cada registro, cada desplazamiento, cada anchura, cada tipo de acceso, cada valor por defecto)
3. Campos del registro CTRL (tabla de cada bit en CTRL)
4. Campos del registro STATUS (tabla de cada bit en STATUS)
5. Campos de INTR_MASK e INTR_STATUS (tabla de cada fuente de interrupción)
6. Campos de SENSOR_CONFIG (intervalo y amplitud)
7. Campos de FAULT_MASK (tabla de cada tipo de fallo)
8. Referencia registro a registro (un párrafo por registro, que describe el comportamiento, los efectos secundarios y el uso)
9. Secuencias habituales (escritura y lectura de verificación, comando y espera, inyección de fallos para pruebas)
10. Observabilidad (cada sysctl que lee o escribe un registro)
11. Notas de simulación (qué registros son dinámicos, cuáles son estáticos, qué hacen los callouts)

Este documento es extenso, quizás unas 100 líneas. Es la única fuente de verdad sobre lo que el driver espera de la simulación. Un lector que olvide el comportamiento de un registro abre `HARDWARE.md` y encuentra la respuesta. Un colaborador que añada un nuevo registro actualiza `HARDWARE.md` primero y luego cambia el código.

### Errores comunes en el diseño de mapas de registros

Los desarrolladores que diseñan su primer mapa de registros suelen cometer los mismos errores. Un breve catálogo para que puedas evitarlos.

**Error 1: Usar números mágicos en el código en lugar de constantes con nombre.** Un driver que escribe `CSR_WRITE_4(sc, 0x00, 0x1)` en lugar de `CSR_WRITE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE)` es difícil de leer e imposible de refactorizar. La regla es: cada desplazamiento es una constante; cada bit es una constante; cada campo es una macro.

**Error 2: Definiciones de bits solapados.** Dos nombres, un solo bit. Un lector que aplique un OR bit a bit sobre `FLAG_A` y `FLAG_B` obtiene un valor que corresponde a algo completamente distinto, porque `FLAG_A` y `FLAG_B` resultan ser el mismo bit. La defensa es un comentario junto a cada nombre de bit que indique su número de bit, más una comprobación en tiempo de compilación (`_Static_assert((MYFIRST_CTRL_ENABLE & MYFIRST_CTRL_RESET) == 0, "bits overlap")` si quieres ser precavido).

**Error 3: Accesos no alineados.** Definir un registro de 32 bits en el desplazamiento `0x06` parece inofensivo, pero falla en cualquier arquitectura donde los accesos no alineados no están permitidos. Alinea siempre los desplazamientos con la anchura del registro.

**Error 4: Olvidar los bits reservados.** Un registro de 32 bits en el que solo están definidos los bits 0 a 3 puede tener valores no especificados en los bits 4 a 31. Un driver que escribe `CSR_WRITE_4(sc, REG, 0xF)` funciona bien por ahora, pero una revisión futura que defina el bit 5 podría romperse porque el driver está escribiendo efectivamente cero en ese bit en cada operación. La defensa es el patrón lectura-modificación-escritura en registros con bits reservados: `CSR_UPDATE_4(sc, REG, mask_of_fields_we_touch, new_field_values)`.

**Error 5: Efectos secundarios en la lectura que el driver no espera.** Un registro RC que el driver lee con fines de depuración borra el estado sobre el que el driver estaba a punto de actuar. La defensa es documentar cada efecto secundario de forma explícita en el header y en `HARDWARE.md`, para que un lector pueda ver el peligro antes de escribir el bug.

**Error 6: Asumir que las escrituras siempre tienen éxito.** Una escritura en un registro RO puede ser ignorada silenciosamente, o puede provocar un fallo, o puede hacer algo inesperado en cierto hardware. La defensa es utilizar la semántica de acceso correcta y someter el uso de cada registro por parte del driver a pruebas de estrés.

**Error 7: Anchuras inconsistentes.** Un registro de 32 bits junto a uno de 8 bits donde la anchura de acceso no es obvia a partir del nombre. La defensa es hacer que la anchura forme parte de la llamada al accessor (`CSR_READ_4` frente a `CSR_READ_1`) y nombrar el tipo del registro en algún lugar visible.

**Error 8: Distribuciones de bits demasiado ingeniosas.** Un campo de 16 bits que abarca dos carriles de byte no adyacentes, lo que requiere recomposición de bits en el driver. Los dispositivos reales hacen esto a veces (a menudo por restricciones históricas), pero un dispositivo simulado debería ceñirse a campos contiguos. La simulación es un instrumento pedagógico; la claridad vence a la astucia.

**Error 9: Valores de reset no especificados.** Un registro cuyo valor de reset no está especificado obliga al driver a adivinar qué contiene el registro tras el attach. La defensa es inicializar cada registro a un valor documentado en la ruta de attach.

**Error 10: Documentación obsoleta.** El código cambia, la documentación no. Dos meses después el driver funciona pero la documentación miente. La defensa es la disciplina de actualizar `HARDWARE.md` en el mismo commit que cambia el código.

Cada error es pequeño de forma aislada y costoso en conjunto. Un driver escrito sin estos errores es un driver que se lee bien, se prueba bien y envejece bien.

### Cerrando la sección 2

Un mapa de registros es la especificación de la interfaz visible por el programador de un dispositivo. El mapa del capítulo 17 amplía el del capítulo 16 con seis nuevos registros: `SENSOR`, `SENSOR_CONFIG`, `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB` y `OP_COUNTER`. Cada registro tiene un propósito, una anchura, un tipo de acceso, un valor de reset y un comportamiento documentado. Vale la pena comprender las decisiones de diseño detrás de cada registro, porque reflejan las decisiones que toman los diseñadores de dispositivos reales.

El mapa está documentado en el header (`myfirst_hw.h`) y en `HARDWARE.md`. El header lo consume el compilador; el markdown lo consumen los humanos. Ambos se mantienen sincronizados a medida que el código evoluciona.

La sección 3 toma el mapa e implementa la simulación. El primer callout se activa; se produce la primera actualización autónoma de un registro; el bloque estático del capítulo 16 adquiere un latido.



## Sección 3: Implementación del backend de hardware simulado

La sección 2 produjo un mapa de registros. La sección 3 hace que ese mapa se comporte. El primer trabajo de la simulación es actualizar registros de forma autónoma, sin que el driver se lo pida. Así es como se comporta el hardware real: un sensor actualiza su valor en cada ciclo de reloj, un controlador de interrupciones captura eventos según llegan, el contador de recepción de una tarjeta de red se incrementa a medida que llegan tramas. El primer paso de simulación del capítulo 17 es producir un latido similar.

La herramienta es una que el lector ya conoce: un `callout`. El capítulo 13 presentó `callout_init_mtx` y `callout_reset` para los temporizadores internos del driver. El capítulo 17 utiliza la misma primitiva con un propósito distinto: dirigir el estado del dispositivo simulado. El callout se activa cada 100 milisegundos, adquiere el mutex del driver, actualiza algunos registros, libera el mutex y programa su próxima activación. Desde el punto de vista del driver, los registros simplemente cambian; desde el punto de vista de la simulación, hay un temporizador en marcha.

### El primer archivo de simulación

La distribución de archivos del capítulo 16 era `myfirst.c` para el ciclo de vida del driver, `myfirst_hw.c` para la capa de acceso al hardware y `myfirst_hw.h` para las definiciones compartidas. El capítulo 17 añade un nuevo archivo, `myfirst_sim.c`, para el backend de simulación. Esta separación mantiene el código de simulación fuera del archivo de acceso al hardware, de modo que un capítulo posterior pueda reemplazar `myfirst_sim.c` con código real orientado a PCI sin tocar los accessors.

Crea `myfirst_sim.c` junto a los archivos del capítulo 16. La primera versión es pequeña:

```c
/*-
 * myfirst_sim.c -- Chapter 17 simulated hardware backend.
 *
 * Adds a callout that drives autonomous register changes, a
 * command-scheduling callout for command-triggered delays, and
 * the fault-injection state that Section 7 will populate.
 *
 * This file assumes Chapter 16's register access layer
 * (myfirst_hw.h, myfirst_hw.c) is present and functional.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_sim.h"
```

Los includes siguen las convenciones de FreeBSD: `sys/param.h` primero, luego `sys/systm.h`, y después los headers de subsistemas específicos. `sys/random.h` será necesario para `arc4random` más adelante; incluirlo ahora evita una segunda edición. Los headers privados del driver van al final.

El archivo se compila frente a los `myfirst.h` y `myfirst_hw.h` existentes, más un nuevo `myfirst_sim.h` que contendrá la API de la simulación. Un esqueleto de ese header:

```c
/* myfirst_sim.h -- Chapter 17 simulation API. */
#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

struct myfirst_softc;

/* Attach/detach the simulation. Called from myfirst_attach/detach. */
int  myfirst_sim_attach(struct myfirst_softc *sc);
void myfirst_sim_detach(struct myfirst_softc *sc);

/* Enable/disable the simulation's autonomous updates. */
void myfirst_sim_enable(struct myfirst_softc *sc);
void myfirst_sim_disable(struct myfirst_softc *sc);

/* Command scheduling: called when the driver writes CTRL.GO. */
void myfirst_sim_start_command(struct myfirst_softc *sc);

/* Sysctl registration for simulation controls. */
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
```

Cinco funciones, un archivo de header, un archivo fuente. La API es pequeña. El estado interno de la simulación reside en un `struct myfirst_sim`, apuntado desde el softc a través de `sc->sim`, con el mismo patrón que `sc->hw`.

### La estructura de estado de la simulación

Dentro de `myfirst_sim.h`, define la estructura de estado:

```c
struct myfirst_sim {
        /* Autonomous update callout. Fires every sensor_interval_ms. */
        struct callout       sensor_callout;

        /* Command completion callout. Fires DELAY_MS after CTRL.GO. */
        struct callout       command_callout;

        /* Last scheduled command's data. Saved so command_cb can
         * latch DATA_OUT when it fires. */
        uint32_t             pending_data;

        /* Whether a command is currently in flight. */
        bool                 command_pending;

        /* Baseline sensor value; the callout oscillates around this. */
        uint32_t             sensor_baseline;

        /* Counter used by the sensor oscillation algorithm. */
        uint32_t             sensor_tick;

        /* Operation counter. Written to OP_COUNTER on every completion. */
        uint32_t             op_counter;

        /* Whether the simulation is running. Stops cleanly at detach. */
        bool                 running;
};
```

Siete campos. Dos son callouts (actualizaciones del sensor y finalización de comandos). Uno es estado booleano (comando en vuelo). Uno es el dato pendiente guardado. Tres son contadores internos. Uno es el indicador de ejecución.

El softc adquiere un puntero a la estructura de simulación:

```c
struct myfirst_softc {
        /* ... all existing fields, including hw ... */
        struct myfirst_sim   *sim;
};
```

La asignación y la inicialización tienen lugar en `myfirst_sim_attach`, que se llama desde `myfirst_attach` después de que `myfirst_hw_attach` ya haya configurado `sc->hw`. Colocar el attach de la simulación después del attach del hardware es deliberado: la simulación lee y escribe registros a través de los accessors del capítulo 16, así que los accessors deben estar listos antes de que la simulación arranque.

### El callout del sensor: la primera actualización autónoma

La actualización de simulación más pequeña y útil es el callout del sensor. Se activa cada 100 milisegundos, actualiza el registro `SENSOR` y se rearma. El código:

```c
static void
myfirst_sim_sensor_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t config, interval_ms, amplitude, phase, value;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        /* Read the current SENSOR_CONFIG. */
        config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
        interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
            MYFIRST_SCFG_INTERVAL_SHIFT;
        amplitude = (config & MYFIRST_SCFG_AMPLITUDE_MASK) >>
            MYFIRST_SCFG_AMPLITUDE_SHIFT;

        /* Compute a simple oscillation. phase cycles 0..7 and back. */
        sim->sensor_tick++;
        phase = sim->sensor_tick & 0x7;
        value = sim->sensor_baseline +
                ((phase < 4) ? phase : (7 - phase)) *
                (amplitude / 4);

        /* Publish. */
        CSR_WRITE_4(sc, MYFIRST_REG_SENSOR, value);

        /* Re-arm at the current interval. */
        if (interval_ms == 0)
                interval_ms = 100;
        callout_reset_sbt(&sim->sensor_callout,
            interval_ms * SBT_1MS, 0,
            myfirst_sim_sensor_cb, sc, 0);
}
```

Paso a paso.

Primero, el callout verifica el lock. Un callout que se programó con `callout_init_mtx(&co, &mtx, 0)` adquiere `mtx` automáticamente antes de llamar al callback, por lo que la aserción es redundante en un driver bien disciplinado, pero es barata y documenta el invariante. Los kernels de depuración detectan cualquier violación.

Segundo, el callout comprueba el indicador `running`. Si la simulación está siendo desmontada (ruta de detach), el callout sale inmediatamente sin rearmarse. La ruta de detach a continuación drena el callout con `callout_drain`, que espera a que termine cualquier callback en vuelo.

Tercero, el callback lee `SENSOR_CONFIG` para conocer el intervalo y la amplitud. Leer la configuración cada vez significa que el usuario puede cambiarla a través de un sysctl y el siguiente callback usará el nuevo valor, sin ningún mecanismo de señalización más allá de la escritura en el registro.

Cuarto, el callback calcula un valor oscilante. El truco `phase < 4 ? phase : (7 - phase)` produce una onda triangular que recorre 0, 1, 2, 3, 4, 3, 2, 1, 0, 1, ... en llamadas sucesivas. Multiplicado por `amplitude / 4`, oscila entre `baseline` y `baseline + amplitude`. La fórmula es simple a propósito; el objetivo es producir un cambio visible, no modelar un sensor real.

Quinto, el callback escribe el nuevo valor en `SENSOR`. Esa escritura es lo que el driver o un observador en espacio de usuario verá.

Sexto, el callback se rearma a sí mismo. `callout_reset_sbt` toma un tiempo binario con signo, por lo que `interval_ms * SBT_1MS` convierte los milisegundos en las unidades correctas. El argumento `pr` (precisión) es cero, lo que significa que el kernel puede diferir el callback hasta un 100% del intervalo para agruparlo con otros temporizadores; un valor menor forzaría una temporización más estricta. El argumento `flags` es cero, lo que significa que no hay indicadores especiales como `C_DIRECT_EXEC` o `C_HARDCLOCK`; el callback se ejecuta en un thread de callout normal con el mutex registrado adquirido.

Si `interval_ms` fuera cero, el código usa por defecto 100 ms. Un intervalo de cero rearmaría inmediatamente sin demora, lo que haría girar al kernel en un bucle cerrado; la comprobación es defensiva.

### Inicialización del callout del sensor

En `myfirst_sim_attach`:

```c
int
myfirst_sim_attach(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim;

        sim = malloc(sizeof(*sim), M_MYFIRST, M_WAITOK | M_ZERO);

        /* Initialise the callouts with the main mutex. */
        callout_init_mtx(&sim->sensor_callout, &sc->mtx, 0);
        callout_init_mtx(&sim->command_callout, &sc->mtx, 0);

        /* Pick a baseline sensor value; 0x1000 is arbitrary but visible. */
        sim->sensor_baseline = 0x1000;

        /* Default config: 100 ms interval, amplitude 64. */
        MYFIRST_LOCK(sc);
        CSR_WRITE_4(sc, MYFIRST_REG_SENSOR_CONFIG,
            (100 << MYFIRST_SCFG_INTERVAL_SHIFT) |
            (64 << MYFIRST_SCFG_AMPLITUDE_SHIFT));
        CSR_WRITE_4(sc, MYFIRST_REG_DELAY_MS, 500);
        CSR_WRITE_4(sc, MYFIRST_REG_FAULT_MASK, 0);
        CSR_WRITE_4(sc, MYFIRST_REG_FAULT_PROB, 0);
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, 0);
        MYFIRST_UNLOCK(sc);

        sc->sim = sim;
        return (0);
}
```

La función asigna la estructura de simulación, inicializa sus callouts con el mutex del driver (para que los callbacks se ejecuten con `sc->mtx` adquirido automáticamente), elige un valor de referencia para el sensor, escribe la configuración por defecto en los registros de simulación bajo el lock y almacena el puntero en el softc.

Los callouts aún no se han iniciado. La simulación está lista pero inactiva. Una función separada, `myfirst_sim_enable`, inicia los callouts; un usuario puede activar y desactivar la simulación a través de un sysctl. Esta separación es útil para la depuración y para las pruebas que necesitan un estado inicial silencioso y conocido.

### Activación y desactivación de la simulación

```c
void
myfirst_sim_enable(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t config, interval_ms;

        MYFIRST_LOCK_ASSERT(sc);

        if (sim->running)
                return;

        sim->running = true;

        config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
        interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
            MYFIRST_SCFG_INTERVAL_SHIFT;
        if (interval_ms == 0)
                interval_ms = 100;

        callout_reset_sbt(&sim->sensor_callout,
            interval_ms * SBT_1MS, 0,
            myfirst_sim_sensor_cb, sc, 0);
}

void
myfirst_sim_disable(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        sim->running = false;

        callout_stop(&sim->sensor_callout);
        callout_stop(&sim->command_callout);
}
```

`myfirst_sim_enable` establece `running` a true y programa el primer callout del sensor. `myfirst_sim_disable` limpia el indicador y detiene ambos callouts. `callout_stop` no espera a que termine ningún callback en vuelo; simplemente cancela cualquier reprogramación pendiente. El callback en vuelo detectará `running == false` en su próxima invocación y saldrá.

Ten en cuenta que ni `callout_stop` ni `callout_reset_sbt` liberan el mutex. Ambas son seguras de llamar con el mutex retenido. Esta es la razón por la que se utilizó `callout_init_mtx`: el mutex se propaga a través del subsistema de callout de modo que los callbacks siempre se ejecutan con él retenido, y las llamadas de control nunca compiten por el lock.

Ten en cuenta también que `myfirst_sim_disable` no drena los callouts. El drenado debe ocurrir en detach, no en disable. La razón es que `callout_drain` duerme, y dormir con un mutex retenido es ilegal. El drenado ocurre en la ruta de detach, una vez que el mutex ha sido liberado.

### Integración de la simulación en attach y detach

En `myfirst_attach` (el archivo principal `myfirst.c`), añade la llamada de attach de la simulación después del attach de hardware:

```c
int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        int error;

        sc = device_get_softc(dev);

        /* ... existing Chapter 11-15 init ... */

        /* Chapter 16: hardware layer. */
        error = myfirst_hw_attach(sc);
        if (error != 0)
                goto fail_hw;

        /* Chapter 17: simulation backend. */
        error = myfirst_sim_attach(sc);
        if (error != 0)
                goto fail_sim;

        /* ... existing sysctl and cdev setup ... */

        myfirst_hw_add_sysctls(sc);
        myfirst_sim_add_sysctls(sc);

        /* Enable the simulation by default. */
        MYFIRST_LOCK(sc);
        myfirst_sim_enable(sc);
        MYFIRST_UNLOCK(sc);

        return (0);

fail_sim:
        myfirst_hw_detach(sc);
fail_hw:
        /* ... existing unwind ... */
        return (error);
}
```

La simulación se adjunta después de la capa de hardware y antes del registro del sysctl. Se activa al final del attach, una vez que todo lo demás está listo. Si algún paso falla, el camino de deshacimiento recorre los pasos en orden inverso: deshabilita lo que se habilitó, desadjunta lo que se adjuntó, libera lo que se asignó.

En `myfirst_detach`, el detach de la simulación ocurre al principio, antes del detach de hardware:

```c
int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* ... existing cdev destroy, wait for outstanding fds ... */

        /* Chapter 17: stop the simulation, drain its callouts. */
        MYFIRST_LOCK(sc);
        myfirst_sim_disable(sc);
        MYFIRST_UNLOCK(sc);
        myfirst_sim_detach(sc);

        /* Chapter 16: detach the hardware layer. */
        myfirst_hw_detach(sc);

        /* ... existing synchronization primitive teardown ... */

        return (0);
}
```

Fíjate en el orden. El deshabilitar ocurre con el lock tomado; el detach ocurre después de soltar el lock. Es en el detach donde tiene lugar el drenaje:

```c
void
myfirst_sim_detach(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim;

        if (sc->sim == NULL)
                return;

        sim = sc->sim;
        sc->sim = NULL;

        callout_drain(&sim->sensor_callout);
        callout_drain(&sim->command_callout);

        free(sim, M_MYFIRST);
}
```

`callout_drain` espera a que termine cualquier callback en vuelo. Si el callback se está ejecutando en otra CPU cuando se llama a `callout_drain`, este bloquea hasta que el callback finaliza y retorna. Una vez que `callout_drain` retorna, ningún callback volverá a ejecutarse jamás para ese callout, por lo que liberar el estado de la simulación es seguro.

El patrón es el mismo que el capítulo 13 enseñó para los callouts en general. El único detalle es que el mutex debe liberarse antes de llamar a `callout_drain`; si el callback está esperando adquirir el mutex cuando llamamos a drain, y nosotros estamos sosteniendo el mutex, el resultado es un deadlock. El orden de soltar el lock antes de drenar es fundamental.

### Primera prueba: las actualizaciones del sensor son visibles

Compila y carga el driver. Luego:

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4096
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4128
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4144
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4160
```

El valor cambia entre lecturas. El valor base es `0x1000 = 4096`, y la oscilación sube `amplitude / 4 = 16` por tick, de modo que los valores son 4096, 4112, 4128, 4144, 4160, 4144, 4128, 4112, 4096, ... Cuatro valores hacia arriba, cuatro hacia abajo, vuelta al valor base.

Si lees con suficiente rapidez, puedes ver el mismo valor dos veces (el callout se dispara cada 100 ms; un sysctl que tarde menos de 100 ms en procesarse puede ver el mismo estado). Si ralentizas y lees aproximadamente una vez por segundo, verás la oscilación completa.

Prueba a cambiar la configuración:

```text
# sysctl dev.myfirst.0.reg_sensor_config=0x01000040
```

Esto establece el intervalo en `0x0100 = 256` ms y la amplitud en `0x0040 = 64`. El sensor ahora se actualiza cada 256 ms. Puedes ver el cambio en el registro de acceso:

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sleep 2
# sysctl dev.myfirst.0.access_log_enabled=0
# sysctl dev.myfirst.0.access_log
```

El log debería mostrar aproximadamente 8 escrituras en `SENSOR` durante la ventana de 2 segundos (2000 ms / 256 ms ~= 7,8). Antes del cambio de configuración, la tasa habría sido de 20 escrituras en 2 segundos.

Esto es lo que produce la simulación. El driver no hace nada durante este tiempo; la simulación se ejecuta por sí sola, impulsada por el callout del sensor.

### Un segundo callout: la finalización de comandos

El callout del sensor es periódico. El callout de comandos es de un solo disparo: se activa una vez, cuando un comando completa, y no hace nada hasta que se emite el siguiente comando.

```c
static void
myfirst_sim_command_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t status;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running || !sim->command_pending)
                return;

        /* Complete the command: copy pending data to DATA_OUT,
         * set STATUS.DATA_AV, clear STATUS.BUSY, increment OP_COUNTER. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);

        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~MYFIRST_STATUS_BUSY;
        status |= MYFIRST_STATUS_DATA_AV;
        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

        sim->op_counter++;
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

        sim->command_pending = false;
}
```

El callback hace cuatro cosas. Copia los datos pendientes en `DATA_OUT` (el "resultado" de la simulación). Actualiza `STATUS`: `BUSY` se limpia, `DATA_AV` se activa. Incrementa `OP_COUNTER`. Y limpia el indicador `command_pending`.

El comando lo inicia `myfirst_sim_start_command`, llamada desde el driver cuando escribe `CTRL.GO`:

```c
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t data_in, delay_ms;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        if (sim->command_pending) {
                /* Command already in flight. Real devices might reject
                 * this, set an error bit, or queue the command. For the
                 * simulation, the simplest behaviour is to treat it as
                 * a no-op. The driver should not do this. */
                device_printf(sc->dev,
                    "sim: overlapping command; ignored\n");
                return;
        }

        /* Snapshot DATA_IN now; the callout fires later. */
        data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
        sim->pending_data = data_in;

        /* Set STATUS.BUSY, clear STATUS.DATA_AV. */
        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                status |= MYFIRST_STATUS_BUSY;
                status &= ~MYFIRST_STATUS_DATA_AV;
                CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
        }

        /* Read the configured delay. */
        delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
        if (delay_ms == 0) {
                /* Zero delay: complete on the next tick. */
                delay_ms = 1;
        }

        sim->command_pending = true;

        callout_reset_sbt(&sim->command_callout,
            delay_ms * SBT_1MS, 0,
            myfirst_sim_command_cb, sc, 0);
}
```

La función toma una instantánea de `DATA_IN` en el estado de la simulación, activa `STATUS.BUSY`, limpia `STATUS.DATA_AV`, lee el `DELAY_MS` configurado y programa el callout del comando. El callout del comando se dispara `delay_ms` milisegundos después y finaliza el comando.

Un punto sutil: ¿por qué tomar una instantánea de `DATA_IN` en lugar de leerlo en el callout del comando? Porque el driver podría volver a escribir en `DATA_IN` antes de que el callout se dispare. La simulación debe usar el valor que estaba en `DATA_IN` cuando se escribió `CTRL.GO`, no el que casualmente esté ahí cuando el callout se ejecute. Tomar la instantánea en el momento de inicio es el comportamiento correcto para la mayoría de los dispositivos reales también: el dispositivo lee el registro de comando cuando se emite la orden y no lo vuelve a leer durante la ejecución.

### Interceptando `CTRL.GO`

La función `myfirst_sim_start_command` se llama cuando el driver escribe `CTRL.GO`. La interceptación de escritura reside en el helper `myfirst_ctrl_update` ya existente (presentado en el capítulo 16):

```c
void
myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old, uint32_t new)
{
        /* Chapter 16 behaviour: log ENABLE transitions. */
        if ((old & MYFIRST_CTRL_ENABLE) != (new & MYFIRST_CTRL_ENABLE)) {
                device_printf(sc->dev, "CTRL.ENABLE now %s\n",
                    (new & MYFIRST_CTRL_ENABLE) ? "on" : "off");
        }

        /* Chapter 17: if GO was set, start a command in the simulation. */
        if (!(old & MYFIRST_CTRL_GO) && (new & MYFIRST_CTRL_GO)) {
                myfirst_sim_start_command(sc);
                /* Clear the GO bit; it is a one-shot trigger. */
                CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_GO, 0);
        }
}
```

La extensión detecta cuando el bit `GO` transiciona de 0 a 1. Cuando eso ocurre, llama a `myfirst_sim_start_command` y limpia inmediatamente el bit `GO`, porque `GO` es un disparador de un solo uso: se afirma, el comando arranca y el bit vuelve a cero. Este es un patrón habitual para los bits de "inicio" en hardware real; el hardware limpia el bit automáticamente una vez que el comando ha sido aceptado.

Este comportamiento de auto-limpieza significa que el driver no tiene que acordarse de limpiar el bit él mismo. Escribe `CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO)` para iniciar un comando, y la simulación se encarga del resto.

### Segunda prueba: el camino del comando

Con el código del camino del comando en su lugar, la simulación ahora soporta un ciclo de comandos. Pruébalo:

```text
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1

# sysctl dev.myfirst.0.reg_ctrl_set_datain=0x12345678   # (new sysctl for DATA_IN)
# sysctl dev.myfirst.0.reg_ctrl_set=0x200                # bit 9 = CTRL.GO

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3    # BUSY | READY

# sleep 1
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 9    # READY | DATA_AV

# sysctl dev.myfirst.0.reg_data_out
dev.myfirst.0.reg_data_out: 305419896   # 0x12345678

# sysctl dev.myfirst.0.reg_op_counter
dev.myfirst.0.reg_op_counter: 1
```

Leer el estado justo después de `CTRL.GO` muestra `BUSY|READY`. Esperar el retraso de 500 ms y volver a leer muestra `READY|DATA_AV`. Leer `DATA_OUT` devuelve el valor que estaba en `DATA_IN` cuando se escribió `CTRL.GO`. `OP_COUNTER` se ha incrementado en uno.

En el ejemplo se insinúa un nuevo sysctl `reg_ctrl_set_datain`; es una adición trivial que duplica `reg_ctrl_set` pero escribe en `DATA_IN` en lugar de en `CTRL`. El patrón es idéntico al `reg_ctrl_set` del capítulo 16: un `SYSCTL_ADD_PROC` con un handler de escritura. Añade un sysctl por cada registro que el usuario pueda querer manipular directamente.

### Protección contra el solapamiento de comandos

La comprobación de `command_pending` en `myfirst_sim_start_command` previene el solapamiento de comandos. Si el driver escribe `CTRL.GO` mientras ya hay un comando en vuelo, la simulación ignora el segundo comando y registra una advertencia. Esto no es realista para todos los dispositivos reales (algunos encolan comandos, otros los rechazan con un error), pero es el comportamiento correcto más sencillo para una simulación de enseñanza.

El driver, por su parte, no debería emitir un comando mientras uno está en vuelo. Puede consultar `STATUS.BUSY` para ver si el dispositivo está listo para aceptar un nuevo comando y esperar a que `BUSY` se limpie antes de escribir `CTRL.GO`. La sección 5 enseña este patrón en el driver.

### Sysctls para los controles de la simulación

Los registros de la simulación ya están expuestos a través de los sysctls de registro del capítulo 16 (`reg_sensor`, `reg_sensor_config`, `reg_delay_ms`, `reg_fault_mask`, `reg_fault_prob`, `reg_op_counter`). El capítulo 17 añade unos pocos sysctls de nivel superior que no están mapeados a registros:

```c
void
myfirst_sim_add_sysctls(struct myfirst_softc *sc)
{
        struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
        struct sysctl_oid *tree = sc->sysctl_tree;

        SYSCTL_ADD_BOOL(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_running", CTLFLAG_RD,
            &sc->sim->running, 0,
            "Whether the simulation callouts are active");

        SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_sensor_baseline", CTLFLAG_RW,
            &sc->sim->sensor_baseline, 0,
            "Baseline value around which SENSOR oscillates");

        SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_op_counter_mirror", CTLFLAG_RD,
            &sc->sim->op_counter, 0,
            "Mirror of the OP_COUNTER value (for observability)");

        /* Add writeable sysctls for the register-mapped fields. */
        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "reg_delay_ms_set",
            CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
            sc, MYFIRST_REG_DELAY_MS, myfirst_sysctl_reg_write,
            "IU", "Command delay in milliseconds (writeable)");

        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "reg_sensor_config_set",
            CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
            sc, MYFIRST_REG_SENSOR_CONFIG, myfirst_sysctl_reg_write,
            "IU", "Sensor config: interval|amplitude (writeable)");
}
```

Los sysctls de nivel superior `sim_running`, `sim_sensor_baseline` y `sim_op_counter_mirror` exponen estado interno que no está mapeado a registros. Los demás añaden sysctls de escritura para los registros del capítulo 17, usando el handler `myfirst_sysctl_reg_write` ya existente del capítulo 16.

La adición mantiene la vista del usuario coherente: toda pieza de estado interesante es visible a través de `sysctl dev.myfirst.0`, y los sysctls de escritura siguen el patrón de sufijo `_set` que el driver ya usa.

### Lo que logró la etapa 1

Al final de la sección 3, el driver tiene:

- Un nuevo archivo `myfirst_sim.c` con el backend de simulación.
- Un nuevo header `myfirst_sim.h` con la API de simulación.
- Una nueva estructura de estado de simulación a la que apunta `sc->sim`.
- Dos callouts: uno para actualizaciones periódicas del sensor, otro para la finalización de comandos de un solo disparo.
- Una extensión de `myfirst_ctrl_update` que intercepta `CTRL.GO`.
- Sysctls de escritura para los nuevos registros que deben ser configurables.

La etiqueta de versión pasa a ser `1.0-sim-stage1`. El driver sigue haciendo todo lo que enseñó el capítulo 16; ahora además tiene dos callouts que producen comportamiento autónomo en los registros.

Compila, carga y prueba:

```text
# cd examples/part-04/ch17-simulating-hardware/stage1-backend
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.sim_running
# sysctl dev.myfirst.0.reg_sensor
# sleep 1
# sysctl dev.myfirst.0.reg_sensor
# sysctl dev.myfirst.0.reg_op_counter
# kldunload myfirst
```

Deberías ver que `sim_running` es 1, el valor del sensor cambia y `op_counter` permanece en cero hasta que emites un comando. Si `kldunload` termina limpiamente, el camino de detach es correcto.

### Lo que la etapa 1 todavía no hace

La etapa 1 tiene la simulación en marcha, pero los caminos de syscall del driver aún no conocen el nuevo comportamiento. Un `write(2)` en `/dev/myfirst0` sigue escribiendo en el ring buffer, no en el dispositivo simulado. Los bytes que van al ring buffer nunca aparecen en `DATA_IN`, nunca causan un comando, nunca aparecen en `DATA_OUT`. La simulación está lista; el driver todavía no la usa de verdad.

La sección 4 da el siguiente paso: conecta la simulación al camino de datos del driver, de modo que una escritura desde el espacio de usuario provoca un comando simulado y el resultado del comando es lo que la lectura desde el espacio de usuario recibe. Hasta entonces, la simulación es un complemento secundario.

### Cerrando la sección 3

El comportamiento de un dispositivo simulado proviene de un pequeño conjunto de primitivas. Un callout para actualizaciones periódicas, un callout para eventos temporizados de un solo disparo y una estructura de estado compartida que ambos callouts consultan. La simulación del capítulo 17 usa tres de estos: un callout del sensor que se ejecuta siempre, un callout de comandos que se ejecuta por cada comando y un indicador interno `command_pending` que previene el solapamiento de comandos.

La simulación vive en su propio archivo, `myfirst_sim.c`, que depende de `myfirst_hw.h` del capítulo 16 para las definiciones de registros. La separación mantiene el código de simulación aislado; un capítulo posterior que reemplace la simulación por código real orientado a PCI puede borrar `myfirst_sim.c` y sustituirlo por `myfirst_pci.c` sin tocar el driver.

La sección 4 comienza la integración real: el camino de datos del driver empieza a enviar y recibir a través de la simulación, y la simulación empieza a parecerse a un dispositivo real desde el lado del driver.



## Sección 4: Exponiendo el dispositivo simulado al driver

La sección 3 construyó un backend de simulación que se ejecuta sobre callouts y escribe en registros a través de los accesores del capítulo 16. La sección 4 plantea una pregunta diferente: ¿cómo ve el driver la simulación? Desde el lado del driver, ¿cuál es el camino de acceso? ¿Cómo es el código y en qué se diferencia del código que hablaría con hardware real?

La respuesta corta es que el camino de acceso no cambia. El objetivo completo de la abstracción `bus_space(9)` del capítulo 16 es que el driver no sabe, ni le importa, si el bloque de registros es real o simulado. La sección 4 es deliberadamente más corta porque el trabajo pesado se hizo en el capítulo 16. Lo que queda es examinar con cuidado cómo la abstracción sobrevive a la adición de comportamiento dinámico, y los pocos pequeños ganchos que el driver necesita para dejar que la simulación dirija su propio comportamiento.

### El tag y el handle, igual que siempre

El capítulo 16 configuró el `bus_space_tag_t` y el `bus_space_handle_t` de la simulación en `myfirst_hw_attach`:

```c
#if defined(__amd64__) || defined(__i386__)
        hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation supports x86 only"
#endif
        hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;
```

Ese código no cambia para el capítulo 17. La simulación escribe registros a través de `CSR_WRITE_4`, que se expande a `myfirst_reg_write`, que se expande a `bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value)`. El mismo tag; el mismo handle; el mismo accesor. Los callouts que introdujo la sección 3 usan exactamente la misma API que el camino de syscall.

Esto no es casualidad. La razón por la que existe `bus_space` es precisamente para ocultar la diferencia entre el acceso a registros real y el simulado. Un driver que usa `bus_space` correctamente puede apuntarse a un BAR PCI real o a una asignación de `malloc(9)` sin ningún cambio en el código de acceso. La simulación ejercita esta propiedad.

Un ejercicio útil en este punto: abre `/usr/src/sys/dev/ale/if_ale.c` en una ventana y `myfirst_hw.c` en otra. Compara el código de acceso a registros. El driver ALE usa `CSR_READ_4(sc, ALE_REG_XYZ)`; tu driver usa `CSR_READ_4(sc, MYFIRST_REG_XYZ)`. La expansión es idéntica. La única diferencia es el tag y el handle: ALE los obtiene de `rman_get_bustag(sc->ale_res[0])` y `rman_get_bushandle(sc->ale_res[0])`; tu driver los obtiene de la configuración de simulación del capítulo 16. Reemplazar la simulación por hardware real, que es lo que hará el capítulo 18, es un cambio de una sola función en `myfirst_hw_attach`.

### Por qué la abstracción importa más ahora

En el Capítulo 16, el argumento a favor de usar `bus_space` era prospectivo: «cuando finalmente pases a hardware real, el código del driver no tendrá que cambiar». En el Capítulo 17, el argumento se vuelve concreto: el bloque de registros cambia ahora por sí solo, y el driver no tiene forma de saber si un cambio provino del callout de simulación o de la máquina de estados interna de un dispositivo real. La abstracción está haciendo un trabajo real.

Considera un ejemplo pequeño. Una función del driver que hace polling de `STATUS.DATA_AV`:

```c
static int
myfirst_wait_for_data(struct myfirst_softc *sc, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (status & MYFIRST_STATUS_DATA_AV)
                        return (0);

                /* Release the lock, sleep briefly, reacquire. */
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfwait", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Esta función lee `STATUS`, comprueba un bit y, o bien retorna, o bien duerme un milisegundo y reintenta. No sabe si el bit está siendo activado por el callout de comandos de la simulación del Capítulo 17, por la máquina de estados interna de un dispositivo real, o por alguna otra cosa. Simplemente hace polling.

La llamada a `pause_sbt` merece un análisis. La función libera el lock del driver antes de dormir y lo vuelve a adquirir después. Dormir con un lock adquirido bloquearía a cualquier otro contexto que lo necesite, incluidos los callouts de simulación; soltar el lock antes de dormir permite que la simulación siga ejecutándose. `pause_sbt` recibe un identificador de sleep ("mfwait"), un tiempo binario con signo (1 ms en este caso), una precisión y flags. Una precisión de cero significa que el kernel puede agrupar este sleep con otros, reduciendo las interrupciones de timer.

La Sección 6 retoma con más profundidad la elección entre `DELAY(9)`, `pause_sbt(9)` y `callout_reset_sbt(9)`. La versión corta: hacer polling de un registro mil veces por segundo durmiendo entre cada poll no es el patrón más eficiente, pero es correcto, simple y legible. El driver del Capítulo 17 lo usa porque el protocolo es pequeño y la sobrecarga del polling no importa. Los drivers de alto rendimiento reales usan interrupciones (Capítulo 19) en su lugar.

### La integración de la ruta de comandos

La ruta de datos actual del driver (`myfirst_write`, del Capítulo 10) escribe bytes en el buffer en anillo. El Capítulo 17 pretende enrutar esos bytes a través de la simulación: el usuario escribe un byte, el driver lo escribe en `DATA_IN`, activa `CTRL.GO`, espera a `STATUS.DATA_AV`, lee `DATA_OUT` y escribe el resultado en el buffer en anillo. Este es el ciclo de comandos completo.

Para la Etapa 2 del driver del Capítulo 17, añadimos la ruta de código del ciclo de comandos. La función de escritura queda así:

```c
static int
myfirst_write_cmd(struct myfirst_softc *sc, uint8_t byte)
{
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        /* Wait for the device to be ready for a new command. */
        error = myfirst_wait_for_ready(sc, 100);
        if (error != 0)
                return (error);

        /* Write the byte to DATA_IN. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)byte);

        /* Issue the command. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        /* Wait for the command to complete (STATUS.DATA_AV). */
        error = myfirst_wait_for_data(sc, 2000);
        if (error != 0)
                return (error);

        /* Check for errors. */
        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (status & MYFIRST_STATUS_ERROR) {
                        /* Clear the error latch. */
                        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                            MYFIRST_STATUS_ERROR, 0);
                        return (EIO);
                }
        }

        /* Read the result. DATA_OUT will hold the byte the simulation
         * echoed (or, if loopback is active, the same byte we wrote). */
        (void)CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);

        /* Clear DATA_AV so the next command can set it. */
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);

        return (0);
}
```

Seis pasos. Esperar a que esté listo, escribir `DATA_IN`, activar `CTRL.GO`, esperar a `DATA_AV`, comprobar errores, leer `DATA_OUT`. Limpiar `DATA_AV` para dejar la simulación en buen estado de cara a la siguiente iteración.

La función auxiliar `myfirst_wait_for_ready` sondea `STATUS.BUSY`:

```c
static int
myfirst_wait_for_ready(struct myfirst_softc *sc, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (!(status & MYFIRST_STATUS_BUSY))
                        return (0);
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfrdy", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Tiene la misma estructura que `myfirst_wait_for_data`; solo difiere el bit que se sondea. Ambas funciones auxiliares son candidatas razonables a una implementación compartida:

```c
static int
myfirst_wait_for_bit(struct myfirst_softc *sc, uint32_t mask,
    uint32_t target, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if ((status & mask) == target)
                        return (0);
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfwait", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

De este modo, `wait_for_ready` llama a `wait_for_bit(sc, MYFIRST_STATUS_BUSY, 0, timeout_ms)` y `wait_for_data` llama a `wait_for_bit(sc, MYFIRST_STATUS_DATA_AV, MYFIRST_STATUS_DATA_AV, timeout_ms)`. Vale la pena extraer la abstracción en cuanto tienes dos usuarios; la Sección 6 retoma este patrón.

### Dónde se integra el comando con el syscall de escritura

El `myfirst_write` del Capítulo 10 introduce bytes en el buffer en anillo. El Capítulo 17 no sustituye esa ruta; la amplía. Para la Etapa 2, añadimos un gancho del ciclo de comandos por byte que envía cada byte a través de la simulación, además de introducirlo en el buffer en anillo.

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t buf[64];
        size_t n;
        int error;

        /* ... existing validation ... */

        while (uio->uio_resid > 0) {
                n = MIN(uio->uio_resid, sizeof(buf));
                error = uiomove(buf, n, uio);
                if (error != 0)
                        return (error);

                MYFIRST_LOCK(sc);
                for (size_t i = 0; i < n; i++) {
                        /* Send each byte through the simulated device. */
                        error = myfirst_write_cmd(sc, buf[i]);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }

                        /* Existing Chapter 10 path: push into ring. */
                        error = cbuf_put(sc->cbuf, buf[i]);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }
                }
                MYFIRST_UNLOCK(sc);
        }

        return (0);
}
```

El bucle procesa un byte cada vez, por claridad. Cada byte pasa por el ciclo de comandos y después va al buffer en anillo. Un driver real normalmente usaría pipelining para esto (escribir varios bytes en un FIFO, emitir un único comando, leer varios bytes de vuelta), pero el objetivo didáctico del Capítulo 17 son los comandos individuales; el pipelining es un refinamiento para más adelante.

Fíjate en el locking. El lock externo cubre el lote completo (hasta 64 bytes). Cada llamada a `myfirst_write_cmd` libera y vuelve a adquirir el lock internamente cuando duerme. Otros contextos que necesitan el lock (en particular, los callouts de la simulación) tienen oportunidad de ejecutarse entre comandos.

### Prueba de la Etapa 2

Compila, carga y prueba:

```text
# kldload ./myfirst.ko
# echo -n "hi" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_op_counter
dev.myfirst.0.reg_op_counter: 2

# dd if=/dev/myfirst0 bs=1 count=2 2>/dev/null
hi
```

Se escriben dos bytes; `OP_COUNTER` se incrementa en dos. Las lecturas devuelven esos mismos dos bytes porque la simulación reenvía `DATA_IN` a `DATA_OUT`. La prueba valida el flujo de extremo a extremo: el espacio de usuario escribe, el driver emite comandos, la simulación los procesa, el driver lee, el espacio de usuario recibe.

El ciclo completo tarda alrededor de 1 segundo (500 ms por byte, dos bytes). Es lento, pero es realista: un dispositivo real con una latencia similar (por ejemplo, un sensor conectado por SPI que tarda 500 ms en producir una lectura) se comportaría exactamente igual. Para acelerarlo, hay que reducir `DELAY_MS` (prueba `sysctl dev.myfirst.0.reg_delay_ms_set=10` para 10 ms por comando).

### Lo que no cambia

Vale la pena enumerar algunas propiedades del driver del Capítulo 16 que no cambian en la Etapa 2.

El registro de accesos sigue funcionando. Cada lectura y escritura de registro que realiza el driver aparece en el registro. Las propias escrituras de registro de la simulación también aparecen (porque pasan por los mismos accesores). El registro es denso, lo cual es bueno: puedes ver el latido de la simulación junto con la actividad del driver.

La interfaz sysctl sigue funcionando. Todos los registros son legibles; todos los registros escribibles siguen siendo escribibles. Las comodidades del Capítulo 16 (leer todos los registros, escribir registros específicos) no han cambiado.

La disciplina de locking sigue funcionando. Cada acceso a registro ocurre con `sc->mtx` adquirido; los callouts de la simulación fueron diseñados para ejecutarse con `sc->mtx` tomado (mediante `callout_init_mtx`), de modo que no hay inversiones de orden de lock de las que preocuparse.

La ruta de detach sigue funcionando. El detach de la simulación se añadió al orden de detach existente, y el drenado de los callouts de la simulación ocurre antes de que se libere la región de hardware.

### Lo que aún necesita atención

Hay algunos cabos sueltos que las Secciones 5 y 6 terminarán de cerrar.

Los timeouts en `myfirst_wait_for_ready` y `myfirst_wait_for_data` están codificados de forma fija (100 ms y 2000 ms respectivamente). Un timeout configurable (quizás expuesto a través de un sysctl) sería mejor; la Sección 5 lo añade.

El sleep de 1 ms de `pause_sbt` es un valor predeterminado razonable, pero no es óptimo para todas las situaciones. En una simulación rápida (por ejemplo, `DELAY_MS=0`), sondear cada 1 ms es un desperdicio; en una simulación lenta (por ejemplo, `DELAY_MS=5000`), sondear cada 1 ms es excesivo. La Sección 6 analiza mejores estrategias.

La ruta de escritura del ciclo de comandos no interactúa con la inyección de fallos de la simulación. Si la simulación está configurada para hacer fallar un comando (trabajo de la Sección 7), el driver necesita reconocer el fallo y recuperarse. La Sección 5 añade la recuperación de errores básica; la Sección 7 añade la infraestructura de inyección de fallos.

La ruta de lectura del driver todavía no está integrada. Las lecturas actualmente extraen del buffer en anillo, que contiene lo que las escrituras anteriores introdujeron en él. Un patrón más realista haría que las lecturas también provocaran comandos (un comando de "dame otro byte"), pero eso es un tema de la Sección 5.

### Cerrando la Sección 4

Exponer el dispositivo simulado al driver no es un mecanismo nuevo en el Capítulo 17. La abstracción `bus_space(9)` del Capítulo 16 ya lo gestiona; las escrituras de la simulación y las lecturas del driver pasan por el mismo tag y handle, y el driver no sabe ni le importa que el bloque de registros esté simulado. Lo que añade la Etapa 2 es la ruta de código del ciclo de comandos: el driver escribe `DATA_IN`, activa `CTRL.GO`, espera a `DATA_AV`, lee `DATA_OUT`. Dos funciones auxiliares (`myfirst_wait_for_ready` y `myfirst_wait_for_data`) hacen explícito el sondeo; una función paraguas (`myfirst_wait_for_bit`) factoriza la estructura compartida. El driver ejercita ahora la simulación con un patrón realista de comando y respuesta.

La Sección 5 profundiza en la historia de las pruebas. Añade recuperación de errores, timeouts configurables, una separación más clara entre las rutas de lectura y escritura, y las primeras pruebas de carga que someten a la simulación a suficiente tráfico como para exponer condiciones de carrera.

## Sección 5: Cómo probar el comportamiento del dispositivo desde el driver

El código del ciclo de comandos de la Sección 4 funciona para un único byte. Expira correctamente cuando la simulación es lenta; devuelve un valor de datos cuando la simulación responde; lee `STATUS` en los puntos apropiados. Lo que aún no ha experimentado es volumen, diversidad ni concurrencia. Un driver que supera una prueba de un solo byte puede fallar aún bajo carga, con escritores concurrentes o cuando la simulación está configurada con parámetros inusuales.

La Sección 5 trata de someter a pruebas de estrés la interacción del driver con el dispositivo simulado. Añade timeouts configurables, una ruta de recuperación de errores más robusta, una integración de la ruta de lectura que permite a la simulación dirigir tanto las lecturas como las escrituras, y un conjunto de pruebas de carga que ejercitan el driver en toda su superficie de comportamiento. Al final de la Sección 5, el driver está en la Etapa 2 del Capítulo 17 y ha sobrevivido a una ejecución de estrés significativa.

### Timeouts configurables

Los timeouts codificados de forma fija son frágiles. 100 ms es suficiente para el `DELAY_MS` predeterminado de 500, pero ¿qué ocurre si el usuario quiere probar una configuración lenta? Subir `DELAY_MS` a 2000 significaría que cada comando expiraría con el límite de 100 ms, lo que constituye un modo de fallo espurio. Hay que hacer los timeouts configurables.

Añade dos campos al softc (o a una subestructura adecuada):

```c
struct myfirst_softc {
        /* ... existing fields ... */
        int      cmd_timeout_ms;        /* max wait for command completion  */
        int      rdy_timeout_ms;        /* max wait for device ready         */
};
```

Inicialízalos en attach:

```c
sc->cmd_timeout_ms = 2000;              /* 2 s default                      */
sc->rdy_timeout_ms = 100;               /* 100 ms default                   */
```

Expónlos a través de sysctl:

```c
SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "cmd_timeout_ms", CTLFLAG_RW, &sc->cmd_timeout_ms, 0,
    "Command completion timeout in milliseconds");

SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "rdy_timeout_ms", CTLFLAG_RW, &sc->rdy_timeout_ms, 0,
    "Device-ready polling timeout in milliseconds");
```

Y cambia `myfirst_write_cmd` para que los use:

```c
error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
/* ... */
error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
```

El conjunto de pruebas puede ahora ajustar los timeouts para que coincidan con la latencia esperada de la prueba. Una prueba de simulación rápida los establece bajos; una prueba de simulación lenta los establece altos. Una prueba patológica los establece muy bajos y espera errores de timeout.

### Integración de la ruta de lectura

El `myfirst_read` del Capítulo 10 extrae del buffer en anillo. El buffer en anillo contiene lo que las escrituras anteriores introdujeron en él. El Capítulo 17 puede extender este patrón: cuando el buffer en anillo está vacío y el lector está dispuesto a esperar, emitir un comando de "genera un byte" al dispositivo simulado y empujar el resultado al buffer en anillo.

No todos los drivers funcionan así. Un dispositivo real tiene una semántica de producción de datos específica del dispositivo; una tarjeta de red produce bytes a medida que llegan los paquetes, no bajo demanda. Para un dispositivo simulado similar a un sensor, un comando de "muestra" que produce una lectura por invocación es un patrón natural. Nuestro driver lo adopta.

Añade una función `myfirst_sample_cmd` que emite un comando y empuja el resultado al buffer en anillo:

```c
static int
myfirst_sample_cmd(struct myfirst_softc *sc)
{
        uint32_t data_out;
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
        if (error != 0)
                return (error);

        /* DATA_IN does not matter for a sample; write a marker. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, 0xCAFE);

        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
        if (error != 0)
                return (error);

        data_out = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);

        /* Push one byte of the sample into the ring. */
        error = cbuf_put(sc->cbuf, (uint8_t)(data_out & 0xFF));
        return (error);
}
```

La ruta de lectura puede ahora llamar a `myfirst_sample_cmd` cuando el buffer en anillo está vacío. Conéctalo a `myfirst_read`:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t byte;
        int error;

        MYFIRST_LOCK(sc);
        while (uio->uio_resid > 0) {
                while (cbuf_empty(sc->cbuf)) {
                        if (ioflag & O_NONBLOCK) {
                                MYFIRST_UNLOCK(sc);
                                return (EWOULDBLOCK);
                        }

                        /* Chapter 17: trigger a sample when ring is empty. */
                        error = myfirst_sample_cmd(sc);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }
                }
                error = cbuf_get(sc->cbuf, &byte);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
                MYFIRST_UNLOCK(sc);
                error = uiomove(&byte, 1, uio);
                MYFIRST_LOCK(sc);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Ahora una lectura de `/dev/myfirst0` desencadena un comando simulado si el buffer en anillo está vacío, extrayendo efectivamente un byte por comando de la simulación. Combinado con la ruta de escritura de la Sección 4 (de tipo push), el driver ejercita ahora ambas direcciones.

### Una primera prueba de carga

Con ambas rutas integradas, el driver está listo para las pruebas de carga. Un script de prueba sencillo:

```sh
#!/bin/sh
# cmd_load.sh: exercise the command path under load.

set -e

# Fast simulation for load testing.
sysctl dev.myfirst.0.reg_delay_ms_set=10

# Spawn 4 writers and 4 readers in parallel.
for i in 1 2 3 4; do
        (for j in $(seq 1 100); do echo -n "X" > /dev/myfirst0; done) &
done
for i in 1 2 3 4; do
        (dd if=/dev/myfirst0 of=/dev/null bs=1 count=100 2>/dev/null) &
done
wait

# Show the resulting counter.
sysctl dev.myfirst.0.reg_op_counter

# Reset to default.
sysctl dev.myfirst.0.reg_delay_ms_set=500
```

Con `DELAY_MS=10` y 4 escritores enviando cada uno 100 bytes más 4 lectores recibiendo cada uno 100 bytes, la prueba debería completarse en aproximadamente 10 segundos (800 comandos a 10 ms cada uno, serializados a través de la simulación). `OP_COUNTER` debería alcanzar al menos 800.

Hay dos cosas que vale la pena observar sobre esta prueba.

Primero, los comandos están serializados. La simulación rechaza los comandos solapados (la comprobación `command_pending` de la Sección 3). El driver espera a que `STATUS.BUSY` se limpie antes de emitir el siguiente comando. Con múltiples procesos del espacio de usuario compitiendo por el driver, el lock sobre `sc->mtx` los serializa. La tasa efectiva es un comando por cada `DELAY_MS` milisegundos, independientemente de cuántos escritores estén en ejecución.

Segundo, el driver no tuvo que cambiar para dar cabida a la concurrencia. El mutex del Capítulo 11, las tasks del Capítulo 14, los accesores del Capítulo 16 y el código del ciclo de comandos de la Sección 4 se componen de forma natural. La disciplina que construyó la Parte 3 rinde frutos aquí: no se necesita sincronización adicional para añadir hardware simulado a un driver que ya se coordina correctamente.

### Refinamiento de la recuperación de errores

El código del ciclo de comandos de la Sección 4 limpia `STATUS.ERROR` y devuelve `EIO`. La Sección 5 lo refina para distinguir varios casos de error.

```c
static int
myfirst_write_cmd(struct myfirst_softc *sc, uint8_t byte)
{
        uint32_t status;
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
        if (error != 0) {
                sc->stats.cmd_rdy_timeouts++;
                return (error);
        }

        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)byte);
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
        if (error != 0) {
                sc->stats.cmd_data_timeouts++;
                /* Clear any partial state in the simulation. */
                myfirst_recover_from_stuck(sc);
                return (error);
        }

        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        if (status & MYFIRST_STATUS_ERROR) {
                CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                    MYFIRST_STATUS_ERROR, 0);
                CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                    MYFIRST_STATUS_DATA_AV, 0);
                sc->stats.cmd_errors++;
                return (EIO);
        }

        (void)CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);
        sc->stats.cmd_successes++;
        return (0);
}
```

Tres cambios respecto a la Sección 4.

Primero, contadores por tipo de error. `sc->stats` es una estructura de contadores `uint64_t` que el driver actualiza en cada resultado. `cmd_successes`, `cmd_rdy_timeouts`, `cmd_data_timeouts` y `cmd_errors` tienen cada uno su propio contador. Un sysctl los expone. Bajo carga, los contadores se convierten en el diagnóstico principal: "el driver emitió 800 comandos, 5 expiraron, 0 tuvieron errores, 795 tuvieron éxito" es un resumen mucho más útil que "el driver se ejecutó".

Segundo, `myfirst_recover_from_stuck`. Cuando un comando expira, la simulación puede estar todavía en medio del procesamiento. Limpiar el estado obsoleto es importante:

```c
static void
myfirst_recover_from_stuck(struct myfirst_softc *sc)
{
        uint32_t status;

        MYFIRST_LOCK_ASSERT(sc);

        /* Clear any pending command flag in the simulation. The next
         * CTRL.GO will be accepted regardless of any stale state. */
        if (sc->sim != NULL) {
                sc->sim->command_pending = false;
                callout_stop(&sc->sim->command_callout);
        }

        /* Force-clear BUSY and DATA_AV. */
        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~(MYFIRST_STATUS_BUSY | MYFIRST_STATUS_DATA_AV);
        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
}
```

Observa que `myfirst_recover_from_stuck` accede directamente a `sc->sim->command_pending`. Esta es una ruta legítima exclusiva de la simulación: en hardware real no existiría tal variable; en su lugar, el driver emitiría un comando de reset al dispositivo. La función es una ruta de recuperación específica de la simulación, y un comentario en el código debería indicarlo. Cuando el Capítulo 18 sustituya la simulación por hardware real, esta función cambiará por completo.

Tercero, limpieza de estado en caso de error. La ruta de error limpia tanto `ERROR` como `DATA_AV`. Dejar cualquiera de los dos activado haría que el `wait_for_data` del siguiente comando retornase inmediatamente (si `DATA_AV` sigue activado) u observase un error inesperado (si `ERROR` sigue activado). La limpieza defensiva garantiza que la simulación está en un estado limpio para el siguiente comando.

### Infraestructura de estadísticas

Vale la pena definir correctamente la estructura `sc->stats`. Colócala en `myfirst.h`:

```c
struct myfirst_stats {
        uint64_t        cmd_successes;
        uint64_t        cmd_rdy_timeouts;
        uint64_t        cmd_data_timeouts;
        uint64_t        cmd_errors;
        uint64_t        cmd_rejected;
        uint64_t        cmd_recoveries;
        uint64_t        samples_taken;
        uint64_t        fault_injected;
};
```

Añádela al softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_stats stats;
};
```

Expón cada contador a través de sysctl:

```c
static void
myfirst_add_stats_sysctls(struct myfirst_softc *sc)
{
        struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
        struct sysctl_oid *tree = sc->sysctl_tree;

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_successes", CTLFLAG_RD,
            &sc->stats.cmd_successes, 0,
            "Successfully completed commands");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_rdy_timeouts", CTLFLAG_RD,
            &sc->stats.cmd_rdy_timeouts, 0,
            "Commands that timed out waiting for READY");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_data_timeouts", CTLFLAG_RD,
            &sc->stats.cmd_data_timeouts, 0,
            "Commands that timed out waiting for DATA_AV");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_errors", CTLFLAG_RD,
            &sc->stats.cmd_errors, 0,
            "Commands that completed with STATUS.ERROR set");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_recoveries", CTLFLAG_RD,
            &sc->stats.cmd_recoveries, 0,
            "Recovery operations triggered");

        /* ... add the remaining counters ... */
}
```

Bajo carga, `sysctl dev.myfirst.0 | grep -E 'cmd_|samples_|fault_'` ofrece un resumen en una sola pantalla del comportamiento del driver en la ruta de comandos. Tras una ejecución de prueba, hacer un diff del antes y el después es la forma de responder a la pregunta «¿se comportó el driver correctamente?».

### Observación del driver bajo carga

Un patrón de depuración útil: observar las estadísticas mientras se ejecuta una prueba de carga. En un terminal:

```text
# sysctl dev.myfirst.0 | grep cmd_ > before.txt
```

En otro terminal, inicia la prueba de carga. Espera a que termine. Luego:

```text
# sysctl dev.myfirst.0 | grep cmd_ > after.txt
# diff before.txt after.txt
```

El diff muestra exactamente cuántos eventos de cada tipo ocurrieron durante la ventana de prueba. Las ejecuciones ideales muestran muchos éxitos y cero timeouts o errores. Las ejecuciones con fallos inyectados (sección 7) muestran valores distintos de cero para los tipos de error esperados; los recuentos inesperados indican un bug.

Para una observación continua, usa `watch`:

```text
# watch -n 1 'sysctl dev.myfirst.0 | grep cmd_'
```

Cada segundo se imprimen los contadores actuales. A medida que se ejecuta la prueba de carga, el contador de éxitos sube, mientras que los contadores de timeouts y errores (en el caso ideal) se mantienen estables.

### Prueba práctica de comportamiento

Una prueba concreta para la sección 5. Configura:

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_delay_ms_set=50    # fast commands
# sysctl dev.myfirst.0.access_log_enabled=1
```

Ejecuta:

```text
# echo -n "Hello, World!" > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=13 of=/dev/null 2>/dev/null
# sysctl dev.myfirst.0 | grep -E 'cmd_|op_counter|samples_'
```

Salida esperada:

```text
dev.myfirst.0.cmd_successes: 26
dev.myfirst.0.cmd_rdy_timeouts: 0
dev.myfirst.0.cmd_data_timeouts: 0
dev.myfirst.0.cmd_errors: 0
dev.myfirst.0.samples_taken: 13
dev.myfirst.0.reg_op_counter: 26
```

Trece escrituras (una por cada byte de "Hello, World!"), trece lecturas (una por cada byte de vuelta). Cada escritura y cada lectura es un comando de simulación, lo que suma 26 en total. Todos con éxito. Todos dentro del presupuesto de latencia.

Ahora inspecciona el registro de accesos:

```text
# sysctl dev.myfirst.0.access_log | head -20
```

El registro muestra el ciclo de comandos en detalle. Para cada comando de escritura:

1. Leer STATUS (polling hasta que BUSY se desactive)
2. Escribir DATA_IN (el byte)
3. Leer/Escribir CTRL (activando GO)
4. Leer STATUS (polling hasta que DATA_AV se active)
5. Leer DATA_OUT (el resultado)
6. Leer/Escribir STATUS (limpiando DATA_AV)

Para cada comando de muestra, la misma secuencia más un `cbuf_put`. El registro es denso pero legible, y es el primer lugar donde deberías mirar si el driver se comportara de forma incorrecta.

### Lo que logró la etapa 2

La etapa 2 integra la simulación con la ruta de datos del driver. Tanto la ruta de escritura como la de lectura ejercitan ahora los comandos simulados. Los timeouts configurables permiten que las pruebas apunten a comportamientos específicos. Los contadores de estadísticas por tipo de error exponen la interacción del driver con la simulación. Una ruta de recuperación limpia el estado bloqueado cuando un comando agota su tiempo. Las pruebas de carga ejercitan el driver a gran volumen y confirman la composición de la sincronización de la parte 3 con la simulación del capítulo 17.

La etiqueta de versión pasa a ser `1.0-sim-stage2`. El driver interactúa ahora con un bloque de registros dinámico de una manera que se aproxima al comportamiento del hardware real.

### Cerrando la sección 5

Probar el driver contra la simulación no es una actividad separada, sino una extensión natural de la ruta de comandos del driver. Añade timeouts configurables, integra los comandos simulados tanto en lectura como en escritura, registra estadísticas por resultado y ejecuta suficiente carga para exponer los modos de fallo más comunes.

La prueba práctica valida el ciclo completo: la entrada desde el espacio de usuario pasa por el driver, por la simulación y vuelve al espacio de usuario intacta. Los contadores confirman que no hay errores. El registro de accesos muestra cada acceso al registro en orden.

La sección 6 profundiza en el debate sobre el timing. El `pause_sbt` de 1 ms en `myfirst_wait_for_bit` es un valor predeterminado razonable, pero no la única opción. `DELAY(9)` es mejor para esperas muy cortas; `callout_reset_sbt` es mejor para la planificación de tipo «lanzar y olvidar». Entender cuándo es apropiado cada uno es el tema de la sección 6.



## Sección 6: Añadir timing y retardos

El timing es donde el hardware real se vuelve interesante. Un dispositivo que procesa un comando en 5 microsegundos necesita un patrón de driver muy diferente al de uno que lo procesa en 500 milisegundos. Un driver que usa la primitiva de timing equivocada para la escala de tiempo equivocada consume CPU en esperas cortas o acumula latencia en las largas y, en el peor de los casos, provoca un deadlock bajo contención.

FreeBSD ofrece tres primitivas de timing principales para el código de drivers: `DELAY(9)`, `pause_sbt(9)` y `callout_reset_sbt(9)`. Cada una es adecuada para una escala de tiempo específica, cada una tiene un coste específico y cada una tiene un requisito de corrección específico. La sección 6 las enseña en orden y muestra cómo elegir la más adecuada para cada situación en la simulación del capítulo 17.

### Las tres primitivas de timing

Las primitivas difieren en tres ejes: si realizan una espera activa (busy-wait) o duermen, si son cancelables y cuál es su coste.

**`DELAY(n)`** realiza una espera activa durante `n` microsegundos. No duerme, no cede la CPU y no libera ningún lock. Es seguro llamarlo desde cualquier contexto, incluido desde un spinlock, un filtro de interrupciones o un callout. Su coste es el tiempo de CPU que consume: 100 microsegundos de `DELAY` son 100 microsegundos en los que la CPU no puede hacer nada más. Su precisión es generalmente muy buena (de un solo dígito de microsegundos), limitada por la resolución del contador de marca de tiempo de la CPU. Es la elección correcta para esperas muy cortas en las que dormir sería poco práctico.

**`pause_sbt(wmesg, sbt, pr, flags)`** pone al thread que lo llama a dormir durante `sbt` unidades de tiempo binario con signo. Mientras el thread duerme, la CPU queda libre para otros trabajos y los locks que el thread no retiene quedan libres para otros contextos. Si el thread tiene un lock retenido, `pause_sbt` no lo libera; es responsabilidad del llamador soltar los locks que no deberían retenerse durante el sueño. La precisión es del orden del tick del temporizador del kernel (1 ms en un sistema típico). Es la elección correcta para esperas cortas o medias en las que el thread no tiene nada más que hacer. `pause_sbt` no es cancelable mediante ningún mecanismo más corto que el sueño en sí; una vez que un thread lo ha llamado, dormirá al menos el tiempo solicitado (salvo eventos excepcionales).

**`callout_reset_sbt(c, sbt, pr, fn, arg, flags)`** planifica un callback para ejecutarse dentro de `sbt` unidades de tiempo binario con signo. El thread que lo llama no espera; regresa inmediatamente. El callback se ejecuta en un thread de callout cuando el tiempo expira. El callout puede cancelarse con `callout_stop` o `callout_drain`. La precisión es similar a la de `pause_sbt`. Es la elección correcta para la planificación de tipo «lanzar y olvidar», para timeouts que pueden cancelarse anticipadamente y para trabajos periódicos.

Las tres primitivas se pueden combinar. Un driver podría usar `DELAY(10)` para esperar 10 microsegundos a que se estabilice un bit de registro, `pause_sbt(..., SBT_1MS * 50, ...)` para dormir 50 ms esperando a que se complete un comando y `callout_reset_sbt(..., SBT_1S * 5, ...)` para planificar un watchdog 5 segundos en el futuro. Cada elección refleja la escala de tiempo y el contexto.

### Tabla de decisión por escala de tiempo

Una referencia rápida para elegir entre las tres primitivas:

| Duración de la espera | Contexto                                  | Primitiva preferida             |
|-----------------------|-------------------------------------------|---------------------------------|
| < 10 us               | Cualquiera                                | `DELAY(9)`                      |
| 10-100 us             | Sin interrupción, sin spinlock            | `DELAY(9)` o `pause_sbt(9)`     |
| 100 us - 10 ms        | Sin interrupción, sin spinlock            | `pause_sbt(9)`                  |
| > 10 ms               | Sin interrupción, sin spinlock            | `pause_sbt(9)` o callout        |
| Lanzar y olvidar      | Cualquiera (máquina de estados callout)   | `callout_reset_sbt(9)`          |
| Interrupción/spinlock | Cualquier duración                        | Solo `DELAY(9)`                 |

La tabla es un punto de partida, no una regla rígida. La pregunta real es: ¿puede el llamador permitirse dormir? Si es así, pause o callout. Si no (spinlock retenido, manejador de interrupciones, manejador de filtros, sección crítica), DELAY.

### Dónde usa ya cada una el driver del capítulo 17

El bucle de polling de la sección 4 usa `pause_sbt` con retardos de 1 ms. Eso es adecuado para la escala de tiempo: el `DELAY_MS` predeterminado de la simulación es 500, así que esperamos que el driver duerma muchas veces antes de que el comando se complete. Dormir permite que otros threads usen la CPU mientras esperamos. `DELAY(1000)` también funcionaría, pero desperdiciaría CPU; el driver actualmente ejecuta unas 500 encuestas por comando sin ningún trabajo de CPU útil entre ellas, y `DELAY` giraría las 500 veces.

Los callouts de simulación de la sección 3 usan `callout_reset_sbt` con intervalos variables. Eso es adecuado porque la simulación necesita hacer trabajo en el futuro sin que ningún thread espere activamente. `pause_sbt` requeriría un thread del kernel dedicado para cada callout, lo que sería un desperdicio. `DELAY` requeriría que el thread permaneciera en modo kernel durante el intervalo completo, lo que es inaceptable.

Todavía no hay ningún `DELAY` en el driver. La sección 6 lo introduce donde corresponde.

### Un buen uso de `DELAY`

Considera la ruta de recuperación de la sección 5. Tras un timeout, el driver llama a `myfirst_recover_from_stuck`, que limpia algún estado y regresa. El código funciona, pero existe una condición de carrera sutil: el callout de comandos de la simulación podría dispararse entre la detección del timeout y la llamada de recuperación, modificando `STATUS` de formas inesperadas. Una pequeña corrección: usar `DELAY(10)` para esperar 10 microsegundos a que termine cualquier callout en vuelo antes de limpiar el estado.

Pero esto sería incorrecto. `DELAY(10)` no hace nada útil aquí. El callout no puede ejecutarse mientras retenemos `sc->mtx` (porque fue registrado con `callout_init_mtx`), así que no hay condición de carrera. Añadir un `DELAY` aquí sería cargo-culting: añadir un retardo porque algo podría salir mal, sin un modelo claro de lo que ese retardo está previniendo.

Un ejemplo mejor. Algunos dispositivos hardware reales requieren un pequeño retardo entre dos escrituras en registros para que la lógica interna del dispositivo se estabilice. Una hoja de datos podría decir: «Tras escribir en `CTRL.RESET`, espera al menos 5 microsegundos antes de escribir en cualquier otro registro». El bit `CTRL.RESET` de la simulación del capítulo 17 no está implementado en realidad, pero podemos imaginarlo. Si lo estuviera, el código de reinicio usaría `DELAY(5)`:

```c
static void
myfirst_reset_device(struct myfirst_softc *sc)
{
        MYFIRST_LOCK_ASSERT(sc);

        /* Assert reset. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_RESET);

        /* Datasheet: wait 5 us for internal state to clear. */
        DELAY(5);

        /* Deassert reset. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_RESET, 0);
}
```

Cinco microsegundos son demasiado cortos para que `pause_sbt` sea eficiente (la resolución del temporizador del kernel es típicamente de 1 ms, así que `pause_sbt` para 5 us en realidad dormiría hasta el siguiente tick del temporizador, desperdiciando casi un milisegundo). `DELAY(5)` es la primitiva correcta.

### Un buen uso de `pause_sbt` (soltando el lock)

El bucle de polling de la sección 4:

```c
for (i = 0; i < timeout_ms; i++) {
        uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        if ((status & mask) == target)
                return (0);
        MYFIRST_UNLOCK(sc);
        pause_sbt("mfwait", SBT_1MS, 0, 0);
        MYFIRST_LOCK(sc);
}
```

El lock se suelta antes de `pause_sbt` y se readquiere después. Este patrón es esencial y merece atención cuidadosa.

Si el lock se retuviera durante `pause_sbt`, el callout de la simulación (que necesita el lock) quedaría bloqueado durante toda la duración del sueño. Como el callout es precisamente lo que el driver está esperando, esto provocaría un deadlock. Soltar el lock permite que el callout se ejecute, que la simulación actualice `STATUS` y que la siguiente iteración del bucle observe el nuevo valor.

El requisito de corrección es que nada de lo que le importa al driver pueda cambiar de forma irrecuperable mientras el lock está soltado. En nuestro caso, las variables de bucle del driver son locales (`i`, `status`, `mask`, `target`); no viven en memoria a la que acceden otros contextos. El estado que otros contextos tocan (el bloque de registros) es exactamente lo que queremos que actualicen. Por tanto, soltar el lock es seguro.

Una variación de este patrón usa una variable de condición dormible en lugar de polling:

```c
MYFIRST_LOCK_ASSERT(sc);

while ((CSR_READ_4(sc, MYFIRST_REG_STATUS) & mask) != target) {
        int error = cv_timedwait_sbt(&sc->data_cv, &sc->mtx,
            SBT_1MS * timeout_ms, 0, 0);
        if (error == EWOULDBLOCK)
                return (ETIMEDOUT);
}
return (0);
```

La variable de condición espera hasta que el bit objetivo cambie (señalado por algún otro contexto) o hasta que el timeout expire. `cv_timedwait_sbt` suelta el mutex automáticamente, duerme y readquiere el mutex antes de retornar. No se necesita ningún envoltorio explícito de `UNLOCK`/`LOCK`.

Para que esto funcione, el callout de la simulación que modifica `STATUS` debe llamar a `cv_signal` en la variable de condición tras el cambio. El código en `myfirst_sim_command_cb` se convierte en:

```c
/* After setting DATA_AV: */
cv_broadcast(&sc->data_cv);
```

El bucle de espera del driver y la actualización de la simulación cooperan a través de la variable de condición: el driver duerme hasta que ocurre algo interesante, y la simulación lo despierta cuando eso sucede. Este patrón es mucho más eficiente que el polling con `pause_sbt`. El driver duerme durante toda la espera; basta con un único despertar; sin thrashing.

El capítulo 17 usa `pause_sbt` en el bucle de polling principal por simplicidad didáctica (puedes ver exactamente cuándo ocurre el sueño), pero el patrón de variable de condición se menciona aquí porque se aproxima más a lo que hacen los drivers en producción, especialmente para esperas largas.

### Un buen uso de `callout_reset_sbt`

Los callouts de simulación de la sección 3 son el ejemplo canónico. El callout del sensor se dispara cada 100 ms, actualiza el registro del sensor y se reactiva. El callout de comando se dispara una sola vez transcurridos `DELAY_MS` milisegundos para completar un comando. Ambos utilizan `callout_reset_sbt` con:

- `sbt` = el intervalo en unidades SBT
- `pr` = 0 (sin indicación de precisión; el kernel puede agrupar disparos)
- `flags` = 0 (sin comportamiento especial)

Para una planificación más precisa, `pr` puede configurarse para acotar el agrupamiento:

```c
callout_reset_sbt(&sim->sensor_callout,
    interval_ms * SBT_1MS,
    SBT_1MS,                   /* precision: within 1 ms */
    myfirst_sim_sensor_cb, sc, 0);
```

Con `pr = SBT_1MS`, el callout se dispara como máximo 1 ms más tarde de lo solicitado. Para nuestra simulación, la precisión no es crítica; el valor predeterminado de 0 es suficiente.

Para un callout que deba ejecutarse en una CPU específica, el flag `C_DIRECT_EXEC` hace que el callback se ejecute directamente desde la interrupción hardclock en lugar de ser despachado a un thread de callout. Esto ofrece mayor rendimiento, pero es más restrictivo: el callback no puede dormir ni adquirir sleep locks. La simulación del capítulo 17 no necesita esto; utiliza callouts basados en mutex simples.

Para un callout alineado con el límite del hardclock, el flag `C_HARDCLOCK` es el adecuado. Nuestra simulación tampoco necesita alineación; el valor predeterminado es suficiente.

### Un ejemplo realista: simulación de un dispositivo lento

Veamos las primitivas en conjunto. Supongamos que la simulación está configurada con `DELAY_MS=2000` (un dispositivo lento, dos segundos por comando). El driver emite un comando de escritura. ¿Qué hace cada primitiva?

1. El driver escribe en `DATA_IN`, activa `CTRL.GO` y llama a `myfirst_wait_for_data(sc, 3000)` (timeout de 3 s).
2. `myfirst_wait_for_data` lee `STATUS`, comprueba que `DATA_AV` no está activo, libera el lock, llama a `pause_sbt("mfwait", SBT_1MS, 0, 0)` y duerme ~1 ms.
3. Mientras tanto, el callout `myfirst_sim_command_cb` de la simulación está programado para ejecutarse en ~2000 ms. Todavía no ha arrancado.
4. El bucle del driver se despierta, reaplica el lock, lee `STATUS` de nuevo, comprueba que `DATA_AV` sigue sin estar activo, libera el lock y vuelve a dormir.
5. Los pasos 2 al 4 se repiten unas 2000 veces a lo largo de los dos segundos.
6. El callout de la simulación se dispara. Lee `STATUS`, activa `DATA_AV` y escribe los valores de finalización del comando.
7. En la siguiente iteración del bucle del driver, este detecta `DATA_AV` activo y retorna.

El tiempo total de CPU consumido por el driver en espera es mínimo. Cada `pause_sbt` es un sleep, no un spin. La CPU queda libre para otros trabajos durante cada uno de los 2000 ciclos de espera.

Supongamos ahora que el driver hubiera usado `DELAY(1000)` en lugar de `pause_sbt(..., SBT_1MS, ...)`. La escala temporal por iteración es la misma, pero el comportamiento de la CPU es completamente diferente. Cada `DELAY(1000)` consume 1000 microsegundos de CPU. 2000 iteraciones equivalen a 2 segundos de CPU pura. El driver funciona, pero acapara un núcleo entero durante toda la duración del comando. Si otros procesos necesitan CPU, esperan.

Esta diferencia ilustra por qué `pause_sbt` es la elección correcta para esperas en la escala de milisegundos: el sleep no tiene coste; el spin consume CPU.

Supongamos ahora que el driver hubiera usado `callout_reset_sbt` en lugar de polling. Esto es más complejo: `callout_reset_sbt` no hace que el thread actual espere. El thread retornaría a su llamador de inmediato. El llamador necesitaría saber que debe dormirse hasta que el callout se dispare. Eso es exactamente lo que hacen las variables de condición. El código quedaría así:

```c
MYFIRST_LOCK_ASSERT(sc);

while (!(CSR_READ_4(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_DATA_AV)) {
        int error = cv_timedwait_sbt(&sc->data_cv, &sc->mtx,
            SBT_1MS * timeout_ms, 0, 0);
        if (error == EWOULDBLOCK)
                return (ETIMEDOUT);
}
```

Y el callout de la simulación llama a `cv_broadcast(&sc->data_cv)` tras activar `DATA_AV`. El thread del driver duerme una sola vez (no 2000); la carga sobre la CPU es mínima; el tiempo de respuesta está acotado por el propio temporizador de la simulación, no por la frecuencia de polling del driver.

En el capítulo 17, el patrón de polling se enseña primero porque es más fácil de leer: el bucle `for` es explícito, el `pause_sbt` es explícito y el `CSR_READ_4` es explícito. El patrón de variable de condición es más eficiente, pero exige comprender las tres primitivas del capítulo 12. Usamos el patrón de polling como escalón y señalamos el patrón de variable de condición como la preferencia en producción.

### El temporizado de la propia simulación

Las decisiones de temporizado de la propia simulación merecen un examen.

El callout del sensor se dispara con el intervalo que especifica el registro `SENSOR_CONFIG`. El valor por defecto es 100 ms. El usuario puede modificarlo. El kernel respeta el intervalo solicitado con una precisión de pocos milisegundos. Esto es suficiente para un sensor de enseñanza; un dispositivo real podría requerir precisión de submicrosegundo, lo que exigiría callbacks `C_DIRECT_EXEC` o incluso fuentes de temporizado no estándar.

El callout del comando se dispara `DELAY_MS` milisegundos después de emitir el comando. De nuevo, la precisión de milisegundos es suficiente. Los timeouts del driver están calibrados en consecuencia: el timeout de comando por defecto es de 2000 ms, muy por encima del valor por defecto de `DELAY_MS`, que es 500 ms.

El intervalo de polling del driver es de 1 ms. Para un comando que tarda 500 ms, eso supone ~500 sondeos, lo cual es tolerable. Para un comando que tarda 5 ms, supone ~5 sondeos, lo cual es suficientemente rápido. Para un comando que tarda 5 segundos, supone 5000 sondeos, lo cual resulta desaprovechado. El intervalo de polling podría ser adaptativo (sondeos más largos para comandos más largos), pero la complejidad de implementación no merece la pena para un driver de enseñanza.

### Restricciones realistas: listo después de 500 ms

Para hacer más concreto el relato del temporizado, configura la simulación con un retardo de 500 ms por comando y ejecuta un test de carga:

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=500
# time echo -n "test" > /dev/myfirst0
```

Salida esperada:

```text
real    0m2.012s
user    0m0.001s
sys     0m0.005s
```

Cuatro bytes, 500 ms cada uno, 2 segundos en total. Casi todo el tiempo es tiempo de sleep del kernel; el uso de CPU es despreciable. El driver es eficiente aunque el comando sea lento.

Ahora ejecuta el test con escritores concurrentes:

```text
# for i in 1 2 3 4; do
    (echo -n "ABCD" > /dev/myfirst0) &
  done
# time wait
```

Cuatro procesos, 4 bytes cada uno, 500 ms por byte. Total de comandos: 16. Con un comando cada 500 ms (serializados a través del driver lock), el tiempo total es de 8 segundos. Resultado esperado:

```text
real    0m8.092s
```

El driver lock serializa los comandos, y la propia comprobación `command_pending` de la simulación impide solapamientos. El rendimiento efectivo es de un comando cada `DELAY_MS` milisegundos, independientemente del número de escritores.

### Manejo elegante de los timeouts

El comportamiento del driver cuando un comando supera el timeout merece un recorrido propio. La ruta del comando:

1. El driver escribe en `DATA_IN` y activa `CTRL.GO`.
2. El driver espera hasta `cmd_timeout_ms` para que se active `STATUS.DATA_AV`.
3. Si la espera supera el timeout:
   a. Incrementa el contador `cmd_data_timeouts`.
   b. Llama a `myfirst_recover_from_stuck` para limpiar el estado obsoleto de la simulación.
   c. Retorna `ETIMEDOUT` al llamador.
4. Si la espera tiene éxito, continúa con la ruta normal.

La recuperación es importante. Una simulación que realmente es lenta (no bloqueada) acabará activando `DATA_AV`, pero el driver ya habrá desistido. Si el driver no limpia el estado pendiente de la simulación, un comando posterior podría ver el `DATA_AV` residual del comando antiguo y creer que el nuevo comando completó de inmediato. La función de recuperación detiene el callout pendiente, borra la marca `command_pending` y limpia los bits relevantes de `STATUS`.

En hardware real, la recuperación equivalente es un reset del dispositivo. El driver escribe en `CTRL.RESET`, espera brevemente y reinicia. Algunos dispositivos disponen de un mecanismo de reset independiente accesible a través del espacio de configuración PCI. El patrón es el mismo: tras un timeout, devuelve el dispositivo a un estado conocido antes de hacer cualquier otra cosa.

### Una técnica de depuración: ralentización artificial

Una técnica de depuración útil para código sensible al temporizado: ralentiza artificialmente la simulación para ampliar las ventanas de condición de carrera.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=3000
# sysctl dev.myfirst.0.cmd_timeout_ms=1000
```

Ahora cada comando tarda 3 segundos, pero el driver solo espera 1 segundo. Cada comando supera el timeout. La ruta de recuperación del driver se ejecuta con cada comando. Los errores en la ruta de recuperación afloran de inmediato:

- Si la ruta de recuperación falla, el kernel entra en pánico en el primer comando.
- Si la ruta de recuperación deja estado obsoleto, el segundo comando falla de alguna manera inusual.
- Si la ruta de recuperación tiene una fuga de memoria, la salida de `vmstat -m | grep myfirst` aumenta con el tiempo.

Esta es una técnica de depuración legítima. Es difícil someter a este tipo de estrés a los drivers reales porque el hardware real es difícil de ralentizar bajo demanda. La simulación, en cambio, está a un solo registro de ser muy lenta. Aprovecha esta capacidad para ejercitar rutas de código que la operación normal apenas alcanza.

### Errores comunes de temporizado

Un breve catálogo de errores de temporizado que conviene evitar.

**Error 1: usar `DELAY` para esperas de milisegundos.** `DELAY(1000000)` realiza una espera activa de un segundo completo, consumiendo un núcleo de CPU durante ese tiempo. Usa `pause_sbt(..., SBT_1S, ...)` en su lugar.

**Error 2: mantener un lock durante una llamada a `pause_sbt`.** Bloquea cualquier contexto que necesite el lock. Libera el lock, duerme y vuelve a adquirirlo.

**Error 3: realizar un spin sin ceder en una condición que depende de otro contexto.** Si tu código espera que un callout active un bit, el callout no puede ejecutarse mientras estás en spin. Cede con `pause_sbt` (liberando el lock) o usa una variable de condición.

**Error 4: usar `callout_reset_sbt` para una acción de disparo y olvido que debería completarse de inmediato.** Si la acción es rápida, hazla directamente. Los callouts tienen coste de configuración y sobrecarga de planificación; solo valen la pena cuando el retardo es significativo.

**Error 5: fijar un timeout menor que el tiempo de operación esperado.** Cada comando supera el timeout. El driver no realiza ningún trabajo útil. Define timeouts generosos pero finitos.

**Error 6: fijar un timeout mucho mayor que el tiempo de operación esperado.** Una operación legítimamente lenta deja colgada la llamada desde el espacio de usuario durante el timeout completo. Los usuarios pierden la paciencia. Define timeouts razonables para cada operación concreta.

**Error 7: no drenar los callouts antes de liberar la memoria que utilizan.** El callout se dispara después de liberar la memoria, y el callback desreferencia memoria ya liberada. Drena antes de liberar.

**Error 8: rearmar un callout desde su propio callback sin comprobar la cancelación.** La ruta de detach pide al callout que se detenga; el callback se rearma de todas formas. Bucle infinito; el detach nunca termina. Comprueba la marca `running` (como hace `myfirst_sim_sensor_cb` de la sección 3) antes de rearmar.

Cada uno de estos errores es habitual en código de driver principiante. Las etapas del capítulo 17 los evitan todos siguiendo los patrones que el capítulo 13 enseñó para los callouts, el capítulo 12 enseñó para las variables de condición y la sección 6 está haciendo ahora explícitos.

### Cierre de la sección 6

Tres primitivas de temporizado, tres escalas temporales, tres perfiles de coste. `DELAY(9)` para esperas muy cortas (< 100 us). `pause_sbt(9)` para esperas medias (100 us a varios segundos) cuando el thread no tiene nada más que hacer. `callout_reset_sbt(9)` para la planificación de tipo disparo y olvido y para trabajo periódico. Elegir la correcta es una decisión de criterio basada en la escala temporal, el contexto y lo que el thread haría de otro modo.

El driver del capítulo 17 usa `pause_sbt` para su bucle de polling y `callout_reset_sbt` para los temporizadores propios de la simulación. Una ruta de reset usa `DELAY(5)` donde corresponde. La historia de temporizado del driver es coherente: nunca desperdicia CPU, nunca mantiene locks durante los sleeps y nunca bloquea otros contextos de manera innecesaria.

La sección 7 añade la última pieza principal de la simulación del capítulo 17: un framework de inyección de fallos. Con el temporizado ya fiable, el driver puede ejercitarse bajo una variedad de condiciones de fallo y validar las rutas de recuperación de errores.



## Sección 7: simulación de errores y condiciones de fallo

Un driver que supera todas las pruebas del camino feliz y nunca ejercita sus rutas de error es un driver que acabará fallando en producción. Las rutas de error son el código que se ejecuta cuando ocurre algo inesperado, e «inesperado» es la palabra más honesta para describir lo que hace el hardware real en un mal día. Una tarjeta de red cuyo PHY ha perdido la señal de enlace. Un controlador de disco cuya cola de comandos se ha desbordado. Un sensor cuya calibración ha salido de rango. Un dispositivo cuyo firmware ha entrado en deadlock tras una secuencia específica de comandos.

El hardware real produce estas condiciones con poca frecuencia, y generalmente en el peor momento posible. Un autor de drivers que espera a que el hardware real las produzca es un autor de drivers cuyos clientes encontrarán los errores. Un autor de drivers que usa la simulación para producirlas bajo demanda puede corregirlos antes de que lleguen a producción.

La sección 7 incorpora un framework de inyección de fallos a la simulación del capítulo 17. El framework permite al lector pedirle a la simulación que se comporte incorrectamente de formas específicas y controladas: un timeout, una corrupción de datos, un estado stuck-busy, un fallo aleatorio. Cada modo ejercita una ruta de código diferente del driver. Al final de la sección 7, el driver habrá experimentado cada error que está diseñado para manejar, y el desarrollador tendrá confianza en que las rutas de error funcionan.

### La filosofía de la inyección de fallos

Antes de escribir código, una breve pausa filosófica. ¿Cómo debería ser un framework de inyección de fallos y cómo no debería ser?

Un buen framework de inyección de fallos:

- Se centra en modos de fallo específicos, no en el caos aleatorio. Un fallo que dice "la siguiente lectura devuelve 0xFFFFFFFF" es útil; uno que dice "puede que ocurra algo malo" no lo es.
- Es controlable con granularidad fina. Los fallos pueden activarse, desactivarse o aplicarse de forma parcial. Se puede inyectar uno, varios o de manera probabilística.
- Es observable. Cuando se dispara un fallo, el framework lo registra (o incrementa un contador). El tester puede ver qué ha ocurrido y correlacionarlo con el comportamiento del driver.
- Es ortogonal a otras pruebas. Una prueba de carga no debería preocuparse por si los fallos están habilitados; una prueba de fallos no debería requerir que la carga esté desactivada.
- Es determinista cuando se configura para serlo. Un fallo configurado con probabilidad del 100 % se dispara siempre. Uno configurado con probabilidad del 50 % se dispara la mitad de las veces, y la decisión pseudoaleatoria es reproducible con una semilla.

Un framework de inyección de fallos deficiente es inútil (nunca activa los caminos que necesitas) o peligroso (produce fallos de los que el driver no puede recuperarse ni siquiera en principio). El objetivo es la falta de fiabilidad útil, no el caos destructivo.

El framework del capítulo 17 logra estos objetivos con un pequeño conjunto de flags en `FAULT_MASK`, una probabilidad en `FAULT_PROB` y un contador que registra cuántos fallos se han producido. La simulación consulta estos valores cada vez que comienza una operación y decide si procede con normalidad o inyecta un fallo según la configuración.

### Los modos de fallo

Los cuatro modos de fallo que implementa el capítulo 17:

**`MYFIRST_FAULT_TIMEOUT`** (bit 0). El siguiente comando nunca llega a completarse. El callout del comando no se programa; `STATUS.DATA_AV` nunca se activa. La función `myfirst_wait_for_data` del driver agota su tiempo de espera, lo que activa la ruta de recuperación.

**`MYFIRST_FAULT_READ_1S`** (bit 1). La siguiente lectura de `DATA_OUT` devuelve `0xFFFFFFFF` en lugar del valor real. Esto simula una lectura de bus que falla de la manera en que lo hacen muchos buses de hardware reales (una lectura de un dispositivo desconectado o sin alimentación devuelve habitualmente todos los bits a uno).

**`MYFIRST_FAULT_ERROR`** (bit 2). El siguiente comando se completa, pero con `STATUS.ERROR` activo. Se espera que el driver detecte el error, limpie el latch y notifique el error al llamador.

**`MYFIRST_FAULT_STUCK_BUSY`** (bit 3). `STATUS.BUSY` queda bloqueado en alto y nunca se borra. La función `myfirst_wait_for_ready` del driver agota su tiempo de espera antes de que pueda emitir ningún comando. Esto simula un dispositivo que se ha quedado bloqueado y necesita un reset.

Cada fallo ejercita una ruta diferente. El fallo de timeout ejercita `cmd_data_timeouts` y la recuperación. El fallo read-1s comprueba si el driver detecta datos corruptos. El fallo de error ejercita `cmd_errors` y la lógica de limpieza de errores. El fallo de busy bloqueado ejercita `cmd_rdy_timeouts`.

Es posible tener varios fallos activos simultáneamente: basta con activar varios bits en `FAULT_MASK`. Cada fallo se aplica de forma independiente en el punto de la simulación en que resulta relevante.

### El campo de probabilidad de fallo

`FAULT_MASK` selecciona qué fallos son candidatos; `FAULT_PROB` controla con qué frecuencia se activa realmente un fallo candidato. Las probabilidades se expresan como enteros entre 0 y 10000, donde 10000 significa el 100% (fallo en todas las operaciones) y 0 significa nunca. Esto proporciona cuatro decimales de precisión sin necesidad de aritmética de punto flotante.

La prueba para determinar si un fallo se activa:

```c
static bool
myfirst_sim_should_fault(struct myfirst_softc *sc)
{
        uint32_t prob, r;

        MYFIRST_LOCK_ASSERT(sc);

        prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
        if (prob == 0)
                return (false);
        if (prob >= 10000)
                return (true);

        r = arc4random_uniform(10000);
        return (r < prob);
}
```

`arc4random_uniform(10000)` devuelve un entero pseudoaleatorio en el rango `[0, 10000)`. Si es menor que `prob`, el fallo se activa. Con `prob = 5000` (50%), el fallo se activa aproximadamente la mitad de las veces. Con `prob = 100` (1%), alrededor de una operación de cada cien. Con `prob = 10000`, en todas las operaciones.

La función se llama al inicio de cada operación. Si devuelve true, la operación aplica un fallo de `FAULT_MASK` (la lógica de la simulación decide qué fallo o fallos aplicar, generalmente el primero que coincide).

### Implementación del fallo de timeout

El fallo de timeout es el más sencillo. En `myfirst_sim_start_command`, comprueba la máscara de fallos antes de programar el callout del comando:

```c
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t data_in, delay_ms, fault_mask;
        bool fault;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        if (sim->command_pending) {
                sc->stats.cmd_rejected++;
                device_printf(sc->dev,
                    "sim: overlapping command; ignored\n");
                return;
        }

        data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
        sim->pending_data = data_in;

        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                status |= MYFIRST_STATUS_BUSY;
                status &= ~MYFIRST_STATUS_DATA_AV;
                CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
        }

        delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
        if (delay_ms == 0)
                delay_ms = 1;

        sim->command_pending = true;

        /* Fault-injection check. */
        fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        fault = (fault_mask != 0) && myfirst_sim_should_fault(sc);

        if (fault && (fault_mask & MYFIRST_FAULT_TIMEOUT)) {
                /* Do not schedule the completion callout. The command
                 * will never complete; the driver will time out.
                 * (Simulation does not clear command_pending; the
                 * driver's recovery path is responsible for that.) */
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting TIMEOUT fault\n");
                return;
        }

        /* Save the fault state for the callout to honour. */
        sim->pending_fault = fault ? fault_mask : 0;

        callout_reset_sbt(&sim->command_callout,
            delay_ms * SBT_1MS, 0,
            myfirst_sim_command_cb, sc, 0);
}
```

Cuatro cambios respecto a la versión de la sección 3.

Primero, un contador de estadísticas para `cmd_rejected` reemplaza al printf simple.

Segundo, la función lee `FAULT_MASK` y llama a `myfirst_sim_should_fault` para decidir si esta operación inyecta un fallo.

Tercero, si se selecciona el fallo de timeout, la función regresa sin programar el callout de finalización. `command_pending` se establece en true, de modo que los comandos solapados siguen siendo rechazados, pero nunca se dispara ningún callout que complete el comando.

Cuarto, la simulación almacena el estado del fallo en `sim->pending_fault` para que el callout (cuando se dispare, en el caso de fallos que no son de timeout) pueda respetar el fallo.

`pending_fault` se añade a la estructura de estado de la simulación:

```c
struct myfirst_sim {
        /* ... existing fields ... */
        uint32_t   pending_fault;   /* FAULT_MASK bits to apply at completion */
};
```

### Implementación del fallo de error

El fallo de error se activa al completarse el comando. En `myfirst_sim_command_cb`:

```c
static void
myfirst_sim_command_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t status, fault;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running || !sim->command_pending)
                return;

        fault = sim->pending_fault;

        /* Always clear BUSY. */
        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~MYFIRST_STATUS_BUSY;

        if (fault & MYFIRST_FAULT_ERROR) {
                /* Set ERROR, do not set DATA_AV. The driver should
                 * detect the error on the next STATUS read. */
                status |= MYFIRST_STATUS_ERROR;
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting ERROR fault\n");
        } else if (fault & MYFIRST_FAULT_READ_1S) {
                /* Normal completion, but DATA_OUT is corrupted. */
                CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, 0xFFFFFFFF);
                status |= MYFIRST_STATUS_DATA_AV;
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting READ_1S fault\n");
        } else {
                /* Normal completion. */
                CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);
                status |= MYFIRST_STATUS_DATA_AV;
        }

        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

        sim->op_counter++;
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

        sim->command_pending = false;
        sim->pending_fault = 0;
}
```

Tres ramas. El fallo de error establece `STATUS.ERROR` en lugar de `STATUS.DATA_AV`, dejando `DATA_OUT` sin modificar. El fallo read-1s escribe `0xFFFFFFFF` en `DATA_OUT` y establece `DATA_AV` con normalidad. La rama normal escribe los datos reales y establece `DATA_AV`.

La simulación incrementa `op_counter` y `fault_injected` según corresponda. El driver verá los efectos a través de los valores de los registros; los contadores permiten a la prueba validar que los fallos se han activado.

### Implementación del fallo de busy bloqueado

El fallo de busy bloqueado es ortogonal al ciclo de comandos: mantiene `STATUS.BUSY` activo con independencia de cualquier comando. Añade un callout que monitoriza `FAULT_MASK` y mantiene `STATUS.BUSY` bloqueado cuando el bit está activo:

```c
static void
myfirst_sim_busy_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t fault_mask, status;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        if (fault_mask & MYFIRST_FAULT_STUCK_BUSY) {
                status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (!(status & MYFIRST_STATUS_BUSY)) {
                        status |= MYFIRST_STATUS_BUSY;
                        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
                }
        }

        /* Re-arm every 50 ms. */
        callout_reset_sbt(&sim->busy_callout,
            50 * SBT_1MS, 0,
            myfirst_sim_busy_cb, sc, 0);
}
```

Inicializa y arranca el callout junto al callout del sensor en `myfirst_sim_attach` y `myfirst_sim_enable`:

```c
callout_init_mtx(&sim->busy_callout, &sc->mtx, 0);
/* ... in enable: */
callout_reset_sbt(&sim->busy_callout, 50 * SBT_1MS, 0,
    myfirst_sim_busy_cb, sc, 0);
/* ... in disable: */
callout_stop(&sim->busy_callout);
/* ... in detach: */
callout_drain(&sim->busy_callout);
```

Así, cuando `FAULT_STUCK_BUSY` está activo, el callout de busy reaserta `STATUS.BUSY` continuamente cada 50 ms. Cualquier comando que el driver intente emitir encontrará `BUSY` activo, esperará a que se borre y agotará su tiempo de espera en `wait_for_ready`. Borrar el bit `FAULT_STUCK_BUSY` detiene la reasertación, y `BUSY` vuelve a su estado natural (el que haya dejado la ruta del comando).

### Las rutas de gestión de errores del driver

Con los fallos conectados, las rutas de gestión de errores del driver se ejercitan. Revísalas ahora para confirmar que funcionan correctamente.

**Timeout en `wait_for_ready`** (de la sección 5):

```c
error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
if (error != 0) {
        sc->stats.cmd_rdy_timeouts++;
        return (error);
}
```

Cuando `FAULT_STUCK_BUSY` está activo, se activa esta ruta. El llamador recibe `ETIMEDOUT`. El driver no intenta emitir el comando porque no puede. El contador refleja el evento.

**Timeout en `wait_for_data`**:

```c
error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
if (error != 0) {
        sc->stats.cmd_data_timeouts++;
        myfirst_recover_from_stuck(sc);
        return (error);
}
```

Cuando `FAULT_TIMEOUT` está activo, se activa esta ruta. El driver llama a `myfirst_recover_from_stuck`, que borra el flag `command_pending` de la simulación y `STATUS.BUSY`. El siguiente comando encuentra un estado limpio. El contador refleja el evento.

**Detección de `STATUS.ERROR`**:

```c
status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
if (status & MYFIRST_STATUS_ERROR) {
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_ERROR, 0);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_DATA_AV, 0);
        sc->stats.cmd_errors++;
        return (EIO);
}
```

Cuando `FAULT_ERROR` está activo, se activa esta ruta. El driver borra `STATUS.ERROR` y `STATUS.DATA_AV`, incrementa el contador y devuelve `EIO`.

**La gestión de `DATA_OUT` corrupto** requiere una adición. Cuando `FAULT_READ_1S` está activo, `DATA_OUT` vale `0xFFFFFFFF`, pero el driver actualmente no comprueba esto. Si el driver debe comprobarlo depende del protocolo: para un sensor, `0xFFFFFFFF` es a menudo un marcador legítimo de "error de lectura"; para otros dispositivos, podría ser un valor de datos plausible. El driver del capítulo 17 lo trata como un posible error a modo ilustrativo:

```c
if (data_out == 0xFFFFFFFF) {
        /* Likely a bus read error. Real devices rarely produce this
         * value as legitimate data; treat as an error. */
        sc->stats.cmd_errors++;
        return (EIO);
}
```

Este comportamiento es una decisión de diseño. Un driver real para un dispositivo real consultaría la hoja de datos del dispositivo para conocer sus valores de error documentados y respondería en consecuencia.

### Prueba de los fallos uno a uno

Con la infraestructura en su lugar, cada fallo puede probarse de forma aislada:

**Prueba 1: fallo de timeout al 100%.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x1    # MYFIRST_FAULT_TIMEOUT
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # 100%
# echo -n "test" > /dev/myfirst0
write: Operation timed out
# sysctl dev.myfirst.0.cmd_data_timeouts
dev.myfirst.0.cmd_data_timeouts: 1
# sysctl dev.myfirst.0.fault_injected
dev.myfirst.0.fault_injected: 1
```

El primer byte provoca un timeout. El driver se recupera. Los bytes siguientes también agotarían el tiempo de espera hasta que se borre el fallo. Los contadores reflejan el evento.

Desactiva el fallo:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# echo -n "test" > /dev/myfirst0
# echo "write succeeded"
write succeeded
# sysctl dev.myfirst.0.cmd_successes
dev.myfirst.0.cmd_successes: 4   # or more, depending on history
```

Los comandos vuelven a tener éxito.

**Prueba 2: fallo de error al 25%.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x4    # MYFIRST_FAULT_ERROR
# sysctl dev.myfirst.0.reg_fault_prob_set=2500   # 25%
# sysctl dev.myfirst.0.cmd_errors
dev.myfirst.0.cmd_errors: 0
# for i in $(seq 1 40); do echo -n "X" > /dev/myfirst0; done
write: Input/output error     (occurs roughly 10 times)
# sysctl dev.myfirst.0.cmd_errors
dev.myfirst.0.cmd_errors: 9    # approximately 25% of 40
```

Aproximadamente uno de cada cuatro comandos termina en error. El driver detecta el error, limpia el estado y devuelve `EIO`. El contador refleja el evento.

**Prueba 3: fallo de busy bloqueado.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x8    # MYFIRST_FAULT_STUCK_BUSY
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # (prob doesn't matter; latch is always on)
# echo -n "X" > /dev/myfirst0
write: Operation timed out
# sysctl dev.myfirst.0.cmd_rdy_timeouts
dev.myfirst.0.cmd_rdy_timeouts: 1
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3    # READY|BUSY
```

El driver no puede emitir ningún comando porque `BUSY` está permanentemente activo. Borra el fallo:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sleep 1     # wait for the busy callout to stop re-asserting
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1    # READY only
# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

### Integración: fallos aleatorios bajo carga

La prueba más realista configura fallos aleatorios con una probabilidad baja y ejecuta el driver bajo carga:

```sh
#!/bin/sh
# fault_stress.sh: random faults under load.

# 1% probability of TIMEOUT or ERROR (bits 0 and 2 both set).
sysctl dev.myfirst.0.reg_fault_mask_set=0x5
sysctl dev.myfirst.0.reg_fault_prob_set=100

# Fast commands so the test runs in reasonable time.
sysctl dev.myfirst.0.reg_delay_ms_set=10
sysctl dev.myfirst.0.cmd_timeout_ms=50    # very short, to spot timeouts fast

# Load: 8 parallel workers, each doing 200 round trips.
for i in $(seq 1 8); do
    (for j in $(seq 1 200); do
        echo -n "X" > /dev/myfirst0 2>/dev/null
        dd if=/dev/myfirst0 bs=1 count=1 of=/dev/null 2>/dev/null
    done) &
done
wait

# Report.
sysctl dev.myfirst.0 | grep -E 'cmd_|fault_|op_counter'

# Clean up.
sysctl dev.myfirst.0.reg_fault_mask_set=0
sysctl dev.myfirst.0.reg_fault_prob_set=0
sysctl dev.myfirst.0.reg_delay_ms_set=500
sysctl dev.myfirst.0.cmd_timeout_ms=2000
```

Contadores esperados tras la ejecución:

- `cmd_successes`: alrededor de 3168 (de los ~3200 ciclos de ida y vuelta esperados)
- `cmd_data_timeouts`: alrededor de 16 (el 1% de la mitad de las operaciones)
- `cmd_errors`: alrededor de 16 (el 1% de la mitad de las operaciones)
- `fault_injected`: alrededor de 32 (suma de los dos anteriores)
- `cmd_recoveries`: alrededor de 16 (una por cada timeout)

Los números exactos varían en cada ejecución (los fallos son aleatorios), pero las proporciones deberían rondar el 1% para cada tipo de fallo. Si las proporciones se desvían notablemente, hay un bug en la inyección de fallos o en el conteo de errores del driver. En cualquier caso, la prueba está haciendo su trabajo al sacar a la luz las anomalías.

### Un error frecuente: la recuperación que nunca limpia todo

Un bug que aparece durante las pruebas de fallos es la lógica de recuperación que no limpia todo. Supongamos que `myfirst_recover_from_stuck` olvidara borrar `STATUS.BUSY`:

```c
/* BUGGY version: */
static void
myfirst_recover_from_stuck(struct myfirst_softc *sc)
{
        MYFIRST_LOCK_ASSERT(sc);
        if (sc->sim != NULL) {
                sc->sim->command_pending = false;
                callout_stop(&sc->sim->command_callout);
        }
        /* Missing: the STATUS cleanup. */
}
```

La prueba de estrés mostraría:

- Primer timeout: la recuperación se ejecuta, `command_pending` se borra.
- Segundo intento de comando: `STATUS.BUSY` sigue activo del primer intento. El comando no puede emitirse. `cmd_rdy_timeouts` sube.
- Tercer intento: igual.
- ... indefinidamente.

El contador `cmd_successes` deja de incrementarse. `cmd_rdy_timeouts` sube sin límite. Una prueba cuidadosa lo detecta y lo señala. Una prueba descuidada simplemente ve "muchos errores" y lo deja pasar.

La lección es que la inyección de fallos pone al descubierto recuperaciones incompletas. Un driver que funciona sin fallos puede seguir teniendo bugs que solo aparecen cuando ocurren fallos. La prueba de estrés es lo que los saca a la luz.

### Combinación de fallos

El framework del capítulo 17 permite tener varios fallos activos simultáneamente:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0xf    # all four faults
# sysctl dev.myfirst.0.reg_fault_prob_set=1000   # 10%
```

Cada operación tiene un 10% de probabilidad de provocar un fallo. Si lo hace, la simulación elige uno de los fallos habilitados (en la práctica, el código los comprueba en orden: TIMEOUT, ERROR, READ_1S, STUCK_BUSY). La respuesta del driver debe ser robusta frente a todos ellos.

Una prueba aún más exigente:

```text
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # 100%
```

Todas las operaciones fallan. El driver nunca completa un comando con éxito. Todas las rutas de error se ejecutan. Deja el driver funcionando un minuto con esta configuración. Si el driver provoca un kernel panic, hay un bug en una ruta de error. Si el driver tiene fugas de memoria (comprueba con `vmstat -m | grep myfirst`), hay una fuga en una ruta de error. Si el driver entra en deadlock (comprueba con `procstat -kk`), hay un deadlock en una ruta de error.

Ejecutar bajo fallos al 100% durante un periodo sostenido es una prueba estándar para los frameworks de inyección de fallos. Es intencionadamente hostil; un driver que la supera es robusto frente a condiciones de fallo reales.

### Observabilidad para las pruebas de fallos

La observabilidad existente del driver (log de accesos, contadores de estadísticas) cubre la mayor parte de lo que las pruebas de fallos necesitan. Merece la pena añadir tres cosas más.

**Entradas del log de accesos para fallos inyectados.** El log de accesos registra cada acceso a un registro; también debería registrar que se inyectó un fallo. Añade un nuevo tipo de entrada en el log de accesos:

```c
#define MYFIRST_CTX_FAULT   0x10   /* fault injected */
```

Y regístralo cuando se active un fallo:

```c
myfirst_access_log_push(sc, offset, 0, 4, true, MYFIRST_CTX_FAULT);
```

Ahora el log de accesos muestra, intercaladas con los accesos normales, entradas que indican "aquí se inyectó el fallo X". El evaluador puede correlacionar la inyección del fallo con el comportamiento posterior del driver.

**Contadores por tipo de fallo.** En lugar de un único contador `fault_injected`, registra cada tipo:

```c
struct myfirst_stats {
        /* ... existing counters ... */
        uint64_t        fault_timeout;
        uint64_t        fault_read_1s;
        uint64_t        fault_error;
        uint64_t        fault_stuck_busy;
};
```

E incrementa el correspondiente según el fallo:

```c
if (fault & MYFIRST_FAULT_TIMEOUT)
        sc->stats.fault_timeout++;
else if (fault & MYFIRST_FAULT_ERROR)
        sc->stats.fault_error++;
else if (fault & MYFIRST_FAULT_READ_1S)
        sc->stats.fault_read_1s++;
```

El sysctl expone cada contador. El evaluador puede ver, de un vistazo, qué tipos de fallo se han activado y cuáles no.

**Un sysctl de resumen de inyección de fallos.** Un único sysctl que informa de "la máscara de fallos es X, la probabilidad es Y, el total inyectado es Z". Útil para comprobaciones rápidas durante la depuración interactiva:

```c
static int
myfirst_sysctl_fault_summary(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        char buf[128];
        uint32_t mask, prob;
        uint64_t injected;

        MYFIRST_LOCK(sc);
        mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
        injected = sc->stats.fault_injected;
        MYFIRST_UNLOCK(sc);

        snprintf(buf, sizeof(buf),
            "mask=0x%x prob=%u/10000 injected=%ju",
            mask, prob, (uintmax_t)injected);
        return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}
```

Invocación:

```text
# sysctl dev.myfirst.0.fault_summary
dev.myfirst.0.fault_summary: mask=0x5 prob=100/10000 injected=47
```

Este es el tipo de resumen que cabe en una sola línea y cuenta toda la historia de la configuración de fallos actual.

### Inyección de fallos segura

La palabra "segura" necesita matiz. La inyección de fallos en la simulación del capítulo 17 es segura porque la simulación se ejecuta en el mismo kernel que el driver, y los fallos son locales: un `DATA_OUT` corrupto es un valor de cuatro bytes corrompido en memoria asignada con `malloc`, no un descriptor DMA corrompido que sobreescribe RAM aleatoria. Un bit `BUSY` bloqueado es un bit bloqueado en un registro simulado, no una cola de hardware que se acumula hasta colgar el sistema.

La inyección de fallos en hardware real es otra historia. Un dispositivo real con timeout deliberado puede mantener un bloqueo del bus indefinidamente, impidiendo que otros dispositivos avancen. Una transacción DMA deliberadamente corrupta puede escribir en direcciones físicas arbitrarias. La inyección de fallos real en sistemas en producción requiere una configuración cuidadosa, hardware de prueba dedicado y, normalmente, un hipervisor o una emulación de dispositivo virtual.

La simulación del capítulo 17 esquiva todo esto al ejecutarse en memoria. El peor caso de un fallo con mal comportamiento es un kernel panic (si se activa un `KASSERT`) o un deadlock (si el código de recuperación es incorrecto). Ambos son recuperables: un kernel de depuración captura los panics de forma limpia, y un driver en deadlock puede eliminarse reiniciando el sistema. Ningún hardware físico corre riesgo.

Esta seguridad es la razón por la que la inyección de fallos tiene su lugar en la simulación, no solo en las pruebas en producción. Las pruebas en producción detectan los fallos que el hardware real efectivamente produce; las pruebas en simulación detectan los fallos que el hardware real produce a veces y que siempre acaban pasando factura.

### Práctica: La lista de comprobación de la inyección de fallos

Un procedimiento para utilizar la inyección de fallos de forma eficaz durante el desarrollo:

1. Escribe primero el camino de error del driver. Antes de activar el fallo, lee tu código e identifica cómo es la recuperación.
2. Activa el fallo al 100% de probabilidad brevemente. Verifica que el camino de error del driver se ejecuta. Comprueba los contadores.
3. Activa el fallo con probabilidad baja (1-10%). Ejecuta una prueba de estrés. Verifica que el driver sigue progresando en su conjunto.
4. Comprueba `vmstat -m | grep myfirst` antes y después de la prueba de estrés. El uso de memoria no debería crecer.
5. Comprueba la salida de `WITNESS` en busca de advertencias sobre el orden de los locks. Un fallo que provoca un camino inusual puede exponer un bug latente en el orden de adquisición.
6. Comprueba la salida de `INVARIANTS` en busca de fallos en las aserciones. Un fallo que corrompe el estado puede disparar un invariante.
7. Desactiva el fallo. Ejecuta el camino sin errores. Verifica que todo vuelve a la normalidad.
8. Documenta el fallo en `HARDWARE.md` o `SIMULATION.md`.

Este procedimiento convierte la inyección de fallos en una actividad disciplinada, no en un experimento caótico. Cada paso tiene un objetivo claro y valida una propiedad específica del driver.

### Una reflexión: lo que nos enseña la inyección de fallos

La inyección de fallos es un microcosmos de la disciplina más amplia del desarrollo de drivers. El driver debe gestionar errores que no puede evitar, procedentes de fuentes que no controla, en momentos que no puede predecir. La inyección de fallos proporciona al desarrollador una herramienta para ejercitar esas tres propiedades bajo demanda.

Más allá de eso, la inyección de fallos revela la estructura de la filosofía de gestión de errores del driver. Un driver que trata cada error como igualmente recuperable gestiona mal algunos fallos del mundo real. Un driver que distingue entre "transitorio" (vuelve a intentarlo) y "catastrófico" (reinicia el dispositivo) tiene un modelo más claro de lo que está haciendo el hardware. Un driver que registra cada error con suficiente contexto para diagnosticar más adelante es un driver cuyos bugs pueden encontrarse mediante análisis de logs.

El driver del Capítulo 17 no llega a ese nivel de sofisticación: tiene un único camino de gestión de errores que trata todos los errores como transitorios y reintenta solo si el llamador así lo hace. Pero la base está en su lugar. Un ejercicio al final del capítulo invita al lector a extender el driver con gestión por tipo de error, y la simulación tiene los puntos de enganche para ejercitar cada tipo.

### Cerrando la sección 7

La inyección de fallos es la forma en que los autores de drivers ejercitan el código de gestión de errores antes de que lleguen los fallos reales. El framework del Capítulo 17 ofrece cuatro modos de fallo (timeout, read-1s, error, stuck-busy), un campo de probabilidad y suficiente observabilidad para correlacionar los fallos inyectados con el comportamiento del driver.

Los caminos de error del driver se validan activando cada fallo de forma aislada y confirmando los incrementos de contador esperados y la recuperación esperada. Combinar fallos y ejecutar bajo carga con probabilidades bajas ejercita la composición de los caminos de error. Ejecutar al 100% de probabilidad pone a prueba todos los caminos de error simultáneamente.

La etiqueta de versión pasa a ser `1.0-sim-stage4`. El driver ha experimentado ahora todos los errores que está diseñado para gestionar, y el desarrollador ha observado cada camino de error ejecutándose correctamente. El trabajo que queda es de organización: refactorizar, versionar, documentar y enlazar con el Capítulo 18.



## Sección 8: Refactorización y versionado del driver de hardware simulado

La etapa 4 produjo un driver que funciona correctamente tanto en condiciones de operación normal como bajo inyección de fallos. La etapa 5 (Sección 8) hace que el driver sea mantenible. Los cambios son organizativos: límites de archivo bien definidos, documentación actualizada, una nueva versión y la pasada de regresión que verifica que nada se ha roto en el proceso.

Un driver que funciona es valioso. Un driver que funciona y resulta fácil de leer para el próximo colaborador es mucho más valioso. La Sección 8 trata precisamente ese segundo paso.

### La disposición final de los archivos

A lo largo del Capítulo 16, el driver estaba formado por `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sync.h`, `cbuf.c` y `cbuf.h`. El Capítulo 17 añadió `myfirst_sim.c` y `myfirst_sim.h`. El árbol final de archivos:

- `myfirst.c`: el ciclo de vida del driver, los manejadores de syscalls y los primitivos de sincronización de los Capítulos 11 a 15.
- `myfirst_hw.c`: la capa de acceso al hardware del Capítulo 16. Macros `CSR_*`, accesores de registros, log de accesos y sysctls de vista de registros.
- `myfirst_hw.h`: el mapa de registros, las máscaras de bits, los valores fijos y los prototipos de la API de la capa de hardware.
- `myfirst_sim.c`: el backend de simulación del Capítulo 17. Callout del sensor, callout de comandos, callout de ocupado, lógica de inyección de fallos y API de simulación.
- `myfirst_sim.h`: los prototipos de la API de la simulación y la estructura de estado.
- `myfirst_sync.h`: la cabecera de primitivos de sincronización del Capítulo 15.
- `cbuf.c`, `cbuf.h`: el buffer en anillo del Capítulo 10.

Siete archivos fuente, tres cabeceras, un Makefile y tres archivos de documentación (`HARDWARE.md`, `LOCKING.md` y el nuevo `SIMULATION.md`). La división refleja cómo los drivers de producción en FreeBSD se organizan: un archivo por responsabilidad, con interfaces nombradas entre ellos.

### Los campos específicos de la simulación se mueven al struct de simulación

El Capítulo 17 introdujo el estado de la simulación de forma gradual. Al final de la Sección 7, algunos campos relacionados con la simulación estaban en el softc en lugar de en el `struct myfirst_sim`. La etapa 5 lo ordena.

Campos que deben vivir en `struct myfirst_sim`:

- Los tres callouts (`sensor_callout`, `command_callout`, `busy_callout`).
- El estado interno de la simulación (`pending_data`, `pending_fault`, `command_pending`, `running`, `sensor_baseline`, `sensor_tick`, `op_counter`).

Campos que deben permanecer en el softc o en `struct myfirst_hw`:

- El bloque de registros y el estado `bus_space` (`regs_buf`, `regs_tag`, `regs_handle`): en `myfirst_hw`.
- Los timeouts propios del driver (`cmd_timeout_ms`, `rdy_timeout_ms`): en el softc.
- Las estadísticas (`sc->stats`): en el softc (compartidas por los caminos de hw y sim).

El razonamiento: el estado del hardware pertenece a la capa de hardware; el estado de la simulación pertenece a la capa de simulación; el comportamiento visible por el driver pertenece al driver. El Capítulo 18 reemplazará la simulación por un backend PCI real, y el reemplazo más limpio es aquel en el que `myfirst_sim.c` y `myfirst_sim.h` desaparecen por completo, para ser reemplazados por `myfirst_pci.c` y, opcionalmente, `myfirst_pci.h`. Los campos del struct de simulación no necesitan sobrevivir a esa transición; los campos del driver sí.

### El documento `SIMULATION.md`

Un nuevo archivo markdown, `SIMULATION.md`, captura la interfaz de la simulación. Las secciones:

1. **Versión y alcance.** "SIMULATION.md versión 1.0. Capítulo 17 completo."
2. **Qué es la simulación.** Un párrafo: "Un dispositivo simulado respaldado por memoria del kernel que imita un protocolo hardware mínimo de comando-respuesta, utilizado para enseñar el desarrollo de drivers sin necesidad de hardware real."
3. **Qué hace la simulación.** Comportamientos enumerados: autonomía del sensor, ciclo de comandos, inyección de fallos, retardos configurables.
4. **Qué no hace la simulación.** Limitaciones enumeradas: sin DMA, sin interrupciones, sin PCI, sin precisión temporal real.
5. **Mapa de callouts.** Tabla de cada callout, su propósito, su intervalo por defecto y su relación con el lock del driver.
6. **Modos de fallo.** Tabla de cada tipo de fallo, su disparador, su efecto y su recuperación.
7. **Referencia de sysctls.** Tabla de cada sysctl expuesto por la capa de simulación, su tipo, su valor por defecto y su propósito.
8. **Guía de desarrollo.** Cómo añadir un nuevo comportamiento a la simulación (dónde colocar el código, qué disciplina de locking aplica, cómo documentarlo).
9. **Relación con el hardware real.** Cómo los patrones de la simulación se corresponden con patrones en drivers reales de FreeBSD.

El documento tiene aproximadamente entre 150 y 200 líneas de markdown. Es la fuente de verdad única sobre lo que promete la simulación. Un desarrollador que lo lea debería poder razonar sobre la simulación sin leer el código.

Un ejemplo de entrada de la tabla de modos de fallo:

```text
| Fault              | Bit  | Trigger                          | Effect                        | Recovery            |
|--------------------|------|----------------------------------|-------------------------------|---------------------|
| MYFIRST_FAULT_TIMEOUT | 0  | should_fault() returns true      | Command callout not scheduled | driver timeout path |
| MYFIRST_FAULT_READ_1S | 1  | should_fault() returns true      | DATA_OUT = 0xFFFFFFFF          | driver error check  |
| MYFIRST_FAULT_ERROR   | 2  | should_fault() returns true      | STATUS.ERROR set instead of DATA_AV | driver error path |
| MYFIRST_FAULT_STUCK_BUSY | 3 | FAULT_MASK bit set             | STATUS.BUSY continuously latched | clear FAULT_MASK  |
```

Esas tablas hacen que la lectura del código resulte mucho más sencilla: un lector ve `MYFIRST_FAULT_READ_1S` en el código y puede consultar la historia completa en un solo lugar.

### El `HARDWARE.md` actualizado

El `HARDWARE.md` del Capítulo 16 describía un bloque de registros estático. El Capítulo 17 lo amplía con los registros dinámicos y los comportamientos que los impulsan. Las secciones que cambian:

**Mapa de registros.** La tabla incluye ahora los registros del Capítulo 17: `SENSOR` (0x28), `SENSOR_CONFIG` (0x2c), `DELAY_MS` (0x30), `FAULT_MASK` (0x34), `FAULT_PROB` (0x38), `OP_COUNTER` (0x3c). Cada uno con su tipo de acceso, valor por defecto y una descripción de una línea.

**Campos del registro CTRL.** Un bit nuevo: `MYFIRST_CTRL_GO` (bit 9). La tabla añade una fila: "bit 9: `GO`, escribir 1 para disparar un comando; el hardware lo limpia automáticamente".

**Campos del registro STATUS.** La tabla incorpora notas sobre qué bits son dinámicos en el Capítulo 17: `READY` se establece en el attach y permanece activo; `BUSY` lo establece `start_command` y lo limpia `command_cb`; `DATA_AV` lo establece `command_cb` y el driver lo limpia tras leer `DATA_OUT`; `ERROR` lo establece la inyección de fallos y el driver lo limpia.

**Campos de SENSOR_CONFIG.** Una nueva subsección que explica la distribución de dos campos (los 16 bits bajos para el intervalo, los 16 bits altos para la amplitud).

**Campos de FAULT_MASK.** Una nueva subsección que lista cada bit de fallo y su efecto.

**Resumen del comportamiento dinámico.** Una nueva subsección que explica qué cambia de forma autónoma: `SENSOR` (cada 100 ms por defecto), `STATUS.BUSY` (establecido y limpiado por el ciclo de comandos o por el callout `FAULT_STUCK_BUSY`), `STATUS.DATA_AV` (establecido por `command_cb`), `OP_COUNTER` (incrementado en cada comando).

**Disciplina de locking para el comportamiento dinámico.** Una frase: "Todas las actualizaciones dinámicas ocurren bajo `sc->mtx`, que los callouts adquieren automáticamente mediante `callout_init_mtx`. El camino de comandos del driver adquiere el mutex y solo lo libera durante las llamadas a `pause_sbt`, donde pueden ejecutarse los callouts de la simulación."

El documento crece de aproximadamente 80 líneas (versión del Capítulo 16) a aproximadamente 140 líneas. Sigue siendo fácil de hojear. Las nuevas secciones están organizadas de forma que un lector que busque información del Capítulo 17 la encuentre rápidamente.

### El `LOCKING.md` actualizado

El `LOCKING.md` del Capítulo 15 describía un orden de locks de `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` y un orden de detach que drenaba varios primitivos. El Capítulo 16 añadió el detach de la capa de hardware a la lista. El Capítulo 17 añade el detach de la capa de simulación.

El orden de detach actualizado, de más externo a más interno:

1. Destruir `sc->cdev` y esperar a que se cierren los descriptores de archivo abiertos.
2. Detener y drenar todos los callouts a nivel del driver (heartbeat, watchdog, tick_source).
3. Deshabilitar y drenar los callouts de la simulación (sensor, command, busy).
4. Liberar el estado de la simulación.
5. Desconectar la capa de hardware (liberar `regs_buf`, liberar `hw`).
6. Destruir los primitivos de sincronización del driver (mutex, sx, cv, sema).

El paso 3 es nuevo en el Capítulo 17. La simulación se detiene antes de desmontar la capa de hardware, porque los callouts de la simulación acceden al bloque de registros a través de los accesores de la capa de hardware. Liberar el bloque de registros mientras un callout sigue en vuelo produciría un use-after-free.

El orden entre el paso 3 y el paso 2 importa. Los callouts a nivel del driver (heartbeat) pueden leer registros, así que deben drenar antes de que drene la simulación. Un orden deliberado: primero los callouts del driver (son los consumidores más externos), luego los callouts de la simulación (los más internos), y por último los registros en sí.

### La actualización de la versión

En `myfirst.c`:

```c
#define MYFIRST_VERSION "1.0-simulated"
```

La versión pasa a `1.0` porque el Capítulo 17 marca un hito real: el driver se comporta ahora, de extremo a extremo, como un driver contra un dispositivo funcional. El `0.9-mmio` del Capítulo 16 era un capítulo de acceso a registros; el `1.0-simulated` del Capítulo 17 es un capítulo de driver completamente funcional. El salto de 0.9 a 1.0 refleja esto.

El comentario en la parte superior del archivo se actualiza:

```c
/*
 * myfirst: a beginner-friendly device driver tutorial vehicle.
 *
 * Version 1.0-simulated (Chapter 17): adds dynamic simulation of the
 * Chapter 16 register block.  Includes autonomous sensor updates,
 * command-triggered delayed events, read-to-clear semantics, and a
 * fault-injection framework.  Simulation code lives in myfirst_sim.c;
 * the driver sees it only through register accesses.
 *
 * ... (previous version notes preserved) ...
 */
```

El comentario del Capítulo 17 ocupa dos frases. Apunta al nuevo archivo y nombra las nuevas capacidades. Un futuro colaborador que lo lea tendrá suficiente contexto para entender lo que representa la versión.

### La pasada de regresión

El Capítulo 15 estableció la disciplina de regresión: tras cada incremento de versión, se ejecuta la suite completa de pruebas de estrés de todos los capítulos anteriores, se confirma que `WITNESS` permanece en silencio, se confirma que `INVARIANTS` permanece en silencio y se confirma que `kldunload` finaliza correctamente.

Para la Etapa 5 esto significa:

- Las pruebas de concurrencia del Capítulo 11 (múltiples escritores, múltiples lectores) pasan.
- Las pruebas de bloqueo del Capítulo 12 (el lector espera datos, el escritor espera espacio) pasan.
- Las pruebas de callout del Capítulo 13 pasan.
- Las pruebas de taskqueue del Capítulo 14 pasan.
- Las pruebas de coordinación del Capítulo 15 pasan.
- Las pruebas de acceso a registros del Capítulo 16 pasan.
- Las pruebas de simulación del Capítulo 17 (inyección de fallos, temporización, comportamiento) pasan.
- `kldunload myfirst` finaliza correctamente tras la suite completa.

No se omite ninguna prueba. Una regresión en las pruebas de cualquier capítulo anterior es un error, no un asunto pendiente. La disciplina es la misma que ha regido a lo largo de la Parte 3.

Las incorporaciones de pruebas del Capítulo 17 incluyen:

- `sim_sensor_oscillates.sh`: confirma que `SENSOR` varía con el tiempo.
- `sim_command_cycle.sh`: ejecuta una serie de comandos de escritura y verifica que `OP_COUNTER` se incrementa.
- `sim_timeout_fault.sh`: activa el fallo por timeout y verifica que el driver se recupera.
- `sim_error_fault.sh`: activa el fallo de error y verifica que el driver informa de `EIO`.
- `sim_stuck_busy_fault.sh`: activa el fallo de bloqueo activo y verifica que el driver alcanza el timeout esperando a que el dispositivo esté listo.
- `sim_mixed_faults_under_load.sh`: probabilidad de fallo del 10 %, 8 trabajadores en paralelo, 30 segundos.

Cada script tiene unas pocas decenas de líneas. En conjunto añaden aproximadamente 300 líneas de infraestructura de pruebas, una cantidad modesta comparada con el driver en sí, pero significativa en términos de la confianza que aporta.

### Ejecución de la etapa final

```text
# cd examples/part-04/ch17-simulating-hardware/stage5-final
# make clean && make
# kldstat | grep myfirst
# kldload ./myfirst.ko
# kldstat -v | grep -i myfirst

myfirst: version 1.0-simulated

# dmesg | tail -5
# sysctl dev.myfirst.0 | head -40
```

La salida de `kldstat -v` muestra `myfirst` en la versión `1.0-simulated`. El final de `dmesg` refleja el probe y el attach del dispositivo sin ningún error. La salida de `sysctl` lista todos los sysctls del capítulo 11 al 17, incluidos los controles de simulación.

Ejecuta el conjunto de pruebas de estrés:

```text
# ../labs/full_regression.sh
```

Si todas las pruebas pasan, el capítulo 17 está completo.

### Una pequeña regla para la refactorización del capítulo 17

La refactorización del capítulo 16 trataba de separar "la lógica de negocio del driver" de "la mecánica de los registros de hardware". La del capítulo 17 trata de separar "la simulación" del "acceso al hardware". La regla se generaliza: cuando un subsistema adquiere una nueva responsabilidad, dale su propio archivo; cuando un archivo adquiere múltiples responsabilidades, divídelas.

Una regla práctica: un archivo de más de 800 a 1000 líneas suele tener más de una responsabilidad. Un header que exporta más de diez funciones aproximadamente merece una división. Un archivo que importa un header de un subsistema ajeno a su propósito principal suele ser señal de una fuga de responsabilidades.

El archivo `myfirst_sim.c` del capítulo 17 tiene unas 300 líneas en la etapa 5. `myfirst_hw.c` tiene unas 400 líneas. `myfirst.c` tiene unas 800 líneas. Cada archivo mantiene una única responsabilidad. Ningún archivo creció sin control. La división escala bien: el capítulo 18 añadirá `myfirst_pci.c` (unas 200 a 300 líneas), el capítulo 19 añadirá `myfirst_intr.c`, y el capítulo 20 añadirá `myfirst_dma.c`. Cada subsistema vive en su propio archivo. El `myfirst.c` principal se mantiene aproximadamente constante en tamaño; los subsistemas crecen a su alrededor.

### Lo que logró la etapa 5

El driver está ahora en `1.0-simulated`. Comparado con `0.9-mmio`, incorpora:

- Un backend de simulación en `myfirst_sim.c` y `myfirst_sim.h`.
- Un bloque de registros dinámico con actualizaciones autónomas del sensor, eventos retardados activados por comandos e inyección de fallos.
- Timeouts configurables y contadores por tipo de error.
- Un documento `SIMULATION.md` que describe la interfaz y los límites de la simulación.
- Un `HARDWARE.md` actualizado que refleja los nuevos registros y el comportamiento dinámico.
- Un `LOCKING.md` actualizado que refleja el nuevo orden de detach.
- Pruebas de regresión que ejercitan todos los comportamientos del capítulo 17.

El código del driver es reconociblemente FreeBSD. La organización es la que usan los drivers reales cuando tienen responsabilidades distintas de simulación, hardware y driver. El vocabulario es el que comparten los drivers reales. Un colaborador que abre el driver por primera vez encuentra una estructura familiar, lee la documentación y puede navegar por el código por subsistema.

### Drivers reales de FreeBSD que usan los mismos patrones

Los patrones que ejercita este capítulo no se limitan al hardware simulado. Hay tres lugares en el árbol de FreeBSD que merece la pena abrir junto al capítulo 17, porque cada uno usa una forma similar en un subsistema real, y leerlos convierte las técnicas de "cómo escribí la simulación" en "cómo opera realmente el kernel".

El primero es el subsistema **`watchdog(4)`**, en `/usr/src/sys/dev/watchdog/watchdog.c`. La rutina central `wdog_kern_pat()` al inicio de ese archivo es una pequeña máquina de estados impulsada por un "pat" periódico desde el espacio de usuario o desde otro subsistema del kernel; si el pat no llega dentro del timeout configurado, el subsistema dispara un manejador de pretimeout y, en última instancia, un reinicio del sistema. El paralelismo con la simulación del capítulo 17 es directo: un valor de timeout en ticks, un callout que avanza el estado en segundo plano, una superficie ioctl (`WDIOC_SETTIMEOUT`) que cambia el intervalo desde el espacio de usuario, y una superficie sysctl que expone el último timeout configurado para su observación. Además es lo bastante corto como para leerlo de principio a fin, lo cual es raro en un subsistema de producción.

El segundo es **`random_harvestq`**, el camino de recolección de entropía, en `/usr/src/sys/dev/random/random_harvestq.c`. La función `random_harvestq_fast_process_event()` y la disciplina de cola que la rodea son la versión del kernel del patrón "aceptar eventos de muchas fuentes y procesarlos en segundo plano" que este capítulo ejercitó con un sensor simulado. La cola de recolección usa un buffer en anillo, un thread de trabajo y contrapresión explícita cuando los consumidores se quedan atrás, y es uno de los ejemplos más claros del árbol de un subsistema parecido a un driver que nunca debe bloquear los caminos de código que lo alimentan. Leerlo después del capítulo 17 muestra cómo luce el patrón de actualización autónoma cuando el "sensor" es a la vez cada fuente de entropía del sistema.

El tercero, que vale la pena mencionar brevemente, es el **dispositivo pseudoaleatorio** en `/usr/src/sys/dev/random/randomdev.c`. Usa una superficie cdev, sysctls configurables y una separación cuidadosa entre el lado de recolección y el lado de salida. Esa separación es la misma división que el capítulo 17 introdujo entre `myfirst_sim.c` y `myfirst.c`, y observar cómo `randomdev.c` organiza sus archivos es un segundo ejemplo útil de la disciplina que esta sección acaba de aplicar al driver simulado.

Ninguno de estos es un subsistema "de juguete". Son código del kernel en producción que lleva años en funcionamiento. El propósito de estas referencias no es pedirte que los domines ahora, sino marcar dónde viven en el código de producción las técnicas que acabas de practicar en simulación, de modo que cuando más adelante abras `/usr/src/sys` y un archivo te resulte familiar, ya hayas visto el patrón antes.

### Cerrando la sección 8

La refactorización es, una vez más, pequeña en código pero significativa en organización. Una nueva división de archivo, un nuevo documento de documentación, actualizaciones a dos archivos de documentación existentes, un incremento de versión y una pasada de regresión. Cada paso es barato; juntos convierten un driver que funciona en uno que se puede mantener.

El driver del capítulo 17 está terminado. El capítulo cierra con laboratorios, desafíos, solución de problemas y un puente al capítulo 18, donde el bloque de registros simulado se reemplaza por un BAR PCI real.



## Laboratorios prácticos

Los laboratorios del capítulo 17 se centran en ejercitar la simulación desde múltiples ángulos: observar el comportamiento autónomo, ejecutar comandos bajo carga, inyectar fallos y observar las reacciones del driver. Cada laboratorio lleva entre 20 y 60 minutos.

### Laboratorio 1: Observa cómo "respira" el sensor

Carga el driver y observa cómo cambia el valor del sensor sin ninguna actividad del driver.

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.sim_running
dev.myfirst.0.sim_running: 1

# while true; do
    sysctl dev.myfirst.0.reg_sensor
    sleep 0.5
  done
```

Deberías ver el valor oscilando. El valor base por defecto es `0x1000 = 4096`, la amplitud es 64, por lo que el valor oscila entre aproximadamente 4096 y 4160 y viceversa, a lo largo de varios segundos.

Cambia la configuración para acelerar la oscilación:

```text
# sysctl dev.myfirst.0.reg_sensor_config_set=0x01000020
```

Esto fija el intervalo en `0x0100 = 256` ms y la amplitud en `0x0020 = 32`. El sensor se actualiza cada 256 ms con un rango más pequeño. Observa cómo cambia la salida.

Cámbiala de nuevo para ralentizarla drásticamente:

```text
# sysctl dev.myfirst.0.reg_sensor_config_set=0x03e80100
```

Esto fija el intervalo en 1000 ms y la amplitud en 256. El sensor cambia una vez por segundo, con un rango mayor.

Detén la simulación:

```text
# sysctl dev.myfirst.0.sim_running=0   # (requires a writeable sysctl, added in the lab examples)
```

El valor del sensor se congela. Vuelve a habilitarla:

```text
# sysctl dev.myfirst.0.sim_running=1
```

Se reanuda. Este laboratorio ejercita el camino de actualización autónoma de principio a fin.

### Laboratorio 2: Ejecuta un comando individual

Emite un comando manualmente y observa cada cambio en los registros.

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sysctl dev.myfirst.0.reg_delay_ms_set=200

# echo -n "A" > /dev/myfirst0
# sysctl dev.myfirst.0.access_log | head -20
# sysctl dev.myfirst.0.access_log_enabled=0
```

El log debería mostrar:

1. Lecturas de `STATUS` (sondeo para que se limpie BUSY al inicio del comando).
2. Una escritura en `DATA_IN` (el byte 'A' = 0x41).
3. Una lectura de `CTRL` seguida de una escritura en `CTRL` (activación del bit GO).
4. Una escritura en `CTRL` (limpieza del bit GO; la lógica de autolimpiado).
5. Lecturas de `STATUS` (sondeo para DATA_AV).
6. Una eventual transición en la que `DATA_AV` se activa (desde el callout de la simulación).
7. Una lectura de `DATA_OUT` (que devuelve 0x41).
8. Una escritura en `STATUS` (limpieza de DATA_AV).

Intenta relacionar cada entrada con el código fuente en `myfirst_sim.c` y `myfirst_write_cmd`. Si alguna entrada no tiene sentido, tienes un hueco que rellenar.

### Laboratorio 3: Estresar el camino de comandos

Ejecuta muchos comandos concurrentes y verifica la corrección.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=20
# sysctl dev.myfirst.0.cmd_successes     # note value

# for i in 1 2 3 4 5 6 7 8; do
    (for j in $(seq 1 50); do
        echo -n "X" > /dev/myfirst0
     done) &
  done
# wait

# sysctl dev.myfirst.0.cmd_successes     # should have grown by 400
# sysctl dev.myfirst.0.cmd_errors        # should still be 0
# sysctl dev.myfirst.0.reg_op_counter    # should match the growth in cmd_successes
```

Ocho escritores, 50 comandos cada uno, 400 en total. Con 20 ms por comando, la prueba tarda unos 8 segundos (serializada en el lock del driver). `cmd_successes` debería crecer en 400. `cmd_errors` debería permanecer en cero (no hay fallos habilitados). `reg_op_counter` debería coincidir con el incremento en `cmd_successes`.

### Laboratorio 4: Inyectar un fallo de timeout

Habilita el fallo de timeout y observa la recuperación del driver.

```text
# sysctl dev.myfirst.0.cmd_data_timeouts          # note value
# sysctl dev.myfirst.0.cmd_recoveries              # note value

# sysctl dev.myfirst.0.reg_fault_mask_set=0x1     # TIMEOUT bit
# sysctl dev.myfirst.0.reg_fault_prob_set=10000   # 100%

# echo -n "X" > /dev/myfirst0
write: Operation timed out

# sysctl dev.myfirst.0.cmd_data_timeouts          # should have grown by 1
# sysctl dev.myfirst.0.cmd_recoveries              # should have grown by 1

# sysctl dev.myfirst.0.reg_status                 # should be 1 (READY, BUSY cleared by recovery)

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

La primera escritura agota el timeout; el driver se recupera; una escritura posterior tiene éxito. Los contadores confirman la secuencia.

### Laboratorio 5: Inyectar un fallo de error

Ejecuta un lote pequeño con un fallo de error al 25% de probabilidad.

```text
# sysctl dev.myfirst.0.cmd_errors                 # note value

# sysctl dev.myfirst.0.reg_fault_mask_set=0x4     # ERROR bit
# sysctl dev.myfirst.0.reg_fault_prob_set=2500    # 25%

# for i in $(seq 1 40); do
    echo -n "X" > /dev/myfirst0 || echo "error on iteration $i"
  done

# sysctl dev.myfirst.0.cmd_errors                 # should have grown by ~10

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
```

Aproximadamente 10 de cada 40 iteraciones deberían informar de un error. El contador de errores del driver debería reflejar el recuento exactamente. El driver debería seguir siendo utilizable después de la prueba (los comandos posteriores tienen éxito).

### Laboratorio 6: Inyectar BUSY bloqueado y observar la espera del driver

Habilita el fallo de BUSY bloqueado, intenta un comando, observa cómo se agota el timeout y verifica la recuperación.

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x8     # STUCK_BUSY bit
# sleep 0.1                                         # let the busy callout assert

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3                         # READY|BUSY

# sysctl dev.myfirst.0.cmd_rdy_timeouts            # note value

# echo -n "X" > /dev/myfirst0
write: Operation timed out                           # after sc->rdy_timeout_ms

# sysctl dev.myfirst.0.cmd_rdy_timeouts            # should have grown by 1

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sleep 0.2                                          # let the busy callout stop

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1                         # READY only

# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

El fallo bloquea `BUSY`; el driver no puede emitir comandos; el driver agota el timeout en `wait_for_ready`. Limpiar el fallo y esperar un momento permite que `BUSY` se limpie (a través del camino de comandos, si algún comando pasó, o a través del estado natural de no haber ningún comando pendiente). El driver se recupera.

### Laboratorio 7: Fallos mixtos bajo carga

Habilita múltiples fallos con bajas probabilidades, ejecuta una prueba de estrés larga y analiza los resultados.

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x5     # TIMEOUT | ERROR
# sysctl dev.myfirst.0.reg_fault_prob_set=200     # 2%
# sysctl dev.myfirst.0.reg_delay_ms_set=10
# sysctl dev.myfirst.0.cmd_timeout_ms=50

# # Record starting counters.
# sysctl dev.myfirst.0 | grep -E 'cmd_|fault_' > /tmp/before.txt

# # 8 workers, 100 commands each.
# for i in 1 2 3 4 5 6 7 8; do
    (for j in $(seq 1 100); do
        echo -n "X" > /dev/myfirst0 2>/dev/null
    done) &
  done
# wait

# # Record ending counters.
# sysctl dev.myfirst.0 | grep -E 'cmd_|fault_' > /tmp/after.txt
# diff /tmp/before.txt /tmp/after.txt

# # Clean up.
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# sysctl dev.myfirst.0.reg_delay_ms_set=500
# sysctl dev.myfirst.0.cmd_timeout_ms=2000
```

El diff debería mostrar que `cmd_successes` crece en unos 784 (800 comandos menos unos 16 fallos). `fault_injected` debería crecer en unos 16. `cmd_data_timeouts` y `cmd_errors` deberían crecer cada uno en unos 8. Los números exactos varían en cada ejecución (por `arc4random`), pero las proporciones son estables.

Comprueba también: `vmstat -m | grep myfirst` no debería mostrar ningún crecimiento en el uso de memoria entre antes y después; la prueba no debería haber causado fugas.

### Laboratorio 8: Observar actualizaciones del sensor durante carga pesada de comandos

El callout del sensor y el callout de comandos comparten el mismo mutex. Una prueba de carga larga podría privar al sensor de actualizaciones si el lock se mantiene constantemente. Compruébalo.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=5          # very fast commands
# # Start a long load test in the background.
# (for i in $(seq 1 5000); do
    echo -n "X" > /dev/myfirst0 2>/dev/null
  done) &

# # Meanwhile, poll the sensor.
# while kill -0 $! 2>/dev/null; do
    sysctl dev.myfirst.0.reg_sensor
    sleep 0.2
  done
```

Cada valor del sensor sondeado debería diferir del anterior (el callout del sensor está en ejecución). Si los valores se congelan durante la carga (todos idénticos durante muchas lecturas), el lock se está manteniendo durante demasiado tiempo y el sensor está siendo privado de CPU. Un driver correcto ve valores del sensor que se actualizan de forma continua incluso bajo carga pesada de comandos, porque el camino de comandos libera el lock durante `pause_sbt`.

### Laboratorio 9: Construir y ejecutar el módulo hwsim2

Una versión independiente de la simulación del capítulo 17, `hwsim2`, se encuentra en las fuentes de acompañamiento. Compílala y cárgala.

```text
# cd examples/part-04/ch17-simulating-hardware/hwsim2-standalone
# make clean && make
# kldload ./hwsim2.ko

# # The hwsim2 module exposes a single register block with sensor
# # updates and command cycle; no cdev, just sysctls.
# sysctl dev.hwsim2.sensor
# sleep 1
# sysctl dev.hwsim2.sensor                         # different value
# sysctl dev.hwsim2.do_command=0x12345678
# sleep 0.6
# sysctl dev.hwsim2.result                         # 0x12345678

# kldunload hwsim2
```

El módulo `hwsim2` tiene unos 150 líneas de C. Leerlo de una sola vez es una útil consolidación del material del capítulo 17.

### Laboratorio 10: Inyectar un ataque de corrupción de memoria (kernel de depuración)

Un ejercicio deliberado de romper y observar. Modifica el callback del sensor de la simulación para que escriba un byte más allá del final del bloque de registros. Esto debería disparar el `KASSERT` en un kernel de depuración.

En `myfirst_sim.c`, añade temporalmente a `myfirst_sim_sensor_cb`:

```c
/* DELIBERATE BUG for Lab 10. Remove after testing. */
CSR_WRITE_4(sc, 0x80, 0xdead);   /* 0x80 is past the 64-byte region */
```

Reconstruye y carga. El kernel debería entrar en pánico en menos de 100 ms (el callback del sensor se dispara, el KASSERT detecta el acceso fuera de límites en el offset `0x80`, la cadena del pánico nombra dicho offset).

Elimina el bug. Reconstruye. Verifica que el driver funciona de nuevo sin problemas.

Este laboratorio muestra el valor de las aserciones de límites en los accesores de hardware: un acceso fuera de límites se dispara inmediatamente en lugar de corromper silenciosamente memoria no relacionada. El código de producción nunca debería eliminar estas aserciones.



## Ejercicios de desafío

Los desafíos amplían el material del capítulo 17 con trabajo de profundización opcional. Cada uno lleva entre una y cuatro horas y ejercita el juicio, no solo las pulsaciones de teclado.

### Desafío 1: `INTR_STATUS` de lectura con limpiado automático

El `INTR_STATUS` del capítulo 17 sigue siendo de lectura/escritura (RW), no de lectura con limpiado automático (RC). Implementa la semántica RC: cuando el driver lea `INTR_STATUS`, devuelve el valor actual y luego lo limpia. Añade un gancho en el camino de los accesores que reconozca los registros RC y los trate de forma especial. Actualiza `HARDWARE.md`.

Reflexiona sobre: ¿cómo garantiza el driver que la lectura ocurra bajo el lock? ¿Qué ocurre si un sysctl lee `INTR_STATUS` (borrando inadvertidamente el estado que el driver necesitaba)?

### Desafío 2: Añadir una cola de comandos

La simulación actual rechaza comandos solapados. Los dispositivos reales suelen encolarlos. Implementa una pequeña queue de comandos: si llega un comando mientras otro está pendiente, añádelo a la queue; cuando el comando actual termine, inicia el siguiente. Limita la queue a 4 entradas.

Piensa en lo siguiente: ¿cómo espera el driver los comandos que están en la queue? ¿Debería el driver mantener su propia vista de la queue, o depender de la vista de la simulación?

### Desafío 3: Simular una deriva en la frecuencia de muestreo

El callout del sensor se dispara a un intervalo fijo. Los sensores reales suelen derivar: el intervalo varía ligeramente con el tiempo por cambios de temperatura o voltaje. Modifica el callback del sensor para añadir una pequeña perturbación aleatoria a cada intervalo (por ejemplo, ±10%). Observa cómo afecta esto al driver.

Reflexiona sobre: ¿importa la deriva para un driver que solo lee el valor del sensor? ¿Importa para un driver que se preocupa por el instante exacto de cada muestra? (el primer driver no lo hace; uno posterior podría).

### Desafío 4: Un `INTR_STATUS` de escritura-uno-para-limpiar

La semántica W1C es una variante habitual de RC. Implementa W1C para `INTR_STATUS`: escribir un 1 en un bit lo limpia; escribir un 0 no tiene efecto; las lecturas devuelven el valor actual sin efectos secundarios. Contrástalo con la implementación RC del Desafío 1.

Reflexiona sobre: ¿cuál es más conveniente para el driver (leer-para-limpiar o escribir-para-limpiar)? ¿Cuál es más defensivo frente a lecturas accidentales durante la depuración?

### Desafío 5: Recuperación de errores sin reinicio

La ruta de recuperación del driver del Capítulo 17 limpia el estado específico de la simulación directamente. El hardware real no puede hacer esto; el driver debe utilizar únicamente operaciones a nivel de registro. Reescribe `myfirst_recover_from_stuck` para que use únicamente escrituras en registros (por ejemplo, un bit `CTRL.RESET`). Ajusta la simulación para que respete `CTRL.RESET`.

Reflexiona sobre: ¿cómo espera el driver a que el reinicio se complete? ¿Afecta el reinicio a otros registros (por ejemplo, ¿limpia la máscara de fallo)?

### Desafío 6: Un depurador en espacio de usuario

Escribe un pequeño programa en espacio de usuario, `myfirstctl`, que abra `/dev/myfirst0`, envíe una serie de ioctls para controlar la simulación y muestre el estado de los registros. Hazlo interactivo: el usuario puede escribir comandos como `status`, `go X`, `delay 100`, `fault timeout 50%`.

Reflexiona sobre: ¿cómo se comunica el programa en espacio de usuario con el driver (ioctl, sysctl, read/write)? ¿Cuál es una sintaxis de comandos razonable?

### Desafío 7: Inyección de fallos dirigida por DTrace

DTrace puede proporcionar sondas dinámicas. Añade una sonda DTrace al comienzo de `myfirst_sim_start_command` que tome un puntero a `sc` como argumento. Escribe un script D que, basándose en alguna condición especificada por el usuario (por ejemplo, «cada 100 llamadas»), establezca `sc->sim->pending_fault = MYFIRST_FAULT_ERROR`. El script inyecta fallos desde el espacio de usuario sin necesidad del framework de inyección de fallos del kernel.

Reflexiona sobre: ¿puede DTrace modificar el estado del kernel de esta forma de manera segura? ¿Cuáles son los límites de lo que DTrace puede hacer?

### Desafío 8: Simular dos dispositivos

El driver actual tiene una única instancia de dispositivo. Modifícalo para que existan dos archivos `/dev/myfirstN`, cada uno con su propia simulación. Verifica que las operaciones sobre un dispositivo no afectan al otro.

Reflexiona sobre: ¿qué cambia en attach? ¿Cómo funciona el árbol sysctl por dispositivo? ¿Necesita la simulación un estado aleatorio por dispositivo?



## Referencia para resolución de problemas

Una referencia rápida para los problemas que la simulación del Capítulo 17 tiene más probabilidad de producir.

### El sensor no cambia

- `sim_running` es 0. Actívalo con el sysctl escribible.
- El campo de intervalo de `SENSOR_CONFIG` es cero. Ponlo a un valor positivo.
- El callout del sensor no está programado. Confirma que se llamó a `myfirst_sim_enable`. Consulta `dmesg` para ver si hay mensajes de error.
- `sc->sim` es NULL. Confirma que `myfirst_sim_attach` se ejecutó correctamente.

### Los comandos siempre agotan el tiempo de espera

- `DELAY_MS` está configurado con un valor muy alto. Redúcelo o aumenta el timeout del comando.
- El bit `FAULT_TIMEOUT` está activado en `FAULT_MASK`. Límpialo.
- El bit `FAULT_STUCK_BUSY` está activado. Límpialo y espera ~100 ms a que el callout de ocupado deje de reactivarse.
- La simulación está desactivada (`sim_running=0`). Actívala.
- Un comando anterior se quedó bloqueado y no se recuperó. Comprueba `command_pending` (a través del sysctl de simulación) y restablece manualmente si es necesario.

### `WITNESS` advierte sobre el orden de locks

- La ruta de detach está adquiriendo locks en el orden incorrecto. Compara el stack trace con `LOCKING.md`.
- Una nueva ruta de código está tomando `sc->mtx` después de un sleep lock. El orden establecido en el Capítulo 15 debe preservarse.

### Kernel panic al ejecutar `kldunload`

- Los callouts de la simulación no se vaciaron antes del `free`. Comprueba que `callout_drain` se ejecuta antes de `free(sim)`.
- El estado de la simulación se liberó mientras un callout estaba en vuelo. El drenaje es la solución.
- La capa de hardware se liberó antes que la capa de simulación. Debe respetarse el orden del Capítulo 17 (detach de simulación primero, luego detach de hardware).

### El uso de memoria crece con el tiempo

- `vmstat -m | grep myfirst` muestra que el tamaño de asignación del driver aumenta. Esto es una fuga de memoria. Revisa las rutas de error de inyección de fallos; con frecuencia olvidan liberar algo.
- El registro de acceso se asigna dinámicamente en algunas configuraciones. Confirma que se libera en el detach.

### El registro de acceso muestra entradas inesperadas

- Un callout se ejecuta a su propio ritmo; todos los callouts del Capítulo 17 son visibles en el registro. Si el registro muestra callouts disparándose más rápido de lo esperado, comprueba el intervalo.
- Una herramienta en espacio de usuario (`watch -n 0.1 sysctl ...`) está haciendo polling muy rápido. Cada acceso sysctl pasa por los accessors.

### La probabilidad de inyección de fallos no coincide con las observaciones

- `FAULT_PROB` tiene una escala de 0 a 10000. Un valor de 50 es el 0,5%, no el 50%. Para el 50%, usa 5000.
- El fallo solo se aplica a las operaciones que activan `myfirst_sim_should_fault`. No todas las operaciones son candidatas; las actualizaciones del sensor, por ejemplo, no pasan por esa ruta.

### Los cambios en `sim_running` se ignoran

- El manejador sysctl puede que no llame a `myfirst_sim_enable` o `myfirst_sim_disable` bajo el lock. Confirma que el manejador adquiere `sc->mtx` antes de delegar.

### Los comandos tienen éxito pero tardan mucho más de lo esperado

- `cmd_timeout_ms` está configurado con un valor muy alto; cualquier comando que se detenga hace que el presupuesto de timeout sea enorme.
- La máscara de fallos está configurada con `STUCK_BUSY` y un comando exitoso se convierte inesperadamente en un bloqueo de `wait_for_ready` hasta el próximo limpiado de fallo.

### Las pruebas de carga son más lentas de lo esperado

- Los comandos se serializan a través del mutex del driver. N escritores con M comandos cada uno tardan N\*M \* `DELAY_MS` en total.
- Reducir `DELAY_MS` acelera la simulación; reducir `cmd_timeout_ms` no acelera nada a menos que se estén produciendo timeouts realmente.



## Cerrando

El Capítulo 17 comenzó con un bloque de registros estático del Capítulo 16 y cierra con un driver que se comporta, de principio a fin, como un driver que habla con un dispositivo funcional. La simulación es dinámica: un registro de sensor se actualiza por sí solo, los comandos programan completaciones diferidas, los bits de `STATUS` cambian con el tiempo y la inyección de fallos produce fallos controlados que ejercitan las rutas de error del driver. El driver gestiona todo esto mediante el vocabulario de registros que enseñó el Capítulo 16, con la disciplina de sincronización que construyó la Parte 3 y con las primitivas de temporización que introdujo la Sección 6.

Lo que el Capítulo 17 no hizo deliberadamente: PCI real (Capítulo 18), interrupciones reales (Capítulo 19), DMA real (Capítulos 20 y 21). Cada uno de esos temas merece su propio capítulo; cada uno extenderá el driver de maneras específicas mientras reutiliza el framework de simulación donde corresponda.

La versión es `1.0-simulated`. El conjunto de archivos ha crecido: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`. La documentación también ha crecido: `LOCKING.md`, `HARDWARE.md` y el nuevo `SIMULATION.md`. La suite de pruebas ahora ejercita cada comportamiento de la simulación, cada modo de fallo y cada ruta de error.

### Una reflexión antes del Capítulo 18

Un momento de pausa antes del siguiente capítulo. El Capítulo 17 enseñó la simulación como una técnica que va más allá del aprendizaje. Los patrones que practicaste aquí (cambios de estado dirigidos por callout, protocolos de comando-respuesta, inyección de fallos, rutas de recuperación) son patrones que usarás a lo largo de toda tu vida escribiendo drivers. Se aplican tanto al arnés de pruebas de un driver de almacenamiento en producción como a este driver tutorial. La habilidad de simulación es permanente.

El Capítulo 17 también enseñó la disciplina de pensar como hardware. Diseñar el mapa de registros, elegir la semántica de acceso, calcular los tiempos de respuesta, decidir qué capturar y qué limpiar: estas son decisiones que el equipo de hardware toma para los dispositivos reales, y un autor de drivers que las comprende lee los datasheets de forma diferente. La próxima vez que te encuentres con el datasheet de un dispositivo real, notarás las decisiones que tomaron los diseñadores y tendrás un marco para evaluar si esas decisiones fueron acertadas.

El Capítulo 18 cambia el escenario por completo. La simulación desaparece (temporalmente); un dispositivo PCI real ocupa su lugar. El bloque de registros pasa de la memoria de `malloc(9)` a un PCI BAR. Los accessors cambian de `X86_BUS_SPACE_MEM` a `rman_get_bustag` y `rman_get_bushandle`. El código de alto nivel del driver no cambia en absoluto, porque la abstracción del Capítulo 16 es portable a través del cambio. Lo que sí cambia es el comportamiento del dispositivo: un dispositivo virtio real en una VM tiene su propio protocolo, su propia temporización, sus propias peculiaridades. Los patrones que enseñó el Capítulo 17 son los que te permiten manejar todo eso.

### Qué hacer si estás atascado

Algunas sugerencias si el material del Capítulo 17 parece denso.

Primero, vuelve a leer la Sección 3. El patrón callout es la base de cada comportamiento dinámico del capítulo. Si la interacción entre `callout_init_mtx`, `callout_reset_sbt`, `callout_stop` y `callout_drain` resulta opaca, todo lo que viene después también lo será. El Capítulo 13 es la otra buena referencia.

Segundo, ejecuta el Laboratorio 1 y el Laboratorio 2 manualmente, paso a paso. Ver cómo el sensor «respira» y trazar un único comando a través del registro de acceso es donde el comportamiento de la simulación se vuelve concreto.

Tercero, omite los desafíos en el primer repaso. Los laboratorios están calibrados para el Capítulo 17; los desafíos asumen que el material del capítulo ya está bien asentado. Vuelve a ellos después del Capítulo 18 si ahora parecen inalcanzables.

Cuarto, abre `myfirst_sim.c` y lee los tres callouts (`sensor`, `command`, `busy`) en orden. Cada uno es una pieza de código autocontenida que ilustra un aspecto del diseño de la simulación. Si puedes explicar cada uno a un colega (o a un patito de goma), habrás interiorizado el núcleo del Capítulo 17.

El objetivo del Capítulo 17 era dar vida al bloque de registros. Si lo ha conseguido, el resto de la Parte 4 parecerá una progresión natural: el Capítulo 18 cambia la simulación por hardware real, el Capítulo 19 añade interrupciones reales, los Capítulos 20 y 21 añaden DMA. Cada capítulo se construye sobre lo que estableció el Capítulo 17.



## Puente hacia el Capítulo 18

El Capítulo 18 se titula *Escribir un driver PCI*. Su alcance es la ruta de hardware real que el Capítulo 17 no tomó deliberadamente: un driver que sondea un bus PCI, identifica un dispositivo real por su vendor ID y device ID, reclama el BAR del dispositivo como recurso de memoria, lo mapea mediante `bus_alloc_resource_any` y se comunica con la región mapeada a través de los mismos macros `CSR_*` que ya usan los drivers de los Capítulos 16 y 17.

El Capítulo 17 preparó el terreno de cuatro maneras específicas.

Primero, **tienes un driver completo**. El driver del Capítulo 17 en `1.0-simulated` ejercita cada patrón de protocolo común: comando-respuesta, completación diferida, sondeo de estado, recuperación de errores, gestión de timeouts. El Capítulo 18 intercambiará el backend de registros sin cambiar la lógica de alto nivel del driver. El intercambio es un cambio de una sola función en `myfirst_hw_attach`; todo lo demás permanece igual.

Segundo, **tienes un modelo de fallos**. El framework de inyección de fallos del Capítulo 17 enseñó al driver a manejar errores. Los dispositivos PCI reales producen errores reales: timeouts de bus, errores ECC incorregibles, transiciones de estado de energía, caídas de enlace. La disciplina de manejar errores simulados es lo que permitirá al driver del Capítulo 18 manejar errores reales.

Tercero, **tienes un modelo de temporización**. El Capítulo 17 enseñó al lector cuándo usar `DELAY(9)`, `pause_sbt(9)` y `callout_reset_sbt(9)`. Los dispositivos PCI reales tienen sus propios requisitos de temporización, que con frecuencia están documentados en sus datasheets. La disciplina que construyó el Capítulo 17 es lo que permitirá al driver respetar esas temporizaciones en el Capítulo 18.

Cuarto, **tienes el hábito de documentar**. `HARDWARE.md`, `LOCKING.md` y `SIMULATION.md` son tres documentos vivos que los colaboradores del driver mantienen actualizados. El capítulo 18 añadirá un cuarto: `PCI.md` u otro documento similar que describa el dispositivo PCI concreto al que se dirige el driver, su ID de fabricante y de dispositivo, la distribución de sus BAR y cualquier particularidad. El hábito de documentar está establecido; el capítulo 18 lo amplía.

Temas concretos que cubrirá el capítulo 18:

- El subsistema PCI en FreeBSD: `pci(4)`, la tupla bus-device-function, `pciconf -lv`.
- El ciclo de vida de probe y attach: `DRIVER_MODULE`, comparación de ID de fabricante y de dispositivo, los métodos `probe` y `attach`.
- Asignación de recursos: `bus_alloc_resource_any`, la especificación de recurso, `SYS_RES_MEMORY`, `RF_ACTIVE`.
- Mapeo de BAR: cómo el BAR de PCI se convierte en una región `bus_space`; `rman_get_bustag` y `rman_get_bushandle`.
- Habilitación del bus mastering: `pci_enable_busmaster` y cuándo es necesario.
- Inicialización en el momento del attach y limpieza en el detach.
- Pruebas frente a un dispositivo virtio en una VM: el sistema invitado FreeBSD sobre `qemu` o `bhyve`, cómo pasar un dispositivo sintético al invitado, cómo verificar que el driver se adjunta y funciona correctamente.

No necesitas adelantar la lectura. El capítulo 17 es preparación suficiente. Trae tu driver `myfirst` en la versión `1.0-simulated`, tu `LOCKING.md`, tu `HARDWARE.md`, tu `SIMULATION.md`, tu kernel habilitado con `WITNESS` y tu kit de pruebas. El capítulo 18 empieza donde terminó el capítulo 17.

Una breve reflexión de cierre. La parte 3 enseñó el vocabulario de sincronización y produjo un driver que se coordinaba consigo mismo. El capítulo 16 dotó al driver de un vocabulario de registros. El capítulo 17 dio al bloque de registros un comportamiento dinámico y al driver un modelo de fallos. El capítulo 18 retirará la simulación y conectará el driver al silicio real. Cada paso ha construido sobre el anterior; el driver que llega al capítulo 18 está casi listo para producción, y solo le falta el pegamento de bus real que el capítulo 18 proporciona.

La conversación con el hardware se está profundizando. El vocabulario es tuyo; el protocolo es tuyo; la disciplina es tuya. El capítulo 18 añade la última pieza que faltaba.

## Referencia: Hoja de referencia rápida de la simulación del capítulo 17

Un resumen de una página sobre la API de simulación del capítulo 17 y los sysctls que expone, para consulta rápida mientras se programa.

### API de simulación (en `myfirst_sim.h`)

| Función                             | Propósito                                                             |
|-------------------------------------|-----------------------------------------------------------------------|
| `myfirst_sim_attach(sc)`            | Asigna el estado de la simulación, registra los callouts (sin iniciarlos). |
| `myfirst_sim_detach(sc)`            | Drena los callouts, libera el estado de la simulación.                |
| `myfirst_sim_enable(sc)`            | Inicia los callouts. Requiere que `sc->mtx` esté tomado.              |
| `myfirst_sim_disable(sc)`           | Detiene los callouts (sin drenado).                                   |
| `myfirst_sim_start_command(sc)`     | Se activa por escritura en `CTRL.GO`; planifica la finalización.      |
| `myfirst_sim_add_sysctls(sc)`       | Registra los sysctls específicos de la simulación.                    |

### Adiciones de registros

| Offset | Registro         | Acceso | Propósito                                                    |
|--------|------------------|--------|--------------------------------------------------------------|
| 0x28   | `SENSOR`         | RO     | Valor del sensor simulado, actualizado por el callout.        |
| 0x2c   | `SENSOR_CONFIG`  | RW     | Intervalo (16 bits bajos) y amplitud (16 bits altos).         |
| 0x30   | `DELAY_MS`       | RW     | Retardo de procesamiento de comandos en milisegundos.         |
| 0x34   | `FAULT_MASK`     | RW     | Máscara de bits de los tipos de fallo activos.                |
| 0x38   | `FAULT_PROB`     | RW     | Probabilidad de fallo, de 0 a 10000 (10000 = 100%).           |
| 0x3c   | `OP_COUNTER`     | RO     | Contador de comandos procesados.                              |

### Adiciones a CTRL

| Bit | Nombre                   | Propósito                                    |
|-----|--------------------------|----------------------------------------------|
| 9   | `MYFIRST_CTRL_GO`        | Inicia un comando. Se limpia solo.           |

### Modos de fallo

| Bit | Nombre                       | Efecto                                                       |
|-----|------------------------------|--------------------------------------------------------------|
| 0   | `MYFIRST_FAULT_TIMEOUT`    | El comando nunca finaliza.                                    |
| 1   | `MYFIRST_FAULT_READ_1S`    | `DATA_OUT` devuelve `0xFFFFFFFF`.                             |
| 2   | `MYFIRST_FAULT_ERROR`      | `STATUS.ERROR` se activa en lugar de `DATA_AV`.               |
| 3   | `MYFIRST_FAULT_STUCK_BUSY` | `STATUS.BUSY` permanece activo de forma continua.             |

### Nuevos sysctls

| Sysctl                                      | Tipo | Propósito                                                    |
|---------------------------------------------|------|--------------------------------------------------------------|
| `dev.myfirst.0.sim_running`                 | RW   | Activa/desactiva la simulación.                              |
| `dev.myfirst.0.sim_sensor_baseline`         | RW   | Valor base del sensor.                                       |
| `dev.myfirst.0.sim_op_counter_mirror`       | RO   | Reflejo de `OP_COUNTER`.                                     |
| `dev.myfirst.0.reg_delay_ms_set`            | RW   | `DELAY_MS` escribible.                                       |
| `dev.myfirst.0.reg_sensor_config_set`       | RW   | `SENSOR_CONFIG` escribible.                                  |
| `dev.myfirst.0.reg_fault_mask_set`          | RW   | `FAULT_MASK` escribible.                                     |
| `dev.myfirst.0.reg_fault_prob_set`          | RW   | `FAULT_PROB` escribible.                                     |
| `dev.myfirst.0.cmd_timeout_ms`              | RW   | Tiempo de espera para la finalización del comando.           |
| `dev.myfirst.0.rdy_timeout_ms`              | RW   | Tiempo de espera de sondeo de dispositivo listo.             |
| `dev.myfirst.0.cmd_successes`               | RO   | Comandos exitosos.                                           |
| `dev.myfirst.0.cmd_rdy_timeouts`            | RO   | Tiempos de espera en espera de listo.                        |
| `dev.myfirst.0.cmd_data_timeouts`           | RO   | Tiempos de espera en espera de datos.                        |
| `dev.myfirst.0.cmd_errors`                  | RO   | Comandos que reportaron un error.                            |
| `dev.myfirst.0.cmd_recoveries`              | RO   | Invocaciones de recuperación.                                |
| `dev.myfirst.0.fault_injected`              | RO   | Total de fallos inyectados.                                  |



## Referencia: Hoja de referencia rápida de primitivas de temporización

| Primitiva                  | Coste                        | Cancelable | Rango apropiado              |
|----------------------------|------------------------------|------------|------------------------------|
| `DELAY(us)`                | Espera activa, CPU al 100%   | No         | < 100 us                     |
| `pause_sbt(..., sbt, ...)` | Duerme, cede la CPU          | No (no interrumpible en esta forma) | 100 us - segundos |
| `callout_reset_sbt(...)`   | Callback planificado         | Sí         | disparar y olvidar, periódico |
| `cv_timedwait_sbt(...)`    | Duerme en condición          | Sí (mediante cv_signal) | esperas que pueden acortarse |

Contextos en los que cada una es válida:

- `DELAY`: cualquiera (incluyendo filtros de interrupciones, spin mutexes).
- `pause_sbt`: contexto de proceso, sin spin locks tomados.
- `callout_reset_sbt`: cualquiera (el callback se ejecuta en contexto de callout).
- `cv_timedwait_sbt`: contexto de proceso, con mutex específico tomado.



## Referencia: Anatomía de un callout de simulación

Una plantilla para añadir un nuevo comportamiento de simulación, basada en el callout del sensor del capítulo 17.

### Paso 1: Declara el callout en el estado de la simulación

```c
struct myfirst_sim {
        /* ... existing fields ... */
        struct callout   my_new_callout;
        int              my_new_interval_ms;
};
```

### Paso 2: Inicializa el callout en attach

```c
/* In myfirst_sim_attach: */
callout_init_mtx(&sim->my_new_callout, &sc->mtx, 0);
sim->my_new_interval_ms = 200;   /* default */
```

### Paso 3: Escribe el callback

```c
static void
myfirst_sim_my_new_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        /* Do the work: update a register, signal a condition, ...  */
        CSR_UPDATE_4(sc, MYFIRST_REG_SENSOR, 0, 0x100);

        /* Re-arm. */
        callout_reset_sbt(&sim->my_new_callout,
            sim->my_new_interval_ms * SBT_1MS, 0,
            myfirst_sim_my_new_cb, sc, 0);
}
```

### Paso 4: Inicia el callout en enable

```c
/* In myfirst_sim_enable: */
callout_reset_sbt(&sim->my_new_callout,
    sim->my_new_interval_ms * SBT_1MS, 0,
    myfirst_sim_my_new_cb, sc, 0);
```

### Paso 5: Detente en disable, drena en detach

```c
/* In myfirst_sim_disable: */
callout_stop(&sim->my_new_callout);

/* In myfirst_sim_detach, after releasing the lock: */
callout_drain(&sim->my_new_callout);
```

### Paso 6: Documenta en SIMULATION.md

Añade una entrada a la tabla de mapa de callouts:

```text
| Callout           | Interval         | Purpose                       |
|-------------------|------------------|-------------------------------|
| my_new_callout    | my_new_interval_ms | ... explain ...              |
```

Seis pasos. Cada uno es mecánico. El patrón es el mismo para cada comportamiento de simulación; una vez que hayas interiorizado la plantilla, añadir nuevos comportamientos es rápido y fiable.



## Referencia: Inventario de scripts de prueba

Un breve catálogo de los scripts de prueba incluidos en `examples/part-04/ch17-simulating-hardware/labs/`.

| Script                            | Propósito                                                            | Tiempo de ejecución típico |
|-----------------------------------|----------------------------------------------------------------------|----------------------------|
| `sim_sensor_oscillates.sh`        | Confirma que el valor de `SENSOR` cambia con el tiempo.              | ~10 s                      |
| `sim_command_cycle.sh`            | Ejecuta 50 comandos, verifica que `OP_COUNTER` coincide.             | ~30 s                      |
| `sim_timeout_fault.sh`            | Activa el fallo de timeout; verifica la recuperación.                | ~5 s                       |
| `sim_error_fault.sh`              | Activa el fallo de error; verifica que `cmd_errors` crece.           | ~10 s                      |
| `sim_stuck_busy_fault.sh`         | Activa el fallo de ocupado bloqueado; verifica el timeout en espera de listo. | ~10 s             |
| `sim_mixed_faults_under_load.sh`  | 2% de fallos aleatorios, 8 trabajadores, 100 comandos cada uno.      | ~30 s                      |
| `sim_sensor_during_load.sh`       | Verifica actualizaciones del sensor durante carga intensa de comandos. | ~30 s                    |
| `full_regression_ch17.sh`         | Todo lo anterior más la regresión de los capítulos 11-16.            | ~3 minutos                 |

Cada script termina con un estado distinto de cero si ocurrió algo inesperado. Un `full_regression_ch17.sh` exitoso significa que todos los comportamientos del capítulo 17 han sido validados y no se han introducido regresiones.



## Referencia: Un recuento honesto de las simplificaciones del capítulo 17

Un capítulo que enseña una porción de un tema amplio inevitablemente simplifica. En aras de la honestidad con el lector, un catálogo de lo que el capítulo 17 simplificó y cómo es la historia completa.

### La interacción del callout con el mutex

El capítulo 17 utiliza `callout_init_mtx` para que cada callback de la simulación se ejecute con `sc->mtx` tomado. Este es el patrón más simple y seguro, pero no el único. Los drivers reales a veces usan `callout_init` (sin mutex asociado) y adquieren el lock dentro del callback; esto ofrece un control más fino sobre el bloqueo, pero es más propenso a errores. Algunos drivers reales usan `callout_init_rm` (para locks de lectura-escritura) o incluso `callout_init_mtx` con un spin mutex para casos en los que el callback debe ejecutarse en contexto hardirq.

La historia completa: la API de callout de FreeBSD admite varios estilos de lock, cada uno adecuado para un modelo de concurrencia diferente. La elección de `callout_init_mtx` en el capítulo 17 es pedagógicamente limpia; los drivers de producción eligen el estilo que se ajusta a su grafo de bloqueo específico. `/usr/src/sys/dev/e1000/if_em.c` usa `callout_init_mtx` para su callout de tick y `callout_init` para su ruta de sondeo DMA protegida con spinlock, lo que ilustra la diferencia.

### El marco de inyección de fallos

La inyección de fallos del capítulo 17 se limita a cuatro modos, una probabilidad y una simple comprobación `should_fault` al inicio de cada operación. Los marcos de inyección de fallos reales (el propio `fail(9)` de FreeBSD, por ejemplo) admiten un vocabulario más rico: inyección específica por punto de llamada, curvas de probabilidad que cambian con el tiempo, inyección determinista para la N-ésima llamada y rutas de inyección que pueden activarse desde el espacio de usuario por nombre.

La historia completa: el marco `fail(9)` en `/usr/src/sys/kern/kern_fail.c` implementa un sistema de inyección de fallos de calidad de producción. El marco del capítulo 17 es una versión simplificada centrada en las necesidades específicas de la simulación. Un ejercicio de desafío al final del capítulo invita al lector a reemplazar el marco ad-hoc del capítulo por uno basado en `fail(9)`.

### La fuente de números aleatorios

El capítulo 17 usa `arc4random_uniform(10000)` como fuente de decisión para la probabilidad de fallo. Esto es no determinista: dos ejecuciones de la misma prueba producirán patrones de fallo diferentes. Para pruebas reproducibles, el autor de un driver usaría una fuente determinista (un LCG simple sembrado con un valor fijo, o un contador) y expondría la semilla a través de un sysctl.

La historia completa: la inyección de fallos reproducible es importante para las pruebas de regresión. Un driver que supera todos los fallos con la semilla `0x12345` un día y falla con la misma semilla la semana siguiente tiene un error que no existía antes. El no determinismo del capítulo 17 es aceptable para la enseñanza, pero insuficiente para conjuntos serios de pruebas de regresión.

### La cola de comandos

El capítulo 17 permite únicamente un comando en vuelo a la vez. Los dispositivos reales suelen encolar comandos; un driver de red podría tener miles de descriptores en vuelo. La restricción de un único comando simplifica la máquina de estados de la simulación y la gestión de bloqueos del driver, pero limita el rendimiento del driver del tutorial.

La historia completa: un modelo de cola de comandos requiere estado por comando (estado, tiempo de llegada, resultado), una estructura de datos de cola con su propia sincronización y un patrón de driver que gestione la fusión de comandos, la finalización por lotes y la finalización fuera de orden. Los anillos de descriptores DMA del capítulo 20 introducen el patrón para dispositivos basados en DMA; los drivers con cola de comandos reales lo extienden también a contextos no DMA.

### El modelo del sensor

El sensor del capítulo 17 produce un valor en onda triangular mediante una fórmula aritmética simple. Los sensores reales producen valores influenciados por ruido físico, deriva de temperatura, jitter de muestreo y no linealidades específicas del dispositivo. Una simulación más realista incluiría ruido gaussiano, un término de deriva lenta y valores atípicos ocasionales.

La historia completa: el modelado de sensores es una disciplina en sí misma, y las pruebas reales de drivers para dispositivos de sensor suelen utilizar datos reales capturados que se reproducen a través de una capa de simulación. El sensor aritmético del capítulo 17 es pedagógicamente suficiente; una simulación de producción sería sustancialmente más rica.

### La taxonomía de errores

El capítulo 17 distingue cuatro tipos de fallo: timeout, lectura de unos, error y ocupado bloqueado. El hardware real produce una gama mucho más amplia de modos de fallo, incluyendo escrituras parciales, bits de interrupción bloqueados, campos invertidos, valores de registro desfasados en uno y ruido eléctrico transitorio. Los cuatro fallos del capítulo son un punto de partida.

La historia completa: los autores de drivers de FreeBSD que trabajan con familias de dispositivos específicas desarrollan con el tiempo taxonomías de fallos mucho más elaboradas. El código de gestión de errores del driver e1000 tiene varios cientos de líneas propias, en las que se tratan docenas de modos de fallo distintos. El capítulo 17 enseña el patrón; aplicarlo a hardware real produce por necesidad una taxonomía más rica.

### La falta de interrupciones

El driver del Capítulo 17 sondea `STATUS` leyendo el registro en un bucle (con `pause_sbt` entre sondeos). Los drivers reales usan interrupciones: el dispositivo genera una interrupción cuando `STATUS` cambia, el manejador de interrupciones del driver despierta el thread en espera y el thread lee el nuevo estado. El sondeo es ineficiente comparado con las interrupciones.

La historia completa: el Capítulo 19 introduce las interrupciones. El driver del Capítulo 17 recibirá en el Capítulo 19 una ruta de activación impulsada por `INTR_STATUS`, y los bucles de sondeo serán mucho más cortos (despertando una sola vez en lugar de cada milisegundo). El sondeo del Capítulo 17 es un escalón pedagógico.

### La precisión temporal

Los callouts del Capítulo 17 se ejecutan aproximadamente en el intervalo configurado, con una precisión del orden de 1 ms (la resolución de temporizador predeterminada del kernel). Los dispositivos reales a veces requieren temporización por debajo del microsegundo. Un driver que dependa de esa temporización debe usar `DELAY(9)` para esperas cortas y no puede confiar en la precisión del callout.

La historia completa: la temporización de alta precisión en los drivers requiere mecanismos específicos de la plataforma (el TSC, el temporizador ACPI-PM, el HPET o temporizadores específicos del hardware en plataformas embebidas). La simulación a escala de milisegundos del Capítulo 17 no ejercita las rutas de sub-milisegundo; un driver para un dispositivo real con esos requisitos usaría `DELAY` o un mecanismo asistido por hardware.

### Resumen

El Capítulo 17 es una simulación didáctica. Cada simplificación que realiza es deliberada, está nombrada y será retomada por un capítulo posterior o por una extensión hacia un driver real. Los patrones que enseña el Capítulo 17 son los patrones que extiende cada capítulo posterior; la disciplina que construye el Capítulo 17 es la disciplina de la que depende cada capítulo posterior. El capítulo queda intencionalmente por debajo de la historia de simulación completa. Los capítulos siguientes y la práctica en el mundo real completan el resto.



## Referencia: Glosario de términos del Capítulo 17

Un breve glosario de los términos introducidos o utilizados ampliamente en el Capítulo 17.

**Actualización autónoma.** Un cambio en un registro que ocurre sin que el driver lo inicie. En el Capítulo 17, el callout del sensor produce actualizaciones autónomas.

**Ciclo de comando.** La secuencia completa de eventos desde que el driver decide emitir un comando hasta que el comando se completa y el driver observa el resultado. En el Capítulo 17: escribir `DATA_IN`, activar `CTRL.GO`, esperar `STATUS.DATA_AV`, leer `DATA_OUT`.

**Callout.** Una primitiva de FreeBSD que planifica la ejecución de un callback en un momento futuro. El callback se ejecuta en un thread de callout. Se crea con `callout_init_mtx`, se arma con `callout_reset_sbt`, se cancela con `callout_stop` y se drena con `callout_drain`.

**Inyección de fallos.** La introducción deliberada de una condición de fallo en el sistema con fines de prueba. El framework del Capítulo 17 admite cuatro modos de fallo configurables a través de `FAULT_MASK` y `FAULT_PROB`.

**Retenido (latched).** Un bit de registro que, una vez activado, permanece activado hasta que se borra explícitamente. Los bits de error en los dispositivos reales son típicamente retenidos. El `STATUS.ERROR` del Capítulo 17 tiene retención.

**Sondeo.** Leer un registro repetidamente para detectar un cambio. La función `myfirst_wait_for_bit` del Capítulo 17 sondea `STATUS` con una pausa de 1 ms entre lecturas.

**Comando pendiente.** Un comando que se ha iniciado pero que todavía no se ha completado. La simulación del Capítulo 17 rastrea esto con `sim->command_pending`.

**Protocolo.** Las reglas que el driver debe seguir al comunicarse con el dispositivo. En el Capítulo 17: escribir `DATA_IN` antes de `CTRL.GO`; esperar `STATUS.DATA_AV` antes de leer `DATA_OUT`; borrar `DATA_AV` después de leer.

**Lectura que borra (RC).** Una semántica de registro en la que leer el registro devuelve su valor actual y luego lo pone a cero. `INTR_STATUS` en muchos dispositivos reales es RC.

**Backend de simulación.** El código del kernel que proporciona el comportamiento del dispositivo simulado. En el Capítulo 17, este es `myfirst_sim.c`.

**Efecto secundario.** Comportamiento que un acceso a un registro desencadena más allá de la lectura o escritura evidente. Las escrituras en `CTRL.RESET` tienen un efecto secundario (reinician el dispositivo). Las lecturas de `INTR_STATUS` tienen un efecto secundario (borran los bits pendientes, bajo semántica RC).

**Persistente (sticky).** Similar a retenido. Un bit que permanece activado hasta que se borra deliberadamente.

**Timeout.** Una espera acotada que devuelve un error si el evento esperado no ocurre en el tiempo establecido. La función `wait_for_bit` del Capítulo 17 agota el tiempo de espera tras el número de milisegundos configurado.



## Referencia: Resumen del diff del driver del Capítulo 17

Un resumen compacto de los cambios que el Capítulo 17 introduce en el driver, para los lectores que quieran ver de un vistazo cómo evolucionó el driver desde `0.9-mmio` hasta `1.0-simulated`.

### Archivos nuevos

- `myfirst_sim.c` (aproximadamente 300 líneas).
- `myfirst_sim.h` (aproximadamente 50 líneas).
- `SIMULATION.md` (aproximadamente 200 líneas).

### Archivos modificados

- `myfirst.c`: se añadieron las llamadas `myfirst_sim_attach` y `myfirst_sim_detach`; se añadieron los helpers `myfirst_write_cmd`, `myfirst_sample_cmd` y `myfirst_wait_for_bit`; se añadieron campos de timeout al softc; se añadieron contadores de estadísticas.
- `myfirst_hw.h`: se añadieron nuevos desplazamientos de registro (0x28 a 0x3c) y máscaras de bit; se añadió `MYFIRST_CTRL_GO`; se añadieron constantes de fallo.
- `myfirst_hw.c`: sin cambios en el código de acceso; `myfirst_ctrl_update` extendida para interceptar `GO`.
- `HARDWARE.md`: se añadieron los nuevos registros, el nuevo bit `CTRL.GO` y las notas sobre comportamiento dinámico.
- `LOCKING.md`: se añadió el paso de detach de la simulación a la secuencia de detach ordenada.
- `Makefile`: se añadió `myfirst_sim.c` a `SRCS`.

### Evolución del número de líneas

Los tamaños de los archivos en cada etapa (aproximados):

| Etapa                  | myfirst.c | myfirst_hw.c | myfirst_sim.c | Total    |
|------------------------|-----------|--------------|---------------|----------|
| Capítulo 16 Etapa 4 (inicio) | 650        | 380           | (aún no existe)       | 1030      |
| Capítulo 17 Etapa 1     | 680        | 400           | 120           | 1200     |
| Capítulo 17 Etapa 2     | 800        | 400           | 150           | 1350     |
| Capítulo 17 Etapa 3     | 820        | 410           | 180           | 1410     |
| Capítulo 17 Etapa 4     | 840        | 410           | 250           | 1500     |
| Capítulo 17 Etapa 5 (final) | 800       | 400           | 300           | 1500     |

El driver creció de aproximadamente 1030 líneas a 1500 líneas a lo largo del capítulo. Unas 470 líneas de código nuevo, repartidas aproximadamente en 150 en `myfirst.c`, 20 en `myfirst_hw.c` y 300 en el nuevo `myfirst_sim.c`. La refactorización de la Etapa 5 redujo ligeramente `myfirst.c` al trasladar algunos helpers a `myfirst_sim.c`.

### Incorporaciones de comportamiento

- El sensor se actualiza autónomamente cada 100 ms.
- `CTRL.GO` desencadena un comando que se completa tras `DELAY_MS` milisegundos.
- `OP_COUNTER` se incrementa con cada comando.
- `FAULT_MASK` y `FAULT_PROB` controlan los fallos inyectados.
- Cuatro modos de fallo (timeout, lectura de unos, error, busy permanente).
- Contadores de estadísticas por resultado.
- Timeouts configurables para el comando y para la señal de listo.
- Ciclo de comando integrado en las rutas de escritura y lectura.

### Incorporaciones de pruebas

- Ocho nuevos scripts de prueba bajo `labs/`.
- Un `full_regression.sh` actualizado que incluye los nuevos scripts.
- Tiempo de ejecución esperado de la regresión completa: aproximadamente 3 minutos.



## Referencia: Cómo leer un driver impulsado por callouts

Un recorrido guiado por un driver real de FreeBSD cuyo diseño es similar a la simulación del Capítulo 17: un driver cuyo trabajo periódico se planifica mediante callouts y cuya tarea principal es responder a cambios de estado. El driver es `/usr/src/sys/dev/led/led.c`, el driver del pseudo-dispositivo LED.

Ábrelo en un terminal. Sigue leyendo.

### La estructura del driver LED

`led.c` tiene unas 400 líneas. Su función es exponer una interfaz para que otros drivers anuncien un dispositivo LED a través de `/dev/led/NAME`, y para que el espacio de usuario haga parpadear el LED escribiendo patrones. El trabajo real con el hardware se delega en el driver que registró el LED; `led.c` gestiona la planificación y la interpretación de los patrones.

Las estructuras de datos clave son:

- `struct led_softc`: estado por LED, que incluye un `struct callout led_ch`, un `struct sbuf *spec` con el patrón de parpadeo y un puntero a función al callback «establecer estado» del driver.
- `struct mtx led_mtx`: un mutex global que protege la lista de todos los LEDs.
- Una lista enlazada de todos los LEDs.

### El callback del callout

El intérprete del patrón de parpadeo se ejecuta en el callback del callout:

```c
static void
led_timeout(void *p)
{
        struct ledsc *sc = p;
        char c;
        int count;

        if (sc->spec == NULL || sc->ptr == NULL || sc->count == 0) {
                /* no pattern; stop blinking */
                sc->func(sc->private, sc->on);
                return;
        }

        c = *(sc->ptr)++;
        if (c == '.') {
                /* Pattern complete, restart. */
                sc->ptr = sbuf_data(sc->spec);
                c = *(sc->ptr)++;
        }

        if (c >= 'a' && c <= 'j') {
                sc->func(sc->private, 0);   /* LED off */
                count = (c - 'a') + 1;
        } else if (c >= 'A' && c <= 'J') {
                sc->func(sc->private, 1);   /* LED on */
                count = (c - 'A') + 1;
        } else {
                count = 1;
        }

        callout_reset(&sc->led_ch, count * hz / 10, led_timeout, sc);
}
```

(El código está ligeramente abreviado respecto al fuente real para facilitar su presentación.)

### El patrón

Observa la forma del callback.

Primero, comprueba si hay algo que hacer. Si `spec == NULL`, el patrón se ha borrado; la función apaga el LED (para dejarlo en un estado conocido) y retorna sin volver a armar el callout. Este es exactamente el patrón `if (!sim->running) return;` de los callouts del Capítulo 17.

Segundo, avanza por el buffer del patrón carácter a carácter. El lenguaje de patrones usa letras minúsculas de 'a' a 'j' para indicar «LED apagado durante 100 ms a 1000 ms» y mayúsculas de 'A' a 'J' para «LED encendido». Se trata de una pequeña máquina de estados impulsada por el callout.

Tercero, llama a `sc->func(sc->private, state)` para conmutar realmente el LED. Este es el trabajo con el hardware, delegado en el driver que registró el LED. El callback `led_timeout` no sabe si el LED es un pin GPIO, un controlador de LED conectado por I2C o un mensaje a un dispositivo remoto; simplemente llama al puntero de función.

Cuarto, vuelve a armar el callout con un intervalo determinado por el carácter actual del patrón. Este es el patrón `callout_reset_sbt` del Capítulo 17, aunque `led.c` usa el antiguo `callout_reset` con intervalos basados en ticks.

### Lecciones para el Capítulo 17

El driver `led.c` ilustra varios patrones del Capítulo 17:

- Un callout hace avanzar una máquina de estados un paso por invocación.
- El callout se vuelve a armar con un intervalo variable basado en el estado actual.
- Una rama de «no hay trabajo que hacer» retorna sin volver a armar; la ruta de detach depende de esto para drenar finalmente.
- El callback usa un puntero de función para el trabajo delegado, manteniendo la lógica del callout separada del trabajo específico del hardware.

La simulación del Capítulo 17 usa los mismos patrones con una forma ligeramente distinta. El callback del sensor actualiza un registro; el callback del comando desencadena una finalización; el callback de busy vuelve a afirmar un bit de estado. Cada uno es una pequeña máquina de estados que el callout hace avanzar.

### Un ejercicio

Lee `led.c` de principio a fin. Es uno de los drivers más cortos de `/usr/src/sys/dev/` e ilustra un diseño limpio impulsado por callouts. Tras la lectura, escribe un párrafo explicando cómo interactúa `led_timeout` con `led_drvinit`, `led_destroy` y `led_write`. Si puedes articular esa relación, habrás interiorizado el patrón de driver impulsado por callouts.



## Referencia: La diferencia entre «en la simulación» y «en la realidad»

El Capítulo 17 simula un dispositivo. Una pregunta natural es: ¿en qué se diferencia la simulación de un dispositivo real y cuáles son concretamente los cambios que necesitaría el driver cuando se intercambien los dos?

Una breve comparativa.

### Lo que la simulación y la realidad tienen en común

- El mapa de registros: los mismos desplazamientos, los mismos anchos, los mismos tipos de acceso.
- Las macros CSR: `CSR_READ_4`, `CSR_WRITE_4` y `CSR_UPDATE_4` se expanden a las mismas llamadas `bus_space_*`.
- La lógica de ciclo de comando del driver: escribir `DATA_IN`, activar `CTRL.GO`, esperar `DATA_AV`, leer `DATA_OUT`.
- La disciplina de locking: `sc->mtx` protege el acceso a los registros en ambos casos.
- Las estadísticas: `cmd_successes`, `cmd_errors` y demás rastrean eventos reales en cualquier caso.

### Lo que difiere

- El tag y el handle: la simulación usa `X86_BUS_SPACE_MEM` y una dirección de `malloc(9)`; la realidad usa el tag y el handle que devuelven `rman_get_bustag` y `rman_get_bushandle` para un PCI BAR asignado.
- El tiempo de vida del bloque de registros: el de la simulación transcurre desde `myfirst_sim_attach` hasta `myfirst_sim_detach`; el de la realidad transcurre desde `bus_alloc_resource_any` hasta `bus_release_resource`.
- El comportamiento autónomo: la simulación lo produce mediante callouts; la realidad lo produce a través de la lógica interna del dispositivo. El driver no aprecia la diferencia.
- Los modos de fallo: los fallos de la simulación son deliberados y controlados; los fallos de la realidad ocurren cuando ocurren.
- Los tiempos: el `DELAY_MS` de la simulación es un registro; los tiempos de la realidad están determinados por el diseño del dispositivo y no pueden modificarse.
- El auto-borrado de `CTRL.GO`: el de la simulación está implementado en la intercepción de escritura; el de la realidad está implementado en silicio. El driver espera el mismo comportamiento.
- La recuperación de errores: `myfirst_recover_from_stuck` de la simulación manipula directamente el estado de la simulación; la ruta de recuperación de la realidad utiliza únicamente operaciones de registro (normalmente un reset).

La lista es corta. Ese es precisamente el objetivo de la abstracción: la mayor parte del driver es idéntica, y las partes que no lo son están bien localizadas.

### El cambio del capítulo 18

Cuando el capítulo 18 sustituye la simulación por PCI real, los cambios en el driver son:

1. `myfirst_hw_attach` cambia: en lugar de asignar el bloque de registros con `malloc`, llama a `bus_alloc_resource_any` para reservar el PCI BAR.
2. `myfirst_hw_detach` cambia: en lugar de liberar el bloque de registros con `free`, llama a `bus_release_resource` para liberar el BAR.
3. Los archivos de simulación (`myfirst_sim.c`, `myfirst_sim.h`) dejan de compilarse; el Makefile los elimina.
4. La lógica de probe del driver incorpora un método `probe` que coincide con el identificador de fabricante y dispositivo del hardware real.
5. El registro con `DRIVER_MODULE` cambia de un estilo de pseudodispositivo a un estilo de conexión PCI.

Todo lo demás (el ciclo de comandos, las estadísticas, el locking, la documentación) permanece igual. La disciplina que construyó el capítulo 17 es lo que hace esto posible.



## Referencia: cuándo la simulación no es suficiente

Una visión equilibrada de los límites de la simulación. El capítulo 17 enseña la simulación como técnica principal, pero no es un sustituto para todo tipo de pruebas. Varios escenarios requieren hardware real o un dispositivo virtual respaldado por un hipervisor.

### Cuando el error está en la temporización del silicio real

Una condición de carrera que solo se manifiesta con determinadas relaciones de reloj del bus, determinados patrones de tráfico en memoria o determinadas revisiones de firmware del dispositivo no puede reproducirse en simulación. La simulación se ejecuta a la velocidad del thread del kernel, no a la velocidad del hardware, y su temporización la dicta el planificador del kernel, no la estructura del bus.

### Cuando el error está en el código específico de la plataforma

Un driver que necesita gestionar la reasignación del IOMMU, atributos de caché específicos o barreras de memoria propias de la arquitectura no puede validarse completamente en simulación. La memoria del kernel en la simulación tiene un comportamiento de caché diferente al de la memoria real del dispositivo, y los caminos específicos de la arquitectura solo se ejercitan cuando se trabaja con memoria real del dispositivo.

### Cuando el error depende de recursos hardware reales

Un driver que necesita configurar el ancho de enlace PCIe, gestionar estados de energía o interactuar con el firmware del dispositivo no puede validarse en simulación. La simulación no tiene ningún enlace PCIe, ningún estado de energía ni ningún firmware.

### Cuando el error está en el propio test

Un test que ejercita la simulación de una manera que no corresponde a cómo se usa el dispositivo real genera una falsa confianza. La simulación puede pasar el test; el dispositivo real puede seguir fallando en un uso realista. Las pruebas contra la simulación deben complementarse con pruebas contra el hardware real siempre que este esté disponible.

### Qué hacer al respecto

El enfoque correcto es el testing por capas. La simulación detecta errores de protocolo, errores de orden de locks, fallos en la gestión de errores y la mayoría de los errores lógicos. Los dispositivos virtuales respaldados por hipervisor (virtio en bhyve o qemu) detectan errores específicos de PCI, problemas de bus mastering y algunos problemas de temporización. El hardware real en un sistema de laboratorio detecta los errores de último kilómetro que nada más puede reproducir.

En el capítulo 17 el foco está en la simulación. En el capítulo 18 el foco está en PCI respaldado por hipervisor. Los capítulos posteriores sobre DMA e interrupciones introducirán superficies de prueba adicionales. Las pruebas en hardware real sobre un sistema de laboratorio dedicado son una disciplina que el autor de drivers desarrolla con el tiempo; este libro puede darte el vocabulario, pero no el hardware.



## Referencia: lecturas recomendadas

Si quieres profundizar en los temas que tocó el capítulo 17, los siguientes son buenos próximos pasos.

### Páginas del manual de FreeBSD

- `callout(9)`: la API de callout al completo.
- `pause(9)` y `pause_sbt(9)`: las primitivas de espera en detalle.
- `arc4random(9)`: el generador de números pseudoaleatorios.
- `bus_space(9)`: revisítalo con la perspectiva de la simulación.
- `fail(9)`: el framework de inyección de fallos en producción.

### Archivos fuente que merece la pena leer

- `/usr/src/sys/dev/led/led.c`: un driver de pseudodispositivo basado en callouts. Corto, legible e ilustrativo.
- `/usr/src/sys/dev/random/random_harvestq.c`: una cola de recolección basada en callouts. Más compleja que `led.c`, pero con un patrón similar.
- `/usr/src/sys/kern/kern_fail.c`: el framework `fail(9)`. Unas 1500 líneas, pero altamente modular.
- `/usr/src/sys/kern/kern_timeout.c`: la implementación del subsistema de callouts. Léelo cuando quieras entender cómo funciona realmente `callout_reset_sbt`.

### Drivers reales con callouts similares a los del capítulo 17

- `/usr/src/sys/dev/ale/if_ale.c`: `ale_tick` es un callout que comprueba periódicamente el estado del enlace. Patrón similar al callout del sensor del capítulo 17.
- `/usr/src/sys/dev/e1000/if_em.c`: `em_local_timer` es un callout que actualiza estadísticas y gestiona eventos de watchdog. Algo más elaborado que el callout del sensor.
- `/usr/src/sys/dev/iwm/if_iwm.c`: usa múltiples callouts para distintas máquinas de estado de protocolo. Un ejemplo avanzado pero educativo.

### Lecturas relacionadas sobre simulación de hardware

- El capítulo del FreeBSD Handbook sobre emulación (para entender cómo se relacionan virtio y bhyve con la simulación dentro del kernel).
- La página del manual `bhyve(8)`, para conocer los dispositivos virtuales respaldados por hipervisor que serán relevantes en el capítulo 18.



## Referencia: un ejemplo completo: el `myfirst_sim.h` final

El `myfirst_sim.h` completo tal como queda al final del capítulo 17, para consulta rápida. El código fuente también está disponible en `examples/part-04/ch17-simulating-hardware/stage5-final/myfirst_sim.h`.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sim.h -- Chapter 17 simulation API.
 *
 * The simulation layer turns the Chapter 16 static register block into
 * a dynamic device: autonomous sensor updates, command-triggered
 * delayed completions, read-to-clear semantics for INTR_STATUS, and a
 * fault-injection framework. The API is small; most of the simulation
 * is in myfirst_sim.c.
 */

#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

#include <sys/callout.h>
#include <sys/stdbool.h>

struct myfirst_softc;

/*
 * Simulation state. One per driver instance. Allocated in
 * myfirst_sim_attach, freed in myfirst_sim_detach.
 */
struct myfirst_sim {
        /* The three simulation callouts. */
        struct callout       sensor_callout;
        struct callout       command_callout;
        struct callout       busy_callout;

        /* Last scheduled command's data. Saved so command_cb can
         * latch DATA_OUT when it fires. */
        uint32_t             pending_data;

        /* Saved fault state for this command. Set in start_command,
         * consumed by command_cb. */
        uint32_t             pending_fault;

        /* Whether a command is currently in flight. */
        bool                 command_pending;

        /* Baseline sensor value; the sensor callout oscillates
         * around this. */
        uint32_t             sensor_baseline;

        /* Counter used by the sensor oscillation algorithm. */
        uint32_t             sensor_tick;

        /* Local operation counter; mirrors OP_COUNTER register. */
        uint32_t             op_counter;

        /* Whether the simulation callouts are running. Checked by
         * every callout before doing work. */
        bool                 running;
};

/*
 * API. All functions assume sc->sim is valid (that is, 
 * myfirst_sim_attach has been called successfully) unless noted
 * otherwise.
 */

/*
 * Allocate and initialise the simulation state. Registers callouts
 * with sc->mtx. Does not start the callouts; call _enable for that.
 * Returns 0 on success, an errno on failure.
 */
int  myfirst_sim_attach(struct myfirst_softc *sc);

/*
 * Drain all simulation callouts, free the simulation state. Safe to
 * call with sc->sim == NULL. The caller must not hold sc->mtx (this
 * function sleeps in callout_drain).
 */
void myfirst_sim_detach(struct myfirst_softc *sc);

/*
 * Start the simulation callouts. Requires sc->mtx held.
 */
void myfirst_sim_enable(struct myfirst_softc *sc);

/*
 * Stop the simulation callouts. Does not drain (that is _detach's
 * job). Requires sc->mtx held.
 */
void myfirst_sim_disable(struct myfirst_softc *sc);

/*
 * Start a command. Called from the driver when CTRL.GO is written.
 * Reads DATA_IN and DELAY_MS, schedules the command completion
 * callout. Rejects overlapping commands. Requires sc->mtx held.
 */
void myfirst_sim_start_command(struct myfirst_softc *sc);

/*
 * Register simulation-specific sysctls on the driver's sysctl tree.
 * Called from myfirst_attach after sc->sysctl_tree is established.
 */
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
```



## Referencia: comparación con los patrones del capítulo 16

Una comparación directa de dónde el capítulo 17 amplía el capítulo 16 y dónde introduce material genuinamente nuevo.

| Patrón                               | Capítulo 16                         | Capítulo 17                                              |
|--------------------------------------|-------------------------------------|----------------------------------------------------------|
| Acceso a registros                   | `CSR_READ_4`, etc.                  | Misma API, sin cambios                                   |
| Registro de accesos                  | Introducido                         | Reutilizado, ampliado con entradas de inyección de fallos |
| Disciplina de locks                  | `sc->mtx` alrededor de cada acceso  | La misma, más callouts mediante `callout_init_mtx`       |
| Estructura de archivos               | `myfirst_hw.c` añadido              | `myfirst_sim.c` añadido                                  |
| Mapa de registros                    | 10 registros, 40 bytes              | 16 registros, 60 bytes (todos en la misma asignación de 64 bytes) |
| Bits CTRL                            | ENABLE, RESET, MODE, LOOPBACK       | Los mismos, más GO (bit 9)                               |
| Bits STATUS                          | READY, BUSY, ERROR, DATA_AV         | Los mismos, pero modificados dinámicamente por los callouts |
| Callouts                             | Uno (reg_ticker_task como tarea)    | Tres (sensor, command, busy)                             |
| Timeouts                             | No aplicable                        | Introducidos (cmd_timeout_ms, rdy_timeout_ms)            |
| Recuperación de errores              | Mínima                              | Ruta de recuperación completa                            |
| Estadísticas                         | Ninguna                             | Contadores por resultado                                 |
| Inyección de fallos                  | Ninguna                             | Cuatro modos de fallo                                    |
| HARDWARE.md                          | Introducido                         | Ampliado                                                 |
| LOCKING.md                           | Ampliado desde el cap. 15           | Ampliado                                                 |
| SIMULATION.md                        | No presente                         | Introducido                                              |

El capítulo 17 amplía el capítulo 16 sin romper nada. Cada capacidad del capítulo 16 se conserva; cada nueva capacidad se añade. El driver en `1.0-simulated` es un superconjunto estricto del driver en `0.9-mmio`.



## Referencia: una nota final sobre la filosofía de la simulación

Un párrafo con el que cerrar el capítulo, que merece la pena releer una vez que hayas terminado los laboratorios.

La simulación es, en esencia, un acto de modelado. Tomas un sistema que no controlas completamente (un dispositivo real) y construyes un sistema más pequeño que sí controlas (una simulación) que se comporta de manera similar. La simulación nunca es perfecta. Su valor no proviene de reproducir el sistema real en cada detalle, sino de preservar las propiedades que importan para la pregunta que estás haciendo.

En el capítulo 17, la propiedad que importa es la corrección del protocolo: ¿gestiona el driver el ciclo de comandos, la temporización, los casos de error, la recuperación? Una simulación que preserva la corrección del protocolo es una simulación que justifica su existencia, aunque no acierte en cada detalle de temporización.

En capítulos posteriores y en tu propio trabajo futuro, las propiedades que importen pueden ser diferentes. Un test de rendimiento necesita una simulación que preserve el comportamiento del throughput. Un test de energía necesita una simulación que preserve los estados de inactividad y actividad. Un test de seguridad necesita una simulación que pueda producir entradas adversariales.

La habilidad que enseña el capítulo 17 no es «cómo simular este dispositivo en particular». Es «cómo identificar qué debe preservar una simulación y cómo construir una que lo preserve». Esa habilidad es transferible, y es la que te servirá en cada driver que escribas.
