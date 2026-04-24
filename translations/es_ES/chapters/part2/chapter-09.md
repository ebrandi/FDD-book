---
title: "Lectura y escritura en dispositivos"
description: "Cómo d_read y d_write mueven bytes de forma segura entre el espacio de usuario y el kernel a través de uio y uiomove."
partNumber: 2
partName: "Building Your First Driver"
chapter: 9
lastUpdated: "2026-04-17"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "es-ES"
---
# Lectura y escritura en dispositivos

## Orientación y objetivos del lector

El capítulo 7 te enseñó a levantar un driver. El capítulo 8 te enseñó cómo ese driver se presenta al espacio de usuario a través de `/dev`. El driver con el que terminaste el capítulo anterior se adjunta como un dispositivo Newbus, crea `/dev/myfirst/0`, lleva un alias en `/dev/myfirst`, asigna estado por apertura, registra mensajes de forma limpia y se desconecta sin fugas de memoria. Cada una de esas piezas era importante, pero ninguna movía un solo byte.

Este capítulo es donde los bytes empiezan a moverse.

Cuando un programa de usuario llama a `read(2)` o `write(2)` sobre uno de tus nodos de dispositivo, el kernel tiene que transferir datos reales entre el espacio de direcciones del usuario y la memoria del driver. Esa transferencia no es un simple `memcpy`. Cruza un límite de confianza. El puntero al buffer que pasó el usuario podría ser inválido. Es posible que el buffer no esté completamente residente en memoria. La longitud podría ser cero, o enorme, o parte de una lista scatter-gather. El usuario podría estar en un jail, podría tener una señal pendiente, podría estar leyendo con `O_NONBLOCK`, podría haber redirigido el resultado a través de un pipe. Tu driver no necesita entender cada uno de esos casos por separado, pero sí necesita cooperar con la única abstracción del kernel que los resuelve todos. Esa abstracción es `struct uio`, y la herramienta principal para usarla es `uiomove(9)`.

Este capítulo es donde finalmente implementamos las entradas `d_read` y `d_write` que el capítulo 7 dejó como stubs. Por el camino, examinaremos con cuidado cómo describe el kernel una petición de E/S, por qué el libro lleva diciéndote desde el capítulo 5 que no toques directamente los punteros de usuario, y cómo dar forma a un driver para que las transferencias parciales, los buffers no alineados, las lecturas interrumpidas por señales y las escrituras cortas se comporten como lo haría un archivo UNIX clásico.

### Por qué este capítulo merece su propio espacio

Podría haber sido tentador escribir un capítulo breve que simplemente dijera "llama a `uiomove`" y siguiera adelante. Eso dejaría al lector con un driver que pasa la prueba más sencilla y luego falla de veinte formas sutiles. La razón por la que este capítulo tiene la extensión que tiene es que la E/S es donde los drivers de principiantes fallan con más frecuencia, y los lugares donde fallan no son donde el código parece arriesgado. Los errores suelen estar en los valores de retorno, en el manejo de `uio_resid`, en el tratamiento de una transferencia de longitud cero, en lo que ocurre cuando el driver se despierta de `msleep(9)` porque el proceso fue terminado, en el sentido en que una lectura parcial debería ir vaciando el buffer.

Un driver que se equivoca en estos detalles compila sin errores, pasa un simple `cat /dev/myfirst` y luego produce datos corruptos cuando un programa real empieza a enviar bytes a través de él. Ese es el tipo de error que consume días. El objetivo de este capítulo es detener esa clase de error desde la raíz.

### El estado del driver al final del capítulo 8

Al final del capítulo 8, el driver `myfirst` tenía la siguiente forma. Vale la pena hacer un repaso porque el capítulo 9 construye directamente sobre él:

- Un único hijo Newbus, creado en `device_identify`, registrado bajo `nexus0`.
- Un `struct myfirst_softc` asignado por Newbus e inicializado en `attach`.
- Un mutex con el nombre del dispositivo, usado para proteger los contadores del softc.
- Un árbol sysctl bajo `dev.myfirst.0.stats` que expone `attach_ticks`, `open_count`, `active_fhs` y `bytes_read`.
- Un cdev principal en `/dev/myfirst/0` con propietario `root:operator` y modo `0660`.
- Un cdev alias en `/dev/myfirst` que apunta al principal.
- Un `struct myfirst_fh` asignado por cada `open(2)`, registrado mediante `devfs_set_cdevpriv(9)` y liberado por un destructor que se ejecuta exactamente una vez por descriptor.
- Manejadores stub de `d_read` y `d_write` que recuperan el estado por apertura, opcionalmente lo consultan y retornan de inmediato: `d_read` devuelve cero bytes (EOF), `d_write` afirma haber consumido todos los bytes estableciendo `uio_resid = 0`.

El capítulo 9 convierte esos stubs en implementaciones reales. La forma exterior del driver no cambia mucho. Un lector nuevo seguirá viendo `/dev/myfirst/0`, seguirá viendo el alias, seguirá viendo los sysctls. Lo que cambia es que un `cat /dev/myfirst/0` producirá ahora una salida, un `echo hello > /dev/myfirst/0` almacenará ahora el texto en la memoria del driver, y un segundo `cat` leerá exactamente lo que depositó la primera escritura. Al final del capítulo, tu driver será un pequeño buffer en memoria, disciplinado, a través del cual podrás hacer pasar bytes y recuperarlos. Aún no será un buffer circular con lecturas bloqueantes; ese es el trabajo del capítulo 10. Será un driver que mueve bytes correctamente.

### Lo que aprenderás

Tras terminar este capítulo podrás:

- Explicar cómo `read(2)` y `write(2)` fluyen desde el espacio de usuario a través de devfs hasta los manejadores de tu `cdevsw`.
- Leer y escribir los campos de `struct uio` sin necesidad de memorizarlos.
- Usar `uiomove(9)` para transferir bytes entre un buffer del kernel y el buffer del llamante en cualquier dirección.
- Usar `uiomove_frombuf(9)` cuando el buffer del kernel tiene un tamaño fijo y quieres una contabilidad automática del desplazamiento.
- Decidir cuándo recurrir a `copyin(9)` o `copyout(9)` en lugar de `uiomove(9)`.
- Devolver recuentos de bytes correctos para transferencias cortas, transferencias vacías, fin de archivo y lecturas interrumpidas.
- Elegir un valor de errno adecuado para cada camino de error que puede tomar la lectura o escritura de un driver.
- Diseñar un buffer interno que el driver rellena desde `d_write` y vacía desde `d_read`.
- Identificar y corregir los errores más comunes en `d_read` y `d_write`.
- Ejercitar el driver con las herramientas del sistema base (`cat`, `echo`, `dd`, `od`, `hexdump`) y con un pequeño programa en C.

### Qué construirás

Llevarás el driver `myfirst` del final del capítulo 8 a través de tres etapas incrementales.

1. **Etapa 1, un lector de mensajes estáticos.** `d_read` devuelve el contenido de una cadena fija en el espacio del kernel. Cada apertura comienza en el desplazamiento cero y va leyendo el mensaje hasta el final. Es el "hola mundo" de las lecturas de dispositivo, pero hecho con un manejo correcto del desplazamiento.
2. **Etapa 2, un buffer de escritura única y lectura múltiple.** El driver posee un buffer de tamaño fijo en el kernel. `d_write` añade datos al final. `d_read` devuelve lo que se ha escrito hasta el momento, a partir de un desplazamiento por descriptor que recuerda hasta dónde ha consumido cada lector. Dos lectores concurrentes siguen viendo su propio progreso de forma independiente.
3. **Etapa 3, un pequeño driver eco.** El mismo buffer, ahora utilizado como almacén de tipo primero en entrar, primero en salir. Cada `write(2)` añade bytes al final. Cada `read(2)` extrae bytes del inicio. Un script de prueba con dos procesos escribe en un terminal y lee los datos del eco en otro. Este es el punto de traspaso al capítulo 10, donde reconstruiremos el mismo driver sobre un verdadero buffer circular, añadiremos soporte de E/S parcial y no bloqueante, y conectaremos `poll(2)` y `kqueue(9)`.

Las tres etapas compilan, se cargan y se comportan de forma predecible. Ejercitarás cada una desde `cat`, `echo` y un pequeño programa de espacio de usuario llamado `rw_myfirst.c` que ejercita los casos extremos que `cat` no alcanza por sí solo.

### Lo que este capítulo no cubre

Varios temas tocan `read` y `write` pero se posponen deliberadamente:

- **Buffers circulares y wrap-around**: El capítulo 10 implementa un verdadero buffer en anillo. La etapa 3 usa aquí un buffer lineal simple para poder mantener el foco en el propio camino de E/S.
- **Lecturas bloqueantes y `poll(2)`**: El capítulo 10 presenta el bloqueo basado en `msleep(9)` y el manejador `d_poll`. Este capítulo mantiene todas las lecturas en modo no bloqueante a nivel del driver; un buffer vacío produce una lectura inmediata de cero bytes.
- **`ioctl(2)`**: El capítulo 25 desarrolla `d_ioctl`. Solo lo mencionamos donde el lector necesita entender por qué ciertos caminos de control pertenecen ahí y no en `write`.
- **Registros de hardware y DMA**: La parte 4 se ocupa de los recursos de bus, `bus_space(9)` y DMA. La memoria que leemos y escribimos en este capítulo es memoria ordinaria del heap del kernel, asignada con `malloc(9)` desde `M_DEVBUF`.
- **Corrección de la concurrencia bajo carga**: La parte 3 está dedicada a las condiciones de carrera, el locking y la verificación. Tomamos las precauciones necesarias con mutexes donde una condición de carrera corrompería el buffer de la etapa 3, pero el análisis más profundo queda aplazado.

Mantenerse dentro de estos límites es lo que hace que el capítulo sea honesto. Un capítulo para principiantes que se adentra en `ioctl`, DMA y `kqueue` es un capítulo que no enseña ninguno de ellos bien.

### Tiempo estimado de dedicación

- **Solo lectura**: aproximadamente una hora.
- **Lectura más escritura de las tres etapas**: alrededor de tres horas, incluyendo un par de ciclos de carga y descarga por etapa.
- **Lectura más todos los laboratorios y desafíos**: de cinco a siete horas en dos o tres sesiones.

Empieza con una máquina de laboratorio recién arrancada. No te apresures. Las etapas son pequeñas a propósito, y el valor real proviene de observar `dmesg`, observar `sysctl` y probar el dispositivo desde el espacio de usuario después de cada cambio.

### Requisitos previos

Antes de comenzar este capítulo, confirma lo siguiente:

- Tienes un driver `myfirst` funcional equivalente al código fuente de la etapa 2 del capítulo 8, que se encuentra en `examples/part-02/ch08-working-with-device-files/stage2-perhandle/`. Si aún no has llegado al final del capítulo 8, detente aquí y vuelve después.
- Tu máquina de laboratorio ejecuta FreeBSD 14.3 con un `/usr/src` coincidente.
- Has estudiado la discusión del capítulo 4 sobre punteros, estructuras y disposición de memoria, así como la del capítulo 5 sobre los modismos y la seguridad en el espacio del kernel.
- Entiendes qué es un `struct cdev` y cómo se relaciona con un `cdevsw`. El capítulo 8 cubrió esto con detalle.

Si tienes dudas sobre alguno de estos puntos, el resto del capítulo será más difícil de lo necesario. Revisa primero las secciones relevantes.

### Cómo sacar el máximo partido a este capítulo

Tres hábitos dan resultados inmediatos.

En primer lugar, mantén `/usr/src/sys/dev/null/null.c` abierto en un segundo terminal. Es el ejemplo más corto, más limpio y más legible de `d_read` y `d_write` que existe en el árbol. Cada idea que introduce este capítulo aparece en algún lugar de `null.c` en cincuenta líneas o menos. Los drivers reales de FreeBSD son el libro de texto; este libro es la guía de lectura.

En segundo lugar, mantén `/usr/src/sys/sys/uio.h` y `/usr/src/sys/sys/_uio.h` abiertos. Las declaraciones que contienen son breves y estables. Léelas una vez ahora, para que cuando el capítulo haga referencia a `uio_iov`, `uio_iovcnt`, `uio_offset` y `uio_resid` no tengas que confiar únicamente en la prosa.

En tercer lugar, reconstruye entre cambios y confirma el comportamiento desde el espacio de usuario antes del siguiente cambio. Este es el hábito que separa escribir drivers de escribir prosa sobre drivers. Ejecutarás `cat`, `echo`, `dd`, `stat`, `sysctl`, `dmesg` y un pequeño programa en C en cada punto de control. No los omitas. Los modos de fallo que este capítulo te enseña a reconocer solo se hacen visibles cuando ejecutas el código.

### Hoja de ruta del capítulo

Las secciones en orden son:

1. Un mapa visual del camino completo de E/S, desde `read(2)` en el espacio de usuario hasta `uiomove(9)` dentro de tu handler.
2. Un breve repaso sobre qué significan `read` y `write` en UNIX, y qué significan concretamente para quien escribe drivers.
3. La anatomía de `d_read`: su firma, qué se le pide que haga y qué se le pide que devuelva.
4. La anatomía de `d_write`: el espejo de `d_read`, más algunos detalles que solo se aplican en la dirección de escritura.
5. Un protocolo de lectura para handlers desconocidos, seguido de un segundo recorrido por un driver real (`mem(4)`) para mostrar una forma diferente.
6. El argumento `ioflag`: de dónde procede, qué bits importan y por qué el capítulo 9 lo ignora en su mayor parte.
7. Un análisis detallado de `struct uio`, el objeto de descripción de E/S del kernel, campo por campo, con tres instantáneas del mismo uio a lo largo de una llamada.
8. `uiomove(9)` y sus funciones compañeras, las funciones que realmente mueven los bytes.
9. `copyin(9)` y `copyout(9)`: cuándo recurrir a ellas y cuándo dejarlas de lado en favor de `uiomove`. Más un caso de estudio preventivo sobre datos estructurados.
10. Buffers internos: estáticos, dinámicos y de tamaño fijo. Cómo elegir uno, cómo gestionarlo de forma segura y los helpers del kernel que debes reconocer.
11. Gestión de errores: los valores de errno que importan para E/S, cómo señalar el fin de archivo y cómo razonar sobre las transferencias parciales.
12. La implementación de `myfirst` en tres etapas, con el código fuente del driver incluido.
13. Un recorrido paso a paso de `read(2)` desde el espacio de usuario, a través del kernel, hasta tu handler, más un recorrido de escritura en espejo.
14. Un flujo de trabajo práctico para las pruebas: `cat`, `echo`, `dd`, `truss`, `ktrace` y la disciplina que los convierte en un ritmo de desarrollo.
15. Observabilidad: sysctl, dmesg y `vmstat -m`, con una instantánea concreta del driver bajo carga ligera.
16. Signed, unsigned y los peligros del off-by-one, una sección breve pero de gran valor.
17. Notas de resolución de problemas para los errores que el material de este capítulo tiene más probabilidades de generar, y una tabla de contraste con patrones de handler correctos frente a los defectuosos.
18. Laboratorios prácticos (siete) que te guían por cada etapa y consolidan el flujo de trabajo de observabilidad.
19. Ejercicios de desafío (ocho) que amplían los patrones.
20. Cerrando el capítulo y un puente hacia el capítulo 10.

Si es la primera vez que recorres este capítulo, léelo de forma lineal y realiza los laboratorios a medida que los encuentres. Si vuelves al material para afianzarlo, las secciones de consulta al final funcionan de forma independiente.

## Mapa visual del camino de E/S

Antes de que el texto se adentre en los detalles, conviene tener una imagen en mente. El diagrama que aparece a continuación muestra el camino que sigue una llamada `read(2)` desde un programa de usuario hasta tu driver, y de vuelta al llamador. Cada recuadro es código real del kernel que puedes encontrar bajo `/usr/src/sys/`. Cada flecha es una llamada a función real. Nada de esto es metafórico.

```text
                         user space
      +----------------------------------------------+
      |   user program                               |
      |                                              |
      |     n = read(fd, buf, 1024);                 |
      |            |                                 |
      |            v                                 |
      |     libc read() wrapper                      |
      |     (syscall trap instruction)               |
      +-------------|--------------------------------+
                    |
     ==============| kernel trust boundary |===============
                    |
                    v
      +----------------------------------------------+
      |  sys_read()                                   |
      |  /usr/src/sys/kern/sys_generic.c              |
      |  - lookup fd in file table                    |
      |  - fget(fd) -> struct file *                  |
      |  - build a uio around buf, count              |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  struct file ops -> vn_read                   |
      |  /usr/src/sys/kern/vfs_vnops.c                |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  devfs_read_f()                               |
      |  /usr/src/sys/fs/devfs/devfs_vnops.c          |
      |  - devfs_fp_check -> cdev + cdevsw            |
      |  - acquire thread-count ref                   |
      |  - compose ioflag from f_flag                 |
      |  - call cdevsw->d_read(dev, uio, ioflag)      |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  YOUR HANDLER (myfirst_read)                  |
      |  - devfs_get_cdevpriv(&fh)                    |
      |  - verify is_attached                         |
      |  - call uiomove(9) to transfer bytes          |
      |            |                                  |
      |            v                                  |
      |     +-----------------------------------+     |
      |     |  uiomove_faultflag()              |     |
      |     |  /usr/src/sys/kern/subr_uio.c     |     |
      |     |  - for each iovec entry           |     |
      |     |    copyout(kaddr, uaddr, n)  ===> |====|====> user's buf
      |     |    decrement uio_resid            |     |
      |     |    advance uio_offset             |     |
      |     +-----------------------------------+     |
      |  - return 0 or an errno                       |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  devfs_read_f continues                       |
      |  - release thread-count ref                   |
      |  - update atime if bytes moved                |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  sys_read finalises                           |
      |  - compute count = orig_resid - uio_resid     |
      |  - return to userland                         |
      +-------------|--------------------------------+
                    |
     ==============| kernel trust boundary |===============
                    |
                    v
      +----------------------------------------------+
      |   user program sees the return value         |
      |   in n                                        |
      +----------------------------------------------+
```

Vale la pena fijar algunas características del diagrama porque reaparecen a lo largo del capítulo.

**El límite de confianza se cruza exactamente dos veces.** Una vez en el camino de bajada (el usuario entra al kernel a través de una trampa de syscall) y una vez en el camino de subida (el kernel devuelve el control al espacio de usuario). Todo lo que hay entre medias es ejecución exclusiva del kernel. Tu handler se ejecuta completamente dentro del kernel, en una pila del kernel, con los registros del usuario guardados.

**Tu handler es el único lugar donde el conocimiento del driver entra en el camino.** Todo lo que hay por encima es maquinaria del kernel que funciona de forma idéntica para todos los dispositivos de caracteres del árbol. Todo lo que hay por debajo es `uiomove` y `copyout`, también maquinaria del kernel. Tu handler es la única función donde se calcula la respuesta a «¿qué bytes debe producir esta lectura?».

**El buffer del usuario nunca es accedido directamente por tu driver.** Lo accede `copyout` desde dentro de `uiomove`. Tu driver le entrega a `uiomove` un puntero del kernel, y `uiomove` es el único código que desreferencia el puntero del usuario en tu nombre. Esta es la forma que adopta el límite de confianza expresada como código: la memoria del usuario solo se accede a través de la única API que sabe hacerlo de forma segura.

**Cada paso tiene un paso correspondiente en el camino de vuelta.** La referencia al contador de threads adquirida por devfs se libera después de que tu handler retorna; el estado del uio se inspecciona para calcular el número de bytes; el control se desenrolla a través de cada capa y regresa al espacio de usuario. Comprender esta simetría es lo que hace que el conteo de referencias parezca algo natural en lugar de arbitrario.

Imprime este diagrama o dibújalo en papel. Cuando leas un driver desconocido más adelante en el libro, vuelve a consultarlo. Cada `d_read` o `d_write` que estudies se encuentra exactamente en este punto de la cadena de llamadas. Las diferencias entre drivers están en el handler; el camino alrededor del handler es constante.

Para `d_write`, la imagen es el reflejo especular. `devfs_write_f` despacha a `cdevsw->d_write`, tu handler llama a `uiomove(9)` en la dirección contraria, `uiomove` llama a `copyin` en lugar de `copyout`, y el kernel se desenrolla de vuelta a `write(2)`. Cada flecha del diagrama tiene su gemela; cada propiedad enumerada arriba se aplica también a las escrituras.



## Los dispositivos en UNIX: un repaso rápido

Vale la pena dedicar diez minutos a repasar antes de ponerse a escribir código. El capítulo 6 presentó el modelo de E/S de UNIX a nivel conceptual; el capítulo 7 lo puso en práctica; el capítulo 8 dio un aspecto ordenado a la superficie del archivo de dispositivo. Los tres tratamientos tenían razones para no detenerse en el comportamiento de `read(2)` y `write(2)` en sí, porque los drivers de esos capítulos no transportaban datos reales. Ahora sí. Un repaso conciso prepara el terreno para todo lo que sigue.

### ¿Qué diferencia a un dispositivo de un archivo?

Desde fuera, parecen idénticos. Ambos se abren con `open(2)`. Ambos se leen con `read(2)` y se escriben con `write(2)`. Ambos se cierran con `close(2)`. Un programa de usuario que funciona con un archivo regular casi siempre funciona con un archivo de dispositivo sin cambios en su código fuente, porque la API del espacio de usuario no hace distinción entre ellos.

Desde dentro, hay diferencias reales, y el autor de un driver necesita interiorizarlas.

Un archivo regular tiene un almacenamiento de respaldo, normalmente bytes en disco gestionados por un sistema de archivos. El kernel decide cuándo hacer lectura anticipada, cuándo usar la caché y cuándo vaciarla. Los datos tienen una identidad persistente; dos programas que lean el byte cero de un archivo ven el mismo byte. Buscar posiciones es barato y sin límites dentro del tamaño del archivo.

Un archivo de dispositivo no tiene almacenamiento de respaldo en el sentido del sistema de archivos. Cuando un programa de usuario lee de él, el driver decide qué bytes producir. Cuando un programa de usuario escribe en él, el driver decide qué hacer con esos bytes. La identidad de los datos es la que define tu driver. Dos programas que lean del mismo dispositivo no ven necesariamente los mismos bytes; según el driver, pueden ver los mismos bytes, pueden ver mitades disjuntas de un único stream, o pueden ver streams completamente independientes. Buscar posiciones puede ser significativo, carecer de sentido o estar activamente prohibido.

La consecuencia práctica para tus handlers `d_read` y `d_write` es que **el driver es la definición canónica de lo que `read` y `write` significan** en este dispositivo. El kernel te entregará una petición de E/S; no te dirá qué hacer con ella. Las convenciones que los programas UNIX esperan (un stream de bytes, valores de retorno coherentes, códigos de error honestos, fin de archivo representado como cero bytes devueltos) son convenciones que tu driver debe cumplir a propósito. El kernel no las impone.

### Cómo trata UNIX los dispositivos como streams de datos

Vale la pena precisar la palabra «stream», porque aparece en cada discusión sobre E/S en UNIX y tiene al menos tres significados distintos según el contexto.

Para nuestros propósitos, un stream es una **secuencia de bytes entregados en orden**. Ni el llamador ni el driver conocen la longitud total de antemano. Cualquiera de las partes puede detenerse en cualquier momento. La secuencia puede tener un final natural (un archivo que se ha leído completamente) o puede continuar indefinidamente (un terminal, un socket de red, un sensor). Las reglas son las mismas en ambos casos: el lector solicita un número determinado de bytes, el escritor solicita que se acepte un número determinado de bytes, y el kernel informa de cuántos bytes se transfirieron realmente.

Un stream no tiene efectos secundarios más allá de la propia transferencia de datos. Si tu driver necesita exponer una superficie de control, una forma de cambiar la configuración, restablecer el estado o negociar parámetros, esa superficie no pertenece a `read` ni a `write`. La interfaz de control es `ioctl(2)`, que se trata en el capítulo 25. No mezcles comandos de control en el stream de datos. Eso hace que tu driver sea más difícil de usar, más difícil de probar y más difícil de evolucionar.

Un stream es unidireccional por llamada. `read(2)` mueve bytes desde el driver hacia el usuario. `write(2)` mueve bytes desde el usuario hacia el driver. Una única llamada al sistema nunca hace ambas cosas. Si necesitas comportamiento dúplex, por ejemplo un patrón petición-respuesta, lo implementas como una escritura seguida de una lectura, con la coordinación que tu driver requiera internamente.

### Acceso secuencial frente a acceso aleatorio

La mayoría de los drivers producen streams secuenciales: los bytes salen en el orden en que llegan, y `lseek(2)` no hace nada de interés o se rechaza directamente. Un terminal, un puerto serie, un dispositivo de captura de paquetes, un stream de registros; todos son secuenciales.

Unos pocos drivers son de acceso aleatorio: el llamador puede direccionar cualquier byte a través de `lseek(2)`, y el mismo offset siempre lee los mismos datos. Un driver de disco en memoria, `/dev/mem`, y algún otro encajan en este modelo. En la mayoría de los aspectos se parecen más a archivos regulares que a dispositivos.

El autor de un driver elige dónde se sitúa el driver en este espectro. Tu driver `myfirst` estará en el extremo secuencial durante la mayor parte de este capítulo, con un matiz: cada descriptor abierto lleva su propio offset de lectura, de modo que dos procesos que lean de forma concurrente empiezan desde puntos distintos del stream. Este es el compromiso que usa la mayoría de los dispositivos de caracteres pequeños. Da a cada lector una visión coherente de lo que ha consumido, sin imponer un contrato de acceso aleatorio real al driver.

La elección se refleja en dos lugares del código:

- **Tu `d_read` actualiza `uio->uio_offset`** (lo que `uiomove(9)` hace por ti) si y solo si el offset tiene significado para ti. En un dispositivo verdaderamente secuencial donde el offset no tiene sentido, el valor se ignora.
- **Tu driver honra o ignora el `uio->uio_offset` entrante** al inicio de cada lectura. Los drivers secuenciales lo ignoran y sirven desde donde estén. Los drivers de acceso aleatorio lo tratan como una dirección dentro de un espacio lineal.

Para el `myfirst` de tres etapas trataremos `uio->uio_offset` como una instantánea por llamada de dónde se encuentra este descriptor en el stream y actualizaremos nuestros contadores internos en consecuencia.

### El papel de read() y write() en los drivers de dispositivo

Dentro del kernel, `read(2)` y `write(2)` sobre un archivo de dispositivo acaban llamando a tus punteros a función `cdevsw->d_read` y `cdevsw->d_write`. Todo lo que hay entre la llamada al sistema y tu función es maquinaria de devfs y VFS; todo lo que hay después de que tu función retorna es el kernel devolviendo el resultado a userland. Tu handler es el único lugar donde se calcula la respuesta específica del driver a «¿qué ocurre en esta llamada?».

El trabajo del handler no es complicado en abstracto:

1. Examina la petición. ¿Cuántos bytes se solicitan o se te entregan?
2. Mueve los bytes. Usa `uiomove(9)` para transferir datos entre tu buffer del kernel y el del usuario.
3. Devuelve un resultado. Un cero para indicar éxito (con `uio_resid` actualizado en consecuencia), o un valor errno para indicar fallo.

Lo que hace que el handler no sea trivial es que el paso 2 es el límite de confianza entre la memoria del usuario y la memoria del kernel, y cada interacción con la memoria del usuario debe ser segura frente a programas de usuario que se comporten mal o sean maliciosos. Por eso existe `uiomove(9)`. Tú no escribes la lógica de seguridad; la escribe el kernel, siempre que lo solicites a través de la API correcta.

### Dispositivos de caracteres frente a dispositivos de bloques: una revisión

El capítulo 8 señaló que FreeBSD no ha distribuido nodos de dispositivo de bloque especiales al espacio de usuario durante muchos años. Los drivers de almacenamiento viven en GEOM y se publican como dispositivos de caracteres. Para los propósitos de este capítulo, el dispositivo de caracteres es la única forma que nos interesa.

La consecuencia práctica es que todo lo que se trata en este capítulo se aplica a cada driver que probablemente escribas en las Partes 2 a 4. `d_read` y `d_write` son los puntos de entrada. `struct uio` es el portador. `uiomove(9)` es el transmisor. Cuando lleguemos a la Parte 6 y examinemos los drivers de almacenamiento respaldados por GEOM, su camino de datos tendrá un aspecto diferente, pero seguirá estando construido con los mismos primitivos que estamos estudiando ahora.

### Ejercicio: clasificar los dispositivos reales en tu sistema FreeBSD

Antes de que el resto del capítulo se adentre en el código, dedica cinco minutos en tu máquina de laboratorio. Abre un terminal y recorre `/dev`:

```sh
% ls /dev
% ls -l /dev/null /dev/zero /dev/random /dev/urandom /dev/console
```

Para cada nodo que veas, hazte tres preguntas:

1. ¿Es secuencial o de acceso aleatorio?
2. Si hago `cat` sobre él, ¿debería producir algún byte? ¿Qué bytes?
3. Si hago `echo something >` sobre él, ¿debería verse algo? ¿Dónde?

Prueba con algunos:

```sh
% head -c 16 /dev/zero | od -An -tx1
% head -c 16 /dev/random | od -An -tx1
% echo "hello" > /dev/null
% echo $?
```

Observa que `/dev/zero` es inagotable, `/dev/random` entrega bytes impredecibles, `/dev/null` consume las escrituras en silencio y devuelve éxito, y ninguno de estos tres admite búsqueda de posición en un sentido útil. Esos comportamientos no son accidentes. Son los handlers `d_read` y `d_write` de esos drivers, haciendo exactamente lo que estamos a punto de estudiar.

Si abres `/usr/src/sys/dev/null/null.c` y miras `null_write`, verás la implementación de una sola línea: `uio->uio_resid = 0; return 0;`. Eso es un handler `write` completamente funcional. El driver ha declarado «he consumido cada byte; sin error». Es la implementación de escritura más pequeña con sentido real en FreeBSD, y al final de este capítulo podrás escribirla, y muchas más elaboradas, sin dudar.



## La anatomía de `d_read()`

El camino de lectura de tu driver comienza en el momento en que devfs despacha una llamada a `cdevsw->d_read`. La firma es fija, declarada en `/usr/src/sys/sys/conf.h`:

```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
```

Toda función `d_read` en el árbol de FreeBSD tiene exactamente esta forma. Los tres argumentos son la descripción completa de la llamada:

- `dev` es el `struct cdev *` que representa el nodo de dispositivo que fue abierto. En un driver que gestiona más de un cdev por instancia, te indica sobre cuál de ellos recae la llamada. En `myfirst`, donde el dispositivo principal y su alias se despachan a través del mismo manejador, ambos resuelven al mismo softc subyacente mediante `dev->si_drv1`.
- `uio` es el `struct uio *` que describe la solicitud de I/O: qué buffers proporcionó el usuario, cuál es su tamaño, en qué punto del flujo debe comenzar la lectura y cuántos bytes quedan por transferir. Lo diseccionaremos en la siguiente sección.
- `ioflag` es una máscara de bits de flags definidos en `/usr/src/sys/sys/vnode.h`. El que importa para la I/O no bloqueante es `IO_NDELAY`, que se activa cuando el usuario abrió el descriptor con `O_NONBLOCK` (o lo pasó más tarde mediante `fcntl(F_SETFL, ...)`). Existen otros flags relacionados con la I/O del sistema de archivos basada en vnodes, pero para los drivers de dispositivo de caracteres normalmente solo inspeccionarás `IO_NDELAY`.

El valor de retorno es un entero de estilo errno: cero en caso de éxito, un código errno positivo en caso de error. **No** es un recuento de bytes. El kernel calcula el recuento de bytes observando cuánto disminuyó `uio_resid` durante la llamada, y comunica ese valor al espacio de usuario como valor de retorno de `read(2)`. Esta inversión es una de las dos o tres cosas más importantes que debes interiorizar de este capítulo. `d_read` devuelve un código de error; el número de bytes transferidos está implícito en el uio.

### Qué se le pide a `d_read`

Resumido en una sola frase, el trabajo consiste en: **producir hasta `uio->uio_resid` bytes desde el dispositivo, entregarlos a través de `uiomove(9)` en el buffer que describe el `uio`, y devolver cero**.

De esa frase se derivan algunos corolarios que conviene hacer explícitos.

La función puede producir menos bytes de los solicitados. Las lecturas cortas son legítimas y esperadas. Un programa de usuario que pidió 4096 bytes y recibió 17 no lo trata como un error, sino como «el driver tenía 17 bytes que dar en ese momento». El número es visible para el llamador porque `uiomove(9)` decrementó `uio_resid` en 17 al mover los bytes.

La función puede producir cero bytes. Una lectura de cero bytes es la forma en que UNIX informa el fin de archivo. Si tu driver no tiene más datos que entregar y no va a tener ninguno más, devuelve cero y deja `uio_resid` sin modificar. El llamador ve un `read(2)` de cero bytes y sabe que el flujo ha terminado.

La función no debe producir más bytes de los solicitados. `uiomove(9)` lo impone por ti; no moverá más de `MIN(uio_resid, n)` bytes en una sola llamada. Si llamas a `uiomove` varias veces dentro de un mismo `d_read`, asegúrate de que tu bucle también respete `uio_resid`.

