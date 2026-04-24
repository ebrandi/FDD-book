---
title: "Escribe tu primer driver"
description: "Un recorrido práctico que construye un driver mínimo de FreeBSD con una gestión del ciclo de vida limpia y ordenada."
partNumber: 2
partName: "Building Your First Driver"
chapter: 7
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Traducción al español asistida por IA usando el modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 600
language: "es-ES"
---
# Escribiendo tu primer driver

## Orientación para el lector y objetivos

Bienvenido a la Parte 2. Si la Parte 1 fue tu base, donde aprendiste el entorno, el lenguaje y la arquitectura, **la Parte 2 es donde construyes**. Este capítulo marca el momento en que dejas de leer sobre drivers y empiezas a escribir uno.

Pero seamos claros sobre qué vamos a construir y, con la misma importancia, qué no vamos a construir todavía. Este capítulo sigue un **enfoque que prioriza la disciplina**: escribirás un driver mínimo que se conecta correctamente, registra eventos de forma adecuada, crea una interfaz de usuario sencilla y se desconecta sin fugas. Sin I/O elaborada, sin acceso a registros de hardware, sin gestión de interrupciones. Eso llega más adelante, cuando la disciplina sea algo natural.

### Qué vas a construir

Cuando avances al siguiente capítulo, tendrás un driver de FreeBSD 14.3 funcional llamado `myfirst` que:

- **Se conecta como pseudodispositivo** usando el framework Newbus
- **Crea un nodo `/dev/myfirst0`** (provisional, solo lectura por ahora)
- **Expone un sysctl de solo lectura** que muestra el estado básico en tiempo de ejecución
- **Registra los eventos del ciclo de vida** correctamente con `device_printf()`
- **Gestiona los errores** con un patrón de desenrollado de etiqueta única
- **Se desconecta limpiamente** sin fugas de recursos ni punteros colgantes

Este driver no hará nada emocionante todavía. No leerá de hardware, no gestionará interrupciones y no procesará paquetes ni bloques. Lo que *sí* hará es demostrar la **disciplina del ciclo de vida**: la base de la que depende todo driver en producción.

### Qué **no** vas a construir (todavía)

Este capítulo aplaza deliberadamente varios temas importantes para que puedas dominar la estructura antes que la complejidad:

- **Semántica de I/O completa**: `read(2)` y `write(2)` serán provisionales. Los caminos reales de lectura y escritura llegan en el Capítulo 9, después de que el Capítulo 8 haya cubierto la política de archivos de dispositivo y la visibilidad en espacio de usuario.
- **Interacción con hardware**: Sin acceso a registros, sin DMA, sin interrupciones. Eso se cubre en la **Parte 4** cuando tengas una base sólida.
- **Especificidades de PCI/USB/ACPI**: Usamos un pseudodispositivo (sin dependencia de bus) en este capítulo. Los patrones de conexión específicos por bus aparecen en la Parte 4 (PCI, interrupciones, DMA) y la Parte 6 (USB, almacenamiento, red).
- **Locks y concurrencia**: Verás un mutex en el softc, pero no ejercitaremos caminos concurrentes complejos hasta la **Parte 3**.
- **Sysctls avanzados**: Solo un nodo de solo lectura por ahora. Los árboles de sysctls más grandes, los manejadores de escritura y las variables ajustables vuelven en la Parte 5.

**Por qué importa esto:** Intentar aprender todo a la vez genera confusión. Manteniendo el alcance reducido, entenderás *por qué* existe cada pieza antes de añadir la siguiente capa.

### Estimación de tiempo

- **Solo lectura**: 2-3 horas para absorber conceptos y explicaciones del código
- **Lectura + escritura de ejemplos**: 4-5 horas si escribes el código del driver tú mismo
- **Lectura + los cuatro laboratorios**: 5-7 horas, incluyendo los ciclos de build, prueba y verificación
- **Desafíos opcionales**: Añade 2-3 horas para los ejercicios de exploración en profundidad

**Ritmo recomendado:** Divide esto en dos o tres sesiones. Sesión 1 hasta el esqueleto y los conceptos básicos de Newbus, sesión 2 hasta el registro de eventos y el manejo de errores, sesión 3 para los laboratorios y las pruebas de humo.

### Requisitos previos

Antes de empezar, asegúrate de tener:

- **FreeBSD 14.3** funcionando en tu laboratorio (VM o máquina real)
- **Los Capítulos 1-6 completados** (especialmente la configuración del laboratorio del Capítulo 2 y el recorrido anatómico del Capítulo 6)
- **`/usr/src` instalado** con las fuentes de FreeBSD 14.3 que coincidan con tu kernel en ejecución
- **Manejo básico de C** del Capítulo 4
- **Conocimiento de programación del kernel** del Capítulo 5

Comprueba tu versión del kernel:

```bash
% freebsd-version -k
14.3-RELEASE
```

Si esto no coincide, vuelve a la guía de configuración del Capítulo 2.

### Objetivos de aprendizaje

Al completar este capítulo, serás capaz de:

- Crear el esqueleto de un driver de FreeBSD mínimo desde cero
- Implementar y explicar los métodos del ciclo de vida probe/attach/detach
- Definir y usar una estructura softc del driver de forma segura
- Crear y destruir nodos en `/dev` usando `make_dev_s()`
- Añadir un sysctl de solo lectura para la observabilidad
- Gestionar errores con un desenrollado disciplinado (patrón de etiqueta única `fail:`)
- Compilar, cargar, probar y descargar tu driver de forma fiable
- Identificar y corregir errores comunes de principiante (fugas de recursos, desreferencias nulas, limpieza ausente)

### Criterios de éxito

Sabrás que has tenido éxito cuando:

- `kldload ./myfirst.ko` se complete sin errores
- `dmesg -a` muestre tu mensaje de conexión
- `ls -l /dev/myfirst0` muestre tu nodo de dispositivo
- `sysctl dev.myfirst.0` devuelva el estado de tu driver
- `kldunload myfirst` limpie sin fugas ni panics
- Puedas repetir el ciclo de carga y descarga de forma fiable
- Un fallo simulado de conexión desenrolle limpiamente (prueba del camino negativo)

### Dónde encaja este capítulo

Estás entrando en la **Parte 2: Construyendo tu primer driver**, el puente entre la teoría y la práctica:

- **Capítulo 7 (este capítulo)**: crear el esqueleto de un driver mínimo con attach/detach limpio
- **Capítulo 8**: conectar las semánticas reales de `open()`, `close()` y archivos de dispositivo
- **Capítulo 9**: implementar los caminos básicos de `read()` y `write()`
- **Capítulo 10**: gestionar buffering, bloqueo y poll/select

Cada capítulo se apoya en el anterior, añadiendo una capa de funcionalidad a la vez.

### Una nota sobre "Hello World" versus "Hello Production"

Probablemente hayas visto módulos del kernel de tipo "hello world" antes: un manejador de eventos `MOD_LOAD` que imprime un mensaje. Está bien para comprobar si el sistema de build funciona, pero no es un driver. No se conecta a nada, no crea interfaces de usuario y no enseña casi nada sobre la disciplina del ciclo de vida.

El driver `myfirst` de este capítulo es diferente. Sigue siendo mínimo, pero sigue los patrones que verás en todos los drivers de FreeBSD en producción:

- Se registra con Newbus
- Implementa probe/attach/detach correctamente
- Gestiona recursos (aunque sean triviales)
- Limpia de forma fiable

Piensa en `myfirst` como **hello production**, no hello world. El salto de juguete a herramienta empieza aquí.

### Cómo usar este capítulo

1. **Lee en orden secuencial**: Cada sección se apoya en la anterior. No te adelantes.
2. **Escribe el código tú mismo**: La memoria muscular importa. Copiar fragmentos está bien, pero escribirlos asienta los patrones.
3. **Completa los laboratorios**: Son puntos de control, no extras opcionales. Cada laboratorio valida la comprensión antes de avanzar.
4. **Usa la lista de verificación final**: Antes de declarar la victoria, recorre la lista de verificación de pruebas de humo (cerca del final de este capítulo). Detecta errores comunes.
5. **Lleva un registro**: Anota qué funcionó, qué falló y qué aprendiste. Tu yo futuro te lo agradecerá.

### Una palabra sobre los errores

*Cometerás* errores. Olvidarás inicializar un puntero, saltarás un paso de limpieza, escribirás mal el nombre de una función. Es algo esperado y **saludable**. Cada error es una oportunidad para practicar la depuración, leer registros y entender la causa y el efecto.

Cuando algo falle:

- Lee el mensaje de error completo. Los mensajes del kernel de FreeBSD son detallados.
- Comprueba `dmesg -a` para ver los eventos del ciclo de vida.
- Usa el árbol de decisiones para la resolución de problemas (sección más adelante en este capítulo).
- Vuelve a la sección correspondiente y compara tu código con los ejemplos.

No pases por alto los errores. Son momentos de aprendizaje.

### Empecemos

Has completado la base. Has recorrido drivers reales en el Capítulo 6. Ahora es el momento de **construir el tuyo propio**. Empecemos con el esqueleto del proyecto.



## Esqueleto del proyecto (estructura KLD)

Todo driver comienza con un esqueleto: una estructura básica que compila, carga y descarga sin hacer gran cosa. Piensa en esto como el armazón de una casa: las paredes, las puertas y el mobiliario llegan después. Ahora mismo estamos construyendo los cimientos y la estructura que lo sostiene todo.

En esta sección, crearás un proyecto de driver de FreeBSD 14.3 mínimo desde cero. Al final tendrás:

- Una estructura de directorios limpia
- Un Makefile sencillo
- Un archivo `.c` con el código de ciclo de vida absolutamente mínimo
- Un build funcional que produce un módulo `myfirst.ko`

Este esqueleto es **aburrido a propósito**. Todavía no creará nodos en `/dev`, no implementará sysctls ni realizará ningún trabajo real. Pero te enseñará el ciclo de build, la estructura básica y la disciplina de una entrada y salida limpias. Domina esto y todo lo demás es simplemente añadir capas.

### Estructura de directorios

Vamos a crear un espacio de trabajo para tu driver. La convención en el árbol de código fuente de FreeBSD es mantener los drivers bajo `/usr/src/sys/dev/<nombreconductor>`, pero para tu primer driver trabajaremos en tu directorio personal. Esto mantiene tus experimentos aislados y hace que reconstruir sea sencillo.

Crea la estructura:

```bash
% mkdir -p ~/drivers/myfirst
% cd ~/drivers/myfirst
```

Tu directorio de trabajo contendrá:

```text
~/drivers/myfirst/
├── myfirst.c      # Driver source code
└── Makefile       # Build instructions
```

Eso es todo. El sistema de build de módulos del kernel de FreeBSD (`bsd.kmod.mk`) se encarga de toda la complejidad (indicadores del compilador, rutas de inclusión, enlazado, etc.) por ti.

**¿Por qué esta organización?**

- **Directorio único**: Mantiene todo junto, fácil de limpiar (`rm -rf ~/drivers/myfirst`).
- **Con el nombre del driver**: Cuando tengas varios proyectos, sabrás qué contiene `~/drivers/myfirst`.
- **Sigue los patrones del árbol**: Los drivers reales de FreeBSD en `/usr/src/sys/dev/` siguen el mismo enfoque de "un directorio por driver".

### El Makefile mínimo

El sistema de build de FreeBSD es extraordinariamente sencillo para módulos del kernel. Crea `Makefile` con estas tres líneas:

```makefile
# Makefile for myfirst driver

KMOD=    myfirst
SRCS=    myfirst.c

.include <bsd.kmod.mk>
```

**Línea por línea:**

- `KMOD= myfirst` - Declara el nombre del módulo. Esto producirá `myfirst.ko`.
- `SRCS= myfirst.c` - Lista los archivos fuente. Solo tenemos uno por ahora.
- `.include <bsd.kmod.mk>` - Incorpora las reglas de build de módulos del kernel de FreeBSD. Esta única línea reemplaza cientos de líneas de lógica makefile manual.

**Importante:** La indentación antes de `.include` es un carácter de **tabulación**, no espacios. Si usas espacios, `make` fallará con un error críptico. (La mayoría de los editores se pueden configurar para insertar tabulaciones al pulsar la tecla Tab.)

**Qué proporciona `bsd.kmod.mk`:**

- Indicadores del compilador correctos para código del kernel (`-D_KERNEL`, `-ffreestanding`, etc.)
- Rutas de inclusión (`-I/usr/src/sys`, `-I/usr/src/sys/dev`, etc.)
- Reglas de enlazado para crear archivos `.ko`
- Objetivos estándar: `make`, `make clean`, `make install`, etc.

No necesitas entender los detalles internos. Solo ten en cuenta que `.include <bsd.kmod.mk>` te proporciona un sistema de build funcional sin coste alguno.

**Prueba el Makefile:**

Antes de escribir ningún código, comprueba la configuración del build:

```bash
% make clean
% ls
Makefile
```

Ahora mismo, `make clean` no hace casi nada (todavía no hay archivos que eliminar), pero confirma que la sintaxis del Makefile es válida.

### El `myfirst.c` básico

Ahora crea `myfirst.c`, el código fuente real del driver. Esta primera versión es **mínima por diseño**: compila, carga y descarga, pero no crea dispositivos, no gestiona I/O y no asigna recursos.

Aquí está el esqueleto:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

/*
 * Module load/unload event handler.
 *
 * This function is called when the module is loaded (MOD_LOAD)
 * and unloaded (MOD_UNLOAD). For now, we just print messages.
 */
static int
myfirst_loader(module_t mod, int what, void *arg)
{
        int error = 0;

        switch (what) {
        case MOD_LOAD:
                printf("myfirst: driver loaded\n");
                break;
        case MOD_UNLOAD:
                printf("myfirst: driver unloaded\n");
                break;
        default:
                error = EOPNOTSUPP;
                break;
        }

        return (error);
}

/*
 * Module declaration.
 *
 * This ties the module name "myfirst" to the loader function above.
 */
static moduledata_t myfirst_mod = {
        "myfirst",              /* module name */
        myfirst_loader,         /* event handler */
        NULL                    /* extra arg (unused here) */
};

/*
 * DECLARE_MODULE registers this module with the kernel.
 *
 * Parameters:
 *   - module name: myfirst
 *   - moduledata: myfirst_mod
 *   - subsystem: SI_SUB_DRIVERS (driver subsystem)
 *   - order: SI_ORDER_MIDDLE (standard priority)
 */
DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myfirst, 1);
```

**Qué hace este código:**

- **Includes**: Incorporan las cabeceras del kernel para la infraestructura del módulo y el registro de eventos.
- **`myfirst_loader()`**: Gestiona los eventos del ciclo de vida del módulo. Por ahora, solo MOD_LOAD y MOD_UNLOAD.
- **`moduledata_t`**: Conecta el nombre del módulo con la función de carga.
- **`DECLARE_MODULE()`**: Registra el módulo en el kernel. Esto es lo que hace que `kldload` reconozca tu módulo.
- **`MODULE_VERSION()`**: Marca el módulo con la versión 1 (incrementa esto si en el futuro cambias el ABI exportado).

**Qué no hace este código (todavía):**

- No crea ningún dispositivo
- No llama a `make_dev()`
- No se registra con Newbus
- No asigna memoria ni recursos

Esto es **solo carga/descarga**: el mínimo absoluto para demostrar que el sistema de build funciona.

### Compilar y probar el esqueleto

Compilemos y carguemos este módulo mínimo:

**1. Compilar:**

```bash
% make
machine -> /usr/src/sys/amd64/include
x86 -> /usr/src/sys/x86/include
i386 -> /usr/src/sys/i386/include
touch opt_global.h
Warning: Object directory not changed from original /usr/home/youruser/project/myfirst
cc  -O2 -pipe  -fno-strict-aliasing -Werror -D_KERNEL -DKLD_MODULE -nostdinc   -include /usr/home/youruser/project/myfirst/opt_global.h -I. -I/usr/src/sys -I/usr/src/sys/contrib/ck/include -fno-common  -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fdebug-prefix-map=./machine=/usr/src/sys/amd64/include -fdebug-prefix-map=./x86=/usr/src/sys/x86/include -fdebug-prefix-map=./i386=/usr/src/sys/i386/include    -MD  -MF.depend.myfirst.o -MTmyfirst.o -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -msoft-float  -fno-asynchronous-unwind-tables -ffreestanding -fwrapv -fstack-protector  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual -Wundef -Wno-pointer-sign -D__printf__=__freebsd_kprintf__ -Wmissing-include-dirs -fdiagnostics-show-option -Wno-unknown-pragmas -Wswitch -Wno-error=tautological-compare -Wno-error=empty-body -Wno-error=parentheses-equality -Wno-error=unused-function -Wno-error=pointer-sign -Wno-error=shift-negative-value -Wno-address-of-packed-member -Wno-format-zero-length   -mno-aes -mno-avx  -std=gnu17 -c myfirst.c -o myfirst.o
ld -m elf_x86_64_fbsd -warn-common --build-id=sha1 -T /usr/src/sys/conf/ldscript.kmod.amd64 -r  -o myfirst.ko myfirst.o
:> export_syms
awk -f /usr/src/sys/conf/kmod_syms.awk myfirst.ko  export_syms | xargs -J % objcopy % myfirst.ko
objcopy --strip-debug myfirst.ko
```

Verás la salida del compilador. Mientras termine con `myfirst.ko` creado y sin errores, todo va bien.

**2. Verificar la salida del build:**

```bash
% ls -l myfirst.ko
-rw-r--r--  1 youruser youruser 11592 Nov  7 00:15 myfirst.ko
```

(El tamaño del archivo variará según el compilador y la arquitectura.)

**3. Cargar el módulo:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 2
myfirst: driver loaded
```

**4. Comprobar que está cargado:**

```bash
% kldstat | grep myfirst
 6    1 0xffffffff82a38000     20b8 myfirst.ko
```

Tu módulo es ahora parte del kernel en ejecución.

**5. Descargar el módulo:**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst: driver unloaded
```

**6. Confirmar que se ha ido:**

```bash
% kldstat | grep myfirst
(no output)
```

Perfecto. Tu esqueleto funciona.

### ¿Qué acaba de pasar?

Vamos a seguir el flujo paso a paso:

1. **Compilación:** `make` invocó el sistema de build de módulos del kernel de FreeBSD, que compiló `myfirst.c` con los flags del kernel y lo enlazó en `myfirst.ko`.
2. **Carga:** `kldload` leyó `myfirst.ko`, lo enlazó en el kernel en ejecución y llamó a tu función `myfirst_loader()` con `MOD_LOAD`.
3. **Registro:** Tu `printf()` escribió "myfirst: driver loaded" en el buffer de mensajes del kernel.
4. **Descarga:** `kldunload` llamó a tu loader con `MOD_UNLOAD`, imprimiste un mensaje y, a continuación, el kernel eliminó tu código de la memoria.

**Idea clave:** Esto todavía no es un driver de Newbus. No hay `probe()`, ni `attach()`, ni dispositivos. Es simplemente un módulo que se carga y se descarga. Piénsalo como la **etapa 0**: demostrar que el sistema de build funciona antes de añadir complejidad.

### Solución de problemas habituales del scaffold

**1. Problema:** `make` falla con "missing separator"

**Causa:** Tu Makefile usa espacios en lugar de tabuladores antes de `.include`.

**Solución:** Sustituye los espacios iniciales por un carácter de tabulación.

**2. Problema:** `kldload` dice "Exec format error"

**Causa:** Incompatibilidad entre la versión de tu kernel y la versión de `/usr/src`.

**Solución:** Verifica que `freebsd-version -k` coincide con tu árbol de código fuente. Recompila tu kernel o vuelve a clonar `/usr/src` para la versión correcta.

**3. Problema:** El módulo se carga pero no aparece ningún mensaje en `dmesg`

**Causa:** Es posible que el buffer de mensajes del kernel haya sobrepasado su capacidad, o que `printf()` esté siendo limitado por frecuencia de llamadas.

**Solución:** Usa `dmesg -a` para ver todos los mensajes, incluidos los más antiguos. Comprueba también `sysctl kern.msgbuf_show_timestamp`.

**4. Problema:** `kldunload` dice "module busy"

**Causa:** Algo sigue usando tu módulo (muy improbable con este scaffold mínimo).

**Solución:** No es aplicable aquí, pero más adelante verás esto si los nodos de dispositivo siguen abiertos o los recursos no se han liberado.

### Buenas prácticas de compilación limpia

A medida que iteres sobre tu driver, adopta estos hábitos desde el principio:

**1. Limpia siempre antes de reconstruir:**

```bash
% make clean
% make
```

Esto garantiza que los archivos objeto obsoletos no contaminen tu build.

**2. Descarga el módulo antes de reconstruir:**

```bash
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
```

Si el módulo no está cargado, `kldunload` falla de forma inofensiva. El `|| true` evita que el shell se detenga.

**3. Usa un script de reconstrucción:**

Crea `~/drivers/myfirst/rebuild.sh`:

```bash
#!/bin/sh
#
# FreeBSD kernel module rebuild script
# Usage: ./rebuild_module.sh <module_name>
#

