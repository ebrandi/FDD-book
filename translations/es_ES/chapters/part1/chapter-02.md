---
title: "Preparación de tu laboratorio"
description: "Este capítulo te guía a través de la configuración de un laboratorio FreeBSD seguro y listo para el desarrollo de drivers."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 2
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 60
language: "es-ES"
---
# Configuración de tu entorno de laboratorio

Antes de poder empezar a escribir código o explorar los componentes internos de FreeBSD, necesitamos un lugar donde sea seguro experimentar, cometer errores y aprender. Ese lugar es tu **entorno de laboratorio**. En este capítulo crearemos la base que utilizarás a lo largo del resto del libro: un sistema FreeBSD configurado para el desarrollo de drivers.

Piensa en este capítulo como la preparación de tu **taller**. Del mismo modo que un carpintero necesita el banco adecuado, las herramientas correctas y el equipo de seguridad necesario antes de construir un mueble, tú necesitas una instalación de FreeBSD fiable, las utilidades de desarrollo apropiadas y una forma de recuperarte rápidamente cuando las cosas salgan mal. La programación del kernel es implacable; un pequeño error en tu driver puede bloquear todo el sistema. Tener un laboratorio dedicado convierte esos bloqueos en parte del proceso de aprendizaje, no en catástrofes.

Al terminar este capítulo serás capaz de:

- Comprender la importancia de aislar tus experimentos de tu ordenador principal.
- Elegir entre usar una máquina virtual o una instalación en hardware real.
- Instalar FreeBSD 14.3 paso a paso.
- Configurar tu sistema con las herramientas y el código fuente necesarios para el desarrollo de drivers.
- Aprender a tomar snapshots, gestionar copias de seguridad y usar control de versiones para que tu progreso nunca se pierda.

A lo largo del camino haremos pausas para **laboratorios prácticos**, de modo que no te limites a leer sobre la configuración, sino que realmente la lleves a cabo. Al terminar, tendrás un laboratorio FreeBSD seguro, reproducible y listo para todo lo que construiremos juntos en los próximos capítulos.

### Guía para el lector: cómo usar este capítulo

Este capítulo es más práctico que teórico. Piensa en él como un manual paso a paso para preparar tu laboratorio FreeBSD antes de comenzar los experimentos de verdad. Se te pedirá que tomes decisiones (máquina virtual o hardware real), que sigas los pasos de instalación y que configures tu sistema FreeBSD.

La mejor manera de usar este capítulo es **seguir los pasos a medida que los lees**. No te limites a hojear el texto; instala FreeBSD realmente, toma los snapshots, anota tus decisiones en tu cuaderno de laboratorio y prueba los ejercicios. Cada sección se apoya en la anterior, así que al final tendrás un entorno completo que coincide con los ejemplos del resto del libro.

Si ya sabes cómo instalar y configurar FreeBSD, puedes hojear o saltarte partes de este capítulo, pero no te saltes los laboratorios; garantizan que tu configuración coincide con la que usaremos a lo largo del libro.

Sobre todo, recuerda: los errores aquí no son fracasos, son parte del proceso. Este es tu lugar seguro para experimentar y aprender.

**Tiempo estimado para completar este capítulo:** entre 1 y 2 horas, según elijas máquina virtual o instalación en hardware real, y dependiendo de si ya tienes experiencia instalando sistemas operativos.

## Por qué es importante un entorno de laboratorio

Antes de empezar a escribir comandos y nuestros primeros fragmentos de código, debemos detenernos un momento y pensar en *dónde* vamos a realizar todo este trabajo. La programación del kernel y el desarrollo de drivers de dispositivo no son como escribir un simple script o una página web. Cuando experimentas con el kernel, estás experimentando con el **corazón del sistema operativo**. Un pequeño error en tu código puede hacer que tu máquina se congele, se reinicie de forma inesperada o incluso corrompa datos si no tienes cuidado.

Esto no significa que el desarrollo de drivers sea peligroso; significa que debemos tomar precauciones y preparar un **entorno seguro** donde los errores sean esperados, recuperables e incluso alentados como parte del proceso de aprendizaje. Ese entorno es lo que llamaremos tu **laboratorio**.

Tu laboratorio debe ser un **entorno dedicado y aislado**. Del mismo modo que un químico no realizaría un experimento en la mesa del comedor familiar sin equipo de protección, tú no deberías ejecutar código del kernel sin terminar en el mismo ordenador donde guardas tus fotos personales, documentos de trabajo o proyectos importantes del colegio. Necesitas un espacio diseñado para la exploración y el fallo, porque el fallo es la forma en que aprenderás.

### ¿Por qué no usar tu ordenador principal?

Es tentador pensar: *«Ya tengo un ordenador con FreeBSD (o Linux, o Windows), ¿por qué no usarlo directamente?»* La respuesta corta: porque tu ordenador principal es para el trabajo, no para experimentos. Si accidentalmente provocas un kernel panic mientras pruebas tu driver, no querrás perder trabajo sin guardar, interrumpir tu conexión de red en mitad de una reunión en línea, ni siquiera dañar un sistema de archivos con datos corruptos.

La configuración de tu laboratorio te da libertad: puedes romper cosas, reiniciar y recuperarte en minutos sin estrés. Esta libertad es esencial para aprender.

### Las máquinas virtuales: el mejor aliado del principiante

La mayoría de los principiantes (e incluso muchos desarrolladores experimentados) empiezan con **máquinas virtuales (VM)**. Una VM es como un ordenador en caja de arena que se ejecuta dentro de tu ordenador real. Se comporta exactamente igual que una máquina física, pero si algo sale mal puedes reiniciarla, tomar un snapshot o reinstalar FreeBSD en minutos. No necesitas un portátil o servidor de repuesto para empezar a desarrollar drivers; tu ordenador actual puede alojar tu laboratorio.

Entraremos en más detalle sobre la virtualización en la siguiente sección, pero aquí tienes lo más destacado:

- **Experimentos seguros**: si tu driver bloquea el kernel, solo cae la VM, no tu ordenador anfitrión.
- **Recuperación sencilla**: los snapshots te permiten guardar el estado de la VM y volver atrás al instante si rompes algo.
- **Sin coste**: no necesitas hardware dedicado.
- **Portátil**: puedes mover tu VM entre ordenadores.

### Hardware real: cuando necesitas la realidad

Hay momentos en los que solo el hardware real sirve; por ejemplo, si quieres desarrollar un driver para una tarjeta PCIe o un dispositivo USB que requiere acceso directo al bus de la máquina. En esos casos, las pruebas en una VM pueden no ser suficientes porque no todas las soluciones de virtualización pueden transferir el hardware de forma fiable.

Si tienes un PC de repuesto, instalar FreeBSD en él te dará el entorno más cercano a la realidad para las pruebas. Pero recuerda: las configuraciones en hardware real no tienen la misma red de seguridad que las VM. Si bloqueas el kernel, tu máquina se reiniciará y tendrás que recuperarte manualmente. Por eso recomiendo empezar en una VM, aunque eventualmente pases al hardware real para proyectos de hardware específicos.

### Ejemplo del mundo real

Para hacerlo más concreto: imagina que estás escribiendo un driver simple que desreferencia accidentalmente un puntero NULL (no te preocupes si eso suena técnico ahora, lo aprenderás todo más adelante). En una VM, tu sistema podría congelarse, pero con un reinicio y una restauración de snapshot vuelves a estar en marcha en minutos. En hardware real, el mismo error podría causar corrupción del sistema de archivos, lo que requeriría un largo proceso de recuperación. Por eso es tan valioso un entorno de laboratorio seguro.

### Laboratorio práctico: preparando tu mentalidad de laboratorio

Antes de instalar siquiera FreeBSD, hagamos un ejercicio sencillo para adoptar la mentalidad correcta.

1. Coge un cuaderno (físico o digital). Este será tu **cuaderno de laboratorio**.
2. Anota:
   - La fecha de hoy
   - La máquina que vas a usar para tu laboratorio FreeBSD (VM o física)
   - Por qué elegiste esa opción (seguridad, comodidad, acceso a hardware real, etc.)
3. Escribe una primera entrada: *«Configuración del laboratorio iniciada. Objetivo: construir un entorno seguro para experimentos con drivers FreeBSD.»*

Puede parecer innecesario, pero mantener un **registro de laboratorio** te ayudará a seguir tu progreso, repetir configuraciones exitosas más adelante y depurar cuando algo salga mal. En el desarrollo profesional de drivers, los ingenieros toman notas muy detalladas; empezar este hábito ahora te hará pensar y trabajar como un verdadero desarrollador de sistemas.

### Resumen