La función debe devolver un errno en caso de fallo. En caso de éxito, el valor de retorno es cero. Los valores distintos de cero son interpretados por el kernel como errores; el kernel los propaga al espacio de usuario a través de `errno`. Los valores habituales son `ENXIO`, `EFAULT`, `EIO`, `EINTR` y `EAGAIN`. Los repasaremos en la sección de gestión de errores.

La función puede bloquearse. `d_read` se ejecuta en un contexto de proceso (el del llamador), por lo que `msleep(9)` y funciones similares son válidas. Así es como los drivers implementan lecturas bloqueantes que esperan datos. No usaremos `msleep(9)` en este capítulo (el capítulo 10 lo introduce formalmente), pero merece la pena saber que tienes pleno derecho a bloquearte.

### Qué **no** se le pide a `d_read`

Una breve lista de cosas de las que el handler no es responsable, porque el kernel o devfs se encargan de ellas:

- **Localizar la memoria de usuario**. El `uio` ya describe el buffer de destino. Tu handler no necesita consultar tablas de páginas ni validar direcciones.
- **Comprobar permisos**. Las credenciales del usuario fueron verificadas por `open(2)`; cuando `d_read` se ejecuta, el llamador ya tiene permiso para leer desde este descriptor.
- **Contar bytes para el llamador**. El kernel calcula el número de bytes a partir de `uio_resid`. Nunca devuelves un recuento de bytes.
- **Imponer el límite de tamaño global**. El kernel ya ha limitado `uio_resid` a un valor que el sistema puede gestionar.

Cada uno de estos puntos supone una tentación en algún momento. Resístelas todas. Cada uno es un lugar donde un handler puede introducir un bug sutil que un uso correcto de `uiomove` evita por diseño.

### Un primer `d_read` real

A continuación se muestra el `d_read` más pequeño y útil del árbol de FreeBSD. Es la función `zero_read` de `/usr/src/sys/dev/null/null.c`, y es la forma en que `/dev/zero` produce un flujo infinito de bytes a cero:

```c
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        void *zbuf;
        ssize_t len;
        int error = 0;

        zbuf = __DECONST(void *, zero_region);
        while (uio->uio_resid > 0 && error == 0) {
                len = uio->uio_resid;
                if (len > ZERO_REGION_SIZE)
                        len = ZERO_REGION_SIZE;
                error = uiomove(zbuf, len, uio);
        }
        return (error);
}
```

Detente un momento en esto. El cuerpo del bucle tiene tres líneas. La condición de terminación es doble: o bien `uio_resid` llega a cero (hemos transferido todo lo que el llamador pidió) o bien `uiomove` devuelve un error. Cada iteración mueve tanto del área rellena de ceros como la solicitud tiene espacio. La función devuelve el último código de error, que es cero si la transferencia se completó sin problemas.

El bucle es necesario porque la región de ceros es finita: una sola llamada a `uiomove` no puede mover desde ella un número arbitrario de bytes, por lo que el bucle divide la transferencia en partes. Para un driver cuya fuente de datos cabe en un único buffer del kernel de tamaño modesto, el bucle se reduce a una sola llamada. La fase 1 de `myfirst` tendrá exactamente esa forma.

Observa también lo que la función **no** hace. No consulta `uio_offset`. No le importa en qué punto de un hipotético flujo comienza la lectura; toda lectura de `/dev/zero` produce bytes a cero. No comprueba el cdev. No comprueba los flags. Hace exactamente una sola cosa, y la hace con una sola API.

Ese es el modelo. Tu `d_read` normalmente se parecerá a alguna variación de ese bucle.

### Una variante: `uiomove_frombuf`

Cuando tus datos de origen son un buffer del kernel de tamaño fijo y quieres que el driver se comporte como un archivo respaldado por ese buffer, la función auxiliar `uiomove_frombuf(9)` se encarga de la aritmética de offset por ti.

Su declaración, extraída de `/usr/src/sys/sys/uio.h`:

```c
int uiomove_frombuf(void *buf, int buflen, struct uio *uio);
```

Su implementación, en `/usr/src/sys/kern/subr_uio.c`, es lo bastante breve como para reproducirla aquí:

```c
int
uiomove_frombuf(void *buf, int buflen, struct uio *uio)
{
        size_t offset, n;

        if (uio->uio_offset < 0 || uio->uio_resid < 0 ||
            (offset = uio->uio_offset) != uio->uio_offset)
                return (EINVAL);
        if (buflen <= 0 || offset >= buflen)
                return (0);
        if ((n = buflen - offset) > IOSIZE_MAX)
                return (EINVAL);
        return (uiomove((char *)buf + offset, n, uio));
}
```

Léela con atención, porque el comportamiento es preciso. La función recibe un puntero `buf` a un buffer del kernel de tamaño `buflen`, consulta `uio->uio_offset`, y:

- Si el offset es negativo o de alguna otra forma no tiene sentido, devuelve `EINVAL`.
- Si el offset está más allá del final del buffer, devuelve cero sin mover ningún byte. Esto es el fin de archivo: el llamador verá una lectura de cero bytes.
- En caso contrario, llama a `uiomove(9)` con un puntero a `buf` en el offset actual y una longitud igual a la cola restante del buffer.

La función no usa bucle; `uiomove` moverá tantos bytes como `uio_resid` tenga espacio, y decrementará `uio_resid` en consecuencia. El driver nunca necesita tocar `uio_offset` después de la llamada, porque `uiomove` ya lo hace.

Si tu driver expone un buffer fijo como archivo legible, basta con un `d_read` de una sola línea:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        return (uiomove_frombuf(sc->buf, sc->buflen, uio));
}
```

La fase 1 de este capítulo usa exactamente ese patrón, con un pequeño ajuste para registrar el offset de lectura por descriptor, de modo que dos lectores concurrentes vean su propio progreso.

### La firma en uso: myfirst_read, fase 1

Así es como quedará nuestro `d_read` de la fase 1. No lo escribas todavía; recorreremos el código fuente completo en la sección de implementación. Verlo aquí sirve principalmente para anclar la discusión.

Antes de leer el código, detente en un detalle que reaparecerá en casi todos los handlers del resto de este capítulo. Las primeras cuatro líneas de cualquier handler que gestiona estado por apertura siguen un **patrón fijo de código repetitivo**:

```c
struct myfirst_fh *fh;
int error;

error = devfs_get_cdevpriv((void **)&fh);
if (error != 0)
        return (error);
```

Este patrón recupera el `fh` por descriptor que `d_open` registró a través de `devfs_set_cdevpriv(9)`, y propaga cualquier fallo de vuelta al kernel sin modificación. Lo verás al principio de `myfirst_read`, `myfirst_write`, `myfirst_ioctl`, `myfirst_poll` y los helpers de `kqfilter`. Cuando un laboratorio posterior diga «recupera el estado por apertura con el código repetitivo habitual de `devfs_get_cdevpriv`», se refiere a este bloque, y el resto del capítulo no volverá a explicarlo. Si algún handler reordena estas líneas, trátalo como una señal de alerta: ejecutar cualquier lógica antes de esta llamada significa que el handler aún no sabe qué apertura está sirviendo. La única sutileza que merece recordar es que la comprobación de actividad `sc == NULL` va *después* de este código repetitivo, no antes, porque necesitas que el estado por apertura se recupere de forma segura incluso en un dispositivo que está siendo destruido.

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        before = uio->uio_offset;
        error = uiomove_frombuf(__DECONST(void *, sc->message),
            sc->message_len, uio);
        if (error == 0)
                fh->reads += (uio->uio_offset - before);
        fh->read_off = uio->uio_offset;
        return (error);
}
```

Unos pocos puntos que vale la pena destacar antes de continuar. La función recupera la estructura por apertura a través de `devfs_get_cdevpriv(9)`, comprueba que el softc está activo y luego delega el trabajo real en `uiomove_frombuf`. Capturamos `uio->uio_offset` en una variable local `before` al entrar, de modo que tras la llamada podemos calcular el número de bytes que el kernel acaba de mover como `uio->uio_offset - before`. Ese incremento se registra en el contador por descriptor. La asignación final a `fh->read_off` guarda la posición en el flujo para que el resto del driver pueda informarla más adelante.

Si el driver no tiene datos que entregar, `uiomove_frombuf` devuelve cero y `uio_resid` queda sin cambios, que es la forma en que se informa el fin de archivo. Si ocurre un error dentro de `uiomove`, lo propagamos hacia arriba devolviendo el código de error. Nada de este handler necesita `copyin` o `copyout` directamente. La seguridad de la transferencia la gestiona `uiomove` en nuestro nombre.

### Lectura de `d_read` en el árbol

Un buen ejercicio de lectura, al terminar esta sección, es buscar `d_read` con grep en `/usr/src/sys/dev` y ver qué hacen otros drivers dentro de él. Encontrarás tres formas recurrentes:

- **Drivers que leen desde un buffer fijo.** Usan `uiomove_frombuf(9)` o un equivalente escrito a mano; una llamada y listo. `/usr/src/sys/fs/pseudofs/pseudofs_vnops.c` usa el helper extensamente; el patrón es idéntico para dispositivos de caracteres.
- **Drivers que leen desde un buffer dinámico.** Adquieren un lock interno, capturan cuántos datos hay disponibles, llaman a `uiomove(9)` con esa longitud, liberan el lock y retornan. Construiremos uno de estos en la fase 2.
- **Drivers que leen desde una fuente bloqueante.** Comprueban si hay datos disponibles y, si no los hay, o bien duermen en una variable de condición (modo bloqueante) o devuelven `EAGAIN` (modo no bloqueante). Esto es territorio del capítulo 10.

Las tres formas comparten la misma columna vertebral de cuatro líneas: recuperar el estado por apertura si lo usas, verificar la actividad, llamar a `uiomove` (o una función similar), devolver el código de error. Las diferencias están en cómo preparan el buffer, no en cómo lo transfieren.



## La anatomía de `d_write()`

El handler de escritura es el reflejo especular del handler de lectura, con algunas pequeñas diferencias en los extremos. La firma, extraída de `/usr/src/sys/sys/conf.h`:

```c
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

La forma es idéntica. Los tres argumentos tienen el mismo significado. El valor de retorno es un errno, cero en caso de éxito. El recuento de bytes se sigue calculando a partir de `uio_resid`: el kernel observa cuánto disminuyó `uio_resid` durante la llamada y lo notifica como valor de retorno de `write(2)`.

### Qué se le pide a `d_write`

Una frase de nuevo: **consumir hasta `uio->uio_resid` bytes del usuario, entregarlos a través de `uiomove(9)` en el lugar donde tu driver guarda sus datos, y devolver cero**.

Los corolarios son casi exactamente los mismos que para las lecturas, con dos diferencias notables:

- Una escritura corta es legítima pero poco habitual. Un driver que acepta menos bytes de los ofrecidos debe actualizar `uio_resid` para reflejar la realidad, y el kernel informará el recuento parcial al espacio de usuario. La mayoría de los programas de usuario bien escritos harán un bucle y reintentarán el resto; muchos no lo harán. La regla general es: acepta todo lo que puedas y, si no puedes aceptar más, devuelve `EAGAIN` para los llamadores no bloqueantes y (eventualmente) duerme para los bloqueantes.
- Una escritura de cero bytes no es el fin de archivo. Es simplemente una escritura que no movió ningún byte. `d_write` no tiene el concepto de EOF; solo las lecturas lo tienen. Un driver que quiere rechazar una escritura devuelve un errno distinto de cero.

El valor de retorno más habitual en el lado del error es `ENOSPC` (sin espacio disponible en el dispositivo) cuando el buffer del driver está lleno, `EFAULT` cuando ocurre un fallo relacionado con punteros dentro de `uiomove`, y `EIO` para errores de hardware de carácter general. Un driver que impone un límite de longitud por escritura puede devolver `EINVAL` o `EMSGSIZE` para escrituras que superen ese límite; veremos cuál elegir más adelante en el capítulo.

### Qué **no** se le pide a `d_write`

La misma lista que para `d_read`: no localiza la memoria de usuario, no comprueba permisos, no cuenta bytes para el llamador y no impone límites a nivel del sistema. El kernel se ocupa de los cuatro.

Una adición específica de las escrituras: **no asumas que los datos entrantes están terminados en nulo o tienen alguna estructura concreta**. Los usuarios pueden escribir bytes arbitrarios. Si tu driver espera una entrada estructurada, debe analizarla de forma defensiva. Si tu driver espera datos binarios, debe gestionar escrituras que no estén alineadas con ningún límite natural. `write(2)` es un flujo de bytes, no una cola de mensajes. El camino de `ioctl` del capítulo 25 es donde corresponden los comandos estructurados y delimitados.

### Un primer `d_write` real

El `d_write` no trivial más sencillo del árbol es `null_write`, de `/usr/src/sys/dev/null/null.c`:

```c
static int
null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        uio->uio_resid = 0;

        return (0);
}
```

Dos líneas. El handler le dice al kernel «he consumido todos los bytes» asignando cero a `uio_resid`, y devuelve éxito. El kernel informa al espacio de usuario de la longitud original de la solicitud como el número de bytes escritos. `/dev/null` no hace nada con los bytes; ese es precisamente el sentido de `/dev/null`. Pero el patrón es instructivo: **asignar `uio_resid = 0` es la forma más concisa de marcar una escritura como completamente consumida**, y es exactamente lo que habría hecho `uiomove(9)` si le hubiéramos proporcionado un destino.

Un caso algo más interesante es `full_write`, también en `null.c`:

```c
static int
full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
{
        return (ENOSPC);
}
```

Esto es lo que hay detrás de `/dev/full`, un dispositivo que está lleno para siempre. Toda escritura falla con `ENOSPC`, y el llamador ve el valor de `errno` correspondiente. El handler no toca `uio_resid`; el kernel comprueba que no se ha movido ningún byte y devuelve un valor de retorno de -1 con `errno = ENOSPC`.

Juntos, estos dos handlers ilustran los dos extremos del lado de escritura: aceptar todo o rechazar todo. Los drivers reales se sitúan en algún punto intermedio, decidiendo cuántos de los bytes ofrecidos pueden aceptar y almacenando esos bytes en algún lugar.

### Una escritura que almacena datos de verdad

A continuación se muestra la forma del handler de escritura que implementaremos al final de este capítulo. De nuevo, no lo escribas todavía; esto es solo una vista previa para orientarte.

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        avail = sc->buflen - sc->bufused;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + sc->bufused, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

El handler bloquea el mutex del softc, comprueba cuánto espacio queda en el buffer, recorta la transferencia a lo que cabe, llama a `uiomove(9)` con esa longitud y contabiliza la transferencia satisfactoria avanzando `bufused`. Si el buffer está lleno, devuelve `ENOSPC` para señalárselo al llamante. Todo lo que el handler hace para gestionar la concurrencia o las escrituras parciales queda capturado en la combinación del lock y el recorte.

Ten en cuenta que `uiomove` se llama **con el mutex tomado**. Esto está bien siempre que el mutex sea un mutex ordinario `MTX_DEF` (como lo es el de `myfirst`) y el contexto de llamada sea un thread del kernel normal que puede dormir. `uiomove` puede provocar un page-fault al copiar hacia o desde memoria de usuario, y los page-faults pueden requerir que el kernel duerma esperando una lectura de disco. Dormir mientras se mantiene un mutex `MTX_DEF` es legal; dormir mientras se mantiene un spin lock (`MTX_SPIN`) sería un error. La Parte 3 cubre las reglas de locking formalmente; por ahora, confía en el tipo de lock que elegiste en el Capítulo 7.

### Simetría con `d_read`

Las lecturas y escrituras son casi idénticas desde el punto de vista del driver. Los datos fluyen en direcciones opuestas, y el campo `uio->uio_rw` le indica a `uiomove` en qué dirección mover los bytes. En el lado del driver pasas los mismos argumentos: un puntero a memoria del kernel, una longitud y el uio. En el lado del usuario, `uiomove` copia desde el buffer del kernel (en una lectura) o hacia él (en una escritura). Raramente tienes que pensar en la dirección; `uio_rw` ya está establecido.

Lo que cambia entre los dos handlers es la **intención**. Una lectura es la oportunidad del driver para producir datos. Una escritura es la oportunidad del driver para consumir datos. Tu código en cada handler sabe qué papel desempeña y realiza la contabilidad adecuada: un lector lleva la cuenta de cuánto ha entregado, un escritor lleva la cuenta de cuánto ha almacenado.

### Leyendo `d_write` en el árbol

Después de leer esta sección, dedica unos minutos con `grep d_write /usr/src/sys/dev | head -20` y observa lo que hacen otros drivers. Aparecen tres formas:

- **Drivers que descartan las escrituras**. Normalmente una línea: ponen `uio_resid = 0` y devuelven cero. El `null_write` del driver `null` es el prototipo.
- **Drivers que almacenan las escrituras**. Bloquean, comprueban la capacidad, llaman a `uiomove(9)`, contabilizan, desbloquean y devuelven. Nuestro handler de la Etapa 3 tiene esta forma.
- **Drivers que reenvían las escrituras al hardware**. Extraen datos del uio, los colocan en un buffer DMA o en un anillo propiedad del hardware, y activan el hardware. Esta forma queda fuera de alcance hasta la Parte 4; la mecánica de `uiomove` es la misma, pero el destino es una región mapeada por DMA en lugar de un buffer asignado con `malloc`.

Todos los drivers reales encajan en una de estas tres. Los que primero acumulan o transforman datos y luego los reenvían a algún sitio tienden a combinar la forma 2 y la forma 3, pero los primitivos son idénticos.

### Cómo leer un `d_read` o `d_write` desconocido en el árbol

Un capítulo como este es más útil cuando te ayuda a leer código ajeno, no solo el tuyo. A medida que explores el árbol de FreeBSD encontrarás handlers que no se parecen en nada a `null_write` o `zero_read`. La forma seguirá estando ahí; la decoración será diferente. Aquí tienes un pequeño protocolo de lectura que elimina las conjeturas.

**Primer paso: busca el tipo de retorno y los nombres de los argumentos.** Todo `d_read_t` y `d_write_t` toma los mismos tres argumentos. Si el handler les ha dado nombres distintos a `dev`, `uio` e `ioflag`, anota lo que eligió el autor (`cdev`, `u`, `flags` son todos habituales). Ten esos nombres en mente mientras lees.

**Segundo paso: localiza la llamada a `uiomove` (o equivalente).** Remóntate desde ahí hacia atrás para entender qué puntero del kernel se le pasa y qué longitud. Ese par es el núcleo del handler. Todo lo que hay antes de la llamada a `uiomove` está preparando el puntero y la longitud; todo lo que hay después es contabilidad.

**Tercer paso: localiza la toma y liberación del lock.** Un handler que toma un lock antes de `uiomove` y lo libera después está serializando con otros handlers. Un handler sin lock opera con datos de solo lectura o utiliza algún otro primitivo de sincronización (una variable de condición, un refcount, un reader lock). Identifica cuál es.

**Cuarto paso: localiza las devoluciones de errno.** Lista los valores de errno que puede producir el handler. Si la lista es corta y cada valor tiene un desencadenante obvio, el handler está bien escrito. Si la lista es larga u opaca, el autor probablemente dejó cabos sueltos.

**Quinto paso: localiza las transiciones de estado.** ¿Qué contadores incrementa el handler? ¿Qué campos por handle toca? Esas transiciones son la firma de comportamiento del driver, y suelen ser la parte que más difiere de un driver a otro.

Aplica este protocolo a `zero_read` en `/usr/src/sys/dev/null/null.c`. Los nombres de los argumentos son los estándar. La llamada a `uiomove` pasa el puntero del kernel `zbuf` (apuntando a `zero_region`) y una longitud recortada por `ZERO_REGION_SIZE`. No hay lock; los datos son constantes. El único errno que puede devolver el handler es lo que devuelva `uiomove`. No hay transiciones de estado; `/dev/zero` no tiene estado.

Ahora aplica el mismo protocolo a `myfirst_write` en la Etapa 3. Nombres de argumentos: estándar. Llamada a `uiomove`: puntero del kernel `sc->buf + bufhead + bufused`, longitud `MIN((size_t)uio->uio_resid, avail)`. Lock: `sc->mtx` tomado antes y liberado después. Devoluciones de errno: `ENXIO` (dispositivo desaparecido), `ENOSPC` (buffer lleno), `EFAULT` a través de `uiomove`, o cero. Transiciones de estado: `sc->bufused += towrite`, `sc->bytes_written += towrite`, `fh->writes += towrite`.

Dos drivers, el mismo protocolo, dos descripciones coherentes de lo que hace el handler. Una vez que apliques este hábito de lectura media docena de veces, los handlers desconocidos dejarán de parecértelo.

### ¿Qué ocurre si dejas `d_read` o `d_write` sin asignar?

Un detalle sobre el que los principiantes a veces se preguntan: ¿qué ocurre si tu `cdevsw` no establece `.d_read` ni `.d_write`? La respuesta corta es que el kernel sustituye un valor predeterminado que devuelve `ENODEV` o actúa como un no-op, dependiendo de qué slot concreto esté vacío y qué otros `d_flags` estén establecidos. La respuesta larga vale la pena conocerla, porque los drivers reales sí usan los valores predeterminados, deliberadamente, cuando quieren expresar «este dispositivo no hace lecturas» o «las escrituras se descartan silenciosamente».

Observa cómo `/usr/src/sys/dev/null/null.c` conecta sus tres drivers:

```c
static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};
```

`.d_read` se asigna al helper del kernel `nullop`, con un cast a `d_read_t *`. `nullop` es una función universal de «no hagas nada, devuelve cero» declarada en `/usr/src/sys/sys/systm.h` y definida en `/usr/src/sys/kern/kern_conf.c`; no toma argumentos y devuelve cero. Se usa en todo el kernel donde un slot de método necesita un valor predeterminado inofensivo. El cast funciona porque `d_read_t` espera una función que devuelva `int`, y la forma `int (*)(void)` de `nullop` es lo suficientemente similar para que el dispatch del cdevsw la llame sin sorpresas.

Para `/dev/null`, `(d_read_t *)nullop` significa «cada lectura devuelve cero bytes, para siempre». Un usuario que hace `cat /dev/null` ve un EOF inmediato. Esto es diferente de `/dev/zero`, que instala `zero_read` para producir un flujo infinito de bytes a cero. El contraste entre los dos drivers es un contraste entre dos comportamientos de lectura predeterminados, y ambos son exactamente una línea en el `cdevsw`.

Si omites tanto `.d_read` como `.d_write` por completo, el kernel los rellena con valores predeterminados que devuelven `ENODEV`. Esa es la elección correcta cuando el dispositivo realmente no admite transferencia de datos; los llamantes ven un error claro en lugar de un éxito silencioso. Pero para dispositivos que deben aceptar escrituras silenciosamente o producir lecturas de cero bytes, establecer el slot a `(d_read_t *)nullop` es el gesto idiomático de FreeBSD.

**Regla práctica:** decide deliberadamente. O implementas el handler (para un comportamiento real), o lo asignas a `(d_read_t *)nullop` / `(d_write_t *)nullop` (para valores predeterminados inofensivos), o lo dejas completamente sin asignar (para `ENODEV`). Todos los drivers reales del árbol eligen una de estas tres opciones deliberadamente, y la elección es visible para los usuarios.

### Un segundo driver real: cómo `mem(4)` usa un mismo handler para ambas direcciones

`null.c` es el ejemplo mínimo canónico. Vale la pena echar un vistazo a un ejemplo algo más rico antes de continuar, porque demuestra un patrón que encontrarás a menudo en el árbol: **un único handler que sirve tanto a `d_read` como a `d_write`**, apoyándose en `uio->uio_rw` para distinguir las dos direcciones.

El driver es `mem(4)`, que expone `/dev/mem` y `/dev/kmem`. Las partes comunes viven en `/usr/src/sys/dev/mem/memdev.c`, y la lógica de lectura y escritura específica de arquitectura vive en `/usr/src/sys/<arch>/<arch>/mem.c`. En amd64, el archivo es `/usr/src/sys/amd64/amd64/mem.c`, y la función es `memrw`.

Observa primero el `cdevsw`:

```c
static struct cdevsw mem_cdevsw = {
        .d_version =    D_VERSION,
        .d_flags =      D_MEM,
        .d_open =       memopen,
        .d_read =       memrw,
        .d_write =      memrw,
        .d_ioctl =      memioctl,
        .d_mmap =       memmmap,
        .d_name =       "mem",
};
```

Tanto `.d_read` como `.d_write` apuntan a la misma función. Esto es legal porque los typedefs `d_read_t` y `d_write_t` son idénticos (ambos son `int (*)(struct cdev *, struct uio *, int)`), de modo que una única función puede satisfacer ambos. El truco consiste en leer `uio->uio_rw` dentro del handler para decidir en qué dirección moverse.

Un esbozo condensado de `memrw` tiene este aspecto:

```c
int
memrw(struct cdev *dev, struct uio *uio, int flags)
{
        struct iovec *iov;
        /* ... locals ... */
        ssize_t orig_resid;
        int error;

        error = 0;
        orig_resid = uio->uio_resid;
        while (uio->uio_resid > 0 && error == 0) {
                iov = uio->uio_iov;
                if (iov->iov_len == 0) {
                        uio->uio_iov++;
                        uio->uio_iovcnt--;
                        continue;
                }
                /* compute a page-bounded chunk size into c */
                /* ... direction-independent mapping logic ... */
                error = uiomove(kernel_pointer, c, uio);
        }
        /*
         * Don't return error if any byte was written.  Read and write
         * can return error only if no i/o was performed.
         */
        if (uio->uio_resid != orig_resid)
                error = 0;
        return (error);
}
```

Hay tres ideas en este esbozo que se generalizan a tus propios drivers.

**Primero, un único handler para ambas direcciones ahorra código cuando el trabajo por byte es idéntico.** La lógica de mapeo en `memrw` resuelve un offset del espacio de usuario a un fragmento de memoria accesible desde el kernel; si estás leyendo o escribiendo en esa memoria se decide después, por `uiomove` al mirar `uio->uio_rw`. Te ahorras la duplicación de un par lectura-escritura casi idéntico, a costa de una única función que debe ser clara sobre en qué dirección se encuentra. Si las dos direcciones comparten casi nada, escribe dos funciones; si comparten casi todo, combínalas.

**Segundo, `memrw` recorre el iovec por sí mismo.** A diferencia de `myfirst`, que entrega toda la transferencia a `uiomove` en una o dos llamadas, `memrw` recorre las entradas del iovec explícitamente para poder mapear cada offset solicitado a memoria del kernel y luego llamar a `uiomove` sobre la región mapeada. Este es el patrón que usas cuando el *puntero del kernel* que tu driver le pasa a `uiomove` depende del offset que se está atendiendo. Es menos habitual que el estilo de `myfirst`, pero es la forma correcta cuando cada fragmento de la transferencia corresponde a una parte diferente del almacenamiento subyacente del driver.

**Tercero, fíjate en el truco de orig_resid al final.** El handler guarda `uio_resid` al entrar y luego, tras el bucle, comprueba si se movió algo en absoluto. Si algo se movió, devuelve cero (éxito) aunque haya ocurrido un error después, porque las convenciones de UNIX exigen que una lectura o escritura con un número de bytes distinto de cero devuelva ese recuento al llamante en lugar de hacer fallar toda la llamada. Este es el idioma del «éxito parcial»: si se movió algún byte, informa del recuento de bytes; solo falla cuando no se movió ningún byte.

Tus handlers de `myfirst` no necesitan este idioma, porque llaman a `uiomove` exactamente una vez. Si `uiomove` tiene éxito, todo se movió; si falla, nada se movió (desde el punto de vista de la contabilidad del driver). El idioma de orig_resid importa cuando tu handler hace un bucle y ese bucle puede interrumpirse a mitad por un error de `uiomove`. Recuerda el patrón; lo usarás en capítulos posteriores cuando tu driver sirva datos de múltiples fuentes.

### Por qué este recorrido mereció la pena

Dos drivers. Dos almacenamientos subyacentes muy diferentes. Un único primitivo. En `null.c`, `zero_read` sirve una región de ceros preasignada; en `memrw`, el handler sirve memoria física mapeada bajo demanda. El código tiene un aspecto diferente en la parte central, porque ahí es donde vive el conocimiento único del driver. Los extremos tienen el mismo aspecto: ambas funciones toman un uio, ambas hacen un bucle sobre `uio_resid`, ambas llaman a `uiomove(9)` para realizar la transferencia real, y ambas devuelven cero en caso de éxito o un errno.

Esa uniformidad es precisamente el objetivo. Todas las operaciones de lectura y escritura de dispositivos de caracteres en el árbol siguen esta forma. Una vez que la reconoces, puedes abrir cualquier driver desconocido en `/usr/src/sys/dev` y leer el handler con confianza: la parte que aún no comprendes siempre está en el centro, nunca en los extremos.

## Cómo funciona el argumento `ioflag`

Tanto `d_read` como `d_write` reciben un tercer argumento que el resto del capítulo apenas ha utilizado hasta ahora. Esta sección ofrece una explicación breve pero útil de qué es `ioflag`, de dónde proviene y cuándo un driver de dispositivo de caracteres debería examinarlo realmente.

### De dónde viene `ioflag`

Cada vez que un proceso realiza un `read(2)` o un `write(2)` sobre un nodo devfs, el kernel construye un valor `ioflag` a partir de las flags del descriptor de archivo actual antes de llamar a tu handler. Esta composición reside en el propio devfs, en `/usr/src/sys/fs/devfs/devfs_vnops.c`. Las líneas relevantes de `devfs_read_f` son las siguientes:

```c
ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);
if (ioflag & O_DIRECT)
        ioflag |= IO_DIRECT;