set -e

# Configuration
MODULE_NAME="${1}"

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Helper functions
print_step() {
    printf "${BLUE}==>${NC} ${1}\n"
}

print_success() {
    printf "${GREEN}✓${NC} ${1}\n"
}

print_error() {
    printf "${RED}✗${NC} ${1}\n" >&2
}

print_warning() {
    printf "${YELLOW}!${NC} ${1}\n"
}

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "This script must be run as root or with sudo"
        exit 1
    fi
}

is_module_loaded() {
    kldstat -q -n "${1}" 2>/dev/null
}

# Validate arguments
if [ -z "${MODULE_NAME}" ]; then
    print_error "Usage: $0 <module_name>"
    exit 1
fi

# Validate source file exists
if [ ! -f "${MODULE_NAME}.c" ]; then
    print_error "Source file '${MODULE_NAME}.c' not found in current directory"
    exit 1
fi

# Check if we have root privileges
check_root

# Check if Makefile exists
if [ ! -f "Makefile" ]; then
    print_error "Makefile not found in current directory"
    exit 1
fi

# Step 1: Unload module if loaded
print_step "Checking if module '${MODULE_NAME}' is loaded..."
if is_module_loaded "${MODULE_NAME}"; then
    print_warning "Module is loaded, unloading..."
    
    # Capture dmesg state before unload
    DMESG_BEFORE_UNLOAD=$(dmesg | wc -l)
    
    if kldunload "${MODULE_NAME}" 2>/dev/null; then
        print_success "Module unloaded successfully"
    else
        print_error "Failed to unload module"
        exit 1
    fi
    
    # Verify unload
    sleep 1
    if is_module_loaded "${MODULE_NAME}"; then
        print_error "Module still loaded after unload attempt"
        exit 1
    fi
    print_success "Verified: module removed from memory"
    
    # Check dmesg for unload messages
    DMESG_AFTER_UNLOAD=$(dmesg | wc -l)
    DMESG_UNLOAD_NEW=$((DMESG_AFTER_UNLOAD - DMESG_BEFORE_UNLOAD))
    
    if [ ${DMESG_UNLOAD_NEW} -gt 0 ]; then
        echo
        print_step "Kernel messages from unload:"
        dmesg | tail -n ${DMESG_UNLOAD_NEW}
        echo
    fi
else
    print_success "Module not loaded, proceeding..."
fi

# Step 2: Clean build artifacts
print_step "Cleaning build artifacts..."
if make clean; then
    print_success "Clean completed"
else
    print_error "Clean failed"
    exit 1
fi

# Step 3: Build module
print_step "Building module..."
if make; then
    print_success "Build completed"
else
    print_error "Build failed"
    exit 1
fi

# Verify module file exists
if [ ! -f "./${MODULE_NAME}.ko" ]; then
    print_error "Module file './${MODULE_NAME}.ko' not found after build"
    exit 1
fi

# Step 4: Load module
print_step "Loading module..."
DMESG_BEFORE=$(dmesg | wc -l)

if kldload "./${MODULE_NAME}.ko"; then
    print_success "Module load command executed"
else
    print_error "Failed to load module"
    exit 1
fi

# Step 5: Verify module is loaded
sleep 1
print_step "Verifying module load..."

if is_module_loaded "${MODULE_NAME}"; then
    print_success "Module is loaded in kernel"
    
    # Show module info
    echo
    kldstat | head -n 1
    kldstat | grep "${MODULE_NAME}"
else
    print_error "Module not found in kldstat output"
    exit 1
fi

# Step 6: Check kernel messages
echo
print_step "Recent kernel messages from load:"
DMESG_AFTER=$(dmesg | wc -l)
DMESG_NEW=$((DMESG_AFTER - DMESG_BEFORE))

if [ ${DMESG_NEW} -gt 0 ]; then
    dmesg | tail -n ${DMESG_NEW}
else
    print_warning "No new kernel messages"
    dmesg | tail -n 5
fi

echo
print_success "Module '${MODULE_NAME}' rebuilt and loaded successfully!"
```

Dale permisos de ejecución:

```bash
% chmod +x rebuild.sh
```

Ahora puedes iterar rápidamente:

```bash
% ./rebuild.sh myfirst
```

Este script descarga, limpia, construye, carga y muestra los mensajes recientes del kernel, todo de una vez. Supone un gran ahorro de tiempo durante el desarrollo.

**Nota:** Puede que te preguntes si este script necesita ser tan complejo. Para un uso puntual, no. Sin embargo, el desarrollo de módulos del kernel implica ciclos repetidos de descargar-reconstruir-cargar, a menudo decenas de veces al día. Construirlo con un manejo de errores adecuado y con validaciones crea una herramienta que reutilizarás con confianza a lo largo de todo tu proceso de desarrollo, ahorrándote incontables horas más adelante. Y lo que es aún más importante: esta es una oportunidad perfecta para practicar la programación defensiva: validar entradas, comprobar errores en cada paso y proporcionar información clara cuando algo va mal. Estos hábitos te serán muy útiles en todo tu trabajo de desarrollo futuro.

### Punto de control de versiones

Antes de continuar, haz un commit de tu scaffold en Git (si utilizas control de versiones, y deberías):

```bash
% cd ~/drivers/myfirst
% git init
% git add Makefile myfirst.c
% git commit -m "Initial scaffold: loads and unloads cleanly"
```

Esto te da un estado conocido y funcional al que volver si rompes algo más adelante. Si usas un repositorio remoto (GitHub, GitLab, etc.), puedes enviar estos cambios con `git push`, aunque no es obligatorio para aprovechar los beneficios del control de versiones local.

### ¿Qué sigue?

Ahora tienes un scaffold funcional: un módulo que compila, se carga y se descarga. Todavía no es un driver Newbus y no crea ninguna superficie visible para el usuario, pero es una base sólida.

En la siguiente sección añadiremos la **integración con Newbus**, transformando este módulo sencillo en un driver de pseudodispositivo propiamente dicho que se registra en el árbol de dispositivos e implementa los métodos del ciclo de vida `probe()` y `attach()`.

## Newbus: lo justo para conectar

Has construido un scaffold que se carga y se descarga. Ahora lo transformaremos en un **driver Newbus**, uno que se registra en el framework de dispositivos de FreeBSD y sigue el ciclo de vida estándar `identify` / `probe` / `attach` / `detach`.

Aquí es donde tu driver deja de ser un módulo pasivo y empieza a comportarse como un driver de dispositivo real. Al final de esta sección tendrás un driver que:

- Se registra como pseudodispositivo en el bus `nexus`
- Proporciona un método `identify()` que crea el dispositivo `myfirst` en el bus
- Implementa `probe()` para reclamar el dispositivo
- Implementa `attach()` para inicializarse (aunque la inicialización sea mínima por ahora)
- Implementa `detach()` para limpiar
- Registra los eventos del ciclo de vida de forma correcta

Lo mantenemos **justo en lo necesario** para mostrar el patrón. Sin asignación de recursos todavía, sin nodos de dispositivo, sin sysctls. Eso llega en secciones posteriores. Por ahora, céntrate en entender **cómo Newbus llama a tu código** y **qué debe hacer cada método**.

### ¿Por qué Newbus?

FreeBSD usa Newbus para gestionar el descubrimiento de dispositivos, la correspondencia entre drivers y dispositivos, y el ciclo de vida. Incluso para los pseudodispositivos (dispositivos puramente software sin hardware subyacente), seguir el patrón Newbus garantiza:

- Comportamiento coherente en todos los drivers
- Integración correcta con el árbol de dispositivos
- Gestión fiable del ciclo de vida (attach / detach / suspend / resume)
- Compatibilidad con herramientas como `devinfo` y `kldunload`

**Modelo mental:** Newbus es el departamento de recursos humanos del kernel. Abre nuevas vacantes (identify), entrevista a los drivers para cada vacante (probe), contrata al más adecuado (attach) y gestiona las bajas (detach). Para hardware real, el bus publica la vacante de forma automática. Para un pseudodispositivo, tu driver también escribe la descripción del puesto, que es exactamente para lo que sirve `identify`.

### El patrón mínimo de Newbus

Todo driver Newbus sigue esta estructura:

1. **Definir los métodos del dispositivo** (`identify`, `probe`, `attach`, `detach`) como funciones
2. **Crear una tabla de métodos** que mapea los nombres de métodos de Newbus a tus funciones
3. **Declarar una estructura de driver** que incluye la tabla de métodos y el tamaño del softc
4. **Registrar el driver** con `DRIVER_MODULE()`

En los drivers que se conectan a un bus real, como `pci` o `usb`, el bus enumera el hardware por su cuenta y pregunta a cada driver registrado "¿es este dispositivo tuyo?" a través de `probe`. Un pseudodispositivo no tiene hardware que enumerar, así que tenemos que decirle al bus que el dispositivo existe. Ese es el trabajo de `identify`. Lo introduciremos en el paso 4, después de que probe y attach estén en su sitio, para que el papel de cada método quede claro antes de que el archivo se llene.

Veamos cada pieza paso a paso.

### Paso 1: incluir los headers de Newbus

Al principio de `myfirst.c`, añade estos includes (sustituyendo o completando los includes mínimos del scaffold):

```c
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>        /* For device_t, Newbus APIs */
#include <sys/conf.h>       /* For cdevsw (used later) */
```

**Qué proporcionan:**

- `<sys/bus.h>` - Tipos principales de Newbus (`device_t`, `device_method_t`) y funciones (`device_printf`, `device_get_softc`, etc.)
- `<sys/conf.h>` - Estructuras del switch de dispositivos de caracteres (las usaremos cuando creemos nodos en `/dev`)

### Paso 2: definir tu softc

El **softc** (contexto de software) es la estructura de datos privada de tu driver por dispositivo. Aunque de momento no almacenamos nada interesante, **todo driver Newbus tiene uno**.

Añade esto cerca del principio de `myfirst.c`, después de los includes:

```c
/*
 * Driver softc (software context).
 *
 * One instance of this structure exists per device.
 * Newbus allocates and zeroes it for us.
 */
struct myfirst_softc {
        device_t        dev;            /* Back-pointer to device_t */
        uint64_t        attach_time;    /* When we attached (ticks) */
        int             is_ready;       /* Simple flag */
};
```

**¿Por qué estos campos?**

- `dev` - Puntero de retorno por comodidad. Te permite llamar a `device_printf(sc->dev, ...)` sin tener que pasar `dev` en todas partes.
- `attach_time` - Estado de ejemplo. Registraremos cuándo se ejecutó `attach()`.
- `is_ready` - Otro flag de ejemplo. Muestra cómo harías un seguimiento del estado del driver.

**Idea clave:** Nunca llamas tú mismo a `malloc()` ni a `free()` sobre el softc. Newbus lo hace de forma automática en función del tamaño que declares en la estructura del driver.

### Paso 3: implementar probe

El método `probe()` responde a una sola pregunta: **"¿Coincide este driver con este dispositivo?"**

Para un pseudodispositivo, la respuesta es siempre sí (no comprobamos IDs PCI ni firmas de hardware). Aun así, implementamos `probe()` para seguir el patrón y establecer una descripción del dispositivo.

Añade esta función:

```c
/*
 * Probe method.
 *
 * Called by Newbus to see if this driver wants to handle this device.
 * For a pseudo-device created by our own identify method, we always accept.
 *
 * The return value is a priority. Higher values win when several drivers
 * are willing to take the same device. ENXIO means "not mine, reject".
 */
static int
myfirst_probe(device_t dev)
{
        device_set_desc(dev, "My First FreeBSD Driver");
        return (BUS_PROBE_DEFAULT);
}
```

**Línea por línea:**

- `device_set_desc()` establece una descripción legible por humanos. Aparece en `devinfo -v` y en los mensajes de attach. La cadena debe permanecer válida durante toda la vida del dispositivo, así que pasa siempre una cadena literal. Si alguna vez necesitas una descripción construida dinámicamente, usa `device_set_desc_copy()`.
- `return (BUS_PROBE_DEFAULT)` le dice a Newbus "yo me encargo de este dispositivo, con la prioridad estándar del sistema operativo base".

**Disciplina en probe:**

- **No** asignes recursos en `probe()`. Si gana otro driver, tus recursos quedarían sin liberar.
- **No** toques el hardware en `probe()` (aquí no es relevante, pero es esencial para drivers de hardware real).
- **Sí** responde rápido. probe se llama con frecuencia durante el arranque y los eventos de conexión en caliente.

**Una nota sobre los valores de prioridad en probe.** Cuando varios drivers están dispuestos a hacerse cargo del mismo dispositivo, el kernel elige aquel cuyo `probe()` devolvió el valor **más alto**. Las constantes en `<sys/bus.h>` reflejan ese orden, siendo las ofertas más específicas numéricamente más grandes:

| Constante               | Valor (FreeBSD 14.3) | Cuándo usarla                                                                  |
|-------------------------|----------------------|--------------------------------------------------------------------------------|
| `BUS_PROBE_SPECIFIC`    | `0`                  | Solo este driver puede gestionar este dispositivo                              |
| `BUS_PROBE_VENDOR`      | `-10`                | Driver del fabricante, supera al driver genérico de clase                      |
| `BUS_PROBE_DEFAULT`     | `-20`                | Driver estándar del sistema operativo base para esta clase                     |
| `BUS_PROBE_LOW_PRIORITY`| `-40`                | Driver más antiguo o menos deseable                                            |
| `BUS_PROBE_GENERIC`     | `-100`               | Driver de reserva genérico                                                     |
| `BUS_PROBE_NOWILDCARD`  | negativo muy grande  | Solo coincide con dispositivos creados explícitamente (p. ej., por identify)   |

`BUS_PROBE_DEFAULT` es la elección correcta para un driver típico, incluido el nuestro: identificamos nuestro propio dispositivo por nombre en `identify()`, así que no existe ningún competidor real, y el valor es suficientemente alto para que nadie nos supere.

### Paso 4: implementar attach

El método `attach()` es donde **inicializas tu driver**. Se asignan recursos, se configura el hardware, se crean los nodos de dispositivo. Por ahora simplemente registraremos un mensaje y rellenaremos el softc.

Añade esta función:

```c
/*
 * Attach method.
 *
 * Called after probe succeeds. Initialize the driver here.
 */
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->attach_time = ticks;  /* Record when we attached */
        sc->is_ready = 1;

        device_printf(dev, "Attached successfully at tick %lu\n",
            (unsigned long)sc->attach_time);

        return (0);
}
```

**Qué hace esto:**

- `device_get_softc(dev)` - Recupera el softc que Newbus ha asignado por nosotros (inicialmente a cero).
- `sc->dev = dev` - Guarda el puntero de retorno `device_t` por comodidad.
- `sc->attach_time = ticks` - Registra el contador de ticks actual del kernel (una marca de tiempo sencilla).
- `sc->is_ready = 1` - Activa un flag (aún no se usa, pero muestra cómo harías un seguimiento del estado).
- `device_printf()` - Registra el evento de attach con el prefijo del nombre de nuestro dispositivo.
- `return (0)` - Éxito. Un valor distinto de cero indicaría un fallo y abortaría el attach.

**Disciplina en attach:**

- **Sí** asigna recursos aquí (memoria, locks, mappings de hardware).
- **Sí** crea las superficies de usuario (nodos en `/dev`, interfaces de red, etc.).
- **Sí** gestiona los fallos con elegancia. Si algo va mal, deshaz lo que empezaste y devuelve un código de error.
- **No** toques el espacio de usuario todavía. attach se ejecuta durante la carga del módulo o el descubrimiento del dispositivo, antes de que cualquier programa de usuario pueda interactuar contigo.

**Vista previa del manejo de errores:**

En este momento attach no puede fallar (no estamos haciendo nada que pueda salir mal). Las secciones posteriores añadirán asignación de recursos y verás cómo deshacer los cambios en caso de fallo.

### Paso 5: implementar detach

El método `detach()` es el inverso de `attach()`: desmonta lo que construiste, libera lo que reclamaste y no dejes ningún rastro.

Añade esta función:

```c
/*
 * Detach method.
 *
 * Called when the driver is being unloaded or the device is removed.
 * Clean up everything you set up in attach().
 */
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        device_printf(dev, "Detaching (was attached for %lu ticks)\n",
            (unsigned long)(ticks - sc->attach_time));

        sc->is_ready = 0;

        return (0);
}
```

**Qué hace esto:**

- Recupera el softc (sabemos que existe porque attach tuvo éxito).
- Registra cuánto tiempo estuvo activo el driver (los `ticks` actuales menos `attach_time`).
- Limpia el flag `is_ready` (no es estrictamente necesario, ya que el softc se liberará pronto, pero es buena práctica).
- Devuelve 0 (éxito).

**Disciplina en detach:**

- **Sí** libera todos los recursos (locks destruidos, memoria liberada, nodos de dispositivo destruidos).
- **Sí** asegúrate de que ninguna I/O activa ni ningún callback pueda alcanzar tu código después de que detach retorne.
- **Sí** devuelve `EBUSY` si el dispositivo está en uso y no puede desconectarse todavía (p. ej., nodos de dispositivo abiertos).
- **No** asumas que el softc sigue siendo válido después de que detach retorne. Newbus lo liberará.

**Por qué importa detach:**

Las implementaciones deficientes de detach son la fuente número uno de kernel panics en la descarga. Si olvidas destruir un lock, liberar un recurso o eliminar un callback, el sistema se bloqueará cuando ese recurso sea accedido después de que tu código haya desaparecido.

### Paso 6: implementar identify

Tenemos probe, attach y detach. Le dicen al kernel **qué hacer** cuando aparece un dispositivo `myfirst`. Pero todavía no existe ningún dispositivo `myfirst` en el bus nexus, y nexus no tiene forma de inventar uno. Tenemos que crear el dispositivo nosotros mismos en el momento en que se registra nuestro driver. Eso es exactamente lo que hace un método `identify`.

Añade esta función:

```c
/*
 * Identify method.
 *
 * Called by Newbus once, right after the driver is registered with the
 * parent bus. Its job is to create child devices that this driver will
 * then probe and attach.
 *
 * Real hardware drivers usually do not need an identify method, because
 * the bus (PCI, USB, ACPI, ...) enumerates devices on its own. A pseudo
 * device has nothing for the bus to find, so we add our single device
 * here, by name.
 */
static void
myfirst_identify(driver_t *driver, device_t parent)
{
        if (device_find_child(parent, driver->name, -1) != NULL)
                return;
        if (BUS_ADD_CHILD(parent, 0, driver->name, -1) == NULL)
                device_printf(parent, "myfirst: BUS_ADD_CHILD failed\n");
}
```

**Línea a línea:**

- `device_find_child(parent, driver->name, -1)` comprueba si ya existe un dispositivo `myfirst` bajo `parent`. Si no hiciéramos esta comprobación, recargar el módulo (o cualquier segundo recorrido por el bus) crearía dispositivos duplicados.
- `BUS_ADD_CHILD(parent, 0, driver->name, -1)` le pide al bus padre que cree un nuevo dispositivo hijo llamado `myfirst`, en el orden `0`, con un número de unidad asignado automáticamente. Tras esta llamada, Newbus ejecutará nuestro `probe` contra el nuevo hijo y, si probe lo acepta, ejecutará attach.
- Registramos el fallo pero no provocamos un panic. `BUS_ADD_CHILD` puede fallar bajo presión de memoria, y la ausencia de un pseudo-dispositivo no debería tumbar el sistema.

**Dónde encaja esto.** `identify` se ejecuta una sola vez por driver y por bus, cuando el driver se enlaza por primera vez a ese bus. Después de identify, toma el control la maquinaria normal de probe y attach del bus. Es el mismo patrón que utilizan drivers como `cryptosoft`, `aesni` y `snd_dummy` en el árbol de código fuente de FreeBSD, que podrás consultar más adelante como referencia.

### Paso 7: Crea la tabla de métodos

Ahora conecta tus funciones con los nombres de métodos de Newbus. Añade esto después de tus definiciones de función:

```c
/*
 * Device method table.
 *
 * Maps Newbus method names to our functions.
 */
