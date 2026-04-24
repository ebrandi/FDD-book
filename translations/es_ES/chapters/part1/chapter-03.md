---
title: "Una introducción suave a UNIX"
description: "Este capítulo ofrece una introducción práctica a los conceptos básicos de UNIX y FreeBSD."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 3
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 120
language: "es-ES"
---
# Una introducción suave a UNIX

Ahora que tu sistema FreeBSD está instalado y funcionando, es el momento de empezar a sentirte cómodo viviendo dentro de él. FreeBSD no es solo un sistema operativo; es parte de una larga tradición que comenzó con UNIX hace más de cincuenta años.

En este capítulo haremos nuestro primer recorrido real por el sistema. Aprenderás a navegar por el sistema de archivos, ejecutar comandos en el shell, gestionar procesos e instalar aplicaciones. A lo largo del camino verás cómo FreeBSD hereda la filosofía UNIX de simplicidad y coherencia, y por qué eso importa para nosotros como futuros desarrolladores de drivers.

Piensa en este capítulo como tu **guía de supervivencia** para trabajar dentro de FreeBSD. Antes de sumergirnos en código C e internos del kernel, necesitas sentirte cómodo moviéndote por el sistema, manipulando archivos y usando las herramientas en las que todo desarrollador confía a diario.

Al final de este capítulo no solo sabrás *qué es UNIX*; estarás usando FreeBSD con confianza, tanto como usuario como aspirante a programador de sistemas.

## Guía de lectura: cómo usar este capítulo

Este capítulo no está pensado solo para hojear; está diseñado para ser a la vez una **referencia** y un **campo de entrenamiento práctico**. El tiempo que te lleve depende del enfoque que elijas:

- **Solo lectura:** unas **2 horas** para recorrer el texto y los ejemplos a un ritmo cómodo para principiantes.
- **Lectura y laboratorios:** unas **4 horas** si te detienes para escribir y ejecutar cada uno de los laboratorios prácticos en tu propio sistema FreeBSD.
- **Lectura y desafíos:** **6 horas o más** si también completas el conjunto completo de 46 ejercicios de desafío al final.

Recomendación: no intentes hacerlo todo en una sola sesión. Divide el capítulo en secciones y, tras cada una, realiza el laboratorio correspondiente antes de continuar. Guarda los desafíos para cuando te sientas seguro y quieras poner a prueba tu dominio.

## Introducción: por qué importa UNIX

Antes de empezar a escribir drivers de dispositivo para FreeBSD, necesitamos hacer una pausa y hablar sobre los cimientos en los que se apoyan: **UNIX**.

Cada driver que escribas para FreeBSD, cada llamada al sistema que explores, cada mensaje del kernel que leas, todos tienen sentido únicamente cuando entiendes el sistema operativo en el que viven. Para un principiante, el mundo de UNIX puede resultar misterioso, lleno de comandos extraños y una filosofía muy diferente a la de Windows o macOS. Pero una vez que aprendes su lógica, comprobarás que no solo es accesible sino también elegante.

Este capítulo trata de darte una **introducción suave** a UNIX tal como aparece en FreeBSD. Al final, te sentirás cómodo navegando por el sistema, trabajando con archivos, ejecutando comandos, gestionando procesos, instalando aplicaciones e incluso escribiendo pequeños scripts para automatizar tus tareas. Estas son habilidades cotidianas para cualquier desarrollador de FreeBSD, y son absolutamente imprescindibles antes de que empecemos con el desarrollo del kernel.

### ¿Por qué deberías aprender UNIX antes de escribir drivers?

Piénsalo así: si escribir drivers es como construir un motor, UNIX es el coche entero que lo rodea. Necesitas saber dónde va el combustible, cómo funciona el cuadro de mandos y qué hacen los controles antes de poder cambiar piezas bajo el capó con seguridad.

Aquí tienes algunas razones por las que aprender los fundamentos de UNIX es imprescindible:

- **En UNIX todo está conectado.** Archivos, dispositivos, procesos: todos siguen reglas coherentes. Una vez que conoces esas reglas, el sistema se vuelve predecible.
- **FreeBSD es un descendiente directo de UNIX.** Los comandos, la estructura del sistema de archivos y la filosofía general no son añadidos; forman parte de su ADN.
- **Los drivers se integran con el userland.** Aunque tu código se ejecutará en el kernel, interactuará con programas de usuario, archivos y procesos. Entender el entorno del userland te ayuda a diseñar drivers que resulten naturales e intuitivos.
- **La depuración requiere habilidades de UNIX.** Cuando tu driver se comporte mal, dependerás de herramientas como `dmesg`, `sysctl` y comandos del shell para averiguar qué está pasando.

### Qué aprenderás en este capítulo

Al final de este capítulo serás capaz de:

- Entender qué es UNIX y cómo encaja FreeBSD en su familia.
- Usar el shell para ejecutar comandos y gestionar archivos.
- Navegar por el sistema de archivos de FreeBSD y saber dónde vive cada cosa.
- Gestionar usuarios, grupos y permisos de archivos.
- Monitorizar procesos y recursos del sistema.
- Instalar y eliminar aplicaciones usando el gestor de paquetes de FreeBSD.
- Automatizar tareas con scripts de shell.
- Explorar los internos de FreeBSD con herramientas como `dmesg` y `sysctl`.

A lo largo del camino te proporcionaré **laboratorios prácticos** para que puedas practicar. Leer sobre UNIX no es suficiente; necesitas **tocar el sistema**. Cada laboratorio incluirá comandos reales que ejecutarás en una instalación de FreeBSD, de modo que cuando llegues al final de este capítulo no solo entenderás UNIX, sino que lo estarás usando con confianza.

### El puente hacia los drivers de dispositivo

¿Por qué dedicamos un capítulo entero a los fundamentos de UNIX si este es un libro sobre escritura de drivers? Porque los drivers no existen de forma aislada. Cuando cargues tu propio módulo del kernel, lo verás aparecer bajo `/dev`. Cuando lo pruebes, usarás comandos del shell para leer y escribir en él. Cuando lo depures, recurrirás a los registros del sistema y a herramientas de monitorización.

Así que considera este capítulo como la construcción de la **alfabetización en el sistema operativo** que necesitas antes de convertirte en desarrollador de drivers. Una vez que la tengas, todo lo demás te resultará menos intimidante y mucho más lógico.

### En resumen

En esta sección de apertura analizamos por qué UNIX importa para cualquiera que quiera escribir drivers para FreeBSD. Los drivers no viven de forma aislada; existen dentro de un sistema operativo más amplio que sigue reglas, convenciones y una filosofía heredada de UNIX. Entender esta base es lo que hace que todo lo demás, desde el uso del shell hasta la depuración de drivers, resulte lógico en lugar de misterioso.

Con esa motivación presente, es el momento de hacerse la pregunta natural que surge a continuación: **¿qué es exactamente UNIX?** Para avanzar, examinaremos más de cerca su historia, sus principios rectores y los conceptos clave que siguen dando forma a FreeBSD hoy en día.

## ¿Qué es UNIX?

Antes de que puedas sentirte cómodo usando FreeBSD, te será de ayuda entender qué es UNIX y por qué importa. UNIX no es solo un trozo de software; es una familia de sistemas operativos, un conjunto de decisiones de diseño e incluso una filosofía que ha dado forma a la informática durante más de cincuenta años. FreeBSD es uno de sus descendientes modernos más importantes, así que aprender UNIX es como estudiar el árbol genealógico para ver dónde encaja FreeBSD.

### Una breve historia de UNIX

UNIX nació en **1969** en Bell Labs, cuando Ken Thompson y Dennis Ritchie crearon un sistema operativo ligero para una minicomputadora PDP-7. En una época en que los mainframes eran enormes, costosos y complejos, UNIX destacaba por ser **pequeño, elegante y diseñado para la experimentación**.

La **reescritura en C de 1973** fue el punto de inflexión. Por primera vez, un sistema operativo era portable: podías mover UNIX a hardware diferente recompilándolo, sin necesidad de reescribirlo todo desde cero. Esto era inaudito en los años setenta y cambió para siempre la trayectoria del diseño de sistemas.

**BSD en Berkeley** es la parte de la historia que lleva directamente a FreeBSD. Estudiantes de posgrado e investigadores de la Universidad de California en Berkeley tomaron el código fuente de UNIX de AT&T y lo ampliaron con características modernas:

- **Memoria virtual** (para que los programas no estuvieran limitados por la RAM física).
- **Redes** (la pila TCP/IP que aún hoy impulsa Internet).
- **El C shell** con scripting y control de trabajos.

En los **años noventa**, tras resolverse las disputas legales sobre el código fuente de UNIX, se lanzó el Proyecto FreeBSD. Su misión: llevar adelante la tradición BSD de forma libre y abierta, para que cualquiera pudiera usarla, modificarla y compartirla.

**Hoy**, FreeBSD es una continuación directa de ese linaje. No es una imitación de UNIX; es el legado de UNIX vivo y en plena forma.

Quizás estés pensando: *"¿Y a mí por qué me importa?"*. Deberías importarte porque cuando asomas la cabeza dentro de `/usr/src` o escribes comandos como `ls` y `ps`, no solo estás usando software; te estás beneficiando de décadas de resolución de problemas y de artesanía, el trabajo de miles de desarrolladores que construyeron y pulieron estas herramientas mucho antes de que tú aparecieras.

### La filosofía UNIX

UNIX no es solo un sistema; es una **mentalidad**. Entender su filosofía hará que todo lo demás, desde los comandos básicos hasta los drivers de dispositivo, resulte más natural.

1. **Haz una sola cosa y hazla bien.**
    En lugar de programas enormes que lo hacen todo, UNIX te ofrece herramientas enfocadas.

   Ejemplo: `grep` solo busca texto. No abre archivos, no los edita ni formatea resultados; eso se lo deja a otras herramientas.

2. **Todo es un archivo.**
    Los archivos no son solo documentos; son la forma en que interactúas con casi todo: dispositivos, procesos, sockets, registros de log.

   Analogía: Imagina el sistema completo como una biblioteca. Cada libro, cada mesa e incluso el cuaderno del bibliotecario forman parte del mismo sistema de archivos.

3. **Construye herramientas pequeñas y luego combínalas.**
    Aquí reside el genio del **operador de tubería (`|`)**. Tomas la salida de un programa y la usas como entrada de otro.

   Ejemplo:

   ```sh
   ps -aux | grep ssh
   ```

   Aquí, un programa lista todos los procesos y otro filtra únicamente los relacionados con SSH. Ninguno de los dos sabe nada del otro, pero el shell los une.

4. **Usa texto plano siempre que sea posible.**
    Los archivos de texto son fáciles de leer, editar, compartir y depurar. El archivo `/etc/rc.conf` de FreeBSD (la configuración del sistema) es simplemente un archivo de texto plano. Sin registros binarios, sin formatos propietarios.

Cuando empieces a escribir drivers de dispositivo, verás esta filosofía en todas partes: tu driver expondrá una **interfaz sencilla bajo `/dev`**, se comportará de manera predecible y se integrará sin problemas con otras herramientas.

### Los sistemas UNIX-like hoy en día

La palabra "UNIX" hoy hace referencia menos a un único sistema operativo y más a una **familia de sistemas UNIX-like**.

- **FreeBSD** - Tu foco en este libro. Se usa en servidores, equipos de red, cortafuegos y sistemas embebidos. Conocido por su fiabilidad y documentación. Muchos dispositivos comerciales (routers, sistemas de almacenamiento) ejecutan FreeBSD de forma silenciosa bajo el capó.
- **Linux** - Creado en 1991, inspirado en los principios de UNIX. Popular en centros de datos, dispositivos embebidos y supercomputadores. A diferencia de FreeBSD, Linux no es un descendiente directo de UNIX, pero comparte las mismas interfaces e ideas.
- **macOS e iOS** - Construidos sobre Darwin, una base derivada de BSD. macOS es un SO certificado como UNIX, lo que significa que sus herramientas de línea de comandos se comportan como las de FreeBSD. Si usas un Mac, ya tienes un sistema UNIX.
- **Otros** - Las variantes comerciales como AIX, Solaris o HP-UX siguen existiendo, pero son poco frecuentes fuera del ámbito empresarial.

Por qué esto importa: una vez que aprendes FreeBSD, te sentirás cómodo en casi cualquier otro sistema UNIX-like. Los comandos, la estructura del sistema de archivos y la filosofía se transfieren de un sistema a otro.

### Conceptos y términos clave

Aquí tienes algunos términos esenciales de UNIX que verás a lo largo de este libro:

- **Kernel** - El corazón del SO. Gestiona la memoria, la CPU, los dispositivos y los procesos. Tus drivers vivirán aquí.
- **Shell** - El programa que interpreta tus comandos. Es tu principal herramienta para comunicarte con el sistema.
- **Userland** - Todo lo que está fuera del kernel: comandos, bibliotecas, daemons. Es donde pasarás la mayor parte de tu tiempo como usuario.
- **Daemon** - Un servicio en segundo plano (como `sshd` para conexiones remotas o `cron` para tareas programadas).
- **Proceso** - Un programa en ejecución. Cada comando crea un proceso.
- **Descriptor de archivo** - Un identificador numérico que el kernel entrega a los programas para trabajar con archivos o dispositivos. Por ejemplo, 0 = entrada estándar, 1 = salida estándar, 2 = error estándar.

Consejo: no te preocupes por memorizar estos términos todavía. Piensa en ellos como personajes que volverás a encontrar más adelante en la historia. Para cuando escribas un driver, los conocerás como viejos amigos.

### Cómo difiere UNIX de Windows

Si has usado principalmente Windows, el enfoque de UNIX te resultará diferente al principio. Aquí tienes algunos contrastes:

- **Unidades de disco frente a árbol unificado**
   Windows utiliza letras de unidad (`C:\`, `D:\`). UNIX tiene un único árbol con raíz en `/`. Los discos y particiones se montan en ese árbol.
- **Registry frente a archivos de texto**
   Windows centraliza la configuración en el Registry. UNIX utiliza archivos de configuración en texto plano bajo `/etc` y `/usr/local/etc`. Puedes abrirlos con cualquier editor de texto.
- **Orientación a GUI frente a orientación a CLI**
   Mientras que Windows asume una interfaz gráfica, UNIX trata la línea de comandos como la herramienta principal. Existen entornos gráficos, pero el shell siempre está disponible.
- **Modelo de permisos**
   UNIX fue multiusuario desde el primer día. Cada archivo tiene permisos (lectura, escritura, ejecución) para el propietario, el grupo y el resto de usuarios. Esto hace que la seguridad y el uso compartido sean más sencillos y coherentes.

Estas diferencias explican por qué UNIX a menudo parece «más estricto», aunque también más transparente. Una vez que te acostumbras, esa coherencia se convierte en una enorme ventaja.

### UNIX en tu vida cotidiana

Aunque nunca hayas iniciado sesión en un sistema FreeBSD, UNIX ya está a tu alrededor:

- Tu router Wi-Fi o tu NAS puede ejecutar FreeBSD o Linux.
- Netflix usa servidores FreeBSD para distribuir vídeo en streaming.
- La PlayStation de Sony usa un sistema operativo basado en FreeBSD.
- macOS e iOS son descendientes directos de BSD UNIX.
- Los teléfonos Android ejecutan Linux, otro sistema similar a UNIX.

Aprender FreeBSD no es solo escribir drivers, es aprender el **lenguaje de la informática moderna**.

### Laboratorio práctico: tus primeros comandos UNIX

Vamos a concretarlo. Abre un terminal en el FreeBSD que instalaste en el capítulo anterior e introduce:

```sh
% uname -a
```

Esto muestra los detalles del sistema: el sistema operativo, el nombre del equipo, la versión de lanzamiento, la compilación del kernel y el tipo de máquina. En FreeBSD 14.x, podrías ver:

```text
FreeBSD freebsd.edsonbrandi.com 14.3-RELEASE FreeBSD 14.3-RELEASE releng/14.3-n271432-8c9ce319fef7 GENERIC amd64
```

Ahora prueba los comandos:

```sh
% date
% whoami
% hostname
```

- `date` - muestra la fecha y hora actuales.
- `whoami` - indica con qué cuenta de usuario has iniciado sesión.
- `hostname` - muestra el nombre de red de la máquina.

Por último, un pequeño experimento con la idea de *"todo es un archivo"* en UNIX:

```sh
% echo "Hello FreeBSD" > /tmp/testfile
% cat /tmp/testfile
```

Acabas de crear un archivo, escribir en él y volver a leerlo. Este es el mismo modelo que utilizarás más adelante para comunicarte con tus propios drivers.

### Cerrando

En esta sección aprendiste que UNIX no es solo un sistema operativo, sino una familia de ideas y principios de diseño que dieron forma a la informática moderna. Viste cómo FreeBSD encaja en esa historia como descendiente directo de BSD UNIX, por qué su filosofía de herramientas pequeñas y texto plano lo hace efectivo, y cómo muchos de los conceptos en los que te apoyarás como desarrollador de drivers, como los procesos, los daemons y los descriptores de archivo, forman parte de UNIX desde sus inicios.

Pero saber qué es UNIX solo nos lleva a la mitad del camino. Para usar FreeBSD de verdad, necesitas una forma de **interactuar con él**. Ahí es donde entra el shell, el intérprete de comandos que te permite hablar el idioma del sistema. En la siguiente sección empezaremos a usar el shell para ejecutar comandos, explorar el sistema de archivos y adquirir experiencia práctica con las herramientas de las que todo desarrollador de FreeBSD depende a diario.

## El shell: tu ventana a FreeBSD

Ahora que sabes qué es UNIX y por qué importa, es el momento de empezar a **hablar con el sistema**. La forma de hacerlo en FreeBSD (y en otros sistemas tipo UNIX) es a través del **shell**.

Piensa en el shell como un **intérprete** y un **traductor** a la vez: escribes un comando en forma legible para los humanos y el shell lo pasa al sistema operativo para que lo ejecute. Es la ventana entre tú y el mundo UNIX.

### ¿Qué es un shell?

En esencia, el shell es simplemente un programa, pero uno muy especial. Escucha lo que escribes, interpreta lo que quieres decir y pide al kernel que lo ejecute.

Algunos shells comunes son:

- **sh** - El shell Bourne original. Sencillo y fiable.
- **csh / tcsh** - El C shell y su versión mejorada, con funciones de scripting inspiradas en el lenguaje C. tcsh es el shell por defecto para nuevos usuarios en FreeBSD.
- **bash** - El Bourne Again Shell, muy popular en Linux.
- **zsh** - Un shell moderno y fácil de usar con múltiples comodidades.

En FreeBSD 14.x, si inicias sesión como usuario normal, probablemente usarás **tcsh**. Si inicias sesión como administrador root, puede que veas **sh**. No te preocupes si no estás seguro de qué shell tienes, veremos cómo comprobarlo en un momento.

Por qué esto importa para los desarrolladores de drivers: usarás el shell constantemente para compilar, cargar y probar tus drivers. Saber manejarlo es tan importante como saber arrancar un coche.

### Cómo saber qué shell estás usando

FreeBSD viene con más de un shell, y puede que notes pequeñas diferencias entre ellos: por ejemplo, el prompt puede tener un aspecto distinto, o algunos atajos pueden comportarse de manera diferente. No te preocupes: los **comandos UNIX fundamentales funcionan igual** independientemente del shell que uses. Aun así, es útil saber qué shell estás usando en cada momento, especialmente si más adelante decides escribir scripts o personalizar tu entorno.

Escribe:

```sh
% echo $SHELL
```

Verás algo como:

```sh
/bin/tcsh
```

o

```sh
/bin/sh
```

Esto te indica tu shell por defecto. No necesitas cambiarlo ahora; ten en cuenta que los shells pueden tener un aspecto ligeramente diferente pero comparten los mismos comandos básicos.

**Consejo práctico**
También hay una forma rápida de comprobar qué shell está ejecutando tu proceso actual:

```sh
% echo $0
```

Puede que muestre `-tcsh`, `sh` u otra cosa. Es ligeramente diferente de `$SHELL`, porque `$SHELL` te indica tu **shell por defecto** (el que obtienes al iniciar sesión), mientras que `$0` te indica el **shell que estás ejecutando realmente en este momento**. Si alguna vez inicias un shell diferente dentro de tu sesión (por ejemplo, escribiendo `sh` en el prompt), `$0` reflejará ese cambio.

### La estructura de un comando

Cada comando del shell sigue el mismo patrón sencillo:

```sh
command [options] [arguments]
```

- **comando** - El programa que quieres ejecutar.
- **opciones** - Indicadores que cambian su comportamiento (normalmente empiezan por `-`).
- **argumentos** - Los objetivos del comando, como nombres de archivo o directorios.

Ejemplo:

```sh
% ls -l /etc
```

- `ls` = listar el contenido del directorio.
- `-l` = opción de "formato largo".
- `/etc` = argumento (el directorio a listar).

Esta coherencia es una de las fortalezas de UNIX: una vez que aprendes el patrón, cada comando te resulta familiar.

### Comandos esenciales para principiantes

Veremos los comandos fundamentales que usarás constantemente.

#### Navegación por directorios

- **pwd** - Print Working Directory
   Muestra en qué parte del sistema de archivos te encuentras.

  ```sh
  % pwd
  ```

  Salida:

  ```
  /home/dev
  ```

- **cd** - Change Directory
   Te mueve a otro directorio.

  ```sh
  % cd /etc
  % pwd
  ```

  Salida:

  ```
  /etc
  ```

- **ls** - List
   Muestra el contenido de un directorio.

  ```sh
  % ls
  ```

  La salida puede incluir:

  ```
  rc.conf   ssh/   resolv.conf
  ```

**Consejo**: Prueba `ls -lh` para ver el tamaño de los archivos en formato legible por humanos.

#### Gestión de archivos y directorios

- **mkdir** - Make Directory

  ```sh
  % mkdir projects
  ```

- **rmdir** - Remove Directory (solo si está vacío)

  ```sh
  % rmdir projects
  ```

- **cp** - Copy

  ```sh
  % cp file1.txt file2.txt
  ```

- **mv** - Move (o renombrar)

  ```sh
  % mv file2.txt notes.txt
  ```

- **rm** - Remove (eliminar)

  ```sh
  % rm notes.txt
  ```

**Advertencia**: `rm` no pide confirmación. Una vez eliminado, el archivo desaparece si no tienes una copia de seguridad. Esta es una trampa habitual para los principiantes.

#### Visualización del contenido de archivos

- **cat** - Concatenate and display file contents

  ```sh
  % cat /etc/rc.conf
  ```

- **less** - View file contents with scrolling

  ```sh
  % less /etc/rc.conf
  ```

  Usa las teclas de flecha o la barra espaciadora; pulsa `q` para salir.

- **head / tail** - Muestran el inicio o el final de un archivo; el parámetro `-n` especifica el número de líneas que quieres ver

  ```sh
  % head -n 5 /etc/rc.conf
  % tail -n 5 /etc/rc.conf
  ```

#### Edición de archivos

Tarde o temprano tendrás que editar un archivo de configuración o un archivo de código fuente. FreeBSD incluye varios editores, cada uno con características distintas:

- **ee (Easy Editor)**

  - Instalado por defecto.
  - Diseñado para ser fácil de usar para principiantes, con menús visibles en la parte superior de la pantalla.
  - Para guardar, pulsa **Esc** y elige *"Leave editor"* → *"Save changes."*
  - Es una gran opción si nunca has usado un editor UNIX antes.

- **vi / vim**

  - El editor UNIX tradicional, siempre disponible.
  - Muy potente, pero con una curva de aprendizaje pronunciada.
  - Los principiantes suelen quedarse bloqueados porque `vi` arranca en *modo comando* en lugar de modo inserción.
  - Para empezar a escribir texto: pulsa **i**, escribe tu texto, luego pulsa **Esc** seguido de `:wq` para guardar y salir.
  - No necesitas dominarlo ahora, pero todo administrador de sistemas y desarrollador acaba aprendiendo al menos los fundamentos de `vi`.

- **nano**

  - No forma parte del sistema base de FreeBSD, pero se puede instalar fácilmente ejecutando el siguiente comando con sesión iniciada como root:

    ```sh
    # pkg install nano
    ```

  - Muy amigable para principiantes, con los atajos listados en la parte inferior de la pantalla.

  - Si vienes de distribuciones Linux como Ubuntu, es posible que ya lo conozcas.

**Consejo para principiantes**
Empieza con `ee` para acostumbrarte a editar archivos en FreeBSD. Cuando te sientas preparado, aprende los fundamentos de `vi`: siempre estará ahí, incluso en entornos de rescate o sistemas mínimos donde no hay nada más instalado.

##### **Laboratorio práctico: tus primeras ediciones**

1. Crea y edita un archivo nuevo con `ee`:

   ```sh
   % ee hello.txt
   ```

   Escribe una línea de texto corta, guarda y sal.

2. Prueba lo mismo con `vi`:

   ```sh
   % vi hello.txt
   ```

   Pulsa `i` para insertar, escribe algo nuevo, luego pulsa **Esc** y escribe `:wq` para guardar y salir.

3. Si instalaste `nano`:

   ```sh
   % nano hello.txt
   ```

   Observa cómo la línea inferior muestra comandos como `^O` para guardar y `^X` para salir.

##### **Error frecuente de principiantes: atascado en `vi`**

Casi todos los principiantes en UNIX han vivido esto: abres un archivo con `vi`, empiezas a pulsar teclas y nada ocurre como esperas. Peor aún, no consigues encontrar la forma de salir.

Esto es lo que está pasando:

- `vi` arranca en **modo comando**, no en modo escritura.
- Para insertar texto, pulsa **i** (insertar).
- Para volver al modo comando, pulsa **Esc**.
- Para guardar y salir: escribe `:wq` y pulsa Intro.
- Para salir sin guardar: escribe `:q!` y pulsa Intro.

**Consejo**: Si abres `vi` por accidente y solo quieres salir, pulsa **Esc**, escribe `:q!` y pulsa Intro. Eso cerrará el editor sin guardar.

### Consejos y atajos

Una vez que te acostumbres a escribir comandos, descubrirás rápidamente que el shell tiene muchas funciones integradas para ahorrar tiempo y reducir errores. Aprenderlas pronto hará que te sientas como en casa mucho antes.

**Nota sobre los shells de FreeBSD:**

- El **shell de inicio de sesión por defecto** para los nuevos usuarios suele ser **`/bin/tcsh`**, que soporta autocompletado con Tab, navegación por el historial con las teclas de flecha y muchos atajos interactivos.
- El shell más minimalista **`/bin/sh`** es excelente para escribir scripts y para uso del sistema, pero no ofrece comodidades como el autocompletado con Tab o el historial con teclas de flecha de forma predeterminada.
- Por tanto, si alguno de los atajos que se describen a continuación no parece funcionar, comprueba qué shell estás usando (`echo $SHELL`).

#### Autocompletado con Tab (tcsh)

 Empieza a escribir un comando o nombre de archivo y pulsa `Tab`. El shell intentará completarlo automáticamente.

```sh
% cd /et<Tab>
```

Se convierte en:

```sh
% cd /etc/
```

Si hay más de una coincidencia, pulsa `Tab` dos veces para ver una lista de posibilidades.
Esta función no está disponible en `/bin/sh`.

#### Historial de comandos (tcsh)

 Pulsa la **flecha arriba** para recuperar el último comando, y sigue pulsándola para retroceder en el tiempo. Pulsa la **flecha abajo** para avanzar de nuevo.

```sh
% sysctl kern.hostname
```

No tienes que volver a escribirlo: pulsa la flecha arriba y pulsa Intro.
En `/bin/sh` no tienes navegación con teclas de flecha, aunque puedes repetir el último comando con `!!`.

#### Comodines (globbing)

 Funciona en *todos* los shells, incluido `/bin/sh`.

```sh
% ls *.conf
```

Lista todos los archivos que empiezan por `host` y terminan en `.conf`.

```sh
% ls host?.conf
```

Coincide con archivos como `host1.conf`, `hostA.conf`, pero no con `hosts.conf`.

#### Edición en la línea de comandos (tcsh)

 En `tcsh` puedes mover el cursor a izquierda y derecha con las teclas de flecha, o usar atajos:

- **Ctrl+A** → Moverse al inicio de la línea.
- **Ctrl+E** → Moverse al final de la línea.
- **Ctrl+U** → Borrar todo desde el cursor hasta el inicio de la línea.

- **Repetir comandos rápidamente (todos los shells)**

  ```sh
  % !!
  ```

  Vuelve a ejecutar tu último comando.

  ```sh
  % !ls
  ```

  Repite el último comando que empezaba por `ls`.

**Consejo**: Si prefieres un shell interactivo más amigable, quédate con **`/bin/tcsh`** (el predeterminado de FreeBSD para usuarios). Si más adelante quieres una personalización avanzada, puedes instalar shells como `bash` o `zsh` desde paquetes o ports. Pero para escribir scripts, usa siempre **`/bin/sh`**, ya que está garantizado que estará presente y es el estándar del sistema.

### Laboratorio práctico: navegación y gestión de archivos

Practiquemos:

1. Ve a tu directorio de inicio:

   ```sh
   % cd ~
   ```

2. Crea un directorio nuevo:

   ```sh
   % mkdir unix_lab
   % cd unix_lab
   ```

3. Crea un archivo nuevo:

   ```sh
   % echo "Hello FreeBSD" > hello.txt
   ```

4. Visualiza el archivo:

   ```sh
   % cat hello.txt
   ```

5. Haz una copia:

   ```sh
   % cp hello.txt copy.txt
   % ls
   ```

6. Renómbralo:

   ```sh
   % mv copy.txt renamed.txt
   ```

7. Elimina el archivo renombrado:

   ```sh
   % rm renamed.txt
   ```

Al completar estos pasos, acabas de navegar por el sistema de archivos, crear archivos, copiarlos, renombrarlos y eliminarlos, el pan de cada día del trabajo con UNIX.

### En resumen

El shell es tu **puerta de entrada a FreeBSD**. Toda interacción con el sistema, ya sea ejecutar comandos, compilar código o probar un driver, pasa a través de él. En esta sección has aprendido qué es el shell, cómo se estructuran los comandos y cómo realizar una navegación básica y gestión de archivos.

A continuación, exploraremos **cómo FreeBSD organiza su sistema de archivos**. Entender la distribución de directorios como `/etc`, `/usr` y `/dev` te dará un mapa mental del sistema, algo especialmente importante cuando empecemos a trabajar con drivers de dispositivo que residen en `/dev`.

## El sistema de archivos de FreeBSD

En Windows puede que estés acostumbrado a unidades como `C:\` y `D:\`. En UNIX y FreeBSD no existen letras de unidad. En su lugar, todo reside en un **único árbol de directorios** que comienza en la raíz `/`.

Esto se denomina **sistema de archivos jerárquico**. En la cima se encuentra `/`, y todo lo demás se ramifica por debajo como carpetas dentro de carpetas. Los dispositivos, los archivos de configuración y los datos de usuario están todos organizados dentro de este árbol.

Aquí tienes un mapa simplificado:

```text
/
├── bin       → Essential user commands (ls, cp, mv)
├── sbin      → System administration commands (ifconfig, shutdown)
├── etc       → Configuration files
├── usr
│   ├── bin   → Non-essential user commands
│   ├── sbin  → Non-essential system admin tools
│   ├── local → Software installed by pkg or ports
│   └── src   → FreeBSD source code
├── var       → Logs, mail, spools, temp runtime data
├── home      → User home directories
├── dev       → Device files
└── boot      → Kernel and boot loader
```

Y aquí tienes una tabla con algunos de los directorios más importantes con los que trabajarás:

| Directorio   | Función                                                                   |
| ------------ | ------------------------------------------------------------------------- |
| `/`          | Raíz de todo el sistema. Todo comienza aquí.                              |
| `/bin`       | Herramientas esenciales de línea de comandos (usadas durante el arranque inicial). |
| `/sbin`      | Binarios del sistema (como `init`, `ifconfig`).                           |
| `/usr/bin`   | Herramientas de línea de comandos y programas de usuario.                 |
| `/usr/sbin`  | Herramientas de nivel de sistema usadas por administradores.              |
| `/usr/src`   | Código fuente de FreeBSD (kernel, bibliotecas, drivers).                  |
| `/usr/local` | Donde van los paquetes y el software instalado.                           |
| `/boot`      | Archivos del kernel y del cargador de arranque.                           |
| `/dev`       | Nodos de dispositivo, archivos que representan dispositivos.              |
| `/etc`       | Archivos de configuración del sistema.                                    |
| `/home`      | Directorios personales de usuario (como `/home/dev`).                     |
| `/var`       | Archivos de log, colas de correo, archivos en tiempo de ejecución.        |
| `/tmp`       | Archivos temporales, se borran al reiniciar.                              |

Entender esta distribución es fundamental para un desarrollador de drivers, ya que algunos directorios, especialmente `/dev`, `/boot` y `/usr/src`, están directamente vinculados al kernel y a los drivers. Pero incluso fuera de ellos, saber dónde reside cada cosa te ayuda a moverte con confianza.

**Sistema base frente a software local**: Una idea clave en FreeBSD es la separación entre el sistema base y el software instalado por el usuario. El sistema base: el kernel, las bibliotecas y las herramientas esenciales residen en `/bin`, `/sbin`, `/usr/bin` y `/usr/sbin`. Todo lo que instales después con pkg o ports va a `/usr/local`. Esta separación mantiene tu OS principal estable a la vez que te permite añadir y actualizar software libremente.

### Los dispositivos como archivos: `/dev`

Una de las ideas centrales de UNIX es que **los dispositivos aparecen como archivos** bajo `/dev`.

Ejemplos:

- `/dev/null`: Un «agujero negro» que descarta todo lo que escribas en él.
- `/dev/zero`: Produce un flujo interminable de bytes nulos.
- `/dev/random`: Proporciona datos aleatorios.
- `/dev/ada0`: Tu primer disco SATA.
- `/dev/da0`: Un dispositivo de almacenamiento USB.
- `/dev/tty`: Tu terminal.

Puedes interactuar con estos dispositivos usando las mismas herramientas que usas con los archivos:

```sh
% echo "test" > /dev/null
% head -c 10 /dev/zero | hexdump
```

Más adelante en este libro, cuando crees un driver, este expondrá un archivo aquí, por ejemplo, `/dev/hello`. Escribir en ese archivo ejecutará el código de tu driver dentro del kernel.

### Rutas absolutas frente a rutas relativas

Al navegar por el sistema de archivos, las rutas pueden ser:

- **Absolutas**: comienzan en la raíz `/`. Ejemplo: `/etc/rc.conf`
- **Relativas**: comienzan desde tu ubicación actual. Ejemplo: `../notes.txt`

Ejemplo:

```sh
% cd /etc      # absolute path
% cd ..        # relative path: move up one directory
```

**Recuerda**: `/` siempre significa la raíz del sistema, mientras que `.` significa «aquí» y `..` significa «un nivel arriba».

#### Ejemplo: navegar con rutas absolutas y relativas

Supón que tu directorio personal contiene esta estructura:

```text
/home/dev/unix_lab/
├── docs/
│   └── notes.txt
├── code/
│   └── test.c
└── tmp/
```

- Para abrir `notes.txt` con una **ruta absoluta**:

  ```sh
  % cat /home/dev/unix_lab/docs/notes.txt
  ```

- Para abrirlo con una **ruta relativa** desde dentro de `/home/dev/unix_lab`:

  ```sh
  % cd /home/dev/unix_lab
  % cat docs/notes.txt
  ```

- O bien, si ya estás dentro del directorio `docs`:

  ```sh
  % cd /home/dev/unix_lab/docs
  % cat ./notes.txt
  ```

Las rutas absolutas funcionan siempre, independientemente de dónde te encuentres, mientras que las rutas relativas dependen de tu directorio actual. Como desarrollador, preferirás las rutas absolutas en scripts (más predecibles) y las rutas relativas cuando trabajes de forma interactiva (más rápidas de escribir).

### Laboratorio práctico: explorar el sistema de archivos

Practiquemos explorando la distribución de FreeBSD:

1. Imprime tu ubicación actual:

```sh
   % pwd
```

2. Ve al directorio raíz y lista su contenido:

   ```sh
   % cd /
   % ls -lh
   ```

3. Echa un vistazo al directorio `/etc`:

   ```sh
   % ls /etc
   % head -n 5 /etc/rc.conf
   ```

4. Explora `/var/log` y visualiza los logs del sistema:

   ```sh
   % ls /var/log
   % tail -n 10 /var/log/messages
   ```

5. Comprueba los dispositivos en `/dev`:

   ```sh
   % ls /dev | head
   ```

Este laboratorio te proporciona un «mapa mental» del sistema de archivos de FreeBSD y muestra cómo los archivos de configuración, los logs y los dispositivos están todos organizados en lugares predecibles.

### En resumen

En esta sección has aprendido que FreeBSD utiliza un **sistema de archivos único y jerárquico** que comienza en `/`, con directorios clave dedicados a binarios del sistema, configuración, logs, datos de usuario y dispositivos. También has visto cómo `/dev` trata los dispositivos como archivos, un concepto fundamental en el que te apoyarás cuando escribas drivers.

Pero los archivos y directorios no son solo una cuestión de estructura, también se trata de **quién puede acceder a ellos**. UNIX es un sistema multiusuario, y cada archivo tiene un propietario, un grupo y bits de permiso que controlan lo que se puede hacer con él. En la siguiente sección exploraremos **los usuarios, los grupos y los permisos**, y aprenderás cómo FreeBSD mantiene el sistema seguro y flexible a la vez.

## Usuarios, grupos y permisos

Una de las mayores diferencias entre UNIX y sistemas como las primeras versiones de Windows es que UNIX fue diseñado desde el principio como un **sistema operativo multiusuario**. Eso significa que asume que varias personas (o servicios) pueden usar la misma máquina al mismo tiempo, y aplica reglas sobre quién puede hacer qué.

Este diseño es esencial para la seguridad, la estabilidad y la colaboración, y como desarrollador de drivers necesitarás entenderlo bien, ya que los permisos suelen controlar quién puede acceder al archivo de dispositivo de tu driver.

### Usuarios y grupos

Toda persona o servicio que utiliza FreeBSD lo hace bajo una **cuenta de usuario**.

- Un **usuario** tiene un nombre de usuario, un ID numérico (UID) y un directorio personal.
- Un **grupo** es una colección de usuarios, identificada por un nombre de grupo y un ID de grupo (GID).

Cada usuario pertenece al menos a un grupo, y los permisos pueden aplicarse tanto a individuos como a grupos.

Puedes ver tu identidad actual con:

   ```sh
% whoami
% id
   ```

Ejemplo de salida:

```text
dev
uid=1001(dev) gid=1001(dev) groups=1001(dev), 0(wheel)
```

Aquí:

- Tu nombre de usuario es `dev`.
- Tu UID es `1001`.
- Tu grupo principal es `dev`.
- También perteneces al grupo `wheel`, que permite acceder a privilegios de administrador (mediante `su` o `sudo`).

### Propiedad de archivos

En FreeBSD, cada archivo y directorio tiene un **propietario** (un usuario) y un **grupo**.

Comprobémoslo con `ls -l`:

```sh
% ls -l hello.txt
```

Salida:

```text
-rw-r--r--  1 dev  dev  12 Aug 23 10:15 hello.txt
```

Desglosándolo:

- `-rw-r--r--` = permisos (los veremos en un momento).
- `1` = número de enlaces (no es importante por ahora).
- `dev` = propietario (el usuario que creó el archivo).
- `dev` = grupo (el grupo asociado al archivo).
- `12` = tamaño del archivo en bytes.
- `Aug 23 10:15` = hora de la última modificación.
- `hello.txt` = nombre del archivo.

Por tanto, este archivo pertenece al usuario `dev` y al grupo `dev`.

### Permisos

Los permisos controlan lo que los usuarios pueden hacer con los archivos y directorios. Hay tres categorías de usuarios:

1. **Propietario**: el usuario que es dueño del archivo.
2. **Grupo**: los miembros del grupo del archivo.
3. **Otros**: todos los demás.

Y tres tipos de bits de permiso:

- **r** = lectura (puede ver el contenido).
- **w** = escritura (puede modificar o eliminar).
- **x** = ejecución (para programas o, en el caso de directorios, la capacidad de entrar en ellos).

Ejemplo:

```text
-rw-r--r--
```

Esto significa:

- **Propietario** = lectura + escritura.
- **Grupo** = solo lectura.
- **Otros** = solo lectura.

Por tanto, el propietario puede modificar el archivo, pero el resto solo puede verlo.

### Cambiar permisos

Para modificar los permisos se utiliza el comando **chmod**.

Hay dos formas:

**Modo simbólico**

```sh
% chmod u+x script.sh
```

Esto añade permiso de ejecución (`+x`) para el usuario (`u`).

**Modo octal**

```sh
% chmod 755 script.sh
```

Aquí, los números representan permisos:

- 7 = rwx
- 5 = r-x
- 0 = ---

Por tanto, `755` significa: propietario = rwx, grupo = r-x, otros = r-x.

### Cambiar la propiedad

A veces necesitas cambiar el propietario de un archivo. Usa `chown`:

   ```sh
% chown root:wheel hello.txt
   ```

Ahora el archivo pertenece a root y al grupo wheel.

**Nota**: Cambiar la propiedad generalmente requiere privilegios de administrador.

### Escenario práctico: directorio de proyecto

Supón que estás trabajando en un proyecto con un compañero y ambos necesitáis acceder a los mismos archivos.

Así es como lo configurarías; ejecuta estos comandos como root:

1. Crea un grupo llamado `proj`:

	```
   # pw groupadd proj
	```

2. Añade ambos usuarios al grupo:

   ```
   # pw groupmod proj -m dev,teammate
   ```

3. Crea un directorio y asígnalo al grupo:

   ```
   # mkdir /home/projdir
   # sudo chown dev:proj /home/projdir
   ```

4. Establece los permisos del grupo para que los miembros puedan escribir:

   ```
   # chmod 770 /home/projdir
   ```

Ahora ambos usuarios pueden trabajar en `/home/projdir`, mientras que los demás no tienen acceso.

Así es exactamente como los sistemas UNIX hacen cumplir la colaboración de forma segura.

### Laboratorio práctico: los permisos en acción

Practiquemos:

1. Crea un nuevo archivo:

   ```sh
   % echo "secret" > secret.txt
   ```

2. Comprueba sus permisos por defecto:

   ```sh
   % ls -l secret.txt
   ```

3. Elimina el acceso de lectura para los demás:

   ```sh
   % chmod o-r secret.txt
   % ls -l secret.txt
   ```

4. Añade permiso de ejecución para el usuario:

   ```sh
   % chmod u+x secret.txt
   % ls -l secret.txt
   ```

5. Intenta cambiar la propiedad (requerirá root):

   ```
   % sudo chown root secret.txt
   % ls -l secret.txt
   ```

Ten en cuenta que `sudo` te pedirá tu contraseña para ejecutar el comando `chown` del paso 5.

Con estos comandos has controlado el acceso a los archivos a un nivel muy detallado, un concepto que se aplica directamente cuando creamos drivers, ya que los drivers también tienen archivos de dispositivo bajo `/dev` con reglas de propiedad y permiso.

### En resumen

En esta sección has aprendido que FreeBSD es un **sistema multiusuario** en el que cada archivo tiene un propietario, un grupo y bits de permiso que controlan el acceso. Has visto cómo inspeccionar y cambiar permisos, cómo gestionar la propiedad y cómo configurar la colaboración de forma segura con grupos.

Estas reglas pueden parecer sencillas, pero son la columna vertebral del modelo de seguridad de FreeBSD. Más adelante, cuando escribas drivers, tus archivos de dispositivo bajo `/dev` también tendrán propiedad y permisos que controlarán quién puede abrirlos y usarlos.

A continuación, veremos los **procesos**, los programas en ejecución que dan vida al sistema. Aprenderás cómo ver qué se está ejecutando, cómo gestionar procesos y cómo FreeBSD mantiene todo organizado entre bastidores.

## Procesos y monitorización del sistema

Hasta ahora has aprendido a navegar por el sistema de archivos y gestionar archivos. Pero un sistema operativo no es solo una cuestión de archivos en disco, también se trata de **programas que se ejecutan en memoria**. Estos programas en ejecución se denominan **procesos**, y entenderlos es fundamental tanto para el uso diario como para el desarrollo de drivers.

### ¿Qué es un proceso?

Un proceso es un programa en movimiento. Cuando ejecutas un comando como `ls`, FreeBSD:

1. Carga el programa en memoria.
2. Le asigna un **identificador de proceso (PID)**.
3. Le proporciona recursos como tiempo de CPU y memoria.
4. Lo monitoriza hasta que termina o se detiene.

Los procesos son la forma en que FreeBSD gestiona todo lo que ocurre en tu sistema. Desde el shell en el que escribes hasta los demonios en segundo plano o tu navegador web, todos son procesos.

**Para los desarrolladores de drivers**: cuando escribes un driver, **los procesos del userland se comunicarán con él**. Saber cómo se crean y gestionan los procesos te ayuda a entender cómo se usan los drivers.

### Procesos en primer plano frente a procesos en segundo plano

Normalmente, cuando ejecutas un comando, este se ejecuta en **primer plano** (foreground), lo que significa que no puedes hacer nada más en esa terminal hasta que termine.

Ejemplo:

   ```sh
% sleep 10
   ```

Este comando pausa durante 10 segundos. Durante ese tiempo, tu terminal está «bloqueada».

Para ejecutar un proceso en **segundo plano** (background), añade un `&` al final:

```sh
% sleep 10 &
```

Ahora recuperas el prompt inmediatamente y el proceso se ejecuta en segundo plano.

Puedes ver los trabajos en segundo plano con:

```sh
% jobs
```

Y recuperar uno en primer plano:

```sh
% fg %1
```

(donde `%1` es el número de trabajo en la lista que ves con `jobs`).

### Visualización de procesos

Para ver qué procesos están en ejecución, utiliza `ps`:

```console
ps aux
```

Ejemplo de salida:

```text
USER   PID  %CPU %MEM  VSZ   RSS  TT  STAT STARTED    TIME COMMAND
root     1   0.0  0.0  1328   640  -  Is   10:00AM  0:00.01 /sbin/init
dev   1024   0.0  0.1  4220  2012  -  S    10:05AM  0:00.02 -tcsh
dev   1055   0.0  0.0  1500   800  -  R    10:06AM  0:00.00 ps aux
```

Aquí:

- `PID` = identificador del proceso.
- `USER` = quién lo inició.
- `%CPU` / `%MEM` = recursos en uso.
- `COMMAND` = el programa en ejecución.

#### Vigilar los procesos y la carga del sistema con `top`

Mientras que `ps` te ofrece una instantánea de los procesos en un momento concreto, a veces necesitas una **vista en tiempo real** de lo que ocurre en tu sistema. Para eso está el comando `top`.

```sh
% top
```

Esto abre una pantalla que se actualiza continuamente con la actividad del sistema. Por defecto, se refresca cada 2 segundos. Para salir, pulsa **q**.

La pantalla de `top` muestra:

- **Promedios de carga** (qué ocupado está tu sistema, promediado a lo largo de 1, 5 y 15 minutos).
- **Tiempo de actividad** (cuánto tiempo lleva el sistema en funcionamiento).
- **Uso de CPU** (usuario, sistema, inactivo).
- **Uso de memoria y swap**.
- **Una lista de procesos**, ordenada por uso de CPU, para que veas qué programas trabajan más.

**Ejemplo de salida de `top` (simplificado):**

```text
last pid:  3124;  load averages:  0.06,  0.12,  0.14                                            up 0+20:43:11  11:45:09
17 processes:  1 running, 16 sleeping
CPU:  0.0% user,  0.0% nice,  0.0% system,  0.0% interrupt,  100% idle
Mem: 5480K Active, 1303M Inact, 290M Wired, 83M Buf, 387M Free
Swap: 1638M Total, 1638M Free

  PID USERNAME    THR PRI NICE   SIZE    RES STATE    C   TIME    WCPU COMMAND
 3124 dev           1  20    0    15M  3440K CPU3     3   0:00   0.03% top
 2780 dev           1  20    0    23M    11M select   0   0:00   0.01% sshd-session
  639 root          1  20    0    14M  2732K select   2   0:02   0.00% syslogd
  435 root          1  20    0    15M  4012K select   2   0:04   0.00% devd
  730 root          1  20    0    14M  2612K nanslp   0   0:00   0.00% cron
  697 root          2  20    0    18M  4388K select   3   0:00   0.00% qemu-ga
 2778 root          1  20    0    23M    11M select   1   0:00   0.00% sshd-session
  726 root          1  20    0    23M  9164K select   3   0:00   0.00% sshd
  760 root          1  68    0    14M  2272K ttyin    1   0:00   0.00% getty
```

Aquí podemos ver:

- El sistema lleva más de un día en funcionamiento.
- Los promedios de carga son muy bajos (el sistema está inactivo).
- La CPU está mayoritariamente ociosa.
- La memoria está mayoritariamente libre.
- El comando `yes` (un programa de prueba que simplemente imprime "y" sin parar) consume casi toda la CPU.

##### Comprobación rápida con `uptime`

Si no necesitas todo el detalle de `top`, puedes usar:

```console
% uptime
```

Que muestra algo como:

```text
 3:45PM  up 2 days,  4:11,  2 users,  load averages:  0.32,  0.28,  0.25
```

Esto te indica:

- La hora actual.
- Cuánto tiempo lleva el sistema en funcionamiento.
- Cuántos usuarios están conectados.
- Los promedios de carga (1, 5, 15 minutos).

**Consejo**: Los promedios de carga son una forma rápida de saber si tu sistema está sobrecargado. En un sistema con un solo CPU, un promedio de carga de `1.00` significa que el CPU está completamente ocupado. En un sistema de 4 núcleos, `4.00` significa que todos los núcleos están al máximo.

**Laboratorio práctico: observar el sistema**

1. Ejecuta `uptime` y anota los promedios de carga de tu sistema.

2. Abre dos terminales en tu máquina FreeBSD.

3. En el primer terminal, inicia un proceso que consuma CPU:

   ```sh
   % yes > /dev/null &
   ```

4. En el segundo terminal, ejecuta `top` para ver cuánta CPU está usando el proceso `yes`.

5. Detén el comando `yes` con `kill %1` o `pkill yes`, o simplemente ejecutando `ctrl+c` en el primer terminal.

6. Ejecuta `uptime` de nuevo y observa cómo el promedio de carga es ligeramente mayor que antes, aunque irá bajando con el tiempo.

### Detener procesos

A veces un proceso se comporta mal o necesita detenerse. Puedes usar:

- **kill** - Enviar una señal a un proceso.

	```sh
	% kill 1055
	```

  (sustituye 1055 por el PID real).

- **kill -9** - Forzar la terminación inmediata de un proceso.

  ```sh
  % kill -9 1055
  ```

Usa `kill -9` solo cuando sea necesario, porque no le da al programa la oportunidad de hacer limpieza.

Cuando usas `kill`, no estás literalmente *"matando"* un proceso a la fuerza; le estás enviando una **señal**. Las señales son mensajes que el kernel entrega a los procesos.

- Por defecto, `kill` envía **SIGTERM (señal 15)**, que pide amablemente al proceso que termine. Los programas bien programados hacen limpieza y salen.
- Si un proceso se niega, puedes enviar **SIGKILL (señal 9)** con `kill -9 PID`. Esto obliga al proceso a detenerse inmediatamente, sin limpieza.
- Otra señal útil es **SIGHUP (señal 1)**, que se usa habitualmente para indicar a los demonios (servicios en segundo plano) que recarguen su configuración.

Prueba esto:

  ```sh
% sleep 100 &
% ps aux | grep sleep
% kill -15 <PID>   # try with SIGTERM first
% kill -9 <PID>    # if still running, use SIGKILL
  ```

Como futuro desarrollador de drivers, esta distinción importa. Tu código puede necesitar manejar la terminación de forma ordenada, liberando recursos en lugar de dejar el kernel en un estado inestable.

#### Jerarquía de procesos: padres e hijos

Cada proceso en FreeBSD (y en los sistemas UNIX en general) tiene un proceso **padre** que lo inició. Por ejemplo, cuando escribes un comando en el shell, el proceso del shell es el padre, y el comando que ejecutas se convierte en su hijo.

Puedes ver esta relación usando `ps` con columnas personalizadas:

```sh
% ps -o pid,ppid,command | head -10
```

Ejemplo de salida (simplificado):

```yaml
  PID  PPID COMMAND
    1     0 /sbin/init
  534     1 /usr/sbin/cron
  720   534 /bin/sh
  721   720 sleep 100
```

Aquí puedes ver:

- El proceso **1** es `init`, el antepasado de todos los procesos.
- `cron` fue iniciado por `init`.
- Un proceso de shell `sh` fue iniciado por `cron`.
- El proceso `sleep 100` fue iniciado por el shell.

Entender la jerarquía de procesos es útil para la depuración: si un padre muere, sus hijos pueden ser **adoptados por `init`**. Más adelante, cuando trabajes con drivers, verás cómo los demonios y servicios del sistema crean y gestionan procesos hijos que interactúan con tu código.

### Monitorización de los recursos del sistema

FreeBSD proporciona comandos sencillos para comprobar el estado del sistema:

- **df -h** - Muestra el uso del disco.

	```sh
	% df -h
	```

  Ejemplo:

  ```yaml
  Filesystem  Size  Used  Avail Capacity  Mounted on
  /dev/ada0p2  50G   20G    28G    42%    /
  ```

- **du -sh** - Muestra el tamaño de un directorio.

  ```
  % du -sh /var/log
  ```

- **freebsd-version** - Muestra la versión del sistema operativo.

  ```
  % freebsd-version
  ```

- **sysctl** - Consulta información del sistema.

  ```sh
  % sysctl hw.model
  % sysctl hw.ncpu
  ```

La salida puede mostrar el modelo de tu CPU y el número de núcleos.

Más adelante, al escribir drivers, usarás frecuentemente `dmesg` y `sysctl` para monitorizar cómo interactúa tu driver con el sistema.

### Laboratorio práctico: trabajar con procesos

Vamos a practicar:

1. Ejecuta un comando sleep en segundo plano:

      ```sh
      % sleep 30 &
      ```

2. Comprueba los trabajos en ejecución:

   ```sh
   % jobs
   ```

3. Lista los procesos:

   ```sh
   % ps aux | grep sleep
   ```

4. Detén el proceso:

   ```sh
   % kill <PID>
   ```

5. Ejecuta `top` y observa la actividad del sistema. Pulsa `q` para salir.

6. Comprueba la información del sistema:

   ```sh
   % sysctl hw.model
   % sysctl hw.ncpu
   ```

### En resumen

En esta sección aprendiste que los procesos son los programas vivos y en ejecución dentro de FreeBSD. Viste cómo iniciarlos, moverlos entre primer y segundo plano, inspeccionarlos con `ps` y `top`, y detenerlos con `kill`. También exploraste comandos básicos de monitorización del sistema para comprobar el uso de disco, CPU y memoria.

Los procesos son esenciales porque son los que dan vida al sistema, y como desarrollador de drivers, los programas que usen tu driver siempre se ejecutarán como procesos.

Pero monitorizar procesos es solo una parte de la historia. Para hacer trabajo real necesitarás más herramientas de las que incluye el sistema base. FreeBSD ofrece una forma limpia y flexible de instalar y gestionar software adicional, desde utilidades sencillas como `nano` hasta aplicaciones grandes como servidores web. En la siguiente sección veremos el **sistema de paquetes de FreeBSD y la Ports Collection**, para que puedas ampliar tu sistema con el software que necesitas.

## Instalación y gestión de software

FreeBSD está diseñado como un sistema operativo ligero y fiable. De serie, dispones de un **sistema base** sólido como una roca: el kernel, las bibliotecas del sistema, las herramientas esenciales y los archivos de configuración. Todo lo que va más allá, editores, compiladores, servidores, herramientas de monitorización e incluso entornos de escritorio, se considera **software de terceros**, y FreeBSD ofrece dos excelentes formas de instalarlo:

1. **pkg** - El gestor de paquetes binarios: rápido, sencillo y cómodo.
2. **La Ports Collection** - Un enorme sistema de construcción basado en código fuente que permite una personalización muy precisa.

Juntos, ofrecen a FreeBSD uno de los ecosistemas de software más flexibles del mundo UNIX.

### Paquetes binarios con pkg

La herramienta `pkg` es el gestor de paquetes moderno de FreeBSD. Te da acceso a **decenas de miles de aplicaciones precompiladas** mantenidas por el equipo de ports de FreeBSD.

Cuando instalas un paquete con `pkg`, esto es lo que ocurre:

- La herramienta descarga un **paquete binario** desde los espejos de FreeBSD.
- Las dependencias se descargan automáticamente.
- Los archivos se instalan bajo `/usr/local`.
- La base de datos de paquetes registra lo que se instaló, para que puedas actualizarlo o eliminarlo más adelante.

#### Comandos habituales

- Actualizar el repositorio de paquetes:

   ```sh
  % sudo pkg update
  ```

- Buscar software:

  ```sh
  % sudo pkg search htop
  ```

- Instalar software:

  ```sh
  % sudo pkg install htop
  ```

- Actualizar todos los paquetes:

  ```sh
  % sudo pkg upgrade
  ```

- Eliminar software:

  ```sh
  % sudo pkg delete htop
  ```

Para los principiantes, `pkg` es la forma más rápida y segura de instalar software.

### La Ports Collection de FreeBSD

La **Ports Collection** es una de las joyas de la corona de FreeBSD. Es un **enorme árbol de recetas de construcción** (llamadas "ports") ubicado bajo `/usr/ports`. Cada port contiene:

- Un **Makefile** que describe cómo descargar, parchear, configurar y construir el software.
- Sumas de comprobación para verificar la integridad.
- Metadatos sobre dependencias y licencias.

Cuando construyes software desde ports, FreeBSD descarga el código fuente del sitio original del proyecto, aplica parches específicos para FreeBSD y lo compila localmente en tu sistema.

#### ¿Por qué usar Ports?

¿Por qué tomarse la molestia de compilar desde código fuente cuando hay paquetes precompilados disponibles?

- **Personalización** - Muchas aplicaciones tienen características opcionales. Con ports, puedes elegir exactamente qué activar o desactivar durante la compilación.
- **Optimización** - Los usuarios avanzados pueden querer compilar con flags ajustados para su hardware.
- **Opciones de última generación** - A veces hay nuevas funcionalidades disponibles en ports antes de que lleguen a los paquetes binarios.
- **Compatibilidad con pkg** - Ports y paquetes comparten la misma infraestructura subyacente. De hecho, los paquetes se construyen a partir de ports mediante el clúster de construcción de FreeBSD.

#### Obtener y explorar el árbol de ports

La Ports Collection vive bajo `/usr/ports`, pero en un sistema FreeBSD recién instalado es posible que este directorio no exista todavía. Comprobémoslo:

```sh
% ls /usr/ports
```

Si ves categorías como `archivers`, `editors`, `net`, `security`, `sysutils` y `www`, entonces Ports está instalado. Si el directorio no existe, tendrás que obtener el árbol de ports tú mismo.

#### Instalar la Ports Collection con Git

La forma oficial y recomendada es usar **Git**:

1. Asegúrate de que `git` está instalado:

   ```sh
   % sudo pkg install git
   ```

2. Clona el repositorio oficial de ports:

   ```sh
   % sudo git clone https://git.FreeBSD.org/ports.git /usr/ports
   ```

   Esto creará `/usr/ports` y lo llenará con toda la Ports Collection. La clonación inicial puede tardar un poco, ya que contiene miles de aplicaciones.

3. Para actualizar el árbol de ports más adelante, simplemente ejecuta:

   ```sh
   % cd /usr/ports
   % sudo git pull
   ```

Existe también una herramienta más antigua llamada `portsnap`, pero **Git es el método moderno y recomendado** porque mantiene tu árbol directamente sincronizado con el repositorio del proyecto FreeBSD.

#### Explorar los ports

Una vez instalado Ports, explóralo:

```sh
% cd /usr/ports
% ls
```

Verás archivos y categorías como:

```text
CHANGES         UIDs            comms           ftp             mail            portuguese      x11
CONTRIBUTING.md UPDATING        converters      games           math            print           x11-clocks
COPYRIGHT       accessibility   databases       german          misc            russian         x11-drivers
GIDs            arabic          deskutils       graphics        multimedia      science         x11-fm
Keywords        archivers       devel           hebrew          net             security        x11-fonts
MOVED           astro           dns             hungarian       net-im          shells          x11-servers
Makefile        audio           editors         irc             net-mgmt        sysutils        x11-themes
Mk              benchmarks      emulators       japanese        net-p2p         textproc        x11-toolkits
README          biology         filesystems     java            news            ukrainian       x11-wm
Templates       cad             finance         korean          polish          vietnamese
Tools           chinese         french          lang            ports-mgmt      www
```

Cada categoría tiene subdirectorios para aplicaciones concretas. Por ejemplo:

```sh
% cd /usr/ports/sysutils/memdump
% ls
```

Aquí encontrarás archivos como `Makefile`, `distinfo`, `pkg-descr` y posiblemente un directorio `files/`. Estos son los "ingredientes" que FreeBSD usa para construir la aplicación: el `Makefile` define el proceso, `distinfo` garantiza la integridad, `pkg-descr` describe qué hace el software y `files/` contiene los parches específicos para FreeBSD.

#### Construir desde Ports

Ejemplo: instalar `memdump` desde ports.

```sh
% cd /usr/ports/sysutils/memdump
% sudo make install clean
```

Durante la construcción puede aparecer un menú de opciones, como activar sensores o colores, instalar documentación, etc. Aquí es donde los ports brillan: tú controlas qué características se compilan.

El proceso `make install clean` hace tres cosas:

- **install** - construye e instala el programa.
- **clean** - elimina los archivos temporales de construcción.

#### Mezclar Ports y paquetes

Una pregunta habitual: *¿Puedo mezclar paquetes y ports?*

Sí, son compatibles, ya que ambos se construyen a partir del mismo árbol de código fuente. Sin embargo, si reconstruyes algo desde ports con opciones personalizadas, debes tener cuidado de no sobreescribirlo accidentalmente con una actualización de paquetes binarios más adelante.

Muchos usuarios instalan la mayoría de las cosas con `pkg`, pero recurren a ports para aplicaciones concretas donde la personalización es importante.

### Dónde se instala el software

Tanto `pkg` como ports instalan el software de terceros bajo `/usr/local`. Esto los mantiene separados del sistema base.

Ubicaciones típicas:

- **Binarios** → `/usr/local/bin`
- **Bibliotecas** → `/usr/local/lib`
- **Configuración** → `/usr/local/etc`
- **Páginas de manual** → `/usr/local/man`

Prueba:

```sh
% which nano
```

Salida:

```text
/usr/local/bin/nano
```

Esto confirma que nano proviene de los paquetes/ports, no del sistema base.

### Ejemplo práctico: instalar vim y htop

Probemos ambos métodos.

#### Usando pkg

```sh
% sudo pkg install vim htop
```

Ejecútalos:

```sh
% vim test.txt
% htop
```

#### Uso de Ports

```sh
% cd /usr/ports/sysutils/htop
% sudo make install clean
```

Ejecútalo:

```sh
% htop
```

Observa cómo la versión de Ports puede preguntarte sobre características opcionales durante el build, mientras que pkg instala con los valores predeterminados.

### Laboratorio práctico: gestión de software

1. Actualiza tu repositorio de paquetes:

	```sh
	% sudo pkg update
	```

2. Instala lynx con pkg:

   ```sh
   % sudo pkg install lynx
   % lynx https://www.freebsd.org
   ```

3. Busca bsdinfo:

   ```sh
   % pkg search bsdinfo
   ```

4. Instala bsdinfo desde ports:

   ```sh
   % cd /usr/ports/sysutils/bsdinfo
   % sudo make install clean
   ```

5. Ejecuta bsdinfo para confirmar que ya está instalado:

   ```sh
   % bsdinfo
   ```

6. Elimina nano:

   ```sh
   % sudo pkg delete nano
   ```

Ya has instalado, ejecutado y eliminado software tanto con pkg como con ports, dos métodos complementarios que le dan a FreeBSD su flexibilidad.

### En resumen

En esta sección, has aprendido cómo gestiona FreeBSD el software de terceros:

- El **sistema pkg** te ofrece instalaciones binarias rápidas y sencillas.
- La **Ports Collection** ofrece flexibilidad y personalización basadas en el código fuente.
- Ambos métodos instalan bajo `/usr/local`, manteniendo el sistema base separado y limpio.

Entender este ecosistema es clave para la cultura de FreeBSD. Muchos administradores instalan las herramientas más habituales con `pkg` y recurren a ports cuando necesitan un control más preciso. Como desarrollador, sabrás apreciar ambos enfoques: pkg por su comodidad, y ports cuando quieras ver exactamente cómo se construye e integra el software.

Pero las aplicaciones son solo una parte de la historia. El **sistema base** de FreeBSD, el kernel y las utilidades principales también necesitan actualizaciones periódicas para mantenerse seguros y fiables. En la siguiente sección, aprenderemos a usar `freebsd-update` para mantener el sistema operativo al día, de modo que siempre tengas una base sólida sobre la que trabajar.

## Mantener FreeBSD al día

Uno de los hábitos más importantes que puedes adquirir como usuario de FreeBSD es mantener tu sistema actualizado. Las actualizaciones corrigen problemas de seguridad, eliminan errores y, a veces, añaden soporte para hardware nuevo. A diferencia del comando `pkg update && pkg upgrade`, que actualiza tus aplicaciones, **el comando `freebsd-update` se usa para actualizar el propio sistema operativo base**, incluidos el kernel y las utilidades principales.

Mantener el sistema al día garantiza que ejecutas FreeBSD de forma segura y te proporciona la misma base sólida sobre la que trabajan otros desarrolladores.

### Por qué importan las actualizaciones

- **Seguridad:** como cualquier software, FreeBSD presenta a veces vulnerabilidades de seguridad. Las actualizaciones las corrigen con rapidez.
- **Estabilidad:** las correcciones de errores mejoran la fiabilidad, algo fundamental si estás desarrollando drivers.
- **Compatibilidad:** las actualizaciones traen soporte para nuevas CPU, chipsets y otro hardware.

No consideres las actualizaciones como algo opcional. Forman parte de una administración de sistemas responsable.

### La herramienta `freebsd-update`

FreeBSD simplifica las actualizaciones con la herramienta `freebsd-update`. Su funcionamiento es el siguiente:

1. **Descarga** información sobre las actualizaciones disponibles.
2. **Aplica** parches binarios a tu sistema.
3. Si es necesario, **reinicia** con el kernel actualizado.

Esto es mucho más sencillo que reconstruir el sistema desde el código fuente (algo que aprenderemos más adelante, cuando necesitemos ese nivel de control).

### El proceso de actualización

Este es el proceso estándar:

1. **Obtener las actualizaciones disponibles**

   ```sh
   % sudo freebsd-update fetch
   ```

   Esto contacta con los servidores de actualización de FreeBSD y descarga los parches de seguridad o correcciones de errores disponibles para tu versión.

2. **Revisar los cambios**
    Tras la descarga, `freebsd-update` puede mostrarte una lista de archivos de configuración que se van a modificar.
    Ejemplo:

   ```yaml
   The following files will be updated as part of updating to 14.1-RELEASE-p3:
   /bin/ls
   /sbin/init
   /etc/rc.conf
   ```

   ¡No te alarmes! Esto no significa que tu sistema esté roto, simplemente indica que algunos archivos van a ser actualizados.

   - Si archivos de configuración del sistema como `/etc/rc.conf` han cambiado en el sistema base, se te pedirá que revises las diferencias.
   - `freebsd-update` utiliza una herramienta de fusión para mostrar los cambios en paralelo.
   - Para principiantes: si no estás seguro, lo más seguro suele ser **aceptar el valor predeterminado (conservar tu versión local)**. Siempre puedes consultar los registros de `/var/db/freebsd-update` más adelante.

**Consejo:** Si en este punto no te sientes cómodo fusionando archivos de configuración, puedes omitir los cambios y revisarlos manualmente más adelante.

3. **Instalar las actualizaciones**

   ```sh
   % sudo freebsd-update install
   ```

   Este paso aplica las actualizaciones que se han descargado.

   - Si la actualización incluye solo programas del userland (como `ls`, `cp`, librerías), ya habrás terminado.
   - Si la actualización incluye un **parche del kernel**, se te pedirá que **reinicies** tras la instalación.

### Sesión de ejemplo

Así podría verse una actualización normal:

```sh
% sudo freebsd-update fetch
Looking up update.FreeBSD.org mirrors... 3 mirrors found.
Fetching metadata signature for 14.3-RELEASE from update1.FreeBSD.org... done.
Fetching metadata index... done.
Fetching 1 patches..... done.
Applying patches... done.
The following files will be updated as part of updating to 14.3-RELEASE-p1:
    /bin/ls
    /bin/ps
    /sbin/init
% sudo freebsd-update install
Installing updates... done.
```

Si el kernel fue actualizado:

```sh
% sudo reboot
```

Tras el reinicio, tu sistema estará completamente actualizado.

### Actualizaciones del kernel con `freebsd-update`

Una de las cosas más útiles de `freebsd-update` es que puede actualizar el propio kernel. No tienes que reconstruirlo manualmente a menos que quieras ejecutar un kernel personalizado (lo veremos más adelante en el libro).

Esto significa que, para la mayoría de los usuarios, mantenerse seguros y actualizados es simplemente cuestión de ejecutar `fetch` + `install` con regularidad.

### Actualizar a una nueva versión con `freebsd-update`

Además de aplicar parches de seguridad y correcciones de errores, `freebsd-update` también puede actualizar tu sistema a una **nueva versión de FreeBSD**. Por ejemplo, si estás ejecutando **FreeBSD 14.2** y quieres actualizar a **14.3**, el proceso es sencillo.

El proceso consta de tres pasos:

1. **Descargar los archivos de actualización**

   ```sh
   % sudo freebsd-update upgrade -r 14.3-RELEASE
   ```

   Sustituye `14.3-RELEASE` por la versión a la que quieras actualizar.

2. **Instalar los nuevos componentes**

   ```sh
   % sudo freebsd-update install
   ```

   Esto instala la primera etapa de las actualizaciones. Si el kernel fue actualizado, necesitarás reiniciar:

   ```sh
   % sudo reboot
   ```

3. **Repetir la instalación**
    Tras el reinicio, vuelve a ejecutar el paso de instalación para terminar de actualizar el resto del sistema:

   ```sh
   % sudo freebsd-update install
   ```

Al finalizar, estarás ejecutando la nueva versión. Puedes confirmarlo con:

```sh
% freebsd-version
```

**Consejo**: las actualizaciones de versión a veces implican fusionar archivos de configuración (igual que las actualizaciones de seguridad). En caso de duda, conserva tus versiones locales; siempre puedes compararlas con los nuevos valores predeterminados almacenados en `/var/db/freebsd-update/`.

Recuerda también que conviene actualizar tus **paquetes** después de una actualización de versión, ya que están compilados contra las nuevas bibliotecas del sistema:

```sh
% sudo pkg update
% sudo pkg upgrade
```

### Laboratorio práctico: ejecutar tu primera actualización

1. Comprueba tu versión actual de FreeBSD:

   ```sh
   % freebsd-version -kru
   ```

   - `-k` → kernel
   - `-r` → en ejecución
   - `-u` → userland

2. Ejecuta `freebsd-update fetch` para ver si hay actualizaciones disponibles.

3. Lee con atención cualquier mensaje sobre fusión de archivos de configuración. Si no estás seguro, elige **conservar tu versión**.

4. Ejecuta `freebsd-update install` para aplicar las actualizaciones.

5. Si el kernel fue actualizado, reinicia:

   ```sh
   % sudo reboot
   ```

**Error frecuente de principiantes: el miedo a fusionar archivos de configuración**

Cuando `freebsd-update` te pide que fusiones cambios, puede resultar intimidante, con mucho texto, símbolos de suma y resta, y solicitudes de confirmación. No te preocupes.

- En caso de duda, conserva tu versión local de archivos como `/etc/rc.conf` o `/etc/hosts`.
- El sistema seguirá funcionando.
- Siempre puedes inspeccionar los nuevos archivos predeterminados más adelante (están almacenados en `/var/db/freebsd-update/`).

Con el tiempo, te sentirás cómodo resolviendo estas fusiones, pero al principio **elegir conservar tu configuración es la opción más segura**.

### En resumen

Con solo dos comandos, `freebsd-update fetch` y `freebsd-update install`, ya sabes cómo mantener el sistema base de FreeBSD parcheado y seguro. Este proceso lleva solo unos minutos, pero garantiza que tu entorno sea seguro y fiable para el trabajo de desarrollo.

Más adelante, cuando empecemos a trabajar en el kernel y a escribir drivers, también aprenderemos a construir e instalar un kernel personalizado desde el código fuente. Pero por ahora, ya tienes los conocimientos esenciales para mantener tu sistema como un profesional.

Y ya que comprobar las actualizaciones es algo que querrás hacer con regularidad, ¿no sería estupendo que el sistema pudiera encargarse de algunas de estas tareas por ti de forma automática? Eso es exactamente lo que veremos a continuación: **la planificación y la automatización** con herramientas como `cron`, `at` y `periodic`.

## Planificación y automatización

Uno de los grandes puntos fuertes de UNIX es que fue diseñado para que el ordenador se encargue de las tareas repetitivas por ti. En lugar de esperar hasta medianoche para ejecutar una copia de seguridad o conectarte cada mañana para arrancar un script de monitorización, puedes decirle a FreeBSD:

> *«Ejecuta este comando a esta hora, todos los días, indefinidamente.»*

Esto no solo ahorra tiempo, sino que también hace tu sistema más fiable. En FreeBSD, las principales herramientas para esto son:

1. **cron**: para tareas recurrentes, como copias de seguridad o monitorización.
2. **at**: para tareas puntuales que quieras programar para más tarde.
3. **periodic**: el sistema integrado de FreeBSD para tareas de mantenimiento rutinario.

### Por qué automatizar tareas

La automatización importa porque mejora:

- **Consistencia**: una tarea programada con cron siempre se ejecutará, aunque se te olvide.
- **Eficiencia**: en lugar de repetir comandos manualmente, los escribes una vez.
- **Fiabilidad**: la automatización ayuda a evitar errores. Los ordenadores no se olvidan de rotar los registros la noche del domingo.
- **Mantenimiento del sistema**: el propio FreeBSD depende en gran medida de cron y periodic para mantener el sistema en buen estado (rotar registros, actualizar bases de datos, ejecutar comprobaciones de seguridad).

### cron: el motor de la automatización

El demonio `cron` se ejecuta continuamente en segundo plano. Cada minuto, comprueba una lista de tareas programadas (almacenadas en crontabs) y ejecuta las que coincidan con la hora actual.

Cada usuario tiene su propio **crontab**, y el sistema tiene uno global. Esto significa que puedes programar tareas personales (como limpiar archivos en tu directorio de inicio) sin tocar las tareas del sistema.

### El formato del crontab

El formato del crontab tiene **cinco campos** que describen *cuándo* ejecutar una tarea, seguidos del propio comando:

   ```yaml
minute   hour   day   month   weekday   command
   ```

- **minuto**: 0-59
- **hora**: 0-23 (formato de 24 horas)
- **día**: 1-31
- **mes**: 1-12
- **día de la semana**: 0-6 (0 = domingo, 6 = sábado)

Una regla mnemotécnica para recordarlo: *«Mi Hermano Dice Mentiras Siempre.»* (Minuto, Hora, Día, Mes, Semana)

#### Ejemplos de tareas cron

- Ejecutar todos los días a medianoche:

	```
	0 0 * * * /usr/bin/date >> /home/dev/midnight.log
	```

- Ejecutar cada 15 minutos:

  ```
  */15 * * * * /home/dev/scripts/check_disk.sh
  ```

- Ejecutar todos los lunes a las 8 h:

  ```
  0 8 * * 1 echo "Weekly meeting" >> /home/dev/reminder.txt
  ```

- Ejecutar a las 3:30 h el primero de cada mes:

  ```
  30 3 1 * * /usr/local/bin/backup.sh
  ```

### Editar y gestionar crontabs

Para editar tu crontab personal:

  ```
crontab -e
  ```

Esto abre tu crontab en el editor predeterminado (`vi` o `ee`).

Para listar tus tareas:

```console
crontab -l
```

Para eliminar tu crontab:

```console
crontab -r
```

### ¿Dónde van los registros?

Cuando cron ejecuta una tarea, su salida (stdout y stderr) se envía por **correo electrónico** al usuario propietario de la tarea. En FreeBSD, estos correos se entregan localmente y se almacenan en `/var/mail/username`.

También puedes redirigir la salida a un archivo de registro para facilitar las cosas:

```text
0 0 * * * /home/dev/backup.sh >> /home/dev/backup.log 2>&1
```

Aquí:

- `>>` añade la salida al archivo `backup.log`.
- `2>&1` redirige los mensajes de error (stderr) al mismo archivo.

De este modo, siempre sabrás qué hicieron tus tareas cron, aunque no revises el correo del sistema.

### at: programación de tareas puntuales

A veces no necesitas una tarea recurrente, sino simplemente ejecutar algo más tarde, una sola vez. Para eso sirve **at**.

Antes de que un usuario pueda usar **at**, el superusuario debe añadir primero el nombre de usuario del usuario al archivo `/var/at/at.allow`.

```sh 
# echo "dev" >> /var/at/at.allow
```

Ahora el usuario puede ejecutar el comando `at`. El uso es bastante sencillo; veamos algunos ejemplos:

- Ejecutar un comando dentro de 10 minutos:

```sh
% echo "echo Hello FreeBSD > /home/dev/hello.txt" | at now + 10 minutes
```

- Ejecutar un comando mañana a las 9 h:

```sh
  % echo "/usr/local/bin/htop" | at 9am tomorrow
```

Las tareas programadas con `at` se ponen en cola y se ejecutan exactamente una vez. Puedes listarlas con `atq` y eliminarlas con `atrm`.

### periodic: el ayudante de mantenimiento de FreeBSD

FreeBSD incluye un sistema de mantenimiento integrado llamado **periodic**. Es un conjunto de scripts de shell que gestionan por ti las tareas de mantenimiento rutinario, para que no tengas que recordarlas manualmente.

Estas tareas se ejecutan automáticamente con **intervalos diarios, semanales y mensuales**, gracias a las entradas ya configuradas en el archivo cron de todo el sistema `/etc/crontab`. Esto significa que un sistema FreeBSD recién instalado ya se encarga de muchas tareas sin que tengas que hacer nada.

#### Dónde viven los scripts

Los scripts están organizados en directorios bajo `/etc/periodic`:

```text
/etc/periodic/daily
/etc/periodic/weekly
/etc/periodic/monthly
/etc/periodic/security
```

- **daily/** - tareas que se ejecutan cada día (rotación de logs, comprobaciones de seguridad, actualizaciones de bases de datos).
- **weekly/** - tareas que se ejecutan una vez a la semana (como actualizar la base de datos de `locate`).
- **monthly/** - tareas que se ejecutan una vez al mes (como los informes mensuales de contabilidad).
- **security/** - comprobaciones adicionales centradas en la seguridad del sistema.

#### Qué hace periodic por defecto

Algunos ejemplos de las tareas incluidas de serie:

- **Comprobaciones de seguridad** - busca binarios setuid, permisos de archivo inseguros o vulnerabilidades conocidas.
- **Rotación de logs** - comprime y archiva los logs en `/var/log` para que no crezcan indefinidamente.
- **Actualización de bases de datos** - reconstruye bases de datos auxiliares, como la que utiliza el comando `locate`.
- **Limpieza de archivos temporales** - elimina restos en `/tmp` y otros directorios de caché.

Tras ejecutarse, los scripts de periodic suelen enviar un resumen de sus resultados al **buzón de correo del usuario root** (puedes leerlo ejecutando `mail` como root).

**Error común de principiantes: «¡No ha pasado nada!»**

Muchos usuarios nuevos de FreeBSD ejecutan su sistema durante unos días sabiendo que periodic debería lanzar tareas diariamente, pero nunca ven ningún resultado y asumen que no ha funcionado. En realidad, los informes de periodic se envían al **correo del usuario root**, no se muestran en pantalla.

Para leerlos, inicia sesión como root y ejecuta:

```console
# mail
```

Pulsa Enter para abrir el buzón y ver los informes. Puedes salir del programa de correo escribiendo `q`.

**Consejo:** Si prefieres recibir estos informes en el buzón de tu usuario habitual, puedes configurar el reenvío de correo en `/etc/aliases` para que el correo de root se redirija a tu cuenta de usuario.

#### Ejecutar periodic manualmente

No es necesario esperar a que cron los dispare. Puedes ejecutar los conjuntos completos de tareas de forma manual:

```sh
% sudo periodic daily
% sudo periodic weekly
% sudo periodic monthly
```

O ejecutar directamente un solo script, por ejemplo:

```sh
% sudo /etc/periodic/security/100.chksetuid
```

#### Personalizar periodic con `periodic.conf`

Periodic no es una caja negra. Su comportamiento se controla mediante `/etc/periodic.conf` y `/etc/periodic.conf.local`.

**Buena práctica**: nunca edites los scripts directamente. En su lugar, sobreescribe su comportamiento en `periodic.conf`; así tus cambios estarán a salvo cuando FreeBSD actualice el sistema base.

Aquí tienes algunas opciones comunes que podrías utilizar:

- **Habilitar o deshabilitar tareas**

  ```
  daily_status_security_enable="YES"
  daily_status_network_enable="NO"
  ```

- **Controlar el manejo de logs**

  ```
  daily_clean_hoststat_enable="YES"
  weekly_clean_pkg_enable="YES"
  ```

- **Habilitar la actualización de la base de datos de locate**

  ```
  weekly_locate_enable="YES"
  ```

- **Controlar la limpieza de tmp**

  ```
  daily_clean_tmps_enable="YES"
  daily_clean_tmps_days="3"
  ```

- **Informes de seguridad**

  ```
  daily_status_security_inline="YES"
  daily_status_security_output="mail"
  ```

Para ver todas las opciones disponibles, usa el comando `man periodic.conf`

#### Descubrir todas las comprobaciones disponibles

A estas alturas ya sabes que periodic ejecuta tareas diarias, semanales y mensuales, pero quizás te preguntes: *¿cuáles son exactamente todas estas comprobaciones y qué hacen?*

Hay varias formas de explorarlas:

1. **Listar los scripts directamente**

   ```sh
   % ls /etc/periodic/daily
   % ls /etc/periodic/weekly
   % ls /etc/periodic/monthly
   % ls /etc/periodic/security
   ```

   Verás archivos con nombres como `100.clean-disks` o `480.leapfile-ntpd`; los nombres son descriptivos y te darán una idea de lo que hace cada script. Los números ayudan a controlar el orden en que se ejecutan.

2. **Leer la documentación**

   Las páginas de manual `periodic(8)` y `periodic.conf(5)` explican muchos de los scripts disponibles y sus opciones. Por ejemplo:

   ```
   man periodic.conf
   ```

   Te ofrece un resumen de las variables de configuración y lo que controlan.

3. **Examinar las cabeceras de los scripts**
    Abre cualquier script en `/etc/periodic/*/` con `less` y lee las primeras líneas de comentarios. Normalmente contienen una explicación legible del propósito del script.

Esto significa que nunca tienes que adivinar qué está haciendo periodic; siempre puedes inspeccionar los scripts, previsualizar su comportamiento o leer la documentación oficial.

#### Por qué esto importa para los desarrolladores

Para los usuarios cotidianos, periodic mantiene el sistema ordenado y seguro sin esfuerzo adicional. Pero como desarrollador, es posible que más adelante quieras:

- Añadir un **script periodic personalizado** para probar tu driver o supervisar su estado una vez al día.
- Rotar o limpiar archivos de log personalizados creados por tu driver.
- Ejecutar comprobaciones de integridad automatizadas (por ejemplo, verificar que el nodo de dispositivo de tu driver existe y responde).

Enganchándote a periodic, te apoyas en el mismo framework que FreeBSD utiliza para su propio mantenimiento.

**Laboratorio práctico: explorar y personalizar periodic**

1. Lista los scripts diarios disponibles:

   ```sh
   % ls /etc/periodic/daily
   ```

2. Ejecútalos manualmente:

   ```sh
   % sudo periodic daily
   ```

3. Abre `/etc/periodic.conf` (créalo si no existe) y añade:

   ```sh
   weekly_locate_enable="YES"
   ```

4. Previsualiza lo que harán las tareas semanales:

   ```sh
   % sudo periodic weekly
   ```

5. Dispara las tareas semanales y luego prueba:

   ```sh
   % locate passwd
   ```

### Laboratorio práctico: automatización de tareas

1. Programa un trabajo para que se ejecute cada minuto a modo de prueba:

```sh
   % crontab -e
   */1 * * * * echo "Hello from cron: $(date)" >> /home/dev/cron_test.log
```

2. Espera unos minutos y comprueba el archivo:

   ```sh
   % tail -n 5 /home/dev/cron_test.log
   ```

3. Programa un trabajo puntual con `at`:

   ```sh
   % echo "date >> /home/dev/at_test.log" | at now + 2 minutes
   ```

   Comprueba más tarde:

   ```sh
   % cat /home/dev/at_test.log
   ```

4. Ejecuta una tarea periódica manualmente:

   ```sh
   % sudo periodic daily
   ```

   Verás informes sobre archivos de registro, seguridad y estado del sistema.

### Errores habituales para principiantes

- Olvidarse de indicar **rutas completas**. Los trabajos de cron no utilizan el mismo entorno que tu shell, así que utiliza siempre rutas completas (`/usr/bin/ls` en lugar de simplemente `ls`).
- Olvidarse de redirigir la salida. Si no lo haces, los resultados pueden enviarse por correo electrónico de forma silenciosa.
- Trabajos solapados. Ten cuidado de no programar trabajos que entren en conflicto o se ejecuten con demasiada frecuencia.

### Por qué esto importa a los desarrolladores de drivers

Puede que te preguntes por qué dedicamos tiempo a los trabajos de cron y las tareas programadas. La respuesta es que la automatización es **la mejor aliada del desarrollador**. Cuando empieces a escribir drivers de dispositivo, a menudo querrás:

- Programar pruebas automáticas de tu driver (por ejemplo, comprobar si carga y descarga correctamente cada noche).
- Rotar y archivar los registros del kernel para hacer un seguimiento del comportamiento del driver a lo largo del tiempo.
- Ejecutar diagnósticos periódicos que interactúen con el nodo `/dev` de tu driver y registrar los resultados para su análisis.

Si dominas cron y periodic ahora, ya sabrás cómo configurar estas rutinas en segundo plano más adelante, ahorrándote tiempo y detectando errores a tiempo.

### En resumen

En esta sección, aprendiste cómo FreeBSD automatiza tareas utilizando tres herramientas principales:

- **cron** para trabajos recurrentes,
- **at** para la programación puntual,
- **periodic** para el mantenimiento integrado del sistema.

Practicaste la creación de trabajos, comprobaste su salida y aprendiste cómo el propio FreeBSD se apoya en la automatización para mantenerse en buen estado.

La automatización es útil, pero a veces necesitas ir más allá de los horarios fijos. Puede que quieras encadenar comandos, usar bucles o añadir lógica para decidir qué ocurre. Ahí es donde entra en juego el **shell scripting**. En la siguiente sección, escribiremos tus primeros scripts y veremos cómo crear automatización personalizada adaptada a tus necesidades.

## Introducción al shell scripting

Has aprendido a ejecutar comandos uno a uno. El shell scripting te permite **guardar esos comandos en un programa reutilizable**. En FreeBSD, el shell nativo y recomendado para el scripting es **`/bin/sh`**. Este shell sigue el estándar POSIX y está disponible en todos los sistemas FreeBSD.

> **Nota importante para usuarios de Linux**
>  En muchas distribuciones de Linux, los ejemplos utilizan **bash**. En FreeBSD, **bash no forma parte del sistema base**. Puedes instalarlo con `pkg install bash`, donde residirá en `/usr/local/bin/bash`. Para scripts portables y sin dependencias en FreeBSD, usa `#!/bin/sh`.

Construiremos esta sección de forma progresiva: shebang y ejecución, variables y entrecomillado, condiciones, bucles, funciones, trabajo con archivos, códigos de retorno y depuración básica. Cada script de ejemplo que aparece a continuación está **completamente comentado** para que un principiante absoluto pueda seguirlo.

### 1) Tu primer script: shebang, hazlo ejecutable, ejecútalo

Crea un archivo llamado `hello.sh`:

```sh
#!/bin/sh
# hello.sh   a first shell script using FreeBSD's native /bin/sh
# Print a friendly message with the current date and the active user.

# 'date' prints the current date and time
# 'whoami' prints the current user
echo "Hello from FreeBSD!"
echo "Date: $(date)"
echo "User: $(whoami)"
```

**Consejo: ¿qué significa `#!` (shebang)?**

La primera línea de este script es:

```sh
#!/bin/sh
```

Esto se llama la **línea shebang**. Los dos caracteres `#!` indican al sistema *qué programa debe interpretar el script*.

- `#!/bin/sh` significa: «ejecuta este script usando el shell **sh**».
- En otros sistemas también puedes ver `#!/bin/tcsh`, `#!/usr/bin/env python3` o `#!/usr/bin/env bash`.

Cuando haces que un script sea ejecutable y lo ejecutas, el sistema consulta esta línea para decidir qué intérprete usar. Sin ella, el script puede fallar o comportarse de forma diferente dependiendo de tu shell de inicio de sesión.

**Regla general**: incluye siempre una línea shebang al principio de tus scripts. En FreeBSD, `#!/bin/sh` es la opción más segura y portable.

Ahora hagamos que el script sea ejecutable y ejecutémoslo:

```sh
% chmod +x hello.sh       # give the user execute permission
% ./hello.sh              # run it from the current directory
```

Si obtienes «Permission denied», olvidaste el `chmod +x`.
Si obtienes «Command not found», probablemente escribiste `hello.sh` sin `./` y el directorio actual no está incluido en el `PATH` del sistema.

**Consejo**: no te sientas presionado a dominar todas las características del scripting de una vez. Empieza con poco: escribe un script de 2 o 3 líneas que imprima tu nombre de usuario y la fecha. Cuando te sientas cómodo, añade condiciones (`if`), luego bucles y finalmente funciones. El shell scripting es como el LEGO: construye un bloque a la vez.

### 2) Variables y entrecomillado

Las variables de shell son cadenas sin tipo. Se asignan con `name=value` y se referencian con `$name`. No debe haber **ningún espacio** alrededor del `=`.

```sh
#!/bin/sh
# vars.sh   demonstrate variables and proper quoting

name="dev"
greeting="Welcome"
# Double quotes preserve spaces and expand variables.
echo "$greeting, $name"
# Single quotes prevent expansion. This prints the literal characters.
echo '$greeting, $name'

# Command substitution captures output of a command.
today="$(date +%Y-%m-%d)"
echo "Today is $today"
```

Errores habituales de principiantes:

- Usar espacios alrededor de `=`: `name = dev` es un error.
- Olvidarse de las comillas cuando las variables pueden contener espacios. Usa `"${var}"` como hábito.

### 3) Estado de salida y operadores de cortocircuito

Cada comando devuelve un **estado de salida**. Cero significa éxito. Distinto de cero significa error. El shell te permite encadenar comandos usando `&&` y `||`.

```sh
#!/bin/sh
# status.sh   show exit codes and conditional chaining

# Try to list a directory that exists. 'ls' should return 0.
ls /etc && echo "Listing /etc succeeded"

# Try something that fails. 'false' always returns nonzero.
false || echo "Previous command failed, so this message appears"

# You can test the last status explicitly using $?
echo "Last status was $?"
```

### 4) Pruebas y condiciones: `if`, `[ ]`, archivos y números

Usa `if` con el comando `test` o su forma entre corchetes `[ ... ]`. Debe haber espacios dentro de los corchetes.

```sh
#!/bin/sh
# ifs.sh   demonstrate file and numeric tests

file="/etc/rc.conf"

# -f tests if a regular file exists
if [ -f "$file" ]; then
  echo "$file exists"
else
  echo "$file does not exist"
fi

num=5
if [ "$num" -gt 3 ]; then
  echo "$num is greater than 3"
fi

# String tests
user="$(whoami)"
if [ "$user" = "root" ]; then
  echo "You are root"
else
  echo "You are $user"
fi
```

Pruebas de archivo útiles:

- `-e` existe
- `-f` archivo regular
- `-d` directorio
- `-r` legible
- `-w` escribible
- `-x` ejecutable

Comparaciones numéricas:

- `-eq` igual
- `-ne` distinto
- `-gt` mayor que
- `-ge` mayor o igual
- `-lt` menor que
- `-le` menor o igual

### 5) Bucles: `for` y `while`

Los bucles te permiten repetir trabajo sobre archivos o líneas de entrada.

```sh
#!/bin/sh
# loops.sh   for and while loops in /bin/sh

# A 'for' loop over pathnames. Always quote expansions to handle spaces safely.
for f in /etc/*.conf; do
  echo "Found conf file: $f"
done

# A 'while' loop to read lines from a file safely.
# The 'IFS=' and 'read -r' avoid trimming spaces and backslash escapes.
count=0
while IFS= read -r line; do
  count=$((count + 1))
done < /etc/hosts
echo "The /etc/hosts file has $count lines"
```

La aritmética en POSIX sh usa `$(( ... ))` para operaciones enteras simples.

### 6) Sentencias `case` para una ramificación ordenada

`case` es ideal cuando tienes varios patrones que comparar.

```sh
#!/bin/sh
# case.sh   handle options with a case statement

action="$1"   # first command line argument

case "$action" in
  start)
    echo "Starting service"
    ;;
  stop)
    echo "Stopping service"
    ;;
  restart)
    echo "Restarting service"
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}" >&2
    exit 2
    ;;
esac
```

### 7) Funciones para organizar tu script

Las funciones mantienen el código legible y reutilizable.

```sh
#!/bin/sh
# functions.sh - Demonstrates using functions and command-line arguments in a shell script.
#
# Usage:
#   ./functions.sh NUM1 NUM2
# Example:
#   ./functions.sh 5 7
#   This will output: "[INFO] 5 + 7 = 12"

# A simple function to print informational messages
say() {
  # "$1" represents the first argument passed to the function
  echo "[INFO] $1"
}

# A function to sum two integers
sum() {
  # "$1" and "$2" are the first and second arguments
  local a="$1"
  local b="$2"

  # Perform arithmetic expansion to add them
  echo $((a + b))
}

# --- Main script execution starts here ---

# Make sure the user provided two arguments
if [ $# -ne 2 ]; then
  echo "Usage: $0 NUM1 NUM2"
  exit 1
fi

say "Beginning work"

# Call the sum() function with the provided arguments
result="$(sum "$1" "$2")"

# Print the result in a nice format
say "$1 + $2 = $result"
```

### 8) Un ejemplo práctico: un pequeño script de copia de seguridad

Este script crea un archivo comprimido con marca de tiempo de un directorio en `~/backups`. Usa únicamente utilidades del sistema base de FreeBSD.

```sh
#!/bin/sh
# backup.sh   create a timestamped tar archive of a directory
# Usage: ./backup.sh /path/to/source
# Notes:
#  - Uses /bin/sh so it runs on a clean FreeBSD 14.x install.
#  - Creates ~/backups if it does not exist.
#  - Names the archive sourcebasename-YYYYMMDD-HHMMSS.tar.gz

set -eu
# set -e: exit immediately if any command fails
# set -u: treat use of unset variables as an error

# Validate input
if [ $# -ne 1 ]; then
  echo "Usage: $0 /path/to/source" >&2
  exit 2
fi

src="$1"

# Verify that source is a directory
if [ ! -d "$src" ]; then
  echo "Error: $src is not a directory" >&2
  exit 3
fi

# Prepare destination directory
dest="${HOME}/backups"
mkdir -p "$dest"

# Build a safe archive name using only the last path component
base="$(basename "$src")"
stamp="$(date +%Y%m%d-%H%M%S)"
archive="${dest}/${base}-${stamp}.tar.gz"

# Create the archive
# tar(1) is in the base system. The flags mean:
#  - c: create  - z: gzip  - f: file name  - C: change to directory
tar -czf "$archive" -C "$(dirname "$src")" "$base"

echo "Backup created: $archive"
```

Ejecútalo:

```sh
% chmod +x backup.sh
% ./backup.sh ~/directory_you_want_to_backup
```

Encontrarás el archivo comprimido en `~/backups`.

### 9) Trabajar con archivos temporales de forma segura

Nunca pongas nombres fijos como `/tmp/tmpfile`. Usa `mktemp(1)` del sistema base.

```sh
#!/bin/sh
# tmp_demo.sh   create and clean a temporary file safely

set -eu

tmpfile="$(mktemp -t myscript)"
# Arrange cleanup on exit for success or error
cleanup() {
  [ -f "$tmpfile" ] && rm -f "$tmpfile"
}
trap cleanup EXIT

echo "Temporary file is $tmpfile"
echo "Hello temp" > "$tmpfile"
echo "Contents: $(cat "$tmpfile")"
```

`trap` programa una función para que se ejecute cuando el script termine, lo que evita dejar archivos obsoletos.

### 10) Depuración de tus scripts

- `set -x` imprime cada comando antes de ejecutarlo. Añádelo cerca del principio y elimínalo una vez que hayas solucionado el problema.
- Usa `echo` para mensajes de progreso para que el usuario sepa qué está ocurriendo.
- Comprueba los códigos de retorno y gestiona los fallos de forma explícita.
- Guarda en un archivo de registro redirigiendo la salida: `mycmd >> ~/my.log 2>&1`.

Ejemplo:

```sh
#!/bin/sh
# debug_demo.sh   show simple tracing

# set -x comment to disable verbose trace:
set -x

echo "Step 1"
ls /etc >/dev/null

echo "Step 2"
date
```

### 11) Reuniéndolo todo: organizar las descargas por tipo

Esta pequeña utilidad ordena los archivos de `~/Downloads` en subcarpetas por extensión. Muestra el uso de bucles, `case`, pruebas y comprobaciones de seguridad.

```sh
#!/bin/sh
# organize_downloads.sh - Tidy ~/Downloads by file extension
#
# Usage:
#   ./organize_downloads.sh
#
# Creates subdirectories like Documents, Images, Audio, Video, Archives, Other
# and moves matched files into them safely.

set -eu

downloads="${HOME}/Downloads"

# Create a temporary file to store the list of files
tmpfile=$(mktemp)

# Remove temporary file when script exits (normal or error)
trap 'rm -f "$tmpfile"' EXIT

# Ensure the Downloads directory exists
if [ ! -d "$downloads" ]; then
  echo "Downloads directory not found at $downloads" >&2
  exit 1
fi

cd "$downloads"

# Create target folders if missing
mkdir -p Documents Images Audio Video Archives Other

# Find all regular files in current directory (non-recursive, excluding hidden files)
# -maxdepth 1: don't search in subdirectories
# -type f: only regular files (not directories or symlinks)
# ! -name ".*": exclude hidden files (those starting with a dot)
count=0
find . -maxdepth 1 -type f ! -name ".*" > "$tmpfile"
while IFS= read -r f; do
  # Strip leading "./" from path
  fname=${f#./}
  
  # Skip if filename is empty (shouldn't happen, but safety check)
  [ -z "$fname" ] && continue

  # Convert filename extension to lowercase for matching
  lower=$(printf '%s' "$fname" | tr '[:upper:]' '[:lower:]')

  case "$lower" in
    *.pdf|*.txt|*.md|*.doc|*.docx)  dest="Documents" ;;
    *.png|*.jpg|*.jpeg|*.gif|*.bmp) dest="Images" ;;
    *.mp3|*.wav|*.flac)             dest="Audio" ;;
    *.mp4|*.mkv|*.mov|*.avi)        dest="Video" ;;
    *.zip|*.tar|*.gz|*.tgz|*.bz2)   dest="Archives" ;;
    *)                              dest="Other" ;;
  esac

  echo "Moving '$fname' -> $dest/"
  mv -n -- "$fname" "$dest/"   # -n prevents overwriting existing files
  count=$((count + 1))         # Increment the counter
done < "$tmpfile"              # Feed the temporary file into the while loop

if [ $count -eq 0 ]; then
  echo "No files to organize."
else
  echo "Done. Organized $count file(s)."
fi
```

### Laboratorio práctico: tres mini tareas

1. **Escribe un registrador**
    Crea `logger.sh` que añada una línea con marca de tiempo a `~/activity.log` con el directorio actual y el usuario. Ejecútalo y luego consulta el registro con `tail`.
2. **Comprueba el espacio en disco**
    Crea `check_disk.sh` que avise cuando el uso del sistema de archivos raíz supere el 80 por ciento. Usa `df -h /` y extrae el porcentaje con el recorte de estilo `${var%%%}` o un simple `awk`. Sal con estado 1 si supera el umbral para que cron pueda avisarte.
3. **Envuelve tu copia de seguridad**
    Crea `backup_cron.sh` que llame a `backup.sh` del ejercicio anterior y registre la salida en `~/backup.log`. Añade una entrada en crontab para ejecutarlo diariamente a las 3 AM. Recuerda usar rutas completas dentro del script.

Todos los scripts deben comenzar con `#!/bin/sh`, contener comentarios que expliquen cada paso, usar comillas alrededor de las expansiones de variables y gestionar los errores donde sea razonable.

### Errores habituales de principiantes y cómo evitarlos

- **Usar características de bash en scripts con `#!/bin/sh`.** Ciñete a las construcciones POSIX. Si necesitas bash, indícalo en el shebang y recuerda que en FreeBSD está en `/usr/local/bin/bash`.
- **Olvidarse de entrecomillar las variables.** Usa `"${var}"` para evitar sorpresas con la división de palabras y el globbing.
- **Asumir el mismo entorno bajo cron.** Usa siempre rutas completas y redirige la salida a un archivo de registro.
- **Usar nombres fijos para archivos temporales.** Usa `mktemp` y `trap` para limpiar.
- **Espacios alrededor de `=` en las asignaciones.** `name=value` es correcto. `name = value` no lo es.

### En resumen

En esta sección, aprendiste la **manera nativa de FreeBSD** de automatizar trabajo con scripts portables que se ejecutan en cualquier instalación limpia de FreeBSD. Ahora puedes escribir pequeños programas con `/bin/sh`, gestionar argumentos, probar condiciones, recorrer archivos en bucle, definir funciones, usar archivos temporales de forma segura y depurar problemas con herramientas simples. Mientras escribes drivers, los scripts te ayudarán a repetir pruebas, recopilar registros y empaquetar builds de forma fiable.

Antes de continuar, un recordatorio: no necesitas memorizar cada construcción u opción de comando. Parte de ser productivo en UNIX es saber dónde **encontrar la información correcta en el momento adecuado**.

En la siguiente sección, profundizaremos en la **portabilidad** en sí, analizando las diferencias sutiles entre shells, los hábitos que mantienen los scripts robustos en distintos sistemas y cómo elegir características que no te sorprendan más adelante.

## Portabilidad del shell: gestión de casos límite y bash frente a sh

Hasta ahora, hemos escrito scripts usando el shell nativo de FreeBSD, `/bin/sh`, que sigue el estándar POSIX. Esto hace que nuestros scripts sean portables entre diferentes sistemas UNIX. Sin embargo, a medida que explores ejemplos de shell scripting en línea o recibas contribuciones de otros desarrolladores, encontrarás scripts escritos para **bash** que utilizan características no disponibles en POSIX sh.

Entender las diferencias entre bash y sh, y saber cómo gestionar casos límite como nombres de archivo inusuales, te ayudará a escribir scripts robustos y a decidir cuándo la portabilidad importa más que la comodidad.

### El problema: nombres de archivo con caracteres especiales

UNIX permite que los nombres de archivo contengan casi cualquier carácter, salvo la barra diagonal `/` (que separa los directorios) y el carácter nulo `\0`. Esto significa que un nombre de archivo puede contener legalmente espacios, saltos de línea, tabulaciones u otros caracteres sorprendentes.

Creemos un archivo con un salto de línea en su nombre para ver cómo afecta a nuestros scripts:

```sh
% cd ~
% touch $'file_with\nnewline.txt'
% ls
file_with?newline.txt
```

El símbolo `?` aparece porque `ls` sustituye los caracteres no imprimibles al mostrar los nombres de archivo. El nombre de archivo real contiene:

```text
file_with
newline.txt
```

Ahora veamos qué ocurre cuando un script intenta procesar este archivo.

### Un enfoque ingenuo que falla

Aquí tienes un script simple que lista los archivos de tu directorio personal:

```sh
#!/bin/sh
# list_files.sh - count files in home directory

set -eu
cd "${HOME}"

count=0
while IFS= read -r f; do
  fname=${f#./}
  echo "File found: '$fname'"
  count=$((count + 1))
done << EOF
$(find . -maxdepth 1 -type f ! -name ".*" -print)
EOF

echo "Total files found: $count"
```

Ejecutar este script con nuestro nombre de archivo inusual produce resultados incorrectos:

```sh
% ./list_files.sh
File found: 'file_with'
File found: 'newline.txt'
Total files found: 2
```

El script cree que un archivo son en realidad dos, porque `find -print` genera una ruta por línea y nuestro nombre de archivo contiene un carácter de salto de línea. El script falla con un nombre de archivo perfectamente válido en UNIX.

### La solución con bash: usar delimitadores nulos

Una forma de solucionar esto es utilizar caracteres nulos (`\0`) como delimitadores en lugar de saltos de línea. Bash admite esto con la opción `-d` del comando `read`:

```sh
#!/usr/local/bin/bash
# list_files_bash.sh - correctly handle unusual filenames with bash

set -eu
cd "${HOME}"

count=0
while IFS= read -r -d '' f; do
  fname=${f#./}
  echo "File found: '$fname'"
  count=$((count + 1))
done < <(find . -maxdepth 1 -type f ! -name ".*" -print0)

echo "Total files found: $count"
```

Observa dos cambios:

1. **Shebang**: cambiado a `#!/usr/local/bin/bash` (la ubicación de bash en FreeBSD tras `pkg install bash`)
2. **Indicador de find**: cambiado de `-print` a `-print0` (genera rutas delimitadas por nulos)
3. **Opción de read**: se añadió `-d ''` para indicar a `read` que use el nulo como delimitador

Esta versión funciona correctamente:

```sh
% ./list_files_bash.sh
File found: 'file_with
newline.txt'
Total files found: 1
```

¿La desventaja? **Este script ahora requiere bash**, que no forma parte del sistema base de FreeBSD. Crea una dependencia.

### La alternativa compatible con POSIX

Si la portabilidad importa más que gestionar todos los casos límite posibles, podemos escribir una versión compatible con POSIX que evite las características específicas de bash:

```sh
#!/bin/sh
# list_files_posix.sh - POSIX-compliant file listing

set -eu
cd "${HOME}"

# Use a temporary file instead of a pipe
tmpfile=$(mktemp)
trap 'rm -f "$tmpfile"' EXIT

# Store find results in temporary file
find . -maxdepth 1 -type f ! -name ".*" > "$tmpfile"

count=0
while IFS= read -r f; do
  fname=${f#./}
  [ -z "$fname" ] && continue
  
  echo "File found: '$fname'"
  count=$((count + 1))
done < "$tmpfile"

echo "Total files found: $count"
```

Esta versión:

- Funciona en cualquier shell compatible con POSIX (no requiene bash)
- Usa un archivo temporal en lugar de una tubería para evitar problemas con variables en subshells
- Limpia automáticamente con `trap`
- Gestiona nombres de archivo con espacios y la mayoría de los caracteres especiales

¿La limitación? Aún no puede gestionar correctamente los nombres de archivo que contienen saltos de línea, porque el comando `read` de POSIX sh no ofrece ninguna forma de usar un delimitador diferente. Para esta versión:

```sh
% ./list_files_posix.sh
File found: 'file_with'
File found: 'newline.txt'
Total files found: 2
```

### Comprendiendo el compromiso

Esto revela un punto de decisión importante en el scripting de shell:

**Portabilidad frente a cobertura de casos extremos**

| Enfoque          | Ventajas                                          | Inconvenientes                                            |
| ---------------- | ------------------------------------------------- | --------------------------------------------------------- |
| **POSIX sh**     | Funciona en cualquier sistema, sin dependencias   | No puede gestionar nombres de archivo con saltos de línea |
| **bash with -d** | Gestiona todos los nombres de archivo válidos     | Requiere que bash esté instalado                          |
| **find -exec**   | Compatible con POSIX, gestiona todo               | Sintaxis más compleja                                     |

Para la mayoría de los scripts del mundo real, el enfoque POSIX es suficiente. Los nombres de archivo con saltos de línea son extremadamente raros fuera de ejemplos artificiales o exploits de seguridad. Los archivos con espacios, caracteres unicode y otros caracteres imprimibles funcionan correctamente con la versión POSIX.

### Cuándo elegir bash

Usa bash cuando:

- Estés escribiendo herramientas personales en un entorno donde sabes que bash estará disponible
- Realmente necesites gestionar nombres de archivo con saltos de línea (muy raro)
- Necesites características específicas de bash como arrays, expresiones regulares extendidas o manipulación avanzada de cadenas
- El script forme parte de un proyecto que ya depende de bash

Usa POSIX sh cuando:

- Escribas scripts de administración del sistema que deban ejecutarse en cualquier sistema FreeBSD
- Contribuyas a los scripts del sistema base de FreeBSD
- Se requiera la máxima portabilidad
- El script pueda ejecutarse en modo de rescate o en entornos mínimos

### Una tercera opción: find -exec

Para completar el panorama, aquí tienes un enfoque compatible con POSIX que gestiona todos los nombres de archivo correctamente sin necesidad de bash:

```sh
#!/bin/sh
# list_files_exec.sh - handle all filenames using find -exec

set -eu
cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -exec sh -c '
  for f; do
    fname=${f#./}
    printf "File found: '\''%s'\''\n" "$fname"
  done
' sh {} +
```

Esto funciona porque `find -exec` pasa los nombres de archivo como argumentos, no a través de tuberías ni de lectura línea por línea. Es compatible con POSIX y gestiona todos los casos extremos, pero la sintaxis resulta menos intuitiva para los principiantes.

### Consejos prácticos

Al escribir scripts de shell:

1. **Empieza con `/bin/sh`** - Comienza con scripts compatibles con POSIX
2. **Entrecomilla tus variables** - Usa siempre `"$var"` para gestionar espacios
3. **Prueba con nombres de archivo inusuales** - Crea archivos de prueba con espacios en sus nombres
4. **Documenta las dependencias** - Si usas bash, indícalo claramente en los comentarios
5. **Acepta las limitaciones razonables** - No sacrifiques la portabilidad por casos extremos que nunca encontrarás

El script organize_downloads.sh que escribimos antes usa el enfoque de archivo temporal compatible con POSIX. Gestiona correctamente la gran mayoría de los nombres de archivo del mundo real y sigue siendo portable en cualquier sistema FreeBSD.

Recuerda: **el mejor script es aquel que funciona de manera fiable en tu entorno de destino**. No añadas bash como dependencia para casos extremos que nunca encontrarás, pero tampoco te mortifiques con las restricciones de POSIX si estás escribiendo herramientas personales en un sistema donde bash ya está instalado.

### En resumen

Ahora ya has comprendido por qué la portabilidad es una decisión de diseño, no algo que se añade a posteriori. Has aprendido a preferir POSIX `/bin/sh` para scripts que deben ejecutarse en cualquier lugar de FreeBSD, a evitar características exclusivas de bash, a usar `printf` en lugar de un `echo` sin restricciones, a entrecomillar las variables por defecto, a comprobar los códigos de salida y a elegir un shebang claro para que el intérprete correcto ejecute tu código. Por el camino, revisitamos los elementos básicos que ya conoces: argumentos, condicionales, bucles, funciones y archivos temporales seguros, y los ajustamos para obtener un comportamiento predecible en cualquier sistema.

Nadie guarda todos los detalles en la cabeza, y tú tampoco necesitas hacerlo. La siguiente sección te muestra dónde **buscar información a la manera de FreeBSD**: páginas de manual, `apropos`, ayuda integrada, el FreeBSD Handbook y los recursos de la comunidad. Estos se convertirán en tus compañeros del día a día a medida que te adentres más en el desarrollo de drivers de dispositivo.

## Cómo buscar ayuda y documentación en FreeBSD

Nadie, ni siquiera el desarrollador más experimentado, recuerda cada comando, opción o llamada al sistema. La verdadera fortaleza de un sistema UNIX como FreeBSD es que se distribuye con **excelente documentación** y cuenta con una comunidad de apoyo que puede ayudarte cuando te quedes atascado.

En esta sección, exploraremos las principales formas de obtener información: **las páginas de manual, el FreeBSD Handbook, los recursos en línea y la comunidad**. Al terminar, sabrás exactamente dónde buscar cuando tengas una pregunta, ya sea sobre el uso de `ls` o sobre cómo escribir un driver de dispositivo.

### El poder de las páginas de manual

Las **páginas de manual**, o **man pages**, son el sistema de referencia integrado de UNIX. Cada comando, llamada al sistema, función de biblioteca, archivo de configuración e interfaz del kernel tiene su man page.

Las lees con el comando `man`, por ejemplo:

```console
% man ls
```

Esto abre la documentación de `ls`, el comando para listar el contenido de un directorio. Usa la barra espaciadora para desplazarte y `q` para salir.

#### Secciones de las man pages

FreeBSD organiza las man pages en secciones numeradas. Un mismo nombre puede existir en varias secciones, por lo que hay que especificar cuál quieres.

- **1** - Comandos de usuario (por ejemplo, `ls`, `cp`, `ps`)
- **2** - Llamadas al sistema (por ejemplo, `open(2)`, `write(2)`)
- **3** - Funciones de biblioteca (biblioteca estándar de C, funciones matemáticas)
- **4** - Drivers de dispositivo y archivos especiales (por ejemplo, `null(4)`, `random(4)`)
- **5** - Formatos de archivo y convenciones (`passwd(5)`, `rc.conf(5)`)
- **7** - Varios (protocolos, convenciones)
- **8** - Comandos de administración del sistema (por ejemplo, `ifconfig(8)`, `shutdown(8)`)
- **9** - Interfaces para el desarrollador del kernel (¡fundamentales para quien escribe drivers!)

Ejemplo:

```sh
% man 2 open      # system call open()
% man 9 bus_space # kernel function for accessing device registers
```

#### La sección 9: el manual del desarrollador del kernel

La mayoría de los usuarios de FreeBSD viven en la sección **1** (comandos de usuario) y los administradores pasan mucho tiempo en la sección **8** (gestión del sistema). Pero como desarrollador de drivers, pasarás gran parte de tu tiempo en la **sección 9**.

La sección 9 contiene la documentación de las **interfaces para el desarrollador del kernel** sobre funciones, macros y subsistemas que solo están disponibles dentro del kernel.

Algunos ejemplos:

- `man 9 device` - Visión general de las interfaces del driver de dispositivo.
- `man 9 bus_space` - Acceso a los registros de hardware.
- `man 9 mutex` - Primitivas de sincronización para el kernel.
- `man 9 taskqueue` - Planificación de trabajo diferido en el kernel.
- `man 9 malloc` - Asignación de memoria dentro del kernel.

A diferencia de la sección 2 (llamadas al sistema) o la sección 3 (bibliotecas), estas **no están disponibles en el espacio de usuario**. Son parte del propio kernel, y las usarás cuando escribas drivers y módulos del kernel.

Piensa en la sección 9 como el **manual de API del desarrollador para el kernel de FreeBSD**.

#### Un primer vistazo práctico

No necesitas entender todos los detalles todavía, pero puedes echar un vistazo:

```sh
% man 9 device
% man 9 bus_dma
% man 9 sysctl
```

Verás que el estilo es diferente al de las man pages de comandos de usuario: estas se centran en **funciones del kernel, estructuras y ejemplos de uso**.

A lo largo de este libro, recurriremos constantemente a la sección 9 a medida que presentemos nuevas características del kernel. Considérala tu compañera más importante en el camino que tienes por delante.

#### Búsqueda en las man pages

Si no sabes el nombre exacto del comando, usa la opción `-k` (equivalente a `apropos`):

```console
man -k network
```

Esto muestra todas las man pages relacionadas con la red.

Otro ejemplo:

```console
man -k disk | less
```

Esto te mostrará herramientas, drivers y llamadas al sistema relacionadas con los discos.

### El FreeBSD Handbook

El **FreeBSD Handbook** es la guía oficial y completa del sistema operativo.

Puedes leerlo en línea:

https://docs.freebsd.org/en/books/handbook/

El Handbook incluye:

- Instalación de FreeBSD
- Administración del sistema
- Redes
- Almacenamiento y sistemas de archivos
- Seguridad y jails
- Temas avanzados

El Handbook es un **excelente complemento de este libro**. Mientras que este libro se centra en el desarrollo de drivers de dispositivo, el Handbook te proporciona un conocimiento amplio del sistema al que siempre puedes volver.

#### Otra documentación

- **Man pages en línea**: https://man.freebsd.org
- **FreeBSD Wiki**: https://wiki.freebsd.org (notas mantenidas por la comunidad, HOWTOs y documentación en proceso).
- **Developer's Handbook**: https://docs.freebsd.org/en/books/developers-handbook, dirigido a programadores.
- **Porter's Handbook**: https://docs.freebsd.org/en/books/porters-handbook, para quienes empaquetan software para FreeBSD.

### Comunidad y soporte

La documentación te llevará lejos, pero a veces necesitas hablar con personas reales. FreeBSD tiene una comunidad activa y acogedora.

- **Listas de correo**: https://lists.freebsd.org
  - `freebsd-questions@` es para ayuda general a usuarios.
  - `freebsd-hackers@` es para debates sobre desarrollo.
  - `freebsd-drivers@` es específica para el desarrollo de drivers de dispositivo.
- **FreeBSD Forums**: https://forums.freebsd.org, un lugar amigable y accesible para principiantes donde hacer preguntas.
- **Grupos de usuarios**:
  - Por todo el mundo existen **grupos de usuarios de FreeBSD y BSD** que organizan encuentros, charlas y talleres.
  - Algunos ejemplos son *NYCBUG (New York City BSD User Group)*, *BAFUG (Bay Area FreeBSD User Group)* y muchos clubes universitarios.
  - Normalmente puedes encontrarlos a través de la FreeBSD Wiki, listas de correo tecnológicas locales o meetup.com.
  - Si no encuentras ninguno cerca, considera iniciar un pequeño grupo; incluso un puñado de entusiastas que se reúnan en línea o en persona puede convertirse en una valiosa red de apoyo.
- **Chat**:
  - **IRC** en Libera.Chat (`#freebsd`).
  - **Discord**: existen comunidades bastante activas; usa este enlace para unirte: https://discord.com/invite/freebsd
- **Reddit**: https://reddit.com/r/freebsd

Los grupos de usuarios y los foros son especialmente valiosos porque a menudo puedes hacer preguntas en tu idioma nativo, o incluso conocer personas que contribuyen a FreeBSD en tu zona.

#### Cómo pedir ayuda

En algún momento, todo el mundo se queda atascado. Una de las fortalezas de FreeBSD es su comunidad activa y solidaria, pero para obtener respuestas útiles, debes hacer preguntas claras, completas y respetuosas.

Cuando publiques en una lista de correo, un foro, IRC o un canal de Discord, incluye:

- **Tu versión de FreeBSD**
   Ejecuta:

  ```sh
  % uname -a
  ```

  Esto indica a quienes te ayudan exactamente qué versión, nivel de parche y arquitectura estás usando.

- **Qué intentabas hacer**
   Describe tu objetivo, no solo el comando que falló. Quienes te ayudan a veces pueden sugerir un enfoque mejor que el que intentaste.

- **Los mensajes de error exactos**
   Copia y pega el texto del error en lugar de parafrasearlo. Incluso las pequeñas diferencias importan.

- **Pasos para reproducir el problema**
   Si otra persona puede reproducir tu problema, normalmente podrá resolverlo mucho más rápido.

- **Qué ya intentaste**
   Menciona los comandos, los cambios de configuración o la documentación que consultaste. Esto demuestra que has hecho un esfuerzo y evita que te sugieran cosas que ya probaste.

#### Ejemplo de una petición de ayuda inadecuada

> "Los ports no funcionan, ¿cómo lo arreglo?"

Omite la versión, los comandos, los errores y el contexto. Nadie puede responder sin adivinar.

#### Ejemplo de una buena petición de ayuda

> "Estoy ejecutando FreeBSD 14.3-RELEASE en amd64. Intenté compilar `htop` desde los ports con `cd /usr/ports/sysutils/htop && make install clean`. La compilación falló con el siguiente error:
>
> ```
> error: ncurses.h: No such file or directory
> ```
>
> Ya probé `pkg install ncurses`, pero el error persiste. ¿Qué debería comprobar a continuación?"

Es breve pero completa; la versión, el comando, el error y los pasos de solución de problemas están todos ahí.

**Consejo**: Sé siempre educado y paciente. Recuerda que la mayoría de los colaboradores de FreeBSD son **voluntarios**. Una pregunta clara y respetuosa no solo aumenta tus posibilidades de recibir una respuesta útil, sino que también genera buena voluntad en la comunidad.

### Laboratorio práctico: explorar la documentación

1. Abre la man page de `ls`. Encuentra y prueba al menos dos opciones que no conocieras.

   ```sh
   % man ls
   ```

2. Usa `man -k` para buscar comandos relacionados con los discos.

   ```sh
   % man -k disk | less
   ```

3. Abre la man page de `open(2)` y compárala con `open(3)`. ¿Cuál es la diferencia?

4. Echa un vistazo a la documentación del desarrollador del kernel:

   ```sh
   % man 9 device
   ```

5. Visita https://docs.freebsd.org/ y busca la página sobre el arranque del sistema (`rc.d`). Compárala con `man rc.conf`.

### En resumen

FreeBSD te ofrece herramientas sólidas para aprender por tu cuenta. Las **man pages** son tu primer recurso; siempre están en tu sistema, siempre actualizadas, y cubren todo, desde los comandos básicos hasta las APIs del kernel. El **Handbook** es tu guía de referencia general, y la comunidad de listas de correo, foros, grupos de usuarios y chats en línea está ahí para ayudarte cuando necesites respuestas de otras personas.

Más adelante, cuando empieces a escribir drivers, dependerás mucho de las man pages (sobre todo de la sección 9) y de las discusiones en las listas de correo y foros de FreeBSD. Saber cómo encontrar información es tan importante como memorizar comandos.

A continuación, nos adentraremos en el sistema para **examinar los mensajes del kernel y sus parámetros configurables**. Herramientas como `dmesg` y `sysctl` te permiten ver qué está haciendo el kernel y serán esenciales cuando empieces a cargar y probar tus propios drivers de dispositivo.

## Una mirada al kernel y el estado del sistema

A estas alturas, ya sabes moverte por FreeBSD, gestionar archivos, controlar procesos e incluso escribir scripts. Eso te convierte en un usuario capaz. Pero escribir drivers significa adentrarse en la **mente del kernel**. Necesitarás ver lo que FreeBSD mismo ve:

- ¿Qué hardware se detectó?
- ¿Qué drivers se cargaron?
- ¿Qué parámetros ajustables existen dentro del kernel?
- ¿Cómo aparecen los dispositivos ante el sistema operativo?

FreeBSD te ofrece **tres ventanas mágicas al estado del kernel**:

1. **`dmesg`** - el diario del kernel.
2. **`sysctl`** - el panel de control lleno de interruptores y medidores.
3. **`/dev`** - la puerta donde los dispositivos aparecen como archivos.

Estas tres herramientas se convertirán en tus **compañeras**. Cada vez que añadas o depures un driver, las usarás. Vamos a verlas ahora, paso a paso.

### dmesg: leyendo el diario del kernel

Imagina FreeBSD como un piloto que pone en marcha un avión. A medida que el sistema arranca, el kernel comprueba su hardware: CPUs, memoria, discos, dispositivos USB, y cada driver comunica su estado. Esos mensajes no se pierden; se almacenan en un buffer que puedes leer en cualquier momento con:

```sh
% dmesg | less
```

Verás líneas como:

```yaml
Copyright (c) 1992-2023 The FreeBSD Project.
Copyright (c) 1979, 1980, 1983, 1986, 1988, 1989, 1991, 1992, 1993, 1994
        The Regents of the University of California. All rights reserved.
FreeBSD is a registered trademark of The FreeBSD Foundation.
FreeBSD 14.3-RELEASE releng/14.3-n271432-8c9ce319fef7 GENERIC amd64
FreeBSD clang version 19.1.7 (https://github.com/llvm/llvm-project.git llvmorg-19.1.7-0-gcd708029e0b2)
VT(vga): text 80x25
CPU: AMD Ryzen 7 5800U with Radeon Graphics          (1896.45-MHz K8-class CPU)
  Origin="AuthenticAMD"  Id=0xa50f00  Family=0x19  Model=0x50  Stepping=0
  Features=0x1783fbff<FPU,VME,DE,PSE,TSC,MSR,PAE,MCE,CX8,APIC,SEP,MTRR,PGE,MCA,CMOV,PAT,PSE36,MMX,FXSR,SSE,SSE2,HTT>
  Features2=0xfff83203<SSE3,PCLMULQDQ,SSSE3,FMA,CX16,SSE4.1,SSE4.2,x2APIC,MOVBE,POPCNT,TSCDLT,AESNI,XSAVE,OSXSAVE,AVX,F16C,RDRAND,HV>
  AMD Features=0x2e500800<SYSCALL,NX,MMX+,FFXSR,Page1GB,RDTSCP,LM>
  AMD Features2=0x8003f7<LAHF,CMP,SVM,CR8,ABM,SSE4A,MAS,Prefetch,OSVW,PCXC>
  Structured Extended Features=0x219c07ab<FSGSBASE,TSCADJ,BMI1,AVX2,SMEP,BMI2,ERMS,INVPCID,RDSEED,ADX,SMAP,CLFLUSHOPT,CLWB,SHA>
  Structured Extended Features2=0x40061c<UMIP,PKU,OSPKE,VAES,VPCLMULQDQ,RDPID>
  Structured Extended Features3=0xac000010<FSRM,IBPB,STIBP,ARCH_CAP,SSBD>
  XSAVE Features=0xf<XSAVEOPT,XSAVEC,XINUSE,XSAVES>
  IA32_ARCH_CAPS=0xc000069<RDCL_NO,SKIP_L1DFL_VME,MDS_NO>
  AMD Extended Feature Extensions ID EBX=0x1302d205<CLZERO,XSaveErPtr,WBNOINVD,IBPB,IBRS,STIBP,STIBP_ALWAYSON,SSBD,VIRT_SSBD,PSFD>
  SVM: NP,NRIP,VClean,AFlush,NAsids=16
  ...
  ...
```

El kernel te está diciendo:

- **qué hardware encontró**,
- **qué driver lo reclamó**,
- y a veces, **qué salió mal**.

Más adelante en este libro, cuando cargues tu propio driver, `dmesg` será el lugar donde buscarás tu primer mensaje "Hello, kernel!".

La salida de `dmesg` puede ser muy larga; puedes usar `grep` para filtrar y ver solo lo que necesitas, por ejemplo:

```sh
% dmesg | grep ada
```

Esto mostrará solo los mensajes sobre los dispositivos de disco (`ada0`, `ada1`).

### sysctl: el panel de control del kernel

Si `dmesg` es el diario, `sysctl` es el **panel de mandos lleno de controles y medidores**. Expone miles de variables del kernel en tiempo de ejecución: algunas de solo lectura (información del sistema), otras ajustables (comportamiento del sistema).

Prueba estos comandos:

```console
% sysctl kern.ostype
% sysctl kern.osrelease
% sysctl hw.model
% sysctl hw.ncpu
```

La salida podría parecerse a:

```text
kern.ostype: FreeBSD
kern.osrelease: 14.3-RELEASE
hw.model: AMD Ryzen 7 5800U with Radeon Graphics
hw.ncpu: 8
```

Aquí le acabas de preguntar al kernel:

- ¿Qué sistema operativo estoy ejecutando?
- ¿Qué versión?
- ¿Qué CPU?
- ¿Cuántos núcleos?

#### Explorando todo

Para ver todos los parámetros que puedes ajustar con `sysctl`, puedes ejecutar el siguiente comando:

```sh
% sysctl -a | less
```

Esto imprime el **panel de control completo**: miles de valores. Están organizados por categorías:

- `kern.*` - propiedades y configuración del kernel.
- `hw.*` - información del hardware.
- `net.*` - detalles de la pila de red.
- `vfs.*` - configuración del sistema de archivos.
- `debug.*` - variables de depuración (a menudo útiles para desarrolladores).

Puede resultar abrumador al principio, pero no te preocupes: aprenderás a encontrar lo que importa.

#### Modificando valores

Algunos sysctls son modificables. Por ejemplo:

```sh
% sudo sysctl kern.hostname=myfreebsd
% hostname
```

Acabas de cambiar el nombre de host en tiempo de ejecución.

Importante: los cambios realizados de esta manera desaparecen tras el reinicio a menos que se guarden en `/etc/sysctl.conf`.

### /dev: donde los dispositivos cobran vida

Ahora llegamos a la parte más emocionante.

FreeBSD representa los dispositivos como **archivos especiales** dentro de `/dev`. Esta es una de las ideas más elegantes de UNIX:

> Si todo es un archivo, entonces todo puede accederse de manera coherente.

Ejecuta:

```sh
% ls -d /dev/* | less
```

Verás un mar de nombres:

- `/dev/null` - el "agujero negro" donde los datos van a desaparecer.
- `/dev/zero` - un flujo infinito de ceros.
- `/dev/random` - números aleatorios criptográficamente seguros.
- `/dev/tty` - tu terminal.
- `/dev/ada0` - tu disco SATA.
- `/dev/da0` - un disco USB.

Prueba a interactuar:

```sh
echo "Testing" > /dev/null         # silently discards output
head -c 16 /dev/zero | hexdump     # shows zeros in hex
head -c 16 /dev/random | hexdump   # random bytes from the kernel
```

Más adelante, cuando crees tu primer driver, aparecerá aquí como un archivo llamado `/dev/hello`. Leer o escribir en ese archivo activará **tu código del kernel**. Ese será el momento en que sentirás el puente entre el userland y el kernel.

### Laboratorio práctico: tu primera mirada al interior

1. Visualiza todos los mensajes del kernel:

	```sh
   % dmesg | less
	```

2. Encuentra tus dispositivos de almacenamiento:

   ```sh
   % dmesg | grep ada
   ```

3. Pregunta al kernel sobre tu CPU:

   ```sh
   % sysctl hw.model
   % sysctl hw.ncpu
   ```

4. Cambia tu nombre de host temporalmente:

   ```sh
   % sudo sysctl kern.hostname=mytesthost
   % hostname
   ```

5. Interactúa con archivos de dispositivo especiales:

   ```
   % echo "Hello FreeBSD" > /dev/null
   % head -c 8 /dev/zero | hexdump
   % head -c 8 /dev/random | hexdump
   ```

Con este breve laboratorio, ya has leído mensajes del kernel, consultado variables del kernel y tocado nodos de dispositivo, exactamente lo que hacen los desarrolladores profesionales a diario.

### Del shell al hardware: la perspectiva global

Para entender por qué herramientas como `dmesg`, `sysctl` y `/dev` son tan útiles, conviene imaginar cómo está estructurado FreeBSD en capas:

```text
+----------------+
|   User Space   |  ← Commands you run: ls, ps, pkg, scripts
+----------------+
        ↓
+----------------+
|   Shell (sh)   |  ← Interprets your commands into syscalls
+----------------+
        ↓
+----------------+
|    Kernel      |  ← Handles processes, memory, devices, filesystems
+----------------+
        ↓
+----------------+
|   Hardware     |  ← CPU, RAM, disks, USB, network cards
+----------------+
```

Cada vez que escribes un comando en el shell, este recorre la pila de arriba abajo:

- El **shell** lo interpreta.
- El **kernel** lo ejecuta gestionando procesos, memoria y dispositivos.
- El **hardware** responde.

Luego los resultados suben de vuelta para que los veas.

Comprender este flujo es fundamental para los desarrolladores de drivers: cuando interactúas con `/dev`, te estás conectando directamente al kernel, que a su vez se comunica con el hardware.

### Errores frecuentes de principiante

Explorar el kernel puede ser emocionante, pero aquí tienes algunos errores frecuentes que debes tener en cuenta:

1. **Confundir `dmesg` con los registros del sistema**

   - `dmesg` solo muestra el ring buffer del kernel, no todos los registros.
   - Los mensajes antiguos pueden desaparecer cuando los nuevos los desplazan.
   - Para ver los registros completos, consulta `/var/log/messages`.

2. **Olvidar que los cambios de `sysctl` no persisten**

   - Si cambias un parámetro con `sysctl`, se restablece al reiniciar.

   - Para hacerlo permanente, añádelo a `/etc/sysctl.conf`.

   - Ejemplo:

   ```sh
     % echo 'kern.hostname="myhost"' | sudo tee -a /etc/sysctl.conf
   ```

3. **Sobreescribir archivos en `/dev`**

   - Las entradas de `/dev` no son archivos normales: son conexiones en vivo al kernel.
   - Redirigir la salida hacia ellas puede tener efectos reales.
   - Escribir en `/dev/null` es seguro, pero escribir datos aleatorios en `/dev/ada0` (tu disco) podría destruirlo.
   - Regla general: explora `/dev/null`, `/dev/zero`, `/dev/random` y `/dev/tty`, pero deja en paz los dispositivos de almacenamiento (`ada0`, `da0`) a menos que sepas exactamente lo que estás haciendo.

4. **Esperar que las entradas de `/dev` no cambien**

   - Los dispositivos aparecen y desaparecen a medida que se añade o elimina hardware.
   - Por ejemplo, conectar una memoria USB puede crear `/dev/da0`.
   - No escribas los nombres de dispositivo directamente en los scripts sin verificarlos antes.

5. **No usar rutas completas en la automatización**

   - El cron y otras herramientas automatizadas pueden no tener el mismo `PATH` que tu shell.
   - Usa siempre rutas completas (`/sbin/sysctl`, `/bin/echo`) cuando escribas scripts de interacciones con el kernel.

### Cerrando

En esta sección, abriste tres ventanas mágicas al kernel de FreeBSD:

- `dmesg`: el diario del sistema, que registra la detección de hardware y los mensajes de los drivers.
- `sysctl`: el panel de control que revela (y a veces ajusta) la configuración del kernel.
- `/dev`: el lugar donde los dispositivos cobran vida como archivos.

La **perspectiva global** que debes recordar es esta: cada vez que escribes un comando, este recorre el shell hacia abajo hasta el kernel y, finalmente, llega al hardware. Los resultados suben de vuelta para que los veas. Herramientas como `dmesg`, `sysctl` y `/dev` te permiten asomarte a ese flujo y ver lo que el kernel está haciendo entre bastidores.

No son solo herramientas abstractas: son exactamente el modo en que verás tu **propio driver** aparecer en el sistema. Cuando cargues tu módulo, verás `dmesg` cobrar vida, puede que expongas un parámetro con `sysctl`, y interactuarás con tu nodo de dispositivo bajo `/dev`.

Vale la pena detenerse un momento para pensar en lo que esto revela sobre el camino que tienes por delante. Cada línea de `dmesg` que describe el attach de hardware, cada nombre de `sysctl` que empieza con `kern.` o `vm.`, y cada archivo bajo `/dev` es la cara visible del código del kernel escrito en C. Cuando ejecutaste `dmesg`, estabas leyendo cadenas que un driver pasó a `device_printf` o `printf` durante el attach. Cuando recorriste `sysctl -a`, estabas atravesando un árbol que los drivers y subsistemas rellenan con `SYSCTL_INT`, `SYSCTL_ULONG` y macros relacionados. Cuando abriste `/dev/null`, el kernel despachó tu `read` o `write` a un driver cuya estructura conocerás en el capítulo 6. Has estado observando las salidas del código de drivers todo este tiempo; los dos próximos capítulos te enseñan a escribir las entradas.

El capítulo 5 te introduce en el **lenguaje C tal como lo usa realmente el kernel**: tipos enteros de ancho fijo, gestión de memoria explícita con `malloc(9)` y `free(9)`, disciplina de punteros en contexto de interrupción, y el subconjunto de C que el Kernel Normal Form (KNF) de FreeBSD considera idiomático. No es «solo C otra vez». El kernel no puede llamar a `printf` de libc, no puede asignar memoria con el `malloc` normal, y no puede asumir las habituales redes de seguridad del userland. El capítulo 5 te muestra qué cambia y por qué, para que el código que escribas después compile, se cargue y se comporte de manera predecible.

El capítulo 6 toma entonces el C que acabas de reaprender y lo ensambla en un primer driver, recorriendo paso a paso la **anatomía de un driver de FreeBSD**: la estructura softc, la tabla de métodos Newbus, el macro `DRIVER_MODULE`, y el camino que un `read` o `write` recorre desde tu nodo de dispositivo `/dev/foo` hasta la rutina del driver que lo atiende. Al final del capítulo 6 habrás cargado tu propio módulo, lo habrás visto anunciarse en `dmesg`, y habrás usado los mismos comandos que practicaste en este capítulo para confirmar que funciona.

Antes de continuar y empezar a aprender sobre programación en C, hagamos una pausa para consolidar todo lo que has aprendido en este capítulo. En la siguiente sección, repasaremos las ideas clave y te daremos un conjunto de desafíos para practicar, ejercicios que te ayudarán a afianzar estas nuevas habilidades y a prepararte para el trabajo que viene.

## Cerrando

¡Enhorabuena! Acabas de completar tu **primera visita guiada por UNIX y FreeBSD**. Lo que empezó como ideas abstractas se está convirtiendo en habilidades prácticas. Ya puedes moverte por el sistema, gestionar archivos, editar e instalar software, controlar procesos, automatizar tareas e incluso asomarte al funcionamiento interno del kernel.

Vamos a tomarnos un momento para repasar lo que has conseguido en este capítulo:

- **Qué es UNIX y por qué importa**: una filosofía de simplicidad, modularidad y "todo es un archivo", heredada por FreeBSD.
- **El shell**: tu ventana al sistema, donde los comandos siguen la estructura coherente de `command [options] [arguments]`.
- **La estructura del sistema de archivos**: una jerarquía única que comienza en `/`, con roles especiales para directorios como `/etc`, `/usr/local`, `/var` y `/dev`.
- **Usuarios, grupos y permisos**: el fundamento del modelo de seguridad de FreeBSD, que controla quién puede leer, escribir o ejecutar.
- **Procesos**: programas en ejecución, con herramientas como `ps`, `top` y `kill` para gestionarlos.
- **Instalación de software**: usando `pkg` para instalaciones binarias rápidas, y la **Ports Collection** para la flexibilidad basada en código fuente.
- **Automatización**: programación de tareas con `cron`, trabajos puntuales con `at`, y mantenimiento con `periodic`.
- **Scripts de shell**: convirtiendo comandos repetitivos en programas reutilizables usando el `/bin/sh` nativo de FreeBSD.
- **Una mirada al interior del kernel**: usando `dmesg`, `sysctl` y `/dev` para observar el sistema a un nivel más profundo.

Eso es mucho, pero no te preocupes si aún no te sientes un experto. El objetivo de este capítulo no era la perfección, sino la **familiaridad**: familiaridad con el shell, familiaridad con la exploración de FreeBSD, y familiaridad con la forma en que UNIX funciona por dentro. Esa familiaridad nos acompañará cuando empecemos a escribir código real para el sistema.

### Zona de práctica

Si quieres una forma práctica de reforzar lo que acabas de leer, las páginas siguientes recopilan **46 ejercicios opcionales**. Ninguno de ellos es obligatorio para continuar con el libro, así que trátalo como material adicional: elige los que cubran áreas donde aún te sientas inseguro, salta los que te parezcan redundantes, y vuelve al resto más adelante si te resultan útiles.

Están agrupados por tema, así que puedes practicar sección por sección o mezclarlos como prefieras.

### Sistema de archivos y navegación (8 ejercicios)

1. Usa `pwd` para confirmar tu directorio actual; luego entra en `/etc` y regresa a tu directorio personal usando `cd`.
2. Crea un directorio `unix_playground` en tu directorio personal. Dentro de él, crea tres subdirectorios: `docs`, `code` y `tmp`.
3. Dentro de `unix_playground/docs`, crea un archivo llamado `readme.txt` con el texto "Welcome to FreeBSD". Usa `echo` y redirección de salida.
4. Copia `readme.txt` en el directorio `tmp`. Verifica que ambos archivos existen con `ls -l`.
5. Renombra el archivo que está en `tmp` a `copy.txt`. Luego elimínalo con `rm`.
6. Usa `find` para localizar todos los archivos `.conf` dentro de `/etc`.
7. Usa rutas absolutas para copiar `/etc/hosts` en tu directorio `docs`. Luego usa rutas relativas para moverlo a `tmp`.
8. Usa `ls -lh` para mostrar los tamaños de archivo en formato legible. ¿Cuál es el archivo más grande dentro de `/etc`?

### Usuarios, grupos y permisos (6 ejercicios)

1. Crea un archivo llamado `secret.txt` en tu directorio personal. Hazlo legible solo por ti.
2. Crea un directorio `shared` y da acceso de lectura y escritura a todos (modo 777). Compruébalo escribiendo un archivo dentro.
3. Usa `id` para listar el UID, GID y grupos de tu usuario.
4. Usa `ls -l` sobre `/etc/passwd` y `/etc/master.passwd`. Compara sus permisos y explica por qué difieren.
5. Crea un archivo y cambia su propietario a `root` usando `sudo chown`. Intenta editarlo como usuario normal. ¿Qué ocurre?
6. Añade un nuevo usuario con `sudo adduser`. Establece una contraseña, inicia sesión con ese usuario y comprueba su directorio personal por defecto.

### Procesos y monitorización del sistema (7 ejercicios)

1. Inicia un proceso en primer plano con `sleep 60`. Mientras se ejecuta, abre otro terminal y usa `ps` para encontrarlo.
2. Inicia el mismo proceso en segundo plano con `sleep 60 &`. Usa `jobs` y `fg` para devolverlo al primer plano.
3. Usa `top` para encontrar qué proceso consume más CPU en ese momento.
4. Inicia un proceso `yes` (`yes > /dev/null &`) para saturar la CPU. Obsérvalo en `top` y luego deténlo con `kill`.
5. Comprueba cuánto tiempo lleva funcionando tu sistema con `uptime`.
6. Usa `df -h` para ver cuánto espacio en disco hay disponible en tu sistema. ¿Qué sistema de archivos está montado en `/`?
7. Ejecuta `sysctl vm.stats.vm.v_page_count` para ver el número de páginas de memoria en tu sistema.

### Instalación y gestión de software (pkg y Ports) (6 ejercicios)

1. Usa `pkg search` para buscar un editor de texto distinto de `nano`. Instálalo, ejecútalo y luego elimínalo.
2. Instala el paquete `htop` con `pkg`. Compara su salida con la del `top` incluido en el sistema base.
3. Explora la Ports Collection navegando hasta `/usr/ports/editors/nano`. Examina el Makefile.
4. Construye `nano` desde ports con `sudo make install clean`. ¿Te preguntó sobre opciones?
5. Actualiza tu árbol de ports usando `git`. ¿Qué categorías se actualizaron?
6. Usa `which` para localizar dónde se instaló el binario de `nano` o `htop`. Comprueba si está en `/usr/bin` o en `/usr/local/bin`.

### Automatización y programación de tareas (cron, at, periodic) (6 ejercicios)

1. Escribe una tarea cron que registre la fecha y la hora actuales cada 2 minutos en `~/time.log`. Espera y compruébalo con `tail`.
2. Escribe una tarea cron que elimine todos los archivos `.tmp` de tu directorio personal cada noche a medianoche.
3. Usa el comando `at` para programar un mensaje para ti mismo dentro de 5 minutos.
4. Ejecuta `sudo periodic daily` y lee su salida. ¿Qué tipo de tareas realiza?
5. Añade una tarea cron que ejecute `df -h` todos los días a las 8:00 y registre el resultado en `~/disk.log`.
6. Redirige la salida de una tarea cron a un archivo de log personalizado (`~/cron_output.log`). Confirma que se capturan tanto la salida normal como los errores.

### Programación en shell (/bin/sh) (7 ejercicios)

1. Escribe un script `hello_user.sh` que imprima tu nombre de usuario, la fecha actual y el número de procesos en ejecución. Hazlo ejecutable y ejecútalo.
2. Escribe un script `organize.sh` que mueva todos los archivos `.txt` de tu directorio personal a una carpeta llamada `texts`. Añade comentarios para explicar cada paso.
3. Modifica `organize.sh` para que también cree subdirectorios por tipo de archivo (`images`, `docs`, `archives`).
4. Escribe un script `disk_alert.sh` que te avise si el uso del sistema de archivos raíz supera el 80%.
5. Escribe un script `logger.sh` que añada una entrada con marca de tiempo a `~/activity.log` con el directorio actual y el usuario.
6. Escribe un script `backup.sh` que cree un archivo `.tar.gz` de `~/unix_playground` en `~/backups/`.
7. Amplía `backup.sh` para que conserve solo las últimas 5 copias de seguridad y elimine las más antiguas automáticamente.

### Una mirada al kernel (dmesg, sysctl, /dev) (6 ejercicios)

1. Usa `dmesg` para encontrar el modelo de tu disco principal.
2. Usa `sysctl hw.model` para mostrar el modelo de tu CPU y `sysctl hw.ncpu` para mostrar cuántos núcleos tienes.
3. Cambia tu hostname temporalmente usando `sysctl kern.hostname=mytesthost`. Compruébalo con `hostname`.
4. Usa `ls /dev` para listar los nodos de dispositivo. Identifica cuáles representan discos, terminales y dispositivos virtuales.
5. Usa `head -c 16 /dev/random | hexdump` para leer 16 bytes aleatorios del kernel.
6. Conecta una memoria USB (si dispones de una) y ejecuta `dmesg | tail`. ¿Puedes ver qué nueva entrada en `/dev/` apareció?

### Cerrando el capítulo

Con estos **46 ejercicios**, has recorrido todos los temas principales de este capítulo:

- Navegación por el sistema de archivos y su estructura
- Usuarios, grupos y permisos
- Procesos y monitorización
- Instalación de software con pkg y ports
- Automatización con cron, at y periodic
- Programación en shell con el `/bin/sh` nativo de FreeBSD
- Introspección del kernel con dmesg, sysctl y /dev

Al completarlos, dejarás de ser un *lector pasivo* para convertirte en un **practicante activo de UNIX**. No solo sabrás cómo funciona FreeBSD, sino que habrás *vivido dentro de él*.

Estos ejercicios son la **memoria muscular** que necesitarás cuando empecemos a programar. Cuando lleguemos a C y más adelante al desarrollo del kernel, ya dominarás con soltura las herramientas cotidianas de un desarrollador UNIX.

### Lo que viene a continuación

El próximo capítulo te presentará el **lenguaje de programación C**, el lenguaje del kernel de FreeBSD. Es la herramienta que utilizarás para crear drivers de dispositivo. No te preocupes si nunca has programado antes: construiremos tu comprensión paso a paso, igual que hemos hecho con UNIX en este capítulo.

Combinando tu nueva fluidez en UNIX con las habilidades de programación en C, estarás listo para empezar a dar forma al propio kernel de FreeBSD.