```

El patrón en `devfs_write_f` es el simétrico. El kernel toma los bits del campo `f_flag` de la tabla de archivos que son relevantes para la E/S, los enmascara y pasa ese subconjunto como `ioflag`.

Esto es importante por dos razones. En primer lugar, significa que el `ioflag` que recibe tu driver es una *instantánea*. Si el programa de usuario cambia su configuración de modo no bloqueante (mediante `fcntl(F_SETFL, O_NONBLOCK)`) entre dos llamadas `read(2)`, cada llamada llevará su propio `ioflag` actualizado. No es necesario guardar el estado en caché ni estar pendiente de los cambios; el kernel vuelve a calcular el valor en cada despacho.

En segundo lugar, significa que la mayoría de las constantes que podrías esperar ver nunca llegan a tu handler. Flags como `O_APPEND`, `O_TRUNC`, `O_CLOEXEC` y los distintos flags del estilo `O_EXLOCK` pertenecen a las capas del sistema de archivos y de la tabla de archivos. No influyen en la E/S de dispositivos de caracteres y no se reenvían.

### Las constantes de flag que importan

Las flags `IO_*` se declaran en `/usr/src/sys/sys/vnode.h`. Para los drivers de dispositivos de caracteres, solo merece la pena recordar un pequeño subconjunto:

```c
#define	IO_UNIT		0x0001		/* do I/O as atomic unit */
#define	IO_APPEND	0x0002		/* append write to end */
#define	IO_NDELAY	0x0004		/* FNDELAY flag set in file table */
#define	IO_DIRECT	0x0010		/* attempt to bypass buffer cache */
```

De estos, **solo `IO_NDELAY` e `IO_DIRECT` se incluyen en el `ioflag` que recibe tu handler**. Los tres primeros bits existen para la E/S de sistemas de archivos. Un driver de dispositivo de caracteres que inspeccione `IO_UNIT` o `IO_APPEND` estará examinando valores que siempre serán cero.

`IO_NDELAY` es el caso habitual. Se activa cuando el descriptor está en modo no bloqueante. Un driver que implemente lecturas bloqueantes (capítulo 10) utiliza este bit para decidir entre dormir o devolver `EAGAIN`. Un driver del capítulo 9 no duerme en ningún caso, así que el bit es meramente informativo, pero los capítulos posteriores sí dependen de él.

`IO_DIRECT` es una sugerencia de que el programa de usuario abrió el descriptor con `O_DIRECT`, pidiendo al kernel que omita las cachés de buffer en la medida de lo posible. Para un driver de caracteres sencillo es casi siempre irrelevante. Los drivers relacionados con almacenamiento pueden optar por respetarlo; la mayoría no lo hacen.

Observa la identidad numérica: `O_NONBLOCK` en `/usr/src/sys/sys/fcntl.h` tiene el valor `0x0004`, e `IO_NDELAY` en `/usr/src/sys/sys/vnode.h` tiene el mismo valor. No es casualidad. El comentario de cabecera situado sobre las definiciones `IO_*` indica explícitamente que `IO_NDELAY` e `IO_DIRECT` están alineados con los bits correspondientes de `fcntl(2)` para que devfs no necesite traducirlos. Tu driver puede inspeccionar el bit de cualquiera de las dos formas y obtendrá la misma respuesta.

### Un handler que comprueba `ioflag`

A continuación se muestra el aspecto que tiene un handler de lectura con soporte de modo no bloqueante a nivel de esqueleto. No utilizaremos esta forma en el capítulo 9 porque nunca dormimos, pero estudiarla ahora facilita la introducción del capítulo 10.

```c
static int
myfirst_read_nb(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        mtx_lock(&sc->mtx);
        while (sc->bufused == 0) {
                if (ioflag & IO_NDELAY) {
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
                /* ... would msleep(9) here in Chapter 10 ... */
        }
        /* ... drain buffer, uiomove, unlock, return ... */
        error = 0;
        mtx_unlock(&sc->mtx);
        return (error);
}
```

La rama sobre `IO_NDELAY` es la única decisión que toma el handler respecto al bloqueo. Todo lo demás en la función es código de E/S habitual. Esa simplicidad forma parte de la razón por la que `ioflag` es un entero simple: la respuesta de un driver a los bits de flag suele ser una única sentencia `if` al inicio del handler, no una máquina de estados compleja.

### Qué hacen las etapas del capítulo 9 con `ioflag`

La etapa 1, la etapa 2 y la etapa 3 **no** inspeccionan `ioflag`. No pueden bloquearse, por lo que el bit de no bloqueo no tiene relevancia; tampoco les importa `IO_DIRECT`. El argumento está presente en las firmas de sus handlers porque el typedef lo exige, y se ignora silenciosamente.

Ignorar silenciosamente un argumento no es un error cuando el comportamiento ignorado es obviamente correcto. Un proceso que abra uno de nuestros descriptores con `O_NONBLOCK` verá un comportamiento idéntico al de quien no lo hizo: ninguna de las dos llamadas duerme nunca, así que el flag no tiene ningún efecto observable. El capítulo 10 es donde conectaremos el flag de verdad.

### Una pequeña ayuda para la depuración

Si tienes curiosidad por saber qué contiene `ioflag` durante una prueba, un único `device_printf` a la entrada te lo dirá:

```c
device_printf(sc->dev, "d_read: ioflag=0x%x resid=%zd offset=%jd\n",
    ioflag, (ssize_t)uio->uio_resid, (intmax_t)uio->uio_offset);
```

Carga el driver, ejecuta `cat /dev/myfirst/0` y observa el valor hexadecimal. Después ejecuta un pequeño programa que use `fcntl(fd, F_SETFL, O_NONBLOCK)` antes de leer, y observa la diferencia. Es un desvío instructivo de dos minutos para cuando estés asimilando el mecanismo por primera vez.

### `ioflag` en el árbol

Busca `IO_NDELAY` en `/usr/src/sys/dev` y encontrarás decenas de coincidencias. Casi todas siguen el mismo patrón: comprobar el bit, devolver `EAGAIN` si está activo y el driver no tiene nada que servir, o dormir en caso contrario. La uniformidad es deliberada. Los drivers de FreeBSD tratan la E/S no bloqueante de la misma manera independientemente de que sean pseudodispositivos, líneas TTY, endpoints USB o almacenamiento respaldado por GEOM, y esa coherencia es parte de la razón por la que los programas de usuario escritos para un tipo de dispositivo se trasladan limpiamente a otro.



## Comprensión profunda de `struct uio`

`struct uio` es la representación que hace el kernel de una petición de E/S. Se pasa a cada invocación de `d_read` y `d_write`. Cada llamada exitosa a `uiomove(9)` la modifica. Todo autor de drivers con el que te encuentres habrá contemplado sus campos en algún momento preguntándose en cuáles puede confiar. Esta sección es donde hacemos que la estructura resulte menos misteriosa.

### La declaración

De `/usr/src/sys/sys/uio.h`:

```c
struct uio {
        struct  iovec *uio_iov;         /* scatter/gather list */
        int     uio_iovcnt;             /* length of scatter/gather list */
        off_t   uio_offset;             /* offset in target object */
        ssize_t uio_resid;              /* remaining bytes to process */
        enum    uio_seg uio_segflg;     /* address space */
        enum    uio_rw uio_rw;          /* operation */
        struct  thread *uio_td;         /* owner */
};
```

Siete campos. Cada uno tiene un propósito específico, y existe exactamente una función, `uiomove(9)`, que los utiliza todos en conjunto. Tu driver leerá algunos campos directamente; otros nunca los tocarás.

### `uio_iov` y `uio_iovcnt`: la lista scatter-gather

Un único `read(2)` o `write(2)` opera sobre un buffer de usuario contiguo. Las llamadas relacionadas `readv(2)` y `writev(2)` operan sobre una lista de buffers (un "iovec"). El kernel representa ambos casos de forma uniforme como una lista de entradas `iovec`, usando una lista de longitud uno para el caso simple.

`uio_iov` apunta a la primera entrada de esa lista. `uio_iovcnt` es el número de entradas. Cada entrada es un `struct iovec`, declarado en `/usr/src/sys/sys/_iovec.h`:

```c
struct iovec {
        void    *iov_base;
        size_t   iov_len;
};
```

`iov_base` es un puntero a memoria de usuario (para un uio de tipo `UIO_USERSPACE`) o a memoria del kernel (para un uio de tipo `UIO_SYSSPACE`). `iov_len` es el número de bytes restantes en esa entrada.

Casi nunca tocarás estos campos directamente. `uiomove(9)` recorre la lista iovec por ti, consumiendo entradas a medida que mueve bytes y dejando la lista consistente con la transferencia restante. Si tu driver accede directamente a `uio_iov` o `uio_iovcnt`, o bien estás escribiendo un driver muy inusual o estás haciendo algo incorrecto. El patrón convencional es: dejar que `uiomove` gestione el iovec, y leer los demás campos para entender el estado de la petición.

### `uio_offset`: el desplazamiento dentro del destino

Para una lectura o escritura sobre un archivo normal, `uio_offset` es la posición en el archivo donde tiene lugar la E/S. El kernel lo incrementa a medida que se mueven bytes, de modo que un `read(2)` secuencial avanza naturalmente por el archivo.

Para un archivo de dispositivo, el significado de `uio_offset` lo define el driver. Un dispositivo verdaderamente secuencial que no tenga noción de posición ignorará el valor entrante y dejará que el valor saliente refleje lo que haya hecho `uiomove`. Un dispositivo respaldado por un buffer fijo tratará el desplazamiento como una dirección dentro de ese buffer y lo respetará.

`uiomove(9)` actualiza `uio_offset` de forma sincronizada con `uio_resid`: por cada byte que mueve, decrementa `uio_resid` en uno e incrementa `uio_offset` en uno. Si tu driver llama a `uiomove` una sola vez por handler, raramente necesitarás leer `uio_offset` directamente. Si tu driver llama a `uiomove` más de una vez, o si utiliza el desplazamiento para indexar en su propio buffer, `uiomove_frombuf(9)` es la función auxiliar que necesitas.

### `uio_resid`: los bytes restantes

`uio_resid` es el número de bytes que aún deben transferirse. Al inicio de `d_read`, es la longitud total que pidió el usuario. Al final de una transferencia exitosa, es lo que no se ha movido; el kernel resta esto de la longitud original para producir el valor de retorno de `read(2)`.

Merece la pena señalar dos trampas en la aritmética con signo. En primer lugar, `uio_resid` es un `ssize_t`, que tiene signo. Un valor negativo es ilegal (y `uiomove` lanzará un `KASSERT` en kernels de depuración), pero ten cuidado de no construir uno accidentalmente mediante aritmética descuidada. En segundo lugar, `uio_resid` puede ser cero al inicio de la llamada. Esto ocurre cuando un programa de usuario llama a `read(fd, buf, 0)` o `write(fd, buf, 0)`. Tu handler no debe tratar el cero como "sin intención del usuario" y proceder a realizar E/S contra lo que podría ser un buffer sin inicializar. El patrón seguro es comprobar si es cero pronto y devolver cero (o aceptar cero y devolver cero, en el caso de las escrituras). `uiomove` lo gestiona limpiamente: devuelve cero de inmediato sin tocar nada. Así que en la práctica la "comprobación temprana" suele ser redundante; lo que importa es que no *asumas* que es distinto de cero.

### `uio_segflg`: dónde reside el buffer

Este campo indica a dónde apuntan los punteros del iovec: al espacio de usuario (`UIO_USERSPACE`), al espacio del kernel (`UIO_SYSSPACE`) o a un mapa de objetos directo (`UIO_NOCOPY`). La enumeración se encuentra en `/usr/src/sys/sys/_uio.h`:

```c
enum uio_seg {
        UIO_USERSPACE,          /* from user data space */
        UIO_SYSSPACE,           /* from system space */
        UIO_NOCOPY              /* don't copy, already in object */
};
```

Para un `d_read` o `d_write` invocado en nombre de un syscall de usuario, `uio_segflg` es `UIO_USERSPACE`. `uiomove(9)` lee el campo y elige la primitiva de transferencia adecuada: `copyin` / `copyout` para segmentos en espacio de usuario, `bcopy` para segmentos en espacio del kernel. Tu driver no necesita bifurcar en función de esto; `uiomove` lo hace por ti.

Ocasionalmente encontrarás código que construye un uio en modo kernel a mano, generalmente para reutilizar una función que acepta un uio pero servirlo desde un buffer del kernel. Ese código establece `uio_segflg` en `UIO_SYSSPACE`. Es legítimo y útil, y lo veremos brevemente en los laboratorios. No lo confundas con un uio de espacio de usuario: las propiedades de seguridad son muy diferentes.

### `uio_rw`: la dirección

La dirección de la transferencia. La enumeración se encuentra en el mismo archivo de cabecera:

```c
enum uio_rw {
        UIO_READ,
        UIO_WRITE
};
```

Para un handler `d_read`, `uio_rw` es `UIO_READ`. Para un handler `d_write`, `uio_rw` es `UIO_WRITE`. El campo le indica a `uiomove` si debe copiar de kernel a usuario (lectura) o de usuario a kernel (escritura). Algunos handlers añaden una aserción sobre esto como comprobación de cordura:

```c
KASSERT(uio->uio_rw == UIO_READ,
    ("Can't be in %s for write", __func__));
```

Esa aserción procede de `zero_read` en `/usr/src/sys/dev/null/null.c`. Es una forma económica de documentar el invariante. Tu driver no necesita aserciones como esta para ser correcto, pero pueden ser una red de seguridad útil durante el desarrollo.

### `uio_td`: el thread propietario

El `struct thread *` del llamador. Para un uio construido en nombre de un syscall, es el thread que realizó el syscall. Algunas APIs del kernel necesitan un puntero a thread; usar `uio->uio_td` en lugar de `curthread` mantiene la asociación explícita cuando el uio se pasa entre funciones.

En un `d_read` o `d_write` sencillo, raramente necesitarás `uio_td`. Resulta útil si tu driver quiere inspeccionar las credenciales del llamador durante la llamada, más allá de lo que `open(2)` ya validó. Eso es poco habitual.

### Una ilustración: qué le ocurre a uio durante una llamada read(fd, buf, 1024)

Recorrer un único `read(2)` ayuda a consolidar la comprensión de cómo se mueven los campos. Supón que un programa de usuario realiza la siguiente llamada:

```c
ssize_t n = read(fd, buf, 1024);
```

El kernel construye un uio que tiene este aspecto aproximado cuando llega a tu `d_read`:

- `uio_iov` apunta a una lista de una sola entrada.
- La única entrada tiene `iov_base = buf` (el buffer del usuario) e `iov_len = 1024`.
- `uio_iovcnt = 1`.
- `uio_offset = <la posición actual del puntero de archivo>`. Para un dispositivo posicionable recién abierto, cero.
- `uio_resid = 1024`.
- `uio_segflg = UIO_USERSPACE`.
- `uio_rw = UIO_READ`.
- `uio_td = <thread llamador>`.

Tu handler llama, por ejemplo, a `uiomove(sc->buf, 300, uio)`. Dentro de `uiomove`, el kernel:

- Toma la primera entrada iovec.
- Determina que 300 es menor que 1024, por lo que moverá 300 bytes.
- Llama a `copyout(sc->buf, buf, 300)`.
- Decrementa `iov_len` en 300, hasta 724.
- Avanza `iov_base` en 300, hasta `buf + 300`.
- Decrementa `uio_resid` en 300, hasta 724.
- Incrementa `uio_offset` en 300.

Tu handler devuelve cero. El kernel calcula el número de bytes como `1024 - 724 = 300` y devuelve 300 desde `read(2)`. El usuario ve 300 bytes en `buf[0..299]` y sabe que puede llamar de nuevo a `read(2)` para obtener el resto, o continuar con lo que tiene.

Eso es todo lo que hace `uiomove`, en orden. No hay ninguna magia.

### Cómo difiere `readv(2)`

Si el usuario llamó a `readv(fd, iov, 3)` con tres entradas iovec, el uio al comienzo de `d_read` tendría `uio_iovcnt = 3`, `uio_iov` apuntando a la lista de tres entradas y `uio_resid` igual a la suma de sus longitudes. Tu manejador realiza una única llamada a `uiomove` (o varias, en un bucle) y `uiomove` recorre la lista por ti. El código del driver es idéntico.

Esta es una de las ventajas silenciosas de la abstracción uio: las lecturas y escrituras scatter-gather son gratuitas. Tu driver fue escrito para un único buffer; ya gestiona peticiones de múltiples buffers.

### Reentrada en un manejador con un uio parcialmente consumido

Una convención que a veces desconcierta a los principiantes: **una única invocación de `d_read` o `d_write` puede realizar múltiples llamadas a `uiomove`**. Cada llamada reduce `uio_resid` y avanza `uio_iov`. El uio permanece coherente entre llamadas. Si el primer `uiomove` de tu manejador transfiere 128 bytes y el siguiente transfiere 256, el kernel simplemente ve una única llamada al manejador que transfirió 384 bytes.

Lo que **no** debes hacer es guardar un puntero uio entre llamadas al manejador e intentar retomarlo más tarde. Un uio es válido durante el tiempo de vida del despacho que lo produjo. Entre despachos, la memoria a la que apunta (incluido el array iovec) puede no ser válida. Si necesitas encolar una petición para procesarla más tarde, copia los datos necesarios del uio a tu propio buffer del kernel y utiliza tu propia cola.

### Qué debe leer tu driver y qué debe dejar intacto

Una guía de referencia rápida, en orden decreciente de frecuencia:

| Campo          | ¿Leerlo?                      | ¿Escribirlo?                                                              |
|----------------|-------------------------------|---------------------------------------------------------------------------|
| `uio_resid`    | Sí, con frecuencia            | Solo para marcar una transferencia como consumida (p. ej., `uio_resid = 0`) |
| `uio_offset`   | Sí, si lo tienes en cuenta    | No, deja que `uiomove` lo actualice                                       |
| `uio_rw`       | Ocasionalmente, para KASSERTs | No                                                                        |
| `uio_segflg`   | Raramente                     | No, salvo que construyas un uio en modo kernel                            |
| `uio_td`       | Raramente                     | No                                                                        |
| `uio_iov`      | Casi nunca                    | Nunca                                                                     |
| `uio_iovcnt`   | Casi nunca                    | Nunca                                                                     |

Si un driver de nivel principiante escribe en `uio_iov` o `uio_iovcnt`, algo ha ido seriamente mal. Si escribe en `uio_resid` de cualquier forma distinta al truco `uio_resid = 0` de «ya lo consumí todo», algo no está del todo bien. Si lee de las tres primeras filas, está en el camino correcto.

### Los campos de uio en la práctica

Todo esto resulta menos intimidante una vez que has visto a un manejador utilizarlo en la práctica. Las etapas myfirst de este capítulo inspeccionan `uio_resid` (para limitar las transferencias), leen ocasionalmente `uio_offset` (para saber en qué punto se encuentra el lector) y delegan todo lo demás en `uiomove`. Las funciones auxiliares realizan el trabajo real y el código del driver permanece reducido.

### El ciclo de vida de un único uio: tres instantáneas

Para afianzar el análisis campo a campo, merece la pena recorrer el estado de un uio en tres momentos de su vida: el instante en que se llama a tu manejador, el instante posterior a un `uiomove` parcial y el instante justo antes de que tu manejador retorne. Cada instantánea captura el mismo uio, de modo que puedes ver exactamente cómo evolucionan los campos.

El ejemplo es una llamada `read(fd, buf, 1024)` sobre un driver cuyo manejador de lectura servirá 300 bytes por llamada.

**Instantánea 1: a la entrada de `d_read`.**

```text
uio_iov     -> [ { iov_base = buf,       iov_len = 1024 } ]
uio_iovcnt  =  1
uio_offset  =  0        (this is the first read on the descriptor)
uio_resid   =  1024
uio_segflg  =  UIO_USERSPACE
uio_rw      =  UIO_READ
uio_td      =  <calling thread>
```

El uio describe una petición completa. El usuario solicitó 1024 bytes, el buffer está en espacio de usuario, la dirección es lectura y el desplazamiento es cero. Esto es lo que el kernel entrega a tu manejador.

**Instantánea 2: tras el retorno exitoso de `uiomove(sc->buf, 300, uio)`.**

```text
uio_iov     -> [ { iov_base = buf + 300, iov_len =  724 } ]
uio_iovcnt  =  1
uio_offset  =  300
uio_resid   =  724
uio_segflg  =  UIO_USERSPACE    (unchanged)
uio_rw      =  UIO_READ         (unchanged)
uio_td      =  <calling thread> (unchanged)
```

Cuatro campos cambiaron de forma coordinada. `iov_base` avanzó 300 posiciones para que la siguiente transferencia coloque bytes después de los que se acaban de escribir. `iov_len` se redujo en 300, porque la entrada iovec ahora describe solo los 724 bytes restantes. `uio_offset` creció en 300, porque la posición en el flujo avanzó 300 bytes. `uio_resid` se redujo en 300, porque se completaron 300 bytes de trabajo.

Tres campos permanecieron fijos: `uio_segflg`, `uio_rw` y `uio_td` describen la *forma* de la petición, que no cambia durante la transferencia. Si tu manejador necesita comprobar alguno de ellos, puede hacerlo antes o después de `uiomove` y obtendrá la misma respuesta.

**Instantánea 3: justo antes de que `d_read` retorne.**

Imagina que el manejador, tras servir 300 bytes, decide que no tiene más datos y retorna cero sin volver a llamar a `uiomove`.

```text
uio_iov     -> [ { iov_base = buf + 300, iov_len =  724 } ]
uio_iovcnt  =  1
uio_offset  =  300
uio_resid   =  724
uio_segflg  =  UIO_USERSPACE
uio_rw      =  UIO_READ
uio_td      =  <calling thread>
```

Idéntica a la Instantánea 2. El manejador no modificó nada; simplemente retornó. El kernel verá `uio_resid = 724` frente al valor inicial `uio_resid = 1024` y calculará `1024 - 724 = 300`, que devolverá al espacio de usuario como resultado de `read(2)`. El llamador recibe un valor de retorno de 300 y sabe que el driver produjo 300 bytes.

Si el manejador hubiera iterado sobre `uiomove` hasta que `uio_resid` llegara a cero, la instantánea al retornar tendría `uio_resid = 0` y el kernel devolvería 1024 al espacio de usuario (una transferencia completa). Si el manejador hubiera llamado a `uiomove` y obtuviera un error, `uio_resid` reflejaría el progreso parcial producido antes del fallo y el manejador devolvería el errno correspondiente.

### Qué te aporta este modelo mental

De las instantáneas se derivan tres observaciones que merece la pena nombrar explícitamente.

**Primero, uio_resid es el contrato.** El kernel confiará en cualquier valor que haya en `uio_resid` cuando tu manejador retorne. Si es menor que al inicio, se transfirieron algunos bytes; la diferencia es la cantidad de bytes. Si no cambió, no se transfirió nada; el valor de retorno será cero (EOF) o un errno (dependiendo de lo que haya devuelto tu manejador).

**Segundo, uiomove es lo único en lo que debes confiar para decrementar uio_resid.** Un driver que resta manualmente de `uio_resid` casi con toda seguridad está haciendo algo incorrecto; el manejo de fallos del kernel, el recorrido del iovec y las actualizaciones del desplazamiento están todos integrados en el camino de código de `uiomove`. Establecer `uio_resid = 0` es la única excepción, empleada por drivers como `null_write` de `null.c` para indicar «actúa como si se hubieran consumido todos los bytes».

**Tercero, el uio es espacio temporal.** Un uio no es un objeto de larga duración. Se crea por cada syscall, se va consumiendo a medida que `uiomove` lo procesa y se descarta cuando tu manejador retorna. Guardar un puntero uio para usarlo más tarde es un error de ciclo de vida esperando a producirse. Si tu driver necesita datos del uio más allá de la llamada actual, debe copiar los bytes a su propio almacenamiento (que es lo que hace `d_write`: copia los bytes a través de `uiomove` hacia `sc->buf`, sin dejar nada en el uio para después).

Estos tres hechos son el fundamento sobre el que se construye todo lo demás en el capítulo. Si los interiorizas, el resto de la maquinaria uio deja de parecer misteriosa.



## Transferencia segura de datos: `uiomove`, `copyin`, `copyout`

Las secciones anteriores describieron `struct uio` y nombraron `uiomove(9)` como la función que mueve bytes. Esta sección explica por qué existe esa función, qué hace internamente y cuándo un driver debería recurrir directamente a `copyin(9)` o `copyout(9)` en su lugar.

### Por qué el acceso directo a memoria es inseguro

Un proceso de usuario tiene su propio espacio de direcciones virtual. Cuando un proceso llama a `read(2)` con un puntero a un buffer, ese puntero es una dirección virtual en el espacio de direcciones del proceso. Puede referirse a una página de memoria que esté presente en RAM física, a una página que haya sido intercambiada al disco o a una página que no esté mapeada en absoluto. Incluso puede ser un puntero que el programa de usuario haya fabricado deliberadamente para intentar hacer que el kernel falle.

Desde el punto de vista del kernel, el espacio de direcciones del usuario no es directamente accesible. El kernel tiene su propio espacio de direcciones; un puntero de usuario entregado al kernel no tiene sentido como puntero del kernel. Incluso si el kernel puede resolver el puntero de usuario mediante la maquinaria de tablas de páginas, usarlo directamente es peligroso: la página podría generar un fallo, la protección de memoria podría ser incorrecta, la dirección podría quedar fuera de las regiones mapeadas del proceso, o el puntero podría haber sido construido para apuntar a memoria del kernel en un intento de filtrar o corromper datos.

El acceso directo a memoria, en otras palabras, no es una característica que el kernel obtenga de forma gratuita. Es un privilegio que debe ejercerse con cuidado, con cada acceso enrutado a través de funciones que saben cómo manejar fallos, comprobar la protección y mantener separados los espacios de direcciones del usuario y del kernel.

En FreeBSD, esas funciones son `copyin(9)` (de usuario a kernel), `copyout(9)` (de kernel a usuario) y `uiomove(9)` (en cualquier dirección, controlado por el uio).

### Qué hacen `copyin(9)` y `copyout(9)`

De `/usr/src/sys/sys/systm.h`:

```c
int copyin(const void * __restrict udaddr,
           void * __restrict kaddr, size_t len);

int copyout(const void * __restrict kaddr,
            void * __restrict udaddr, size_t len);
```

`copyin` toma un puntero al espacio de usuario, un puntero al espacio del kernel y una longitud. Copia `len` bytes del usuario al kernel. `copyout` es el proceso inverso: puntero del kernel, puntero del usuario, longitud. Copia del kernel al usuario.

Ambas funciones validan la dirección del usuario, provocan el acceso a la página de usuario si es necesario, realizan la copia y capturan cualquier fallo que se produzca. Devuelven cero en caso de éxito y `EFAULT` si la dirección del usuario era inválida o si la copia falló por cualquier otra razón. Nunca corrompen memoria silenciosamente; siempre completan la copia o informan del error.

Estas dos primitivas son el fundamento sobre el que se construyen todas las transferencias de memoria entre usuario y kernel. Son las que invoca `uiomove(9)` internamente cuando el uio está en espacio de usuario. Son las que utilizan `fubyte(9)`, `subyte(9)` y un puñado de otras funciones de conveniencia. Son las funciones en las que el kernel confía como frontera de seguridad.

### Qué hace `uiomove(9)`

`uiomove(9)` es un envoltorio sobre `copyin` / `copyout` que comprende la estructura uio. Su implementación es breve y merece la pena leerla; reside en `/usr/src/sys/kern/subr_uio.c`.

A grandes rasgos, el algoritmo es el siguiente:

1. Verificar la coherencia del uio: la dirección es válida, el resid no es negativo, el thread propietario es el thread actual si el segmento está en espacio de usuario.
2. Iterar: mientras el llamador haya pedido más bytes (`n > 0`) y el uio tenga capacidad restante (`uio->uio_resid > 0`), consumir la siguiente entrada iovec.
3. Para cada entrada iovec, calcular cuántos bytes mover (el mínimo entre la longitud de la entrada, la cantidad restante del llamador y el resid del uio), y llamar a `copyin` o `copyout` (para segmentos en espacio de usuario) o `bcopy` (para segmentos en espacio del kernel), según `uio_rw` y `uio_segflg`.
4. Avanzar `iov_base` e `iov_len` del iovec a medida que se mueven los bytes; decrementar `uio_resid` e incrementar `uio_offset`.
5. Si alguna copia falla, interrumpir el bucle y devolver el error.

La función devuelve cero en caso de éxito o un código errno en caso de fallo. El fallo más frecuente es `EFAULT` por un puntero de usuario inválido.

La propiedad fundamental de `uiomove` es que es **la única función que tu driver debería utilizar para mover bytes a través de un uio**. No `bcopy`, no `memcpy`, no `copyout`. El uio contiene la información que `uiomove` necesita para elegir la primitiva correcta, y el driver no tiene que adivinarla.

### Cuándo usar cada función

La división del trabajo es sencilla en la práctica.

Usa `uiomove(9)` en los manejadores `d_read` y `d_write` siempre que el uio describa la transferencia. Este es el caso más habitual con diferencia.

Usa `copyin(9)` y `copyout(9)` directamente cuando tengas un puntero de usuario procedente de un origen distinto de un uio. Ejemplos:

- Dentro de un manejador `d_ioctl`, para comandos de control que portan punteros al espacio de usuario como argumentos (Capítulo 25).
- Dentro de un thread del kernel que acepta datos suministrados por el usuario a través de un mecanismo que has construido tú mismo, no a través de un uio.
- Cuando se lee o escribe un fragmento pequeño de memoria de usuario de tamaño fijo que no es el objeto de la syscall.

**No** utilices `copyin` ni `copyout` dentro de `d_read` o `d_write` para obtener datos del iovec del uio. Pasa siempre por `uiomove`. El iovec no tiene por qué ser un único buffer contiguo, y aunque lo fuera, tu driver no tiene motivo para traspasar la abstracción uio y acceder a él directamente.

### Tabla de referencia rápida

| Situación                                                                                          | Herramienta preferida  |
|----------------------------------------------------------------------------------------------------|------------------------|
| Transferir bytes a través de un uio (lectura o escritura)                                          | `uiomove(9)`           |
| Transferir bytes a través de un uio, con un buffer de kernel fijo y offset automático              | `uiomove_frombuf(9)` |
| Leer un puntero de usuario conocido que no está contenido en un uio                                | `copyin(9)`            |
| Escribir en un puntero de usuario conocido que no está contenido en un uio                         | `copyout(9)`         |
| Leer una cadena terminada en nulo desde el espacio de usuario                                      | `copyinstr(9)`         |
| Leer un solo byte desde el espacio de usuario                                                      | `fubyte(9)`            |
| Escribir un solo byte en el espacio de usuario                                                     | `subyte(9)`            |

`fubyte` y `subyte` son de uso muy específico; la mayoría de los drivers nunca los necesitan. Se incluyen aquí para que los reconozcas. `copyinstr` resulta útil en ocasiones en rutas de control que reciben una cadena de usuario; no recurriremos a él en este capítulo.

### ¿Por qué no usar `memcpy` directamente?

A veces, un principiante se pregunta: "¿no podría simplemente convertir el puntero de usuario con un cast y copiar los bytes con `memcpy`?" La respuesta es un no rotundo, y merece la pena entender por qué.

`memcpy` asume que ambos punteros apuntan a memoria accesible en el espacio de direcciones actual. Un puntero de usuario no garantiza eso. En arquitecturas que separan los punteros de usuario y de kernel a nivel de hardware (SMAP en amd64, por ejemplo), la CPU rechazará el acceso. En arquitecturas que comparten el espacio de direcciones, el puntero podría ser inválido de todos modos, o podría apuntar a una página que ha sido enviada al área de intercambio, o a una página que el kernel tiene prohibido tocar. Ninguno de estos casos es seguro dentro de un `memcpy` ordinario; el fallo resultante causaría un pánico del sistema o filtraría información a través del límite de confianza.

Las primitivas del kernel `copyin` y `copyout` existen precisamente para manejar estos casos correctamente. Instalan un manejador de fallos antes del acceso, de modo que un puntero de usuario incorrecto devuelve `EFAULT` en lugar de provocar un pánico. Respetan SMAP y protecciones similares. Pueden esperar a que la página sea traída desde el área de intercambio. Nada de eso es opcional, y nada de eso es algo que tu driver deba replicar.

La regla práctica: si un puntero proviene del espacio de usuario, pásalo por `copyin` / `copyout` / `uiomove`. No lo desreferencies directamente. No uses `memcpy` a través de él. No lo pases a ninguna función que vaya a usar `memcpy` a través de él. Si te detienes en el límite de abstracción, el kernel te ofrece una interfaz estable, segura y bien documentada. Si lo cruzas, eres el dueño de cada bug para siempre.

### Qué ocurre ante un fallo

Algo concreto: ¿qué hace `uiomove` cuando el puntero de usuario es incorrecto?

El kernel instala un manejador de fallos antes de la copia, típicamente a través de su tabla de gestores de trampas. Cuando la CPU genera un fallo en el acceso de usuario, el gestor de fallos detecta que la instrucción problemática se encuentra dentro de un camino de código de `copyin` o `copyout`, salta al camino de retorno de error y devuelve `EFAULT`. Sin pánico. Sin corrupción de datos. El llamador de `uiomove` recibe un valor de retorno distinto de cero, lo propaga al llamador de `d_read` o `d_write`, y la syscall regresa a userland con `errno = EFAULT`.

El driver no necesita hacer nada especial para cooperar con este mecanismo. Solo necesita comprobar el valor de retorno de `uiomove` y propagar los errores. Haremos eso en todos los manejadores del capítulo.

### Alineación y seguridad de tipos

Una sutileza más que merece mencionarse. El buffer del usuario es un flujo de bytes. No lleva información de tipos. Si tu driver coloca una `struct` en el buffer y el usuario la extrae, el usuario obtiene bytes; esos bytes pueden estar o no correctamente alineados para un acceso a `struct` en la arquitectura del llamador.

Para `myfirst` el problema no surge, porque los bytes son texto arbitrario del usuario. Para drivers que quieran exportar datos estructurados, la convención es o bien exigir al usuario que copie los bytes con memcpy en una estructura local alineada antes de interpretarlos, o bien incluir una negociación explícita de alineación y versión en el formato de datos. `ioctl(2)` esquiva el problema porque su disposición de datos forma parte del número de comando `IOCTL`; `read` y `write` no tienen ese lujo.

Este es uno de los lugares donde resulta tentador, y a la vez incorrecto, añadir datos estructurados sobre `read`/`write`. Si tu driver quiere entregar datos tipados a userland, la interfaz `ioctl` o un mecanismo RPC externo son las herramientas adecuadas. `read` y `write` transportan bytes. Esa es la promesa, y la promesa es lo que los mantiene portables.

### Un pequeño ejemplo trabajado

Supón que un programa de usuario escribe cuatro enteros en tu dispositivo:

```c
int buf[4] = { 1, 2, 3, 4 };
write(fd, buf, sizeof(buf));
```

En el `d_write` del driver, el uio tiene el siguiente aspecto:

- Una entrada iovec con `iov_base = <dirección de usuario de buf[0]>`, `iov_len = 16`.
- `uio_resid = 16`, `uio_offset = 0`, `uio_segflg = UIO_USERSPACE`, `uio_rw = UIO_WRITE`.

Un manejador ingenuo podría llamar a `uiomove(sc->intbuf, 16, uio)`, donde `sc->intbuf` es `int sc->intbuf[4];`. `uiomove` emitirá un `copyin` que copia los 16 bytes. Si tiene éxito, `sc->intbuf` contendrá los cuatro enteros en el orden de bytes del programa llamador.

Pero atención: el usuario puede haber escrito esos enteros en el orden de bytes de una CPU completamente diferente si el driver alguna vez se utiliza entre arquitecturas distintas. El usuario puede haber usado `int32_t` donde el driver usó `int`. El usuario puede haber alineado la estructura de forma diferente. Para `myfirst` nada de esto importa porque tratamos los datos como bytes opacos. Para un driver que expone datos estructurados a través de `read`/`write`, estos problemas se multiplican rápidamente, y son la razón por la que la mayoría de los drivers reales o bien usan `ioctl` para las cargas estructuradas, o bien declaran un formato de transmisión explícito (orden de bytes, anchura de campos, alineación) en su documentación.

La lección: `uiomove` mueve bytes. No sabe ni le importa nada de los tipos. Tu driver tiene que decidir qué significan esos bytes.

### Un mini caso práctico: cuando una ida y vuelta de struct sale mal

Para que el punto "los bytes no son tipos" quede claro, recorramos un intento plausible, aunque erróneo, de exponer un contador del kernel a través de `read(2)` como una estructura tipada.

Supón que tu driver mantiene un conjunto de contadores:

```c
struct myfirst_stats {
        uint64_t reads;
        uint64_t writes;
        uint64_t errors;
        uint32_t flags;
};
```

Y supón que, con cierto optimismo, los expones a través de `d_read`:

```c
static int
stats_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_stats snap;

        mtx_lock(&sc->mtx);
        snap.reads  = sc->stat_reads;
        snap.writes = sc->stat_writes;
        snap.errors = sc->stat_errors;
        snap.flags  = sc->stat_flags;
        mtx_unlock(&sc->mtx);

        return (uiomove(&snap, sizeof(snap), uio));
}
```

A primera vista parece correcto. Los bytes llegan al espacio de usuario. Un lector puede convertir el buffer a un `struct myfirst_stats` y mirar los campos. El autor lo prueba en amd64, ve los valores correctos y publica el driver.

Hay tres problemas esperando ahí.

**Problema 1: relleno de la struct.** La disposición de `struct myfirst_stats` depende del compilador y la arquitectura. En amd64 con el ABI por defecto, `uint64_t` se alinea a 8 bytes, de modo que la struct ocupa 8 bytes para `reads`, 8 para `writes`, 8 para `errors`, 4 para `flags`, más 4 bytes de relleno final para redondear el tamaño a 32. El programa de usuario debe declarar una struct con el *mismo* relleno para leer los campos correctamente. Un programa de usuario que redeclare la struct con `#pragma pack(1)` o use una versión de compilador diferente interpretará mal los bytes y verá basura en `errors`.