static device_method_t myfirst_methods[] = {
        /* Device interface */
        DEVMETHOD(device_identify,      myfirst_identify),
        DEVMETHOD(device_probe,         myfirst_probe),
        DEVMETHOD(device_attach,        myfirst_attach),
        DEVMETHOD(device_detach,        myfirst_detach),

        DEVMETHOD_END
};
```

**Qué significa esta tabla:**

- `DEVMETHOD(device_identify, myfirst_identify)` indica: «cuando el bus invita a cada driver a crear sus dispositivos, ejecuta `myfirst_identify()`.»
- `DEVMETHOD(device_probe, myfirst_probe)` indica: «cuando el kernel llama a `DEVICE_PROBE(dev)`, ejecuta `myfirst_probe()`.»
- Lo mismo para attach y detach.
- `DEVMETHOD_END` termina la tabla y es obligatorio.

**Entre bastidores:** El macro `DEVMETHOD()` y el sistema kobj (objetos del kernel) generan el código de enlace que despacha hacia tus funciones. No necesitas entender los detalles internos; basta con saber que esta tabla es la manera en que Newbus localiza tu código.

### Paso 8: Declara el driver

Une todo en una estructura `driver_t`:

```c
/*
 * Driver declaration.
 *
 * Specifies our method table and softc size.
 */
static driver_t myfirst_driver = {
        "myfirst",              /* Driver name */
        myfirst_methods,        /* Method table */
        sizeof(struct myfirst_softc)  /* Softc size */
};
```

**Parámetros:**

- `"myfirst"`: nombre del driver (aparece en los registros y como prefijo del nombre del dispositivo).
- `myfirst_methods`: puntero a la tabla de métodos que acabas de crear.
- `sizeof(struct myfirst_softc)`: indica a Newbus cuánta memoria asignar por dispositivo.

**¿Por qué el tamaño del softc?** Newbus asigna un softc por instancia de dispositivo. Al declarar el tamaño aquí, nunca tendrás que asignarlo ni liberarlo manualmente; Newbus gestiona su ciclo de vida.

### Paso 9: Regístrate con DRIVER_MODULE

Sustituye el antiguo macro `DECLARE_MODULE()` del andamiaje por esto:

```c
/*
 * Driver registration.
 *
 * Attach this driver under the nexus bus. Our identify method will
 * create the actual myfirst child device when the module loads.
 */

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
MODULE_VERSION(myfirst, 1);
```

**Qué hace esto:**

- `DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0)` registra `myfirst` como un driver dispuesto a enlazarse por debajo del bus `nexus`. Los dos ceros finales son un manejador de eventos de módulo opcional y su argumento; nuestro driver mínimo no los necesita.
- `MODULE_VERSION(myfirst, 1)` marca el módulo con la versión 1, de modo que otros módulos puedan declarar una dependencia sobre él.

**¿Por qué `nexus`?**

`nexus` es el bus raíz de FreeBSD, la cima del árbol de dispositivos de cada arquitectura. El capítulo 6 te advirtió, con razón, de que `nexus` raramente es el padre correcto para un driver de hardware real: un driver PCI debe situarse bajo `pci`, un driver USB bajo `usbus`, etcétera. Los pseudodispositivos son distintos. No tienen bus físico, por lo que la convención en el árbol de código fuente de FreeBSD es enlazarlos a `nexus` y crear el dispositivo hijo por sí mismos mediante un método `identify`. Esto es exactamente lo que hacen `cryptosoft`, `aesni` y `snd_dummy`, y exactamente lo que estamos haciendo aquí.

### Paso 10: Elimina el antiguo cargador de módulos

Ya no necesitas la función `myfirst_loader()` ni la estructura `moduledata_t` del andamiaje. Newbus se encarga ahora del ciclo de vida del módulo a través de `identify`, `probe`, `attach` y `detach`. Elimina esas partes antiguas por completo.

Tu archivo `myfirst.c` debe contener ahora:

- Includes
- Estructura softc
- `myfirst_identify()`
- `myfirst_probe()`
- `myfirst_attach()`
- `myfirst_detach()`
- Tabla de métodos
- Estructura del driver
- `DRIVER_MODULE()` y `MODULE_VERSION()`

Sin manejador de eventos `MOD_LOAD`.

### Paso 11: Ajusta el Makefile

Añade esta línea a tu Makefile:

```makefile
# Required for Newbus drivers: generates device_if.h and bus_if.h
SRCS+=   device_if.h bus_if.h
```

**Por qué es necesario:**

El framework Newbus de FreeBSD utiliza un sistema de despacho de métodos construido sobre kobj. Las entradas `DEVMETHOD()` de tu tabla de métodos hacen referencia a identificadores de método declarados en las cabeceras generadas `device_if.h` y `bus_if.h`. `bsd.kmod.mk` sabe cómo construirlas a partir de `/usr/src/sys/kern/device_if.m` y `/usr/src/sys/kern/bus_if.m`, pero solo lo hace si las incluyes en `SRCS`. Si olvidas esta línea obtendrás un error confuso sobre identificadores de método desconocidos al compilar.

### Compilar y probar el driver Newbus

Vamos a compilar y probar:

**1. Limpiar y compilar:**

```bash
% make clean
% make
```

No deberías ver ningún error.

**2. Cargar el módulo:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
```

Observa:

- El nombre del dispositivo es `myfirst0` (nombre del driver + número de unidad).
- Se ha enlazado "on nexus0" (el bus padre).
- Ha aparecido tu mensaje de attach personalizado.

**3. Comprobar el árbol de dispositivos:**

```bash
% devinfo -v | grep myfirst
    myfirst0
```

Tu driver forma ahora parte del árbol de dispositivos.

**4. Descargar:**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst0: Detaching (was attached for 5432 ticks)
```

Tu mensaje de detach muestra cuánto tiempo estuvo enlazado el driver.

**5. Verificar que ha desaparecido:**

```bash
% devinfo -v | grep myfirst
(no output)
```

### ¿Qué ha cambiado?

Comparado con el andamiaje, tu driver ahora:

- **Se registra con Newbus** en lugar de usar un cargador de módulos simple.
- **Añade un dispositivo hijo** (`myfirst0`) al árbol de dispositivos mediante `identify`.
- **Sigue el ciclo de vida identify / probe / attach / detach** en lugar del simple carga/descarga.
- **Asigna y gestiona un softc** de forma automática.

Este es el **patrón fundamental** de todo driver de FreeBSD. Domínalo y el resto es simplemente añadir capas.

### Errores comunes en Newbus (y cómo evitarlos)

**Error 0: Olvidar el método identify en un pseudodispositivo**

**Síntoma:** `kldload` tiene éxito, pero no aparece ningún dispositivo `myfirst0`, no hay mensaje de probe en `dmesg` y `devinfo` no muestra nada bajo `nexus0`. El driver compiló y se cargó, pero nunca se enlazó.

**Causa:** El driver se registró con `DRIVER_MODULE(..., nexus, ...)` pero no se proporcionó ningún método `device_identify`. Nexus no tiene nada que enumerar, por lo que probe y attach nunca se llaman.

**Solución:** Añade el método `identify` mostrado en el paso 6 y coloca `DEVMETHOD(device_identify, myfirst_identify)` en la tabla de métodos. Esta es la razón más común por la que el driver de pseudodispositivo de un principiante «se carga pero no hace nada».

---

**Error 1: Asignar recursos en probe**

**Incorrecto:**

```c
static int
myfirst_probe(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        sc->something = malloc(...);  /* BAD! */
        return (BUS_PROBE_DEFAULT);
}
```

**Por qué es incorrecto:** Si probe falla o gana otro driver, tu asignación queda sin liberar.

**Correcto:** Asigna en `attach()`, donde ya sabes que el driver ha sido seleccionado.

---

**Error 2: Olvidar devolver 0 desde attach**

**Incorrecto:**

```c
static int
myfirst_attach(device_t dev)
{
        /* ... setup ... */
        /* (missing return statement) */
}
```

**Por qué es incorrecto:** El compilador podría advertirte, pero el valor de retorno es indefinido. Podrías devolver basura accidentalmente, causando que attach falle de manera misteriosa.

**Correcto:** Termina siempre attach con `return (0)` en caso de éxito o `return (error_code)` en caso de fallo.

---

**Error 3: No limpiar en detach**

**Incorrecto:**

```c
static int
myfirst_detach(device_t dev)
{
        device_printf(dev, "Detaching\n");
        return (0);
        /* (forgot to free resources, destroy locks, etc.) */
}
```

**Por qué es incorrecto:** Los recursos quedan sin liberar. Los locks permanecen activos. La siguiente carga podría causar un pánico.

**Correcto:** Detach debe deshacer todo lo que hizo attach. Cubriremos el patrón de limpieza en detalle en la sección de manejo de errores.

### Diagrama de temporización del ciclo de vida en Newbus

```text
[ Boot or kldload ]
        |
        v
   identify(parent)  --> "What devices does this driver provide?"
        |                 (Pseudo-devices: BUS_ADD_CHILD here)
        |                 (Real hardware: usually omitted)
        v
    probe(dev)  --> "Is this device mine?"
        |            (Check IDs, set description)
        | (return a probe priority such as BUS_PROBE_DEFAULT)
        v
    attach(dev)  --> "Initialize and prepare for use"
        |            (Allocate resources, create surfaces)
        |            (If fails, undo what was done, return error)
        v
  [ Device ready, normal operation ]
        |
        | (time passes, I/O happens, sysctls read, etc.)
        |
        v
    detach(dev)  --> "Shutdown and cleanup"
        |            (Destroy surfaces, release resources)
        |            (Return EBUSY if still in use)
        v
    [ Module unloaded or device gone ]
```

**Idea clave:** Cada paso es distinto. Identify crea el dispositivo, probe lo reclama, attach lo inicializa, detach lo limpia. No difumines los límites.

### Comprobación rápida

Antes de continuar, asegúrate de que puedes responder a estas preguntas:

1. **¿Dónde asigno memoria para el estado del driver?**
   Respuesta: En `attach()`, o simplemente usa el softc (Newbus lo asigna por ti).

2. **¿Qué devuelve `device_get_softc()`?**
   Respuesta: Un puntero a los datos privados por dispositivo de tu driver (en este caso, `struct myfirst_softc *`).

3. **¿Cuándo se llama a probe?**
   Respuesta: Durante la enumeración de dispositivos. Para un bus real, eso ocurre cuando el bus descubre un dispositivo. Para nuestro pseudodispositivo, ocurre justo después de que nuestro método `identify` llama a `BUS_ADD_CHILD()` para colocar un dispositivo `myfirst` en el bus nexus.

4. **¿Qué debe hacer detach?**
   Respuesta: Deshacer todo lo que hizo attach, liberar recursos y asegurarse de que ningún camino de código pueda alcanzar el driver después.

5. **¿Por qué usamos `nexus` para este driver y por qué necesita un método `identify`?**
   Respuesta: Porque es un pseudodispositivo sin bus físico. `nexus` es el padre convencional para dispositivos puramente software, pero nexus no tiene dispositivos que enumerar, así que creamos los nuestros propios mediante `identify`.

Si esas respuestas tienen sentido para ti, estás listo para la siguiente sección: añadir gestión de estado real con el softc.

---

## softc y estado del ciclo de vida

Has visto la estructura softc declarada, asignada y recuperada, pero todavía no hemos hablado de **por qué existe** ni de **cómo usarla correctamente**. En esta sección exploraremos el patrón softc en profundidad: qué debe contener, cómo inicializarlo y acceder a él de forma segura, y cómo evitar los errores más comunes.

El softc es la **memoria** de tu driver. Cada recurso, cada lock, cada estadística y cada indicador vive aquí. Hacerlo bien es la diferencia entre un driver fiable y uno que entra en pánico bajo carga.

### ¿Qué es el softc?

El **softc** (contexto de software) es una estructura por dispositivo que almacena todo lo que tu driver necesita para funcionar. Piensa en él como el «espacio de trabajo» o «cuaderno» del driver: una instancia por dispositivo que contiene todo el estado que hace funcionar a ese dispositivo concreto.

**Propiedades clave:**

- **Por dispositivo:** si tu driver gestiona múltiples dispositivos (p. ej., `myfirst0`, `myfirst1`), cada uno tiene su propio softc.
- **Asignado por el kernel:** tú declaras el tipo de estructura y su tamaño; Newbus asigna la memoria y la pone a cero.
- **Ciclo de vida:** existe desde la creación del dispositivo (antes de `attach()`) hasta su eliminación (después de `detach()`).
- **Patrón de acceso:** recupéralo con `device_get_softc(dev)` al inicio de cada método.

**¿Por qué no variables globales?**

Las variables globales no pueden gestionar múltiples dispositivos. Si almacenaras el estado en globales, `myfirst0` y `myfirst1` se sobreescribirían mutuamente los datos. El patrón softc resuelve esto de forma elegante: cada dispositivo tiene su propio estado aislado.

### ¿Qué debe contener el softc?

Un softc bien diseñado contiene:

**1. Identificación y gestión interna**

- `device_t dev`: puntero de vuelta al dispositivo (para registros y callbacks).
- `int unit`: número de unidad del dispositivo (se puede extraer de `dev`, pero es útil tenerlo en caché).
- `char name[16]`: cadena con el nombre del dispositivo, si la necesitas con frecuencia.

**2. Recursos**

- `struct resource *mem_res`: regiones MMIO (para drivers de hardware).
- `int mem_rid`: identificador de recurso para memoria.
- `struct resource *irq_res`: recurso de interrupción.
- `void *irq_handler`: cookie del manejador de interrupción.
- `bus_dma_tag_t dma_tag`: etiqueta DMA (para drivers que realizan DMA).

**3. Primitivas de sincronización**

- `struct mtx mtx`: mutex para proteger el estado compartido.
- `struct sx sx`: lock compartido/exclusivo si es necesario.
- `struct cv cv`: variable de condición para dormir/despertar.

**4. Indicadores de estado del dispositivo**

- `int is_attached`: se activa en attach, se borra en detach.
- `int is_open`: se activa cuando el nodo `/dev` está abierto.
- `uint32_t flags`: campo de bits para estado diverso (en ejecución, suspendido, error, etc.).

**5. Estadísticas y contadores**

- `uint64_t tx_packets`: paquetes transmitidos (ejemplo de driver de red).
- `uint64_t rx_bytes`: bytes recibidos.
- `uint64_t errors`: recuento de errores.
- `time_t last_reset`: cuándo se borraron las estadísticas por última vez.

**6. Datos específicos del driver**

- Registros de hardware, colas, buffers, estructuras de trabajo y cualquier cosa específica del funcionamiento de tu driver.

**Qué no debe contener:**

- **Buffers grandes:** el softc vive en memoria del kernel (anclada, no paginable). Los buffers grandes deben asignarse por separado con `malloc()` o `contigmalloc()` y referenciarse desde el softc mediante un puntero.
- **Datos constantes:** usa arrays globales `const` o tablas estáticas en su lugar.
- **Variables temporales:** las variables locales de función son suficientes. No llenes el softc de temporales por operación.

### Nuestro softc de myfirst (ejemplo mínimo)

Revisemos y ampliemos nuestra definición de softc:

```c
struct myfirst_softc {
        device_t        dev;            /* Back-pointer */
        int             unit;           /* Device unit number */

        struct mtx      mtx;            /* Protects shared state */

        uint64_t        attach_ticks;   /* When attach() ran */
        uint64_t        open_count;     /* How many times opened */
        uint64_t        bytes_read;     /* Bytes read from device */

        int             is_attached;    /* 1 if attach succeeded */
        int             is_open;        /* 1 if /dev node is open */
};
```

**Campo por campo:**

- `dev`: puntero de vuelta estándar. Casi todos los drivers lo incluyen.
- `unit`: número de unidad en caché (obtenido de `device_get_unit(dev)`). Opcional, pero práctico.
- `mtx`: mutex para proteger el acceso concurrente. Aunque todavía no ejercitamos la concurrencia, incluirlo ahora fomenta buenos hábitos.
- `attach_ticks`: momento en que se produjo el attach (ticks del kernel). Estado de ejemplo sencillo.
- `open_count` / `bytes_read`: contadores. Los drivers reales los rastrean para estadísticas y observabilidad.
- `is_attached` / `is_open`: indicadores para el estado del ciclo de vida. Útiles en la comprobación de errores.

**¿Por qué un mutex ahora?**

Aunque nuestro driver mínimo todavía no lo necesita, incluir el mutex enseña el **patrón**. Todo driver acabará necesitando locking, y es más fácil diseñarlo desde el principio que incorporarlo después.

Todavía no usamos el mutex para proteger datos compartidos de verdad. La concurrencia, el orden de los locks y los peligros del deadlock llegan en la Parte 3. Por ahora, el lock está aquí para establecer el patrón de ciclo de vida y para que el orden del detach sea seguro. Consulta el peligro de «destruir locks mientras los threads aún los tienen» del Capítulo 6.

### Inicialización del softc en attach()

Newbus pone el softc a cero antes de llamar a `attach()`, pero aún así necesitas inicializar ciertos campos de forma explícita (locks, punteros de retorno, flags).

A continuación se muestra el `attach()` actualizado:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        /* Initialize back-pointer and unit */
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Record attach time */
        sc->attach_ticks = ticks;

        /* Set state flags */
        sc->is_attached = 1;
        sc->is_open = 0;

        /* Initialize counters */
        sc->open_count = 0;
        sc->bytes_read = 0;

        device_printf(dev, "Attached at tick %lu\n",
            (unsigned long)sc->attach_ticks);

        return (0);
}
```

**Qué ha cambiado:**

- **`mtx_init()`**: Inicializa el mutex. Parámetros:
  - `&sc->mtx`: Dirección del campo mutex en el softc.
  - `device_get_nameunit(dev)`: Devuelve una cadena como "myfirst0" (se usa en la depuración de locks).
  - `"myfirst"`: Nombre del tipo de lock (aparece en las trazas de locks).
  - `MTX_DEF`: Mutex estándar (en contraposición al spin mutex).
- **`sc->is_attached = 1`**: Flag que indica que ya estamos listos.
- **Inicialización de contadores**: Se ponen a cero de forma explícita (aunque Newbus ya ha puesto a cero el softc entero, hacerlo así de manera explícita documenta la intención).

**Disciplina:** Inicializa todos los campos que se comprobarán más adelante. No des por sentado que "cero significa no inicializado" tiene siempre la semántica correcta (para flags, quizás; para punteros, desde luego que no).

### Destrucción del softc en detach()

En `detach()`, debes deshacer todo lo que hizo `attach()`. En lo que respecta al softc, eso significa:

- Destruir los locks (mutexes, sx, cv, etc.)
- Liberar cualquier memoria o recurso al que apunte el softc
- Limpiar los flags (no es estrictamente necesario, pero es una buena práctica)

`detach()` actualizado:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;
        uint64_t uptime;

        sc = device_get_softc(dev);

        /* Calculate how long we were attached */
        uptime = ticks - sc->attach_ticks;

        /* Refuse detach if device is open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach while device is open\n");
                return (EBUSY);
        }

        /* Log stats before shutting down */
        device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times, read %lu bytes\n",
            (unsigned long)uptime,
            (unsigned long)sc->open_count,
            (unsigned long)sc->bytes_read);

        /* Destroy the mutex */
        mtx_destroy(&sc->mtx);

        /* Clear attached flag */
        sc->is_attached = 0;

        return (0);
}
```

**Qué hay de nuevo:**

- **`if (sc->is_open) return (EBUSY)`**: Rechaza el detach si el nodo de `/dev` sigue abierto. Esto previene cuelgues por acceso a recursos ya liberados.
- **Registro de estadísticas**: Muestra cuánto tiempo estuvo activo el driver y qué hizo.
- **`mtx_destroy(&sc->mtx)`**: **Crítico.** Cada `mtx_init()` debe tener su `mtx_destroy()` correspondiente, o se producirá una fuga de recursos de lock en el kernel.
- **Limpieza de flags**: No es estrictamente necesario (Newbus liberará el softc en breve), pero es buena programación defensiva.

**Error habitual:** Olvidar `mtx_destroy()`. Esto provoca pánicos de rastreo de locks en la siguiente carga si se usa un kernel con WITNESS o INVARIANTS.

### Acceso al softc desde otros métodos

Cada método del driver que necesita estado comienza de la misma forma:

```c
static int
myfirst_some_method(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        /* Now use sc-> to access state */
        ...
}
```

Este es el **patrón idiomático** que encontrarás en todos los drivers de FreeBSD. Una sola línea para entrar en el mundo de tu driver.

**¿Por qué no pasar el softc directamente?**

Los métodos de Newbus están definidos para recibir `device_t`. El softc es un detalle de implementación de tu driver. Al recuperarlo de forma consistente mediante `device_get_softc()`, el driver se mantiene flexible (podrías cambiar la estructura del softc sin cambiar las firmas de los métodos).

### Uso de locks para proteger el softc

Aunque todavía no hemos añadido operaciones concurrentes, veamos un avance del **patrón de locking** que usarás cuando llegue el momento.

**Patrón básico:**

```c
static void
myfirst_increment_counter(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        sc->open_count++;
        mtx_unlock(&sc->mtx);
}
```

**Reglas:**

- **Adquiere el lock antes de modificar estado compartido.**
- **Libera el lock en cuanto hayas terminado** (no lo mantengas más tiempo del necesario).
- **Nunca retornes con un lock adquirido** (salvo que estés usando patrones avanzados de traspaso de locks).
- **Documenta el orden de los locks** si mantienes varios a la vez (para evitar deadlock).

**Cuándo necesitarás esto:** En cuanto añadas `open()`, `read()`, `write()`, o cualquier método que pueda ejecutarse concurrentemente (programas de usuario llamando a tu driver desde varios threads, o manejadores de interrupciones actualizando estadísticas).

Por ahora, el mutex existe pero no se ejercita. Lo usaremos en secciones posteriores cuando añadamos puntos de entrada concurrentes.

### Buenas prácticas con el softc

**1. Mantenlo organizado**

Agrupa los campos relacionados:

```c
struct myfirst_softc {
        /* Identification */
        device_t        dev;
        int             unit;