Hemos introducido la idea de tu **entorno de laboratorio** y por qué es tan importante para el desarrollo seguro de drivers. Tanto si eliges una máquina virtual como un ordenador físico de repuesto, la clave es tener un lugar dedicado donde sea válido cometer errores.

En la siguiente sección estudiaremos más de cerca los **pros y contras de las máquinas virtuales frente al hardware real**. Al final de esa sección sabrás exactamente qué configuración tiene más sentido para ti como punto de partida en FreeBSD.

## Elegir tu configuración: máquina virtual o hardware real

Ahora que entiendes por qué es importante un entorno de laboratorio dedicado, la siguiente pregunta es: **¿dónde deberías construirlo?** FreeBSD puede ejecutarse de dos formas principales para tus experimentos:

1. **Dentro de una máquina virtual (VM)**, ejecutándose sobre tu sistema operativo existente.
2. **Directamente en hardware físico** (lo que suele llamarse *bare metal*).

Ambas opciones funcionan y ambas se usan ampliamente en el desarrollo real de FreeBSD. La elección correcta depende de tus objetivos, tu hardware y tu nivel de comodidad. Comparémoslas lado a lado.

### Máquinas virtuales: tu caja de arena en una caja

Una **máquina virtual** es un software que te permite ejecutar FreeBSD como si fuera un ordenador separado, pero dentro de tu ordenador existente. Las soluciones de VM más populares son:

- **VirtualBox** (gratuito y multiplataforma, ideal para principiantes).
- **VMware Workstation / Fusion** (comercial, pulido, muy utilizado).
- **bhyve** (el hipervisor nativo de FreeBSD, ideal si quieres ejecutar FreeBSD *sobre* FreeBSD).

Por qué los desarrolladores prefieren las VM para el trabajo con el kernel:

- **Los snapshots salvan el día**: antes de probar código arriesgado, tomas un snapshot. Si el sistema entra en pánico o se rompe, restauras en segundos.
- **Varios laboratorios en una sola máquina**: puedes crear varias instancias de FreeBSD, cada una para un proyecto diferente.
- **Fácil de compartir**: puedes exportar una imagen de VM y compartirla con un compañero.

**Cuándo preferir las VM:**

- Estás empezando y quieres el entorno más seguro posible.
- No tienes hardware de repuesto que dedicar.
- Esperas bloquear el kernel con frecuencia mientras aprendes (y lo harás).

### Hardware real: la situación auténtica

Ejecutar FreeBSD **directamente en hardware** es lo más parecido a «la realidad». Esto significa que FreeBSD arranca como único sistema operativo en la máquina, comunicándose directamente con la CPU, la memoria, el almacenamiento y los periféricos.

Ventajas:

- **Pruebas con hardware real**: esencial cuando se desarrollan drivers para PCIe, USB u otros dispositivos físicos.
- **Rendimiento**: sin sobrecarga de VM. Tienes acceso completo a los recursos de tu sistema.
- **Precisión**: algunos errores solo aparecen en hardware real, especialmente los relacionados con el tiempo.

Desventajas:

- **Sin red de seguridad**: si el kernel se bloquea, toda tu máquina cae.
- **La recuperación lleva tiempo**: si corrompes el sistema operativo, puede que tengas que reinstalar FreeBSD.
- **Se necesita hardware dedicado**: necesitarás un PC o portátil de repuesto que puedas dedicar completamente a los experimentos.

**Cuándo preferir el hardware real:**

- Planeas desarrollar un driver para hardware que no funciona bien en VM.
- Ya tienes una máquina de repuesto que puedes dedicar completamente a FreeBSD.
- Quieres el máximo realismo, aunque eso implique más riesgo.

### Estrategia híbrida

Muchos desarrolladores profesionales usan **ambas opciones**. Realizan la mayor parte de su experimentación y prototipado en una VM, donde es seguro y rápido, y solo pasan al hardware real cuando su driver es lo bastante estable como para probarlo con hardware físico. No tienes que comprometerte con una para siempre; puedes empezar con una VM hoy y añadir una máquina en hardware real más adelante si la necesitas.

### Tabla comparativa rápida

| Característica          | Máquina virtual                    | Hardware real                        |
| ----------------------- | ---------------------------------- | ------------------------------------ |
| **Seguridad**           | Muy alta (snapshots, rollback)     | Baja (recuperación manual necesaria) |
| **Rendimiento**         | Ligeramente inferior (sobrecarga)  | Rendimiento completo del sistema     |
| **Acceso al hardware**  | Limitado / dispositivos emulados   | Hardware real completo               |
| **Dificultad de setup** | Fácil y rápido                     | Moderada (instalación completa)      |
| **Coste**               | Ninguno (se ejecuta en tu PC)      | Requiere máquina dedicada            |
| **Ideal para**          | Principiantes, aprendizaje seguro  | Pruebas avanzadas de hardware        |

### Laboratorio práctico: decidiendo tu camino

1. Fíjate en los recursos que tienes ahora mismo. ¿Tienes algún portátil o ordenador de sobremesa libre que puedas dedicar a experimentos con FreeBSD?
   - Si la respuesta es sí -> El bare metal es una opción para ti.
   - Si la respuesta es no -> Una VM es el punto de partida perfecto.
2. En tu **cuaderno de laboratorio**, anota:
   - Qué opción vas a utilizar (VM o bare metal).
   - Por qué la has elegido.
   - Cualquier limitación que preveas (por ejemplo, "Usando una VM, puede que aún no pueda probar el USB passthrough").
3. Si eliges VM, anota qué hipervisor vas a utilizar (VirtualBox, VMware, bhyve, etc.).

Esta decisión no te compromete para siempre. Siempre puedes añadir un segundo entorno más adelante. El objetivo ahora es empezar con una configuración segura y fiable.

### En resumen

Hemos comparado las máquinas virtuales y las configuraciones en bare metal, y hemos visto las ventajas y desventajas de cada opción. Para la mayoría de los principiantes, empezar con una VM es el mejor equilibrio entre seguridad, comodidad y flexibilidad. Si más adelante necesitas interactuar con hardware real, siempre puedes añadir un sistema bare metal a tu conjunto de herramientas.

En la siguiente sección nos pondremos manos a la obra con la **instalación de FreeBSD 14.3**, primero en una VM y luego cubriremos los puntos clave para las instalaciones en bare metal. Aquí es donde tu laboratorio empieza a tomar forma de verdad.

## Instalación de FreeBSD (VM y bare metal)

En este punto ya has decidido si vas a configurar FreeBSD en una **máquina virtual** o en **bare metal**. Ahora es el momento de instalar el sistema operativo que servirá de base para todos nuestros experimentos. Nos centraremos en **FreeBSD 14.3**, la última versión estable en el momento de escribir estas líneas, de modo que todo lo que hagas coincida con los ejemplos del libro.

El instalador de FreeBSD es de texto, pero no te dejes intimidar: es muy sencillo y en menos de 20 minutos tendrás un sistema funcionando y listo para el desarrollo.

### Descargar la ISO de FreeBSD

1. Visita la página oficial de descargas de FreeBSD:
    https://www.freebsd.org/where
2. Elige la imagen **14.3-RELEASE**.
   - Si vas a instalar en una VM, descarga la **ISO Disk1 de amd64** (`FreeBSD-14.3-RELEASE-amd64-disc1.iso`).
   - Si vas a instalar en hardware real, la misma ISO funciona, aunque también puedes considerar la **imagen memstick** si prefieres escribirla en una memoria USB.

### Instalación de FreeBSD en VirtualBox (paso a paso)

Si todavía no tienes **VirtualBox** instalado, tendrás que configurarlo antes de crear tu VM de FreeBSD. VirtualBox está disponible para Windows, macOS, Linux e incluso Solaris como sistema anfitrión. Descarga la última versión desde el sitio oficial:

https://www.virtualbox.org/wiki/Downloads

Elige el paquete que corresponda a tu sistema operativo anfitrión (por ejemplo, Windows hosts o macOS hosts), descárgalo y sigue el instalador. La instalación es muy sencilla y solo lleva unos minutos. Una vez finalizada, lanza VirtualBox y ya estarás listo para crear tu primera máquina virtual de FreeBSD.

Con todo preparado, veamos el proceso en VirtualBox, ya que es el punto de entrada más sencillo para la mayoría de los lectores. Los pasos son similares en VMware o bhyve.

Para empezar, ejecuta la aplicación VirtualBox en tu ordenador. En la pantalla principal, selecciona **Home** en la columna izquierda y luego haz clic en **New**. Sigue los pasos que se indican a continuación:

1. **Crea una nueva VM** en VirtualBox:

   - VM Name: `FreeBSD Lab`
   
   - VM Folder: Elige un directorio donde guardar tu VM de FreeBSD
   
   - ISO Image: Elige el archivo ISO de FreeBSD que descargaste antes
   
   - OS Edition: Déjalo en blanco
   
   - OS: Elige `BSD`
   
   - OS Distribution: Elige `FreeBSD`
   
   - OS Version: Elige `FreeBSD (64-bit)`
   
     Haz clic en **Next** para continuar

![image-20250823183742036](https://freebsd.edsonbrandi.com/images/image-20250823183742036.png)

2. **Asigna recursos**:

   - Base Memory: al menos 2 GB (se recomiendan 4 GB).

   - Number of CPUs: 2 o más si están disponibles.

   - Disk Size: 30 GB o más.
   
     Haz clic en **Next** para continuar

![image-20250823183937505](https://freebsd.edsonbrandi.com/images/image-20250823183937505.png)

3. **Revisa tus opciones**: Si estás conforme con el resumen, haz clic en **Finish** para crear la VM.

![image-20250823184925505](https://freebsd.edsonbrandi.com/images/image-20250823184925505.png)

4. **Inicia la máquina virtual**: Haz clic en el botón verde **Start**. Al arrancar la VM, utilizará el disco de instalación de FreeBSD que especificaste al crearla.

![image-20250823185259010](https://freebsd.edsonbrandi.com/images/image-20250823185259010.png)

5. **Arranca la VM**: La VM mostrará el gestor de arranque de FreeBSD; pulsa **1** para proseguir con el arranque de FreeBSD.

![image-20250823185756980](https://freebsd.edsonbrandi.com/images/image-20250823185756980.png)

6. **Ejecuta el instalador**: Durante el arranque, el instalador se ejecutará automáticamente. Elige **[ Install ]** para continuar.

![image-20250823190016799](https://freebsd.edsonbrandi.com/images/image-20250823190016799.png)

7. **Distribución del teclado**: Elige tu idioma o distribución de teclado preferida; el valor predeterminado es la distribución US. Pulsa **Enter** para continuar.

![image-20250823190046619](https://freebsd.edsonbrandi.com/images/image-20250823190046619.png)

8. **Nombre de host**: Escribe el nombre de host para tu laboratorio; en el ejemplo he elegido `fbsd-lab`. Pulsa **Enter** para continuar.

![image-20250823190129010](https://freebsd.edsonbrandi.com/images/image-20250823190129010.png)

9. **Selección de distribución**: Deja los valores predeterminados (sistema base, kernel). Pulsa **Enter** para continuar.

![image-20250823190234155](https://freebsd.edsonbrandi.com/images/image-20250823190234155.png)

10. **Particionado**: Elige *Auto (UFS)* salvo que quieras aprender ZFS más adelante. Pulsa **Enter** para continuar.

![image-20250823190350815](https://freebsd.edsonbrandi.com/images/image-20250823190350815.png)

11. **Partición**: Elige **[ Entire Disk ]**. Pulsa **Enter** para continuar.

![image-20250823190450571](https://freebsd.edsonbrandi.com/images/image-20250823190450571.png)

12. **Esquema de partición**: Elige **GPT GUID Partition Table**. Pulsa **Enter** para continuar.

![image-20250823190622981](https://freebsd.edsonbrandi.com/images/image-20250823190622981.png)

13. **Editor de particiones**: Acepta los valores predeterminados y elige **[Finish]**. Pulsa **Enter** para continuar.

![image-20250823190742861](https://freebsd.edsonbrandi.com/images/image-20250823190742861.png)

14. **Confirmación**: En esta pantalla confirmarás que deseas proceder con la instalación de FreeBSD. Tras esta confirmación, el instalador comenzará a escribir datos en tu disco duro. Para proceder con la instalación, elige **[Commit]** y pulsa **Enter** para continuar.

![image-20250823190903913](https://freebsd.edsonbrandi.com/images/image-20250823190903913.png)

15. **Verificación de suma de comprobación**: Al inicio del proceso, el instalador de FreeBSD comprobará la integridad de los archivos de instalación.

![image-20250823191020839](https://freebsd.edsonbrandi.com/images/image-20250823191020839.png)

16. **Extracción de archivos**: Una vez validados los archivos, el instalador los extraerá en tu disco duro.

![image-20250823191053163](https://freebsd.edsonbrandi.com/images/image-20250823191053163.png)

17. **Contraseña de root**: Cuando el instalador termine de extraer los archivos, tendrás que elegir una contraseña para el acceso root. Elige una que puedas recordar. Pulsa **Enter** para continuar.

![image-20250823191405000](https://freebsd.edsonbrandi.com/images/image-20250823191405000.png)

18. **Configuración de red**: Elige la interfaz de red (**em0**) que deseas usar y pulsa **Enter** para continuar.

![image-20250823191520068](https://freebsd.edsonbrandi.com/images/image-20250823191520068.png)

19. **Configuración de red**: Elige **[ Yes ]** para habilitar **IPv4** en tu interfaz de red y pulsa **Enter** para continuar.

![image-20250823191559429](https://freebsd.edsonbrandi.com/images/image-20250823191559429.png)

20. **Configuración de red**: Elige **[ Yes ]** para habilitar **DHCP** en tu interfaz de red; si prefieres usar una dirección IP estática, elige **[ No ]**. Pulsa **Enter** para continuar.

![image-20250823191626027](https://freebsd.edsonbrandi.com/images/image-20250823191626027.png)

21. **Configuración de red**: Elige **[ No ]** para deshabilitar **IPv6** en tu interfaz de red y pulsa **Enter** para continuar.

![image-20250823191705347](https://freebsd.edsonbrandi.com/images/image-20250823191705347.png)

22. **Configuración de red**: Escribe la dirección IP de tus servidores DNS preferidos; en el ejemplo estoy usando el DNS de Google. Pulsa **Enter** para continuar.

![image-20250823191748088](https://freebsd.edsonbrandi.com/images/image-20250823191748088.png)

23. **Selector de zona horaria**: Elige la zona horaria deseada para tu sistema FreeBSD; en este ejemplo estoy usando **UTC**. Pulsa **Enter** para continuar.

![image-20250823191820859](https://freebsd.edsonbrandi.com/images/image-20250823191820859.png)

24. **Confirmación de zona horaria**: Confirma la zona horaria que deseas usar. Elige **[ YES ]** y pulsa **Enter** para continuar.

![image-20250823191849469](https://freebsd.edsonbrandi.com/images/image-20250823191849469.png)

25. **Fecha y hora**: El instalador te dará la oportunidad de ajustar manualmente la fecha y la hora. Por lo general es seguro elegir **[ Skip ]**. Pulsa **Enter** para continuar.

![image-20250823191926758](https://freebsd.edsonbrandi.com/images/image-20250823191926758.png)

![image-20250823191957558](https://freebsd.edsonbrandi.com/images/image-20250823191957558.png)

26. **Configuración del sistema**: El instalador te dará la oportunidad de elegir algunos servicios que se iniciarán en el boot. Selecciona **ntpd** y pulsa **Enter** para continuar.

![image-20250823192055299](https://freebsd.edsonbrandi.com/images/image-20250823192055299.png)

27. **Hardening del sistema**: El instalador te dará la oportunidad de habilitar algunas medidas de seguridad que se aplicarán en el boot. Acepta los valores predeterminados por ahora y pulsa **Enter** para continuar.

![image-20250823192128039](https://freebsd.edsonbrandi.com/images/image-20250823192128039.png)

28. **Comprobación de firmware**: El instalador verificará si algún componente de hardware necesita firmware específico para funcionar correctamente y lo instalará si es necesario. Pulsa **Enter** para continuar.

![image-20250823192211024](https://freebsd.edsonbrandi.com/images/image-20250823192211024.png)

29. **Añadir cuentas de usuario**: El instalador te dará la oportunidad de añadir un usuario normal a tu sistema. Elige **[ Yes ]** y pulsa **Enter** para continuar.

![image-20250823192233281](https://freebsd.edsonbrandi.com/images/image-20250823192233281.png)

30. **Crear un usuario**: El instalador te pedirá que introduzcas la información del usuario y que respondas a algunas preguntas básicas. Debes elegir tu **nombre de usuario** y **contraseña** preferidos; puedes **aceptar las respuestas predeterminadas** para todas las preguntas excepto para la pregunta ***"Invite USER into other groups?"***, a la que debes responder "**wheel**". Este es el grupo en FreeBSD que te permitirá usar el comando `su` para convertirte en root durante una sesión normal.

![image-20250823192452683](https://freebsd.edsonbrandi.com/images/image-20250823192452683.png)

31. **Crear un usuario**: Después de haber respondido a todas las preguntas y creado tu usuario, el instalador de FreeBSD te preguntará si deseas añadir otro usuario. Pulsa simplemente **Enter** para aceptar la respuesta predeterminada (no) y pasar al menú de configuración final.

![image-20250823192600794](https://freebsd.edsonbrandi.com/images/image-20250823192600794.png)

32. **Configuración final**: En este punto ya has completado la instalación de FreeBSD. Este menú final te permite revisar y modificar las opciones que has realizado en los pasos anteriores. Selecciona **Exit** para salir del instalador y pulsa **Enter**.

![image-20250823192642433](https://freebsd.edsonbrandi.com/images/image-20250823192642433.png)

33. **Configuración manual**: El instalador te preguntará si deseas abrir una shell para realizar configuraciones manuales en tu sistema recién instalado. Elige **[ No ]** y pulsa **Enter**.

![image-20250823192704460](https://freebsd.edsonbrandi.com/images/image-20250823192704460.png)

34. **Expulsa el disco de instalación**: Antes de reiniciar la VM, necesitamos expulsar el disco virtual que hemos utilizado para la instalación. Para ello, haz clic izquierdo con el ratón en el icono de CD/DVD de la barra de estado inferior de la ventana de tu VM de VirtualBox y luego haz clic derecho en la opción "Remove Disk From Virtual Drive".

![image-20250823193213602](https://freebsd.edsonbrandi.com/images/image-20250823193213602.png)

Si por algún motivo recibes un mensaje indicando que el disco óptico virtual está en uso y no puede expulsarse, haz clic en el botón "Force Unmount". Después ya podrás proceder al reinicio.

![image-20250823193252804](https://freebsd.edsonbrandi.com/images/image-20250823193252804.png)

35. **Reinicia tu VM**: Pulsa **Enter** en este menú para reiniciar tu VM de FreeBSD.

![image-20250823192732830](https://freebsd.edsonbrandi.com/images/image-20250823192732830.png)

### Instalación de FreeBSD en hardware físico

Si usas un PC o portátil disponible, tendrás que instalar FreeBSD directamente desde una memoria USB de arranque. Así es como se hace:

#### Paso 1: Prepara una memoria USB

- Necesitas una memoria USB con al menos **2 GB de capacidad**.
- Asegúrate de hacer una copia de seguridad de cualquier dato que contenga; el proceso borrará todo.

#### Paso 2: Descarga la imagen adecuada

- Para instalaciones desde USB, descarga la **imagen memstick** (`FreeBSD-14.3-RELEASE-amd64-memstick.img`).

#### Paso 3: Crea el USB de arranque (instrucciones para Windows)

En Windows, la herramienta más sencilla es **Rufus**:

1. Descarga Rufus desde https://rufus.ie.
2. Conecta tu memoria USB.
3. Abre Rufus y selecciona:
   - **Device**: tu memoria USB.
   - **Boot selection**: el archivo `.img` de memstick de FreeBSD que descargaste.
   - **Partition scheme**: MBR
   - **Target System**: BIOS (o UEFI-CSM)
   - **File system**: deja el valor por defecto.
4. Haz clic en *Start*. Rufus te advertirá que todos los datos serán destruidos; acéptalo.
5. Espera a que el proceso termine. Tu memoria USB ya es arrancable.

![image-20250823210622431](https://freebsd.edsonbrandi.com/images/image-20250823210622431.png)

Si ya dispones de un sistema UNIX o similar, puedes crear el USB desde el terminal con el comando `dd`:

```console
% sudo dd if=FreeBSD-14.3-RELEASE-amd64-memstick.img of=/dev/da0 bs=1M
```

Sustituye `/dev/da0` por la ruta de tu dispositivo USB.

#### Paso 4: Arranca desde el USB

1. Conecta la memoria USB en el equipo de destino.
2. Accede al menú de arranque del BIOS/UEFI (normalmente pulsando F12, Esc o Del durante el encendido).
3. Selecciona la unidad USB como dispositivo de arranque.

#### Paso 5: Ejecuta el instalador

Una vez que FreeBSD arranque, sigue los mismos pasos del instalador descritos anteriormente, donde elegimos la distribución de teclado, el nombre del equipo, los componentes del sistema, etc.

Cuando la instalación concluya, retira la memoria USB y reinicia. FreeBSD arrancará ahora desde el disco duro.

### Primer arranque

Tras la instalación, verás el menú de arranque de FreeBSD:

![image-20250823213050882](https://freebsd.edsonbrandi.com/images/image-20250823213050882.png)

Seguido del prompt de inicio de sesión:

![image-20250823212856938](https://freebsd.edsonbrandi.com/images/image-20250823212856938.png)

¡Enhorabuena! Tu máquina de laboratorio FreeBSD está ahora activa y lista para configurarse.

### Cerrando

Acabas de completar uno de los hitos más importantes: instalar FreeBSD 14.3 en tu entorno de laboratorio dedicado. Ya sea en una VM o en hardware físico, ahora tienes un sistema limpio que puedes romper, reparar y reconstruir con seguridad mientras aprendes.

En la siguiente sección recorreremos la **configuración inicial** que debes realizar justo después de la instalación: configurar la red, habilitar los servicios esenciales y preparar el sistema para el trabajo de desarrollo.

## Primer arranque y configuración inicial

Cuando tu sistema FreeBSD termina su primer reinicio tras la instalación, te encuentras con algo muy diferente a Windows o macOS. No hay un escritorio vistoso, ni iconos, ni ningún asistente de "primeros pasos". En su lugar, el sistema te deja directamente en un **prompt de inicio de sesión**.

No te preocupes, es completamente normal e intencionado. FreeBSD es un sistema UNIX diseñado para la estabilidad y la flexibilidad, no para causar una primera impresión llamativa. El entorno por defecto es deliberadamente mínimo para que tú, como administrador, mantengas el control total. Piénsalo como la primera vez que te sientas ante una máquina de laboratorio recién instalada: el shell está vacío, las herramientas aún no están instaladas, pero el sistema está listo para que lo moldees a las necesidades exactas de tu trabajo.

En esta sección llevaremos a cabo los **primeros pasos esenciales** para que tu laboratorio FreeBSD resulte cómodo, seguro y listo para el desarrollo de drivers.

### Inicio de sesión

En el prompt de inicio de sesión:

- Introduce el nombre de usuario que creaste durante la instalación.
- Escribe tu contraseña (recuerda que los sistemas UNIX no muestran `*` mientras escribes la contraseña).

Ahora estás dentro de FreeBSD como usuario normal.

![image-20250823212710535](https://freebsd.edsonbrandi.com/images/image-20250823212710535.png)

### Cambio al usuario root

Algunas tareas, como instalar software o editar archivos del sistema, requieren **privilegios de root**. Debes evitar permanecer conectado como root todo el tiempo (es demasiado arriesgado si cometes un error al escribir un comando), pero sí es buena práctica cambiar temporalmente a root cuando sea necesario:

```console
% su -
Password:
```

Introduce la contraseña de root que estableciste durante la instalación. El prompt cambiará de `%` a `#`, lo que indica que ahora eres root.

![image-20250823213238499](https://freebsd.edsonbrandi.com/images/image-20250823213238499.png)

### Configuración del nombre del equipo y la hora

Tu sistema necesita un nombre y una configuración horaria correcta.

- Para consultar el nombre del equipo:

  ```
  % hostname
  ```

  Si quieres cambiarlo, edita `/etc/rc.conf`:

  ```
  # ee /etc/rc.conf
  ```

  Añade o ajusta esta línea:

  ```
  hostname="fbsd-lab"
  ```

- Para sincronizar la hora, asegúrate de que NTP está habilitado (normalmente lo estará si lo seleccionaste durante la instalación). Puedes comprobarlo con:

  ```
  % date
  ```

  Si la hora es incorrecta, corrígela manualmente por ahora:

  ```
  # date 202508231530
  ```

  (Esto establece la fecha y hora al 23 de agosto de 2025, 15:30; el formato es `YYYYMMDDhhmm`).

### Conceptos básicos de red

La mayoría de las instalaciones con DHCP "simplemente funcionan". Para verificarlo:

```console
% ifconfig
```

Deberías ver una interfaz (como `em0`, `re0` o `vtnet0` en VMs) con una dirección IP. Si no es así, puede que tengas que habilitar DHCP en `/etc/rc.conf`:

```ini
ifconfig_em0="DHCP"
```

Sustituye `em0` por el nombre real de tu interfaz tal como aparece en `ifconfig`.

![image-20250823213433266](https://freebsd.edsonbrandi.com/images/image-20250823213433266.png)

### Instalación y configuración de `sudo`

Como buena práctica, deberías usar `sudo` en lugar de cambiar a root para cada comando privilegiado.

1. Instala sudo:

   ```
   # pkg install sudo
   ```

2. Añade tu usuario al grupo `wheel` (si no lo hiciste al crearlo):

   ```
   # pw groupmod wheel -m yourusername
   ```

3. Ahora, vamos a habilitar el grupo `wheel` para usar `sudo`.

   Ejecuta el comando `visudo` y busca estas líneas en el editor de archivos que se abrirá:

	```sh
	##
	## User privilege specification
	##
	root ALL=(ALL:ALL) ALL

	## Uncomment to allow members of group wheel to execute any command
	# %wheel ALL=(ALL:ALL) ALL

	## Same thing without a password
	#%wheel ALL=(ALL:ALL) NOPASSWD: ALL
	```
Borra el `#` de la línea `#%wheel ALL=(ALL:ALL) NOPASSWD: ALL`, coloca el cursor sobre el carácter que quieres eliminar usando las teclas de dirección y pulsa **x**; para guardar el archivo y salir del editor, pulsa **ESC** y luego escribe **:wq** y pulsa **Enter**.

4. Para verificar que funciona correctamente, cierra sesión y vuelve a iniciarla, y luego ejecuta:

   ```
   % sudo whoami
   root
   ```

Ahora tu usuario puede realizar tareas de administración de forma segura sin necesidad de permanecer conectado como root.

### Actualización del sistema

Antes de instalar las herramientas de desarrollo, actualiza tu sistema:

```console
# freebsd-update fetch install
# pkg update
# pkg upgrade
```

Esto garantiza que ejecutas los últimos parches de seguridad.

![image-20250823215034288](https://freebsd.edsonbrandi.com/images/image-20250823215034288.png)

### Creación de un entorno cómodo

Incluso los pequeños ajustes hacen que el trabajo diario sea más fluido:

- **Habilita el historial y la completación de comandos** (si usas `tcsh`, que es el shell por defecto para los usuarios, ya viene incluido).

- **Edita `.cshrc`** en tu directorio personal para añadir alias útiles:

  ```
  alias ll 'ls -lh'
  alias cls 'clear'
  ```

- **Instala un editor más amigable** (opcional):

  ```
  # pkg install nano
  ```

### Endurecimiento básico de tu laboratorio

Aunque se trate de un **entorno de laboratorio**, es importante añadir algunas capas de seguridad. Esto es especialmente cierto si habilitas **SSH**, tanto si ejecutas FreeBSD dentro de una VM en tu portátil como si lo haces en una máquina física independiente. Una vez que SSH está activo, tu sistema acepta conexiones remotas, y eso significa que debes tomar ciertas precauciones.

Tienes dos enfoques sencillos. Elige el que prefieras; ambos son válidos para un laboratorio.

#### Opción A: Reglas mínimas de `pf` (bloquear todo el tráfico entrante excepto SSH)

1. Habilita `pf` y crea un pequeño conjunto de reglas:

   ```
   # sysrc pf_enable="YES"
   # nano /etc/pf.conf
   ```

   Escribe esto en `/etc/pf.conf` (sustituye `vtnet0`/`em0` por tu interfaz):

   ```sh
   set skip on lo
   
   ext_if = "em0"           # VM often uses vtnet0; on bare metal you may see em0/re0/igb0, etc.
   tcp_services = "{ ssh }"
   
   block in all
   pass out all keep state
   pass in on $ext_if proto tcp to (self) port $tcp_services keep state
   ```

2. Inicia `pf` (y persistirá tras los reinicios):

   ```
   # service pf start
   ```

**Nota para VM:** Si tu VM usa NAT, puede que también necesites configurar el **reenvío de puertos** en tu hipervisor (por ejemplo, VirtualBox: puerto del host 2222 -> puerto del invitado 22) y luego conectarte por SSH a `localhost -p 2222`. La regla de `pf` anterior sigue aplicándose **dentro** del invitado.

#### Opción B: Usa los presets integrados de `ipfw` (muy accesible para principiantes)

1. Habilita `ipfw` con el preset `workstation` y abre SSH:

   ```
   # sysrc firewall_enable="YES"
   # sysrc firewall_type="workstation"
   # sysrc firewall_myservices="22/tcp"
   # sysrc firewall_logdeny="YES"
   # service ipfw start
   ```

   - `workstation` proporciona un conjunto de reglas con estado que "protege esta máquina" y es fácil para empezar.
   - `firewall_myservices` lista los servicios entrantes que quieres permitir; aquí permitimos SSH en TCP/22.
   - Puedes cambiar a otros presets más adelante (por ejemplo, `client`, `simple`) según evolucionen tus necesidades.

**Consejo:** Elige **`pf`** o **`ipfw`**, pero no ambos. Para un primer laboratorio, el preset de `ipfw` es el camino más rápido; el pequeño conjunto de reglas de `pf` es igualmente válido y muy explícito.

#### Mantén el sistema actualizado

Ejecuta estos comandos regularmente para mantenerte al día:

```console
% sudo freebsd-update fetch install
% sudo pkg update && pkg upgrade
```

**¿Por qué molestarse en una VM?** Porque una VM sigue siendo una máquina real en tu red. Los buenos hábitos aquí te preparan para entornos de producción más adelante.

### Cerrando

Tu sistema FreeBSD ya no es un esqueleto vacío: ahora tiene un nombre de equipo, red funcionando, el sistema base actualizado y una cuenta de usuario con acceso a `sudo`. También has aplicado una capa de seguridad pequeña pero significativa: un cortafuegos sencillo que sigue permitiendo SSH y actualizaciones periódicas. No son simplemente ajustes opcionales, sino el tipo de hábitos que definen a un desarrollador de sistemas responsable.

En la siguiente sección instalaremos las **herramientas de desarrollo** necesarias para la programación de drivers, incluidos compiladores, depuradores, editores y el propio árbol de código fuente de FreeBSD. Aquí es donde tu laboratorio deja de ser un lienzo en blanco y se convierte en una estación de desarrollo real.

## Preparación del sistema para el desarrollo

Ahora que tu laboratorio FreeBSD está instalado, actualizado y con un nivel básico de seguridad, es el momento de convertirlo en un **entorno de desarrollo de drivers** completo. Este paso añade las piezas necesarias para compilar, depurar y gestionar versiones del código del kernel: el compilador, el depurador, un sistema de control de versiones y el árbol de código fuente de FreeBSD. Sin ellos, no podrás compilar ni probar el código que escribiremos en capítulos posteriores.

La buena noticia es que FreeBSD ya incluye la mayor parte de lo que necesitamos. En esta sección instalaremos las piezas que faltan, verificaremos que todo funciona y ejecutaremos una pequeña prueba con un módulo "hello" para demostrar que tu laboratorio está listo para el desarrollo de drivers.

### Instalación de las herramientas de desarrollo

FreeBSD incluye **Clang/LLVM** en el sistema base. Para confirmarlo:

```console
% cc --version
FreeBSD clang version 19.1.7 (...)
```

Si ves una cadena de versión como la anterior, estás listo para compilar código C.

Aun así, necesitarás algunas herramientas adicionales:

```console
# pkg install git gmake gdb
```

- `git`: sistema de control de versiones.
- `gmake`: GNU make (algunos proyectos lo requieren además del `make` propio de FreeBSD).
- `gdb`: el depurador GNU.

### Elección de un editor

Todo desarrollador tiene su editor favorito. FreeBSD incluye `vi` por defecto, un editor muy capaz pero con una curva de aprendizaje pronunciada. Si eres completamente nuevo, puedes empezar sin problemas con **`ee` (Easy Editor)**, que te guía con ayuda en pantalla, o instalar **`nano`**, que tiene atajos más sencillos como Ctrl+O para guardar y Ctrl+X para salir:

```console
% sudo pkg install nano
```

Pero tarde o temprano querrás aprender **`vim`**, la versión mejorada de `vi`. Es rápido, muy configurable y ampliamente utilizado en el desarrollo de FreeBSD. Una de sus grandes ventajas es el **resaltado de sintaxis**, que hace que el código C sea mucho más fácil de leer.

#### Configuración de Vim para el resaltado de sintaxis

1. Instala vim:

   ```
   # pkg install vim
   ```

2. Crea un archivo de configuración en tu directorio personal:

   ```
   % ee ~/.vimrc
   ```

3. Añade estas líneas:

   ```
   syntax on
   set number
   set tabstop=8
   set shiftwidth=8
   set autoindent
   set background=dark
   ```

   - `syntax on` -> habilita el resaltado de sintaxis.
   - `set number` -> muestra los números de línea.
   - Los ajustes de tabulación y sangría siguen el **estilo de codificación de FreeBSD** (tabulaciones de 8 espacios, no de 4).
   - `set background=dark` -> hace que los colores sean legibles en un terminal oscuro.

4. Guarda el archivo y abre un programa en C:

   ```
   % vim hello.c
   ```

   Ahora deberías ver palabras clave, cadenas y comentarios resaltados con color.

#### Resaltado de sintaxis en Nano

Si prefieres `nano`, también admite resaltado de sintaxis. La configuración se encuentra en `/usr/local/share/nano/`. Para activarlo en C:

```console
% cp /usr/local/share/nano/c.nanorc ~/.nanorc
```

Abre ahora un archivo `.c` con `nano` y verás el resaltado básico.

#### Easy Editor (ee)

`ee` es la opción más sencilla: sin resaltado, solo texto plano. Es una buena opción para quienes están empezando y resulta muy práctica para editar archivos de configuración rápidamente, aunque es probable que te quedes sin sus capacidades cuando avances en el desarrollo de drivers.

### Acceso a la documentación

Las **páginas del manual** son tu biblioteca de referencia integrada. Prueba esto:

```console
% man 9 malloc
```

Esto muestra la página del manual de la función del kernel `malloc(9)`. El número de sección `(9)` indica que forma parte de las **interfaces del kernel**, donde pasaremos la mayor parte del tiempo más adelante.

Otros comandos útiles:

- `man 1 ls` -> documentación de comandos de usuario.
- `man 5 rc.conf` -> formato de archivo de configuración.
- `man 9 intro` -> introducción a las interfaces de programación del kernel.

### Instalación del árbol de código fuente de FreeBSD

La mayor parte del desarrollo de drivers requiere acceso al código fuente del kernel de FreeBSD. Lo almacenarás en `/usr/src`.

A partir de este momento, cada vez que este libro mencione un archivo como `/usr/src/sys/kern/kern_module.c`, se trata de un archivo real dentro del árbol de código fuente que estás a punto de clonar. `/usr/src` es la ubicación convencional del árbol de código fuente de FreeBSD en un sistema FreeBSD, y cada ruta con la forma `/usr/src/...` en capítulos posteriores corresponde directamente a un archivo dentro del checkout de `src` que se muestra a continuación. Los capítulos posteriores no volverán a explicar esta convención; simplemente citarán la ruta y esperarán que la encuentres en esa ubicación.

Clónalo con Git:

```console
% sudo git clone --branch releng/14.3 --depth 1 https://git.FreeBSD.org/src.git src /usr/src
```

Esto tardará varios minutos y descargará varios gigabytes. Cuando termine, tendrás disponible el árbol de código fuente completo del kernel.

Verifica con:

```console
% ls /usr/src/sys
```

Deberías ver directorios como `dev`, `kern`, `net` y `vm`. Ahí es donde vive el kernel de FreeBSD.

#### Advertencia: ajusta las cabeceras a tu kernel en ejecución

FreeBSD es muy estricto a la hora de compilar módulos del kernel cargables frente al conjunto exacto de cabeceras que corresponde al kernel que estás ejecutando. Si tu kernel se construyó a partir de 14.3-RELEASE pero `/usr/src` apunta a una rama o versión diferente, puedes encontrarte con errores de compilación o carga confusos. Para evitar problemas en los ejercicios de este libro, asegúrate de tener el árbol de código fuente de **FreeBSD 14.3** instalado en `/usr/src` y de que coincida con tu kernel en ejecución. Una comprobación rápida es `freebsd-version -k`, que debería mostrar `14.3-RELEASE`, y tu `/usr/src` debería estar en la rama `releng/14.3` tal como se indica arriba.

**Consejo**: si `/usr/src` ya existe y apunta a otro lugar, puedes redirigirlo:

```console
% sudo git -C /usr/src fetch --all --tags
% sudo git -C /usr/src checkout releng/14.3
% sudo git -C /usr/src pull --ff-only
```

Con el kernel y las cabeceras alineados, tu módulo de ejemplo se compilará y cargará de forma fiable.

### Prueba de tu entorno de trabajo: un "Hello Kernel Module"

Para confirmar que todo funciona, vamos a compilar y cargar un pequeño módulo del kernel. Todavía no es un driver, pero demuestra que tu laboratorio puede compilar e interactuar con el kernel.

1. Crea un archivo llamado `hello_world.c`:

```c
/*
 * hello_world.c - Simple FreeBSD kernel module
 * Prints messages when loaded and unloaded
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

/*
 * Load handler - called when the module is loaded
 */
static int
hello_world_load(module_t mod, int cmd, void *arg)
{
    int error = 0;

    switch (cmd) {
    case MOD_LOAD:
        printf("Hello World! Kernel module loaded.\n");
        break;
    case MOD_UNLOAD:
        printf("Goodbye World! Kernel module unloaded.\n");
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

    return (error);
}

/*
 * Module declaration
 */
static moduledata_t hello_world_mod = {
    "hello_world",      /* module name */
    hello_world_load,   /* event handler */
    NULL                /* extra data */
};

/*
 * Register the module with the kernel
 * DECLARE_MODULE(name, data, sub-system, order)
 */
DECLARE_MODULE(hello_world, hello_world_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(hello_world, 1);
```

1. Crea un `Makefile`:

```console
# Makefile for hello_world kernel module

KMOD=   hello_world
SRCS=   hello_world.c

.include <bsd.kmod.mk>
```

1. Compila el módulo:

```console
# make
```

Esto debería crear un archivo `hello.ko`.

1. Carga el módulo:

```console
# kldload ./hello_world.ko
```

Comprueba el mensaje en el registro del sistema:

```console
% dmesg | tail -n 5
```

Deberías ver:

`Hello World! Kernel module loaded.`.

1. Descarga el módulo:

```console
# kldunload hello_world.ko
```

Comprueba de nuevo:

```console
% dmesg | tail -n 5
```

Deberías ver: 

`Goodbye World! Kernel module unloaded.`

### Laboratorio práctico: verificación de tu entorno de desarrollo

1. Instala `git`, `gmake` y `gdb`.
2. Verifica que Clang funciona con `% cc --version`.
3. Instala y configura `vim` con resaltado de sintaxis, o configura `nano` si lo prefieres.
4. Clona el árbol de código fuente de FreeBSD 14.3 en `/usr/src`.
5. Escribe, compila y carga el módulo del kernel `hello_world`.
6. Registra los resultados (¿viste el mensaje "Hello, kernel world!"?) en tu **cuaderno de laboratorio**.

### En resumen

Has equipado tu taller de FreeBSD con las herramientas esenciales: compilador, depurador, control de versiones, documentación y el propio código fuente del kernel. Incluso has compilado y cargado tu primer módulo del kernel, lo que demuestra que tu configuración funciona de principio a fin.

En la siguiente sección veremos el **uso de snapshots y copias de seguridad** para que puedas experimentar libremente sin miedo a perder tu progreso. Esto te dará la confianza para asumir riesgos mayores y recuperarte rápidamente cuando algo falle.

## Uso de snapshots y copias de seguridad

Una de las mayores ventajas de configurar un **entorno de laboratorio** es que puedes experimentar sin miedo. Cuando escribes código del kernel, los errores son inevitables: un puntero incorrecto, un bucle infinito o una rutina de descarga defectuosa pueden hacer que el sistema operativo entero se bloquee. En lugar de preocuparte, puedes tratar los bloqueos como parte del proceso de aprendizaje, *siempre que* tengas una forma de recuperarte rápidamente.

Ahí es donde entran en juego los **snapshots y las copias de seguridad**. Los snapshots te permiten «congelar» tu laboratorio de FreeBSD en un punto seguro y luego revertirlo al instante si algo sale mal. Las copias de seguridad protegen tus archivos importantes, como tu código o tus notas de laboratorio, en caso de que necesites reinstalar el sistema.

En esta sección exploraremos ambas opciones.

### Snapshots en máquinas virtuales

Si ejecutas FreeBSD en una VM (VirtualBox, VMware, bhyve), dispones de una gran red de seguridad: los **snapshots**.

- En **VirtualBox** o **VMware**, los snapshots se gestionan desde la GUI; puedes guardarlos, restaurarlos y eliminarlos con unos pocos clics.
- En **bhyve**, los snapshots se gestionan a través del **backend de almacenamiento**, normalmente ZFS. Creas un snapshot del dataset que contiene la imagen de disco de la VM y lo reviertes cuando es necesario.

#### Flujo de trabajo de ejemplo con VirtualBox

1. Apaga tu VM de FreeBSD cuando termines la configuración inicial.

2. En VirtualBox Manager, selecciona tu VM -> **Snapshots** -> haz clic en **Take**.

3. Nómbralo: `Clean FreeBSD 14.3 Install`.

   ![image-20250823231838089](https://freebsd.edsonbrandi.com/images/image-20250823231838089.png)

   ![image-20250823231940246](https://freebsd.edsonbrandi.com/images/image-20250823231940246.png)

4. Más adelante, antes de probar código del kernel arriesgado, crea otro snapshot: `Before Hello Driver`.

   ![image-20250823232320392](https://freebsd.edsonbrandi.com/images/image-20250823232320392.png)

5. Si el sistema se bloquea o rompes la red, simplemente restaura el snapshot.

![image-20250823232420760](https://freebsd.edsonbrandi.com/images/image-20250823232420760.png)

#### Flujo de trabajo de ejemplo con bhyve (con ZFS)

Si el disco de tu VM está almacenado en un dataset ZFS, por ejemplo `/zroot/vm/freebsd.img`:

1. Crea un snapshot antes de los experimentos:

   ```
   # zfs snapshot zroot/vm@clean-install
   ```

2. Realiza cambios, prueba código o incluso bloquea el kernel.

3. Reviértelo al instante:

   ```
   # zfs rollback zroot/vm@clean-install
   ```

### Snapshots en hardware real

Si ejecutas FreeBSD directamente en hardware, no tienes el lujo de los snapshots mediante GUI. Pero si instalaste FreeBSD con **ZFS**, sigues teniendo acceso a las mismas herramientas de snapshot.

Con ZFS:

```console
# zfs snapshot -r zroot@clean-install
```

- Esto crea un snapshot de tu sistema de archivos raíz.

- Si algo sale mal, puedes revertir:

  ```
  # zfs rollback -r zroot@clean-install
  ```

Los snapshots de ZFS son instantáneos y no duplican datos: solo registran los cambios. Para laboratorios en hardware real serios, ZFS es muy recomendable.

Si instalaste con **UFS** en lugar de ZFS, no dispondrás de snapshots. En ese caso, recurre a las **copias de seguridad** (véase más adelante) y quizás considera reinstalar con ZFS más adelante si quieres esta red de seguridad.

### Copia de seguridad de tu trabajo

Los snapshots protegen el **estado del sistema**, pero también necesitas proteger tu **trabajo**: tu código de drivers, tus notas y tus repositorios de Git.

Estrategias sencillas:

- **Git**: Si usas Git (y deberías hacerlo), sube tu código a un servicio remoto como GitHub o GitLab. Esta es la mejor copia de seguridad.

- **Tarballs**: Crea un archivo comprimido de tu proyecto:

  ```
  % tar czf mydriver-backup.tar.gz mydriver/
  ```

- **Copia al host**: Si usas una VM, copia los archivos del invitado al host (carpetas compartidas de VirtualBox o `scp` por SSH).

**Nota**: Piensa en tu VM como algo desechable, pero tu **código es valioso**. Haz siempre una copia de seguridad antes de probar cambios peligrosos.

### Laboratorio práctico: romper y arreglar

1. Si usas VirtualBox/VMware:

   - Crea un snapshot llamado `Before Break`.
   - Como root, ejecuta algo inofensivo pero destructivo (por ejemplo, elimina `/tmp/*`).
   - Restaura el snapshot y confirma que `/tmp` ha vuelto a la normalidad.

2. Si usas bhyve con almacenamiento respaldado por ZFS:

   - Crea un snapshot de tu dataset de VM.
   - Elimina un archivo de prueba dentro del invitado.
   - Revierte el snapshot de ZFS.

3. Si estás en hardware real con ZFS:

   - Crea un snapshot `zroot@before-break`.
   - Elimina un archivo de prueba.
   - Revierte con `zfs rollback` y confirma que el archivo se ha restaurado.

4. Haz una copia de seguridad del código fuente de tu módulo del kernel `hello_world` con:

   ```
   % tar czf hello-backup.tar.gz hello_world/
   ```

Anota en tu **cuaderno de laboratorio**: qué método usaste, cuánto tardó y cuánta confianza tienes ahora para experimentar.

### En resumen

Al aprender a usar **snapshots y copias de seguridad**, has añadido una de las redes de seguridad más importantes a tu laboratorio. Ahora puedes bloquear, romper o configurar mal FreeBSD y recuperarte en minutos. Esta libertad es lo que hace tan valioso un laboratorio: te permite centrarte en aprender, sin miedo a cometer errores.

En la siguiente sección configuraremos el **control de versiones con Git** para que puedas hacer un seguimiento de tu progreso, gestionar tus experimentos y compartir tus drivers.

## Configuración del control de versiones

Hasta ahora has preparado tu laboratorio de FreeBSD, instalado las herramientas e incluso compilado tu primer módulo del kernel. Pero imagina esto: haces un cambio en tu driver, lo pruebas y de repente nada funciona. Ojalá pudieras volver a la última versión que funcionaba. O quizás quieres mantener dos experimentos diferentes sin mezclarlos.

Esto es exactamente por qué los desarrolladores usan **sistemas de control de versiones**: herramientas que registran el historial de tu trabajo, te permiten volver a estados anteriores y facilitan compartir código con otros. En el mundo de FreeBSD (y en la mayoría de proyectos de código abierto), el estándar es **Git**.

En esta sección aprenderás a usar Git para gestionar tus drivers desde el primer día.

### Por qué importa el control de versiones

- **Seguimiento de cambios**: cada experimento, cada corrección, cada error queda guardado.
- **Deshacer con seguridad**: si tu código deja de funcionar, puedes volver a una versión buena conocida.
- **Organizar experimentos**: puedes trabajar en ideas nuevas en «ramas» sin romper tu código principal.
- **Compartir tu trabajo**: si quieres recibir comentarios de otros o publicar tus drivers, Git lo facilita.
- **Hábito profesional**: todo proyecto de software serio (incluido el propio FreeBSD) usa control de versiones.

Piensa en Git como el **cuaderno de laboratorio de tu código**, pero más inteligente: no solo registra lo que hiciste, sino que también puede restaurar tu código a cualquier punto pasado en el tiempo.

### Instalación de Git

Si aún no has instalado Git en la sección 2.5, hazlo ahora:

```console
# pkg install git
```

Comprueba la versión:

```console
% git --version
git version 2.45.2
```

### Configuración de Git (tu identidad)

Antes de usar Git, configura tu identidad para que tus commits queden correctamente etiquetados:

```console
% git config --global user.name "Your Name"
% git config --global user.email "you@example.com"
```

No es necesario que sea tu nombre real ni tu correo electrónico si solo estás experimentando en local, pero si alguna vez compartes código públicamente, conviene usar algo coherente.

Puedes comprobar tu configuración con:

```console
% git config --list
```

### Creación de tu primer repositorio

Vamos a poner tu módulo del kernel `hello_world` bajo control de versiones.

1. Ve al directorio donde creaste `hello_world.c` y el `Makefile`.

2. Inicializa un repositorio de Git:

   ```
   % git init
   ```

   Esto crea un directorio oculto `.git` donde Git almacena su historial.

3. Añade tus archivos:

   ```
   % git add hello_world.c Makefile
   ```

4. Haz tu primer commit:

   ```
   % git commit -m "Initial commit: hello_world kernel module"
   ```

5. Comprueba el historial:

   ```
   % git log
   ```

   Deberías ver tu commit en la lista.

### Buenas prácticas para los commits

- **Escribe mensajes de commit claros**: describe qué cambió y por qué.

  - Malo: `fix stuff`
  - Bueno: `Fix null pointer dereference in hello_loader()`

- **Haz commits con frecuencia**: los commits pequeños son más fáciles de entender y revertir.

- **Mantén los experimentos separados**: si pruebas una idea nueva, crea una rama:

  ```
  % git checkout -b experiment-null-fix
  ```

Aunque nunca compartas tu código, estos hábitos te ayudarán a depurar y aprender más rápido.

### Uso de repositorios remotos (opcional)

Por ahora, puedes mantenerlo todo en local. Pero si quieres sincronizar tu código entre máquinas o compartirlo públicamente, puedes enviarlo a un servicio remoto como **GitHub** o **GitLab**.

Flujo de trabajo básico:

```console
% git remote add origin git@github.com:yourname/mydriver.git
% git push -u origin main
```

Esto es opcional en el laboratorio, pero muy útil si quieres hacer una copia de seguridad de tu trabajo en la nube.

### Laboratorio práctico: Control de versiones para tu driver

1. Inicializa un repositorio Git en el directorio de tu módulo `hello`.

2. Realiza tu primer commit.

3. Edita `hello_world.c` (por ejemplo, cambia el texto del mensaje).

4. Ejecuta:

   ```
   % git diff
   ```

   para ver exactamente qué cambió.

5. Haz un commit del cambio con un mensaje claro.

6. Anota en tu **diario de laboratorio**:

   - Cuántos commits realizaste.
   - Qué hizo cada commit.
   - Cómo revertirías los cambios si algo se rompiese.

### En resumen

Ya has dado los primeros pasos con Git, una de las herramientas más importantes de tu kit de desarrollo. A partir de ahora, cada driver que escribas en este libro debería vivir en su propio repositorio Git. Así nunca perderás tu progreso y siempre tendrás un registro de tus experimentos.

En la siguiente sección hablaremos sobre **documentar tu trabajo**, otro hábito clave de los desarrolladores profesionales. Un README bien redactado o un mensaje de commit claro pueden marcar la diferencia entre código que entiendes un año después y código que tienes que reescribir desde cero.

## Documentar tu trabajo

El desarrollo de software no consiste solo en escribir código; también implica asegurarte de que *tú* (y a veces otras personas) podáis entender ese código más adelante. Cuando trabajas en drivers de FreeBSD, a menudo volverás a un proyecto semanas o meses después y te preguntarás: *"¿Por qué escribí esto? ¿Qué estaba probando? ¿Qué cambié?"*

Sin documentación, perderás horas redescubriendo tu propio proceso de pensamiento. Con buenas notas, podrás retomar exactamente donde lo dejaste.

Piensa en la documentación como la **memoria de tu laboratorio**. Del mismo modo que los científicos llevan cuadernos de laboratorio detallados, los desarrolladores deben mantener notas claras, READMEs y mensajes de commit.

### Por qué importa la documentación

- **Tu yo del futuro te lo agradecerá**: Los detalles que hoy parecen obvios se olvidarán en un mes.
- **La depuración se vuelve más fácil**: Cuando algo falla, las notas te ayudan a entender qué cambió.
- **Compartir es más sencillo**: Si publicas tu driver, otros podrán aprender de tu README.
- **Hábito profesional**: FreeBSD es famoso por la alta calidad de su documentación; seguir esta tradición hará que tu trabajo encaje de forma natural en el ecosistema.

### Escribir un README sencillo

Todo proyecto debería comenzar con un archivo `README.md`. Como mínimo, incluye:

1. **Nombre del proyecto**:

   ```
   Hello Kernel Module
   ```

2. **Descripción**:

   ```
   A simple "Hello, kernel world!" module for FreeBSD 14.3.
   ```

3. **Cómo compilar**:

   ```
   % make
   ```

4. **Cómo cargar/descargar**:

   ```
   # kldload ./hello_world.ko
   # kldunload hello_world
   ```

5. **Notas**:

   ```
   This was created as part of my driver development lab, Chapter 2.
   ```

### Usar los mensajes de commit como documentación

Los mensajes de commit de Git son una forma de documentación. En conjunto, cuentan la historia de tu proyecto. Sigue estos consejos:

- Escribe los mensajes de commit en tiempo presente ("Add feature", no "Added feature").
- Haz que la primera línea sea breve (50 caracteres o menos).
- Si es necesario, añade una línea en blanco y luego una explicación más extensa.

Ejemplo:

```text
Fix panic when unloading hello module

The handler did not check for NULL before freeing resources,
causing a panic when unloading. Added a guard condition.
```

### Llevar un diario de laboratorio

En la sección 2.1 sugerimos empezar un diario de laboratorio. Ahora es un buen momento para convertirlo en un hábito. Mantén un archivo de texto (por ejemplo, `LABLOG.md`) en la raíz de tu repositorio Git. Cada vez que pruebes algo nuevo, añade una entrada breve:

```text
2025-08-23
- Built hello module successfully.
- Confirmed "Hello, kernel world!" appears in dmesg.
- Tried unloading/reloading multiple times, no errors.
- Next step: experiment with passing parameters to the module.
```

Este registro no necesita estar pulido; es solo para ti. Más adelante, cuando depures, estas notas pueden ser de un valor incalculable.

### Herramientas de ayuda

- **Markdown**: Tanto el README como los diarios de laboratorio se pueden escribir en Markdown (`.md`), que es fácil de leer como texto plano y se muestra con un formato agradable en GitHub/GitLab.
- **Páginas del manual**: Anota siempre qué páginas del manual utilizaste (por ejemplo, `man 9 module`). Esto te recordará cuáles fueron tus fuentes.
- **Capturas de pantalla y registros**: Si usas una VM, toma capturas de pantalla de los pasos importantes o guarda la salida de los comandos en archivos mediante redirección (`dmesg > dmesg.log`).

### Laboratorio práctico: Documentar tu primer módulo

1. En el directorio de tu módulo `hello_world`, crea un `README.md` que describa qué hace, cómo compilarlo y cómo cargarlo y descargarlo.

2. Añade tu `README.md` a Git y haz un commit:

   ```
   % git add README.md
   % git commit -m "Add README for hello_world module"
   ```

3. Crea un archivo `LABLOG.md` y registra las actividades de hoy.

4. Revisa tu historial de Git con:

   ```
   % git log --oneline
   ```

   para ver cómo tus commits cuentan la historia de tu proyecto.

### En resumen

Ahora sabes cómo documentar tus experimentos con drivers de FreeBSD para no perder nunca el hilo de lo que hiciste ni del motivo. Con un `README`, mensajes de commit significativos y un diario de laboratorio, estás construyendo hábitos que te harán un desarrollador más profesional y eficiente.

En la siguiente sección cerraremos este capítulo repasando todo lo que has construido: un laboratorio de FreeBSD seguro con las herramientas adecuadas, copias de seguridad, control de versiones y documentación, todo listo para la exploración más profunda de FreeBSD en el Capítulo 3.

## En resumen

¡Enhorabuena! ¡Ya has construido tu laboratorio de FreeBSD!

En este capítulo:

- Comprendiste por qué un **entorno de laboratorio seguro** es fundamental para el desarrollo de drivers.
- Elegiste la configuración adecuada para tu situación: una **máquina virtual** o **bare metal**.
- Instalaste **FreeBSD 14.3** paso a paso.
- Realizaste la **configuración inicial**, incluida la red, los usuarios y el bastionado básico.
- Instalaste las **herramientas de desarrollo** esenciales: compilador, depurador, Git y editores.
- Configuraste el **resaltado de sintaxis** en tu editor, lo que facilita la lectura del código C.
- Clonaste el **árbol de código fuente de FreeBSD 14.3** en `/usr/src`.
- Compilaste y probaste tu primer **módulo del kernel**.
- Aprendiste a usar **snapshots y copias de seguridad** para recuperarte rápidamente de los errores.
- Empezaste a usar **Git** para el control de versiones y añadiste un **README** y un **diario de laboratorio** para documentar tu trabajo.

Es un progreso impresionante para un solo capítulo. Ahora tienes un taller completo: un sistema FreeBSD en el que puedes escribir, construir, probar, romper cosas y recuperarte tantas veces como necesites.

Lo más importante que te llevas no son solo las herramientas que instalaste, sino la **mentalidad**:

- Espera cometer errores.
- Registra tu proceso.
- Usa snapshots, copias de seguridad y Git para recuperarte y aprender.

### Ejercicios

1. **Snapshots**
   - Toma un snapshot de tu VM o un snapshot ZFS en bare metal.
   - Realiza un cambio deliberado (por ejemplo, elimina `/tmp/testfile`).
   - Revierte los cambios y verifica que el sistema se ha restaurado.
2. **Control de versiones**
   - Realiza una pequeña edición en tu módulo del kernel `hello_world.c`.
   - Haz un commit del cambio con Git.
   - Usa `git log` y `git diff` para revisar tu historial.
3. **Documentación**
   - Añade una nueva entrada en tu `LABLOG.md` describiendo el trabajo de hoy.
   - Actualiza tu `README.md` con una nota nueva (por ejemplo, menciona la salida de `uname -a`).
4. **Reflexión**
   - En tu diario de laboratorio, responde: *¿Cuáles son las tres medidas de seguridad más importantes que configuré en el Capítulo 2?*

### Lo que viene a continuación

En el próximo capítulo nos adentraremos en tu nuevo laboratorio de FreeBSD y exploraremos cómo **usar el propio sistema**. Aprenderás los fundamentos de los comandos UNIX, la navegación y la gestión de archivos. Estas habilidades te harán sentir cómodo trabajando dentro de FreeBSD y te prepararán para los temas más avanzados que están por venir.

Tu laboratorio está listo. Ahora es el momento de aprender a trabajar en él.