**Problema 2: orden de bytes.** Una máquina amd64 almacena `uint64_t` en little-endian. Un programa de usuario que corre en la misma arquitectura decodifica correctamente. Un programa de usuario que corre de forma remota en una máquina big-endian, leyendo los bytes a través de una tubería de red, ve los enteros con los bytes invertidos. El driver no eligió un orden de bytes para la transmisión, de modo que el formato resulta dependiente de la CPU por accidente.

**Problema 3: atomicidad del snapshot.** El lector puede extraer los bytes con `uiomove` después de que `mtx_unlock` libere el mutex y antes de que el kernel devuelva el control al llamador. Entre esos dos momentos, los campos `snap.reads`, `snap.writes`, etc. ya están capturados en la variable local `snap` en la pila, de modo que *esa parte* está bien. Pero el ejemplo es lo suficientemente pequeño como para que el bug no aparezca; un snapshot más grande podría capturarse a través de varias adquisiciones de mutex y exhibir lecturas rotas.

**La solución no es "esforzarse más" con la disposición de la struct.** La solución es dejar de usar `read(2)` para datos estructurados. Existen dos opciones mejores:

- **`sysctl`**: el capítulo lo ha usado a lo largo de todo el texto. Los contadores individuales se exponen como nodos con nombre y tipo conocido. `sysctl(3)` en el lado del usuario devuelve enteros directamente; sin disposición de struct, sin relleno, sin orden de bytes.
- **`d_ioctl`**: el Capítulo 25 desarrolla `ioctl` en detalle. Para este caso de uso, un `ioctl` con una estructura de petición bien definida sería lo apropiado, y las macros `_IOR` / `_IOW` documentan el tamaño y la dirección.

La interfaz `read(2)` promete "un flujo de bytes que el driver define"; nada más. Si respetas la promesa, tu driver es portable, comprobable y resistente a la deriva silenciosa de la disposición. Si rompes la promesa exponiendo estructuras tipadas, heredas todas las trampas ABI que los protocolos de red tardaron décadas en aprender a evitar.

Para `myfirst` en este capítulo nunca llegamos al problema, porque solo movemos flujos de bytes opacos de un lado a otro. El objetivo del caso práctico es ayudarte a reconocer la forma del error antes de que alguien te pase un driver que ya lo está cometiendo.

### Resumen de esta sección

- Usa `uiomove(9)` dentro de `d_read` y `d_write`. Lee el uio, elige la primitiva correcta y gestiona los fallos de usuario y kernel por ti.
- Usa `uiomove_frombuf(9)` cuando quieras aritmética automática de desplazamiento dentro del buffer.
- Usa `copyin(9)` y `copyout(9)` solo cuando tengas un puntero de usuario fuera del contexto del uio, típicamente en `d_ioctl`.
- No desreferencies punteros de usuario directamente. Nunca.
- Comprueba los valores de retorno. Cualquier copia puede fallar con `EFAULT`, y tu manejador debe propagar el error.

Estas reglas son pocas, pero cubren casi todos los errores de seguridad en E/S que cometen los drivers de principiantes.



## Gestión de buffers internos en tu driver

Los manejadores de lectura y escritura son la superficie visible del camino de E/S de tu driver. Detrás de ellos, el driver necesita almacenar datos en algún lugar. Esta sección trata sobre cómo se diseña, asigna, protege y libera ese almacenamiento, al nivel accesible que este capítulo necesita. El Capítulo 10 extenderá el buffer hasta convertirlo en un anillo verdadero y lo hará seguro bajo carga concurrente; aquí nos quedamos deliberadamente por debajo de eso.

### Por qué necesitas buffers

Un buffer es almacenamiento temporal entre llamadas de E/S. Un driver lo usa por al menos tres razones:

1. **Ajuste de velocidad.** Productores y consumidores no llegan al mismo tiempo. Una escritura puede depositar bytes que una lectura posterior recogerá.
2. **Remodelado de peticiones.** Un usuario puede leer en unidades que no se alinean con la forma en que el driver produce datos. Un buffer absorbe la discrepancia.
3. **Aislamiento.** Los bytes dentro del buffer del driver son datos del kernel. No son punteros de usuario, ni direcciones DMA, ni están en una lista scatter-gather. Todo lo que hay en un buffer del kernel es accesible por el driver de forma segura y uniforme.

Para `myfirst`, el buffer es un pequeño trozo de almacenamiento en RAM. `d_write` escribe en él; `d_read` lee de él. El buffer es el estado del driver. El mecanismo uio es la fontanería que mueve los bytes hacia dentro y hacia fuera.

### Asignación estática frente a dinámica

Existen dos diseños razonables para decidir dónde vive el buffer.

**La asignación estática** coloca el buffer dentro de la estructura softc o como un array a nivel de módulo:

```c
struct myfirst_softc {
        ...
        char buf[4096];
        size_t bufused;
        ...
};
```

Ventajas: la asignación nunca falla, el tamaño es explícito y la vida útil está trivialmente ligada al softc. Inconvenientes: el tamaño queda fijado en tiempo de compilación; si más adelante quieres que sea ajustable, tendrás que refactorizar.

**La asignación dinámica** usa `malloc(9)` de un bucket `M_*`:

```c
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
```

Ventajas: el tamaño puede elegirse en tiempo de attach desde un sysctl o un parámetro ajustable; puede redimensionarse si se tiene cuidado. Inconvenientes: la asignación puede fallar (menos relevante con `M_WAITOK`, más con `M_NOWAIT`); el driver tiene un camino de liberación adicional que gestionar.

Para buffers pequeños, la asignación estática dentro del softc es la opción más sencilla, y la que el Capítulo 7 usó implícitamente al confiar en Newbus para asignar todo el softc. El Capítulo 9 usará asignación dinámica porque el buffer es lo suficientemente grande como para que incluirlo en el softc sea ligeramente costoso, y porque el patrón dinámico es el que usarás repetidamente más adelante en el libro.

### La llamada `malloc(9)`

El `malloc(9)` del kernel toma tres argumentos: el tamaño, un tipo malloc (que es una etiqueta que el kernel usa para contabilidad y depuración) y una palabra de flags. Una forma habitual:

```c
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
```

`M_DEVBUF` es el tipo malloc genérico para "buffer de dispositivo", definido en todo el árbol y apropiado para datos privados del driver que no merecen un tipo dedicado. Si tu driver crece lo suficiente como para justificar su propia etiqueta, puedes declarar una con `MALLOC_DECLARE(M_MYFIRST)` y `MALLOC_DEFINE(M_MYFIRST, "myfirst", "myfirst driver data")`, y usar `M_MYFIRST` en su lugar. Por ahora, `M_DEVBUF` está bien.

Los bits de flag más relevantes en esta etapa:

- `M_WAITOK`: la asignación puede dormir esperando memoria. En el contexto de attach, casi siempre es la opción correcta.
- `M_NOWAIT`: no duerme; devuelve `NULL` si la memoria escasea. Es necesario cuando te encuentras en un contexto que no puede dormir (un manejador de interrupciones, dentro de un lock que no permite dormir).
- `M_ZERO`: pone a cero la memoria antes de devolverla. Combínalo con `M_WAITOK` o `M_NOWAIT` según corresponda.

Una llamada con `M_WAITOK` sin `M_NOWAIT` tiene garantizado devolver un puntero válido en FreeBSD 14.3. El kernel dormirá y posiblemente activará la recuperación de memoria si es necesario, pero en la práctica no devolverá `NULL`. Aun así, comprobar si el valor es `NULL` es una medida defensiva que no cuesta nada; lo haremos.

### La llamada `free(9)` correspondiente

Cada `malloc(9)` tiene una llamada correspondiente a `free(9)`. La firma es:

```c
free(sc->buf, M_DEVBUF);
```

El tipo malloc que se pasa a `free` debe coincidir con el que se pasó a `malloc` para el mismo puntero. Pasar un tipo diferente corrompería la contabilidad del kernel y es uno de los fallos que los kernels compilados con `INVARIANTS` detectan en tiempo de ejecución.

El lugar donde poner el `free` depende de dónde estaba el `malloc`: attach reserva, detach libera. Si attach falla a medias, la ruta de desenrollado de errores libera todo lo que se había reservado antes del fallo. Vimos este patrón en el Capítulo 7; lo reutilizaremos aquí.

### Tamaño del buffer

Elegir el tamaño del buffer es una decisión de diseño. Para un driver de laboratorio, cualquier tamaño pequeño funciona. Algunas pautas:

- **Pequeño** (unos pocos cientos de bytes a unos pocos kilobytes): válido para demostraciones. Fácil de razonar. Las cargas de trabajo del usuario mayores que el buffer observarán `ENOSPC` o lecturas cortas rápidamente; esto es una característica para la enseñanza, no un fallo.
- **De tamaño de página** (4096 bytes): un valor predeterminado habitual y sensato. La asignación de memoria está alineada a páginas de forma gratuita, y muchas herramientas tratan 4 KiB como una unidad natural.
- **Mayor** (varios kilobytes hasta un megabyte): apropiado para drivers que esperan almacenar en el buffer una gran cantidad de datos. Recuerda que la memoria del kernel no es infinita; un driver desbocado que reserva un megabyte por cada apertura puede desestabilizar el sistema.

Para la Fase 2 de `myfirst` utilizaremos un buffer de 4096 bytes. Es lo suficientemente grande como para que quepa una prueba razonable (un párrafo de texto, unos pocos enteros), y lo suficientemente pequeño como para que el comportamiento de `ENOSPC` sea fácil de provocar desde una shell.

### Desbordamientos de buffer

El fallo más habitual en un driver que gestiona su propio buffer es escribir más allá del final del buffer. Este fallo es absolutamente fatal en el espacio del kernel. Un programa en espacio de usuario que desborda un buffer puede corromper su propio heap; un módulo del kernel que hace lo mismo puede corromper la memoria de otro subsistema, y el fallo (o, peor aún, el comportamiento incorrecto silencioso) puede manifestarse lejos del origen del problema.

La defensa es la disciplina aritmética. Cada vez que tu código vaya a escribir `N` bytes a partir del desplazamiento `O` en un buffer de tamaño `S`, verifica que `O + N <= S` antes de la escritura. En el manejador de la Fase 3 anterior, la expresión `towrite = MIN((size_t)uio->uio_resid, avail)` es exactamente esa comprobación: `towrite` queda limitado a `avail`, donde `avail` es `sc->buflen - sc->bufused`. No hay forma de superar `sc->buflen`.

Un fallo relacionado es la confusión entre valores con signo y sin signo. `uio_resid` es `ssize_t`; `sc->bufused` es `size_t`. Mezclarlos descuidadamente puede producir un valor negativo que se desborda al convertirlo a `size_t`, con resultados catastróficos. La macro `MIN` y un cast explícito a `(size_t)` valen la pequeña cantidad de ruido que añaden al código.

### Consideraciones sobre el locking

Si tu driver puede recibir llamadas desde más de un contexto de usuario simultáneamente, el buffer necesita un lock. Dos escritores simultáneos podrían generar una condición de carrera sobre `bufused`; dos lectores simultáneos podrían competir sobre los desplazamientos de lectura; un escritor y un lector podrían intercalar sus actualizaciones de estado de un modo que corrompiera ambos.

En `myfirst`, el campo `struct mtx mtx` que llevamos desde el Capítulo 7 es el lock que utilizaremos. Es un mutex ordinario de tipo `MTX_DEF`, lo que significa que puede mantenerse durante una llamada a `uiomove` (que puede dormir ante un fallo de página). Lo mantendremos durante cada actualización de `bufused` y durante el `uiomove` que transfiere bytes hacia o desde el buffer compartido.

La Parte 3 profundiza mucho más en la estrategia de locking. Por ahora, la regla es: **protege cualquier campo que más de un manejador pueda tocar al mismo tiempo**. En la Fase 3, eso son `sc->buf` y `sc->bufused`. Tu `fh` por apertura es por descriptor; no necesita el mismo lock, porque dos manejadores no pueden ejecutarse para el mismo descriptor concurrentemente en los casos que ejercitaremos.

### Una vista previa de los buffers circulares

El Capítulo 10 construye un ring buffer propiamente dicho: un buffer de tamaño fijo donde los punteros `head` y `tail` se persiguen el uno al otro. Se diferencia del buffer lineal que usamos en el Capítulo 9 en dos aspectos:

1. No necesita reiniciarse entre usos. Los punteros dan la vuelta; el buffer se reutiliza en el mismo lugar.
2. Puede admitir streaming en estado estacionario. Un buffer lineal se llena y luego rechaza escrituras; un ring buffer mantiene una ventana deslizante de datos recientes.

La Fase 3 de este capítulo *no* implementa un anillo. Implementa un buffer lineal en el que `d_write` añade datos y `d_read` extrae. Cuando el buffer está lleno, `d_write` devuelve `ENOSPC`; cuando está vacío, `d_read` devuelve cero bytes. Esto es suficiente para hacer funcionar correctamente la ruta de I/O sin la contabilidad adicional de un anillo. El Capítulo 10 añade esa contabilidad sobre las mismas formas de manejador.

### Una nota sobre la seguridad de thread del fh por descriptor

La `struct myfirst_fh` que tu driver reserva en `d_open` es por descriptor. Dos manejadores para el mismo descriptor no pueden ejecutarse concurrentemente en los escenarios que ejercita este capítulo (el kernel serializa las operaciones por archivo a través de la maquinaria de descriptores de archivo para los casos habituales), por lo que los campos dentro de `fh` no necesitan su propio lock. Dos manejadores para descriptores *diferentes* sí se ejecutan concurrentemente, pero tocan estructuras `fh` diferentes.

Este es un invariante tranquilizador, pero no absoluto. Un driver que pasa el puntero `fh` a un thread del kernel que se ejecuta en paralelo con el syscall debe añadir su propia sincronización. No haremos eso en este capítulo; por ahora el `fh` es seguro siempre que solo lo toques desde el interior del manejador al que se le dio el descriptor.

### Helpers del kernel que debes reconocer

Antes de continuar, vale la pena nombrar el pequeño conjunto de macros y funciones auxiliares que el capítulo ha estado usando. Están definidos en las cabeceras estándar de FreeBSD, y los principiantes a veces copian y pegan código que los usa sin saber de dónde vienen ni qué restricciones se aplican.

`MIN(a, b)` y `MAX(a, b)` están disponibles en el código del kernel a través de `<sys/libkern.h>`, que se incluye transitivamente mediante `<sys/systm.h>`. Evalúan cada argumento como máximo dos veces, por lo que `MIN(count++, limit)` es un fallo: `count` se incrementa dos veces. Un driver bien escrito evita los efectos secundarios dentro de los argumentos de `MIN`/`MAX`.

```c
towrite = MIN((size_t)uio->uio_resid, avail);
```

El cast explícito a `(size_t)` es parte del patrón, no un adorno estilístico. `uio_resid` es `ssize_t`, que tiene signo; `avail` es `size_t`, que no lo tiene. Sin el cast, el compilador elige un tipo para la comparación, y los compiladores modernos advierten cuando un valor con signo y uno sin signo se encuentran en el mismo `MIN` / `MAX`. El cast hace explícita la intención: ya hemos comprobado que `uio_resid` no es negativo (el kernel lo garantiza), por lo que el cast es seguro.

`howmany(x, d)`, definido en `<sys/param.h>`, calcula `(x + d - 1) / d`. Úsalo cuando necesites una división de techo. Un driver que reserva páginas para almacenar un recuento de bytes escribe frecuentemente:

```c
npages = howmany(buflen, PAGE_SIZE);
```

`rounddown(x, y)` y `roundup(x, y)` alinean `x` hacia abajo o hacia arriba hasta el múltiplo de `y` más cercano. `roundup2` y `rounddown2` son variantes más rápidas que solo funcionan cuando `y` es una potencia de dos. Así es como los drivers alinean los buffers a páginas o los desplazamientos a bloques.

`__DECONST(type, ptr)` elimina el `const` sin generar una advertencia del compilador. Es la forma elegante de decirle al compilador «sé que este puntero está declarado como `const`, pero he verificado que la función a la que llamo no modificará los datos, así que deja de quejarte». Se usó alrededor de `zero_region` en el `zero_read` de `null.c`; lo usamos en el `myfirst_read` de la Fase 1. Prefiere este cast a uno simple `(void *)`, porque señala la intención.

`curthread` es una macro específica de arquitectura (resuelta a través de un registro por CPU) que apunta al thread en ejecución en ese momento. `uio->uio_td` suele ser igual a `curthread` cuando el uio proviene de un syscall; ambos son intercambiables en ese contexto, pero el valor transportado por el uio es más autodocumentado.

`bootverbose` es un entero que se establece en un valor distinto de cero si el kernel arrancó con `-v` o si el operador lo activó mediante sysctl. Proteger las líneas de registro prolijas con `if (bootverbose)` es el modismo de FreeBSD para el registro de depuración que es visible bajo demanda pero silencioso por defecto.

Reconocer estos helpers cuando los encuentres en otros drivers acorta el tiempo que se tarda en leer código desconocido. Ninguno es exótico; todos forman parte del vocabulario estándar que se espera que un colaborador del kernel lea sin necesidad de buscarlos.



## Manejo de errores y casos límite

Un driver de principiante que «funciona en el camino feliz» es un driver que tarde o temprano acabará haciendo colapsar el kernel. La parte interesante del manejo de I/O son las partes que no son el camino feliz: lecturas de longitud cero, escrituras parciales, punteros de usuario inválidos, señales entregadas a mitad de llamada, buffers agotados y varias docenas de variaciones de todo ello. Esta sección recorre los casos más habituales y los valores errno que les corresponden.

### Los valores errno que importan para I/O

FreeBSD tiene un espacio errno amplio. Solo unos pocos son frecuentes en las rutas de I/O de los drivers; aprenderlos bien es más útil que repasar toda la lista por encima.

`0`: éxito. Devuelve esto cuando la transferencia se completó correctamente. El recuento de bytes es implícito en `uio_resid`.

`ENXIO` («Device not configured»): la operación no puede continuar porque el dispositivo no está en un estado utilizable. Devuélvelo desde `d_open`, `d_read` o `d_write` si falta el softc, `is_attached` es false, o el driver ha recibido la orden de cerrarse. Es el error idiomático de «el cdev existe pero el dispositivo subyacente no».

`EFAULT` («Bad address»): un puntero de usuario era inválido. Rara vez lo devuelves directamente; `uiomove(9)` lo devuelve en tu nombre cuando falla un `copyin`/`copyout`. Propágalo devolviendo el error que produzca `uiomove`.

`EINVAL` («Invalid argument»): algún parámetro no tiene sentido. Para una lectura o escritura, suele ser un desplazamiento fuera de rango (si tu driver respeta los desplazamientos) o una solicitud malformada. Evita usarlo como cajón de sastre.

`EAGAIN` («Resource temporarily unavailable»): la operación se bloquearía, pero se había establecido `O_NONBLOCK`. Para un `d_read` que no tiene datos, esta es la respuesta correcta en modo no bloqueante. Para un `d_write` que no tiene espacio, lo mismo. Lo manejaremos en la Fase 3.

`EINTR` («Interrupted system call»): se entregó una señal mientras el thread estaba bloqueado dentro del driver. Tu `d_read` puede devolver `EINTR` si un sleep fue interrumpido por una señal. El kernel entonces reintentará el syscall de forma transparente (según el flag `SA_RESTART`) o volverá al userland con `errno = EINTR`. Veremos el manejo de `EINTR` en el Capítulo 10; el Capítulo 9 no bloquea y, por tanto, no produce `EINTR`.

`EIO` («Input/output error»): un cajón de sastre para errores de hardware. Úsalo cuando tu driver se comunica con hardware real y el hardware reporta un fallo. Poco frecuente en `myfirst`, que no tiene hardware.

`ENOSPC` («No space left on device»): el buffer del driver está lleno y no puede aceptar más datos. La respuesta correcta para una escritura cuando no hay espacio. La Fase 3 devuelve esto.

`EPIPE` («Broken pipe»): usado por drivers similares a pipes cuando el extremo remoto ha cerrado. No es relevante para `myfirst`.

`ERANGE`, `EOVERFLOW`, `EMSGSIZE`: menos frecuentes en los drivers de caracteres; aparecen cuando el kernel o el driver quiere decir «el número que pediste está fuera de rango». No los usaremos en este capítulo.

### Fin de archivo en una lectura

Por convención, una lectura que devuelve cero bytes (porque `uiomove` no movió nada y tu manejador devolvió cero) es interpretada por el llamador como fin de archivo. Las shells, `cat`, `head`, `tail`, `dd` y la mayoría de las demás herramientas del sistema base dependen de esta convención.

La implicación para tu `d_read`: cuando tu driver no tiene más que entregar, devuelve cero. No devuelvas un errno. `uio_resid` debe seguir teniendo su valor original, porque no se movieron bytes.

En la Fase 1 y la Fase 2 de `myfirst`, el EOF se produce cuando el desplazamiento de lectura por descriptor ha alcanzado la longitud del buffer. `uiomove_frombuf` devuelve cero en ese caso de forma natural, por lo que no necesitamos un camino de código especial.

En la Etapa 3, donde `d_read` vacía un buffer al que `d_write` ha ido añadiendo datos, el comportamiento del EOF es más sutil: «no hay datos ahora mismo» no es lo mismo que «no habrá más datos nunca». Informaremos de «no hay datos ahora mismo» como una lectura de cero bytes. Un programa de usuario cuidadoso podría interpretar eso como EOF y detenerse; uno menos cuidadoso hará un bucle y volverá a llamar a `read(2)`. El Capítulo 10 presenta las estrategias adecuadas de lectura bloqueante o `poll(2)` que permiten a un programa de usuario esperar más datos sin realizar espera activa.

### Lecturas y escrituras de longitud cero

Una solicitud de longitud cero (`read(fd, buf, 0)` o `write(fd, buf, 0)`) es legal. Significa «no hagas nada, pero dime si podrías haber hecho algo». El kernel gestiona la mayor parte del despacho por ti: si `uio_resid` es cero en la entrada, cualquier llamada a `uiomove` es una operación nula, y tu handler devuelve cero. El llamante recibe una transferencia de cero bytes y ningún error.

Hay dos puntos sutiles. Primero, no trates `uio_resid == 0` como una condición de error. No lo es. Es una solicitud legítima. Segundo, no asumas que `uio_resid == 0` significa fin de archivo; solo indica que el llamante solicitó cero bytes. El EOF trata de que el driver se quede sin datos, no de que el llamante no pida ninguno.

### Transferencias parciales

Una lectura corta es aquella que devuelve menos bytes de los solicitados. Una escritura corta es aquella que consumió menos bytes de los ofrecidos. Ambas son legales y esperadas en la E/S de UNIX; los programas de usuario bien escritos las manejan con un bucle.

Tu driver es el árbitro definitivo de cuánto transferir. La familia de funciones `uiomove` transfiere como máximo `MIN(user_request, driver_offer)` bytes en una llamada. Si tu código llama a `uiomove(buf, 128, uio)` y el usuario pidió 1024, el kernel transfiere 128 y deja 896 en `uio_resid`. El llamante recibe un retorno de 128 bytes de `read(2)`.

Un programa de usuario mal escrito que no repita el bucle ante una E/S corta perderá bytes. Ese no es el problema de tu driver; UNIX funciona así desde 1971. Un driver bien escrito es aquel que devuelve conteos de bytes honestos (a través de `uio_resid`) y valores errno predecibles, incluso cuando se producen transferencias parciales.

### Cómo gestionar EFAULT devuelto por uiomove

Cuando `uiomove(9)` devuelve un error distinto de cero, el valor más habitual es `EFAULT`. Para cuando lo recibes, el kernel ya ha:

- Instalado un manejador de fallo alrededor de la copia.
- Observado el fallo.
- Deshecho la copia parcial.
- Devuelto `EFAULT` al llamante de `uiomove`.

Tu handler dispone de dos opciones para responder:

1. **Propagar el error**. Devuelve `EFAULT` (o el errno que se haya devuelto) desde `d_read` / `d_write`. Es la opción más sencilla y casi siempre la correcta.
2. **Ajustar el estado del driver y devolver éxito**. Si se transfirieron algunos bytes antes del fallo, `uio_resid` puede haberse reducido ya. El kernel reportará ese éxito parcial al espacio de usuario. Puede que quieras actualizar cualquier contador interno del driver que refleje hasta dónde llegó la transferencia.

En la práctica, la opción 1 es la respuesta universal, a menos que tengas una razón concreta para hacer más. La opción 2 añade complejidad que rara vez merece la pena.

### Programación defensiva para la entrada del usuario

Cada byte que un usuario escribe en tu dispositivo es no confiable. Suena dramático, y también es literalmente cierto. Un módulo del kernel que interpreta una escritura de usuario como una estructura y desreferencia un puntero de esa estructura es un módulo del kernel con una vulnerabilidad trivial de escritura arbitraria en memoria.

La regla general es: **trata los bytes de tu buffer como datos arbitrarios, no como una estructura tipada, a menos que hayas elegido deliberadamente un formato de transferencia que validas en cada límite**. Para `myfirst` esto es sencillo, porque nunca interpretamos los bytes; son solo carga útil. Para drivers que exponen una interfaz de escritura estructurada (por ejemplo, un driver que permite a los usuarios configurar el comportamiento mediante escrituras), el camino defensivo es:

- Validar la longitud de la escritura frente al tamaño de mensaje esperado.
- Copiar los bytes en una estructura en espacio del kernel (no en un puntero de usuario).
- Validar cada campo de esa estructura antes de actuar sobre él.
- No almacenar nunca un puntero de usuario dentro de tu driver para un uso posterior.

Estas reglas son más fáciles de seguir de lo que parecen, pero es fácil romperlas sin darse cuenta. El Capítulo 25 las revisará cuando examinemos el diseño de `ioctl`. Por ahora, el listón es bajo: tu driver `myfirst` debe copiar bytes a través de `uiomove`, no interpretarlos.

### Registro de errores frente a fallos silenciosos

Cuando un handler devuelve un errno, el error se propaga al espacio de usuario como el valor `errno` de la syscall fallida. La mayoría de los programas de usuario lo verán allí y lo reportarán. Algunos lo ignorarán.

Durante el desarrollo de drivers, también es útil registrar los errores significativos en `dmesg`. `device_printf(9)` es la herramienta adecuada, ya que etiqueta cada línea con el nombre del dispositivo Newbus para que puedas identificar qué instancia produjo el mensaje. Un ejemplo de la Etapa 3:

```c
if (avail == 0) {
        mtx_unlock(&sc->mtx);
        if (bootverbose)
                device_printf(sc->dev, "write rejected: buffer full\n");
        return (ENOSPC);
}
```

La guarda `if (bootverbose)` es un idioma habitual de FreeBSD para el registro detallado: solo imprime si el kernel se arrancó con el indicador `-v` o si se activó el sysctl `bootverbose`, lo que mantiene los registros de producción silenciosos y al mismo tiempo ofrece a los desarrolladores una forma de ver detalles.

No registres cada error en cada llamada; eso genera ruido en el registro, lo que dificulta encontrar los problemas reales. Registra la primera aparición de una condición, o hazlo periódicamente, o solo bajo `bootverbose`. La elección depende del driver. Para `myfirst`, un solo registro por transición (buffer vacío, buffer lleno) es suficiente.

### Previsibilidad y facilidad de uso

Un principiante que escribe un driver a menudo se centra en hacer el camino habitual rápido. Un autor de drivers más experimentado se centra en hacer los caminos de error previsibles. La diferencia es esta: cuando un operador utiliza tu driver y algo falla, el valor errno, el mensaje del registro y la reacción en el espacio de usuario deben formar una historia clara. Si `read(2)` devuelve `-1` con `errno = EIO` y los registros están en silencio, el operador no tiene por dónde empezar. Si los registros dicen «myfirst0: read failed, device detached» y el usuario recibe `ENXIO`, la historia se explica sola.

Apunta a eso. Devuelve el errno correcto. Registra la causa subyacente una sola vez. Haz que las transferencias parciales sean honestas. Nunca descartes datos en silencio.

### Una tabla breve de convenciones

| Situación en d_read                                          | Retorno        |
|--------------------------------------------------------------|----------------|
| Sin datos que entregar, pero podrían llegar más tarde        | `0` con `uio_resid` sin cambios |
| Sin datos, y nunca habrá más (EOF)                           | `0` con `uio_resid` sin cambios |
| Algunos datos entregados, otros no                           | `0` con `uio_resid` reflejando el resto |
| Entrega completa                                             | `0` con `uio_resid = 0` |
| Puntero de usuario no válido                                 | `EFAULT` (de `uiomove`) |
| Dispositivo no disponible / desconectándose                  | `ENXIO` |
| No bloqueante, bloquearía                                    | `EAGAIN` |
| Error de hardware                                            | `EIO` |

| Situación en d_write                                         | Retorno        |
|--------------------------------------------------------------|----------------|
| Aceptación completa                                          | `0` con `uio_resid = 0` |
| Aceptación parcial                                           | `0` con `uio_resid` reflejando el resto |
| Sin espacio, bloquearía                                      | `EAGAIN` (no bloqueante) o bloqueo (bloqueante) |
| Sin espacio, de forma permanente                             | `ENOSPC` |
| Puntero no válido                                            | `EFAULT` (de `uiomove`) |
| Dispositivo no disponible                                    | `ENXIO` |
| Error de hardware                                            | `EIO` |

Ambas tablas son breves a propósito. La mayoría de los drivers utilizan solo cuatro o cinco valores errno en total. Cuanto más clara sea tu historia de errores, mejor será trabajar con tu driver.



## Evolución de tu driver: las tres etapas

Con la teoría asentada, pasamos al código. Esta sección recorre tres etapas de `myfirst`, cada una pequeña, cada una un driver completo que carga, se ejecuta y ejercita un patrón de E/S específico.

Las etapas están diseñadas para construirse una sobre la otra:

- **Etapa 1** añade un camino de lectura que sirve un mensaje fijo en espacio del kernel. Este es `myfirst_read` en su forma más sencilla.
- **Etapa 2** añade un camino de escritura que deposita datos del usuario en un buffer del kernel, y un camino de lectura que lee desde ese mismo buffer. El tamaño del buffer se fija en el momento del attach y no da la vuelta.
- **Etapa 3** convierte la Etapa 2 en un buffer FIFO (primero en entrar, primero en salir), de modo que las escrituras añaden datos y las lecturas los consumen, y el driver puede servir un flujo continuo (aunque finito).

Las tres etapas parten del código de la etapa 2 del Capítulo 8. El sistema de build (`Makefile`) no cambia. Los handlers `attach` y `detach` crecen ligeramente en cada etapa. La forma de `cdevsw`, los métodos Newbus, la fontanería del `fh` por apertura y el árbol sysctl permanecen igual. Pasarás la mayor parte del tiempo examinando `d_read` y `d_write`.

### Etapa 1: un lector de mensaje estático

El driver de la Etapa 1 guarda un mensaje fijo en memoria del kernel y lo sirve a los lectores. `d_read` usa `uiomove_frombuf(9)` para entregar el mensaje. `d_write` permanece como un stub: devuelve éxito sin consumir ningún byte. Esta etapa es el puente entre el stub del Capítulo 8 y un lector real; introduce `uiomove_frombuf` en el contexto más sencillo posible.

Añade un par de campos al softc para almacenar el mensaje y su longitud, y un offset por descriptor al `fh`:

```c
struct myfirst_softc {
        /* ...existing Chapter 8 fields... */

        const char *message;
        size_t      message_len;
};

struct myfirst_fh {
        struct myfirst_softc *sc;
        uint64_t              reads;
        uint64_t              writes;
        off_t                 read_off;
};
```

En `myfirst_attach`, inicializa el mensaje:

```c
static const char myfirst_message[] =
    "Hello from myfirst.\n"
    "This is your first real read path.\n"
    "Chapter 9, Stage 1.\n";

sc->message = myfirst_message;
sc->message_len = sizeof(myfirst_message) - 1;
```

Observa el `- 1`: no queremos servir el byte NUL terminador al espacio de usuario. Los archivos de texto no llevan un NUL al final, y tampoco debería hacerlo un dispositivo que se comporte como uno.