        /* Synchronization */
        struct mtx      mtx;

        /* Resources */
        struct resource *mem_res;
        int             mem_rid;

        /* Statistics */
        uint64_t        tx_packets;
        uint64_t        rx_bytes;

        /* State flags */
        int             is_attached;
        int             is_open;
};
```

**2. Comenta los campos no evidentes**

```c
        int             pending_requests;  /* Must hold mtx to access */
        time_t          last_activity;     /* Protected by mtx */
```

**3. Usa tipos de anchura fija para los contadores**

```c
        uint64_t        packets;  /* Not "unsigned long" */
        uint32_t        errors;   /* Not "int" */
```

**¿Por qué?** Portabilidad. Los tamaños de `int` y `long` varían según la arquitectura. `uint64_t` siempre ocupa 64 bits.

**4. Evita el desperdicio por relleno (padding)**

El compilador inserta relleno para alinear los campos. Coloca primero los campos más grandes y luego los más pequeños:

```c
/* Good: no wasted padding */
struct example {
        uint64_t        big_counter;  /* 8 bytes */
        uint32_t        medium;       /* 4 bytes */
        uint32_t        medium2;      /* 4 bytes */
        uint16_t        small;        /* 2 bytes */
        uint8_t         tiny;         /* 1 byte */
        uint8_t         tiny2;        /* 1 byte */
};
```

**5. Pon a cero los campos que vayas a comprobar**

```c
        sc->is_open = 0;       /* Explicit, even though Newbus zeroed it */
        sc->bytes_read = 0;
```

**¿Por qué?** Claridad. Quien lea el código sabrá que *pretendías* el valor cero, no que confiabas en un cero implícito.

### Depuración de problemas con el softc

**Problema:** Pánico del kernel "NULL pointer dereference" en tu driver.

**Causa probable:** Olvidaste recuperar el softc, o lo recuperaste después de un punto donde `dev` podría ser inválido.

**Solución:** Escribe siempre `sc = device_get_softc(dev);` al inicio de cada método.

---

**Problema:** Pánico de mutex "already locked" o "not locked".

**Causa probable:** Olvidaste `mtx_init()` en `attach()` o hay llamadas a `mtx_lock()` y `mtx_unlock()` desparejadas.

**Solución:** Comprueba tus pares de init/destroy. Usa kernels con WITNESS habilitado (`options WITNESS` en la configuración del kernel) para detectar violaciones de locking.

---

**Problema:** Las estadísticas o los flags parecen aleatorios o corruptos.

**Causa probable:** Acceso concurrente sin locking, o acceso al softc después de que `detach()` lo haya liberado.

**Solución:** Asegúrate de que todo el estado compartido está protegido por el mutex. Asegúrate de que ninguna ruta de código (callbacks, temporizadores, threads) pueda llegar al driver después de que `detach()` retorne.

### Autocomprobación rápida

Antes de continuar, asegúrate de que entiendes:

1. **¿Qué es el softc?**
   Respuesta: Estructura de datos privada por dispositivo que contiene todo el estado del driver.

2. **¿Quién asigna el softc?**
   Respuesta: Newbus, a partir del tamaño declarado en la estructura `driver_t`.

3. **¿Cuándo debes inicializar el mutex?**
   Respuesta: En `attach()`, antes de cualquier código que pueda usarlo.

4. **¿Cuándo debes destruir el mutex?**
   Respuesta: En `detach()`, antes de que la función retorne.

5. **¿Por qué rechazamos el detach si `is_open` es verdadero?**
   Respuesta: Para evitar liberar recursos mientras programas de usuario todavía tienen el dispositivo abierto, lo que provocaría un cuelgue.

Si esas respuestas están claras, estás listo para añadir **disciplina de registro** en la siguiente sección.

---

## Etiqueta de registro e higiene del dmesg

Un driver bien comportado **habla cuando debe** y **calla cuando no debe**. Registrar demasiado satura el `dmesg` y dificulta la depuración; registrar demasiado poco deja a ciegas a los usuarios y desarrolladores cuando algo sale mal. Esta sección te enseña **cuándo, qué y cómo registrar** en un driver de FreeBSD.

Al terminar, sabrás:

- Qué eventos **deben** registrarse (attach, errores, cambios de estado críticos)
- Qué eventos **deberían** registrarse (información opcional a nivel de depuración)
- Qué eventos **no deben** registrarse nunca (spam por paquete u operación)
- Cómo usar `device_printf()` de forma efectiva
- Cómo crear registro con límite de tasa para rutas calientes
- Cómo hacer que tus registros sean legibles y útiles

### Por qué importa el registro

Cuando un driver se comporta mal, el `dmesg` suele ser el primer lugar donde desarrolladores y usuarios buscan. Un buen registro responde a preguntas como:

- ¿Se realizó el attach del driver correctamente?
- ¿Qué hardware encontró?
- ¿Se produjo algún error? ¿Por qué?
- ¿Está el dispositivo operativo o en estado de error?

Un registro deficiente satura la consola, oculta mensajes críticos u omite detalles importantes.

**Modelo mental:** El registro es como las notas que toma un médico durante una consulta. Escribe lo suficiente para diagnosticar problemas más adelante, pero no anotes cada latido.

### Las reglas de oro del registro en drivers

**Regla 1: Registra los eventos del ciclo de vida**

Registra siempre:

- Attach correcto (una línea por dispositivo)
- Fallos en el attach (con motivo)
- Detach correcto (opcional, pero recomendado)
- Fallos en el detach (con motivo)

**Ejemplo:**

```c
device_printf(dev, "Attached successfully\n");
device_printf(dev, "Failed to allocate memory resource: error %d\n", error);
```

---

**Regla 2: Registra los errores**

Cuando algo sale mal, **registra siempre qué ha ocurrido y por qué**. Incluye:

- Qué operación ha fallado
- Código de error (valor errno)
- Contexto (si procede)

**Ejemplo:**

```c
if (error != 0) {
        device_printf(dev, "Could not allocate IRQ resource: error %d\n", error);
        return (error);
}
```

**Mal ejemplo:**

```c
if (error != 0) {
        return (error);  /* User sees nothing! */
}
```

---

**Regla 3: Nunca registres en rutas calientes**

"Ruta caliente" es el código que se ejecuta con frecuencia durante la operación normal (cada paquete, cada interrupción, cada llamada de lectura/escritura).

**Nunca hagas esto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int flag)
{
        device_printf(dev, "Read called\n");  /* BAD: spams logs */
        ...
}
```

**¿Por qué?** Si un programa lee de tu dispositivo en un bucle, generará miles de líneas de registro por segundo, dejando la consola inutilizable.

**Cuándo registrar eventos de ruta caliente:** Solo durante el desarrollo con fines de depuración, y protegido por un flag de depuración o un sysctl deshabilitado por defecto.

---

**Regla 4: Usa device_printf() para mensajes específicos del dispositivo**

`device_printf()` antepone automáticamente a tu mensaje el nombre del dispositivo:

```c
device_printf(dev, "Interrupt timeout\n");
```

Salida:

```text
myfirst0: Interrupt timeout
```

Esto deja claro inmediatamente **qué dispositivo** está hablando, especialmente cuando existen varias instancias.

**No uses `printf()` a secas:**

```c
printf("Interrupt timeout\n");  /* Which device? Unknown. */
```

---

**Regla 5: Limita la tasa de advertencias en rutas de error repetitivas**

Si un error puede repetirse rápidamente (por ejemplo, un timeout de DMA en cada trama), limita su tasa:

```c
static int
myfirst_check_fifo(struct myfirst_softc *sc)
{
        if (fifo_is_full(sc)) {
                if (sc->log_fifo_full == 0) {
                        device_printf(sc->dev, "FIFO full, dropping packets\n");
                        sc->log_fifo_full = 1;  /* Only log once until cleared */
                }
                return (ENOSPC);
        }
        sc->log_fifo_full = 0;  /* Clear flag when condition resolves */
        return (0);
}
```

**Este patrón registra la primera aparición, suprime las repeticiones y vuelve a registrar cuando la condición cambia.**

---

**Regla 6: Sé conciso y útil**

Compara:

**Mal:**

```c
device_printf(dev, "Something went wrong in the code here\n");
```

**Bien:**

```c
device_printf(dev, "Failed to map BAR0 MMIO region: error %d\n", error);
```

El buen ejemplo te dice **qué** ha fallado, **dónde** (BAR0) y **cómo** (código de error).

### Patrones de registro para eventos habituales

**Attach correcto:**

```c
device_printf(dev, "Attached successfully, hardware rev %d.%d\n",
    hw_major, hw_minor);
```

**Fallo en el attach:**

```c
device_printf(dev, "Attach failed: could not allocate IRQ\n");
goto fail;
```

**Detach:**

```c
device_printf(dev, "Detached, uptime %lu seconds\n",
    (unsigned long)(ticks - sc->attach_ticks) / hz);
```

**Fallo en la asignación de recursos:**

```c
if (sc->mem_res == NULL) {
        device_printf(dev, "Could not allocate memory resource\n");
        error = ENXIO;
        goto fail;
}
```

**Estado de hardware inesperado:**

```c
if (status & DEVICE_ERROR_BIT) {
        device_printf(dev, "Hardware reported error 0x%x\n", status);
        /* attempt recovery or fail */
}
```

**Primera apertura:**

```c
if (sc->open_count == 0) {
        device_printf(dev, "Device opened for the first time\n");
}
```

(Solo si es inusual o relevante; no registres cada apertura en producción.)

### Macro de registro con límite de tasa (vista previa avanzada)

Para errores que podrían repetirse rápidamente, puedes definir una macro de registro con límite de tasa:

```c
#define MYFIRST_RATELIMIT_HZ 1  /* Max once per second */

static int
myfirst_log_ratelimited(struct myfirst_softc *sc, const char *fmt, ...)
{
        static time_t last_log = 0;
        time_t now;
        va_list ap;

        now = time_second;
        if (now - last_log < MYFIRST_RATELIMIT_HZ)
                return (0);  /* Too soon, skip */

        last_log = now;

        va_start(ap, fmt);
        device_vprintf(sc->dev, fmt, ap);
        va_end(ap);

        return (1);
}
```

**Uso:**

```c
if (error_condition) {
        myfirst_log_ratelimited(sc, "DMA timeout occurred\n");
}
```

Esto limita el registro a **una vez por segundo**, aunque la condición se dispare miles de veces.

**Cuándo usarlo:** Solo para errores en rutas calientes que podrían saturar los registros (tormentas de interrupciones, desbordamientos de cola, etc.). No es necesario para attach/detach ni para errores poco frecuentes.

### Qué registrar durante el desarrollo frente a producción

**Desarrollo (detallado):**

- Entrada/salida de cada función (protegida por un flag de depuración)
- Lecturas/escrituras de registros
- Transiciones de estado
- Asignación/liberación de recursos

**Producción (silencioso):**

- Ciclo de vida de attach/detach
- Errores
- Cambios de estado críticos (enlace activo/inactivo, reinicio del dispositivo)
- Primera aparición de errores repetitivos

**Transición:** Empieza con detalle y luego reduce a medida que el driver se estabiliza. Deja el registro de depuración protegido por guards en tiempo de compilación o mediante sysctl para solucionar problemas en el futuro.

### Uso de sysctls para el registro de depuración

En lugar de codificar el nivel de verbosidad, expón un sysctl:

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

Luego envuelve los registros de depuración:

```c
if (myfirst_debug) {
        device_printf(dev, "DEBUG: entering attach\n");
}
```

**Ventaja:** Los usuarios o desarrolladores pueden habilitar el registro sin recompilar:

```bash
% sysctl hw.myfirst.debug=1
```

Los sysctls se tratan en detalle en la siguiente sección; esto es solo una vista previa.

### Inspección de los registros

**Ver todos los mensajes del kernel:**

```bash
% dmesg -a
```

**Ver los mensajes recientes:**

```bash
% dmesg | tail -n 20
```

**Buscar tu driver:**

```bash
% dmesg | grep myfirst
```

**Limpiar el buffer de mensajes (si se prueba repetidamente):**

```bash
% sudo dmesg -c > /dev/null
```

(No siempre es aconsejable, pero resulta útil cuando quieres empezar desde cero para hacer pruebas.)

### Ejemplo: registro en myfirst

Añadamos registro disciplinado a nuestro driver. Actualiza `attach()` y `detach()`:

**attach() actualizado:**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Record attach time */
        sc->attach_ticks = ticks;
        sc->is_attached = 1;

        /* Log attach success */
        device_printf(dev, "Attached successfully at tick %lu\n",
            (unsigned long)sc->attach_ticks);

        return (0);
}
```

**detach() actualizado:**

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;
        uint64_t uptime_ticks;

        sc = device_get_softc(dev);

        /* Refuse detach if open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach: device is open\n");
                return (EBUSY);
        }

        /* Calculate uptime */
        uptime_ticks = ticks - sc->attach_ticks;

        /* Log detach */
        device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times\n",
            (unsigned long)uptime_ticks,
            (unsigned long)sc->open_count);

        /* Cleanup */
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;

        return (0);
}
```

**Qué estamos registrando:**

- **Attach:** Confirma el éxito y registra cuándo ocurrió.
- **Rechazo del detach:** Si el dispositivo está abierto, explica por qué falló el detach.
- **Detach correcto:** Muestra el tiempo de actividad y las estadísticas de uso.

Esto proporciona a los usuarios y desarrolladores una visibilidad clara de los eventos del ciclo de vida.

### Errores habituales en el registro

**Error 1: Registrar dentro de locks**

**Incorrecto:**

```c
mtx_lock(&sc->mtx);
device_printf(dev, "Locked, doing work\n");  /* Can cause priority inversion */
/* ... work ... */
mtx_unlock(&sc->mtx);
```

**Por qué es incorrecto:** `device_printf()` puede bloquearse (adquiriendo locks internos). Llamarlo mientras se mantiene tu mutex puede provocar deadlock o inversión de prioridad.

**Correcto:**

```c
mtx_lock(&sc->mtx);
/* ... work ... */
mtx_unlock(&sc->mtx);

device_printf(dev, "Work completed\n");  /* Log after releasing lock */
```

---

**Error 2: Registros en varias líneas que pueden entrelazarse**

**Incorrecto:**

```c
printf("myfirst0: Attach starting\n");
printf("myfirst0: Step 1\n");
printf("myfirst0: Step 2\n");
```

**Por qué es incorrecto:** Si otro driver o componente del kernel registra algo entre tus líneas, tu mensaje queda fragmentado.

**Correcto:**

```c
device_printf(dev, "Attach starting: step 1, step 2 completed\n");
```

O usa un único `sbuf` (buffer de cadena) e imprímelo de una sola vez (uso avanzado).

---

**Error 3: Registrar datos sensibles**

No registres:

- Datos de usuario (contenido de paquetes, datos de archivos, etc.)
- Claves criptográficas o secretos
- Nada que vulnere las expectativas de privacidad

**Asume siempre que los registros son públicos.**

### Autocomprobación rápida

Antes de continuar, confirma que entiendes lo siguiente:

1. **¿Cuándo debes registrar un mensaje?**
   Respuesta: En eventos del ciclo de vida (attach/detach), errores y cambios de estado críticos.

2. **¿Cuándo no debes registrar?**
   Respuesta: En rutas calientes (interrupciones, bucles de lectura/escritura, operaciones por paquete).

3. **¿Por qué usar `device_printf()` en lugar de `printf()`?**
   Respuesta: Incluye automáticamente el nombre del dispositivo, lo que hace los mensajes más claros.

4. **¿Cómo se limita la frecuencia de un mensaje que podría repetirse rápidamente?**
   Respuesta: Usa un flag o una marca de tiempo para registrar el momento del último mensaje y suprimir las repeticiones.

5. **¿Qué debe incluir todo mensaje de error?**
   Respuesta: Qué falló, por qué (código de error) y suficiente contexto para diagnosticar el problema.

Si tienes claro todo esto, estás listo para añadir tu primera superficie visible para el usuario: un nodo `/dev`.

---

## Una superficie de usuario temporal: /dev (solo vista previa)

Todo driver necesita una forma de que los programas de usuario interactúen con él. Para los dispositivos de caracteres, esa superficie es un **nodo de dispositivo** en `/dev`. En esta sección crearemos `/dev/myfirst0`, pero aún no implementaremos I/O completo; haremos lo suficiente para mostrar el patrón y demostrar que el dispositivo es accesible desde el espacio de usuario.

Esto es una **vista previa**, no la implementación completa. La semántica real de `read()` y `write()` llega en los **capítulos 8 y 9**. Aquí nos centramos en:

- Crear el nodo `/dev` con `make_dev_s()`
- Definir un `cdevsw` (conmutador de dispositivo de caracteres) con métodos stub
- Gestionar `open()` y `close()` para registrar el estado del dispositivo
- Limpiar el nodo de dispositivo en `detach()`

Piensa en esto como **montar la puerta principal** antes de amueblar la casa. La puerta se abre y se cierra, pero las habitaciones están vacías.

### ¿Qué es un conmutador de dispositivo de caracteres (cdevsw)?

El **cdevsw** es una estructura que contiene punteros a función para las operaciones de dispositivo de caracteres: `open`, `close`, `read`, `write`, `ioctl`, `mmap`, etc. Cuando un programa de usuario llama a `open("/dev/myfirst0", ...)`, el kernel busca el cdevsw asociado a ese nodo de dispositivo y llama a tu función `d_open`.

**Definición de la estructura** (abreviada):

```c
struct cdevsw {
        int     d_version;    /* D_VERSION (API version) */
        d_open_t        *d_open;      /* open(2) handler */
        d_close_t       *d_close;     /* close(2) handler */
        d_read_t        *d_read;      /* read(2) handler */
        d_write_t       *d_write;     /* write(2) handler */
        d_ioctl_t       *d_ioctl;     /* ioctl(2) handler */
        const char *d_name;   /* Device name */
        /* ... more fields ... */
};
```

**Idea clave:** proporcionas implementaciones para las operaciones que tu dispositivo admite y dejas las demás como `NULL` (lo que el kernel interpreta como "no soportado" o "comportamiento por defecto").

### Definición del cdevsw para myfirst

Definiremos un cdevsw mínimo con manejadores de `open` y `close`, y por ahora dejaremos `read` / `write` como stubs.

Añade esto cerca del principio de `myfirst.c`, después de la definición de softc:

```c
/* Forward declarations for cdevsw methods */
static d_open_t         myfirst_open;
static d_close_t        myfirst_close;
static d_read_t         myfirst_read;
static d_write_t        myfirst_write;

/*
 * Character device switch.
 *
 * Maps system calls to our driver functions.
 */
static struct cdevsw myfirst_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       myfirst_open,
        .d_close =      myfirst_close,
        .d_read =       myfirst_read,
        .d_write =      myfirst_write,
        .d_name =       "myfirst",
};
```

**Qué significa esto:**

- `d_version = D_VERSION` - Marca de versión de API obligatoria.
- `d_open = myfirst_open` - Cuando el usuario llama a `open("/dev/myfirst0", ...)`, el kernel llama a `myfirst_open()`.
- Lo mismo ocurre con `close`, `read`, `write`.
- `d_name = "myfirst"` - Nombre base para los nodos de dispositivo (combinado con el número de unidad para formar `myfirst0`, `myfirst1`, etc.).

### Implementación de open()

El manejador `open()` se invoca cuando un programa de usuario abre el nodo de dispositivo. Es tu oportunidad para:

- Verificar que el dispositivo está listo
- Registrar el estado de apertura (incrementar contadores, activar flags)
- Devolver un error si el dispositivo no puede abrirse (por ejemplo, acceso exclusivo, hardware no listo)

Añade esta función:

```c
/*
 * open() handler.
 *
 * Called when a user program opens /dev/myfirst0.
 */
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;

        sc = dev->si_drv1;  /* Retrieve softc from cdev */

        if (sc == NULL || !sc->is_attached) {
                return (ENXIO);  /* Device not ready */
        }

        mtx_lock(&sc->mtx);
        if (sc->is_open) {
                mtx_unlock(&sc->mtx);
                return (EBUSY);  /* Only allow one opener (exclusive access) */
        }

        sc->is_open = 1;
        sc->open_count++;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Device opened (count: %lu)\n",
            (unsigned long)sc->open_count);

        return (0);
}
```

**Qué hace esto:**

- **`sc = dev->si_drv1`** - Recupera el softc. Cuando creemos el nodo de dispositivo, guardaremos aquí el puntero al softc.
- **`if (!sc->is_attached)`** - Comprobación de seguridad. Si el dispositivo no está attached, se rechaza la apertura.
- **`if (sc->is_open) return (EBUSY)`** - Impone acceso exclusivo (solo una apertura simultánea). Los dispositivos reales pueden permitir varias aperturas; esto es solo un ejemplo sencillo.
- **`sc->is_open = 1`** - Marca el dispositivo como abierto.
- **`sc->open_count++`** - Incrementa el contador de aperturas acumuladas.
- **`device_printf()`** - Registra el evento de apertura (por ahora; lo eliminarías en producción).

**Disciplina de lock:** mantenemos el mutex mientras comprobamos y actualizamos `is_open`, lo que garantiza la seguridad entre threads.

### Implementación de close()

El manejador `close()` se invoca cuando se libera la última referencia al dispositivo abierto. Limpia aquí el estado específico de la apertura.

Añade esta función:

```c
/*
 * close() handler.
 *
 * Called when the user program closes /dev/myfirst0.
 */
static int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;

        sc = dev->si_drv1;

        if (sc == NULL) {
                return (ENXIO);
        }

        mtx_lock(&sc->mtx);
        sc->is_open = 0;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Device closed\n");

        return (0);
}
```

**Qué hace esto:**

- Borra el flag `is_open`.
- Registra el evento de cierre.
- Devuelve 0 (éxito).

**Patrón sencillo:** `open()` activa los flags y `close()` los borra.

### Stubs de read() y write()

Implementaremos stubs mínimos que devuelven éxito pero no hacen nada. Esto demuestra que el nodo de dispositivo está bien conectado sin comprometerse todavía con la semántica de I/O.

**Stub de read():**

```c
/*
 * read() handler (stubbed).
 *
 * For now, just return EOF (0 bytes read).
 * Real implementation in Chapter 9.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* Return EOF immediately */
        return (0);
}
```

**Stub de write():**

```c
/*
 * write() handler (stubbed).
 *
 * For now, pretend we wrote everything.
 * Real implementation in Chapter 9.
 */
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* Pretend we consumed all bytes */
        uio->uio_resid = 0;
        return (0);
}
```

**Qué hacen:**

- **`read()`** - Devuelve 0 (EOF), lo que significa "no hay datos disponibles".
- **`write()`** - Establece `uio->uio_resid = 0`, lo que significa "todos los bytes escritos".

Los programas de usuario percibirán esto como un dispositivo que "acepta las escrituras pero las descarta" y cuyas "lecturas devuelven EOF inmediatamente". Aún no es útil, pero demuestra que el mecanismo subyacente funciona.

### Creación del nodo de dispositivo en attach()

Ahora unimos todo. En `attach()`, crea el nodo `/dev` y asócialo con tu softc.

Añade esto al final de `myfirst_attach()`, justo antes de `return (0)`:

```c
        /* Create /dev node */
        {
                struct make_dev_args args;
                int error;

                make_dev_args_init(&args);
                args.mda_devsw = &myfirst_cdevsw;
                args.mda_uid = UID_ROOT;
                args.mda_gid = GID_WHEEL;
                args.mda_mode = 0600;  /* rw------- (root only) */
                args.mda_si_drv1 = sc;  /* Stash softc pointer */

                error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
                if (error != 0) {
                        device_printf(dev, "Failed to create device node: error %d\n", error);
                        mtx_destroy(&sc->mtx);
                        return (error);
                }
        }

        device_printf(dev, "Created /dev/%s\n", devtoname(sc->cdev));
```

**Qué hace esto:**

- **`make_dev_args_init(&args)`** - Inicializa la estructura de argumentos con los valores por defecto.
- **`args.mda_devsw = &myfirst_cdevsw`** - Asocia este cdev con nuestro cdevsw.
- **`args.mda_uid / gid / mode`** - Establece el propietario y los permisos. `0600` significa lectura/escritura solo para root.
- **`args.mda_si_drv1 = sc`** - Guarda el puntero al softc para que `open()` / `close()` puedan recuperarlo.
- **`make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit)`** - Crea `/dev/myfirst0` (o `myfirst1`, etc., según el número de unidad).
- **Gestión de errores:** si `make_dev_s()` falla, destruye el mutex y devuelve el error.

**Importante:** guardamos el `struct cdev *` en `sc->cdev` para poder destruirlo más adelante en `detach()`.

**Añade el campo cdev al softc:**

Actualiza `struct myfirst_softc`:

```c
struct myfirst_softc {
        device_t        dev;
        int             unit;
        struct mtx      mtx;
        uint64_t        attach_ticks;
        uint64_t        open_count;
        uint64_t        bytes_read;
        int             is_attached;
        int             is_open;

        struct cdev     *cdev;  /* /dev node */
};
```

### Destrucción del nodo de dispositivo en detach()

En `detach()`, debes eliminar el nodo `/dev` **antes** de liberar el softc.

Añade esto cerca del principio de `myfirst_detach()`, después de la comprobación de `is_open`:

```c
        /* Destroy /dev node */
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
```

**Qué hace esto:**

- **`destroy_dev(sc->cdev)`** - Elimina `/dev/myfirst0` del sistema de archivos. Los descriptores de archivo abiertos quedan invalidados y las operaciones posteriores sobre ellos devuelven errores.
- **`sc->cdev = NULL`** - Borra el puntero (programación defensiva).

**El orden importa:** destruye el nodo de dispositivo **antes** de destruir el mutex o liberar otros recursos. Esto garantiza que ninguna operación del espacio de usuario pueda alcanzar tu driver después de que detach comience a desmantelar los recursos.

### Construir, probar y verificar

Compilemos y probemos el nuevo nodo de dispositivo:

**1. Limpiar y construir:**

```bash
% make clean && make
```

**2. Cargar el driver:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
```

**3. Comprobar el nodo de dispositivo:**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 15:45 /dev/myfirst0
```

¡Éxito! El nodo de dispositivo existe.

**4. Probar open y close:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
(no output, immediate EOF)
```

Comprueba dmesg:

```bash
% dmesg | tail -n 2
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

Los manejadores `open()` y `close()` se ejecutaron.

**5. Probar write:**

```bash
% sudo sh -c 'echo "hello" > /dev/myfirst0'
% dmesg | tail -n 2
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

La escritura se realizó correctamente (aunque los datos se descartaron).

**6. Descargar el driver:**

```bash
% sudo kldunload myfirst
% ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

El nodo de dispositivo se destruyó correctamente al descargar.

### ¿Qué acaba de ocurrir?

- Creaste un conmutador de dispositivo de caracteres (`cdevsw`) que mapea syscalls a tus funciones.
- Implementaste manejadores de `open()` y `close()` que registran el estado.
- Dejaste `read()` y `write()` como stubs para demostrar que el mecanismo subyacente funciona.
- Creaste `/dev/myfirst0` en `attach()` y lo destruiste en `detach()`.
- Los programas de usuario ya pueden llamar a `open("/dev/myfirst0", ...)` e interactuar con tu driver.

### Errores comunes con los nodos de dispositivo

**Error 1: olvidar destruir el nodo de dispositivo en detach**

**Incorrecto:**

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        mtx_destroy(&sc->mtx);
        return (0);
        /* Forgot destroy_dev(sc->cdev)! */
}
```

**Por qué es incorrecto:** el nodo de dispositivo persiste tras la descarga. Intentar abrirlo provoca un crash del kernel (el código ha desaparecido, pero el nodo permanece).

**Correcto:** llama siempre a `destroy_dev()` en detach.

---

**Error 2: acceder al softc en open/close sin comprobar is_attached**

**Incorrecto:**

```c
static int
myfirst_open(struct cdev *dev, ...)
{
        struct myfirst_softc *sc = dev->si_drv1;
        /* No check if sc or sc->is_attached is valid */
        mtx_lock(&sc->mtx);  /* Might be NULL or freed! */
        ...
}
```

**Por qué es incorrecto:** si `detach()` se ejecuta de forma concurrente, el softc podría ser inválido.

**Correcto:** comprueba `sc != NULL` y `sc->is_attached` antes de acceder al estado.

---

**Error 3: usar make_dev() en lugar de make_dev_s()**

**Patrón antiguo:**

```c
sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT, GID_WHEEL, 0600, "myfirst%d", sc->unit);
if (sc->cdev == NULL) {
        /* Error handling */
}
```

**Por qué está obsoleto:** `make_dev()` puede fallar y devolver NULL, lo que obliga a comprobaciones de error incómodas.

**Patrón moderno:** `make_dev_s()` devuelve un código de error, lo que hace la gestión de errores más limpia:

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
if (error != 0) {
        /* Handle error */
}
```

**Prefiere `make_dev_s()`** en código nuevo.

### Comprobación rápida

Antes de avanzar, confirma:

1. **¿Qué es el cdevsw?**
   Respuesta: una estructura que mapea syscalls (`open`, `read`, `write`, etc.) a funciones del driver.

2. **¿Cómo recupera open() el softc?**
   Respuesta: a través de `dev->si_drv1`, que establecemos al crear el nodo de dispositivo.

3. **¿Cuándo debes llamar a destroy_dev()?**
   Respuesta: en `detach()`, antes de liberar el softc.

4. **¿Por qué comprobamos `is_attached` en open()?**
   Respuesta: para asegurarnos de que el dispositivo no ha comenzado a desconectarse, lo que podría llevar a acceder a memoria liberada.

5. **¿Qué devuelve el stub de read()?**
   Respuesta: 0 (EOF), lo que indica que no hay datos disponibles.

Si tienes todo claro, estás listo para añadir **observabilidad mediante sysctl** en la siguiente sección.

---


## Un plano de control mínimo: sysctl de solo lectura

Los nodos de dispositivo en `/dev` permiten que los programas de usuario envíen y reciban datos, pero no son la única forma de exponer tu driver al mundo exterior. Los **sysctls** ofrecen un plano de control y observabilidad ligero que permite a usuarios y administradores consultar el estado del driver, leer estadísticas y, opcionalmente, ajustar parámetros en tiempo de ejecución.

En esta sección añadiremos un **sysctl de solo lectura** que expone estadísticas básicas del driver. Esto te dará una idea de la infraestructura sysctl de FreeBSD sin comprometerte con parámetros ajustables de lectura/escritura completos ni con jerarquías complejas (eso lo veremos cuando abordemos la observabilidad y la depuración en la Parte 5).

Al terminar, tendrás:

- Un nodo sysctl bajo `dev.myfirst.0.*` que muestra el tiempo de attach, el contador de aperturas y los bytes leídos
- Comprensión de los sysctls estáticos frente a los dinámicos
- Un patrón que podrás ampliar más adelante para una observabilidad más compleja

### Por qué importan los sysctls

Los sysctls ofrecen **observabilidad fuera de banda**, es decir, una forma de inspeccionar el estado del driver sin abrir el dispositivo ni generar I/O. Son esenciales para:

- **Depuración:** "¿Está el driver realmente attached? ¿Cuál es el estado actual?"
- **Monitorización:** "¿Cuántas veces se ha abierto este dispositivo? ¿Hay algún error?"
- **Ajuste:** (sysctls de lectura/escritura, tratados más adelante) "Ajusta el tamaño del buffer o el valor de timeout."

**Ejemplo de uso:** una interfaz de red podría exponer `dev.em.0.rx_packets` y `dev.em.0.tx_errors` para que las herramientas de monitorización puedan seguir el rendimiento sin analizar flujos de paquetes.

**Modelo mental:** los sysctls son como un "panel de estado" en el lateral de tu driver, visible mediante comandos `sysctl` sin afectar al funcionamiento normal.

### El árbol sysctl de FreeBSD

Los sysctls se organizan jerárquicamente, como un sistema de archivos:

```ini
kern.ostype = "FreeBSD"
hw.ncpu = 8
dev.em.0.rx_packets = 123456
```

**Ramas principales habituales:**

- `kern.*` - Parámetros del kernel
- `hw.*` - Información de hardware
- `dev.*` - Nodos específicos de dispositivo (aquí va tu driver)
- `net.*` - Parámetros de la pila de red

**El espacio de nombres de tu driver:** `dev.<drivername>.<unit>.*`

Para `myfirst`, eso significa `dev.myfirst.0.*` para la primera instancia.

### Sysctls estáticos frente a dinámicos

**Sysctls estáticos:**

- Se declaran en tiempo de compilación mediante macros `SYSCTL_*`
- Sencillos de definir, pero no pueden crearse ni destruirse dinámicamente
- Adecuados para ajustes globales del driver o constantes

**Ejemplo:**

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw, OID_AUTO, myfirst_debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

**Sysctls dinámicos:**

- Se crean en tiempo de ejecución (normalmente en `attach()`)
- Pueden destruirse en `detach()`
- Adecuados para el estado por dispositivo (como las estadísticas de `myfirst0`, `myfirst1`, etc.)

**En este capítulo usaremos sysctls dinámicos** para que cada instancia del dispositivo tenga sus propios nodos.

### Añadir un contexto sysctl al softc

Los sysctls dinámicos necesitan un **contexto sysctl** (`struct sysctl_ctx_list`) para rastrear los nodos que creas. Esto hace que la limpieza sea automática cuando liberas el contexto.

Añade estos campos a `struct myfirst_softc`:

```c
struct myfirst_softc {
        device_t        dev;
        int             unit;
        struct mtx      mtx;
        uint64_t        attach_ticks;
        uint64_t        open_count;
        uint64_t        bytes_read;
        int             is_attached;
        int             is_open;
        struct cdev     *cdev;

        /* Sysctl context for dynamic nodes */
        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid       *sysctl_tree;  /* Root of our subtree */
};
```

**Qué hacen estos campos:**

- `sysctl_ctx` - Registra todos los nodos sysctl que creamos. Cuando llamamos a `sysctl_ctx_free()`, todos los nodos se destruyen automáticamente.
- `sysctl_tree` - OID (identificador de objeto) raíz para `dev.myfirst.0.*`. Los nodos hijo se cuelgan aquí.

### Creación del árbol sysctl en attach()

Añade este código a `myfirst_attach()`, después de crear el nodo `/dev`:

```c
        /* Initialize sysctl context */
        sysctl_ctx_init(&sc->sysctl_ctx);

        /* Create device sysctl tree: dev.myfirst.0 */
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        if (sc->sysctl_tree == NULL) {
                device_printf(dev, "Failed to create sysctl tree\n");
                destroy_dev(sc->cdev);
                mtx_destroy(&sc->mtx);
                return (ENOMEM);
        }

        /* Add individual sysctl nodes */
        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Number of times device was opened");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes read from device");

        device_printf(dev, "Sysctl tree created under dev.myfirst.%d.stats\n",
            sc->unit);
```

**Qué hace esto:**

1. **`sysctl_ctx_init(&sc->sysctl_ctx)`** - Inicializa el contexto (debe ser lo primero).

2. **`SYSCTL_ADD_NODE()`** - Crea un nodo de subárbol `dev.myfirst.0.stats`. Parámetros:
   - `&sc->sysctl_ctx` - Contexto propietario de este nodo.
   - `SYSCTL_CHILDREN(device_get_sysctl_tree(dev))` - Padre (el árbol sysctl del dispositivo).
   - `OID_AUTO` - Asigna automáticamente un número OID.
   - `"stats"` - Nombre del nodo.
   - `CTLFLAG_RD | CTLFLAG_MPSAFE` - Solo lectura, seguro para MP.
   - `0` - Función handler (ninguna para un nodo).
   - `"Driver statistics"` - Descripción.

3. **`SYSCTL_ADD_U64()`** - Añade un sysctl de entero sin signo de 64 bits. Parámetros:
   - `&sc->sysctl_ctx` - Contexto.
   - `SYSCTL_CHILDREN(sc->sysctl_tree)` - Padre (subárbol `stats`).
   - `OID_AUTO` - Asigna automáticamente el OID.
   - `"attach_ticks"` - Nombre de la hoja.
   - `CTLFLAG_RD` - Solo lectura.
   - `&sc->attach_ticks` - Puntero a la variable que se expone.
   - `0` - Indicación de formato (0 = valor por defecto).
   - `"Tick count..."` - Descripción.

4. **Gestión de errores:** Si la creación del nodo falla, se libera lo asignado y se devuelve `ENOMEM`.

**Resultado:** Ahora tienes tres sysctls:

- `dev.myfirst.0.stats.attach_ticks`
- `dev.myfirst.0.stats.open_count`
- `dev.myfirst.0.stats.bytes_read`

### Destrucción del árbol de sysctl en detach()

La limpieza es sencilla: libera el contexto y todos los nodos se destruyen automáticamente.

Añade esto a `myfirst_detach()`, después de destruir el nodo de dispositivo:

```c
        /* Free sysctl context (destroys all nodes) */
        sysctl_ctx_free(&sc->sysctl_ctx);
```

Eso es todo. Una sola línea limpia todo.

**Por qué es seguro:** `sysctl_ctx_free()` recorre la lista del contexto y elimina cada nodo. Siempre que los hayas creado todos a través del contexto, la limpieza es automática.

### Compilación, carga y prueba de sysctls

**1. Limpiar y compilar:**

```bash
% make clean && make
```

**2. Cargar el driver:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 4
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**3. Consultar los sysctls:**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**4. Abrir el dispositivo y comprobar de nuevo:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 1
```

¡El contador se incrementó!

**5. Descargar y verificar la limpieza:**

```bash
% sudo kldunload myfirst
% sysctl dev.myfirst.0.stats
sysctl: unknown oid 'dev.myfirst.0.stats'
```

Los sysctls se destruyeron correctamente.

### Cómo hacer que los sysctls sean más útiles

Por ahora, los sysctls solo exponen números brutos. Vamos a hacerlos más amigables para el usuario.

**Añade un sysctl de uptime legible por humanos:**

En lugar de exponer recuentos de ticks brutos, calcula el uptime en segundos.

Añade una función manejadora:

```c
/*
 * Sysctl handler for uptime_seconds.
 *
 * Computes how long the driver has been attached, in seconds.
 */
static int
sysctl_uptime_seconds(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t uptime;

        uptime = (ticks - sc->attach_ticks) / hz;

        return (sysctl_handle_64(oidp, &uptime, 0, req));
}
```

**Regístralo en attach():**

```c
        SYSCTL_ADD_PROC(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "uptime_seconds", CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, sysctl_uptime_seconds, "QU",
            "Seconds since driver attached");