El nuevo `myfirst_read`:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        before = uio->uio_offset;
        error = uiomove_frombuf(__DECONST(void *, sc->message),
            sc->message_len, uio);
        if (error == 0)
                fh->reads += (uio->uio_offset - before);
        fh->read_off = uio->uio_offset;
        return (error);
}
```

Hay dos detalles en los que merece la pena detenerse.

Primero, `uio->uio_offset` es la posición en el flujo por descriptor. El kernel la mantiene entre llamadas, avanzándola a medida que `uiomove_frombuf` mueve bytes. El primer `read(2)` sobre un descriptor recién abierto comienza en el offset cero; cada `read(2)` posterior comienza donde terminó el anterior. Cuando el offset alcanza `sc->message_len`, `uiomove_frombuf` devuelve cero sin mover ningún byte, y el llamante ve EOF.

Segundo, `before` captura `uio->uio_offset` en la entrada para poder calcular cuántos bytes se han movido. Después de que `uiomove_frombuf` retorna, la diferencia es el tamaño de la transferencia, y la añadimos al contador `reads` por descriptor. Aquí es donde el campo `fh->reads` del Capítulo 8 por fin demuestra su utilidad.

El cast `__DECONST` es un idioma de FreeBSD para eliminar el calificador `const`. `uiomove_frombuf` acepta un `void *` sin `const` porque está preparada para mover en cualquier dirección, pero en este contexto sabemos que la dirección es de kernel a usuario (una lectura), por lo que sabemos que el buffer del kernel no será modificado. Eliminar el `const` aquí es seguro; usar un cast simple `(void *)` también funcionaría, pero documenta menos la intención.

`myfirst_write` permanece tal como la dejó el Capítulo 8 para la Etapa 1:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_fh *fh;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        (void)fh;
        uio->uio_resid = 0;
        return (0);
}
```

Las escrituras se aceptan y se descartan, con la forma de `/dev/null`. La Etapa 2 cambiará esto.

Compila y carga. Una prueba rápida desde userland:

```sh
% cat /dev/myfirst/0
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
%
```

Una segunda lectura desde el mismo descriptor devuelve EOF, porque el offset ya ha sobrepasado el final del mensaje:

```sh
% cat /dev/myfirst/0 /dev/myfirst/0
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
```

Un momento: `cat` lee el mensaje dos veces. Esto se debe a que `cat` abre el archivo dos veces (una por argumento) y cada apertura obtiene un descriptor nuevo con su propio `uio_offset`. Si quieres verificar que dos aperturas realmente ven offsets independientes, abre el dispositivo desde un pequeño programa en C y lee más de una vez desde el mismo descriptor:

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int
main(void)
{
        int fd = open("/dev/myfirst/0", O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        char buf[64];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
                fwrite(buf, 1, n, stdout);
        }
        close(fd);
        return 0;
}
```

El primer `read(2)` devuelve el mensaje; el segundo devuelve cero (EOF); el programa termina. Esto confirma que `uio_offset` se mantiene por descriptor.

La Etapa 1 es intencionadamente breve. Introduce tres ideas (el helper `uiomove_frombuf`, los offsets por descriptor y el idioma `__DECONST`) sin sobrecargar al lector. El resto del capítulo construye sobre esto.

### Etapa 2: un buffer de escritura única y múltiple lectura

La Etapa 2 extiende el driver para aceptar escrituras. El driver asigna un buffer del kernel en el attach, las escrituras depositan datos en él y las lecturas los extraen. No hay circulación: una vez que el buffer se llena, las escrituras adicionales devuelven `ENOSPC`. Las lecturas ven todo lo que se ha escrito hasta ese momento, comenzando desde su propio offset por descriptor.

La estructura `myfirst_softc` crece con unos pocos campos más:

```c
struct myfirst_softc {
        /* ...existing Chapter 8 fields... */

        char    *buf;
        size_t   buflen;
        size_t   bufused;

        uint64_t bytes_read;
        uint64_t bytes_written;
};
```

`buf` es el puntero devuelto por `malloc(9)`. `buflen` es su tamaño, una constante en tiempo de compilación por simplicidad; puedes hacerlo configurable más adelante. `bufused` es la marca de nivel máximo: el número de bytes que se han escrito hasta el momento.

Dos nuevos nodos sysctl para observabilidad:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bytes_written", CTLFLAG_RD,
    &sc->bytes_written, 0, "Total bytes written into the buffer");

SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bufused", CTLFLAG_RD,
    &sc->bufused, 0, "Current byte count in the buffer");
```

`bufused` es de tipo `size_t`, y la macro sysctl para enteros sin signo es `SYSCTL_ADD_UINT` en plataformas de 32 bits o `SYSCTL_ADD_U64` en plataformas de 64 bits. Dado que este driver está pensado para FreeBSD 14.3 en amd64 en el laboratorio habitual, `SYSCTL_ADD_UINT` es suficiente; el campo se presentará como `unsigned int` aunque el tipo interno sea `size_t`. Si apuntas a arm64 u otra plataforma de 64 bits, usa `SYSCTL_ADD_U64` y realiza el cast correspondiente.

Asigna el buffer en `attach`:

```c
#define MYFIRST_BUFSIZE 4096

sc->buflen = MYFIRST_BUFSIZE;
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
if (sc->buf == NULL) {
        error = ENOMEM;
        goto fail_mtx;
}
sc->bufused = 0;
```

Libéralo en `detach`:

```c
if (sc->buf != NULL) {
        free(sc->buf, M_DEVBUF);
        sc->buf = NULL;
}
```

Ajusta el desenrollado de errores en `attach` para incluir la liberación del buffer:

```c
fail_dev:
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        destroy_dev(sc->cdev);
        sysctl_ctx_free(&sc->sysctl_ctx);
        free(sc->buf, M_DEVBUF);
        sc->buf = NULL;
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
```

Ahora el manejador de lectura:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        size_t have;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        have = sc->bufused;
        before = uio->uio_offset;
        error = uiomove_frombuf(sc->buf, have, uio);
        if (error == 0) {
                sc->bytes_read += (uio->uio_offset - before);
                fh->reads += (uio->uio_offset - before);
        }
        fh->read_off = uio->uio_offset;
        mtx_unlock(&sc->mtx);
        return (error);
}
```

El manejador de lectura toma el mutex para leer `bufused` de forma consistente y, a continuación, llama a `uiomove_frombuf` con el nivel máximo alcanzado como tamaño efectivo del buffer. Un lector que se ejecute antes de que se haya producido cualquier escritura verá `have = 0`, y `uiomove_frombuf` devolverá cero, lo que el llamador interpretará como EOF. Un lector que se ejecute después de algunas escrituras verá el `bufused` actual y recibirá hasta ese número de bytes.

El manejador de escritura:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        avail = sc->buflen - sc->bufused;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + sc->bufused, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                sc->bytes_written += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

Fíjate en el límite: `towrite = MIN(uio->uio_resid, avail)`. Si el usuario intenta escribir 8 KiB y solo quedan 512 bytes de espacio, aceptamos 512 bytes y dejamos que el kernel informe de una escritura parcial de 512 al espacio de usuario. Un llamador bien escrito iterará con los bytes restantes; uno menos cuidadoso perderá el excedente. Esa es responsabilidad del llamador; el driver ha hecho su parte con honestidad.

Prueba de humo desde userland:

```sh
% sudo kldload ./myfirst.ko
% echo "hello" | sudo tee /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
hello
% echo "more" | sudo tee -a /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
hello
more
% sysctl dev.myfirst.0.stats.bufused
dev.myfirst.0.stats.bufused: 11
%
```

El buffer creció 6 bytes por `"hello\n"` y 5 más por `"more\n"`, dando un total de 11 bytes. `cat` lee los 11 bytes completos. Un segundo `cat` abierto desde cero comienza en el desplazamiento cero y los vuelve a leer.

¿Qué ocurre si intentamos escribir más de lo que el buffer puede almacenar?

```sh
% dd if=/dev/zero bs=1024 count=8 | sudo tee /dev/myfirst/0 > /dev/null
dd: stdout: No space left on device
tee: /dev/myfirst/0: No space left on device
8+0 records in
7+0 records out
```

`dd` escribió 7 bloques de 1024 bytes antes de que el octavo fallara. `tee` informa del error. El driver aceptó hasta su límite y devolvió `ENOSPC` de forma limpia. El kernel propagó el valor de errno de vuelta al espacio de usuario.

### Etapa 3: un driver echo con cola FIFO

La etapa 3 convierte el buffer en una FIFO. Las escrituras añaden datos al final. Las lecturas los extraen desde el principio. Cuando el buffer está vacío, las lecturas devuelven cero bytes (EOF en vacío). Cuando el buffer está lleno, las escrituras devuelven `ENOSPC`.

El buffer sigue siendo lineal: sin desplazamiento circular. Después de una lectura que agota todos los datos, `bufused` vale cero y la siguiente escritura comienza de nuevo en el offset cero de `sc->buf`. Esto mantiene la contabilidad al mínimo y permite que la etapa se centre en el cambio de dirección de I/O en lugar de en la mecánica de un ring buffer.

El softc gana un campo más:

```c
struct myfirst_softc {
        /* ...existing fields... */

        size_t  bufhead;   /* index of next byte to read */
        size_t  bufused;   /* bytes in the buffer, from bufhead onward */

        /* ...remaining fields... */
};
```

`bufhead` es el offset del primer byte pendiente de leer. `bufused` es el número de bytes válidos a partir de `bufhead`. El invariante `bufhead + bufused <= buflen` se cumple siempre.

Reinicia ambos en `attach`:

```c
sc->bufhead = 0;
sc->bufused = 0;
```

Nuevo manejador de lectura:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t toread;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        if (sc->bufused == 0) {
                mtx_unlock(&sc->mtx);
                return (0); /* EOF-on-empty */
        }
        toread = MIN((size_t)uio->uio_resid, sc->bufused);
        error = uiomove(sc->buf + sc->bufhead, toread, uio);
        if (error == 0) {
                sc->bufhead += toread;
                sc->bufused -= toread;
                sc->bytes_read += toread;
                fh->reads += toread;
                if (sc->bufused == 0)
                        sc->bufhead = 0;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

Hay algunos detalles que difieren de la etapa 2. La lectura ya no respeta `uio->uio_offset`; el offset por descriptor carece de sentido en una FIFO donde todos los descriptores ven el mismo flujo y ese flujo desaparece a medida que se consume. Cuando `bufused` llega a cero, reiniciamos `bufhead` a cero, lo que mantiene la siguiente escritura alineada al inicio del buffer y evita que los datos se acumulen hacia el final.

Este truco de "colapso al vaciar" no es un ring buffer, pero se le acerca lo suficiente para ilustrar una FIFO pedagógica. El paso de realineación adicional es `O(1)` y prácticamente no tiene coste.

Nuevo manejador de escritura (prácticamente igual al de la etapa 2, pero observa dónde añade los datos):

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, tail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        tail = sc->bufhead + sc->bufused;
        avail = sc->buflen - tail;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + tail, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                sc->bytes_written += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

La escritura añade en `sc->bufhead + sc->bufused`, y no solo en `sc->bufused`, porque el segmento de datos válidos se ha desplazado a medida que las lecturas lo han ido consumiendo.

Prueba rápida:

```sh
% echo "one" | sudo tee /dev/myfirst/0 > /dev/null
% echo "two" | sudo tee -a /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
one
two
% cat /dev/myfirst/0
%
```

Tras el primer `cat`, el buffer queda vacío. El segundo `cat` no encuentra datos y termina de inmediato.

Esta es la forma de la etapa 3. El driver es una FIFO en memoria pequeña y honesta. Los usuarios pueden introducir bytes, extraerlos y observar los contadores desde sysctl. Eso es I/O real, y es el punto de partida sobre el que el capítulo 10 construye.



## Trazando un `read(2)` desde el espacio de usuario hasta tu manejador

Antes de empezar con los laboratorios, da un vistazo paso a paso a lo que ocurre exactamente cuando un programa de usuario llama a `read(2)` en uno de tus nodos. Entender este recorrido es una de esas cosas que cambia la forma en que lees el código de un driver. Cada manejador que encuentras en el árbol de código fuente está sentado al final de la cadena de llamadas que se describe a continuación; una vez que reconoces esa cadena, todos los manejadores empiezan a resultarte familiares.

### Paso 1: el programa de usuario llama a `read(2)`

El wrapper de `read` de la biblioteca C es una traducción delgada de la llamada en una trampa de syscall: coloca el descriptor de archivo, el puntero al buffer y el recuento en los registros correspondientes, y ejecuta la instrucción de trampa para la arquitectura actual. El control pasa al kernel.

Esta parte no tiene nada que ver con los drivers. Es la misma para toda syscall. Lo que importa es que el kernel está ahora ejecutándose en nombre del proceso de usuario, en el espacio de direcciones del kernel, con los registros del usuario guardados y las credenciales del proceso accesibles a través de `curthread->td_ucred`.

### Paso 2: el kernel busca el descriptor de archivo

El kernel llama a `sys_read(2)` (en `/usr/src/sys/kern/sys_generic.c`), que valida los argumentos, busca el descriptor de archivo en la tabla de archivos del proceso llamador y adquiere una referencia sobre el `struct file` resultante.

Si el descriptor no está abierto, la llamada falla aquí con `EBADF`. Si el descriptor está abierto pero no es legible (por ejemplo, el usuario abrió el dispositivo con `O_WRONLY`), la llamada también falla con `EBADF`. El driver no interviene; es `sys_read` quien impone el modo de acceso.

### Paso 3: el vector de operaciones de archivo genérico realiza el despacho

El `struct file` tiene una etiqueta de tipo de archivo (`f_type`) y un vector de operaciones de archivo (`f_ops`). Para un archivo regular, el vector despacha a la capa VFS; para un socket, despacha a los sockets; para un dispositivo abierto a través de devfs, despacha a `vn_read`, que a su vez llama a la operación de vnode `VOP_READ` sobre el vnode detrás del archivo.

Puede parecer indirección por el mero hecho de serlo. En realidad es la forma en que el kernel mantiene el resto del recorrido de la syscall idéntico para cualquier tipo de archivo. Los drivers no necesitan conocer esta capa; devfs y VFS acaban entregando la llamada a tu manejador.

### Paso 4: VFS llama a devfs

Las operaciones de sistema de archivos del vnode apuntan a la implementación de devfs de la interfaz de vnode (`devfs_vnops`). `VOP_READ` sobre un vnode de devfs llama a `devfs_read_f`, que mira el cdev detrás del vnode, adquiere una referencia de recuento de threads sobre él (incrementando `si_threadcount`), y llama a `cdevsw->d_read`. Esa es tu función.

Dos detalles de este paso tienen implicaciones para tu driver.

El primero: **el incremento de `si_threadcount` es lo que `destroy_dev(9)` usa para saber que tu manejador está activo**. Cuando se descarga un módulo y se ejecuta `destroy_dev`, este espera hasta que cada invocación en curso de cada manejador retorne. La referencia se incrementa antes de llamar a tu `d_read` y se libera después de que retorne. Este mecanismo es la razón por la que tu driver puede descargarse de forma segura mientras un usuario está en medio de un `read(2)`.

El segundo: **la llamada es síncrona desde el punto de vista de la capa VFS**. VFS llama a tu manejador, espera a que retorne y luego propaga el resultado. No necesitas hacer nada especial para participar en esta sincronización; simplemente retorna desde tu manejador cuando hayas terminado.

### Paso 5: tu manejador `d_read` se ejecuta

Aquí es donde hemos estado a lo largo de todo el capítulo. El manejador:

- Recibe un `struct cdev *dev` (el nodo que se está leyendo), un `struct uio *uio` (la descripción de I/O) y un `int ioflag` (indicadores de la entrada de la tabla de archivos).
- Recupera el estado por apertura mediante `devfs_get_cdevpriv(9)`.
- Verifica que el driver sigue activo.
- Transfiere bytes a través de `uiomove(9)`.
- Retorna cero o un código errno.

Nada de este paso debería resultar misterioso a estas alturas.

### Paso 6: el kernel deshace la pila y notifica el resultado

`devfs_read_f` recibe tu valor de retorno. Si es cero, calcula el recuento de bytes a partir de la disminución de `uio->uio_resid` y devuelve dicho recuento. Si no es cero, convierte el errno en el valor de error de la syscall. El `vn_read` de VFS pasa el resultado hacia arriba a `sys_read`. `sys_read` escribe el resultado en el registro de valor de retorno.

El control vuelve al espacio de usuario. El wrapper de `read` de la biblioteca C examina el resultado: un valor positivo se devuelve como valor de retorno de `read(2)`; un valor negativo establece `errno` y devuelve `-1`.

El programa de usuario ve el entero que esperaba y su flujo de control continúa.

### Paso 7: los recuentos de referencias se deshacen

A la salida, `devfs_read_f` libera la referencia de recuento de threads sobre el cdev. Si `destroy_dev(9)` estaba esperando a que `si_threadcount` llegara a cero, puede ahora proceder con el desmontaje.

Esta es la razón por la que toda la cadena está estructurada con tanto cuidado. Cada referencia va emparejada; cada incremento tiene su decremento correspondiente; cada fragmento de estado que toca el manejador es propiedad del manejador, del softc o del `fh` por apertura. Si alguno de esos invariantes se rompe, la descarga deja de ser segura.

### Por qué este rastreo te importa

Tres conclusiones.

**La primera**: el mecanismo descrito es la razón por la que tu manejador no necesita hacer nada especial para coexistir con la descarga del módulo. Siempre que retornes de `d_read` en un tiempo finito, el kernel dejará que tu driver se descargue limpiamente. Esta es parte de la razón por la que el capítulo 9 mantiene todas las lecturas no bloqueantes a nivel de driver.

**La segunda**: cada capa entre `read(2)` y tu manejador la prepara el kernel antes de que tu código se ejecute. El buffer del usuario es válido (o `uiomove` reportará `EFAULT`), el cdev está activo (o devfs habría rechazado la llamada), el modo de acceso es compatible con el descriptor (o `sys_read` habría rechazado la llamada), y las credenciales del proceso corresponden al thread actual. Puedes centrarte en la tarea de tu driver y confiar en las capas.

**La tercera**: cuando leas un driver desconocido en el árbol y su `d_read` parezca extraño, puedes recorrer la cadena en sentido inverso. ¿Quién llamó a este manejador? ¿Qué estado prepararon? ¿Qué invariantes promete mi manejador al retornar? La cadena te lo dice. Las respuestas suelen ser las mismas que para `myfirst`.

### El espejo: rastrear un `write(2)`

Una escritura sigue el mismo tipo de cadena, en sentido inverso. Un desglose completo en siete pasos sería en su mayor parte una reafirmación del rastreo de lectura con palabras sustituidas, por lo que el párrafo siguiente es deliberadamente comprimido.

El usuario llama a `write(fd, buf, 1024)`. La biblioteca C lanza la trampa al kernel. `sys_write(2)` en `/usr/src/sys/kern/sys_generic.c` valida los argumentos, busca el descriptor y adquiere una referencia sobre su `struct file`. El vector de operaciones de archivo despacha a `vn_write`, que llama a `VOP_WRITE` sobre el vnode de devfs. `devfs_write_f` en `/usr/src/sys/fs/devfs/devfs_vnops.c` adquiere la referencia de recuento de threads sobre el cdev, compone el `ioflag` a partir de `fp->f_flag` y llama a `cdevsw->d_write` con el uio que describe el buffer del llamador.

Tu manejador `d_write` se ejecuta. Recupera el estado por apertura mediante `devfs_get_cdevpriv(9)`, comprueba que el driver sigue activo, toma el lock que el driver necesita alrededor del buffer, limita la longitud de la transferencia al espacio disponible y llama a `uiomove(9)` para copiar bytes del espacio de usuario al buffer del kernel. Si tiene éxito, el manejador actualiza su contabilidad y retorna cero. `devfs_write_f` libera la referencia de recuento de threads. `vn_write` deshace la pila a través de `sys_write`, que calcula el recuento de bytes a partir de la disminución de `uio_resid` y lo devuelve. El usuario ve el valor de retorno de `write(2)`.

Hay tres cosas que difieren de la cadena de lectura de forma sustancial.

**Primera: el kernel ejecuta `copyin` dentro de `uiomove` en lugar de `copyout`.** Mismo mecanismo, dirección opuesta. El manejo de fallos es idéntico: un puntero de usuario incorrecto devuelve `EFAULT`, una copia parcial deja `uio_resid` coherente con lo que sí se transfirió, y el manejador simplemente propaga el código de error.

**Segunda: `ioflag` lleva `IO_NDELAY` de la misma forma, pero la interpretación del driver es diferente.** En una lectura, el modo no bloqueante significa "devuelve `EAGAIN` si no hay datos". En una escritura, significa "devuelve `EAGAIN` si no hay espacio". Condiciones simétricas, valores errno simétricos.

**Tercera: las actualizaciones de `atime` y `mtime` son específicas a la dirección.** `devfs_read_f` actualiza `si_atime` si se transfirieron bytes; `devfs_write_f` actualiza `si_mtime` (y `si_ctime` en algunos caminos) si se transfirieron bytes. Estos son los valores que `stat(2)` reporta para el nodo, y la razón por la que `ls -lu /dev/myfirst/0` muestra marcas de tiempo distintas para lecturas y escrituras. Tu driver no gestiona estos campos; lo hace devfs.

Una vez que reconoces los rastreos de lectura y escritura como imágenes especulares, has interiorizado la mayor parte del camino de despacho de un dispositivo de caracteres. Cada capítulo a partir de aquí añadirá ganchos (un `d_poll`, un `d_kqfilter`, un `d_ioctl`, un camino `mmap`) que se sitúan en la misma cadena en ranuras ligeramente distintas. La cadena en sí permanece constante.



## Flujo de trabajo práctico: probar tu driver desde la shell

Las herramientas del sistema base son tu primer y mejor banco de pruebas. Esta sección es una guía de campo breve para usarlas bien con un driver que estás desarrollando. Ninguno de los comandos que aparecen a continuación te resultará nuevo, pero usarlos para el trabajo con drivers tiene un ritmo que merece aprenderse de forma explícita.

### `cat(1)`: la primera comprobación

`cat` lee desde sus argumentos y escribe en la salida estándar. Para un driver que sirve un mensaje estático o un buffer vaciado, `cat` es la forma más rápida de ver qué produce el camino de lectura:

```sh
% cat /dev/myfirst/0
```

Si la salida es la que esperas, el camino de lectura está vivo. Si está vacía, o bien tu driver no tiene nada que entregar (comprueba `sysctl dev.myfirst.0.stats.bufused`) o bien tu manejador está devolviendo EOF en la primera llamada. Si la salida está corrompida, o bien tu buffer no está inicializado o bien estás entregando bytes más allá de `bufused`.

`cat` abre su argumento una vez y lee de él hasta EOF. Cada `read(2)` es una llamada separada a tu `d_read`. Usa `truss(1)` para ver cuántas llamadas hace `cat`:

```sh
% truss cat /dev/myfirst/0 2>&1 | grep read
```

La salida muestra cada `read(2)` con sus argumentos y valor de retorno. Si esperabas una sola lectura y ves tres, eso te indica algo sobre el tamaño de tu buffer; si esperabas tres lecturas y ves una sola, tu handler entregó todos los datos en una única llamada.

### `echo(1)` y `printf(1)`: escrituras simples

`echo` es la forma más rápida de enviar una cadena conocida al camino de escritura de tu driver:

```sh
% echo "hello" | sudo tee /dev/myfirst/0 > /dev/null
```

Hay dos cosas que notar. Primero, `echo` añade un salto de línea por defecto; la cadena que enviaste tiene seis bytes, no cinco. Usa `echo -n` para suprimir el salto de línea cuando sea necesario. Segundo, el uso de `tee` está ahí para resolver un problema de permisos: la redirección del shell (`>`) se ejecuta con los privilegios del usuario, por lo que `sudo echo > /dev/myfirst/0` no consigue abrir el nodo. Redirigir a través de `tee`, que se ejecuta bajo `sudo`, evita ese problema.

`printf` te da más control:

```sh
% printf 'abc' | sudo tee /dev/myfirst/0 > /dev/null
```

Tres bytes, sin salto de línea. Usa `printf '\x41\x42\x43'` para patrones binarios.

### `dd(1)`: la herramienta de precisión

Para cualquier prueba que necesite un número de bytes concreto o un tamaño de bloque específico, `dd` es la herramienta adecuada. `dd` es además una de las pocas herramientas del sistema base que informa sobre lecturas y escrituras cortas en su resumen, lo que la hace especialmente útil para probar el comportamiento del driver:

```sh
% sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=128 count=4
4+0 records in
4+0 records out
512 bytes transferred in 0.001234 secs (415000 bytes/sec)
```

Los contadores `X+Y records in` / `X+Y records out` tienen un significado preciso: `X` es el número de transferencias de bloque completo, `Y` es el número de transferencias cortas. Una línea que diga `0+4 records out` significa que cada bloque fue aceptado solo parcialmente. Es el driver diciéndote algo.

`dd` también te permite leer con un tamaño de bloque conocido:

```sh
% sudo dd if=/dev/myfirst/0 of=/tmp/dump bs=64 count=1
```

Esto emite exactamente un `read(2)` de 64 bytes. Tu manejador ve `uio_resid = 64`; respondes con lo que tengas; el resultado es lo que `dd` escribe en `/tmp/dump`.

El flag `iflag=fullblock` le indica a `dd` que repita las lecturas cortas hasta que haya completado el bloque solicitado. Es útil cuando quieres capturar toda la salida del driver sin perder bytes por el comportamiento predeterminado de lecturas cortas.

### `od(1)` y `hexdump(1)`: inspección a nivel de bytes

Para probar drivers, `od` y `hexdump` te permiten ver los bytes exactos que emitió tu driver:

```sh
% sudo dd if=/dev/myfirst/0 bs=32 count=1 | od -An -tx1z
  68 65 6c 6c 6f 0a                                 >hello.<
```

El flag `-An` suprime la impresión de direcciones. `-tx1z` muestra los bytes en hex y ASCII. Si la salida esperada es texto, lo ves a la derecha; si es binaria, ves el hex a la izquierda.

Estas herramientas se vuelven esenciales cuando una lectura produce bytes inesperados. «Parece raro» y «Puedo ver cada byte en hex» son estados de depuración muy distintos.

### `sysctl(8)` y `dmesg(8)`: la voz del kernel

Tu driver publica contadores a través de `sysctl` y eventos del ciclo de vida a través de `dmesg`. Ambos merecen ser consultados en cada prueba:

```sh
% sysctl dev.myfirst.0
% dmesg | tail -20
```

La salida de `sysctl` es tu vista del estado actual del driver. `dmesg` es tu vista del historial del driver desde el arranque (o desde que el buffer circular dio la vuelta).

Un buen hábito: después de cada prueba, ejecuta ambos. Si los números no coinciden con lo esperado, habrás acotado el error rápidamente.

### `fstat(1)`: ¿quién tiene el descriptor abierto?

Cuando tu driver se niega a descargarse («module busy»), la pregunta es: «¿quién tiene `/dev/myfirst/0` abierto ahora mismo?». `fstat(1)` responde a eso:

```sh
% fstat -p $(pgrep cat) /dev/myfirst/0
USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W NAME
ebrandi  cat          1234    3 /dev         0 crw-rw----  myfirst/0  r /dev/myfirst/0
```

Alternativamente, `fuser(8)`:

```sh
% sudo fuser /dev/myfirst/0
/dev/myfirst/0:         1234
```

Cualquiera de las dos herramientas muestra los procesos que tienen el descriptor abierto. Termina el proceso culpable (con cuidado; no elimines nada que no hayas iniciado tú) y el módulo se descargará.

### `truss(1)` y `ktrace(1)`: observando las syscalls

Para un programa de usuario cuya interacción con tu driver quieras inspeccionar, `truss` muestra cada syscall y su valor de retorno:

```sh
% truss ./rw_myfirst
open("/dev/myfirst/0",O_WRONLY,0666)             = 3 (0x3)
write(3,"round-trip test payload\n",24)          = 24 (0x18)
close(3)                                         = 0 (0x0)
...
```

`ktrace` graba en un archivo que `kdump` imprime después; es la herramienta adecuada cuando quieres capturar un trace de un programa de larga ejecución.

Estas dos herramientas no son específicas de drivers, pero son la forma de confirmar desde fuera que tu driver produce los resultados que verá un programa de usuario.

### Un ritmo de prueba sugerido

Para cada etapa del capítulo, prueba este bucle:

1. Compila y carga.
2. `cat` para producir la salida inicial, confírmala a ojo.
3. `sysctl dev.myfirst.0` para comprobar que los contadores coinciden.
4. `dmesg | tail` para ver los eventos del ciclo de vida.
5. Escribe algo con `echo` o `dd`.
6. Léelo de vuelta.
7. Repite con un tamaño mayor, un tamaño en el límite y un tamaño patológico.
8. Descarga.

Tras un par de iteraciones, esto se vuelve automático y rápido. Es el tipo de ritmo que convierte el desarrollo de drivers de algo tedioso en una rutina.

### Un ejemplo concreto con `truss`

Ejecutar un programa de userland bajo `truss(1)` es una de las formas más rápidas de ver exactamente qué syscalls hace al driver y qué valores de retorno produce el kernel. Aquí tienes una sesión típica con el driver de la etapa 3 cargado y vacío:

```sh
% truss ./rw_myfirst rt 2>&1
open("/dev/myfirst/0",O_WRONLY,00)               = 3 (0x3)
write(3,"round-trip test payload, 24b\n",29)     = 29 (0x1d)
close(3)                                         = 0 (0x0)
open("/dev/myfirst/0",O_RDONLY,00)               = 3 (0x3)
read(3,"round-trip test payload, 24b\n",255) = 29 (0x1d)
close(3)                                         = 0 (0x0)
exit(0x0)
```

Hay algunas cosas que merecen atención. Cada línea muestra una syscall, sus argumentos y su valor de retorno tanto en decimal como en hex. La llamada `write` recibió 29 bytes y el driver los aceptó todos (el valor de retorno coincide con la longitud solicitada). La llamada `read` recibió un buffer de 255 bytes de espacio y el driver produjo 29 bytes de contenido; una lectura corta que el programa de usuario acepta explícitamente. Ambas llamadas `open` devolvieron 3, porque los descriptores de archivo 0, 1 y 2 son los flujos estándar y el primer descriptor libre es el 3.

Si fuerzas una escritura corta limitando el driver, `truss` lo mostrará claramente:

```sh
% truss ./write_big 2>&1 | head
open("/dev/myfirst/0",O_WRONLY,00)               = 3 (0x3)
write(3,"<8192 bytes of data>",8192)             = 4096 (0x1000)
write(3,"<4096 bytes of data>",4096)             ERR#28 'No space left on device'
close(3)                                         = 0 (0x0)
```

La primera escritura solicitó 8192 bytes y fue aceptada por 4096. La segunda escritura no tenía nada que decir porque el buffer está lleno; el driver devolvió `ENOSPC`, que `truss` mostró como `ERR#28 'No space left on device'`. Esta es la vista desde el lado del usuario; tu driver estaba devolviendo cero (con `uio_resid` decrementado a 4096) para la primera llamada y `ENOSPC` para la segunda. Comparar lo que ve `truss` con lo que dice tu `device_printf` es una manera excelente de detectar discrepancias entre la intención del driver y lo que informa el kernel.

`truss -f` sigue los forks, lo que es útil cuando tu arnés de pruebas lanza procesos trabajadores. `truss -d` precede cada línea con una marca de tiempo relativa; útil para razonar sobre la latencia entre llamadas. Ambos flags son una pequeña inversión; los beneficios se acumulan rápidamente cuando empiezas a ejecutar pruebas de estrés multiproceso.

### Una nota rápida sobre `ktrace`

`ktrace(1)` es el hermano mayor de `truss`. Graba un trace binario en un archivo (`ktrace.out` por defecto) que luego formateas con `kdump(1)`. Es la herramienta adecuada cuando:

- La ejecución de la prueba es larga y no quieres ver la salida en tiempo real.
- Quieres capturar detalles demasiado finos para `truss` (temporización de syscalls, entrega de señales, búsquedas namei).
- Quieres reproducir un trace después, quizás en otra máquina.

Una sesión típica:

```sh
% sudo ktrace -i ./stress_rw -s 5
% sudo kdump | head -40
  2345 stress_rw CALL  open(0x800123456,0x1<O_WRONLY>)
  2345 stress_rw NAMI  "/dev/myfirst/0"
  2345 stress_rw RET   open 3
  2345 stress_rw CALL  write(0x3,0x800123500,0x40)
  2345 stress_rw RET   write 64
  2345 stress_rw CALL  write(0x3,0x800123500,0x40)
  2345 stress_rw RET   write 64
...
```

Para el capítulo 9, la diferencia entre `truss` y `ktrace` es pequeña. Usa `truss` como opción predeterminada; recurre a `ktrace` cuando necesites más detalle o un trace grabado.

### Observando la memoria del kernel con `vmstat -m`

Tu driver asigna memoria del kernel a través de `malloc(9)` con el tipo `M_DEVBUF`. El `vmstat -m` de FreeBSD muestra cuántas asignaciones están activas en cada categoría de tipo. Ejecútalo mientras tu driver está cargado e inactivo, y de nuevo mientras tiene un buffer asignado; el incremento será visible en la fila `devbuf`:

```sh
% vmstat -m | head -1
         Type InUse MemUse HighUse Requests  Size(s)
% vmstat -m | grep devbuf
       devbuf   415   4120K       -    39852  16,32,64,128,256,512,1024,2048,...
```

La columna `InUse` es el recuento actual de asignaciones activas de este tipo. `MemUse` es el tamaño total actualmente en uso. `HighUse` es el máximo histórico desde el arranque. `Requests` es el recuento acumulado de llamadas `malloc` que seleccionaron este tipo.

Carga el driver de la etapa 2. `InUse` sube en uno (el buffer de 4096 bytes), `MemUse` sube aproximadamente 4 KiB y `Requests` se incrementa. Descarga. `InUse` baja en uno; `MemUse` baja los 4 KiB. Si no es así, tienes una fuga de memoria, y `vmstat -m` acaba de decírtelo.

Este es el segundo canal de observabilidad que merece la pena añadir a tu ritmo de pruebas. `sysctl` muestra contadores propios del driver. `dmesg` muestra líneas de log propias del driver. `vmstat -m` muestra recuentos de asignaciones del kernel, y detecta una clase de error (olvidarse de liberar) que los dos primeros no pueden ver.

Para un driver que declara su propio tipo malloc mediante `MALLOC_DEFINE(M_MYFIRST, "myfirst", ...)`, `vmstat -m | grep myfirst` es incluso mejor: aísla las asignaciones de tu driver del pool genérico `devbuf`. `myfirst` se mantiene con `M_DEVBUF` a lo largo de este capítulo por simplicidad, pero cambiar a un tipo dedicado es una modificación pequeña que quizás quieras hacer antes de distribuir un driver fuera del entorno de laboratorio del libro.



## Observabilidad: haciendo tu driver legible

Un driver que hace lo correcto vale más si puedes confirmar, desde fuera del kernel, que lo está haciendo. Esta sección es una breve reflexión sobre las decisiones de observabilidad que ha ido tomando este capítulo, y el motivo.

### Tres superficies: sysctl, dmesg, userland

Tu driver presenta tres superficies al operador:

- **sysctl** para contadores en tiempo real: valores puntuales que el operador puede consultar.
- **dmesg (device_printf)** para eventos del ciclo de vida: apertura, cierre, errores, transiciones.
- nodos **/dev** para el camino de datos: los bytes reales.

Cada uno tiene un papel distinto. sysctl le dice al operador *qué es verdad en este momento*. dmesg le dice al operador *qué cambió recientemente*. `/dev` es lo que el operador está usando realmente.

Un driver bien observado usa los tres, de forma deliberada. Un driver mínimamente observado usa solo el tercero, y depurarlo requiere un depurador o mucho trabajo de suposición.

### Sysctl: contadores frente a estado

`myfirst` expone contadores a través del árbol sysctl bajo `dev.myfirst.0.stats`:

- `attach_ticks`: un valor puntual (cuando el driver realizó su attach).
- `open_count`: un contador monótonamente creciente (aperturas totales).
- `active_fhs`: un recuento en tiempo real (descriptores actuales).
- `bytes_read`, `bytes_written`: contadores monótonamente crecientes.
- `bufused`: un valor en tiempo real (ocupación actual del buffer).

Los contadores monótonamente crecientes son más fáciles de razonar que los valores en tiempo real, porque su tasa de cambio es informativa incluso cuando el valor absoluto no lo es. Un operador que ve `bytes_read` aumentar a 1 MB/s ha aprendido algo, aunque 1 MB/s no tenga significado fuera de contexto.

Los valores en tiempo real son esenciales cuando el estado importa para tomar decisiones (`active_fhs > 0` significa que la descarga fallará). Elige primero contadores monótonamente crecientes; usa valores en tiempo real cuando los necesites.

### dmesg: eventos que merecen verse

`device_printf(9)` escribe en el buffer de mensajes del kernel, que `dmesg` muestra. Cada línea merece verse exactamente una vez: usa `dmesg` para eventos, no para estado continuo.

Los eventos que registra `myfirst`:

- Attach (una vez por instancia).
- Open (una vez por apertura).
- Destructor (una vez por cierre de descriptor).
- Detach (una vez por instancia).

Son cuatro líneas por instancia por ciclo de carga y descarga, más dos líneas por par apertura/cierre. Manejable.

Lo que no registramos:

- Cada llamada `read` o `write`. Eso inundaría `dmesg` con cualquier carga de trabajo real.
- Cada lectura de `sysctl`. Son pasivas.
- Cada transferencia exitosa. Los contadores de `sysctl` llevan esa información, y lo hacen de forma más compacta.

Si un driver necesita registrar algo que ocurre muchas veces por segundo, la respuesta habitual es proteger el registro con `if (bootverbose)`, de modo que permanezca silencioso en sistemas en producción pero esté disponible para los desarrolladores que arrancan con `boot -v`. Para `myfirst` no necesitamos ni eso.

### La trampa del exceso de logging

Un driver que registra cada operación es un driver que oculta sus eventos importantes en un mar de ruido. Si tu `dmesg` muestra diez mil líneas de `read returned 0 bytes`, la línea que dice `buffer full, returning ENOSPC` es invisible.

Mantén los logs escasos. Registra transiciones, no estados. Registra una vez por instancia, no una vez por llamada. Ante la duda, silencio.

### Contadores que añadirás más adelante

Los capítulos 10 y posteriores ampliarán el árbol de contadores con:

- `reads_blocked`, `writes_blocked`: recuento de llamadas que tuvieron que dormir (capítulo 10).
- `poll_waiters`: recuento de suscriptores activos de `poll(2)` (capítulo 10).
- `drain_waits`, `overrun_events`: diagnósticos del buffer circular (capítulo 10).

Cada uno es una cosa más que un operador puede consultar para entender qué está haciendo el driver. El patrón es el mismo: expón los contadores, mantén el mecanismo silencioso, deja que el operador decida cuándo inspeccionar.

### Cómo se ve tu driver con carga ligera

Un ejemplo concreto es más útil que los consejos abstractos. Carga la Etapa 3, ejecuta el programa compañero `stress_rw` durante unos segundos con `sysctl dev.myfirst.0.stats` observando desde otro terminal, y verás algo parecido a esto:

**Antes de que `stress_rw` arranque:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 0
dev.myfirst.0.stats.bytes_written: 0
dev.myfirst.0.stats.bufused: 0
```

Actividad nula, un attach, buffer vacío.

**Durante la ejecución de `stress_rw`, con `watch -n 0.5 sysctl dev.myfirst.0.stats`:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 2
dev.myfirst.0.stats.active_fhs: 2
dev.myfirst.0.stats.bytes_read: 1358976
dev.myfirst.0.stats.bytes_written: 1359040
dev.myfirst.0.stats.bufused: 64
```

Dos descriptores activos (escritor + lector), contadores en ascenso, buffer con 64 bytes de datos en tránsito. `bytes_written` va ligeramente por delante de `bytes_read`, que es exactamente lo que cabría esperar: el escritor ha producido un bloque que el lector todavía no ha consumido del todo. La diferencia coincide con `bufused`.

**Después de que `stress_rw` termina:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 2
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 4800000
dev.myfirst.0.stats.bytes_written: 4800000
dev.myfirst.0.stats.bufused: 0
```

Ambos descriptores cerrados. El total de aperturas es 2 (acumulativo). Activos: 0. `bytes_read` iguala a `bytes_written`; el lector se ha puesto al día por completo. El buffer está vacío.

Tres patrones característicos que conviene observar. Primero, `active_fhs` siempre refleja los descriptores activos en ese momento; es un valor en tiempo real, no un contador acumulativo. Segundo, `bytes_read == bytes_written` en estado estable cuando el lector va al día, más lo que haya en `bufused`. Tercero, `open_count` es un valor acumulativo que nunca disminuye; una forma rápida de detectar rotación intensa es observar cómo crece mientras `active_fhs` permanece estable.

Un driver que se comporta de manera predecible bajo carga es un driver que puedes operar con confianza. Cuando los contadores cuadran tal como describe este párrafo, tienes tu primer driver real, no un juguete.

## Con signo, sin signo y los peligros del error por uno

Una breve sección sobre una clase de fallo que ha causado más panics del kernel que casi cualquier otra. Aparece con especial frecuencia en los manejadores de I/O.

### `ssize_t` vs `size_t`

Dos tipos dominan el código de I/O:

- `size_t`: sin signo, se usa para tamaños y contadores. `sizeof(x)` devuelve `size_t`. `malloc(9)` recibe `size_t`. `memcpy` recibe `size_t`.
- `ssize_t`: con signo, se usa cuando un valor puede ser negativo (normalmente -1 para indicar error). `read(2)` y `write(2)` devuelven `ssize_t`. `uio_resid` es `ssize_t`.

Ambos tipos tienen el mismo ancho en todas las plataformas que FreeBSD admite, pero no se convierten entre sí de forma silenciosa sin generar avisos, y se comportan de manera muy diferente cuando la aritmética produce un underflow.

Una resta de valores `size_t` que produciría un resultado negativo se convierte en cambio en un valor positivo enorme, porque `size_t` es sin signo. Por ejemplo:

```c
size_t avail = sc->buflen - sc->bufused;
```

Si `sc->bufused` es mayor que `sc->buflen`, `avail` es un número enorme, y el siguiente `uiomove` intenta una transferencia que sobrepasa el final del buffer.

La defensa es el invariante. En cada sección de gestión de buffer del capítulo, mantenemos `sc->bufhead + sc->bufused <= sc->buflen`. Mientras ese invariante se cumpla, `sc->buflen - (sc->bufhead + sc->bufused)` no puede producir un underflow.

El riesgo está en los caminos de código que violan el invariante de forma accidental. Un doble free que restaura un valor ya consumido; una escritura que actualiza `bufused` dos veces; una condición de carrera entre escritores. Esos son los fallos que hay que buscar cuando `avail` parece incorrecto.

### `uio_resid` comparado con valores sin signo

`uio_resid` es `ssize_t`. Los tamaños de tu buffer son `size_t`. Un código como este:

```c
if (uio->uio_resid > sc->buflen) ...
```

Se compilará con una comparación entre valores con signo y sin signo. Los compiladores modernos advierten sobre esto; el aviso debe tomarse en serio.

El patrón más seguro es hacer una conversión explícita:

```c
if ((size_t)uio->uio_resid > sc->buflen) ...
```

O usar `MIN`, que hemos estado usando:

```c
towrite = MIN((size_t)uio->uio_resid, avail);
```

La conversión es justificable porque `uio_resid` está documentado como no negativo en uios válidos (y `uiomove` realiza `KASSERT` sobre él). La conversión mantiene contento al compilador y hace explícita la intención.

### Error por uno en contadores

Un contador actualizado en el lado incorrecto de una comprobación de error es un fallo clásico:

```c
sc->bytes_read += towrite;          /* BAD: happens even on error */
error = uiomove(sc->buf, towrite, uio);
```

La forma correcta es incrementar después del éxito:

```c
error = uiomove(sc->buf, towrite, uio);
if (error == 0)
        sc->bytes_read += towrite;
```

Por eso tenemos `if (error == 0)` protegiendo cada actualización de contador en el capítulo. El coste es una línea de código. El beneficio es que tus contadores coinciden con la realidad.

### El patrón `uio_offset - before`

Cuando quieres saber «¿cuántos bytes movió realmente `uiomove`?», la manera más limpia es comparar `uio_offset` antes y después:

```c
off_t before = uio->uio_offset;
error = uiomove_frombuf(sc->buf, sc->buflen, uio);
size_t moved = uio->uio_offset - before;
```

Esto funciona tanto para transferencias completas como para transferencias parciales. `moved` es el recuento de bytes real, independientemente de lo que pidiera el llamante o de cuánto hubiera disponible.

El patrón no tiene coste en tiempo de ejecución (dos restas) y es inequívoco en el código. Úsalo cuando tu driver quiera contar bytes; la alternativa, inferir el recuento desde `uio_resid`, requiere conocer el tamaño de la petición original, lo que supone más contabilidad.



## Solución de problemas adicionales: los casos límite

Ampliando la sección de solución de problemas anterior, aquí tienes algunos escenarios más que probablemente encontrarás la primera vez que escribas un driver real.

### «La segunda lectura en el mismo descriptor devuelve cero»

Es lo esperado para un driver de mensaje estático (etapa 1): una vez que `uio_offset` llega al final del mensaje, `uiomove_frombuf` devuelve cero.

Es inesperado para un driver FIFO (etapa 3): la primera lectura vació el buffer, y ningún escritor lo ha rellenado. El llamante no debería emitir una segunda lectura consecutiva sin que se haya producido una escritura entre medias.

Para distinguir los dos casos, comprueba `sysctl dev.myfirst.0.stats.bufused`. Si es cero, el buffer está vacío. Si no es cero y sigues viendo cero bytes, hay un fallo.

### «El driver devuelve cero bytes inmediatamente cuando el buffer tiene datos»

El manejador de lectura está tomando la rama incorrecta. Causas habituales:

- Una comprobación `bufused == 0` colocada en el lugar incorrecto. Si la comprobación se ejecuta antes de obtener el estado por apertura, puede cortocircuitar la lectura antes del trabajo real.
- Un `return 0;` accidental más arriba en el manejador (por ejemplo, en una rama de debug que quedó de un experimento anterior).
- Un `mtx_unlock` ausente en un camino de error, haciendo que toda llamada posterior se bloquee en el mutex indefinidamente. Síntoma: la segunda llamada se cuelga, no devuelve cero bytes; pero vale la pena comprobarlo.

### «Mi `uiomove_frombuf` siempre devuelve cero independientemente del buffer»

Dos causas habituales:

- El argumento `buflen` es cero. `uiomove_frombuf` devuelve cero inmediatamente si `buflen <= 0`.
- `uio_offset` ya está en `buflen` o más allá. `uiomove_frombuf` devuelve cero para señalar EOF en ese caso.

Añade un `device_printf` que registre los argumentos a la entrada para confirmar en cuál de los casos estás.

### «El buffer desborda hacia la memoria adyacente»

Tu aritmética es incorrecta. En algún punto estás llamando a `uiomove(sc->buf + X, N, uio)` donde `X + N > sc->buflen`. La escritura avanza silenciosamente y corrompe la memoria del kernel.

El kernel suele entrar en pánico poco después, posiblemente en un subsistema completamente ajeno. El mensaje de pánico no mencionará tu driver; mencionará el vecino de heap que fue corrompido.

Si sospechas esto, recompila con `INVARIANTS` y `WITNESS` (y en muchas plataformas, KASAN en amd64). Estas funciones del kernel detectan desbordamientos de buffer mucho antes que el kernel por defecto.

### «Un proceso que lee del dispositivo se cuelga para siempre»

Como el capítulo 9 no implementa I/O bloqueante, esto no debería ocurrir con `myfirst` en la etapa 3. Si ocurre, la causa más probable es que el proceso esté manteniendo un descriptor de archivo mientras intentas descargar el driver; `destroy_dev(9)` está esperando a que `si_threadcount` llegue a cero, y el proceso se encuentra dentro de tu manejador por algún motivo.

Para diagnosticar: `ps auxH | grep <your-test>`; `gdb -p <pid>` y `bt`. La pila debería revelar dónde está aparcado el thread.

Si el manejador de la etapa 3 duerme accidentalmente (por ejemplo, porque añadiste un `tsleep` mientras experimentabas con material del capítulo 10 antes de tiempo), la solución es eliminar el sleep. El driver del capítulo 9 no bloquea.

### `kldunload` indica `kldunload: can't unload file: Device busy`

Síntoma clásico de un descriptor todavía abierto. Usa `fuser /dev/myfirst/0` para encontrar el proceso problemático, cierra el descriptor o termina el proceso, e inténtalo de nuevo.

### «Modifiqué el driver y `make` compila, pero `kldload` falla con error de versión»

Tu entorno de build no coincide con el kernel en ejecución. Comprueba:

```sh
% freebsd-version -k
14.3-RELEASE
% ls /usr/obj/usr/src/amd64.amd64/sys/GENERIC
```

Si `/usr/src` corresponde a una versión diferente, tus cabeceras producen un módulo que el kernel rechaza. Recompila contra las fuentes correspondientes. En una VM de laboratorio esto normalmente significa sincronizar `/usr/src` con la versión en ejecución mediante `fetch` o `freebsd-update src-install`.

### «Veo cada byte escrito por el dispositivo impreso dos veces en dmesg»

Tienes un `device_printf` dentro del hot path que imprime cada transferencia. Elimínalo o protégelo con `if (bootverbose)`.

Una versión más leve del mismo fallo: un registro de una sola línea que imprime la longitud de cada transferencia. Para cargas de trabajo de prueba pequeñas parece bien; para una carga de trabajo real de usuario enterrará dmesg y provocará compresión de marcas de tiempo en el buffer del kernel.

### «Mi `d_read` es llamado pero mi `d_write` no»

O el programa de usuario nunca llama a `write(2)` sobre el dispositivo, o lo llama con el descriptor no abierto para escritura (`O_RDONLY`). Comprueba ambos casos.

Además: confirma que `cdevsw.d_write` está asignado a `myfirst_write`. Un fallo de copiar y pegar que lo asigne a `myfirst_read` hace que ambas direcciones acaben en el manejador de lectura, con resultados predeciblemente confusos.



## Notas de diseño: por qué cada etapa se detiene donde lo hace

Una breve metasección sobre por qué las tres etapas del capítulo 9 tienen los límites que tienen. Este es el tipo de razonamiento de diseño del capítulo que merece hacerse explícito, porque es el razonamiento que aplicarás cuando diseñes tus propios drivers.

### Por qué existe la etapa 1

La etapa 1 es el `d_read` más pequeño posible que no es `/dev/null`. Introduce:

- El helper `uiomove_frombuf(9)`, la manera más sencilla de sacar un buffer fijo al espacio de usuario.
- La gestión del desplazamiento por descriptor.
- El patrón de usar `uio_offset` como portador de estado.

La etapa 1 no hace nada con las escrituras; el stub del capítulo 8 es suficiente.

Sin la etapa 1, el salto de los stubs a un driver de lectura y escritura con buffer es demasiado grande. La etapa 1 te permite confirmar, con código mínimo, que el manejador de lectura está conectado correctamente. Todo lo demás se construye sobre esa confirmación.

### Por qué existe la etapa 2

La etapa 2 introduce:

- Un buffer del kernel asignado dinámicamente.
- Un camino de escritura que acepta datos del usuario.
- Un camino de lectura que respeta el desplazamiento del llamante a lo largo del buffer acumulado.
- El primer uso realista del mutex del softc en un manejador de I/O.

La etapa 2 deliberadamente no vacía en las lecturas. El buffer crece hasta llenarse; las escrituras siguientes devuelven `ENOSPC`. Esto permite que dos lectores concurrentes confirmen que cada uno tiene su propio `uio_offset`, que es la propiedad que la etapa 1 no podía demostrar (porque la etapa 1 no tenía nada que escribir).

### Por qué existe la etapa 3

La etapa 3 introduce:

- Lecturas que vacían el buffer.
- La coordinación entre un puntero de cabeza y un contador de uso.
- La semántica FIFO que la mayoría de los drivers reales aproximan.

La etapa 3 no da la vuelta. Los punteros de cabeza y de uso avanzan hacia adelante por el buffer, y el buffer se colapsa al principio cuando está vacío. Un ring buffer apropiado (con cabeza y cola dando la vuelta en un array de tamaño fijo) pertenece al capítulo 10 porque se combina naturalmente con lecturas bloqueantes y `poll(2)`: un ring hace eficiente la operación en estado estable, y la operación eficiente en estado estable es exactamente lo que necesita un lector bloqueante.

### Por qué no hay ring buffer aquí

Un ring buffer supone entre cinco y quince líneas de contabilidad adicional más allá de lo que hace la etapa 3. Añadirlo ahora no sería una gran cantidad de código. La razón de aplazarlo es pedagógica: los dos conceptos («semántica del camino de I/O» y «mecánica del ring buffer») son independientemente confusos para un principiante, y dividirlos en dos capítulos permite que cada capítulo aborde un montón de confusión a la vez.

Cuando el capítulo 10 introduzca el ring, el lector ya dominará el camino de I/O. El nuevo material es únicamente la contabilidad del ring.

### Por qué no hay bloqueo

El bloqueo es útil, pero introduce `msleep(9)`, variables de condición, el hook de desmontaje `d_purge` y una maraña de cuestiones de corrección sobre qué despertar y cuándo. Cada uno de ellos es un tema sustancial. Mezclarlos en el capítulo 9 duplicaría su longitud y reduciría su claridad a la mitad.

La primera sección del capítulo 10 es «cuando tu driver tiene que esperar». Es una continuación natural.

### Lo que las etapas **no** pretenden ser

Las etapas no son una simulación de un driver de hardware. No imitan el DMA. No simulan interrupciones. No pretenden ser nada más que lo que son: drivers en memoria que ejercitan el camino de I/O de UNIX.

Esto importa porque más adelante en el libro, cuando escribamos drivers de hardware reales, el camino de I/O tendrá el mismo aspecto. Los detalles del hardware (de dónde vienen los bytes, adónde van) cambiarán, pero la forma del manejador, el uso de uiomove, las convenciones de errno, los patrones de contadores: todo ello será reconocible desde el capítulo 9.

Un driver que mueve bytes correctamente a través del límite de confianza usuario/kernel es el 80% de cualquier driver real. El capítulo 9 enseña ese 80%.



## Laboratorios prácticos

Los laboratorios siguientes siguen las tres etapas anteriores. Cada laboratorio es un punto de control que demuestra que tu driver está haciendo lo que el texto acaba de describir. Lee el laboratorio completo antes de empezar, y hazlos en orden.

### Laboratorio 9.1: compilar y cargar la etapa 1

**Objetivo:** compilar el driver de la etapa 1, cargarlo, leer el mensaje estático y confirmar la gestión del desplazamiento por descriptor.

**Pasos:**

1. Parte del árbol de archivos de ejemplo: `cp -r examples/part-02/ch09-reading-and-writing/stage1-static-message ~/drivers/ch09-stage1`. También puedes modificar el driver de la Fase 2 del Capítulo 8 siguiendo el recorrido de la Fase 1 descrito más arriba.
2. Entra en el directorio y compila:
   ```sh
   % cd ~/drivers/ch09-stage1
   % make
   ```
3. Carga el módulo:
   ```sh
   % sudo kldload ./myfirst.ko
   ```
4. Confirma que el dispositivo está presente:
   ```sh
   % ls -l /dev/myfirst/0
   crw-rw----  1 root  operator ... /dev/myfirst/0
   ```
5. Lee el mensaje:
   ```sh
   % cat /dev/myfirst/0
   Hello from myfirst.
   This is your first real read path.
   Chapter 9, Stage 1.
   ```
6. Compila la herramienta de espacio de usuario `rw_myfirst.c` del árbol de ejemplos y ejecútala en modo «leer dos veces»:
   ```sh
   % cc -o rw_myfirst rw_myfirst.c
   % ./rw_myfirst read
   [read 1] 75 bytes:
   Hello from myfirst.
   This is your first real read path.
   Chapter 9, Stage 1.
   [read 2] 0 bytes (EOF)
   ```
7. Confirma el contador por descriptor:
   ```sh
   % dmesg | tail -5
   ```
   Deberías ver las líneas `open via /dev/myfirst/0 fh=...` y `per-open dtor fh=...` del Capítulo 8, junto con el cuerpo del mensaje leído.
8. Descarga el módulo:
   ```sh
   % sudo kldunload myfirst
   ```

**Criterios de éxito:**

- `cat` imprime el mensaje.
- La herramienta de espacio de usuario muestra 75 bytes en la primera lectura y 0 en la segunda.
- `dmesg` muestra un open y un destructor por cada invocación de `./rw_myfirst read`.

**Errores frecuentes:**

- Olvidar el `-1` en `sizeof(myfirst_message) - 1`. El mensaje incluirá un byte NUL al final que aparecerá como un carácter espurio en la salida del programa.
- No llamar a `devfs_get_cdevpriv` antes de la comprobación `sc == NULL`. El resto del capítulo depende de este orden; ejecútalo para entender por qué es el correcto.
- Usar `(void *)sc->message` en lugar de `__DECONST(void *, sc->message)`. Ambas formas funcionan en la mayoría de compiladores; la forma con `__DECONST` es la convención y suprime una advertencia en algunas configuraciones del compilador.

### Laboratorio 9.2: Etapa 2 con escrituras y lecturas

**Objetivo:** Construir la Etapa 2, introducir datos desde el userland, recuperarlos y observar los contadores sysctl.

**Pasos:**

1. Desde el árbol de ejemplos: `cp -r examples/part-02/ch09-reading-and-writing/stage2-readwrite ~/drivers/ch09-stage2`.
2. Compilar y cargar:
   ```sh
   % cd ~/drivers/ch09-stage2
   % make
   % sudo kldload ./myfirst.ko
   ```
3. Comprobar el estado inicial:
   ```sh
   % sysctl dev.myfirst.0.stats
   dev.myfirst.0.stats.attach_ticks: ...
   dev.myfirst.0.stats.open_count: 0
   dev.myfirst.0.stats.active_fhs: 0
   dev.myfirst.0.stats.bytes_read: 0
   dev.myfirst.0.stats.bytes_written: 0
   dev.myfirst.0.stats.bufused: 0
   ```
4. Escribir una línea de texto:
   ```sh
   % echo "the quick brown fox" | sudo tee /dev/myfirst/0 > /dev/null
   ```
5. Leerla de vuelta:
   ```sh
   % cat /dev/myfirst/0
   the quick brown fox
   ```
6. Observar los contadores:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 20
   % sysctl dev.myfirst.0.stats.bytes_written
   dev.myfirst.0.stats.bytes_written: 20
   % sysctl dev.myfirst.0.stats.bytes_read
   dev.myfirst.0.stats.bytes_read: 20
   ```
7. Provocar `ENOSPC`:
   ```sh
   % dd if=/dev/zero bs=1024 count=8 | sudo tee /dev/myfirst/0 > /dev/null
   ```
   Se espera un error de escritura corta. Inspecciona `sysctl dev.myfirst.0.stats.bufused`; su valor debería ser 4096 (el tamaño del buffer).
8. Confirmar que las lecturas siguen entregando el contenido:
   ```sh
   % sudo cat /dev/myfirst/0 | od -An -c | head -3
   ```
9. Descargar:
   ```sh
   % sudo kldunload myfirst
   ```

**Criterios de éxito:**

- Las escrituras depositan bytes; las lecturas los devuelven.
- `bufused` coincide con el número de bytes escritos desde el último reinicio.
- `dd` muestra una escritura corta cuando el buffer se llena; el driver devuelve `ENOSPC`.
- `dmesg` muestra las líneas de apertura y destructor para cada proceso que abrió el dispositivo.

**Errores habituales:**

- Olvidarse de liberar `sc->buf` en `detach`. El driver se descargará sin quejarse, pero una comprobación posterior de fugas de memoria del kernel (`vmstat -m | grep devbuf`) mostrará desviación.
- Mantener el mutex del softc mientras se llama a `uiomove`, sin asegurarse de que el mutex es un `MTX_DEF` y no un spin lock. El `mtx_init(..., MTX_DEF)` del Capítulo 7 es la elección correcta; no lo cambies.
- Omitir el reinicio `sc->bufused = 0` en `attach`. Newbus inicializa el softc a cero, pero hacer la inicialización explícita es la convención; además, facilita futuras refactorizaciones.

### Laboratorio 9.3: Comportamiento FIFO en la Etapa 3

**Objetivo:** Construir la Etapa 3, ejercitar el comportamiento FIFO desde dos terminales y confirmar que las lecturas drenan el buffer.

**Pasos:**

1. Desde el árbol de ejemplos: `cp -r examples/part-02/ch09-reading-and-writing/stage3-echo ~/drivers/ch09-stage3`.
2. Compilar y cargar:
   ```sh
   % cd ~/drivers/ch09-stage3
   % make
   % sudo kldload ./myfirst.ko
   ```
3. En el terminal A, escribe algunos bytes:
   ```sh
   % echo "message A" | sudo tee /dev/myfirst/0 > /dev/null
   ```
4. En el terminal B, léelos:
   ```sh
   % cat /dev/myfirst/0
   message A
   ```
5. Lee de nuevo en el terminal B:
   ```sh
   % cat /dev/myfirst/0
   ```
   Se espera que no haya salida. El buffer está vacío.
6. En el terminal A, escribe dos líneas en rápida sucesión:
   ```sh
   % echo "first" | sudo tee /dev/myfirst/0 > /dev/null
   % echo "second" | sudo tee /dev/myfirst/0 > /dev/null
   ```
7. En el terminal B, lee:
   ```sh
   % cat /dev/myfirst/0
   first
   second
   ```
   Se espera la concatenación de las dos líneas. Ambas escrituras se añadieron al mismo buffer antes de que se produjera cualquier lectura.
8. Inspecciona los contadores:
   ```sh
   % sysctl dev.myfirst.0.stats
   ```
   `bufused` debería haber vuelto a cero. `bytes_read` y `bytes_written` deberían coincidir.
9. Descargar:
   ```sh
   % sudo kldunload myfirst
   ```

**Criterios de éxito:**

- Las escrituras añaden bytes al buffer; las lecturas lo drenan.
- Una lectura cuando el buffer está drenado regresa inmediatamente (EOF cuando está vacío).
- `bytes_read` siempre es igual a `bytes_written` una vez que el lector se ha puesto al día.

**Errores habituales:**

- No reiniciar `bufhead = 0` cuando `bufused` llega a cero. El buffer "derivará" hacia el final de `sc->buf` y rechazará escrituras mucho antes de estar lleno.
- Olvidarse de actualizar `bufhead` a medida que las lecturas drenan. El driver leerá los mismos bytes repetidamente.
- Usar `uio->uio_offset` como desplazamiento por descriptor. En un FIFO los desplazamientos son compartidos; un desplazamiento por descriptor no tiene sentido y confundirá a quien haga las pruebas.

### Laboratorio 9.4: Usar `dd` para medir el comportamiento de las transferencias

**Objetivo:** Usar `dd(1)` para generar transferencias de tamaño conocido, leer los resultados y comprobar que los contadores coinciden.

`dd` es la herramienta ideal aquí porque permite controlar el tamaño de bloque, el número de bloques y el comportamiento ante escrituras cortas.

1. Recargar el driver de la Etapa 3 en estado limpio:
   ```sh
   % sudo kldunload myfirst; sudo kldload ./myfirst.ko
   ```
2. Escribir 512 bytes en un único bloque:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=512 count=1
   1+0 records in
   1+0 records out
   512 bytes transferred
   ```
3. Observar `bufused = 512`:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 512
   ```
4. Leerlos de vuelta con el mismo tamaño de bloque:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/out bs=512 count=1
   1+0 records in
   1+0 records out
   512 bytes transferred
   ```
5. Comprobar que el FIFO está vacío:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 0
   ```
6. Escribir 8192 bytes en un bloque grande:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=8192 count=1
   dd: /dev/myfirst/0: No space left on device
   0+0 records in
   0+0 records out
   0 bytes transferred
   ```
   El driver aceptó 4096 bytes (el tamaño del buffer) de los 8192 solicitados y devolvió una escritura corta para el resto.
7. Alternativamente, usa `bs=4096` con `count=2`:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=4096 count=2
   dd: /dev/myfirst/0: No space left on device
   1+0 records in
   0+0 records out
   4096 bytes transferred
   ```
   El primer bloque de 4096 bytes se completó con éxito; el segundo falló con `ENOSPC`.
8. Drenar:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/out bs=4096 count=1
   % sudo kldunload myfirst
   ```

**Criterios de éxito:**

- `dd` informa de los bytes esperados en cada paso.
- El driver acepta hasta 4096 bytes y rechaza el resto con `ENOSPC`.
- `bufused` refleja el estado del buffer tras cada operación.

### Laboratorio 9.5: Un pequeño programa C de ida y vuelta

**Objetivo:** Escribir un programa C corto en el userland que abra el dispositivo, escriba bytes conocidos, cierre el descriptor, lo abra de nuevo, lea los bytes y verifique que coinciden.

1. Guarda el siguiente código como `rw_myfirst.c` en `~/drivers/ch09-stage3`:

```c
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static const char payload[] = "round-trip test payload\n";

int
main(void)
{
        int fd;
        ssize_t n;

        fd = open("/dev/myfirst/0", O_WRONLY);
        if (fd < 0) { perror("open W"); return 1; }
        n = write(fd, payload, sizeof(payload) - 1);
        if (n != (ssize_t)(sizeof(payload) - 1)) {
                fprintf(stderr, "short write: %zd\n", n);
                return 2;
        }
        close(fd);

        char buf[128] = {0};
        fd = open("/dev/myfirst/0", O_RDONLY);
        if (fd < 0) { perror("open R"); return 3; }
        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) { perror("read"); return 4; }
        close(fd);

        if ((size_t)n != sizeof(payload) - 1 ||
            memcmp(buf, payload, n) != 0) {
                fprintf(stderr, "mismatch: wrote %zu, read %zd\n",
                    sizeof(payload) - 1, n);
                return 5;
        }

        printf("round-trip OK: %zd bytes\n", n);
        return 0;
}
```

2. Compilar y ejecutar:
   ```sh
   % cc -o rw_myfirst rw_myfirst.c
   % sudo ./rw_myfirst
   round-trip OK: 24 bytes
   ```
3. Inspecciona `dmesg` para ver las dos aperturas y los dos destructores.

**Criterios de éxito:**

- El programa imprime `round-trip OK: 24 bytes`.
- `dmesg` muestra un par apertura/destructor para la escritura y otro para la lectura.

**Errores habituales:**

- Escribir menos bytes que el payload sin comprobar el valor de retorno. `write(2)` puede devolver un conteo corto; la prueba debe gestionarlo.
- Confundir `O_WRONLY` con `O_RDONLY`. `open(2)` impone el modo contra los bits de acceso del nodo; abrir con el modo incorrecto devuelve `EACCES` (o similar).
- Asumir que `read(2)` devuelve el conteo solicitado. Puede devolver menos; de nuevo, el código llamante debe iterar.

### Laboratorio 9.6: Inspeccionando ida y vuelta binaria

**Objetivo:** Confirmar que el driver gestiona datos binarios arbitrarios, no solo texto, enviando bytes aleatorios y verificando que los mismos bytes regresan.

1. Con la Etapa 3 cargada y vacía, escribe 256 bytes aleatorios:
   ```sh
   % sudo dd if=/dev/urandom of=/tmp/sent bs=256 count=1
   % sudo dd if=/tmp/sent of=/dev/myfirst/0 bs=256 count=1
   ```
2. Lee la misma cantidad de bytes:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/received bs=256 count=1
   ```
3. Compara:
   ```sh
   % cmp /tmp/sent /tmp/received && echo MATCH
   MATCH
   ```
4. Inspecciona ambos archivos byte a byte:
   ```sh
   % od -An -tx1 /tmp/sent | head -2
   % od -An -tx1 /tmp/received | head -2
   ```
5. Prueba un patrón patológico: todo ceros, todo `0xff`, y luego un archivo lleno de un único byte. Confirma que cada patrón hace la ida y vuelta de forma exacta.

**Criterios de éxito:**

- `cmp` no informa de diferencias.
- El driver preserva cada bit de la entrada.
- Sin reordenación de bytes, sin "interpretación útil", sin transformaciones inesperadas.

Este laboratorio es breve pero importante: verifica que tu driver es un almacén de bytes transparente, no un filtro de texto que interpreta algunos bytes de forma especial. Si alguna vez ves diferencias entre los archivos enviado y recibido, tienes un fallo en la ruta de transferencia, probablemente un recuento erróneo de longitud o un error de uno en la aritmética del buffer.

### Laboratorio 9.7: Observar un driver en ejecución de extremo a extremo

**Objetivo:** Combinar sysctl, dmesg, truss y vmstat en una única observación de extremo a extremo del driver de la Etapa 3 bajo carga real. Este laboratorio no tiene código nuevo; es el puente entre "escribí el driver" y "puedo ver lo que está haciendo".

**Pasos:**

1. Con la Etapa 3 cargada en estado limpio, abre cuatro terminales. El terminal A ejecutará los ciclos de carga y descarga del driver. El terminal B monitorizará sysctl. El terminal C seguirá dmesg. El terminal D ejecutará la carga de trabajo del usuario.
2. **Terminal A:**
   ```sh
   % sudo kldload ./myfirst.ko
   % vmstat -m | grep devbuf
   ```
   Anota los valores de `InUse` y `MemUse` de la fila `devbuf`.
3. **Terminal B:**
   ```sh
   % watch -n 1 sysctl dev.myfirst.0.stats
   ```
4. **Terminal C:**
   ```sh
   % sudo dmesg -c > /dev/null
   % sudo dmesg -w
   ```
   El `-c` borra los mensajes acumulados; el `-w` espera nuevos mensajes.
5. **Terminal D:**
   ```sh
   % cd examples/part-02/ch09-reading-and-writing/userland
   % make
   % sudo truss ./rw_myfirst rt 2>&1 | tail -10
   ```
6. Comprueba el terminal B: deberías ver `open_count` incrementarse en 2 (uno para la escritura y otro para la lectura), `active_fhs` volver a 0, y `bytes_read == bytes_written`.
7. Comprueba el terminal C: deberías ver dos líneas de apertura y dos líneas de destructor procedentes de `device_printf`.
8. En el terminal A, ejecuta `vmstat -m | grep devbuf` de nuevo. `InUse` y `MemUse` deberían haber vuelto a sus valores previos a la carga más lo que el propio driver asignó (típicamente solo el buffer de 4 KiB y el softc).
9. **Prueba de estrés:** en el terminal D,
   ```sh
   % sudo ./stress_rw -s 5
   ```
   Observa el terminal B. Deberías ver oscilar `bufused`, crecer los contadores y que `active_fhs` llegue a 2 mientras la prueba se ejecuta.
10. Cuando la prueba de estrés termine, en el terminal B verifica que `active_fhs` es 0. En el terminal A:
    ```sh
    % sudo kldunload myfirst
    % vmstat -m | grep devbuf
    ```
    `InUse` debería haber vuelto a su referencia previa a la carga. Si no es así, tu driver ha perdido una asignación y `vmstat -m` te acaba de informar de ello.

**Criterios de éxito:**

- Los contadores sysctl coinciden con la carga de trabajo que ejecutaste.
- Dmesg muestra un par apertura/destructor por cada apertura/cierre de descriptor.
- La salida de truss coincide con tu modelo mental de lo que hizo el programa.
- `vmstat -m | grep devbuf` vuelve a su referencia tras la descarga.
- Sin panics, sin advertencias, sin deriva inexplicable de contadores.

**Por qué importa este laboratorio:** es el primer laboratorio que ejercita toda la cadena de observabilidad a la vez. En producción, la señal de que algo va mal casi nunca llega de un crash; llega de un contador que se ha desviado de sus límites, de una línea de `dmesg` que nadie esperaba, o de una lectura de `vmstat -m` que no se corresponde con la realidad. Desarrollar el hábito de observar las cuatro superficies al mismo tiempo es lo que separa "escribí un driver" de "soy responsable de un driver".



## Ejercicios de desafío

Estos desafíos amplían el material sin introducir temas que corresponden a capítulos posteriores. Cada uno utiliza únicamente los primitivos que hemos presentado. Inténtalos antes de consultar el árbol de ejemplos; el aprendizaje está en el intento, no en la respuesta.

### Desafío 9.1: Contadores de lectura por descriptor

Extiende la Etapa 2 para que el contador de `reads` por descriptor se exponga mediante un sysctl. El contador debe estar disponible por descriptor activo, lo que implica un sysctl por `fh` en lugar de uno por softc.

Este desafío es más difícil de lo que parece: los sysctls se asignan y liberan en momentos conocidos del ciclo de vida del softc, y la estructura por descriptor solo vive mientras dure su descriptor. Una solución limpia registra un nodo sysctl por `fh` en `d_open` y lo cancela en el destructor. Ten cuidado con los tiempos de vida; el contexto sysctl debe liberarse antes que la memoria del `fh`.

*Pista:* `sysctl_ctx_init` y `sysctl_ctx_free` son por contexto. Puedes dar a cada `fh` su propio contexto y liberarlo en el destructor.

*Alternativa:* mantener una lista enlazada de punteros a `fh` en el softc (bajo el mutex) y exponerla mediante un manejador sysctl personalizado que recorra la lista bajo demanda. Este es el patrón que usa `/usr/src/sys/kern/tty_info.c` para estadísticas por proceso.

### Desafío 9.2: Una prueba con `readv(2)`

Escribe un programa de usuario que use `readv(2)` para leer del driver en tres buffers separados de 8, 16 y 32 bytes. Confirma que el driver entrega bytes en los tres buffers en secuencia.

El kernel y `uiomove(9)` ya gestionan `readv(2)`; el driver no necesita cambios. El propósito de este desafío es convencerte de ese hecho.

*Pista:* `struct iovec iov[3] = {{buf1, 8}, {buf2, 16}, {buf3, 32}};`, después `readv(fd, iov, 3)`. El valor de retorno es el total de bytes entregados a los tres buffers; los valores individuales de `iov_len` no se modifican en el lado del usuario.

### Desafío 9.3: Demostración de escritura corta

Modifica `myfirst_write` de la Etapa 2 para que acepte como máximo 128 bytes por llamada, independientemente de `uio_resid`. Un programa de usuario que escriba 1024 bytes debería ver una escritura corta de 128 cada vez.

Luego escribe un programa de prueba corto que escriba 1024 bytes en una única llamada a `write(2)`, observe el valor de retorno de escritura corta e itere hasta que se hayan aceptado los 1024 bytes.

Preguntas para reflexionar:

- ¿Gestiona `cat` correctamente las escrituras cortas? (Sí.)
- ¿Las gestiona correctamente `echo > /dev/myfirst/0 "..."`? (Normalmente sí, a través de `printf` en el shell, pero no siempre; vale la pena comprobarlo.)
- ¿Qué ocurre si eliminas el comportamiento de escritura corta e intentas superar el tamaño del buffer? (Obtienes `ENOSPC` tras la primera escritura de 4096 bytes.)

Este desafío enseña a separar "el driver hace lo correcto" de "los programas de usuario asumen lo que hacen los drivers".

### Desafío 9.4: Un sensor para `ls -l`

Haz que la respuesta del driver a una lectura dependa de la salida de `ls -l` del propio dispositivo. Es decir: cada lectura produce la marca de tiempo actual del nodo de dispositivo.

*Pista:* `sc->cdev->si_ctime` y `sc->cdev->si_mtime` son campos `struct timespec` del cdev. Puedes convertirlos en una cadena usando formato `printf`, colocarla en un buffer del kernel y enviarla al espacio de usuario con `uiomove_frombuf(9)`.

*Advertencia:* devfs puede actualizar `si_ctime` / `si_mtime` a medida que se accede a los nodos. Observa qué ocurre cuando ejecutas `touch /dev/myfirst/0` y lees de nuevo.

### Desafío 9.5: Un driver de eco invertido

Modifica la Etapa 3 de modo que cada lectura devuelva los bytes en orden inverso al que se escribieron. Una escritura de `"hello"` seguida de una lectura debería producir `"olleh"`.

Este desafío trata exclusivamente de la gestión del buffer. Las llamadas a `uiomove` permanecen igual; lo que cambias son las direcciones que les pasas.

*Pista:* Puedes invertir el buffer en cada lectura (costoso) o almacenar los bytes en orden inverso en la escritura (más barato). Ninguno es la respuesta «correcta»; cada opción tiene propiedades diferentes de corrección y concurrencia. Elige una y arguméntala en un comentario.

### Desafío 9.6: Viaje de ida y vuelta binario

Escribe un programa de usuario que escriba un `struct timespec` en el driver y luego lea uno de vuelta. Compara las dos estructuras. ¿Son iguales? Deberían serlo, porque `myfirst` es un almacén de bytes transparente.

Amplía el programa para escribir dos valores `struct timespec`, luego haz `lseek(fd, sizeof(struct timespec), SEEK_SET)` y lee el segundo. ¿Qué ocurre? (Pista: la FIFO no admite seeks de forma significativa.)

Este desafío ilustra el punto «la lectura y la escritura transportan bytes, no tipos» de la sección sobre transferencia segura de datos. Los bytes hacen el viaje de ida y vuelta perfectamente; la información de tipo no.

### Desafío 9.7: Un conjunto de pruebas de vista hexadecimal

Escribe un script de shell corto que, dado un número de bytes N, genere N bytes aleatorios con `dd if=/dev/urandom bs=$N count=1`, los envíe por pipe a tu driver de la Etapa 3, luego los lea de vuelta con `dd if=/dev/myfirst/0 bs=$N count=1`, y compare las dos secuencias con `cmp`. El script debe informar de éxito cuando las secuencias coincidan y de una salida similar a un diff cuando no coincidan. Ejecútalo con N = 1, 2, 4, ..., 4096 para cubrir tamaños pequeños, de frontera y que llenen la capacidad.

Preguntas que responder mientras ejecutas la barrida:

- ¿Todos los tamaños hacen el viaje de ida y vuelta correctamente hasta 4096 inclusive?
- Con 4097, ¿qué hace el driver? ¿El conjunto de pruebas informa del error de forma significativa?
- ¿Hay algún tamaño en el que `cmp` informe de una diferencia? Si es así, ¿cuál fue la causa subyacente?

Este desafío recompensa la combinación de las herramientas de la sección Flujo de trabajo práctico: `dd` para transferencias precisas, `cmp` para verificación a nivel de byte, `sysctl` para contadores y el shell para la orquestación. Un conjunto de pruebas robusto como este es el tipo de hábito que se amortiza cada vez que refactorizas un driver y quieres saber rápidamente si el comportamiento sigue siendo el correcto.

### Desafío 9.8: ¿Quién tiene el descriptor abierto?

Escribe un pequeño programa C que abra `/dev/myfirst/0`, se bloquee en `pause()` (para mantener el descriptor indefinidamente) y se ejecute hasta recibir `SIGTERM`. En un segundo terminal, ejecuta `fstat | grep myfirst` y luego `fuser /dev/myfirst/0`. Observa la salida. Ahora intenta hacer `kldunload myfirst`. ¿Qué error obtienes? ¿Por qué?

Ahora mata al proceso que mantiene el descriptor con `SIGTERM` o simplemente con `kill`. Observa cómo se dispara el destructor en `dmesg`. Intenta `kldunload` de nuevo. Debería funcionar.

Este desafío es breve, pero asienta una de las invariantes más sutiles del capítulo: un driver no puede descargarse mientras haya algún descriptor de archivo abierto sobre uno de sus cdevs, y FreeBSD ofrece a los operadores un conjunto estándar de herramientas para encontrar al proceso que lo mantiene. La próxima vez que un `kldunload` en el mundo real falle con `EBUSY`, ya habrás visto antes la forma del problema.



## Resolución de problemas comunes

Todos los errores de `d_read` / `d_write` que probablemente cometerás caen en un pequeño número de categorías. Esta sección es una breve guía de campo.

### "Mi driver devuelve cero bytes aunque escribí datos"

Normalmente se trata de uno de dos bugs.

**Bug 1**: Olvidaste actualizar `bufused` (o su equivalente) tras el `uiomove` exitoso. La escritura llegó, los bytes se movieron, pero el estado del driver nunca reflejó la llegada. La siguiente lectura ve `bufused == 0` y notifica EOF.

Solución: actualiza siempre tus campos de seguimiento dentro de `if (error == 0) { ... }` después de que `uiomove` retorne.

**Bug 2**: Restableces `bufused` (o `bufhead`) en algún lugar inadecuado. Un patrón habitual es añadir una línea de restablecimiento dentro de `d_open` o `d_close` «por limpieza». Eso borra los datos que el llamador anterior escribió.

Solución: restablece el estado global del driver solo en `attach` (al cargar) o en `detach` (al descargar). El estado por descriptor pertenece a `fh`, se restablece con `malloc(M_ZERO)` y el destructor se encarga de limpiarlo.

### "Mis lecturas devuelven basura"

El buffer no está inicializado. `malloc(9)` sin `M_ZERO` devuelve un bloque de memoria cuyo contenido es indefinido. Si tu `d_read` accede más allá de `bufused`, o lee desde desplazamientos que no se han escrito, los bytes que ves son restos de la memoria que el kernel reciclió.

Solución: pasa siempre `M_ZERO` a `malloc` en `attach`. Limita siempre las lecturas a la marca de nivel máximo actual (`bufused`), no al tamaño total del buffer (`buflen`).

Existe una variante más grave de este bug. Un driver que devuelve memoria del kernel no inicializada al espacio de usuario acaba de filtrar estado del kernel hacia el espacio de usuario. En producción, eso es un agujero de seguridad. En desarrollo es un bug; en producción es un CVE.

### "El kernel entra en pánico con un fallo de página en una dirección de usuario"

Llamaste a `memcpy` o `bcopy` directamente sobre un puntero de usuario en lugar de pasar por `uiomove` / `copyin` / `copyout`. El acceso provocó un fallo, el kernel no tenía ningún manejador de fallos instalado y el resultado fue un pánico.

Solución: nunca desreferencies un puntero de usuario directamente. Rútalo a través de `uiomove(9)` (en los manejadores) o de `copyin(9)` / `copyout(9)` (en otros contextos).

### "El driver se niega a descargarse"

Tienes al menos un descriptor de archivo aún abierto. `detach` devuelve `EBUSY` cuando `active_fhs > 0`; el módulo no se descargará hasta que todos los `fh` hayan sido destruidos.

Solución: cierra el descriptor en el espacio de usuario. Si un proceso en segundo plano lo está manteniendo, mata el proceso (tras confirmar que es tuyo; no mates daemons del sistema). `fstat -p <pid>` muestra qué archivos tiene abiertos un proceso; `fuser /dev/myfirst/0` muestra qué procesos tienen el nodo abierto.

El Capítulo 10 introducirá los patrones de `destroy_dev_drain` para drivers que necesitan forzar a un lector bloqueado a salir. El Capítulo 9 no bloquea, así que este problema no surge en operación normal; cuando surge, es porque el espacio de usuario está manteniendo el descriptor en algún lugar inesperado.

### "Mi manejador de escritura devuelve EFAULT"

Tu llamada a `uiomove` encontró una dirección de usuario inválida. Las causas habituales:

- Un programa de usuario llamó a `write(fd, NULL, n)` o `write(fd, (void*)0xdeadbeef, n)`.
- Un programa de usuario escribió un puntero que ya había liberado.
- Pasaste accidentalmente un puntero del kernel como destino a `uiomove`. Esto puede ocurrir si construyes un uio a mano para datos en espacio del kernel y luego lo pasas a un manejador que espera un uio en espacio de usuario. El `copyout` resultante ve una dirección «de usuario» que en realidad es una dirección del kernel; dependiendo de la arquitectura, obtienes `EFAULT` o una corrupción sutil.

Solución: comprueba `uio->uio_segflg`. Para manejadores dirigidos por el usuario, debería ser `UIO_USERSPACE`. Si estás pasando un uio en espacio del kernel, asegúrate de que `uio_segflg == UIO_SYSSPACE` y de que tus rutas de código conocen la diferencia.

### "Mis contadores son incorrectos con escrituras concurrentes"

Dos escritores compitieron en `bufused`. Cada uno leyó el valor actual, le sumó y volvió a escribirlo, y el segundo escritor sobreescribió la actualización del primero con un valor desactualizado.

Solución: toma `sc->mtx` alrededor de cada operación de lectura-modificación-escritura del estado compartido. La Parte 3 convierte esto en un tema de primer orden; para el Capítulo 9, un único mutex alrededor de toda la sección crítica es suficiente.

### "Los contadores sysctl no reflejan el estado real"

Dos variantes.

**Variante A**: el contador es un `size_t`, pero la macro sysctl es `SYSCTL_ADD_U64`. En arquitecturas de 32 bits, la macro lee 8 bytes cuando el campo solo tiene 4 bytes de ancho; la mitad del valor es basura.

Solución: haz que la macro sysctl coincida con el tipo del campo. `size_t` se empareja con `SYSCTL_ADD_UINT` en plataformas de 32 bits y con `SYSCTL_ADD_U64` en plataformas de 64 bits. Para garantizar la portabilidad, usa `uint64_t` para los contadores y convierte al actualizar.

**Variante B**: el contador nunca se actualiza porque la actualización está dentro del bloque `if (error == 0)` y `uiomove` devolvió un error distinto de cero. En realidad, ese es el comportamiento correcto: no debes contar bytes que no moviste. El síntoma solo parece un bug si intentas usar el contador para depurar el error.

Solución: añade un contador `error_count` que se incremente en cada retorno distinto de cero, independientemente de `bytes_read` y `bytes_written`. Útil para la depuración.

### "La primera lectura tras una carga reciente devuelve cero bytes"

Normalmente es intencionado. En la Etapa 3, un buffer vacío devuelve cero bytes. Si esperabas el mensaje estático de la Etapa 1, comprueba que estés ejecutando el driver de la Etapa 1 y no uno posterior.

Si es involuntario, verifica que `attach` esté estableciendo `sc->buf`, `sc->buflen` y `sc->message_len` como se espera. Un bug habitual es copiar y pegar el código de attach de la Etapa 1 en la Etapa 2 y dejar la asignación de `sc->message = ...` en su sitio, que luego tiene prioridad sobre la línea de `malloc`.

### "La compilación falla con una referencia desconocida a uiomove_frombuf"

Olvidaste incluir `<sys/uio.h>`. Añádelo en la parte superior de `myfirst.c`.

### "Mi manejador se llama dos veces por un solo read(2)"

Casi con toda seguridad no es así. Lo más probable es que tu manejador esté siendo llamado una vez con `uio_iovcnt > 1` (una llamada `readv(2)`), y que dentro de `uiomove` cada entrada del iovec se esté agotando por turnos. El bucle interno de `uiomove` puede realizar múltiples llamadas a `copyout` en lo que es una única invocación de tu manejador.

Verifica añadiendo un `device_printf` a la entrada y salida de tu `d_read`. Deberías ver una entrada y una salida por cada llamada `read(2)` en el espacio de usuario, independientemente del número de iovec.



## Patrones de contraste: manejadores correctos e incorrectos

La guía de resolución de problemas anterior es reactiva: ayuda cuando algo ya ha ido mal. Esta sección es su complemento prescriptivo. Cada entrada muestra una forma plausible pero incorrecta de escribir parte de un manejador, la empareja con la reescritura correcta y explica la diferencia. Estudiar los contrastes de antemano es la forma más rápida de evitar los bugs desde el principio.

Lee cada par con atención. La versión correcta es el patrón al que debes recurrir; la versión incorrecta es la forma que pueden producir tus propias manos cuando trabajas deprisa. Reconocer el error en situaciones reales, meses después, vale los cinco minutos que tardas en interiorizar la diferencia hoy.

### Contraste 1: Devolver un recuento de bytes

**Incorrecto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... */
        error = uiomove_frombuf(sc->message, sc->message_len, uio);
        if (error)
                return (error);
        return (sc->message_len); /* BAD: returning a count */
}
```

**Correcto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... */
        return (uiomove_frombuf(sc->message, sc->message_len, uio));
}
```

**Por qué es importante.** El valor de retorno del manejador es un errno, no un recuento. El kernel calcula el recuento de bytes a partir del cambio en `uio->uio_resid` y lo comunica al espacio de usuario. Un retorno positivo distinto de cero se interpreta como un errno; si devolvieras `sc->message_len`, el llamador recibiría un valor `errno` muy extraño. Por ejemplo, devolver `75` se manifestaría como `errno = 75`, que en FreeBSD resulta ser `EPROGMISMATCH`. El bug es a la vez incorrecto y profundamente confuso para cualquiera que lo observe desde el lado del usuario.

La regla es simple y absoluta: los manejadores devuelven valores errno, nunca recuentos. Si quieres conocer el recuento de bytes, calcúlalo a partir del uio.

### Contraste 2: Gestionar una petición de longitud cero

**Incorrecto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        if (uio->uio_resid == 0)
                return (EINVAL); /* BAD: zero-length is legal */
        /* ... */
}
```

**Correcto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* No special case. uiomove handles zero-resid cleanly. */
        return (uiomove_frombuf(sc->message, sc->message_len, uio));
}
```

**Por qué es importante.** Una llamada `read(fd, buf, 0)` es una llamada UNIX completamente válida. Un driver que la rechace con `EINVAL` rompe los programas que utilizan lecturas de cero bytes para comprobar el estado del descriptor. `uiomove` devuelve cero inmediatamente si el uio no tiene nada que mover; tu manejador no necesita tratarlo como un caso especial. Tratarlo incorrectamente como caso especial es peor que no tratarlo como caso especial en absoluto.

### Contraste 3: Aritmética de capacidad del buffer

**Incorrecto:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - sc->bufused;
towrite = uio->uio_resid;            /* BAD: no clamp */
error = uiomove(sc->buf + sc->bufused, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
mtx_unlock(&sc->mtx);
return (error);
```

**Correcto:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - sc->bufused;
if (avail == 0) {
        mtx_unlock(&sc->mtx);
        return (ENOSPC);
}
towrite = MIN((size_t)uio->uio_resid, avail);
error = uiomove(sc->buf + sc->bufused, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
mtx_unlock(&sc->mtx);
return (error);
```

**Por qué importa.** La versión defectuosa pasa a `uiomove` una longitud de `uio_resid`, que puede superar la capacidad restante del buffer. `uiomove` no moverá más bytes de los que indica `uio_resid`, pero el *destino* es `sc->buf + sc->bufused`, y la aritmética no tiene en cuenta `sc->buflen`. Si el usuario escribe 8 KiB en un buffer de 4 KiB con `bufused = 0`, el manejador escribirá 4 KiB más allá del final de `sc->buf`. Eso es un desbordamiento clásico del heap del kernel: el crash no será inmediato, no implicará a tu driver, y puede manifestarse como un panic dentro de un subsistema completamente ajeno medio segundo después.

La versión correcta limita la transferencia a `avail`, garantizando que la aritmética de punteros permanezca dentro del buffer. El límite es una única llamada a `MIN`, y no es opcional.

### Contraste 4: Mantener un spin lock durante `uiomove`

**Con errores:**

```c
mtx_lock_spin(&sc->spin);            /* BAD: spin lock, not a regular mutex */
error = uiomove(sc->buf + off, n, uio);
mtx_unlock_spin(&sc->spin);
return (error);
```

**Correcto:**

```c
mtx_lock(&sc->mtx);                  /* MTX_DEF mutex */
error = uiomove(sc->buf + off, n, uio);
mtx_unlock(&sc->mtx);
return (error);
```

**Por qué importa.** `uiomove(9)` puede dormirse. Cuando llama a `copyin` o `copyout`, la página de usuario puede estar paginada al disco, y el kernel puede necesitar recuperarla desde allí, lo que implica esperar una operación de E/S. Dormirse mientras se mantiene un spin lock (`MTX_SPIN`) provoca un deadlock en el sistema. El framework `WITNESS` de FreeBSD entra en pánico la primera vez que ocurre esto, si `WITNESS` está habilitado. En un kernel sin `WITNESS`, el resultado es un livelock silencioso.

La regla es sencilla: no se pueden mantener spin locks durante llamadas a funciones que puedan dormirse, y `uiomove` puede dormirse. Usa un mutex `MTX_DEF` (el tipo por defecto, y el que usa `myfirst`) para el estado del softc que manejan los handlers de E/S.

### Contraste 5: Reiniciar estado compartido en `d_open`

**Con errores:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        /* ... */
        mtx_lock(&sc->mtx);
        sc->bufused = 0;                 /* BAD: wipes other readers' data */
        sc->bufhead = 0;
        mtx_unlock(&sc->mtx);
        /* ... */
}
```

**Correcto:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        /* ... no shared-state reset ... */
        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* fh starts zeroed, which is correct per-descriptor state */
        /* ... register fh with devfs_set_cdevpriv, bump counters ... */
}
```

**Por qué importa.** `d_open` se ejecuta una vez por descriptor. Si dos lectores abren el dispositivo, el segundo open borrará lo que dejó el primero. El estado global del driver (`sc->bufused`, `sc->buf`, los contadores) pertenece al driver completo y solo se reinicia en `attach` y `detach`. El estado por descriptor pertenece a `fh`, que `malloc(M_ZERO)` inicializa a ceros automáticamente.

Un driver que reinicia el estado compartido en `d_open` parece funcionar con un único abridor, pero corrompe silenciosamente el estado cuando aparecen dos. El fallo es invisible hasta el día en que dos usuarios acceden al dispositivo a la vez.

### Contraste 6: Contabilizar antes de conocer el resultado

**Con errores:**

```c
sc->bytes_written += towrite;       /* BAD: count before success */
error = uiomove(sc->buf + tail, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
```

**Correcto:**

```c
error = uiomove(sc->buf + tail, towrite, uio);
if (error == 0) {
        sc->bufused += towrite;
        sc->bytes_written += towrite;
}
```

**Por qué importa.** Si `uiomove` falla a mitad del proceso, puede que algunos bytes se hayan transferido y otros no. El contador `sc->bytes_written` debe reflejar lo que realmente llegó al buffer, no lo que el driver intentó. Actualizar los contadores antes de conocer el resultado hace que los contadores mientan. Si un usuario lee el sysctl para diagnosticar un problema, verá números que no se corresponden con la realidad.

La regla: actualiza los contadores dentro de la rama `if (error == 0)`, para que el éxito sea el único camino que los incrementa. Es un coste pequeño para obtener un gran beneficio en corrección.

### Contraste 7: Desreferenciar un puntero de usuario directamente

**Con errores:**

```c
/* Imagine the driver somehow gets a user pointer, maybe through ioctl. */
static int
handle_user_string(void *user_ptr)
{
        char buf[128];
        memcpy(buf, user_ptr, 128);     /* BAD: user pointer in memcpy */
        /* ... */
}
```

**Correcto:**

```c
static int
handle_user_string(void *user_ptr)
{
        char buf[128];
        int error;

        error = copyin(user_ptr, buf, sizeof(buf));
        if (error != 0)
                return (error);
        /* ... */
}
```

**Por qué importa.** `memcpy` asume que ambos punteros hacen referencia a memoria accesible en el espacio de direcciones actual. Un puntero de usuario no cumple esa condición. Según la plataforma, el resultado de pasar un puntero de usuario a `memcpy` en contexto del kernel puede ir desde un fallo equivalente a `EFAULT` (en amd64 con SMAP habilitado) hasta la corrupción silenciosa de datos (en plataformas sin separación usuario/kernel), pasando por un kernel panic directo.

`copyin` y `copyout` son la única forma correcta de acceder a la memoria de usuario desde el contexto del kernel. Instalan un manejador de fallos, validan la dirección, recorren las tablas de páginas de forma segura y devuelven `EFAULT` ante cualquier fallo. El coste de rendimiento son unas pocas instrucciones adicionales; el beneficio en corrección es que el kernel no entra en pánico cuando se ejecuta un programa de usuario con errores.

### Contraste 8: Fuga de una estructura por descriptor en un fallo de `d_open`

**Con errores:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_fh *fh;
        int error;

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* ... set fields ... */
        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0)
                return (error);         /* BAD: fh is leaked */
        return (0);
}
```

**Correcto:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_fh *fh;
        int error;

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* ... set fields ... */
        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0) {
                free(fh, M_DEVBUF);     /* free before returning */
                return (error);
        }
        return (0);
}
```

**Por qué importa.** Cuando `devfs_set_cdevpriv` falla, el kernel no registra el destructor, por lo que el destructor nunca se ejecutará para este `fh`. Si el handler retorna sin liberar `fh`, se produce una fuga de memoria. Con carga sostenida, los fallos repetidos de `d_open` pueden acumular suficiente fuga de memoria como para desestabilizar el kernel.

La regla: en las rutas de desenrollado de errores, toda asignación realizada hasta ese momento debe liberarse. El lector del Capítulo 8 ya ha visto este patrón en attach; se aplica igualmente a `d_open`.

### Cómo usar esta tabla de contrastes

Estos ocho pares no son una lista exhaustiva. Son los errores que hemos visto con más frecuencia en los primeros drivers de estudiantes, y los errores que el texto del capítulo ha intentado ayudarte a evitar. Léelos una vez ahora. Antes de escribir tu primer driver real fuera de este libro, vuélvelos a leer.

Un hábito útil durante el desarrollo: cada vez que termines un handler, repásalo mentalmente contra la tabla de contrastes. ¿Devuelve el handler un recuento de bytes? ¿Trata como caso especial el resid cero? ¿Tiene un limitador de capacidad? ¿Es correcto el tipo de mutex? ¿Reinicia el estado compartido en `d_open`? ¿Contabiliza los bytes en caso de fallo? ¿Desreferencia algún puntero de usuario directamente? ¿Produce fuga de memoria en un fallo de `d_open`? Ocho preguntas, cinco minutos. El precio de la revisión es pequeño; el coste de llevar uno de estos errores a producción es grande.



## Autoevaluación antes del Capítulo 10

El Capítulo 9 ha abarcado mucho terreno. Antes de dejarlo, repasa la siguiente lista de verificación. Si algún punto te hace dudar, merece la pena releer la sección correspondiente antes de continuar. Esto no es un examen; es una forma rápida de identificar los puntos donde tu modelo mental puede todavía ser débil.

**Conceptos:**

- [ ] Puedo explicar en una frase para qué sirve `struct uio`.
- [ ] Puedo nombrar los tres campos de `struct uio` que mi driver lee con más frecuencia.
- [ ] Puedo explicar por qué `uiomove(9)` se prefiere sobre `copyin` / `copyout` dentro de `d_read` y `d_write`.
- [ ] Puedo explicar por qué `memcpy` a través de la frontera usuario/kernel no es seguro.
- [ ] Puedo explicar la diferencia entre `ENXIO`, `EAGAIN`, `ENOSPC` y `EFAULT` en términos de driver.

**Mecánica:**

- [ ] Puedo escribir un handler `d_read` mínimo que sirva un buffer fijo usando `uiomove_frombuf(9)`.
- [ ] Puedo escribir un handler `d_write` mínimo que añada datos a un buffer del kernel con un limitador de capacidad correcto.
- [ ] Sé dónde colocar la adquisición y liberación del mutex alrededor de la transferencia.
- [ ] Sé cómo propagar un errno de `uiomove` de vuelta al espacio de usuario.
- [ ] Sé cómo marcar una escritura como completamente consumida con `uio_resid = 0`.

**Observabilidad:**

- [ ] Puedo leer `sysctl dev.myfirst.0.stats` e interpretar cada contador.
- [ ] Puedo detectar una fuga de memoria con `vmstat -m | grep devbuf`.
- [ ] Puedo usar `truss(1)` para ver qué syscalls realiza mi programa de prueba.
- [ ] Puedo usar `fstat(1)` o `fuser(8)` para encontrar quién tiene abierto un descriptor.

**Trampas:**

- [ ] No devolvería un recuento de bytes desde `d_read` / `d_write`.
- [ ] No rechazaría una petición de longitud cero con `EINVAL`.
- [ ] No reiniciaría `sc->bufused` dentro de `d_open`.
- [ ] No mantendría un spin lock durante una llamada a `uiomove`.

Cualquier «no» aquí es una señal, no un veredicto. Vuelve a leer la sección correspondiente; realiza un pequeño experimento en tu laboratorio; regresa a la lista de verificación. Cuando hayas marcado todas las casillas, estarás completamente preparado para el Capítulo 10.



## En resumen

Acabas de implementar los puntos de entrada que dan vida a un driver. Al final del Capítulo 7, tu driver tenía un esqueleto. Al final del Capítulo 8, tenía una puerta bien formada. Ahora, al final del Capítulo 9, los datos fluyen a través de la puerta en ambas direcciones.

La lección central del capítulo es más corta de lo que parece. Todo `d_read` que escribas tiene la misma columna vertebral de tres líneas: obtener el estado por descriptor, verificar que el dispositivo sigue activo, llamar a `uiomove`. Todo `d_write` que escribas tiene una columna vertebral similar con una decisión extra (¿cuánto espacio tengo?) y un limitador (`MIN(uio_resid, avail)`) que previene el desbordamiento del buffer. Todo lo demás en el capítulo es contexto: por qué `struct uio` tiene la forma que tiene, por qué `uiomove` es el único transmisor seguro, por qué importan los valores de errno, por qué importan los contadores, por qué el buffer debe liberarse en cada ruta de error.

### Las tres ideas más importantes

**Primero, `struct uio` es el contrato entre tu driver y la maquinaria de E/S del kernel.** Lleva todo lo que tu handler necesita saber sobre una llamada: qué pidió el usuario, dónde está la memoria del usuario, en qué dirección debe moverse la transferencia y cuánto progreso se ha realizado. No necesitas memorizar los siete campos. Necesitas reconocer `uio_resid` (el trabajo restante), `uio_offset` (la posición, si te interesa) y `uio_rw` (la dirección), y debes confiar en `uiomove(9)` para el resto.

**Segundo, `uiomove(9)` es la frontera entre la memoria de usuario y la memoria del kernel.** Todo lo que tu driver transfiera entre ambas pasa a través de él (o de uno de sus parientes cercanos: `uiomove_frombuf`, `copyin`, `copyout`). Esto no es una sugerencia. El acceso directo a punteros a través de la frontera de confianza o bien corrompe memoria o bien filtra información, y el kernel no tiene una forma barata de detectar el error antes de que se convierta en un CVE. Si un puntero proviene del espacio de usuario, enrútalo a través de las funciones de frontera de confianza del kernel. Siempre.

**Tercero, un handler correcto suele ser un handler corto.** Si tu `d_read` o `d_write` tiene más de quince líneas, probablemente algo está mal. Los handlers más largos o bien duplican lógica que pertenece a otro lugar (en la gestión del buffer, en la configuración del estado por descriptor, en los sysctls), o bien intentan hacer algo que el driver no debería hacer en un handler de la ruta de datos (normalmente, algo que pertenece a `d_ioctl`). Mantén los handlers cortos. Coloca la maquinaria que invocan en funciones auxiliares con nombres descriptivos. Tu yo futuro te lo agradecerá.

### La forma del driver con el que terminas el capítulo

Tu `myfirst` de la Fase 3 es un FIFO en memoria pequeño y honesto. Las características principales:

- Un buffer del kernel de 4 KiB, asignado en `attach` y liberado en `detach`.
- Un mutex por instancia que protege `bufhead`, `bufused` y los contadores asociados.
- Un `d_read` que vacía el buffer y avanza `bufhead`, colapsando a cero cuando el buffer se vacía.
- Un `d_write` que añade datos al buffer y devuelve `ENOSPC` cuando se llena.
- Contadores por descriptor almacenados en `struct myfirst_fh`, asignados en `d_open` y liberados en el destructor.
- Un árbol de sysctls que expone el estado activo del driver.
- Desenrollado limpio de errores en `attach` y ordenación limpia en `detach`.

Esa forma volverá a aparecer, reconocible, en la mitad de los drivers que leerás en la Parte 4 y la Parte 6. Es un patrón general, no una demostración puntual.

### Qué deberías practicar antes de empezar el Capítulo 10

Cinco ejercicios, en orden aproximado de dificultad creciente:

1. Reconstruye las tres fases desde cero, sin mirar el árbol de ejemplos. Compara tu resultado con el árbol después; las diferencias son lo que te queda por interiorizar.
2. Introduce un error intencionado en la Fase 3: olvida reiniciar `bufhead` cuando `bufused` llega a cero. Observa lo que ocurre en la segunda escritura grande. Explica el síntoma en términos del código.
3. Añade un sysctl que exponga `sc->buflen`. Hazlo de solo lectura. Después conviértelo en un ajustable que pueda establecerse en el momento de carga mediante `kenv` o `loader.conf` y recogerse en `attach`. (El Capítulo 10 revisita los ajustables de forma formal; esto es un avance.)
4. Escribe un script de shell que escriba datos aleatorios de longitud conocida en `/dev/myfirst/0` y los lea de vuelta a través de `sha256`. Compara los hashes. ¿Coinciden los hashes incluso cuando el tamaño de escritura supera el buffer? (No deberían; piensa por qué.)
5. Encuentra un driver bajo `/usr/src/sys/dev` que implemente tanto `d_read` como `d_write`. Lee sus handlers. Mapéalos contra los patrones de este capítulo. Buenos candidatos: `/usr/src/sys/dev/null/null.c` (ya lo conoces), `/usr/src/sys/dev/random/randomdev.c`, `/usr/src/sys/dev/speaker/spkr.c`.

### Una mirada al Capítulo 10

El Capítulo 10 toma el driver de la Fase 3 y lo hace escalar. Aparecen cuatro nuevas capacidades:

- **Un buffer circular** reemplaza al buffer lineal. Las escrituras y lecturas pueden ocurrir de forma continua sin el colapso explícito que usa la Fase 3.
- **Las lecturas bloqueantes** llegan. Un lector que llama a `read(2)` sobre un buffer vacío puede esperar hasta que haya datos disponibles, en lugar de devolver cero bytes inmediatamente. La primitiva del kernel es `msleep(9)`; el handler `d_purge` es la red de seguridad para el desmontaje.
- **Las E/S no bloqueantes** se convierten en una característica de primer orden. Los usuarios de `O_NONBLOCK` reciben `EAGAIN` donde una llamada bloqueante esperaría.
- **Integración con `poll(2)` y `kqueue(9)`**. Un programa de usuario puede esperar a que el dispositivo sea legible o escribible sin intentar la operación activamente. Esta es la forma estándar de integrar un dispositivo en un bucle de eventos.

Los cuatro capítulos siguientes se construyen sobre las mismas formas de `d_read` / `d_write` que acabas de implementar. Extenderás los manejadores en lugar de reescribirlos, y el estado por descriptor que ya tienes en marcha llevará la contabilidad necesaria. El capítulo anterior a ese (este) es donde el camino de I/O en sí es correcto. El capítulo 10 es donde ese camino se vuelve eficiente.

Antes de cerrar el archivo, una última palabra de aliento. El material de este capítulo no es tan difícil como puede parecer en una primera lectura. El patrón es pequeño. Las ideas son reales, pero son finitas, y acabas de ejercitarlas todas contra código que funciona de verdad. Cuando leas el `d_read` o el `d_write` de un driver real en el árbol, reconocerás lo que hace la función y por qué. Ya no eres un principiante. Eres un aprendiz con una herramienta real en las manos.

## Referencia: las declaraciones y funciones auxiliares usadas en este capítulo

Una referencia consolidada de las declaraciones, funciones auxiliares y constantes en las que se apoya el capítulo. Guarda esta página como referencia mientras escribes drivers; la mayoría de las preguntas de los principiantes se resuelven con una consulta en una de estas tablas.

### Firmas de `d_read` y `d_write`

De `/usr/src/sys/sys/conf.h`:

```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