```

**Prueba:**

```bash
% sysctl dev.myfirst.0.stats.uptime_seconds
dev.myfirst.0.stats.uptime_seconds: 42
```

¡Mucho más legible que los recuentos de ticks brutos!

### Sysctls de solo lectura frente a los de lectura y escritura

Nuestros sysctls son de solo lectura (`CTLFLAG_RD`). Para hacerlos escribibles, usa `CTLFLAG_RW` y añade un manejador que valide la entrada.

**Ejemplo (solo como vista previa, sin implementar ahora):**

```c
static int
sysctl_set_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new_level, error;

        new_level = sc->debug_level;
        error = sysctl_handle_int(oidp, &new_level, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        if (new_level < 0 || new_level > 3)
                return (EINVAL);  /* Reject invalid values */

        sc->debug_level = new_level;
        device_printf(sc->dev, "Debug level set to %d\n", new_level);

        return (0);
}
```

Volveremos a los sysctls de lectura y escritura en la Parte 5, cuando analicemos en profundidad las herramientas de depuración y observabilidad. Por ahora, la exposición de solo lectura es suficiente.

### Buenas prácticas con sysctl

**1. Expón métricas significativas**

- Contadores (paquetes, errores, aperturas, cierres)
- Indicadores de estado (attached, abierto, habilitado)
- Valores derivados (uptime, throughput, utilización)

**No expongas:**

- Punteros internos o direcciones (riesgo de seguridad)
- Datos brutos sin sentido (usa manejadores para darles un formato legible)

---

**2. Usa nombres y descripciones descriptivos**

**Correcto:**

```c
SYSCTL_ADD_U64(..., "rx_packets", ..., "Packets received");
```

**Incorrecto:**

```c
SYSCTL_ADD_U64(..., "cnt1", ..., "Counter");
```

---

**3. Agrupa los sysctls relacionados en subárboles**

```text
dev.myfirst.0.stats.*    (statistics)
dev.myfirst.0.config.*   (tunable parameters)
dev.myfirst.0.debug.*    (debug flags and counters)
```

---

**4. Protege el acceso concurrente**

Si un sysctl lee o escribe estado compartido, mantén el lock apropiado:

```c
static int
sysctl_read_counter(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;

        mtx_lock(&sc->mtx);
        value = sc->some_counter;
        mtx_unlock(&sc->mtx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

---

**5. Limpia en detach**

Llama siempre a `sysctl_ctx_free(&sc->sysctl_ctx)` en detach, o filtrarás OIDs.

### Errores comunes con sysctl

**Error 1: Olvidar sysctl_ctx_init**

**Incorrecto:**

```c
SYSCTL_ADD_NODE(&sc->sysctl_ctx, ...);  /* Context not initialized! */
```

**Por qué está mal:** Un contexto sin inicializar provoca panics o fugas.

**Correcto:** Llama a `sysctl_ctx_init(&sc->sysctl_ctx)` en attach antes de añadir nodos.

---

**Error 2: No liberar el contexto en detach**

**Incorrecto:**

```c
static int
myfirst_detach(device_t dev)
{
        /* ... destroy other resources ... */
        return (0);
        /* Forgot sysctl_ctx_free! */
}
```

**Por qué está mal:** Los nodos sysctl persisten después de la descarga. El siguiente acceso causa un crash.

**Correcto:** Llama siempre a `sysctl_ctx_free(&sc->sysctl_ctx)` en detach.

---

**Error 3: Exponer punteros brutos**

**Incorrecto:**

```c
SYSCTL_ADD_PTR(..., "softc_addr", ..., &sc, ...);  /* Security hole! */
```

**Por qué está mal:** Filtra el layout del espacio de direcciones del kernel (evasión de KASLR).

**Correcto:** Nunca expongas punteros a través de sysctls.

### Comprobación rápida

Antes de continuar, comprueba:

1. **¿Qué es un sysctl?**
   Respuesta: Una variable del kernel o un valor calculado que se expone mediante el comando `sysctl`.

2. **¿Dónde se encuentran los sysctls de un driver?**
   Respuesta: Bajo `dev.<drivername>.<unit>.*`.

3. **¿Qué debes llamar en attach() antes de añadir nodos?**
   Respuesta: `sysctl_ctx_init(&sc->sysctl_ctx)`.

4. **¿Qué limpia todos los nodos sysctl en detach()?**
   Respuesta: `sysctl_ctx_free(&sc->sysctl_ctx)`.

5. **¿Por qué usar un manejador en lugar de exponer una variable directamente?**
   Respuesta: Para calcular valores derivados (como el uptime) o validar escrituras.

Si todo eso está claro, estás listo para aprender el **manejo de errores y el desmontaje limpio** en la siguiente sección.

---

## Rutas de error y desmontaje limpio

Hasta ahora, hemos escrito `attach()` asumiendo que todo tiene éxito. Pero los drivers reales deben manejar los fallos de forma elegante: si la asignación de memoria falla, si un recurso no está disponible o si el hardware no se comporta como se espera, el driver debe **deshacer lo que empezó** y devolver un error sin dejar estado parcial.

Esta sección enseña el **patrón de desmontaje de etiqueta única**, el idioma estándar de FreeBSD para la limpieza de errores. Domínalo, y tu driver nunca filtrará recursos, sin importar dónde ocurra el fallo.

### Por qué importa el manejo de errores

Un manejo de errores deficiente provoca:

- **Fugas de recursos** (memoria, locks, nodos de dispositivo)
- **Kernel panics** (acceso a memoria liberada, doble liberación)
- **Estado inconsistente** (dispositivo medio attached, locks inicializados pero no destruidos)

**Impacto real:** Un driver con rutas de error descuidadas puede funcionar bien durante la operación normal, pero provocar un panic del sistema cuando encuentra un fallo inusual (memoria agotada, hardware ausente, etc.).

**Tu objetivo:** Asegurarte de que `attach()` tenga éxito completo o falle completamente, sin términos medios.

### El patrón de desmontaje de etiqueta única

El código del kernel de FreeBSD utiliza un **patrón de desmontaje basado en goto** para la limpieza. Tiene este aspecto:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;

        /* Step 1: Initialize mutex */
        mtx_init(&sc->mtx, "myfirst", NULL, MTX_DEF);

        /* Step 2: Allocate memory resource (example) */
        sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
            &sc->mem_rid, RF_ACTIVE);
        if (sc->mem_res == NULL) {
                device_printf(dev, "Failed to allocate memory resource\n");
                error = ENXIO;
                goto fail_mtx;
        }

        /* Step 3: Create device node */
        error = create_dev_node(sc);
        if (error != 0) {
                device_printf(dev, "Failed to create device node: %d\n", error);
                goto fail_mem;
        }

        /* Step 4: Create sysctls */
        error = create_sysctls(sc);
        if (error != 0) {
                device_printf(dev, "Failed to create sysctls: %d\n", error);
                goto fail_dev;
        }

        device_printf(dev, "Attached successfully\n");
        return (0);

fail_dev:
        destroy_dev(sc->cdev);
fail_mem:
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
fail_mtx:
        mtx_destroy(&sc->mtx);
        return (error);
}
```

**Cómo funciona:**

- Cada paso de inicialización tiene una etiqueta de limpieza correspondiente.
- Si un paso falla, salta a la etiqueta que deshace todo lo **completado hasta ese momento**.
- Las etiquetas están ordenadas en **orden inverso** a la inicialización.
- Cada etiqueta pasa a la siguiente, de modo que un fallo en el paso 4 deshace el 3, el 2 y el 1.

**¿Por qué este patrón?**

- **Limpieza centralizada:** Todas las rutas de error convergen en una única secuencia de desmontaje.
- **Fácil de mantener:** Añadir un nuevo paso implica añadir un goto y una etiqueta de limpieza.
- **Sin duplicación:** No repites el código de limpieza en cada rama de error.

### Aplicación del patrón a myfirst

Vamos a refactorizar nuestro `attach()` para manejar los errores correctamente.

**Antes (sin manejo de errores):**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_init(&sc->mtx, ...);
        create_dev_node(sc);
        create_sysctls(sc);

        return (0);  /* What if something failed? */
}
```

**Después (con desmontaje limpio):**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        struct make_dev_args args;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Step 1: Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Step 2: Record attach time and initialize state */
        sc->attach_ticks = ticks;
        sc->is_attached = 1;
        sc->is_open = 0;
        sc->open_count = 0;
        sc->bytes_read = 0;

        /* Step 3: Create /dev node */
        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0600;
        args.mda_si_drv1 = sc;

        error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
        if (error != 0) {
                device_printf(dev, "Failed to create device node: %d\n", error);
                goto fail_mtx;
        }

        /* Step 4: Initialize sysctl context */
        sysctl_ctx_init(&sc->sysctl_ctx);

        /* Step 5: Create sysctl tree */
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        if (sc->sysctl_tree == NULL) {
                device_printf(dev, "Failed to create sysctl tree\n");
                error = ENOMEM;
                goto fail_dev;
        }

        /* Step 6: Add sysctl nodes */
        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Number of times device was opened");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes read from device");

        device_printf(dev, "Attached successfully\n");
        return (0);

        /* Error unwinding (in reverse order of initialization) */
fail_dev:
        destroy_dev(sc->cdev);
        sysctl_ctx_free(&sc->sysctl_ctx);
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}
```

**Mejoras clave:**

- Cada operación que puede fallar se comprueba.
- Los fallos saltan a la etiqueta de limpieza apropiada.
- La secuencia de desmontaje deshace exactamente lo que tuvo éxito.
- Todas las rutas devuelven un código de error (nunca se devuelve éxito después de un fallo).

### Convención de nombres para etiquetas

Elige nombres de etiquetas que indiquen qué hay que deshacer:

- `fail_mtx` - Destruye el mutex
- `fail_mem` - Libera el recurso de memoria
- `fail_dev` - Destruye el nodo de dispositivo
- `fail_irq` - Libera el recurso de interrupción

O usa números si lo prefieres:

- `fail1`, `fail2`, `fail3`

Cualquiera de los dos funciona, pero los nombres descriptivos hacen el código más fácil de leer.

### Prueba de rutas de error

**Simula un fallo** para verificar que tu lógica de desmontaje funciona.

Añade un fallo deliberado después de la inicialización del mutex:

```c
        mtx_init(&sc->mtx, ...);

        /* Simulate allocation failure for testing */
        if (1) {  /* Change to 0 to disable */
                device_printf(dev, "Simulated failure\n");
                error = ENXIO;
                goto fail_mtx;
        }
```

**Compila y carga:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
```

**Comprueba dmesg:**

```bash
% dmesg | tail -n 2
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulated failure
```

**Verifica la limpieza:**

```bash
% devinfo | grep myfirst
(no output - device didn't attach)

% ls /dev/myfirst*
ls: cannot access '/dev/myfirst*': No such file or directory
```

**Observación clave:** El driver falló de forma limpia. Sin nodo de dispositivo, sin fugas de sysctl, sin panic.

Ahora desactiva el fallo simulado y prueba de nuevo el attach normal.

### Errores comunes en el manejo de errores

**Error 1: No comprobar los valores de retorno**

**Incorrecto:**

```c
make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
/* Forgot to check error! */
```

**Por qué está mal:** Si `make_dev_s()` falla, `sc->cdev` podría ser NULL o basura, y continúas como si todo fuera bien.

**Correcto:** Comprueba siempre `error` y actúa en consecuencia.

---

**Error 2: Limpieza parcial**

**Incorrecto:**

```c
fail_dev:
        destroy_dev(sc->cdev);
        return (error);
        /* Forgot to destroy mutex! */
```

**Por qué está mal:** El mutex sigue inicializado. La siguiente carga provoca un panic en la reinicialización.

**Correcto:** Cada etiqueta debe deshacer **todo** lo inicializado antes.

---

**Error 3: Doble limpieza**

**Incorrecto:**

```c
fail_dev:
        destroy_dev(sc->cdev);
        mtx_destroy(&sc->mtx);
        goto fail_mtx;

fail_mtx:
        mtx_destroy(&sc->mtx);  /* Destroyed twice! */
        return (error);
}
```

**Por qué está mal:** La doble liberación o doble destrucción provoca panics.

**Correcto:** Cada recurso debe limpiarse exactamente una vez, en su etiqueta correspondiente.

---

**Error 4: Devolver éxito después de un fallo**

**Incorrecto:**

```c
if (error != 0) {
        goto fail_mtx;
}
return (0);  /* Even if we jumped to fail_mtx! */
```

**Por qué está mal:** El goto omite el return, pero el patrón implica que todas las rutas de error deben **devolver un código de error**.

**Correcto:** Asegúrate de que las etiquetas de error terminen con `return (error)`.

### La visión completa: attach y detach

**Lógica de attach:**

1. Inicializa los recursos en orden.
2. Comprueba cada paso en busca de fallos.
3. En caso de fallo, salta a la etiqueta de desmontaje correspondiente al último paso exitoso.
4. Las etiquetas de desmontaje pasan en orden inverso, limpiando todo.

**Lógica de detach:**