El valor de retorno es cero en caso de éxito, y un errno positivo en caso de error. El conteo de bytes se calcula a partir del cambio en `uio->uio_resid` y se devuelve al espacio de usuario como valor de retorno de `read(2)` / `write(2)`.

### El `struct uio` canónico

De `/usr/src/sys/sys/uio.h`:

```c
struct uio {
        struct  iovec *uio_iov;         /* scatter/gather list */
        int     uio_iovcnt;             /* length of scatter/gather list */
        off_t   uio_offset;             /* offset in target object */
        ssize_t uio_resid;              /* remaining bytes to process */
        enum    uio_seg uio_segflg;     /* address space */
        enum    uio_rw uio_rw;          /* operation */
        struct  thread *uio_td;         /* owner */
};
```

### Las enumeraciones `uio_seg` y `uio_rw`

De `/usr/src/sys/sys/_uio.h`:

```c
enum uio_rw  { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_NOCOPY };
```

### La familia `uiomove`

De `/usr/src/sys/sys/uio.h`:

```c
int uiomove(void *cp, int n, struct uio *uio);
int uiomove_frombuf(void *buf, int buflen, struct uio *uio);
int uiomove_fromphys(struct vm_page *ma[], vm_offset_t offset, int n,
                     struct uio *uio);
int uiomove_nofault(void *cp, int n, struct uio *uio);
int uiomove_object(struct vm_object *obj, off_t obj_size, struct uio *uio);
```

En el código de drivers para principiantes, solo `uiomove` y `uiomove_frombuf` son habituales. Los demás están orientados a subsistemas específicos del kernel (I/O de páginas físicas, copias sin fallos de página, objetos respaldados por VM) y quedan fuera del alcance de este capítulo.

### `copyin` y `copyout`

De `/usr/src/sys/sys/systm.h`:

```c
int copyin(const void * __restrict udaddr,
           void * __restrict kaddr, size_t len);
int copyout(const void * __restrict kaddr,
            void * __restrict udaddr, size_t len);
int copyinstr(const void * __restrict udaddr,
              void * __restrict kaddr, size_t len,
              size_t * __restrict lencopied);
```

Úsalos en rutas de control (`d_ioctl`) donde llega un puntero de usuario fuera de la abstracción uio. Dentro de `d_read` y `d_write`, prefiere `uiomove`.

### Los bits de `ioflag` relevantes para los dispositivos de caracteres

De `/usr/src/sys/sys/vnode.h`:

```c
#define IO_NDELAY       0x0004  /* FNDELAY flag set in file table */
```

Se activa cuando el descriptor está en modo no bloqueante. Tu `d_read` o `d_write` puede usarlo para decidir si bloquear (flag ausente) o devolver `EAGAIN` (flag activo). La mayoría de los demás flags `IO_*` son de nivel de sistema de archivos y no son relevantes para los dispositivos de caracteres.

### Asignación de memoria

De `/usr/src/sys/sys/malloc.h`:

```c
void *malloc(size_t size, struct malloc_type *type, int flags);
void  free(void *addr, struct malloc_type *type);
```

Flags habituales: `M_WAITOK`, `M_NOWAIT`, `M_ZERO`. Tipos habituales para drivers: `M_DEVBUF` (genérico) o un tipo específico del driver declarado mediante `MALLOC_DECLARE` / `MALLOC_DEFINE`.

### Estado por apertura (del capítulo 8, usado aquí)

De `/usr/src/sys/sys/conf.h`:

```c
int  devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtr);
int  devfs_get_cdevpriv(void **datap);
void devfs_clear_cdevpriv(void);
```

El patrón es: asignar en `d_open`, registrar con `devfs_set_cdevpriv`, recuperar en cada manejador posterior con `devfs_get_cdevpriv`, y liberar en el destructor que `devfs_set_cdevpriv` registró.

### Valores de errno usados en este capítulo

| Errno         | Significado en el contexto de un driver                         |
|---------------|-----------------------------------------------------------------|
| `0`           | Éxito.                                                          |
| `ENXIO`       | Dispositivo no configurado (softc ausente, no conectado).       |
| `EFAULT`      | Dirección de usuario inválida. Habitualmente propagado desde `uiomove`. |
| `EIO`         | Error de entrada/salida. Problema de hardware.                  |
| `ENOSPC`      | Sin espacio en el dispositivo. Buffer lleno.                    |
| `EAGAIN`      | Bloquearía; relevante en modo no bloqueante (capítulo 10).      |
| `EINVAL`      | Argumento inválido.                                             |
| `EACCES`      | Permiso denegado en `open(2)`.                                  |
| `EPIPE`       | Tubería rota. No usado por `myfirst`.                           |

### Patrones útiles de `device_printf(9)`

```c
device_printf(sc->dev, "open via %s fh=%p\n", devtoname(sc->cdev), fh);
device_printf(sc->dev, "write rejected: buffer full (used=%zu)\n",
    sc->bufused);
device_printf(sc->dev, "read delivered %zd bytes\n",
    (ssize_t)(before - uio->uio_offset));
```

Están escritos pensando en la legibilidad. Una línea en `dmesg` que hay que descifrar es una línea que probablemente no se leerá cuando importa.

### Las tres etapas de un vistazo

| Etapa | `d_read`                                                     | `d_write`                                         |
|-------|--------------------------------------------------------------|---------------------------------------------------|
| 1     | Servir mensaje fijo mediante `uiomove_frombuf`               | Descartar escrituras (como `/dev/null`)            |
| 2     | Servir buffer hasta `bufused`                                | Añadir al buffer, `ENOSPC` si está lleno           |
| 3     | Vaciar buffer desde `bufhead`, reiniciar cuando quede vacío  | Añadir en `bufhead + bufused`, `ENOSPC` si está lleno |

La etapa 3 es la base sobre la que se construye el capítulo 10.

### Lista consolidada de archivos del capítulo

Archivos complementarios en `examples/part-02/ch09-reading-and-writing/`:

- `stage1-static-message/`: código fuente del driver de la etapa 1 y Makefile.
- `stage2-readwrite/`: código fuente del driver de la etapa 2 y Makefile.
- `stage3-echo/`: código fuente del driver de la etapa 3 y Makefile.
- `userland/rw_myfirst.c`: pequeño programa en C para probar ciclos de lectura y escritura.
- `userland/stress_rw.c`: prueba de estrés multiproceso para el laboratorio 9.3 y posteriores.
- `README.md`: un mapa breve del árbol de archivos complementarios.

Cada etapa es independiente; puedes construir, cargar y probar cualquiera de ellas sin necesidad de construir las demás. Los Makefiles son idénticos salvo por el nombre del driver (siempre `myfirst`) y los flags de ajuste opcionales.



## Apéndice A: Una mirada más detallada al bucle interno de `uiomove`

Para los lectores que quieran ver exactamente qué hace `uiomove(9)`, este apéndice recorre el bucle principal de `uiomove_faultflag` tal como aparece en `/usr/src/sys/kern/subr_uio.c`. No necesitas leer esto para escribir un driver. Está aquí porque una sola lectura del bucle aclarará todas las dudas que puedas tener después sobre la semántica de uio.

### La configuración

Al entrar, la función tiene:

- Un puntero del kernel `cp` proporcionado por el llamador (tu driver).
- Un entero `n` proporcionado por el llamador (el máximo de bytes a mover).
- El uio proporcionado por el despacho del kernel.
- Un booleano `nofault` que indica si los fallos de página durante la copia deben manejarse o son fatales.

Verifica algunos invariantes básicos: la dirección es `UIO_READ` o `UIO_WRITE`, el thread propietario es el thread actual cuando el segmento es espacio de usuario, y `uio_resid` es no negativo. Cualquier violación dispara un `KASSERT` y provocará un panic en un kernel con `INVARIANTS` activado.

### El bucle principal

```c
while (n > 0 && uio->uio_resid) {
        iov = uio->uio_iov;
        cnt = iov->iov_len;
        if (cnt == 0) {
                uio->uio_iov++;
                uio->uio_iovcnt--;
                continue;
        }
        if (cnt > n)
                cnt = n;

        switch (uio->uio_segflg) {
        case UIO_USERSPACE:
                switch (uio->uio_rw) {
                case UIO_READ:
                        error = copyout(cp, iov->iov_base, cnt);
                        break;
                case UIO_WRITE:
                        error = copyin(iov->iov_base, cp, cnt);
                        break;
                }
                if (error)
                        goto out;
                break;

        case UIO_SYSSPACE:
                switch (uio->uio_rw) {
                case UIO_READ:
                        bcopy(cp, iov->iov_base, cnt);
                        break;
                case UIO_WRITE:
                        bcopy(iov->iov_base, cp, cnt);
                        break;
                }
                break;
        case UIO_NOCOPY:
                break;
        }
        iov->iov_base = (char *)iov->iov_base + cnt;
        iov->iov_len -= cnt;
        uio->uio_resid -= cnt;
        uio->uio_offset += cnt;
        cp = (char *)cp + cnt;
        n -= cnt;
}
```

Cada iteración realiza una unidad de trabajo: copiar hasta `cnt` bytes (donde `cnt` es `MIN(iov->iov_len, n)`) entre la entrada iovec actual y el buffer del kernel. La dirección se elige mediante los dos bloques `switch` anidados. Tras una copia exitosa, todos los campos de contabilidad avanzan de forma coordinada: la entrada iovec se reduce en `cnt`, el resid del uio se reduce en `cnt`, el offset del uio crece en `cnt`, el puntero del kernel `cp` avanza en `cnt`, y el `n` del llamador se reduce en `cnt`.

Cuando una entrada iovec se agota por completo (`cnt == 0` al entrar al bucle), la función avanza a la siguiente entrada. Cuando el `n` del llamador llega a cero o el resid del uio llega a cero, el bucle termina.

Si `copyin` o `copyout` devuelve un valor distinto de cero, la función salta a `out` sin actualizar los campos de esa iteración, por lo que la contabilidad de la copia parcial es consistente: los bytes que sí se copiaron se reflejan en `uio_resid`, y los que no se copiaron siguen pendientes.

### Lo que debes retener

Del bucle se desprenden tres invariantes que importan para el código de tu driver.

- **Tu llamada a `uiomove(cp, n, uio)` mueve como máximo `MIN(n, uio->uio_resid)` bytes.** No hay forma de pedir más de lo que el uio tiene espacio; la función limita al lado que sea menor.
- **Ante una transferencia parcial, el estado es consistente.** `uio_resid` refleja exactamente los bytes que no se movieron. Puedes hacer otra llamada y continuará correctamente desde donde quedó.
- **El manejo de fallos está dentro del bucle, no alrededor de él.** Un fallo durante un `copyin` / `copyout` devuelve `EFAULT` para el resto; los campos siguen siendo consistentes.

Estos tres hechos explican por qué la estructura de tres líneas a la que seguimos volviendo (`uiomove`, comprobar error, actualizar estado) es suficiente. El kernel hace el trabajo complicado dentro del bucle; tu driver solo tiene que cooperar.



## Apéndice B: Por qué se permite `read(fd, buf, 0)`

Una nota breve sobre una pregunta frecuente: ¿por qué permite UNIX una llamada `read(fd, buf, 0)` o `write(fd, buf, 0)`?

Hay dos respuestas, y ambas merece la pena conocer.

**La respuesta práctica**: la I/O de longitud cero es una prueba gratuita. Un programa de usuario que quiera comprobar si un descriptor está en buen estado puede llamar a `read(fd, NULL, 0)` sin comprometerse a una transferencia real. Si el descriptor está roto, la llamada devuelve un error. Si está bien, la llamada devuelve cero y no tiene casi ningún coste.

**La respuesta semántica**: la interfaz de I/O de UNIX usa conteos de bytes de forma consistente, y tratar el cero como caso especial es más trabajo que permitirlo. Una llamada con `count == 0` es una operación sin efecto bien definida: el kernel no tiene que hacer nada y puede devolver cero de inmediato. La alternativa, devolver `EINVAL` para llamadas con conteo cero, obligaría a cada programa de usuario que calcule un conteo dinámicamente a protegerse contra ese caso. Ese es el tipo de cambio que rompe décadas de código sin ningún beneficio.

La consecuencia en el lado del driver, que ya señalamos antes: tu manejador no debe entrar en panic ni devolver error con un `uio_resid` de cero. El kernel gestiona efectivamente ese caso por ti cuando usas `uiomove`, que devuelve cero de inmediato si no hay nada que mover.

Si alguna vez te encuentras escribiendo `if (uio->uio_resid == 0) return (EINVAL);` en un driver, detente. Esa es la respuesta incorrecta. La I/O de conteo cero es válida; devuelve cero.



## Apéndice C: Un recorrido breve por la ruta de lectura de `/dev/zero`

Como análisis final, merece la pena recorrer exactamente qué ocurre cuando un programa de usuario llama a `read(2)` sobre `/dev/zero`. El driver es `/usr/src/sys/dev/null/null.c` y el manejador es `zero_read`. Una vez que entiendas esta ruta, entenderás todo lo que contiene el capítulo 9.

### Del espacio de usuario al despacho del kernel

El usuario llama a:

```c
ssize_t n = read(fd, buf, 1024);
```

La biblioteca de C realiza el syscall `read`. El kernel busca `fd` en la tabla de archivos del proceso llamador, recupera el `struct file`, identifica su vnode y despacha la llamada a devfs.

devfs identifica el cdev asociado al vnode, adquiere una referencia sobre él y llama a su puntero de función `d_read` (`zero_read`) con el uio que el kernel preparó.

### Inside `zero_read`

```c
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        void *zbuf;
        ssize_t len;
        int error = 0;

        KASSERT(uio->uio_rw == UIO_READ,
            ("Can't be in %s for write", __func__));
        zbuf = __DECONST(void *, zero_region);
        while (uio->uio_resid > 0 && error == 0) {
                len = uio->uio_resid;
                if (len > ZERO_REGION_SIZE)
                        len = ZERO_REGION_SIZE;
                error = uiomove(zbuf, len, uio);
        }
        return (error);
}
```

- Verificar que la dirección es correcta. Buena práctica; un `KASSERT` no tiene coste en kernels de producción.
- Asignar `zbuf` para que apunte a `zero_region`, una zona grande preasignada rellena de ceros.
- Bucle: mientras el llamador quiera más bytes, determinar el tamaño de la transferencia (el mínimo entre `uio_resid` y el tamaño de la zona de ceros), llamar a `uiomove` y acumular cualquier error.
- Retornar.

### Inside `uiomove`

En la primera iteración, `uiomove` ve `uio_resid = 1024`, `len = 1024` (ya que `ZERO_REGION_SIZE` es mucho mayor), `uio_segflg = UIO_USERSPACE`, `uio_rw = UIO_READ`. Selecciona `copyout(zbuf, buf, 1024)`. El kernel realiza la copia, gestionando cualquier fallo de página en el buffer del usuario. En caso de éxito, `uio_resid` cae a cero, `uio_offset` crece en 1024 y el iovec queda completamente consumido.

### De vuelta en la pila

`uiomove` devuelve cero. El bucle en `zero_read` ve `uio_resid == 0` y termina. `zero_read` devuelve cero.

devfs libera su referencia sobre el cdev. El kernel calcula el conteo de bytes como `1024 - 0 = 1024`. `read(2)` devuelve 1024 al usuario.

El buffer del usuario contiene ahora 1024 bytes de cero.

### Lo que esto te dice sobre tu propio driver

Dos observaciones.

En primer lugar, cada decisión de la ruta de datos en `zero_read` es también una decisión que tú tomas ahora. Qué tamaño de bloque mover por iteración; de qué buffer leer; cómo gestionar el error de `uiomove`. Las decisiones de tu driver diferirán en los detalles (tu buffer no es una zona de ceros preasignada, tu tamaño de bloque no es `ZERO_REGION_SIZE`), pero la forma es idéntica.

En segundo lugar, todo lo que está por encima de `zero_read` es maquinaria del kernel que no tienes que escribir. Tú implementas el manejador, y el kernel se encarga del syscall, la búsqueda del descriptor de archivo, el despacho VFS, el enrutamiento de devfs, el conteo de referencias y el manejo de fallos. Ese es el poder de la abstracción: tú aportas el conocimiento de tu driver, y todo lo demás viene gratis.

La otra cara es que, cuando escribes un driver, te comprometes a *cooperar* con esa maquinaria. Cada invariante del que dependen `uiomove` y devfs es ahora tu responsabilidad mantener. El capítulo te ha ido guiando a través de esos invariantes uno a uno, construyendo tres drivers pequeños que cada uno ejercita un subconjunto diferente.

A estas alturas, el patrón debería resultarte familiar.

## Apéndice D: Valores de retorno comunes de `read(2)` / `write(2)` en el lado del usuario

Una guía de referencia rápida sobre lo que ve un programa de usuario cuando se comunica con tu driver. Esto no es código del driver; es la vista desde el otro lado de la frontera de confianza. Leerla de vez en cuando es la mejor vacuna contra los errores sutiles que surgen cuando el driver hace algo distinto de lo que espera un programa UNIX bien comportado.

### `read(2)`

- Un entero positivo: esa cantidad de bytes se depositó en el buffer del llamante. Un número menor que el solicitado indica una lectura parcial (short read); el llamante itera.
- Cero: fin de archivo. No se producirán más bytes en este descriptor. El llamante se detiene.
- `-1` con `errno = EAGAIN`: modo no bloqueante, no hay datos disponibles en este momento. El llamante espera (a través de `select(2)` / `poll(2)` / `kqueue(2)`) y lo intenta de nuevo.
- `-1` con `errno = EINTR`: una señal interrumpió la lectura. El llamante normalmente reintenta, salvo que el manejador de la señal le indique lo contrario.
- `-1` con `errno = EFAULT`: el puntero al buffer era inválido. El llamante tiene un error.
- `-1` con `errno = ENXIO`: el dispositivo ha desaparecido. El llamante debe cerrar el descriptor y abandonar.
- `-1` con `errno = EIO`: el dispositivo reportó un error de hardware. El llamante puede reintentar o informar del error.

### `write(2)`

- Un entero positivo: esa cantidad de bytes fue aceptada. Un número menor que el ofrecido indica una escritura parcial (short write); el llamante itera con el resto.
- Cero: teóricamente posible, raramente se ve en la práctica. Normalmente se trata igual que una escritura parcial de cero bytes.
- `-1` con `errno = EAGAIN`: modo no bloqueante, no hay espacio en este momento. El llamante espera y reintenta.
- `-1` con `errno = ENOSPC`: no hay espacio de forma permanente. El llamante deja de escribir o reabre el descriptor.
- `-1` con `errno = EPIPE`: el lector cerró. Relevante para dispositivos similares a pipes, no para `myfirst`.
- `-1` con `errno = EFAULT`: el puntero al buffer era inválido.
- `-1` con `errno = EINTR`: interrumpido por una señal. Normalmente se reintenta.

### Qué significa esto para tu driver

Dos conclusiones.

La primera: `EAGAIN` es la manera en que los llamantes no bloqueantes esperan que un driver diga "no hay datos / no hay espacio ahora mismo, vuelve más tarde". Un llamante no bloqueante que recibe `EAGAIN` no lo trata como un error; espera una notificación de activación (normalmente a través de `poll(2)`) y reintenta. El Capítulo 10 hace que este mecanismo funcione para `myfirst`.

La segunda: `ENOSPC` es la manera en que un driver señala una condición permanente de espacio agotado en una escritura. Se diferencia de `EAGAIN` en que el llamante no espera que los reintentos tengan éxito pronto. En la Etapa 3 de `myfirst` usamos `ENOSPC` cuando el buffer se llena y no hay ningún lector drenándolo activamente; el Capítulo 10 añadirá `EAGAIN` encima de la misma condición para lectores y escritores no bloqueantes.

Un driver que devuelve el errno incorrecto aquí es prácticamente indistinguible de un driver que se comporta mal. El coste de hacerlo bien es mínimo. El coste de hacerlo mal se manifiesta en programas de usuario confusos meses después.



## Apéndice E: La hoja de referencia en una página

Si solo tienes cinco minutos antes de empezar el Capítulo 10, aquí está la versión en una página de todo lo anterior.

**Las firmas:**

```c
static int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
static int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
```

Devuelve cero en caso de éxito, un errno positivo en caso de fallo. Nunca devuelvas un conteo de bytes.

**La estructura de tres líneas para lecturas:**

```c
error = devfs_get_cdevpriv((void **)&fh);
if (error) return error;
return uiomove_frombuf(sc->buf, sc->buflen, uio);
```

O, para un buffer dinámico:

```c
mtx_lock(&sc->mtx);
toread = MIN((size_t)uio->uio_resid, sc->bufused);
error = uiomove(sc->buf + offset, toread, uio);
if (error == 0) { /* update state */ }
mtx_unlock(&sc->mtx);
return error;
```

**La estructura de tres líneas para escrituras:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - (sc->bufhead + sc->bufused);
if (avail == 0) { mtx_unlock(&sc->mtx); return ENOSPC; }
towrite = MIN((size_t)uio->uio_resid, avail);
error = uiomove(sc->buf + sc->bufhead + sc->bufused, towrite, uio);
if (error == 0) { sc->bufused += towrite; }
mtx_unlock(&sc->mtx);
return error;
```

**Lo que debes recordar sobre uio:**

- `uio_resid`: bytes pendientes. `uiomove` lo decrementa.
- `uio_offset`: posición, si tiene significado. `uiomove` lo incrementa.
- `uio_rw`: dirección. Confía en que `uiomove` lo usa correctamente.
- Todo lo demás: no lo toques.

**Lo que no debes hacer:**

- No desreferences punteros de usuario directamente.
- No uses `memcpy` / `bcopy` entre usuario y kernel.
- No devuelvas conteos de bytes.
- No reinicies el estado global del driver en `d_open` / `d_close`.
- No olvides `M_ZERO` en `malloc(9)`.
- No mantengas un spin lock durante `uiomove`.

**Valores de errno:**

- `0`: éxito.
- `ENXIO`: dispositivo no listo.
- `ENOSPC`: buffer lleno (permanente).
- `EAGAIN`: bloquearía (no bloqueante).
- `EFAULT`: procedente de `uiomove`, propágalo.
- `EIO`: error de hardware.

Eso es el capítulo.



## Resumen del capítulo

Este capítulo construyó el camino de datos. Partiendo de los stubs del Capítulo 8, implementamos `d_read` y `d_write` en tres etapas, cada una de ellas un driver completo y cargable.

- **La Etapa 1** ejercitó `uiomove_frombuf(9)` contra una cadena de texto estática en el kernel, con manejo de desplazamiento por descriptor que hacía que el progreso de dos lectores concurrentes fuera independiente.
- **La Etapa 2** introdujo un buffer dinámico en el kernel, un camino de escritura que añadía datos a él y un camino de lectura que los servía desde él. El buffer se dimensionaba en el momento del attach, y un buffer lleno rechazaba nuevas escrituras con `ENOSPC`.
- **La Etapa 3** convirtió el buffer en una cola FIFO (primero en entrar, primero en salir). Las lecturas drenaban desde la cabeza, las escrituras añadían datos al final, y el driver colapsaba `bufhead` a cero cuando el buffer quedaba vacío.

A lo largo del camino diseccionamos `struct uio` campo por campo, explicamos por qué `uiomove(9)` es la única manera legítima de cruzar la frontera de confianza usuario/kernel en un manejador de lectura o escritura, y construimos un pequeño vocabulario de valores errno que un driver bien comportado utiliza: `ENXIO`, `EFAULT`, `ENOSPC`, `EAGAIN`, `EIO`. Recorrimos el bucle interno de `uiomove` para que sus garantías parezcan ganadas y no misteriosas. Y terminamos con cinco laboratorios, seis desafíos, una guía de resolución de problemas y una hoja de referencia en una página.

El driver de la Etapa 3 es el punto de entrada al Capítulo 10. Mueve bytes correctamente. Aún no los mueve de manera eficiente: un buffer vacío devuelve cero bytes inmediatamente, un buffer lleno devuelve `ENOSPC` inmediatamente, no hay bloqueo, no hay integración con `poll(2)`, no hay buffer circular. El Capítulo 10 corrige todo eso, construyendo sobre los patrones que acabamos de trazar.

El patrón que acabas de aprender se repite. Cada manejador de I/O de dispositivo de caracteres en `/usr/src/sys/dev` se basa en la misma firma de tres argumentos, el mismo `struct uio` y el mismo primitivo `uiomove(9)`. Las diferencias entre drivers están en cómo preparan los datos, no en cómo los mueven. Ahora que reconoces la maquinaria de movimiento, cada manejador que abras se vuelve legible casi de inmediato.

Ya sabes lo suficiente para leer cualquier `d_read` o `d_write` en el árbol de FreeBSD y entender qué está haciendo. Es un hito significativo. Tómate un momento para apreciarlo antes de pasar la página.