Detach es más sencillo: deshaz todo en orden inverso a `attach()`, asumiendo éxito completo:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Refuse if device is open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach: device is open\n");
                return (EBUSY);
        }

        device_printf(dev, "Detaching\n");

        /* Undo in reverse order of attach */
        destroy_dev(sc->cdev);                /* Step 1: Drop user surface first */
        sysctl_ctx_free(&sc->sysctl_ctx);    /* Step 2: Free sysctl context */
        mtx_destroy(&sc->mtx);                /* Step 3: Destroy mutex last */
        sc->is_attached = 0;

        return (0);
}
```

**Simetría:** Cada paso de `attach()` tiene una acción correspondiente en `detach()`, en orden inverso.

**Nota de orden:** Destruimos el dispositivo (`destroy_dev`) antes de liberar el contexto sysctl y destruir el mutex. Esto sigue la guía de advertencias del Capítulo 6: «Destroy device before locks». La llamada a `destroy_dev()` bloquea hasta que todas las operaciones de archivo en curso concluyen, garantizando que ninguna ruta de código pueda alcanzar nuestros locks después de que el dispositivo desaparezca.

### Lista de comprobación de programación defensiva

Antes de dar por terminadas tus rutas de error, comprueba:

- [ ] Cada función que puede fallar se comprueba
- [ ] Cada error establece `error` y salta a una etiqueta de limpieza
- [ ] Cada etiqueta de limpieza deshace exactamente lo que la precedió
- [ ] Las etiquetas están en orden inverso a la inicialización
- [ ] `detach()` deshace todo lo que hizo `attach()`, en orden inverso
- [ ] Ningún recurso se libera dos veces
- [ ] Ningún recurso se filtra en caso de fallo

### Comprobación rápida

Antes de continuar, comprueba:

1. **¿Qué es el patrón de desmontaje de etiqueta única?**
   Respuesta: Una secuencia de limpieza basada en goto en la que cada etiqueta deshace progresivamente más recursos, en orden inverso a la inicialización.

2. **¿Por qué están las etiquetas de limpieza en orden inverso?**
   Respuesta: Porque debes deshacer el paso más reciente primero, luego los anteriores, recorriendo la inicialización hacia atrás.

3. **¿Qué debe hacer cada ruta de error antes de retornar?**
   Respuesta: Saltar a la etiqueta de limpieza apropiada y asegurarse de que se ejecuta `return (error)`.

4. **¿Cómo se prueban las rutas de error?**
   Respuesta: Simula fallos (por ejemplo, fuerza que una asignación falle) y verifica que la limpieza es correcta (sin fugas, sin panics).

5. **¿Cuándo debe detach negarse a continuar?**
   Respuesta: Cuando el dispositivo está todavía en uso (por ejemplo, cuando `is_open` es true), devuelve `EBUSY`.

Si todo eso está claro, estás listo para explorar **ejemplos de drivers reales en el árbol de código fuente de FreeBSD**.

---

## Puntos de anclaje en el árbol de código fuente (solo referencia)

Has construido un driver mínimo desde los principios más básicos. Ahora vamos a **anclar tu comprensión** señalándote drivers reales de FreeBSD 14.3 que demuestran los mismos patrones que acabas de aprender. Esta sección es un **recorrido guiado**, no un análisis detallado. Piensa en ella como una lista de lecturas para cuando quieras ver cómo el código de producción aplica las lecciones de este capítulo.

### ¿Por qué examinar drivers reales?

Los drivers reales te muestran:

- Cómo escalan los patrones hacia hardware complejo
- Cómo luce el código en contexto (el archivo completo, no solo fragmentos)
- Variaciones de los patrones (lógica de attach diferente, tipos de recursos, estilos de manejo de errores)
- Modismos y convenciones de FreeBSD en la práctica

**No se espera que entiendas cada línea** de estos drivers ahora mismo. El objetivo es **reconocer el andamiaje** que ya has construido y ver cómo se extiende hacia drivers más capaces.

### Punto de anclaje 1: `/usr/src/sys/dev/null/null.c`

**Qué es:** Los pseudodispositivos `/dev/null`, `/dev/zero` y `/dev/full`.

**Por qué estudiarlo:**

- Los dispositivos de caracteres más simples posibles
- Sin hardware, sin recursos, solo cdevsw + el manejador MOD_LOAD
- Muestra cómo se implementan `read()` y `write()` (aunque de forma trivial)
- Buena referencia para I/O con stubs

**Qué buscar:**

- Las estructuras `cdevsw` (usa `grep -n cdevsw` para encontrarlas)
- Los manejadores `null_write()` y `zero_read()`
- El cargador del módulo (`null_modevent()`, usa `grep -n modevent` para encontrarlo)
- Cómo se usa `make_dev_credf()` (usa `grep -n make_dev` para encontrarlo)

**Ubicación del archivo:**

```bash
% less /usr/src/sys/dev/null/null.c
```

**Exploración rápida:**

```bash
% grep -n "cdevsw\|make_dev" /usr/src/sys/dev/null/null.c
```

### Punto de anclaje 2: `/usr/src/sys/dev/led/led.c`

**Qué es:** El framework de control de LED, utilizado por los drivers específicos de plataforma para exponer los LEDs como `/dev/led/*`.

**Por qué estudiarlo:**

- Sencillo todavía, pero muestra la gestión de recursos (callouts, listas)
- Demuestra la creación dinámica de dispositivos por LED
- Utiliza locking (`struct mtx`)
- Muestra cómo los drivers gestionan múltiples instancias

**Qué buscar:**

- La estructura `ledsc` (cerca del principio del archivo; usa `grep -n ledsc` para encontrarla), análoga a tu softc
- La función `led_create()` (usa `grep -n "led_create\|led_destroy"` para encontrarla), que crea nodos de dispositivo de forma dinámica
- La función `led_destroy()`, patrón de limpieza
- Cómo se protege la lista global de LEDs con un mutex

**Ubicación del archivo:**

```bash
% less /usr/src/sys/dev/led/led.c
```

**Vistazo rápido:**

```bash
% grep -n "ledsc\|led_create\|led_destroy" /usr/src/sys/dev/led/led.c
```

### Referencia 3: `/usr/src/sys/net/if_tuntap.c`

**Qué es:** Las interfaces de red pseudo `tun` y `tap` (dispositivos de túnel).

**Por qué estudiarlo:**

- Driver híbrido: dispositivo de caracteres **y** interfaz de red
- Muestra cómo registrarse con `ifnet` (la pila de red)
- Ciclo de vida más complejo (dispositivos clone, estado por apertura)
- Buen ejemplo de locking y concurrencia en entornos reales

**Qué buscar:**

- La `struct tuntap_softc` (usa `grep -n "struct tuntap_softc"` para encontrarla), mucho más rica que la tuya
- La función `tun_create()`, que registra el `ifnet`
- El `cdevsw` y cómo coordina con el lado de red
- Uso de `if_attach()` e `if_detach()` para la integración de red

**Ubicación del archivo:**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

**Advertencia:** Este es un archivo grande y complejo (aproximadamente 2000 líneas). No intentes entenderlo todo. Céntrate en:

```bash
% grep -n "tuntap_softc\|if_attach\|make_dev" /usr/src/sys/net/if_tuntap.c | head -20
```

### Referencia 4: `/usr/src/sys/dev/uart/uart_bus_pci.c`

**Qué es:** Código de conexión PCI para dispositivos UART (puertos serie).

**Por qué estudiarlo:**

- Driver de hardware real (bus PCI)
- Muestra cómo `probe()` comprueba los IDs de PCI
- Demuestra la asignación de recursos (puertos I/O, IRQs)
- Desenrollado de errores en `attach()`

**Qué buscar:**

- La función `uart_pci_probe()` (usa `grep -n uart_pci_probe` para encontrarla), coincidencia de IDs PCI
- La función `uart_pci_attach()`, asignación de recursos
- Uso de `bus_alloc_resource()` y `bus_release_resource()`
- La tabla `device_method_t` (usa `grep -n device_method` para encontrarla)

**Ubicación del archivo:**

```bash
% less /usr/src/sys/dev/uart/uart_bus_pci.c
```

**Exploración rápida:**

```bash
% grep -n "uart_pci_probe\|uart_pci_attach\|device_method" /usr/src/sys/dev/uart/uart_bus_pci.c
```

**Consejo:** Este archivo es pequeño (aproximadamente 250 líneas) y muy limpio. Es un excelente ejemplo de un driver Newbus real.

### Referencia 5: patrones `DRIVER_MODULE` y `MODULE_VERSION`

Busca estas macros al final de los archivos de driver:

```bash
% grep -rn 'DRIVER_MODULE\|MODULE_VERSION' /usr/src/sys/dev/null/ /usr/src/sys/dev/led/
```

Verás los mismos patrones de registro que usaste en `myfirst`. Para los drivers que se conectan a `nexus` y proporcionan su propio método `identify`, el patrón en `/usr/src/sys/crypto/aesni/aesni.c` y `/usr/src/sys/dev/sound/dummy.c` es el que más se parece a lo que escribiste.

### Cómo usar estas referencias

**1. Empieza con null.c**

Lee el archivo completo; es corto (aproximadamente 220 líneas). Deberías reconocer casi todo.

**2. Hojea led.c**

Céntrate en la estructura y el ciclo de vida (creación y destrucción). No te pierdas en la máquina de estados.

**3. Echa un vistazo a if_tuntap.c**

Ábrelo, desplázate por él y observa la estructura híbrida (cdevsw + ifnet). No intentes entenderlo todo; simplemente aprecia su forma general.

**4. Estudia uart_bus_pci.c**

Lee `probe()` y `attach()`. Este es tu puente hacia los drivers de hardware real (que se tratan en la Parte 4).

**5. Compara con tu driver**

Para cada referencia, pregúntate:

- ¿Qué se parece a mi driver `myfirst`?
- ¿Qué es diferente?
- ¿Qué conceptos nuevos veo (callouts, ifnet, recursos PCI)?

**6. Anota lo que debes aprender a continuación**

Cuando veas algo desconocido (por ejemplo, `callout_reset`, `if_attach`, `bus_alloc_resource`), toma nota. Son temas para capítulos posteriores.

### Repaso rápido: patrones comunes en los drivers

| Patrón                       | null.c | led.c | if_tuntap.c | uart_pci.c   | myfirst.c     |
|------------------------------|--------|-------|-------------|--------------|---------------|
| Usa `cdevsw`                 | sí     | sí    | sí          | no           | sí            |
| Usa `ifnet`                  | no     | no    | sí          | no           | no            |
| probe/attach de Newbus       | no     | no    | no (clone)  | sí           | sí            |
| Tiene método `identify`      | no     | no    | no          | no           | sí            |
| Manejador de carga de módulo | sí     | sí    | sí          | no (Newbus)  | no (Newbus)   |
| Asigna softc                 | no     | no    | sí          | sí           | sí (Newbus)   |
| Usa locking                  | no     | sí    | sí          | sí           | sí            |
| Asigna recursos de bus       | no     | no    | no          | sí           | no            |
| Crea nodos en `/dev`         | sí     | sí    | sí          | no           | sí            |

### Qué omitir (de momento)

Al leer estos drivers, no te quedes atascado en:

- Acceso a registros de hardware (`bus_read_4`, `bus_write_2`)
- Configuración de interrupciones (`bus_setup_intr`, registro del manejador)
- DMA (`bus_dma_tag_create`, `bus_dmamap_load`)
- Locking avanzado (locks de solo lectura, orden de lock)
- Manejo de paquetes de red (cadenas `mbuf`, `if_transmit`)

Los aprenderás en sus capítulos dedicados. Por ahora, céntrate en la **estructura y el ciclo de vida**.

### Ejercicio de autoestudio

Elige una referencia (`null.c` es la recomendada para principiantes) y:

1. Lee el archivo completo
2. Identifica la estructura `cdevsw`
3. Encuentra los manejadores `open`, `close`, `read`, `write`
4. Traza el flujo de carga y descarga del módulo
5. Compara con tu driver `myfirst`

Escribe un párrafo en tu cuaderno de laboratorio: "Esto es lo que aprendí de [nombre del driver]."

### Comprobación rápida

Antes de pasar a los laboratorios, confirma:

1. **¿Por qué estudiar drivers reales?**
   Respuesta: Para ver patrones en contexto, aprender los idiomas del código y pasar de ejemplos mínimos a código de producción.

2. **¿Qué driver es el más sencillo?**
   Respuesta: `null.c`; es únicamente pseudo, sin estado ni recursos.

3. **¿Qué driver muestra una estructura híbrida?**
   Respuesta: `if_tuntap.c`; es a la vez un dispositivo de caracteres y una interfaz de red.

4. **¿Qué debes omitir al leer drivers complejos?**
   Respuesta: Los detalles específicos del hardware (registros, DMA, interrupciones), hasta que los capítulos posteriores los cubran.

5. **¿Cómo debes usar estas referencias?**
   Respuesta: Como ejemplos de referencia, no como material de estudio detallado. Hojea, compara, anota los nuevos conceptos y sigue adelante.

Si todo esto te queda claro, estás listo para los **laboratorios prácticos**, donde construirás, probarás y ampliarás tu driver.

---

## Laboratorios prácticos

Has leído sobre la estructura de los drivers, has visto los patrones y has recorrido el código. Ahora es el momento de **construir, probar y validar** tu comprensión a través de cuatro laboratorios prácticos. Cada laboratorio es un punto de control que te asegura haber dominado los conceptos antes de seguir adelante.

### Descripción general de los laboratorios

| Laboratorio | Enfoque                        | Duración   | Aprendizaje clave                                              |
|-------------|--------------------------------|------------|----------------------------------------------------------------|
| Lab 7.1     | Búsqueda en el código fuente   | 20-30 min  | Navegar por el código fuente de FreeBSD, identificar patrones  |
| Lab 7.2     | Compilar y cargar              | 30-40 min  | Compilar, cargar, verificar el ciclo de vida                   |
| Lab 7.3     | Nodo de dispositivo            | 30-40 min  | Crear `/dev`, probar open/close                                |
| Lab 7.4     | Manejo de errores              | 30-45 min  | Simular fallos, verificar el desenrollado                      |

**Tiempo total:** 2-2,5 horas si completas todos los laboratorios en una sola sesión.

**Requisitos previos:**

- Entorno de laboratorio FreeBSD 14.3 del Capítulo 2
- `/usr/src` instalado
- Conocimientos básicos de shell y editor
- Proyecto `~/drivers/myfirst` de las secciones anteriores

Comencemos.

---

### Lab 7.1: Búsqueda en el código fuente

**Objetivo:** Familiarizarte con el árbol de código fuente de FreeBSD buscando e identificando patrones de drivers.

**Habilidades que se practican:**

- Navegar por `/usr/src/sys`
- Usar `grep` y `find`
- Leer código de drivers reales

**Instrucciones:**

**1. Localiza el driver null:**

```bash
% find /usr/src/sys -name "null.c" -type f
```

Salida esperada:

```text
/usr/src/sys/dev/null/null.c
```

**2. Abre y explora:**

```bash
% less /usr/src/sys/dev/null/null.c
```

**3. Encuentra las estructuras cdevsw:**

Busca `cdevsw` dentro de `less` escribiendo `/cdevsw` y pulsando Intro.

Deberías llegar a las líneas que definen `null_cdevsw`, `zero_cdevsw` y `full_cdevsw`.

**4. Encuentra el manejador de eventos del módulo:**

Busca `modevent`:

```text
/modevent
```

Deberías ver `null_modevent()`.

**5. Identifica la creación de dispositivos:**

Busca `make_dev`:

```text
/make_dev
```

Deberías encontrar tres llamadas que crean `/dev/null`, `/dev/zero` y `/dev/full`.

**6. Compara con tu driver:**

Abre tu `myfirst.c` y compara:

- ¿Cómo crea `null.c` los nodos de dispositivo? (Respuesta: `make_dev_credf` en el cargador del módulo)
- ¿Cómo los crea tu driver? (Respuesta: `make_dev_s` en `attach()`)

**7. Encuentra el driver LED:**

```bash
% find /usr/src/sys -name "led.c" -path "*/dev/led/*"
```

**8. Busca softc:**

```bash
% grep -n "struct ledsc" /usr/src/sys/dev/led/led.c | head -5
```

Deberías ver la definición de `struct ledsc` cerca del principio del archivo, justo después del bloque `#include`.

**9. Repite el proceso con if_tuntap:**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

Busca `tuntap_softc`. Observa cuánto más rico es que tu softc mínimo.

**10. Registra tus hallazgos:**

En tu cuaderno de laboratorio, escribe:

```text
Lab 7.1 Completed:
- Located null.c, led.c, if_tuntap.c
- Identified cdevsw, module loader, and softc structures
- Compared patterns to myfirst driver
- Key insight: [your observation]
```

**Criterios de éxito:**

- [ ] Encontrados los tres archivos de driver
- [ ] Localizados cdevsw y softc en cada uno
- [ ] Identificadas las llamadas de creación de dispositivos
- [ ] Comparado con tu driver

**Si te quedas atascado:** Usa `grep -r "DRIVER_MODULE" /usr/src/sys/dev/null/` para encontrar las macros clave.

---

### Lab 7.2: Compilar, cargar y verificar el ciclo de vida

**Objetivo:** Compilar tu driver, cargarlo en el kernel, verificar los eventos del ciclo de vida y descargarlo limpiamente.

**Habilidades que se practican:**

- Construir módulos del kernel
- Cargar y descargar con `kldload`/`kldunload`
- Inspeccionar `dmesg` y `devinfo`

**Instrucciones:**

**1. Ve a tu driver:**

```bash
% cd ~/drivers/myfirst
```

**2. Limpia y compila:**

```bash
% make clean
% make
```

Verifica que se creó `myfirst.ko`:

```bash
% ls -lh myfirst.ko
-rwxr-xr-x  1 youruser yourgroup  8.5K Nov  6 16:00 myfirst.ko
```

**3. Carga el módulo:**

```bash
% sudo kldload ./myfirst.ko
```

**4. Comprueba los mensajes del kernel:**

```bash
% dmesg | tail -n 5
```

Salida esperada:

```text
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**5. Verifica el árbol de dispositivos:**

```bash
% devinfo -v | grep myfirst
  myfirst0
```

**6. Comprueba el nodo de dispositivo:**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 16:00 /dev/myfirst0
```

**7. Consulta los sysctls:**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**8. Descarga el módulo:**

```bash
% sudo kldunload myfirst
```

**9. Verifica la limpieza:**

```bash
% dmesg | tail -n 2
myfirst0: Detaching: uptime 1234 ticks, opened 0 times
```

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

```bash
% sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

**10. Recarga y verifica la idempotencia:**

```bash
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
```

Todos los ciclos deben completarse sin errores.

**11. Registra los resultados:**

Cuaderno de laboratorio:

```text
Lab 7.2 Completed:
- Built myfirst.ko successfully
- Loaded without errors
- Verified attach messages in dmesg
- Verified /dev node and sysctls
- Unloaded cleanly
- Repeated load/unload cycle 3 times: all succeeded
```

**Criterios de éxito:**

- [ ] El módulo compila sin errores
- [ ] Se carga sin pánico del kernel
- [ ] Aparece el mensaje de attach en dmesg
- [ ] `/dev/myfirst0` existe mientras está cargado
- [ ] Los sysctls son legibles
- [ ] La descarga elimina todo
- [ ] La recarga funciona de forma fiable

**Resolución de problemas:**

- Si la compilación falla, comprueba la sintaxis del Makefile (tabuladores, no espacios).
- Si la carga falla con "Exec format error", comprueba que la versión del kernel y del código fuente coinciden.
- Si la descarga indica "module busy", comprueba que ningún proceso tenga el dispositivo abierto.

---

### Lab 7.3: Probar la apertura y el cierre del nodo de dispositivo

**Objetivo:** Interactuar con `/dev/myfirst0` desde el espacio de usuario y verificar que tus manejadores `open()` y `close()` son invocados.

**Habilidades que se practican:**

- Acceso a dispositivos desde el espacio de usuario
- Monitorizar los registros del driver
- Seguir los cambios de estado

**Instrucciones:**

**1. Carga el driver:**

```bash
% sudo kldload ./myfirst.ko
```

**2. Abre el dispositivo con `cat` (lectura):**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
```

(Sin salida, EOF inmediato)

**3. Comprueba los registros:**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

**4. Escribe en el dispositivo:**

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
```

**5. Comprueba los registros de nuevo:**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

**6. Verifica el contador sysctl:**

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 2
```

**7. Prueba el acceso exclusivo:**

Abre dos terminales.

Terminal 1:

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 10'
```

(Esto mantiene el dispositivo abierto durante 10 segundos)

Terminal 2 (rápidamente, mientras el terminal 1 sigue en espera):

```bash
% sudo sh -c 'cat < /dev/myfirst0'
cat: /dev/myfirst0: Device busy
```

¡Funciona! El acceso exclusivo está garantizado.

**8. Intenta descargar mientras está abierto:**

Terminal 1 (mantén el dispositivo abierto):

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

Terminal 2:

```bash
% sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

Comprueba dmesg:

```bash
% dmesg | tail -n 2
myfirst0: Cannot detach: device is open
```

¡Perfecto! Tu `detach()` rechaza correctamente la descarga mientras el dispositivo está en uso.

**9. Cierra y vuelve a intentar la descarga:**

Terminal 1: Espera a que `sleep 30` termine (o interrumpe con Ctrl+C).

Terminal 2:

```bash
% sudo kldunload myfirst
(succeeds)
```

**10. Registra los resultados:**

Cuaderno de laboratorio:

```text
Lab 7.3 Completed:
- Opened device with cat, verified open/close logs
- Opened device with echo, verified counter increment
- Exclusive access enforced (second open returned EBUSY)
- Detach refused while device open
- Detach succeeded after close
```

**Criterios de éxito:**

- [ ] La apertura activa el manejador `open()` (registrado)
- [ ] El cierre activa el manejador `close()` (registrado)
- [ ] El contador sysctl se incrementa en cada apertura
- [ ] La segunda apertura devuelve `EBUSY`
- [ ] detach devuelve `EBUSY` mientras está abierto
- [ ] detach tiene éxito tras el cierre

**Resolución de problemas:**

- Si no ves los registros "Device opened", comprueba que `device_printf()` está presente en tu manejador `open()`.
- Si el acceso exclusivo no se aplica, verifica la comprobación `if (sc->is_open) return (EBUSY)` en `open()`.

---

### Lab 7.4: Simular un fallo en attach y verificar el desenrollado

**Objetivo:** Introducir un fallo deliberado en `attach()` y verificar que la limpieza es correcta (sin fugas, sin panics).

**Habilidades practicadas:**

- Prueba de rutas de error
- Depuración de fallos en attach
- Verificación de la limpieza de recursos

**Instrucciones:**

**1. Añadir un fallo simulado:**

Edita `myfirst.c` y añade esto después de la inicialización del mutex en `attach()`:

```c
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* SIMULATED FAILURE FOR LAB 7.4 */
        device_printf(dev, "Simulating attach failure for testing\n");
        error = ENXIO;
        goto fail_mtx;

        /* (rest of attach continues...) */
```

**2. Recompilar:**

```bash
% make clean && make
```

**3. Intentar cargar el módulo:**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

**4. Revisar dmesg:**

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulating attach failure for testing
```

**5. Verificar que no hay fugas:**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

```bash
% sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

```bash
% devinfo -v | grep myfirst
(no output)
```

El dispositivo falló al hacer el attach y no quedó ningún recurso sin liberar.

**6. Intentar cargar de nuevo:**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

Sigue fallando de forma limpia (sin panics por doble inicialización).

**7. Eliminar el fallo simulado:**

Edita `myfirst.c` y elimina o comenta el bloque del fallo simulado.

**8. Recompilar y cargar normalmente:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -n 5
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**9. Probar otro punto de fallo:**

Inyecta el fallo después de crear el nodo de dispositivo:

```c
        error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
        if (error != 0) {
                device_printf(dev, "Failed to create device node: %d\n", error);
                goto fail_mtx;
        }

        /* SIMULATED FAILURE AFTER DEV NODE CREATION */
        device_printf(dev, "Simulating failure after dev node creation\n");
        error = ENOMEM;
        goto fail_dev;
```

**10. Recompilar y probar:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Cannot allocate memory
```

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulating failure after dev node creation
```

**11. Verificar que el nodo en `/dev` fue destruido:**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

Aunque el nodo llegó a crearse, la ruta de error lo destruyó correctamente.

**12. Eliminar la simulación y restaurar el funcionamiento normal:**

Elimina el segundo fallo simulado, recompila y carga el módulo con normalidad.

**13. Registrar los resultados:**

Cuaderno de laboratorio:

```text
Lab 7.4 Completed:
- Simulated failure after mutex init: cleanup correct
- Simulated failure after dev node creation: cleanup correct
- Verified no leaks in either case
- Verified no panics on repeated load attempts
- Restored normal operation
```

**Criterios de éxito:**

- [ ] Fallo simulado tras el mutex: sin fugas de recursos
- [ ] Fallo simulado tras el nodo de dispositivo: nodo destruido
- [ ] Múltiples intentos de carga no producen panics
- [ ] Funcionamiento normal restaurado tras eliminar la simulación

**Resolución de problemas:**

- Si aparece un panic, la ruta de error tiene un fallo. Comprueba que cada `goto` salte a la etiqueta correcta.
- Si hay fugas de recursos, asegúrate de que cada etiqueta de limpieza es alcanzable y está correctamente definida.

---

### ¡Laboratorios completados!

Ya has:

- Navegado por el árbol de código fuente de FreeBSD (Laboratorio 7.1)
- Construido, cargado y verificado tu driver (Laboratorio 7.2)
- Probado `open`/`close` y el acceso exclusivo (Laboratorio 7.3)
- Verificado el desenrollado de errores con fallos simulados (Laboratorio 7.4)

**Date una palmadita en la espalda.** Has pasado de leer sobre drivers a **construir y probar uno tú mismo**. Este es un hito importante.

---

## Ejercicios breves

Estos ejercicios refuerzan los conceptos de este capítulo. Son **opcionales pero recomendables** si quieres profundizar en tu comprensión antes de continuar.

### Ejercicio 7.1: Añadir un indicador sysctl

**Tarea:** Añade un nuevo sysctl de solo lectura que indique si el dispositivo está actualmente abierto.

**Pasos:**

1. En `attach()`, añade:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "is_open", CTLFLAG_RD,
    &sc->is_open, 0, "1 if device is currently open");
```

2. Recompila, carga y prueba:

```bash
% sysctl dev.myfirst.0.stats.is_open
dev.myfirst.0.stats.is_open: 0

% sudo sh -c 'exec 3<>/dev/myfirst0; sysctl dev.myfirst.0.stats.is_open; exec 3<&-'
dev.myfirst.0.stats.is_open: 1
```

**Resultado esperado:** El indicador muestra `1` mientras está abierto, `0` tras cerrar.

---

### Ejercicio 7.2: Registrar la primera y la última apertura

**Tarea:** Modifica `open()` para registrar solo la primera apertura y `close()` para registrar solo el último cierre.

**Pistas:**

- Comprueba `sc->open_count` antes y después de incrementarlo.
- En `close()`, decrementa un contador y comprueba si llega a cero.

**Comportamiento esperado:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
myfirst0: Device opened for the first time
myfirst0: Device closed (no more openers)

% sudo sh -c 'cat < /dev/myfirst0'
(no log: not the first open)
myfirst0: Device closed (no more openers)
```

---

### Ejercicio 7.3: Añadir un sysctl «Restablecer estadísticas»

**Tarea:** Añade un sysctl de solo escritura que restablezca `open_count` y `bytes_read` a cero.

**Pasos:**

1. Define un manejador:

```c
static int
sysctl_reset_stats(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int error, val;

        val = 0;
        error = sysctl_handle_int(oidp, &val, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        mtx_lock(&sc->mtx);
        sc->open_count = 0;
        sc->bytes_read = 0;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Statistics reset\n");
        return (0);
}
```

2. Regístralo:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "reset_stats", CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, sysctl_reset_stats, "I",
    "Write 1 to reset statistics");
```

3. Prueba:

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 5

% sudo sysctl dev.myfirst.0.stats.reset_stats=1
dev.myfirst.0.stats.reset_stats: 0 -> 1

% dmesg | tail -n 1
myfirst0: Statistics reset

% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 0
```

---

### Ejercicio 7.4: Probar la carga y descarga 100 veces

**Tarea:** Escribe un script que cargue y descargue tu driver 100 veces, comprobando si hay fallos o fugas de recursos.

**Script (`~/drivers/myfirst/stress_test.sh`):**

```bash
#!/bin/sh
set -e

for i in $(seq 1 100); do
        echo "Iteration $i"
        sudo kldload ./myfirst.ko
        sleep 0.1
        sudo kldunload myfirst
done

echo "Stress test completed: 100 cycles"
```

**Ejecuta:**

```bash
% chmod +x stress_test.sh
% ./stress_test.sh
```

**Resultado esperado:** Todas las iteraciones se completan sin errores.

**Si falla:** Comprueba `dmesg` en busca de mensajes de pánico o recursos con fugas.

---

### Ejercicio 7.5: Comparar tu driver con null.c

**Tarea:** Abre `/usr/src/sys/dev/null/null.c` en paralelo con tu `myfirst.c`. Enumera 5 similitudes y 5 diferencias.

**Observaciones de ejemplo:**

**Similitudes:**

1. Ambos usan `cdevsw` para las operaciones de dispositivo de caracteres.
2. Ambos crean nodos en `/dev`.
3. Ambos tienen manejadores `open` y `close`.
4. Ambos devuelven EOF en la lectura.
5. Ambos registran los eventos attach/detach.

**Diferencias:**

1. `null.c` usa el manejador `MOD_LOAD`; `myfirst` usa Newbus.
2. `null.c` no tiene un softc; `myfirst` sí.
3. `null.c` crea varios dispositivos (`null`, `zero`, `full`); `myfirst` crea uno.
4. `null.c` no usa sysctls; `myfirst` sí.
5. `null.c` no mantiene estado; `myfirst` lleva la cuenta de contadores.

---

## Desafíos opcionales

Estos son ejercicios avanzados para los lectores que quieran ir más allá de los fundamentos. No los intentes hasta haber completado todos los laboratorios y ejercicios.

### Desafío 7.1: Implementar un buffer de lectura simple

**Objetivo:** En lugar de devolver EOF inmediatamente, devuelve una cadena fija en `read()`.

**Pasos:**

1. Añade un buffer al softc:

```c
        char    read_buffer[64];  /* Data to return on read */
        size_t  read_len;         /* Length of valid data */
```

2. En `attach()`, rellena el buffer:

```c
        snprintf(sc->read_buffer, sizeof(sc->read_buffer),
            "Hello from myfirst driver!\n");
        sc->read_len = strlen(sc->read_buffer);
```

3. En `myfirst_read()`, copia los datos al espacio de usuario:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        size_t len;
        int error;

        len = MIN(uio->uio_resid, sc->read_len);
        if (len == 0)
                return (0);  /* EOF */

        error = uiomove(sc->read_buffer, len, uio);
        return (error);
}
```

4. Prueba:

```bash
% sudo cat /dev/myfirst0
Hello from myfirst driver!
```

**Comportamiento esperado:** La lectura devuelve la cadena una vez y luego EOF en las lecturas siguientes.

---

### Desafío 7.2: Permitir múltiples aperturas simultáneas

**Objetivo:** Elimina la comprobación de acceso exclusivo para que varios programas puedan abrir el dispositivo a la vez.

**Pasos:**

1. Elimina la comprobación `if (sc->is_open) return (EBUSY)` en `open()`.
2. Usa un **contador de referencias** en lugar de un indicador booleano:

```c
        int     open_refcount;  /* Number of current openers */
```

3. En `open()`:

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount++;
        mtx_unlock(&sc->mtx);
```

4. En `close()`:

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount--;
        mtx_unlock(&sc->mtx);
```

5. En `detach()`, rechaza si `open_refcount > 0`:

```c
        if (sc->open_refcount > 0) {
                device_printf(dev, "Cannot detach: device has %d openers\n",
                    sc->open_refcount);
                return (EBUSY);
        }
```

6. Prueba con dos terminales:

Terminal 1:

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

Terminal 2:

```bash
% sudo sh -c 'cat < /dev/myfirst0'
(succeeds instead of returning EBUSY)
```

---

### Desafío 7.3: Añadir un contador de escrituras

**Objetivo:** Registra cuántos bytes se han escrito en el dispositivo.

**Pasos:**

1. Añade al softc:

```c
        uint64_t        bytes_written;
```

**Nota:** Este es un manejador de escritura descartable exclusivo del Capítulo 7. Deliberadamente no usamos `uiomove()` ni almacenamos datos del usuario aquí. El movimiento completo de datos, el buffering y `uiomove()` se tratan en el Capítulo 9.

2. En `myfirst_write()`:

```c
        size_t len = uio->uio_resid;

        mtx_lock(&sc->mtx);
        sc->bytes_written += len;
        mtx_unlock(&sc->mtx);

        uio->uio_resid = 0;
        return (0);
```

3. Expón el valor mediante sysctl:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, ..., "bytes_written", ...);
```

4. Prueba:

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
% sysctl dev.myfirst.0.stats.bytes_written
dev.myfirst.0.stats.bytes_written: 5
```

---

### Desafío 7.4: Crear un segundo dispositivo (myfirst1)

**Objetivo:** Crea manualmente una segunda instancia del dispositivo para probar el soporte de múltiples dispositivos.

**Pista:** Ahora mismo, tu driver crea `myfirst0` automáticamente. Para probar varios dispositivos necesitarías desencadenar un segundo ciclo probe/attach. Esto es complejo (requiere manipulación a nivel de bus o clonación), así que considéralo solo como investigación.

**Enfoque alternativo:** Estudia `/usr/src/sys/net/if_tuntap.c` para ver cómo gestiona los dispositivos clone (creando nuevas instancias bajo demanda).

---

### Desafío 7.5: Implementar registro limitado por frecuencia

**Objetivo:** Añade un registro limitado por frecuencia para los eventos `open` (como máximo un mensaje por segundo).

**Pasos:**

1. Añade al softc:

```c
        time_t  last_open_log;
```

2. En `open()`:

```c
        time_t now = time_second;

        if (now - sc->last_open_log >= 1) {
                device_printf(sc->dev, "Device opened (count: %lu)\n",
                    (unsigned long)sc->open_count);
                sc->last_open_log = now;
        }
```

3. Prueba abriendo rápidamente:

```bash
% for i in $(seq 1 10); do sudo sh -c 'cat < /dev/myfirst0'; done
```

**Comportamiento esperado:** Solo aparecen unos pocos mensajes de registro (limitados por frecuencia).

---

## Errores comunes y árbol de decisión para diagnóstico

Incluso con un código cuidadoso, encontrarás problemas. Esta sección proporciona un árbol de decisión para diagnosticar los problemas más comunes con rapidez.

### Síntoma: El driver no carga

**Comprueba:**

- [ ] ¿Coincide `freebsd-version -k` con la versión de `/usr/src`?
  - **No:** Reconstruye el kernel o vuelve a clonar `/usr/src` para la versión correcta.
  - **Sí:** Continúa.

- [ ] ¿Se completa `make` sin errores?
  - **No:** Lee los mensajes de error del compilador. Causas comunes:
    - Puntos y comas faltantes
    - Llaves sin cerrar
    - Funciones no definidas (includes faltantes)
  - **Sí:** Continúa.

- [ ] ¿Falla `kldload` con «Exec format error»?
  - **Sí:** El kernel y el módulo no coinciden. Recompila con las fuentes correctas.
  - **No:** Continúa.

- [ ] ¿Falla `kldload` con «No such file or directory»?
  - **Sí:** Comprueba la ruta del módulo (`./myfirst.ko` frente a `/boot/modules/myfirst.ko`).
  - **No:** Continúa.

- [ ] Comprueba `dmesg` en busca de errores de attach:

```bash
% dmesg | tail -n 10
```

Busca mensajes de error de tu driver.

---

### Síntoma: El nodo de dispositivo no aparece

**Comprueba:**

- [ ] ¿Se completó `attach()` correctamente?

```bash
% dmesg | grep myfirst
```

Busca el mensaje «Attached successfully».

- [ ] Si attach falló, ¿se ejecutó el manejo de errores?

Busca mensajes de error en `dmesg`.

- [ ] ¿Se completó `make_dev_s()` correctamente?

Añade registro:

```c
device_printf(dev, "make_dev_s returned: %d\n", error);
```

- [ ] ¿Es correcto el nombre del nodo de dispositivo?

```bash
% ls -l /dev/myfirst*
```

Comprueba la ortografía y el número de unidad.

---

### Síntoma: Pánico del kernel al cargar

**Comprueba:**

- [ ] ¿Olvidaste `mtx_init()`?

Pánico durante `mtx_lock()`: olvidaste inicializar el mutex.

- [ ] ¿Desreferenciaste un puntero NULL?

Pánico con «NULL pointer dereference»: comprueba el valor de retorno de `device_get_softc()`.

- [ ] ¿Corrompiste la memoria?

Activa WITNESS e INVARIANTS en la configuración del kernel y recompila:

```text
options WITNESS
options INVARIANTS
```

Reinicia y vuelve a cargar tu driver. WITNESS detectará las violaciones de lock.

---

### Síntoma: Pánico del kernel al descargar

**Comprueba:**

- [ ] ¿Olvidaste `destroy_dev()`?

La descarga provoca un pánico cuando el usuario intenta acceder al nodo de dispositivo.

- [ ] ¿Olvidaste `mtx_destroy()`?

Los kernels con WITNESS activado entran en pánico al descargar si los locks no se destruyen.

- [ ] ¿Olvidaste `sysctl_ctx_free()`?

Las fugas de OID de sysctl pueden provocar pánicos al recargar.

- [ ] ¿Hay código en ejecución cuando descargas?

Comprueba:
  - Nodos de dispositivo abiertos (`sc->is_open` debe ser false)
  - Temporizadores o callbacks activos (no se usan en este capítulo, pero son habituales más adelante)

---

### Síntoma: «Device Busy» al descargar

**Comprueba:**

- [ ] ¿Está el dispositivo todavía abierto?

```bash
% fstat | grep myfirst
```

Si un proceso tiene el dispositivo abierto, la descarga fallará.

- [ ] ¿Devolviste `EBUSY` desde `detach()`?

Comprueba la lógica de `detach()`:

```c
if (sc->is_open) {
        return (EBUSY);
}
```

---

### Síntoma: Los sysctls no aparecen

**Comprueba:**

- [ ] ¿Se ejecutó `sysctl_ctx_init()`?

- [ ] ¿Tuvo éxito `SYSCTL_ADD_NODE()`?

Añade registro:

```c
if (sc->sysctl_tree == NULL) {
        device_printf(dev, "sysctl tree creation failed\n");
}
```

- [ ] ¿Es correcta la ruta del sysctl?

```bash
% sysctl dev.myfirst
```

Comprueba si hay errores tipográficos en el nombre del driver o en el número de unidad.

---

### Síntoma: Los eventos open/close no se registran

**Comprueba:**

- [ ] ¿Añadiste `device_printf()` a los manejadores?

- [ ] ¿Está `cdevsw` registrado correctamente?

Comprueba que `args.mda_devsw = &myfirst_cdevsw` esté establecido antes de `make_dev_s()`.

- [ ] ¿Está `si_drv1` establecido correctamente?

```c
args.mda_si_drv1 = sc;
```

Si es NULL, `open()` fallará.

---

### Síntoma: El módulo carga pero no hace nada

**Comprueba:**

- [ ] ¿Se ejecutó `attach()`?

```bash
% dmesg | grep myfirst
```

Si no ves ninguna salida y tu driver se conecta a `nexus`, la causa más probable es que falte el método identify. Sin él, `nexus` no tiene ningún dispositivo `myfirst` que explorar y tu código no se llama nunca. Vuelve a leer la sección **Paso 6: Implementar Identify** anterior y confirma que `DEVMETHOD(device_identify, myfirst_identify)` está presente en tu tabla de métodos.

- [ ] ¿Está el dispositivo en el bus correcto?

Para los pseudo-dispositivos, usa `nexus` (y recuerda proporcionar `identify`). Para PCI, usa `pci`. Para USB, usa `usbus`.

- [ ] ¿Devolvió `probe()` una prioridad sin error?

Si `probe()` devuelve `ENXIO`, el driver no hará attach. Para nuestro pseudo-dispositivo, `BUS_PROBE_DEFAULT` es el valor correcto.

---

### Consejos generales de depuración

**Activa el arranque detallado:**

```bash
% sudo sysctl boot.verbose=1
```

Vuelve a cargar tu driver para ver mensajes más detallados.

**Usa la depuración con printf:**

Añade llamadas a `device_printf()` en puntos clave para trazar el flujo de ejecución.

**Comprueba el estado de los locks:**

Si usas WITNESS, comprueba el orden de los locks:

```bash
% sysctl debug.witness.fullgraph
```

**Guarda dmesg después de un pánico:**

```bash
% sudo dmesg -a > panic.log
```

Analiza el registro en busca de pistas.

---

## Rúbrica de autoevaluación

Usa esta rúbrica para evaluar tu comprensión antes de pasar al Capítulo 8.

### Conocimiento fundamental

**Valórate (1-5, donde 5 = totalmente seguro):**

- [ ] Puedo explicar qué es el softc y por qué existe. (Puntuación: __/5)
- [ ] Entiendo el ciclo de vida probe/attach/detach. (Puntuación: __/5)
- [ ] Puedo crear un nodo de dispositivo usando `make_dev_s()`. (Puntuación: __/5)
- [ ] Puedo implementar manejadores básicos de `open`/`close`. (Puntuación: __/5)
- [ ] Puedo añadir un sysctl de solo lectura. (Puntuación: __/5)
- [ ] Entiendo el patrón de desenrollado con etiqueta única. (Puntuación: __/5)
- [ ] Puedo probar los caminos de error con fallos simulados. (Puntuación: __/5)

**Puntuación total: __/35**

**Interpretación:**

- **30-35:** Excelente. Estás listo para el Capítulo 8.
- **25-29:** Bien. Repasa los puntos débiles antes de continuar.
- **20-24:** Suficiente. Vuelve a repasar los laboratorios y ejercicios.
- **<20:** Dedica más tiempo a este capítulo.

---

### Habilidades prácticas

**¿Puedes hacer esto sin consultar notas?**

- [ ] Construir un módulo del kernel con `make`.
- [ ] Cargar un módulo con `kldload`.
- [ ] Comprobar los mensajes de attach en `dmesg`.
- [ ] Consultar sysctls con `sysctl`.
- [ ] Abrir un nodo de dispositivo con `cat` o redirección del shell.
- [ ] Descargar un módulo con `kldunload`.
- [ ] Simular un fallo de attach.
- [ ] Verificar la limpieza tras el fallo.

**Puntuación:** 1 punto por habilidad. **Objetivo:** 7/8 o más.

---

### Lectura de código

**¿Reconoces estos patrones en código real de FreeBSD?**

- [ ] Identificar una estructura `cdevsw`.
- [ ] Localizar los métodos `probe()`, `attach()`, `detach()`.
- [ ] Encontrar la definición de la estructura softc.
- [ ] Identificar la macro `DRIVER_MODULE()`.
- [ ] Detectar el desenrollado de errores con etiquetas goto.
- [ ] Encontrar llamadas a `make_dev()` o `make_dev_s()`.
- [ ] Identificar la creación de sysctls (`SYSCTL_ADD_*`).

**Puntuación:** 1 punto por patrón. **Objetivo:** 6/7 o más.

---

### Comprensión conceptual

**Verdadero o falso:**

1. El softc es asignado por el driver en `attach()`. (**Falso**. Newbus lo asigna a partir del tamaño declarado en `driver_t`.)
2. `probe()` debe asignar recursos. (**Falso**. `probe()` solo inspecciona el dispositivo y decide si reclamarlo; `attach()` se encarga de la asignación.)
3. `detach()` debe deshacer todo lo que hizo `attach()`. (**Verdadero**.)
4. Las etiquetas de error deben estar en orden inverso al de la inicialización. (**Verdadero**.)
5. Puedes omitir `mtx_destroy()` si el módulo se está descargando. (**Falso**. Cada `mtx_init()` necesita su correspondiente `mtx_destroy()`.)
6. Los sysctls se limpian automáticamente cuando el módulo se descarga. (**Falso**. Solo si llamas a `sysctl_ctx_free()` sobre el contexto por dispositivo que inicializaste.)
7. `make_dev_s()` es más seguro que `make_dev()`. (**Verdadero**. Devuelve un error explícito y evita la condición de carrera en la que `make_dev()` podía fallar sin una forma clara de reportarlo.)
8. Un pseudo-dispositivo que hace attach al bus `nexus` debe proporcionar un método `identify`. (**Verdadero**. Sin él, el bus no tiene ningún dispositivo que detectar.)

**Puntuación:** 1 punto por respuesta correcta. **Objetivo:** 7/8 o más.

---

### Evaluación general

Suma tus puntuaciones:

- Conocimiento fundamental: __/35
- Habilidades prácticas: __/8
- Lectura de código: __/7
- Comprensión conceptual: __/8

**Total: __/58**

**Calificación:**

- **51-58:** A (Dominio excelente)
- **44-50:** B (Buena comprensión)
- **35-43:** C (Adecuado, pero repasa las áreas más débiles)
- **<35:** Repasa el capítulo antes de continuar

---

## Cerrando el capítulo y mirando hacia adelante

¡Enhorabuena! Has completado el capítulo 7 y has construido tu primer driver de FreeBSD desde cero. Reflexionemos sobre lo que has logrado y echemos un vistazo a lo que está por venir.

### Lo que has construido

Tu driver `myfirst` es mínimo pero completo:

- **Disciplina en el ciclo de vida:** probe/attach/detach limpio, sin fugas de recursos.
- **Superficie de usuario:** nodo `/dev/myfirst0` que abre y cierra de forma fiable.
- **Observabilidad:** sysctls de solo lectura que muestran el tiempo de attach, el número de aperturas y los bytes leídos.
- **Gestión de errores:** patrón de desenrollado con una sola etiqueta que se recupera de forma elegante ante los fallos.
- **Registro de eventos:** uso correcto de `device_printf()` para eventos del ciclo de vida y errores.

Esto no es un juguete. Es un **andamiaje de calidad de producción**, la misma base de la que parte cualquier driver de FreeBSD.

### Lo que has aprendido

Ahora comprendes:

- Cómo Newbus descubre y conecta drivers
- El papel del softc (estado por dispositivo)
- Cómo crear y destruir nodos de dispositivo
- Cómo exponer métricas a través de sysctls
- Cómo gestionar errores sin fugar recursos
- Cómo probar las rutas del ciclo de vida (carga/descarga, apertura/cierre, fallos simulados)

Estas habilidades son **transferibles**. Ya escribas un driver PCI, un driver USB o una interfaz de red, usarás estos mismos patrones.

### Lo que aún falta (y por qué)

Tu driver todavía no hace mucho:

- **Semántica de lectura/escritura:** definida pero sin implementar. (**Capítulos 8 y 9**)
- **Buffering:** sin colas, sin ring buffers. (**Capítulo 10**)
- **Interacción con hardware:** sin registros, sin DMA, sin interrupciones. (**Parte 4**)
- **Concurrencia:** mutex presente pero sin ejercitar. (**Parte 3**)
- **I/O real:** sin bloqueo, sin poll/select. (**Capítulo 10**)

Esto fue intencionado. **Domina la estructura antes que la complejidad.** No aprenderías carpintería construyendo un rascacielos el primer día. Empezarías con un banco de trabajo, igual que has hecho aquí.

### Lo que viene a continuación

**Capítulo 8, Trabajando con archivos de dispositivo.** Permisos y propietario en devfs, nodos persistentes y las comprobaciones desde el espacio de usuario que utilizarás para inspeccionar y probar tu dispositivo.

**Capítulos 9 y 10, Leyendo y escribiendo en dispositivos, y gestionando la entrada y salida de forma eficiente.** Implementa lectura y escritura con `uiomove`, introduce buffering y control de flujo, y define la semántica de bloqueo, no bloqueo y `poll` o `kqueue` con una gestión de errores adecuada.

---

### Tus próximos pasos

**Antes de pasar al capítulo 8:**

1. **Completa todos los laboratorios** de este capítulo si aún no lo has hecho.
2. **Intenta al menos dos ejercicios** para reforzar los patrones aprendidos.
3. **Prueba tu driver a fondo:** cárgalo y descárgalo 10 veces, ábrelo y ciérralo 10 veces, simula un fallo más.
4. **Guarda tu código en Git:** esto es un hito importante.

```bash
% cd ~/drivers/myfirst
% git add myfirst.c Makefile
% git commit -m "Chapter 7 complete: minimal driver with lifecycle discipline"
```

5. **Descansa.** Te lo has ganado. La programación del kernel es intensa, y el tiempo de consolidación es importante.

**Cuando estés listo para el capítulo 8:**

- Extenderás este mismo driver (sin empezar desde cero).
- La estructura que has construido aquí seguirá siendo válida.
- Los conceptos se irán construyendo de forma incremental, sin reiniciarse.

### Unas palabras finales

Construir un driver desde cero puede resultar abrumador al principio. Pero mira lo que has logrado:

- Empezaste sin nada más que un Makefile y un archivo `.c` en blanco.
- Construiste un driver que compila, se carga, hace el attach, opera, hace el detach y se descarga de forma limpia.
- Probaste las rutas de error y verificaste la limpieza.
- Expusiste el estado a través de sysctls y creaste un nodo de dispositivo accesible desde el espacio de usuario.

**Esto no es suerte de principiante. Es competencia.** La mayoría de los desarrolladores de software de propósito general nunca tocan un módulo del kernel, y mucho menos construyen uno con rutas de attach y detach disciplinadas. Tú acabas de hacerlo.

El camino desde "hola módulo" hasta "driver de producción" es largo, pero has dado el paso más difícil: **empezar**. Cada capítulo a partir de aquí añade una capa más de capacidad, una herramienta más a tu arsenal.

Mantén actualizado tu cuaderno de laboratorio. Sigue experimentando. Sigue preguntando «¿por qué?» cuando algo no tenga sentido. Y, lo más importante, **sigue construyendo**.

Bienvenido al mundo del desarrollo de drivers de FreeBSD. Te has ganado tu lugar aquí.

### Nos vemos en el capítulo 8

En el próximo capítulo, daremos vida a tu nodo de dispositivo implementando semántica de archivo real: gestión del estado por apertura, manejo de acceso exclusivo frente a acceso compartido, y preparación del terreno para el I/O real.

Hasta entonces, disfruta de tu éxito. Has construido algo real, y eso merece celebrarse.

*«El experto en cualquier cosa fue una vez un principiante.» - Helen Hayes*
