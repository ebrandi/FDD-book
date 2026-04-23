---
title: "Escrevendo Seu Primeiro Driver"
description: "Um passo a passo prático que constrói um driver FreeBSD mínimo com disciplina rigorosa de ciclo de vida."
partNumber: 2
partName: "Building Your First Driver"
chapter: 7
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 600
language: "pt-BR"
---
# Escrevendo Seu Primeiro Driver

## Orientação ao Leitor e Objetivos

Bem-vindo à Parte 2. Se a Parte 1 foi sua base, onde você aprendeu o ambiente, a linguagem e a arquitetura, **a Parte 2 é onde você constrói**. Este capítulo marca o momento em que você para de ler sobre drivers e começa a escrever um.

Mas vamos ser claros sobre o que estamos construindo e, tão importante quanto, o que ainda não estamos construindo. Este capítulo segue uma abordagem de **disciplina em primeiro lugar**: você vai escrever um driver mínimo que realiza o attach de forma limpa, registra eventos corretamente, cria uma interface simples para o usuário e faz o detach sem vazamentos. Sem I/O sofisticado, sem acesso a registradores de hardware, sem tratamento de interrupções. Esses tópicos chegam depois, quando a disciplina já for algo natural.

### O Que Você Vai Construir

Quando você avançar para o próximo capítulo, terá um driver FreeBSD 14.3 funcional chamado `myfirst` que:

- **Realiza o attach como pseudo-dispositivo** usando o framework Newbus
- **Cria um nó `/dev/myfirst0`** (com implementação mínima, somente leitura por enquanto)
- **Expõe um sysctl somente leitura** mostrando o estado básico em tempo de execução
- **Registra eventos do ciclo de vida** de forma limpa com `device_printf()`
- **Trata erros** com um padrão de desfazimento com rótulo único
- **Realiza o detach de forma limpa** sem vazamentos de recursos ou ponteiros inválidos

Este driver ainda não fará nada de empolgante. Ele não vai ler hardware, não vai tratar interrupções e não vai processar pacotes ou blocos. O que ele *vai* fazer é demonstrar **disciplina de ciclo de vida**, a base da qual todo driver de produção depende.

### O Que Você **Não** Vai Construir (Ainda)

Este capítulo adia deliberadamente vários tópicos importantes para que você possa dominar a estrutura antes da complexidade:

- **Semântica completa de I/O**: `read(2)` e `write(2)` terão implementações mínimas. Os caminhos reais de leitura e escrita chegam no Capítulo 9, depois que o Capítulo 8 tiver coberto a política de arquivos de dispositivo e a visibilidade no userland.
- **Interação com hardware**: Sem acesso a registradores, sem DMA, sem interrupções. Esses tópicos são cobertos na **Parte 4**, quando você tiver uma base sólida.
- **Especificidades de PCI/USB/ACPI**: Usamos um pseudo-dispositivo (sem dependência de barramento) neste capítulo. Padrões de attach específicos de barramento aparecem na Parte 4 (PCI, interrupções, DMA) e na Parte 6 (USB, armazenamento, rede).
- **Locking e concorrência**: Você vai ver um mutex no softc, mas não vamos exercitar caminhos concorrentes complexos até a **Parte 3**.
- **sysctls avançados**: Apenas um nó somente leitura por enquanto. Árvores de sysctl maiores, handlers de escrita e tunables aparecem na Parte 5.

**Por que isso importa:** Tentar aprender tudo de uma vez leva à confusão. Ao manter o escopo restrito, você vai entender *por que* cada peça existe antes de adicionar a próxima camada.

### Estimativa de Tempo

- **Leitura apenas**: 2 a 3 horas para absorver os conceitos e os percursos pelo código
- **Leitura mais digitação dos exemplos**: 4 a 5 horas se você digitar o código do driver você mesmo
- **Leitura mais todos os quatro laboratórios**: 5 a 7 horas, incluindo ciclos de build, teste e verificação
- **Desafios opcionais**: Acrescente 2 a 3 horas para os exercícios de aprofundamento

**Ritmo recomendado:** Divida em duas ou três sessões. A primeira sessão vai até o scaffold e os fundamentos do Newbus, a segunda sessão vai até o registro de eventos e o tratamento de erros, a terceira sessão é dedicada aos laboratórios e aos testes de fumaça.

### Pré-requisitos

Antes de começar, certifique-se de que você tem:

- **FreeBSD 14.3** rodando no seu laboratório (VM ou hardware real)
- **Capítulos 1 a 6 concluídos** (especialmente a configuração do laboratório do Capítulo 2 e o tour anatômico do Capítulo 6)
- **`/usr/src` instalado** com os fontes do FreeBSD 14.3 correspondentes ao kernel em execução
- **Fluência básica em C** adquirida no Capítulo 4
- **Noções de programação de kernel** adquiridas no Capítulo 5

Verifique a versão do seu kernel:

```bash
% freebsd-version -k
14.3-RELEASE
```

Se isso não corresponder, revise as instruções de configuração do Capítulo 2.

### Resultados de Aprendizagem

Ao concluir este capítulo, você será capaz de:

- Criar o scaffold de um driver FreeBSD mínimo do zero
- Implementar e explicar os métodos de ciclo de vida probe/attach/detach
- Definir e usar uma estrutura softc de driver com segurança
- Criar e destruir nós `/dev` usando `make_dev_s()`
- Adicionar um sysctl somente leitura para observabilidade
- Tratar erros com desfazimento disciplinado (padrão de rótulo único `fail:`)
- Construir, carregar, testar e descarregar seu driver de forma confiável
- Identificar e corrigir erros comuns de iniciantes (vazamentos de recursos, dereferências de ponteiro nulo, limpeza ausente)

### Critérios de Sucesso

Você saberá que teve sucesso quando:

- `kldload ./myfirst.ko` concluir sem erros
- `dmesg -a` mostrar sua mensagem de attach
- `ls -l /dev/myfirst0` exibir seu nó de dispositivo
- `sysctl dev.myfirst.0` retornar o estado do seu driver
- `kldunload myfirst` limpar tudo sem vazamentos ou panics
- Você conseguir repetir o ciclo de carga/descarga de forma confiável
- Uma falha simulada de attach desfizer tudo de forma limpa (teste de caminho negativo)

### Onde Este Capítulo se Encaixa

Você está entrando na **Parte 2 - Construindo Seu Primeiro Driver**, a ponte entre teoria e prática:

- **Capítulo 7 (este capítulo)**: criar o scaffold de um driver mínimo com attach/detach limpos
- **Capítulo 8**: conectar as semânticas reais de `open()`, `close()` e arquivo de dispositivo
- **Capítulo 9**: implementar os caminhos básicos de `read()` e `write()`
- **Capítulo 10**: tratar buffering, bloqueio e poll/select

Cada capítulo se baseia no anterior, adicionando uma camada de funcionalidade por vez.

### Uma Nota Sobre "Hello World" Versus "Hello Production"

Você provavelmente já viu módulos de kernel do tipo "hello world": um handler de evento `MOD_LOAD` que imprime uma mensagem. Isso é válido para verificar se o sistema de build funciona, mas não é um driver. Ele não realiza attach em nada, não cria interfaces para o usuário e ensina quase nada sobre disciplina de ciclo de vida.

O driver `myfirst` deste capítulo é diferente. Ele ainda é mínimo, mas segue os padrões que você verá em todo driver FreeBSD de produção:

- Registra-se com o Newbus
- Implementa probe/attach/detach corretamente
- Gerencia recursos (mesmo que sejam triviais)
- Realiza a limpeza de forma confiável

Pense no `myfirst` como **hello production**, não hello world. A transição de brinquedo para ferramenta começa aqui.

### Como Usar Este Capítulo

1. **Leia sequencialmente**: Cada seção se baseia na anterior. Não pule à frente.
2. **Digite o código você mesmo**: A memória muscular importa. Copiar trechos é válido, mas digitar fixa os padrões.
3. **Complete os laboratórios**: Eles são pontos de verificação, não extras opcionais. Cada laboratório valida o entendimento antes de avançar.
4. **Use a lista de verificação final**: Antes de declarar vitória, percorra a lista de testes de fumaça (próxima ao final deste capítulo). Ela captura erros comuns.
5. **Mantenha um registro**: Anote o que funcionou, o que falhou e o que você aprendeu. O você do futuro vai agradecer.

### Uma Palavra Sobre Erros

Você *vai* encontrar erros. Vai esquecer de inicializar um ponteiro, vai pular uma etapa de limpeza, vai digitar errado o nome de uma função. Isso é esperado e **saudável**. Cada erro é uma oportunidade de praticar depuração, ler logs e entender causa e efeito.

Quando algo quebrar:

- Leia a mensagem de erro completa. As mensagens do kernel do FreeBSD são detalhadas.
- Verifique `dmesg -a` para eventos do ciclo de vida.
- Use a árvore de decisão de resolução de problemas (seção mais adiante neste capítulo).
- Revise a seção relevante e compare seu código com os exemplos.

Não passe rápido pelos erros. Eles são momentos de aprendizado.

### Vamos Começar

Você completou a base. Você percorreu drivers reais no Capítulo 6. Agora é hora de **construir o seu próprio**. Vamos começar com o scaffold do projeto.



## Scaffold do Projeto (esqueleto KLD)

Todo driver começa com um scaffold, uma estrutura básica que compila, carrega e descarrega sem fazer muita coisa. Pense nisso como a estrutura de uma casa: paredes, portas e móveis vêm depois. Por enquanto, estamos construindo a fundação e o esqueleto que sustentam tudo.

Nesta seção, você vai criar um projeto mínimo de driver FreeBSD 14.3 do zero. Ao final, você terá:

- Uma estrutura de diretórios limpa
- Um Makefile simples
- Um arquivo `.c` com o código mínimo absoluto de ciclo de vida
- Um build funcional que produz um módulo `myfirst.ko`

Este scaffold é **intencionalmente entediante**. Ele ainda não vai criar nós `/dev`, não vai implementar sysctls e não vai fazer nenhum trabalho real. Mas vai ensinar o ciclo de build, a estrutura básica e a disciplina de entrada e saída limpas. Domine isso, e todo o resto é apenas adicionar camadas.

### Estrutura de Diretórios

Vamos criar um workspace para o seu driver. A convenção na árvore de código-fonte do FreeBSD é manter os drivers em `/usr/src/sys/dev/<drivername>`, mas para o seu primeiro driver, vamos trabalhar no seu diretório home. Isso mantém seus experimentos isolados e torna a reconstrução simples.

Crie a estrutura:

```bash
% mkdir -p ~/drivers/myfirst
% cd ~/drivers/myfirst
```

Seu diretório de trabalho vai conter:

```text
~/drivers/myfirst/
├── myfirst.c      # Driver source code
└── Makefile       # Build instructions
```

É só isso. O sistema de build de módulos do kernel do FreeBSD (`bsd.kmod.mk`) cuida de toda a complexidade (flags do compilador, caminhos de include, linking, etc.) para você.

**Por que essa organização?**

- **Diretório único**: Mantém tudo junto, fácil de limpar (`rm -rf ~/drivers/myfirst`).
- **Nomeado como o driver**: Quando você tiver múltiplos projetos, saberá o que `~/drivers/myfirst` contém.
- **Segue os padrões da árvore**: Os drivers reais do FreeBSD em `/usr/src/sys/dev/` seguem a mesma abordagem de "um diretório por driver".

### O Makefile Mínimo

O sistema de build do FreeBSD é notavelmente simples para módulos do kernel. Crie o `Makefile` com estas três linhas:

```makefile
# Makefile for myfirst driver

KMOD=    myfirst
SRCS=    myfirst.c

.include <bsd.kmod.mk>
```

**Linha por linha:**

- `KMOD= myfirst` - Declara o nome do módulo. Isso vai produzir `myfirst.ko`.
- `SRCS= myfirst.c` - Lista os arquivos de código-fonte. Por enquanto temos apenas um.
- `.include <bsd.kmod.mk>` - Incorpora as regras de build de módulos do kernel do FreeBSD. Esta única linha substitui centenas de linhas de lógica manual de makefile.

**Importante:** A indentação antes de `.include` é um **caractere de tabulação**, não espaços. Se você usar espaços, o `make` vai falhar com um erro críptico. (A maioria dos editores pode ser configurada para inserir tabulações quando você pressiona a tecla Tab.)

**O que `bsd.kmod.mk` oferece:**

- Flags corretas do compilador para código de kernel (`-D_KERNEL`, `-ffreestanding`, etc.)
- Caminhos de include (`-I/usr/src/sys`, `-I/usr/src/sys/dev`, etc.)
- Regras de linking para criar arquivos `.ko`
- Alvos padrão: `make`, `make clean`, `make install`, etc.

Você não precisa entender os detalhes internos. Saiba apenas que `.include <bsd.kmod.mk>` oferece um sistema de build funcional de graça.

**Teste o Makefile:**

Antes de escrever qualquer código, teste a configuração do build:

```bash
% make clean
% ls
Makefile
```

Por enquanto, `make clean` não faz quase nada (ainda não há arquivos para deletar), mas confirma que a sintaxe do Makefile é válida.

### O `myfirst.c` Básico

Agora crie `myfirst.c`, o código-fonte real do driver. Esta primeira versão é **mínima por design**: ela compila, carrega e descarrega, mas não cria dispositivos, não trata I/O e não aloca recursos.

Aqui está o esqueleto:

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

**O que este código faz:**

- **Includes**: Incorpora os headers do kernel para a infraestrutura de módulos e registro de eventos.
- **`myfirst_loader()`**: Trata os eventos do ciclo de vida do módulo. Por enquanto, apenas MOD_LOAD e MOD_UNLOAD.
- **`moduledata_t`**: Conecta o nome do módulo à função de carga.
- **`DECLARE_MODULE()`**: Registra o módulo com o kernel. É isso que faz o `kldload` reconhecer seu módulo.
- **`MODULE_VERSION()`**: Marca o módulo com a versão 1 (incremente isso se você mudar o ABI exportado no futuro).

**O que este código não faz (ainda):**

- Não cria nenhum dispositivo
- Não chama `make_dev()`
- Não se registra com o Newbus
- Não aloca memória ou recursos

Isso é **apenas carga/descarga**, o mínimo absoluto para provar que o sistema de build funciona.

### Construindo e Testando o Scaffold

Vamos compilar e carregar este módulo mínimo:

**1. Build:**

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

Você verá a saída do compilador. Desde que termine com `myfirst.ko` sendo criado e sem erros, está tudo certo.

**2. Verifique a saída do build:**

```bash
% ls -l myfirst.ko
-rw-r--r--  1 youruser youruser 11592 Nov  7 00:15 myfirst.ko
```

(O tamanho do arquivo vai variar de acordo com o compilador e a arquitetura.)

**3. Carregue o módulo:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 2
myfirst: driver loaded
```

**4. Verifique se está carregado:**

```bash
% kldstat | grep myfirst
 6    1 0xffffffff82a38000     20b8 myfirst.ko
```

Seu módulo agora faz parte do kernel em execução.

**5. Descarregue o módulo:**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst: driver unloaded
```

**6. Confirme que foi removido:**

```bash
% kldstat | grep myfirst
(no output)
```

Perfeito. Seu scaffold funciona.

### O Que Acabou de Acontecer?

Vamos acompanhar o fluxo passo a passo:

1. **Build:** `make` invocou o sistema de build de módulos do kernel do FreeBSD, que compilou `myfirst.c` com as flags de kernel e o vinculou em `myfirst.ko`.
2. **Carregamento:** `kldload` leu `myfirst.ko`, vinculou-o ao kernel em execução e chamou sua função `myfirst_loader()` com `MOD_LOAD`.
3. **Log:** Seu `printf()` escreveu "myfirst: driver loaded" no buffer de mensagens do kernel.
4. **Descarregamento:** `kldunload` chamou seu loader com `MOD_UNLOAD`, você imprimiu uma mensagem e o kernel removeu seu código da memória.

**Ponto importante:** Este ainda não é um driver Newbus. Não há `probe()`, não há `attach()`, não há dispositivos. É apenas um módulo que carrega e descarrega. Pense nisso como o **estágio 0**: provar que o sistema de build funciona antes de adicionar qualquer complexidade.

### Solução de Problemas Comuns do Scaffold

**1. Problema:** `make` falha com "missing separator"

**Causa:** Seu Makefile usa espaços em vez de tabulações antes de `.include`.

**Solução:** Substitua os espaços iniciais por um caractere de tabulação.

**2. Problema:** `kldload` exibe "Exec format error"

**Causa:** Incompatibilidade entre a versão do seu kernel e a versão de `/usr/src`.

**Solução:** Verifique se `freebsd-version -k` corresponde à sua árvore de código-fonte. Reconstrua seu kernel ou clone novamente `/usr/src` para a versão correta.

**3. Problema:** O módulo carrega, mas nenhuma mensagem aparece em `dmesg`

**Causa:** O buffer de mensagens do kernel pode ter rolado, ou `printf()` está sendo limitado por taxa.

**Solução:** Use `dmesg -a` para ver todas as mensagens, incluindo as mais antigas. Verifique também `sysctl kern.msgbuf_show_timestamp`.

**4. Problema:** `kldunload` exibe "module busy"

**Causa:** Algo ainda está usando seu módulo (muito improvável com este scaffold mínimo).

**Solução:** Não aplicável aqui, mas mais tarde você verá isso se os nós de dispositivo ainda estiverem abertos ou os recursos não forem liberados.

### Boas Práticas de Build Limpo

Conforme você itera no seu driver, adote esses hábitos desde cedo:

**1. Sempre limpe antes de reconstruir:**

```bash
% make clean
% make
```

Isso garante que arquivos objeto desatualizados não contaminem seu build.

**2. Descarregue antes de reconstruir:**

```bash
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
```

Se o módulo não estiver carregado, `kldunload` falha sem consequências. O `|| true` impede que o shell pare.

**3. Use um script de rebuild:**

Crie `~/drivers/myfirst/rebuild.sh`:

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

Torne-o executável:

```bash
% chmod +x rebuild.sh
```

Agora você pode iterar rapidamente:

```bash
% ./rebuild.sh myfirst
```

Esse script descarrega, limpa, constrói, carrega e exibe as mensagens recentes do kernel, tudo de uma vez. É um grande economizador de tempo durante o desenvolvimento.

**Nota:** Você pode se perguntar se esse script precisa ser tão complexo. Para uso único, não precisa. No entanto, o desenvolvimento de módulos do kernel envolve ciclos repetidos de descarregar-reconstruir-carregar, muitas vezes dezenas de vezes por dia. Construí-lo com tratamento de erros e validação adequados cria uma ferramenta que você reutilizará com confiança ao longo do seu processo de desenvolvimento, economizando incontáveis horas posteriormente. Mais importante ainda, esta é uma oportunidade perfeita para praticar programação defensiva: validar entradas, verificar erros a cada etapa e fornecer feedback claro quando algo dá errado. Esses hábitos serão valiosos em todo o seu trabalho de desenvolvimento futuro.

### Ponto de Verificação no Controle de Versão

Antes de prosseguir, faça um commit do seu scaffold no Git (se você estiver usando controle de versão, e deveria estar):

```bash
% cd ~/drivers/myfirst
% git init
% git add Makefile myfirst.c
% git commit -m "Initial scaffold: loads and unloads cleanly"
```

Isso fornece um estado conhecido e funcional para retornar caso você quebre algo depois. Se você estiver usando um repositório remoto (GitHub, GitLab, etc.), pode enviar essas alterações com `git push`, mas isso não é obrigatório para os benefícios do controle de versão local.

### O Que Vem a Seguir?

Você agora tem um scaffold funcional: um módulo que compila, carrega e descarrega. Ainda não é um driver Newbus e não cria nenhuma superfície visível ao usuário, mas é uma base sólida.

Na próxima seção, adicionaremos a **integração com o Newbus**, transformando este módulo simples em um driver de pseudo-dispositivo adequado que se registra na árvore de dispositivos e implementa os métodos de ciclo de vida `probe()` e `attach()`.

## Newbus: Apenas o Suficiente para o Attach

Você construiu um scaffold que carrega e descarrega. Agora vamos transformá-lo em um **driver Newbus**, que se registra no framework de dispositivos do FreeBSD e segue o ciclo de vida padrão `identify` / `probe` / `attach` / `detach`.

É aqui que seu driver deixa de ser um módulo passivo e começa a se comportar como um driver de dispositivo real. Ao final desta seção, você terá um driver que:

- Se registra como um pseudo-dispositivo no barramento `nexus`
- Fornece um método `identify()` que cria o dispositivo `myfirst` no barramento
- Implementa `probe()` para reivindicar o dispositivo
- Implementa `attach()` para inicializar (mesmo que a inicialização seja mínima por ora)
- Implementa `detach()` para fazer a limpeza
- Registra eventos do ciclo de vida adequadamente

Estamos mantendo isso no **mínimo necessário** para mostrar o padrão. Sem alocação de recursos ainda, sem nós de dispositivo, sem sysctls. Esses elementos aparecem em seções posteriores. Por ora, concentre-se em entender **como o Newbus chama seu código** e **o que cada método deve fazer**.

### Por Que o Newbus?

O FreeBSD usa o Newbus para gerenciar a descoberta de dispositivos, a correspondência de drivers e o ciclo de vida. Mesmo para pseudo-dispositivos (dispositivos puramente de software sem hardware subjacente), seguir o padrão Newbus garante:

- Comportamento consistente em todos os drivers
- Integração adequada com a árvore de dispositivos
- Gerenciamento confiável do ciclo de vida (attach / detach / suspend / resume)
- Compatibilidade com ferramentas como `devinfo` e `kldunload`

**Modelo mental:** O Newbus é o departamento de RH do kernel. Ele abre novas vagas (identify), entrevista drivers para cada vaga (probe), contrata o mais adequado (attach) e gerencia as demissões (detach). Para hardware real, o barramento publica a vaga automaticamente. Para um pseudo-dispositivo, seu driver também escreve a descrição da vaga, e é para isso que o `identify` serve.

### O Padrão Mínimo do Newbus

Todo driver Newbus segue esta estrutura:

1. **Definir os métodos do dispositivo** (`identify`, `probe`, `attach`, `detach`) como funções
2. **Criar uma tabela de métodos** mapeando os nomes dos métodos Newbus para suas funções
3. **Declarar uma estrutura de driver** que inclui a tabela de métodos e o tamanho do softc
4. **Registrar o driver** com `DRIVER_MODULE()`

Para drivers que se conectam a um barramento real como `pci` ou `usb`, o barramento enumera o hardware por conta própria e pergunta a cada driver registrado "este dispositivo é seu?" por meio do `probe`. Um pseudo-dispositivo não tem hardware a enumerar, portanto precisamos informar ao barramento que o dispositivo existe. Essa é a função do `identify`. Vamos apresentá-lo no passo 4 abaixo, após o probe e o attach estarem no lugar, para que a função de cada método fique clara antes que o arquivo fique congestionado.

Vamos percorrer cada parte passo a passo.

### Passo 1: Incluir os Cabeçalhos do Newbus

No topo de `myfirst.c`, adicione esses includes (substituindo ou complementando os includes mínimos do scaffold):

```c
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>        /* For device_t, Newbus APIs */
#include <sys/conf.h>       /* For cdevsw (used later) */
```

**O que esses fornecem:**

- `<sys/bus.h>` - Tipos centrais do Newbus (`device_t`, `device_method_t`) e funções (`device_printf`, `device_get_softc`, etc.)
- `<sys/conf.h>` - Estruturas de switch de dispositivo de caracteres (usaremos isso ao criar nós `/dev`)

### Passo 2: Defina Seu Softc

O **softc** (contexto de software) é a estrutura de dados privada por dispositivo do seu driver. Mesmo que não estejamos armazenando nada interessante ainda, **todo driver Newbus tem um**.

Adicione isso próximo ao topo de `myfirst.c`, após os includes:

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

**Por que esses campos?**

- `dev` - Ponteiro de retorno conveniente. Permite chamar `device_printf(sc->dev, ...)` sem passar `dev` por toda parte.
- `attach_time` - Estado de exemplo. Registraremos quando o `attach()` foi executado.
- `is_ready` - Outro sinalizador de exemplo. Mostra como você rastrearia o estado do driver.

**Ponto-chave:** Você nunca chama `malloc()` ou `free()` no softc por conta própria. O Newbus faz isso automaticamente com base no tamanho que você declara na estrutura do driver.

### Passo 3: Implemente o Probe

O método `probe()` responde a uma pergunta: **"Este driver corresponde a este dispositivo?"**

Para um pseudo-dispositivo, a resposta é sempre sim (não estamos verificando IDs PCI ou assinaturas de hardware). Mas ainda implementamos `probe()` para seguir o padrão e definir uma descrição do dispositivo.

Adicione esta função:

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

**Linha por linha:**

- `device_set_desc()` define uma descrição legível por humanos. Ela aparece em `devinfo -v` e nas mensagens de attach. A string deve permanecer válida durante a vida útil do dispositivo, portanto sempre passe um literal de string aqui. Se você precisar de uma descrição construída dinamicamente, use `device_set_desc_copy()` em vez disso.
- `return (BUS_PROBE_DEFAULT)` informa ao Newbus "Vou lidar com este dispositivo, com a prioridade padrão do sistema operacional base."

**Disciplina do probe:**

- **Não** aloque recursos em `probe()`. Se outro driver vencer, seus recursos vazarão.
- **Não** acesse o hardware em `probe()` (não relevante aqui, mas essencial para drivers de hardware real).
- **Retorne rapidamente.** O probe é chamado com frequência durante o boot e eventos de hot-plug.

**Uma nota sobre os valores de prioridade do probe.** Quando vários drivers estão dispostos a assumir o mesmo dispositivo, o kernel escolhe aquele cujo `probe()` retornou o valor **mais alto**. As constantes em `<sys/bus.h>` refletem essa ordenação, com as ofertas mais específicas sendo numericamente maiores:

| Constante               | Valor (FreeBSD 14.3) | Quando usar                                                            |
|-------------------------|----------------------|------------------------------------------------------------------------|
| `BUS_PROBE_SPECIFIC`    | `0`                  | Apenas este driver pode lidar com este dispositivo                     |
| `BUS_PROBE_VENDOR`      | `-10`                | Driver fornecido pelo fabricante, supera o driver genérico de classe   |
| `BUS_PROBE_DEFAULT`     | `-20`                | Driver padrão do sistema operacional base para esta classe             |
| `BUS_PROBE_LOW_PRIORITY`| `-40`                | Driver mais antigo ou menos desejável                                  |
| `BUS_PROBE_GENERIC`     | `-100`               | Driver genérico de fallback                                            |
| `BUS_PROBE_NOWILDCARD`  | muito negativo       | Corresponde apenas a dispositivos criados explicitamente (p. ex., pelo identify) |

`BUS_PROBE_DEFAULT` é a escolha certa para um driver típico, incluindo o nosso: identificamos nosso próprio dispositivo pelo nome em `identify()`, portanto nenhum concorrente real existe, e o valor é alto o suficiente para que nada nos supere.

### Passo 4: Implemente o Attach

O método `attach()` é onde você **inicializa seu driver**. Recursos são alocados, o hardware é configurado, nós de dispositivo são criados. Por ora, apenas registraremos uma mensagem e preencheremos o softc.

Adicione esta função:

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

**O que isso faz:**

- `device_get_softc(dev)` - Recupera o softc que o Newbus alocou para nós (zerado inicialmente).
- `sc->dev = dev` - Salva o ponteiro de retorno `device_t` por conveniência.
- `sc->attach_time = ticks` - Registra a contagem atual de ticks do kernel (um timestamp simples).
- `sc->is_ready = 1` - Define um sinalizador (não utilizado ainda, mas mostra como você rastrearia o estado).
- `device_printf()` - Registra o evento de attach com o prefixo do nome do nosso dispositivo.
- `return (0)` - Sucesso. Um valor não zero indicaria falha e abortaria a conexão.

**Disciplina do attach:**

- **Aloque** recursos aqui (memória, locks, mapeamentos de hardware).
- **Crie** superfícies de usuário (nós `/dev`, interfaces de rede, etc.).
- **Trate** falhas com cuidado. Se algo der errado, desfaça o que você iniciou e retorne um código de erro.
- **Não** acesse o espaço do usuário ainda. O attach é executado durante o carregamento do módulo ou a descoberta de dispositivos, antes que qualquer programa do usuário possa interagir com você.

**Prévia do tratamento de erros:**

Por ora, o attach não pode falhar (não estamos fazendo nada que possa dar errado). Seções posteriores adicionarão alocação de recursos, e você verá como desfazer as operações em caso de falha.

### Passo 5: Implemente o Detach

O método `detach()` é o inverso do `attach()`: desmonte o que você construiu, libere o que você reivindicou e não deixe rastros.

Adicione esta função:

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

**O que isso faz:**

- Recupera o softc (sabemos que ele existe porque o attach foi bem-sucedido).
- Registra por quanto tempo o driver esteve conectado (o valor atual de `ticks` menos `attach_time`).
- Limpa o sinalizador `is_ready` (não estritamente necessário, pois o softc será liberado em breve, mas é uma boa prática).
- Retorna 0 (sucesso).

**Disciplina do detach:**

- **Libere** todos os recursos (locks destruídos, memória liberada, nós de dispositivo destruídos).
- **Garanta** que nenhuma I/O ativa ou callback possa alcançar seu código após o retorno do detach.
- **Retorne** `EBUSY` se o dispositivo estiver em uso e não puder ser desconectado ainda (p. ex., nós de dispositivo abertos).
- **Não** presuma que o softc ainda é válido após o retorno do detach. O Newbus o liberará.

**Por que o detach é importante:**

Implementações deficientes do detach são a principal fonte de kernel panics no descarregamento. Se você esquecer de destruir um lock, liberar um recurso ou remover um callback, o sistema travará quando esse recurso for acessado após seu código ter sido removido.

### Passo 6: Implemente o Identify

Temos probe, attach e detach. Eles dizem ao kernel **o que fazer** quando um dispositivo `myfirst` aparece. Mas ainda não existe nenhum dispositivo `myfirst` no barramento nexus, e o nexus não tem como criar um por conta própria. Precisamos criar o dispositivo nós mesmos no momento em que nosso driver é registrado. É exatamente isso que um método `identify` faz.

Adicione esta função:

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

**Linha por linha:**

- `device_find_child(parent, driver->name, -1)` verifica se um dispositivo `myfirst` já existe abaixo de `parent`. Se não fizéssemos essa verificação, recarregar o módulo (ou qualquer segunda varredura do barramento) criaria dispositivos duplicados.
- `BUS_ADD_CHILD(parent, 0, driver->name, -1)` solicita ao barramento pai que crie um novo dispositivo filho chamado `myfirst`, na ordem `0`, com um número de unidade escolhido automaticamente. Após essa chamada, o Newbus executará nosso `probe` contra o novo filho e, se o probe aceitar, executará o attach.
- Registramos no log em caso de falha, mas não causamos um panic. `BUS_ADD_CHILD` pode falhar sob pressão de memória, e a ausência de um pseudo-dispositivo não deve derrubar o sistema.

**Onde isso se encaixa.** O `identify` é executado uma vez por driver por barramento, quando o driver é anexado pela primeira vez a esse barramento. Após o identify, o mecanismo normal de probe e attach do barramento assume o controle. Este é o mesmo padrão utilizado por drivers como `cryptosoft`, `aesni` e `snd_dummy` na árvore de código-fonte do FreeBSD, que você poderá consultar mais adiante como referência.

### Passo 7: Crie a Tabela de Métodos

Agora conecte suas funções aos nomes de métodos do Newbus. Adicione isto após as definições de suas funções:

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

**O que essa tabela significa:**

- `DEVMETHOD(device_identify, myfirst_identify)` diz "quando o barramento convida cada driver a criar seus dispositivos, execute `myfirst_identify()`."
- `DEVMETHOD(device_probe, myfirst_probe)` diz "quando o kernel chama `DEVICE_PROBE(dev)`, execute `myfirst_probe()`."
- O mesmo vale para attach e detach.
- `DEVMETHOD_END` encerra a tabela e é obrigatório.

**Por baixo dos panos:** O macro `DEVMETHOD()` e o sistema kobj (objetos do kernel) geram o código de ligação que despacha para suas funções. Você não precisa entender os detalhes internos; basta saber que essa tabela é como o Newbus encontra seu código.

### Passo 8: Declare o Driver

Una tudo em uma estrutura `driver_t`:

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

**Parâmetros:**

- `"myfirst"` - Nome do driver (aparece nos logs e como prefixo do nome do dispositivo).
- `myfirst_methods` - Ponteiro para a tabela de métodos que você acabou de criar.
- `sizeof(struct myfirst_softc)` - Informa ao Newbus quanta memória alocar por dispositivo.

**Por que o tamanho do softc?** O Newbus aloca um softc por instância de dispositivo. Ao declarar o tamanho aqui, você nunca precisa alocá-lo ou liberá-lo manualmente: o Newbus gerencia o ciclo de vida.

### Passo 9: Registre com DRIVER_MODULE

Substitua o antigo macro `DECLARE_MODULE()` do scaffold por este:

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

**O que isso faz:**

- `DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0)` registra `myfirst` como um driver disposto a se anexar abaixo do barramento `nexus`. Os dois zeros ao final são um handler de evento de módulo opcional e seu argumento; nosso driver mínimo não precisa deles.
- `MODULE_VERSION(myfirst, 1)` marca o módulo com a versão 1, para que outros módulos possam declarar uma dependência dele.

**Por que `nexus`?**

`nexus` é o barramento raiz do FreeBSD, o topo da árvore de dispositivos de qualquer arquitetura. O Capítulo 6 aconselhou você, corretamente, que `nexus` raramente é o pai adequado para um driver de hardware real: um driver PCI pertence a `pci`, um driver USB pertence a `usbus`, e assim por diante. Pseudo-dispositivos são diferentes. Eles não têm barramento físico, então a convenção na árvore de código-fonte do FreeBSD é anexá-los a `nexus` e criar o dispositivo filho por conta própria através de um método `identify`. É exatamente isso que `cryptosoft`, `aesni` e `snd_dummy` fazem, e é exatamente o que estamos fazendo aqui.

### Passo 10: Remova o Antigo Carregador de Módulo

Você não precisa mais da função `myfirst_loader()` nem da estrutura `moduledata_t` do scaffold. O Newbus agora conduz o ciclo de vida do módulo por meio de `identify`, `probe`, `attach` e `detach`. Remova completamente essas peças antigas.

Seu arquivo `myfirst.c` deve conter agora:

- Includes
- Estrutura softc
- `myfirst_identify()`
- `myfirst_probe()`
- `myfirst_attach()`
- `myfirst_detach()`
- Tabela de métodos
- Estrutura do driver
- `DRIVER_MODULE()` e `MODULE_VERSION()`

Sem mais handler de evento `MOD_LOAD`.

### Passo 11: Ajuste o Makefile

Adicione esta linha ao seu Makefile:

```makefile
# Required for Newbus drivers: generates device_if.h and bus_if.h
SRCS+=   device_if.h bus_if.h
```

**Por que isso é necessário:**

O framework Newbus do FreeBSD usa um sistema de despacho de métodos construído sobre o kobj. As entradas `DEVMETHOD()` na sua tabela de métodos se referem a identificadores de método declarados nos cabeçalhos gerados `device_if.h` e `bus_if.h`. O `bsd.kmod.mk` sabe como construí-los a partir de `/usr/src/sys/kern/device_if.m` e `/usr/src/sys/kern/bus_if.m`, mas só o faz se você listá-los em `SRCS`. Se você esquecer essa linha, receberá um erro confuso sobre identificadores de método desconhecidos ao compilar.

### Construa e Teste o Driver Newbus

Vamos compilar e testar:

**1. Limpe e construa:**

```bash
% make clean
% make
```

Você não deve ver nenhum erro.

**2. Carregue o módulo:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
```

Observe:

- O nome do dispositivo é `myfirst0` (nome do driver + número de unidade).
- Ele se anexou "on nexus0" (o barramento pai).
- Sua mensagem de attach personalizada apareceu.

**3. Verifique a árvore de dispositivos:**

```bash
% devinfo -v | grep myfirst
    myfirst0
```

Seu driver agora faz parte da árvore de dispositivos.

**4. Descarregue:**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst0: Detaching (was attached for 5432 ticks)
```

Sua mensagem de detach mostra por quanto tempo o driver ficou anexado.

**5. Verifique que foi removido:**

```bash
% devinfo -v | grep myfirst
(no output)
```

### O Que Mudou?

Em comparação com o scaffold, seu driver agora:

- **Registra-se no Newbus** em vez de usar um carregador de módulo simples.
- **Adiciona um dispositivo filho** (`myfirst0`) à árvore de dispositivos por meio de `identify`.
- **Segue o ciclo de vida identify / probe / attach / detach** em vez de apenas load/unload.
- **Aloca e gerencia um softc** automaticamente.

Este é o **padrão fundamental** de todo driver FreeBSD. Domine isso e o restante é apenas adicionar camadas.

### Erros Comuns no Newbus (e Como Evitá-los)

**Erro 0: Esquecer o método identify em um pseudo-dispositivo**

**Sintoma:** `kldload` é concluído com sucesso, mas nenhum dispositivo `myfirst0` aparece, nenhuma mensagem de probe no `dmesg`, e `devinfo` não mostra nada abaixo de `nexus0`. O driver compilou e foi carregado, mas nunca se anexou.

**Causa:** O driver foi registrado com `DRIVER_MODULE(..., nexus, ...)` mas nenhum método `device_identify` foi fornecido. O nexus não tem nada a enumerar, então probe e attach nunca são chamados.

**Correção:** Adicione o método `identify` mostrado no Passo 6 e coloque `DEVMETHOD(device_identify, myfirst_identify)` na tabela de métodos. Esta é a razão mais comum para um driver de pseudo-dispositivo de iniciante "carregar mas não fazer nada."

---

**Erro 1: Alocar recursos no probe**

**Errado:**

```c
static int
myfirst_probe(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        sc->something = malloc(...);  /* BAD! */
        return (BUS_PROBE_DEFAULT);
}
```

**Por que está errado:** Se o probe falhar ou outro driver ganhar, sua alocação vaza.

**Correto:** Aloque em `attach()`, onde você sabe que o driver foi selecionado.

---

**Erro 2: Esquecer de retornar 0 do attach**

**Errado:**

```c
static int
myfirst_attach(device_t dev)
{
        /* ... setup ... */
        /* (missing return statement) */
}
```

**Por que está errado:** O compilador pode alertar, mas o valor de retorno fica indefinido. Você pode acidentalmente retornar lixo, fazendo o attach falhar de forma misteriosa.

**Correto:** Sempre termine o attach com `return (0)` em caso de sucesso ou `return (error_code)` em caso de falha.

---

**Erro 3: Não limpar no detach**

**Errado:**

```c
static int
myfirst_detach(device_t dev)
{
        device_printf(dev, "Detaching\n");
        return (0);
        /* (forgot to free resources, destroy locks, etc.) */
}
```

**Por que está errado:** Recursos vazam. Locks permanecem ativos. O próximo carregamento pode causar um panic.

**Correto:** O detach deve desfazer tudo que o attach fez. Vamos cobrir o padrão de limpeza em detalhes na seção de tratamento de erros.

### Diagrama de Tempo do Ciclo de Vida do Newbus

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

**Insight fundamental:** Cada passo é distinto. Identify cria o dispositivo, probe o reivindica, attach inicializa, detach limpa. Não misture as fronteiras entre eles.

### Verificação Rápida

Antes de avançar, certifique-se de que consegue responder a estas perguntas:

1. **Onde devo alocar memória para o estado do driver?**
   Resposta: Em `attach()`, ou simplesmente use o softc (o Newbus o aloca para você).

2. **O que `device_get_softc()` retorna?**
   Resposta: Um ponteiro para os dados privados por dispositivo do seu driver (`struct myfirst_softc *` neste caso).

3. **Quando probe é chamado?**
   Resposta: Durante a enumeração de dispositivos. Para um barramento real, isso acontece quando o barramento descobre um dispositivo. Para nosso pseudo-dispositivo, acontece logo após nosso método `identify` chamar `BUS_ADD_CHILD()` para colocar um dispositivo `myfirst` no barramento nexus.

4. **O que detach deve fazer?**
   Resposta: Desfazer tudo que attach fez, liberar recursos e garantir que nenhum caminho de código possa alcançar o driver depois disso.

5. **Por que usamos `nexus` para este driver, e por que ele precisa de um método `identify`?**
   Resposta: Porque é um pseudo-dispositivo sem barramento físico. `nexus` é o pai convencional para dispositivos puramente de software, mas o nexus não tem dispositivos a enumerar, então criamos os nossos por meio de `identify`.

Se essas respostas fazem sentido, você está pronto para a próxima seção: adicionar gerenciamento real de estado com o softc.

---

## softc e Estado do Ciclo de Vida

Você viu a estrutura softc declarada, alocada e recuperada, mas ainda não falamos sobre **por que ela existe** ou **como usá-la corretamente**. Nesta seção, vamos explorar o padrão softc em profundidade: o que vai nela, como inicializá-la e acessá-la com segurança, e como evitar armadilhas comuns.

O softc é a **memória** do seu driver. Cada recurso, cada lock, cada estatística e cada flag vive aqui. Acertar isso é a diferença entre um driver confiável e um que entra em panic sob carga.

### O Que É o softc?

O **softc** (contexto de software) é uma estrutura por dispositivo que armazena tudo que seu driver precisa para operar. Pense nela como o "espaço de trabalho" ou o "caderno" do driver: uma instância por dispositivo, contendo todo o estado que faz aquele dispositivo específico funcionar.

**Propriedades principais:**

- **Por dispositivo:** Se seu driver gerencia múltiplos dispositivos (por exemplo, `myfirst0`, `myfirst1`), cada um recebe seu próprio softc.
- **Alocado pelo kernel:** Você declara o tipo e o tamanho da estrutura; o Newbus aloca e zera a memória.
- **Ciclo de vida:** Existe desde a criação do dispositivo (antes de `attach()`) até a exclusão do dispositivo (após `detach()`).
- **Padrão de acesso:** Recupere com `device_get_softc(dev)` no início de cada método.

**Por que não usar variáveis globais?**

Variáveis globais não conseguem lidar com múltiplos dispositivos. Se você armazenasse estado em variáveis globais, `myfirst0` e `myfirst1` sobrescreveriam os dados um do outro. O padrão softc resolve isso de forma elegante: cada dispositivo tem seu próprio estado isolado.

### O Que Pertence ao softc?

Um softc bem projetado contém:

**1. Identificação e manutenção**

- `device_t dev` - Ponteiro de retorno para o dispositivo (para logging e callbacks)
- `int unit` - Número de unidade do dispositivo (frequentemente extraído de `dev`, mas conveniente de manter em cache)
- `char name[16]` - String de nome do dispositivo, caso você precise dela com frequência

**2. Recursos**

- `struct resource *mem_res` - Regiões MMIO (para drivers de hardware)
- `int mem_rid` - ID de recurso para memória
- `struct resource *irq_res` - Recurso de interrupção
- `void *irq_handler` - Cookie do handler de interrupção
- `bus_dma_tag_t dma_tag` - Tag DMA (para drivers que usam DMA)

**3. Primitivas de sincronização**

- `struct mtx mtx` - Mutex para proteger o estado compartilhado
- `struct sx sx` - Lock compartilhado/exclusivo, se necessário
- `struct cv cv` - Variável de condição para suspender e acordar

**4. Flags de estado do dispositivo**

- `int is_attached` - Definido em attach, limpo em detach
- `int is_open` - Definido quando o nó `/dev` está aberto
- `uint32_t flags` - Bitfield para estado diverso (running, suspended, error, etc.)

**5. Estatísticas e contadores**

- `uint64_t tx_packets` - Pacotes transmitidos (exemplo de driver de rede)
- `uint64_t rx_bytes` - Bytes recebidos
- `uint64_t errors` - Contagem de erros
- `time_t last_reset` - Quando as estatísticas foram limpas pela última vez

**6. Dados específicos do driver**

- Registradores de hardware, filas, buffers, estruturas de trabalho, qualquer coisa exclusiva da operação do seu driver.

**O que não pertence:**

- **Buffers grandes:** O softc vive em memória do kernel (wired, não paginável). Buffers grandes devem ser alocados separadamente com `malloc()` ou `contigmalloc()` e referenciados por ponteiros a partir do softc.
- **Dados constantes:** Use arrays globais `const` ou tabelas estáticas.
- **Variáveis temporárias:** Variáveis locais de função são adequadas. Não sobrecarregue o softc com temporárias por operação.

### Nosso softc myfirst (Exemplo Mínimo)

Vamos revisitar e expandir nossa definição de softc:

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

**Campo a campo:**

- `dev` - Ponteiro de retorno padrão. Quase todo driver inclui este.
- `unit` - Número de unidade em cache (de `device_get_unit(dev)`). Opcional, mas conveniente.
- `mtx` - Mutex para proteger acesso concorrente. Mesmo que ainda não estejamos exercitando concorrência, incluí-lo agora ensina bons hábitos.
- `attach_ticks` - Quando nos anexamos (ticks do kernel). Estado de exemplo simples.
- `open_count` / `bytes_read` - Contadores. Drivers reais rastreiam esses valores para estatísticas e observabilidade.
- `is_attached` / `is_open` - Flags para o estado do ciclo de vida. Úteis na verificação de erros.

**Por que um mutex agora?**

Mesmo que nosso driver mínimo não precise dele ainda, incluir o mutex ensina o **padrão**. Todo driver eventualmente precisará de locking, e é mais fácil projetá-lo desde o início do que acrescentá-lo depois.

Ainda não usamos o mutex para proteção real de dados compartilhados. Concorrência, ordenação de locks e armadilhas de deadlock chegam na Parte 3. Por ora, o lock está aqui para estabelecer o padrão de ciclo de vida e para garantir uma ordenação segura no detach. Veja a armadilha de "destruir locks enquanto threads ainda os mantêm" discutida no Capítulo 6.

### Inicializando o softc em attach()

O Newbus zera o softc antes de chamar `attach()`, mas você ainda precisa inicializar determinados campos explicitamente (locks, back-pointers, flags).

Veja o `attach()` atualizado:

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

**O que mudou:**

- **`mtx_init()`** - Inicializa o mutex. Parâmetros:
  - `&sc->mtx` - Endereço do campo mutex no softc.
  - `device_get_nameunit(dev)` - Retorna uma string como "myfirst0" (usada na depuração de locks).
  - `"myfirst"` - Nome do tipo de lock (aparece nos lock traces).
  - `MTX_DEF` - Mutex padrão (em oposição ao spin mutex).
- **`sc->is_attached = 1`** - Flag indicando que o driver está pronto.
- **Inicialização dos contadores** - Zerá-los explicitamente (mesmo que o Newbus já tenha zerado o softc inteiro, ser explícito documenta a intenção).

**Disciplina:** Inicialize todos os campos que serão testados posteriormente. Não assuma que "zero significa não inicializado" é sempre a semântica correta (para flags, talvez; para ponteiros, definitivamente não).

### Destruindo o softc em detach()

Em `detach()`, você deve desfazer tudo o que `attach()` fez. Para o softc, isso significa:

- Destruir locks (mutexes, sx, cv, etc.)
- Liberar qualquer memória ou recurso apontado pelo softc
- Limpar flags (não estritamente necessário, mas é uma boa prática)

`detach()` atualizado:

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

**O que há de novo:**

- **`if (sc->is_open) return (EBUSY)`** - Recusa o detach se o nó `/dev` ainda estiver aberto. Isso evita crashes por acesso a recursos já liberados.
- **Registro de estatísticas** - Mostra por quanto tempo o driver ficou ativo e o que fez.
- **`mtx_destroy(&sc->mtx)`** - **Crítico.** Todo `mtx_init()` deve ter um `mtx_destroy()` correspondente, ou você vaza recursos de lock do kernel.
- **Limpar flags** - Não é estritamente necessário (o Newbus liberará o softc em breve), mas é uma boa prática de programação defensiva.

**Erro comum:** Esquecer o `mtx_destroy()`. Isso causa panics no rastreamento de locks na próxima carga do módulo, caso você esteja usando kernels com WITNESS ou INVARIANTS.

### Acessando o softc em Outros Métodos

Todo método do driver que precisa de estado começa da mesma forma:

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

Esse é o **padrão idiomático** que você verá em todo driver FreeBSD. Uma linha para entrar no mundo do seu driver.

**Por que não passar o softc diretamente?**

Os métodos do Newbus são definidos para receber `device_t`. O softc é um detalhe de implementação do seu driver. Ao sempre recuperá-lo via `device_get_softc()`, o driver permanece flexível (você pode alterar a estrutura do softc sem precisar mudar as assinaturas dos métodos).

### Usando Locks para Proteger o softc

Mesmo sem ter adicionado operações concorrentes ainda, vamos antecipar o **padrão de locking** que você usará quando precisar.

**Padrão básico:**

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

**Regras:**

- **Adquira o lock antes de modificar estado compartilhado.**
- **Libere o lock assim que terminar** (não segure locks por mais tempo do que o necessário).
- **Nunca retorne com um lock adquirido** (a menos que você esteja usando padrões avançados de transferência de lock).
- **Documente a ordem dos locks** se você mantiver múltiplos locks simultaneamente (para evitar deadlock).

**Quando você precisará disso:** Assim que adicionar `open()`, `read()`, `write()` ou qualquer método que possa ser executado de forma concorrente (programas do usuário chamando seu driver a partir de múltiplas threads, ou handlers de interrupção atualizando estatísticas).

Por enquanto, o mutex existe mas não está sendo utilizado. Vamos usá-lo em seções posteriores, quando adicionarmos pontos de entrada concorrentes.

### Boas Práticas para o softc

**1. Mantenha-o organizado**

Agrupe campos relacionados:

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

**2. Comente campos não óbvios**

```c
        int             pending_requests;  /* Must hold mtx to access */
        time_t          last_activity;     /* Protected by mtx */
```

**3. Use tipos de largura fixa para contadores**

```c
        uint64_t        packets;  /* Not "unsigned long" */
        uint32_t        errors;   /* Not "int" */
```

**Por quê?** Portabilidade. Os tamanhos de `int` e `long` variam por arquitetura. `uint64_t` é sempre 64 bits.

**4. Evite desperdício de padding**

O compilador insere padding para alinhar campos. Coloque os campos maiores primeiro e os menores depois:

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

**5. Zere os campos que você vai testar**

```c
        sc->is_open = 0;       /* Explicit, even though Newbus zeroed it */
        sc->bytes_read = 0;
```

**Por quê?** Clareza. Quem lê o código sabe que você *quis* zerar o campo, e não que estava contando com a zeragem implícita.

### Depurando Problemas no softc

**Problema:** Kernel panic "NULL pointer dereference" no seu driver.

**Causa provável:** Você esqueceu de recuperar o softc, ou o recuperou após um ponto em que `dev` pode ser inválido.

**Solução:** Sempre coloque `sc = device_get_softc(dev);` no início de cada método.

---

**Problema:** Panic de mutex "already locked" ou "not locked".

**Causa provável:** `mtx_init()` esquecido em `attach()` ou chamadas desbalanceadas de `mtx_lock()` / `mtx_unlock()`.

**Solução:** Verifique seus pares init/destroy. Use kernels com WITNESS habilitado (`options WITNESS` no kernel config) para detectar violações de lock.

---

**Problema:** Estatísticas ou flags parecem aleatórias ou corrompidas.

**Causa provável:** Acesso concorrente sem locking, ou acesso ao softc após `detach()` tê-lo liberado.

**Solução:** Garanta que todo estado compartilhado esteja protegido pelo mutex. Garanta que nenhum caminho de código (callbacks, timers, threads) possa alcançar o driver após `detach()` retornar.

### Verificação Rápida

Antes de prosseguir, verifique se você entende:

1. **O que é o softc?**
   Resposta: Estrutura de dados privada por dispositivo que armazena todo o estado do driver.

2. **Quem aloca o softc?**
   Resposta: O Newbus, com base no tamanho declarado na estrutura `driver_t`.

3. **Quando você deve inicializar o mutex?**
   Resposta: Em `attach()`, antes de qualquer código que possa utilizá-lo.

4. **Quando você deve destruir o mutex?**
   Resposta: Em `detach()`, antes da função retornar.

5. **Por que recusamos o detach se `is_open` for verdadeiro?**
   Resposta: Para evitar liberar recursos enquanto programas do usuário ainda mantêm o dispositivo aberto, o que causaria um crash.

Se essas respostas estiverem claras, você está pronto para adicionar **disciplina de logging** na próxima seção.

---

## Etiqueta de Logging e Higiene do dmesg

Um driver bem-comportado **fala quando deve** e **fica em silêncio quando não deve**. Fazer log em excesso inunda o `dmesg` e dificulta a depuração; fazer log de menos deixa usuários e desenvolvedores às cegas quando algo dá errado. Esta seção ensina **quando, o que e como fazer log** em um driver FreeBSD.

Ao final, você saberá:

- Quais eventos **devem obrigatoriamente** ser registrados (attach, erros, mudanças críticas de estado)
- Quais eventos **deveriam** ser registrados (informações opcionais em nível de debug)
- Quais eventos **não devem** ser registrados (spam por pacote ou por operação)
- Como usar `device_printf()` de forma eficaz
- Como criar logging com rate limiting para hot paths
- Como tornar seus logs legíveis e úteis para diagnóstico

### Por que o Logging Importa

Quando um driver se comporta de forma incorreta, o `dmesg` costuma ser o primeiro lugar onde desenvolvedores e usuários procuram respostas. Bons logs respondem perguntas como:

- O driver fez attach com sucesso?
- Que hardware ele encontrou?
- Ocorreu algum erro? Por quê?
- O dispositivo está operacional ou em estado de erro?

Logs ruins enchem o console de spam, ocultam mensagens críticas ou omitem detalhes importantes.

**Modelo mental:** O logging é como um médico fazendo anotações durante um exame. Escreva o suficiente para diagnosticar problemas depois, mas não registre cada batimento cardíaco.

### As Regras de Ouro do Logging em Drivers

**Regra 1: Registre eventos de ciclo de vida**

Sempre registre:

- Attach bem-sucedido (uma linha por dispositivo)
- Falhas no attach (com o motivo)
- Detach bem-sucedido (opcional, mas recomendado)
- Falhas no detach (com o motivo)

**Exemplo:**

```c
device_printf(dev, "Attached successfully\n");
device_printf(dev, "Failed to allocate memory resource: error %d\n", error);
```

---

**Regra 2: Registre erros**

Quando algo der errado, **sempre registre o que e por quê**. Inclua:

- Qual operação falhou
- Código de erro (valor de errno)
- Contexto (se relevante)

**Exemplo:**

```c
if (error != 0) {
        device_printf(dev, "Could not allocate IRQ resource: error %d\n", error);
        return (error);
}
```

**Exemplo ruim:**

```c
if (error != 0) {
        return (error);  /* User sees nothing! */
}
```

---

**Regra 3: Nunca faça log em hot paths**

"Hot path" = código que é executado frequentemente durante a operação normal (cada pacote, cada interrupção, cada chamada de read/write).

**Nunca faça isso:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int flag)
{
        device_printf(dev, "Read called\n");  /* BAD: spams logs */
        ...
}
```

**Por quê?** Se um programa lê do seu dispositivo em loop, você gerará milhares de linhas de log por segundo, tornando o console inutilizável.

**Quando registrar eventos em hot paths:** Apenas para depuração durante o desenvolvimento, protegido por um flag de debug ou sysctl desabilitado por padrão.

---

**Regra 4: Use device_printf() para mensagens específicas de dispositivo**

`device_printf()` adiciona automaticamente o nome do dispositivo como prefixo da mensagem:

```c
device_printf(dev, "Interrupt timeout\n");
```

Saída:

```text
myfirst0: Interrupt timeout
```

Isso deixa imediatamente claro **qual dispositivo** está enviando a mensagem, especialmente quando existem múltiplas instâncias.

**Não use `printf()` simples:**

```c
printf("Interrupt timeout\n");  /* Which device? Unknown. */
```

---

**Regra 5: Use rate limiting em avisos de caminhos de erro repetitivos**

Se um erro pode se repetir rapidamente (por exemplo, timeout de DMA a cada frame), aplique rate limiting:

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

**Esse padrão registra a primeira ocorrência, suprime as repetições e registra novamente quando a condição muda.**

---

**Regra 6: Seja conciso e informativo**

Compare:

**Ruim:**

```c
device_printf(dev, "Something went wrong in the code here\n");
```

**Bom:**

```c
device_printf(dev, "Failed to map BAR0 MMIO region: error %d\n", error);
```

O exemplo bom informa **o que** falhou, **onde** (BAR0) e **como** (código de erro).

### Padrões de Logging para Eventos Comuns

**Attach com sucesso:**

```c
device_printf(dev, "Attached successfully, hardware rev %d.%d\n",
    hw_major, hw_minor);
```

**Falha no attach:**

```c
device_printf(dev, "Attach failed: could not allocate IRQ\n");
goto fail;
```

**Detach:**

```c
device_printf(dev, "Detached, uptime %lu seconds\n",
    (unsigned long)(ticks - sc->attach_ticks) / hz);
```

**Falha na alocação de recurso:**

```c
if (sc->mem_res == NULL) {
        device_printf(dev, "Could not allocate memory resource\n");
        error = ENXIO;
        goto fail;
}
```

**Estado inesperado do hardware:**

```c
if (status & DEVICE_ERROR_BIT) {
        device_printf(dev, "Hardware reported error 0x%x\n", status);
        /* attempt recovery or fail */
}
```

**Primeiro open:**

```c
if (sc->open_count == 0) {
        device_printf(dev, "Device opened for the first time\n");
}
```

(Mas somente se isso for incomum ou relevante; não registre cada open em produção.)

### Macro de Logging com Rate Limiting (Prévia Avançada)

Para erros que podem se repetir rapidamente, você pode definir uma macro de logging com rate limiting:

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

Isso limita o log a **uma vez por segundo**, mesmo que a condição seja acionada milhares de vezes.

**Quando usar:** Apenas para erros em hot paths que poderiam gerar spam nos logs (tempestades de interrupção, estouros de fila, etc.). Não é necessário para attach/detach ou erros raros.

### O que Registrar durante o Desenvolvimento versus Produção

**Desenvolvimento (verboso):**

- Entrada e saída de cada função (protegida por um flag de debug)
- Leituras e escritas de registradores
- Transições de estado
- Alocação e liberação de recursos

**Produção (silencioso):**

- Ciclo de vida do attach/detach
- Erros
- Mudanças críticas de estado (link up/down, reset do dispositivo)
- Primeira ocorrência de erros repetitivos

**Transição:** Comece verboso e reduza à medida que o driver se estabiliza. Deixe o logging de debug protegido por guards de tempo de compilação ou sysctl para futuras investigações.

### Usando Sysctls para Logging de Debug

Em vez de codificar a verbosidade diretamente, exponha um sysctl:

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

Em seguida, envolva os logs de debug:

```c
if (myfirst_debug) {
        device_printf(dev, "DEBUG: entering attach\n");
}
```

**Benefício:** Usuários ou desenvolvedores podem habilitar o logging sem recompilar:

```bash
% sysctl hw.myfirst.debug=1
```

Abordaremos sysctls em detalhes na próxima seção; isso é apenas uma prévia.

### Inspecionando os Logs

**Ver todas as mensagens do kernel:**

```bash
% dmesg -a
```

**Ver mensagens recentes:**

```bash
% dmesg | tail -n 20
```

**Buscar mensagens do seu driver:**

```bash
% dmesg | grep myfirst
```

**Limpar o buffer de mensagens (ao testar repetidamente):**

```bash
% sudo dmesg -c > /dev/null
```

(Nem sempre é aconselhável, mas útil quando você quer um ponto de partida limpo para testes.)

### Exemplo: Logging no myfirst

Vamos adicionar logging disciplinado ao nosso driver. Atualize `attach()` e `detach()`:

**attach atualizado:**

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

**detach atualizado:**

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

**O que estamos registrando:**

- **Attach:** Confirma o sucesso e registra quando ocorreu.
- **Recusa de detach:** Se o dispositivo estiver aberto, explica por que o detach falhou.
- **Sucesso no detach:** Exibe o tempo de atividade e as estatísticas de uso.

Isso oferece a usuários e desenvolvedores visibilidade clara sobre os eventos do ciclo de vida.

### Erros Comuns de Logging

**Erro 1: Fazer log dentro de locks**

**Errado:**

```c
mtx_lock(&sc->mtx);
device_printf(dev, "Locked, doing work\n");  /* Can cause priority inversion */
/* ... work ... */
mtx_unlock(&sc->mtx);
```

**Por que é errado:** `device_printf()` pode bloquear (adquirindo locks internos). Chamá-lo enquanto seu mutex está adquirido pode causar deadlock ou inversão de prioridade.

**Certo:**

```c
mtx_lock(&sc->mtx);
/* ... work ... */
mtx_unlock(&sc->mtx);

device_printf(dev, "Work completed\n");  /* Log after releasing lock */
```

---

**Erro 2: Logs multilinha que podem se intercalar**

**Errado:**

```c
printf("myfirst0: Attach starting\n");
printf("myfirst0: Step 1\n");
printf("myfirst0: Step 2\n");
```

**Por que é errado:** Se outro driver ou componente do kernel registrar algo entre as suas linhas, a sua mensagem ficará fragmentada.

**Certo:**

```c
device_printf(dev, "Attach starting: step 1, step 2 completed\n");
```

Ou use um único `sbuf` (buffer de string) e imprima-o de uma só vez (avançado).

---

**Erro 3: Registrar dados sensíveis**

Não registre:

- Dados do usuário (conteúdo de pacotes, dados de arquivos, etc.)
- Chaves criptográficas ou segredos
- Qualquer coisa que viole expectativas de privacidade

**Sempre assuma que os logs são públicos.**

### Verificação Rápida

Antes de prosseguir, confirme que você entendeu:

1. **Quando você deve registrar logs?**
   Resposta: Eventos de ciclo de vida (attach/detach), erros e mudanças críticas de estado.

2. **Quando você não deve registrar logs?**
   Resposta: Em hot paths (interrupções, loops de leitura/escrita, operações por pacote).

3. **Por que usar `device_printf()` em vez de `printf()`?**
   Resposta: Porque inclui automaticamente o nome do dispositivo, tornando os logs mais claros.

4. **Como você limita a frequência de um log que pode se repetir rapidamente?**
   Resposta: Use uma flag ou timestamp para rastrear o horário do último log e suprimir repetições.

5. **O que todo log de erro deve incluir?**
   Resposta: O que falhou, por que (código de erro) e contexto suficiente para diagnosticar o problema.

Se tudo isso ficou claro, você está pronto para adicionar a primeira superfície visível ao usuário: um nó `/dev`.

---

## Uma Superfície Temporária para o Usuário: /dev (Prévia)

Todo driver precisa de uma forma para que programas do espaço do usuário interajam com ele. Para dispositivos de caracteres, essa superfície é um **nó de dispositivo** em `/dev`. Nesta seção, vamos criar `/dev/myfirst0`, mas ainda não implementaremos I/O completo, apenas o suficiente para mostrar o padrão e provar que o dispositivo é acessível a partir do espaço do usuário.

Isso é uma **prévia**, não a implementação completa. A semântica real de `read()` e `write()` vem nos **Capítulos 8 e 9**. Aqui, o foco está em:

- Criar o nó em `/dev` usando `make_dev_s()`
- Definir um `cdevsw` (character device switch) com métodos provisórios
- Tratar `open()` e `close()` para rastrear o estado do dispositivo
- Limpar o nó de dispositivo em `detach()`

Pense nisso como **instalar a porta da frente** antes de mobiliar a casa. A porta abre e fecha, mas os cômodos estão vazios.

### O que é um Character Device Switch (cdevsw)?

O **cdevsw** é uma estrutura que contém ponteiros de função para operações de dispositivos de caracteres: `open`, `close`, `read`, `write`, `ioctl`, `mmap`, entre outras. Quando um programa do usuário chama `open("/dev/myfirst0", ...)`, o kernel busca o cdevsw associado a esse nó de dispositivo e chama sua função `d_open`.

**Definição da estrutura** (abreviada):

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

**Ponto importante:** você fornece implementações para as operações que seu dispositivo suporta e deixa as demais como `NULL` (o que o kernel interpreta como "não suportado" ou "comportamento padrão").

### Definindo o cdevsw para myfirst

Vamos definir um cdevsw mínimo com handlers de `open` e `close`, e stubs de `read` / `write` por enquanto.

Adicione isso próximo ao topo de `myfirst.c`, após a definição do softc:

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

**O que isso significa:**

- `d_version = D_VERSION` - Carimbo de versão da API. Obrigatório.
- `d_open = myfirst_open` - Quando o usuário chama `open("/dev/myfirst0", ...)`, o kernel chama `myfirst_open()`.
- O mesmo vale para `close`, `read` e `write`.
- `d_name = "myfirst"` - Nome base para os nós de dispositivo (combinado com o número da unidade para formar `myfirst0`, `myfirst1`, etc.).

### Implementando open()

O handler de `open()` é chamado quando um programa do usuário abre o nó de dispositivo. É a sua oportunidade de:

- Verificar se o dispositivo está pronto
- Rastrear o estado de abertura (incrementar contadores, definir flags)
- Retornar um erro se o dispositivo não puder ser aberto (por exemplo, acesso exclusivo, hardware não pronto)

Adicione esta função:

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

**O que isso faz:**

- **`sc = dev->si_drv1`** - Recupera o softc. Quando criarmos o nó de dispositivo, armazenaremos o ponteiro do softc aqui.
- **`if (!sc->is_attached)`** - Verificação de sanidade. Se o dispositivo não estiver anexado, recusa a abertura.
- **`if (sc->is_open) return (EBUSY)`** - Impõe acesso exclusivo (apenas um opener por vez). Dispositivos reais podem permitir múltiplos openers; este é apenas um exemplo simples.
- **`sc->is_open = 1`** - Marca o dispositivo como aberto.
- **`sc->open_count++`** - Incrementa o contador de aberturas ao longo da vida do dispositivo.
- **`device_printf()`** - Registra o evento de abertura (por enquanto; você removeria isso em produção).

**Disciplina de lock:** Mantemos o mutex enquanto verificamos e atualizamos `is_open`, garantindo segurança entre threads.

### Implementando close()

O handler de `close()` é chamado quando a última referência ao dispositivo aberto é liberada. Limpe o estado específico da abertura aqui.

Adicione esta função:

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

**O que isso faz:**

- Limpa a flag `is_open`.
- Registra o evento de fechamento.
- Retorna 0 (sucesso).

**Padrão simples:** `open()` define as flags, `close()` as limpa.

### Stubs de read() e write()

Vamos implementar stubs mínimos que retornam sucesso, mas não fazem nada. Isso prova que o nó de dispositivo está corretamente conectado, sem compromisso com a semântica de I/O ainda.

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

**O que esses stubs fazem:**

- **`read()`** - Retorna 0 (EOF), significando "nenhum dado disponível."
- **`write()`** - Define `uio->uio_resid = 0`, significando "todos os bytes foram escritos."

Programas do usuário vão enxergar isso como um dispositivo que "aceita escritas mas as descarta" e cujas "leituras retornam EOF imediatamente." Ainda não tem utilidade prática, mas prova que o encanamento funciona.

### Criando o Nó de Dispositivo em attach()

Agora vamos juntar tudo. Em `attach()`, crie o nó em `/dev` e associe-o ao seu softc.

Adicione isso ao final de `myfirst_attach()`, logo antes do `return (0)`:

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

**O que isso faz:**

- **`make_dev_args_init(&args)`** - Inicializa a estrutura de argumentos com valores padrão.
- **`args.mda_devsw = &myfirst_cdevsw`** - Associa este cdev ao nosso cdevsw.
- **`args.mda_uid / gid / mode`** - Define o proprietário e as permissões. `0600` significa leitura e escrita apenas pelo root.
- **`args.mda_si_drv1 = sc`** - Armazena o ponteiro do softc para que `open()` / `close()` possam recuperá-lo.
- **`make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit)`** - Cria `/dev/myfirst0` (ou `myfirst1`, etc., baseado no número da unidade).
- **Tratamento de erros:** Se `make_dev_s()` falhar, destroi o mutex e retorna o erro.

**Importante:** Salvamos o `struct cdev *` em `sc->cdev` para que possamos destruí-lo mais tarde em `detach()`.

**Adicione o campo cdev ao softc:**

Atualize `struct myfirst_softc`:

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

### Destruindo o Nó de Dispositivo em detach()

Em `detach()`, você deve remover o nó de `/dev` **antes** de o softc ser liberado.

Adicione isso próximo ao início de `myfirst_detach()`, após a verificação de `is_open`:

```c
        /* Destroy /dev node */
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
```

**O que isso faz:**

- **`destroy_dev(sc->cdev)`** - Remove `/dev/myfirst0` do sistema de arquivos. Quaisquer descritores de arquivo abertos são invalidados, e operações subsequentes sobre eles retornam erros.
- **`sc->cdev = NULL`** - Limpa o ponteiro (programação defensiva).

**A ordem importa:** Destrua o nó de dispositivo **antes** de destruir o mutex ou liberar outros recursos. Isso garante que nenhuma operação do espaço do usuário consiga alcançar seu driver depois que detach começa a desmontar tudo.

### Compilar, Testar e Verificar

Vamos compilar e testar o novo nó de dispositivo:

**1. Limpar e compilar:**

```bash
% make clean && make
```

**2. Carregar o driver:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
```

**3. Verificar o nó de dispositivo:**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 15:45 /dev/myfirst0
```

Sucesso! O nó de dispositivo existe.

**4. Testar abertura e fechamento:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
(no output, immediate EOF)
```

Verifique o dmesg:

```bash
% dmesg | tail -n 2
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

Seus handlers de `open()` e `close()` foram executados.

**5. Testar escrita:**

```bash
% sudo sh -c 'echo "hello" > /dev/myfirst0'
% dmesg | tail -n 2
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

A escrita foi bem-sucedida (embora os dados tenham sido descartados).

**6. Descarregar o driver:**

```bash
% sudo kldunload myfirst
% ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

O nó de dispositivo foi corretamente destruído no descarregamento.

### O que Acabou de Acontecer?

- Você criou um character device switch (`cdevsw`) mapeando syscalls para suas funções.
- Você implementou handlers de `open()` e `close()` que rastreiam o estado.
- Você criou stubs de `read()` e `write()` para provar que o encanamento funciona.
- Você criou `/dev/myfirst0` em `attach()` e o destruiu em `detach()`.
- Programas do usuário agora podem chamar `open("/dev/myfirst0", ...)` e interagir com o seu driver.

### Erros Comuns com Nós de Dispositivo

**Erro 1: Esquecer de destruir o nó de dispositivo em detach**

**Errado:**

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

**Por que está errado:** O nó de dispositivo persiste após o descarregamento. Tentar abri-lo causa um crash no kernel (o código foi removido, mas o nó permanece).

**Correto:** Sempre chame `destroy_dev()` em detach.

---

**Erro 2: Acessar o softc em open/close sem verificar is_attached**

**Errado:**

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

**Por que está errado:** Se `detach()` for executado de forma concorrente, o softc pode ser inválido.

**Correto:** Verifique `sc != NULL` e `sc->is_attached` antes de acessar o estado.

---

**Erro 3: Usar make_dev() em vez de make_dev_s()**

**Padrão antigo:**

```c
sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT, GID_WHEEL, 0600, "myfirst%d", sc->unit);
if (sc->cdev == NULL) {
        /* Error handling */
}
```

**Por que está desatualizado:** `make_dev()` pode falhar e retornar NULL, exigindo verificações de erro desajeitadas.

**Padrão moderno:** `make_dev_s()` retorna um código de erro, tornando o tratamento de erros mais limpo:

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
if (error != 0) {
        /* Handle error */
}
```

**Prefira `make_dev_s()`** em código novo.

### Verificação Rápida

Antes de avançar, confirme:

1. **O que é o cdevsw?**
   Resposta: Uma estrutura que mapeia syscalls (`open`, `read`, `write`, etc.) para funções do driver.

2. **Como open() recupera o softc?**
   Resposta: Via `dev->si_drv1`, que definimos ao criar o nó de dispositivo.

3. **Quando você deve chamar destroy_dev()?**
   Resposta: Em `detach()`, antes de o softc ser liberado.

4. **Por que verificamos `is_attached` em open()?**
   Resposta: Para garantir que o dispositivo não começou a se desanexar, o que poderia levar ao acesso de memória liberada.

5. **O que o stub de read() retorna?**
   Resposta: 0 (EOF), indicando que não há dados disponíveis.

Se isso estiver claro, você está pronto para adicionar **observabilidade via sysctl** na próxima seção.

---


## Um Pequeno Plano de Controle: sysctl Somente Leitura

Os nós de dispositivo em `/dev` permitem que programas do usuário enviem e recebam dados, mas não são a única forma de expor seu driver ao mundo externo. Os **sysctls** fornecem um plano leve de controle e observabilidade, permitindo que usuários e administradores consultem o estado do driver, leiam estatísticas e (opcionalmente) ajustem parâmetros em tempo de execução.

Nesta seção, vamos adicionar um **sysctl somente leitura** que expõe estatísticas básicas do driver. Isso dá a você uma amostra da infraestrutura de sysctl do FreeBSD sem comprometer-se com tunáveis de leitura e escrita completos ou hierarquias complexas (que voltam quando tratamos de observabilidade e depuração na Parte 5).

Ao final, você terá:

- Um nó sysctl em `dev.myfirst.0.*` mostrando o tempo de anexação, a contagem de aberturas e os bytes lidos
- Compreensão de sysctls estáticos versus dinâmicos
- Um padrão que você pode estender mais tarde para observabilidade mais complexa

### Por que os Sysctls São Importantes

Os sysctls fornecem **observabilidade fora de banda**, uma forma de inspecionar o estado do driver sem abrir o dispositivo nem disparar I/O. Eles são essenciais para:

- **Depuração:** "O driver está realmente anexado? Qual é o estado atual?"
- **Monitoramento:** "Quantas vezes este dispositivo foi aberto? Algum erro?"
- **Ajuste fino:** (Sysctls de leitura e escrita, abordados mais adiante) "Ajustar o tamanho do buffer ou o valor de timeout."

**Exemplo de uso:** Uma interface de rede pode expor `dev.em.0.rx_packets` e `dev.em.0.tx_errors` para que ferramentas de monitoramento acompanhem o desempenho sem analisar fluxos de pacotes.

**Modelo mental:** Sysctls são como um "painel de status" na lateral do seu driver, visível via comandos `sysctl` sem afetar a operação normal.

### A Árvore de Sysctl do FreeBSD

Os sysctls são organizados hierarquicamente, como um sistema de arquivos:

```ini
kern.ostype = "FreeBSD"
hw.ncpu = 8
dev.em.0.rx_packets = 123456
```

**Ramos comuns de nível superior:**

- `kern.*` - Parâmetros do kernel
- `hw.*` - Informações de hardware
- `dev.*` - Nós específicos de dispositivo (é aqui que seu driver vai)
- `net.*` - Parâmetros da pilha de rede

**O namespace do seu driver:** `dev.<nomedodriver>.<unidade>.*`

Para `myfirst`, isso significa `dev.myfirst.0.*` para a primeira instância.

### Sysctls Estáticos vs Dinâmicos

**Sysctls estáticos:**

- Declarados em tempo de compilação usando macros `SYSCTL_*`
- Simples de definir, mas não podem ser criados ou destruídos dinamicamente
- Bons para configurações em todo o driver ou constantes

**Exemplo:**

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw, OID_AUTO, myfirst_debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

**Sysctls dinâmicos:**

- Criados em tempo de execução (geralmente em `attach()`)
- Podem ser destruídos em `detach()`
- Bons para estado por instância de dispositivo (como estatísticas de `myfirst0`, `myfirst1`, etc.)

**Neste capítulo, usaremos sysctls dinâmicos** para que cada instância de dispositivo tenha seus próprios nós.

### Adicionando um Contexto de Sysctl ao softc

Sysctls dinâmicos requerem um **contexto de sysctl** (`struct sysctl_ctx_list`) para rastrear os nós que você cria. Isso torna a limpeza automática quando você libera o contexto.

Adicione estes campos a `struct myfirst_softc`:

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

**O que esses campos fazem:**

- `sysctl_ctx` - Rastreia todos os nós sysctl que criamos. Quando chamamos `sysctl_ctx_free()`, todos os nós são destruídos automaticamente.
- `sysctl_tree` - OID (Object Identifier) raiz para `dev.myfirst.0.*`. Os nós filhos se conectam aqui.

### Criando a Árvore de Sysctl em attach()

Adicione este código a `myfirst_attach()`, após criar o nó em `/dev`:

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

**O que isso faz:**

1. **`sysctl_ctx_init(&sc->sysctl_ctx)`** - Inicializa o contexto (deve ser o primeiro passo).

2. **`SYSCTL_ADD_NODE()`** - Cria um nó de subárvore `dev.myfirst.0.stats`. Parâmetros:
   - `&sc->sysctl_ctx` - Contexto que possui este nó.
   - `SYSCTL_CHILDREN(device_get_sysctl_tree(dev))` - Pai (a árvore sysctl do dispositivo).
   - `OID_AUTO` - Atribui automaticamente um número de OID.
   - `"stats"` - Nome do nó.
   - `CTLFLAG_RD | CTLFLAG_MPSAFE` - Somente leitura, MP-safe.
   - `0` - Função handler (nenhuma para um nó).
   - `"Driver statistics"` - Descrição.

3. **`SYSCTL_ADD_U64()`** - Adiciona um sysctl de inteiro sem sinal de 64 bits. Parâmetros:
   - `&sc->sysctl_ctx` - Contexto.
   - `SYSCTL_CHILDREN(sc->sysctl_tree)` - Pai (subárvore `stats`).
   - `OID_AUTO` - Atribui OID automaticamente.
   - `"attach_ticks"` - Nome da folha.
   - `CTLFLAG_RD` - Somente leitura.
   - `&sc->attach_ticks` - Ponteiro para a variável a ser exposta.
   - `0` - Dica de formato (0 = padrão).
   - `"Tick count..."` - Descrição.

4. **Tratamento de erros:** Se a criação do nó falhar, limpe os recursos e retorne `ENOMEM`.

**Resultado:** Agora você tem três sysctls:

- `dev.myfirst.0.stats.attach_ticks`
- `dev.myfirst.0.stats.open_count`
- `dev.myfirst.0.stats.bytes_read`

### Destruindo a Árvore de Sysctl em detach()

A limpeza é simples: libere o contexto e todos os nós são destruídos automaticamente.

Adicione isto ao `myfirst_detach()`, após destruir o nó de dispositivo:

```c
        /* Free sysctl context (destroys all nodes) */
        sysctl_ctx_free(&sc->sysctl_ctx);
```

É só isso. Uma linha cuida de tudo.

**Por que é seguro:** `sysctl_ctx_free()` percorre a lista do contexto e remove cada nó. Desde que você os tenha criado via o contexto, a limpeza é automática.

### Build, Carregamento e Teste dos Sysctls

**1. Limpe e construa:**

```bash
% make clean && make
```

**2. Carregue o driver:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 4
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**3. Consulte os sysctls:**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**4. Abra o dispositivo e verifique novamente:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 1
```

O contador incrementou!

**5. Descarregue e verifique a limpeza:**

```bash
% sudo kldunload myfirst
% sysctl dev.myfirst.0.stats
sysctl: unknown oid 'dev.myfirst.0.stats'
```

Os sysctls foram corretamente destruídos.

### Tornando os Sysctls Mais Úteis

Por ora, os sysctls apenas expõem números brutos. Vamos torná-los mais amigáveis.

**Adicione um sysctl de uptime legível:**

Em vez de expor contagens de ticks brutos, calcule o uptime em segundos.

Adicione uma função handler:

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

**Registre-a no attach():**

```c
        SYSCTL_ADD_PROC(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "uptime_seconds", CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, sysctl_uptime_seconds, "QU",
            "Seconds since driver attached");
```

**Teste:**

```bash
% sysctl dev.myfirst.0.stats.uptime_seconds
dev.myfirst.0.stats.uptime_seconds: 42
```

Muito mais legível do que contagens de ticks brutas!

### Sysctls de Leitura vs. Leitura e Escrita

Nossos sysctls são somente leitura (`CTLFLAG_RD`). Para torná-los graváveis, use `CTLFLAG_RW` e adicione um handler que valide a entrada.

**Exemplo (apenas para visualização, não implementado agora):**

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

Voltaremos aos sysctls de leitura e escrita na Parte 5, quando examinaremos depuração e ferramentas de observabilidade em profundidade. Por enquanto, a exposição somente leitura é suficiente.

### Boas Práticas com Sysctls

**1. Exponha métricas significativas**

- Contadores (pacotes, erros, aberturas, fechamentos)
- Flags de estado (attached, aberto, habilitado)
- Valores derivados (uptime, throughput, utilização)

**Não exponha:**

- Ponteiros internos ou endereços (risco de segurança)
- Dados brutos sem sentido (use handlers para formatar adequadamente)

---

**2. Use nomes e descrições claros**

**Bom:**

```c
SYSCTL_ADD_U64(..., "rx_packets", ..., "Packets received");
```

**Ruim:**

```c
SYSCTL_ADD_U64(..., "cnt1", ..., "Counter");
```

---

**3. Agrupe sysctls relacionados em subárvores**

```text
dev.myfirst.0.stats.*    (statistics)
dev.myfirst.0.config.*   (tunable parameters)
dev.myfirst.0.debug.*    (debug flags and counters)
```

---

**4. Proteja o acesso concorrente**

Se um sysctl lê ou escreve estado compartilhado, segure o lock apropriado:

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

**5. Limpe em detach**

Sempre chame `sysctl_ctx_free(&sc->sysctl_ctx)` no detach, ou você vaza OIDs.

### Erros Comuns com Sysctls

**Erro 1: Esquecer sysctl_ctx_init**

**Errado:**

```c
SYSCTL_ADD_NODE(&sc->sysctl_ctx, ...);  /* Context not initialized! */
```

**Por que está errado:** Um contexto não inicializado causa panics ou vazamentos.

**Certo:** Chame `sysctl_ctx_init(&sc->sysctl_ctx)` no attach antes de adicionar nós.

---

**Erro 2: Não liberar o contexto em detach**

**Errado:**

```c
static int
myfirst_detach(device_t dev)
{
        /* ... destroy other resources ... */
        return (0);
        /* Forgot sysctl_ctx_free! */
}
```

**Por que está errado:** Os nós de sysctl persistem após o descarregamento. O próximo acesso causa crash.

**Certo:** Sempre chame `sysctl_ctx_free(&sc->sysctl_ctx)` em detach.

---

**Erro 3: Expor ponteiros brutos**

**Errado:**

```c
SYSCTL_ADD_PTR(..., "softc_addr", ..., &sc, ...);  /* Security hole! */
```

**Por que está errado:** Vaza o layout do espaço de endereços do kernel (bypass de KASLR).

**Certo:** Nunca exponha ponteiros via sysctls.

### Verificação Rápida

Antes de avançar, confirme:

1. **O que é um sysctl?**
   Resposta: Uma variável do kernel ou valor computado exposto via o comando `sysctl`.

2. **Onde ficam os sysctls de um driver?**
   Resposta: Sob `dev.<drivername>.<unit>.*`.

3. **O que você deve chamar em attach() antes de adicionar nós?**
   Resposta: `sysctl_ctx_init(&sc->sysctl_ctx)`.

4. **O que limpa todos os nós de sysctl em detach()?**
   Resposta: `sysctl_ctx_free(&sc->sysctl_ctx)`.

5. **Por que usar um handler em vez de expor uma variável diretamente?**
   Resposta: Para calcular valores derivados (como uptime) ou validar escritas.

Se tudo isso estiver claro, você está pronto para aprender sobre **tratamento de erros e desfazimento limpo** na próxima seção.

---

## Caminhos de Erro e Desfazimento Limpo

Até agora, escrevemos `attach()` assumindo que tudo terá sucesso. Mas drivers reais precisam lidar com falhas de forma elegante: se uma alocação de memória falhar, se um recurso não estiver disponível, ou se o hardware se comportar de forma inesperada, seu driver deve **desfazer o que já iniciou** e retornar um erro sem deixar estado parcial para trás.

Esta seção ensina o **padrão de desfazimento com rótulo único**, o idioma padrão do FreeBSD para limpeza de erros. Domine isso e seu driver jamais vazará recursos, independentemente de onde a falha ocorra.

### Por Que o Tratamento de Erros Importa

Um tratamento de erros inadequado causa:

- **Vazamentos de recursos** (memória, locks, nós de dispositivo)
- **Panics do kernel** (acesso a memória liberada, double-frees)
- **Estado inconsistente** (dispositivo parcialmente attached, locks inicializados mas não destruídos)

**Impacto no mundo real:** Um driver com caminhos de erro descuidados pode funcionar perfeitamente em operação normal e, depois, causar um panic no sistema ao encontrar uma falha incomum (falta de memória, hardware ausente, etc.).

**Seu objetivo:** Garantir que `attach()` ou seja bem-sucedido por completo ou falhe por completo, sem meio-termo.

### O Padrão de Desfazimento com Rótulo Único

O código do kernel FreeBSD usa um **padrão de desfazimento baseado em goto** para limpeza. Ele funciona assim:

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

**Como funciona:**

- Cada etapa de inicialização tem um rótulo de limpeza correspondente.
- Se uma etapa falhar, pule para o rótulo que desfaz tudo que **já foi concluído até aquele ponto**.
- Os rótulos são dispostos em **ordem inversa** à da inicialização.
- Cada rótulo flui para o próximo, de forma que uma falha na etapa 4 desfaz as etapas 3, 2 e 1.

**Por que esse padrão?**

- **Limpeza centralizada:** Todos os caminhos de erro convergem para uma única sequência de desfazimento.
- **Fácil de manter:** Adicionar uma nova etapa significa adicionar apenas um goto e um rótulo de limpeza.
- **Sem duplicação:** Você não repete o código de limpeza em cada ramificação de erro.

### Aplicando o Padrão ao myfirst

Vamos refatorar nosso `attach()` para tratar erros adequadamente.

**Antes (sem tratamento de erros):**

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

**Depois (com desfazimento limpo):**

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

**Principais melhorias:**

- Toda operação que pode falhar é verificada.
- Falhas pulam para o rótulo de limpeza apropriado.
- A sequência de desfazimento desfaz exatamente o que foi bem-sucedido.
- Todos os caminhos retornam um código de erro (nunca retorne sucesso após uma falha).

### Convenção de Nomes para Rótulos

Escolha nomes de rótulos que indiquem o que precisa ser desfeito:

- `fail_mtx` - Destrua o mutex
- `fail_mem` - Libere o recurso de memória
- `fail_dev` - Destrua o nó de dispositivo
- `fail_irq` - Libere o recurso de interrupção

Ou use números se preferir:

- `fail1`, `fail2`, `fail3`

Qualquer abordagem funciona, mas nomes descritivos tornam o código mais fácil de ler.

### Testando os Caminhos de Erro

**Simule uma falha** para verificar se a lógica de desfazimento está correta.

Adicione uma falha deliberada após a inicialização do mutex:

```c
        mtx_init(&sc->mtx, ...);

        /* Simulate allocation failure for testing */
        if (1) {  /* Change to 0 to disable */
                device_printf(dev, "Simulated failure\n");
                error = ENXIO;
                goto fail_mtx;
        }
```

**Construa e carregue:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
```

**Verifique o dmesg:**

```bash
% dmesg | tail -n 2
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulated failure
```

**Verifique a limpeza:**

```bash
% devinfo | grep myfirst
(no output - device didn't attach)

% ls /dev/myfirst*
ls: cannot access '/dev/myfirst*': No such file or directory
```

**Observação importante:** O driver falhou de forma limpa. Nenhum nó de dispositivo, nenhum vazamento de sysctl, nenhum panic.

Agora desative a falha simulada e teste o attach normal novamente.

### Erros Comuns no Tratamento de Erros

**Erro 1: Não verificar valores de retorno**

**Errado:**

```c
make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
/* Forgot to check error! */
```

**Por que está errado:** Se `make_dev_s()` falhar, `sc->cdev` pode ser NULL ou lixo, e você prossegue como se tudo estivesse bem.

**Certo:** Sempre verifique `error` e ramifique conforme necessário.

---

**Erro 2: Limpeza parcial**

**Errado:**

```c
fail_dev:
        destroy_dev(sc->cdev);
        return (error);
        /* Forgot to destroy mutex! */
```

**Por que está errado:** O mutex permanece inicializado. O próximo carregamento causa panic ao tentar reinicializá-lo.

**Certo:** Cada rótulo deve desfazer **tudo** que foi inicializado antes dele.

---

**Erro 3: Limpeza dupla**

**Errado:**

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

**Por que está errado:** Double-free ou double-destroy causa panics.

**Certo:** Cada recurso deve ser liberado exatamente uma vez, em seu rótulo correspondente.

---

**Erro 4: Retornar sucesso após falha**

**Errado:**

```c
if (error != 0) {
        goto fail_mtx;
}
return (0);  /* Even if we jumped to fail_mtx! */
```

**Por que está errado:** O goto ignora o return, mas o padrão exige que todos os caminhos de erro **retornem um código de erro**.

**Certo:** Garanta que os rótulos de erro terminem com `return (error)`.

### O Quadro Completo: Attach e Detach

**Lógica de attach:**

1. Inicialize os recursos na ordem correta.
2. Verifique o resultado de cada etapa.
3. Em caso de falha, pule para o rótulo de desfazimento correspondente à última etapa bem-sucedida.
4. Os rótulos de desfazimento fluem em ordem inversa, limpando tudo.

**Lógica de detach:**

O detach é mais simples: desfaça tudo em ordem inversa ao `attach()`, assumindo sucesso completo:

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

**Simetria:** Cada etapa do `attach()` tem uma ação correspondente no `detach()`, em ordem inversa.

**Nota sobre ordenação:** Destruímos o dispositivo (`destroy_dev`) antes de liberar o contexto de sysctl e destruir o mutex. Isso segue a orientação do Capítulo 6: "Destrua o dispositivo antes dos locks." A chamada `destroy_dev()` bloqueia até que todas as operações de arquivo sejam concluídas, garantindo que nenhum caminho de código possa acessar nossos locks após o dispositivo ter sido removido.

### Lista de Verificação de Programação Defensiva

Antes de declarar seus caminhos de erro concluídos, verifique:

- [ ] Toda função que pode falhar é verificada
- [ ] Todo erro define `error` e pula para um rótulo de limpeza
- [ ] Todo rótulo de limpeza desfaz exatamente o que o precedeu
- [ ] Os rótulos estão em ordem inversa à da inicialização
- [ ] `detach()` desfaz tudo que `attach()` fez, em ordem inversa
- [ ] Nenhum recurso é liberado duas vezes
- [ ] Nenhum recurso vaza em caso de falha

### Verificação Rápida

Antes de avançar, confirme:

1. **O que é o padrão de desfazimento com rótulo único?**
   Resposta: Uma sequência de limpeza baseada em goto, onde cada rótulo desfaz progressivamente mais recursos, em ordem inversa à da inicialização.

2. **Por que os rótulos de limpeza estão em ordem inversa?**
   Resposta: Porque você deve desfazer a etapa mais recente primeiro e depois as anteriores, percorrendo a inicialização de trás para frente.

3. **O que todo caminho de erro deve fazer antes de retornar?**
   Resposta: Pular para o rótulo de limpeza apropriado e garantir que `return (error)` seja executado.

4. **Como você testa os caminhos de erro?**
   Resposta: Simule falhas (por exemplo, force uma falha de alocação) e verifique se a limpeza está correta (sem vazamentos, sem panics).

5. **Quando detach deve se recusar a prosseguir?**
   Resposta: Quando o dispositivo ainda está em uso (por exemplo, `is_open` é verdadeiro), retorne `EBUSY`.

Se tudo isso estiver claro, você está pronto para explorar **exemplos reais de drivers na árvore de código-fonte do FreeBSD**.

---

## Âncoras na Árvore de Código (Apenas Referência)

Você construiu um driver mínimo a partir dos primeiros princípios. Agora vamos **ancorar seu entendimento** apontando para drivers reais do FreeBSD 14.3 que demonstram os mesmos padrões que você acabou de aprender. Esta seção é um **tour guiado**, não um percurso exaustivo. Pense nela como uma lista de leitura para quando você quiser ver como o código de produção aplica as lições deste capítulo.

### Por Que Estudar Drivers Reais?

Drivers reais mostram:

- Como os padrões escalam para hardware complexo
- Como o código parece em contexto (arquivo completo, não apenas trechos)
- Variações nos padrões (lógica de attach diferente, tipos de recursos, estilos de tratamento de erros)
- Idiomas e convenções do FreeBSD na prática

**Não se espera que você entenda cada linha** desses drivers agora. O objetivo é **reconhecer o andaime** que você já construiu e ver como ele se expande para drivers mais capazes.

### Âncora 1: `/usr/src/sys/dev/null/null.c`

**O que é:** Os pseudo-dispositivos `/dev/null`, `/dev/zero` e `/dev/full`.

**Por que estudá-lo:**

- Dispositivos de caracteres mais simples possíveis
- Sem hardware, sem recursos, apenas cdevsw + handler MOD_LOAD
- Mostra como `read()` e `write()` são implementados (mesmo que de forma trivial)
- Boa referência para I/O com stubs

**O que procurar:**

- As estruturas `cdevsw` (use `grep -n cdevsw` para encontrá-las)
- Os handlers `null_write()` e `zero_read()`
- O loader do módulo (`null_modevent()`, use `grep -n modevent` para encontrá-lo)
- Como `make_dev_credf()` é utilizado (use `grep -n make_dev` para encontrá-lo)

**Localização do arquivo:**

```bash
% less /usr/src/sys/dev/null/null.c
```

**Leitura rápida:**

```bash
% grep -n "cdevsw\|make_dev" /usr/src/sys/dev/null/null.c
```

### Âncora 2: `/usr/src/sys/dev/led/led.c`

**O que é:** O framework de controle de LEDs, usado por drivers específicos de plataforma para expor LEDs como `/dev/led/*`.

**Por que estudá-lo:**

- Ainda simples, mas demonstra gerenciamento de recursos (callouts, listas)
- Mostra criação dinâmica de dispositivos por LED
- Usa locking (`struct mtx`)
- Mostra como drivers gerenciam múltiplas instâncias

**O que procurar:**

- A estrutura `ledsc` (próxima ao topo do arquivo; use `grep -n ledsc` para encontrá-la), análoga ao seu softc
- A função `led_create()` (use `grep -n "led_create\|led_destroy"` para encontrá-la), que cria nós de dispositivo dinamicamente
- A função `led_destroy()`, padrão de limpeza
- Como a lista global de LEDs é protegida com um mutex

**Localização do arquivo:**

```bash
% less /usr/src/sys/dev/led/led.c
```

**Verificação rápida:**

```bash
% grep -n "ledsc\|led_create\|led_destroy" /usr/src/sys/dev/led/led.c
```

### Âncora 3: `/usr/src/sys/net/if_tuntap.c`

**O que é:** As pseudo-interfaces de rede `tun` e `tap` (dispositivos de túnel).

**Por que estudar:**

- Driver híbrido: dispositivo de caracteres **e** interface de rede
- Mostra como registrar-se com `ifnet` (a pilha de rede)
- Ciclo de vida mais complexo (clone devices, estado por abertura)
- Bom exemplo de locking e concorrência no mundo real

**O que observar:**

- A `struct tuntap_softc` (use `grep -n "struct tuntap_softc"` para encontrá-la), muito mais rica do que a sua
- A função `tun_create()`, que registra o `ifnet`
- O `cdevsw` e como ele se coordena com o lado de rede
- Uso de `if_attach()` e `if_detach()` para integração com a rede

**Localização do arquivo:**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

**Atenção:** Este é um arquivo grande e complexo (~2000 linhas). Não tente entender tudo. Concentre-se em:

```bash
% grep -n "tuntap_softc\|if_attach\|make_dev" /usr/src/sys/net/if_tuntap.c | head -20
```

### Âncora 4: `/usr/src/sys/dev/uart/uart_bus_pci.c`

**O que é:** A cola de anexação PCI para dispositivos UART (portas seriais).

**Por que estudar:**

- Driver de hardware real (barramento PCI)
- Mostra como `probe()` verifica IDs de PCI
- Demonstra alocação de recursos (portas de I/O, IRQs)
- Tratamento de erros com desfazimento em `attach()`

**O que observar:**

- A função `uart_pci_probe()` (use `grep -n uart_pci_probe` para encontrá-la), correspondência de IDs de PCI
- A função `uart_pci_attach()`, alocação de recursos
- Uso de `bus_alloc_resource()` e `bus_release_resource()`
- A tabela `device_method_t` (use `grep -n device_method` para encontrá-la)

**Localização do arquivo:**

```bash
% less /usr/src/sys/dev/uart/uart_bus_pci.c
```

**Varredura rápida:**

```bash
% grep -n "uart_pci_probe\|uart_pci_attach\|device_method" /usr/src/sys/dev/uart/uart_bus_pci.c
```

**Dica:** Este arquivo é pequeno (~250 linhas) e muito limpo. É um excelente exemplo de driver Newbus real.

### Âncora 5: Padrões `DRIVER_MODULE` e `MODULE_VERSION`

Procure essas macros no final dos arquivos de driver:

```bash
% grep -rn 'DRIVER_MODULE\|MODULE_VERSION' /usr/src/sys/dev/null/ /usr/src/sys/dev/led/
```

Você verá os mesmos padrões de registro que utilizou em `myfirst`. Para drivers que se anexam ao `nexus` e fornecem seu próprio método `identify`, o padrão em `/usr/src/sys/crypto/aesni/aesni.c` e `/usr/src/sys/dev/sound/dummy.c` é o mais próximo do que você escreveu.

### Como usar estas âncoras

**1. Comece com null.c**

Leia o arquivo inteiro, ele é curto (~220 linhas). Você deve reconhecer quase tudo.

**2. Percorra led.c rapidamente**

Foque na estrutura e no ciclo de vida (criação/destruição). Não se perca na máquina de estados.

**3. Dê uma olhada prévia em if_tuntap.c**

Abra o arquivo, role pelo conteúdo, observe a estrutura híbrida (cdevsw + ifnet). Não tente entender tudo; apenas perceba o formato geral.

**4. Estude uart_bus_pci.c**

Leia `probe()` e `attach()`. Este é seu ponto de passagem para drivers de hardware real (abordados na Parte 4).

**5. Compare com o seu driver**

Para cada âncora, pergunte-se:

- O que é semelhante ao meu driver `myfirst`?
- O que é diferente?
- Quais novos conceitos estou vendo (callouts, ifnet, recursos de PCI)?

**6. Anote o que aprender a seguir**

Quando você encontrar algo desconhecido (por exemplo, `callout_reset`, `if_attach`, `bus_alloc_resource`), anote. Esses são tópicos para capítulos posteriores.

### Tour rápido: padrões comuns entre drivers

| Padrão                    | null.c       | led.c | if_tuntap.c | uart_pci.c | myfirst.c     |
|---------------------------|--------------|-------|-------------|------------|---------------|
| Usa `cdevsw`              | sim          | sim   | sim         | não        | sim           |
| Usa `ifnet`               | não          | não   | sim         | não        | não           |
| probe/attach Newbus       | não          | não   | não (clone) | sim        | sim           |
| Tem método `identify`     | não          | não   | não         | não        | sim           |
| Manipulador de carga de módulo | sim     | sim   | sim         | não (Newbus)| não (Newbus) |
| Aloca softc               | não          | não   | sim         | sim        | sim (Newbus)  |
| Usa locking               | não          | sim   | sim         | sim        | sim           |
| Aloca recursos de barramento | não       | não   | não         | sim        | não           |
| Cria nós em `/dev`        | sim          | sim   | sim         | não        | sim           |

### O que pular (por enquanto)

Ao ler esses drivers, não trave em:

- Acesso a registradores de hardware (`bus_read_4`, `bus_write_2`)
- Configuração de interrupções (`bus_setup_intr`, registro de handlers)
- DMA (`bus_dma_tag_create`, `bus_dmamap_load`)
- Locking avançado (locks de leitura majoritária, ordem de locks)
- Tratamento de pacotes de rede (cadeias de `mbuf`, `if_transmit`)

Você aprenderá tudo isso nos capítulos dedicados a cada tema. Por enquanto, concentre-se em **estrutura e ciclo de vida**.

### Exercício de estudo individual

Escolha uma âncora (`null.c` é recomendado para iniciantes) e:

1. Leia o arquivo inteiro
2. Identifique a estrutura `cdevsw`
3. Encontre os handlers `open`, `close`, `read`, `write`
4. Acompanhe o fluxo de carga/descarga do módulo
5. Compare com o seu driver `myfirst`

Escreva um parágrafo no seu caderno de laboratório: "Aqui está o que aprendi com o driver [nome do driver]."

### Verificação rápida

Antes de passar para os laboratórios, confirme:

1. **Por que olhar para drivers reais?**
   Resposta: Para ver padrões em contexto, aprender idiomas e fazer a ponte entre exemplos mínimos e código de produção.

2. **Qual driver é o mais simples?**
   Resposta: `null.c`, pois é puramente pseudo, sem estado ou recursos.

3. **Qual driver mostra estrutura híbrida?**
   Resposta: `if_tuntap.c`, pois é ao mesmo tempo um dispositivo de caracteres e uma interface de rede.

4. **O que você deve pular ao ler drivers complexos?**
   Resposta: Detalhes específicos de hardware (registradores, DMA, interrupções) até que os capítulos posteriores os abordem.

5. **Como você deve usar essas âncoras?**
   Resposta: Como exemplos de referência, não como material de estudo aprofundado. Percorra, compare, anote novos conceitos e siga em frente.

Se tudo isso estiver claro, você está pronto para os **laboratórios práticos**, onde irá construir, testar e estender seu driver.

---

## Laboratórios Práticos

Você leu sobre a estrutura do driver, viu os padrões e percorreu o código. Agora é hora de **construir, testar e validar** seu entendimento por meio de quatro laboratórios práticos. Cada laboratório é um ponto de verificação que garante que você dominou os conceitos antes de avançar.

### Visão geral dos laboratórios

| Lab | Foco | Duração | Aprendizado principal |
|-----|------|---------|-----------------------|
| Lab 7.1 | Caça ao código-fonte | 20-30 min | Navegar pela árvore de código-fonte do FreeBSD, identificar padrões |
| Lab 7.2 | Build e carga | 30-40 min | Compilar, carregar, verificar ciclo de vida |
| Lab 7.3 | Nó de dispositivo | 30-40 min | Criar `/dev`, testar open/close |
| Lab 7.4 | Tratamento de erros | 30-45 min | Simular falhas, verificar desfazimento |

**Tempo total:** 2 a 2,5 horas se você completar todos os laboratórios em uma única sessão.

**Pré-requisitos:**

- Ambiente de laboratório FreeBSD 14.3 do Capítulo 2
- `/usr/src` instalado
- Habilidades básicas com shell e editor
- Projeto `~/drivers/myfirst` das seções anteriores

Vamos começar.

---

### Lab 7.1: Caça ao tesouro no código-fonte

**Objetivo:** Familiarizar-se com a árvore de código-fonte do FreeBSD localizando e identificando padrões de drivers.

**Habilidades praticadas:**

- Navegar por `/usr/src/sys`
- Usar `grep` e `find`
- Ler código real de drivers

**Instruções:**

**1. Localize o driver null:**

```bash
% find /usr/src/sys -name "null.c" -type f
```

Saída esperada:

```text
/usr/src/sys/dev/null/null.c
```

**2. Abra e percorra:**

```bash
% less /usr/src/sys/dev/null/null.c
```

**3. Encontre as estruturas cdevsw:**

Pesquise por `cdevsw` dentro do `less` digitando `/cdevsw` e pressionando Enter.

Você deve chegar às linhas que definem `null_cdevsw`, `zero_cdevsw` e `full_cdevsw`.

**4. Encontre o manipulador de eventos do módulo:**

Pesquise por `modevent`:

```text
/modevent
```

Você deve ver `null_modevent()`.

**5. Identifique a criação do dispositivo:**

Pesquise por `make_dev`:

```text
/make_dev
```

Você deve encontrar três chamadas criando `/dev/null`, `/dev/zero` e `/dev/full`.

**6. Compare com o seu driver:**

Abra o seu `myfirst.c` e compare:

- Como `null.c` cria nós de dispositivo? (Resposta: `make_dev_credf` no carregador de módulo)
- Como o seu driver os cria? (Resposta: `make_dev_s` em `attach()`)

**7. Encontre o driver LED:**

```bash
% find /usr/src/sys -name "led.c" -path "*/dev/led/*"
```

**8. Procure softc:**

```bash
% grep -n "struct ledsc" /usr/src/sys/dev/led/led.c | head -5
```

Você deve ver a definição de `struct ledsc` próxima ao início do arquivo, logo após o bloco de `#include`.

**9. Repita para if_tuntap:**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

Pesquise por `tuntap_softc`. Observe como ele é muito mais rico do que o seu softc mínimo.

**10. Registre suas descobertas:**

No seu caderno de laboratório, escreva:

```text
Lab 7.1 Completed:
- Located null.c, led.c, if_tuntap.c
- Identified cdevsw, module loader, and softc structures
- Compared patterns to myfirst driver
- Key insight: [your observation]
```

**Critérios de sucesso:**

- [ ] Encontrou os três arquivos de driver
- [ ] Localizou cdevsw e softc em cada um
- [ ] Identificou as chamadas de criação de dispositivo
- [ ] Comparou com o seu driver

**Se travar:** Use `grep -r "DRIVER_MODULE" /usr/src/sys/dev/null/` para encontrar as macros principais.

---

### Lab 7.2: Build, carga e verificação do ciclo de vida

**Objetivo:** Compilar seu driver, carregá-lo no kernel, verificar os eventos do ciclo de vida e descarregá-lo de forma limpa.

**Habilidades praticadas:**

- Construir módulos do kernel
- Carregar/descarregar com `kldload`/`kldunload`
- Inspecionar `dmesg` e `devinfo`

**Instruções:**

**1. Navegue até o seu driver:**

```bash
% cd ~/drivers/myfirst
```

**2. Limpe e construa:**

```bash
% make clean
% make
```

Verifique se `myfirst.ko` foi criado:

```bash
% ls -lh myfirst.ko
-rwxr-xr-x  1 youruser yourgroup  8.5K Nov  6 16:00 myfirst.ko
```

**3. Carregue o módulo:**

```bash
% sudo kldload ./myfirst.ko
```

**4. Verifique as mensagens do kernel:**

```bash
% dmesg | tail -n 5
```

Saída esperada:

```text
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**5. Verifique a árvore de dispositivos:**

```bash
% devinfo -v | grep myfirst
  myfirst0
```

**6. Verifique o nó de dispositivo:**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 16:00 /dev/myfirst0
```

**7. Consulte os sysctls:**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**8. Descarregue o módulo:**

```bash
% sudo kldunload myfirst
```

**9. Verifique a limpeza:**

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

**10. Recarregue e verifique idempotência:**

```bash
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
```

Todos os ciclos devem ser concluídos sem erros.

**11. Registre os resultados:**

Caderno de laboratório:

```text
Lab 7.2 Completed:
- Built myfirst.ko successfully
- Loaded without errors
- Verified attach messages in dmesg
- Verified /dev node and sysctls
- Unloaded cleanly
- Repeated load/unload cycle 3 times: all succeeded
```

**Critérios de sucesso:**

- [ ] O módulo compila sem erros
- [ ] Carrega sem kernel panic
- [ ] A mensagem de attach aparece no dmesg
- [ ] `/dev/myfirst0` existe enquanto o módulo está carregado
- [ ] Os sysctls são legíveis
- [ ] A descarga remove tudo
- [ ] O recarregamento funciona de forma confiável

**Solução de problemas:**

- Se o build falhar, verifique a sintaxe do Makefile (tabulações, não espaços).
- Se a carga falhar com "Exec format error", verifique a compatibilidade entre as versões do kernel e do código-fonte.
- Se a descarga indicar "module busy", verifique se nenhum processo está mantendo o dispositivo aberto.

---

### Lab 7.3: Testar abertura e fechamento do nó de dispositivo

**Objetivo:** Interagir com `/dev/myfirst0` a partir do espaço do usuário e verificar se os handlers `open()` e `close()` são chamados.

**Habilidades praticadas:**

- Acesso a dispositivos pelo espaço do usuário
- Monitoramento de logs do driver
- Acompanhamento de mudanças de estado

**Instruções:**

**1. Carregue o driver:**

```bash
% sudo kldload ./myfirst.ko
```

**2. Abra o dispositivo com `cat` (leitura):**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
```

(Sem saída, EOF imediato)

**3. Verifique os logs:**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

**4. Escreva no dispositivo:**

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
```

**5. Verifique os logs novamente:**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

**6. Verifique o contador via sysctl:**

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 2
```

**7. Teste o acesso exclusivo:**

Abra dois terminais.

Terminal 1:

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 10'
```

(Isto mantém o dispositivo aberto por 10 segundos)

Terminal 2 (rapidamente, enquanto o terminal 1 ainda está dormindo):

```bash
% sudo sh -c 'cat < /dev/myfirst0'
cat: /dev/myfirst0: Device busy
```

Perfeito! O acesso exclusivo está sendo aplicado.

**8. Tente descarregar enquanto o dispositivo está aberto:**

Terminal 1 (mantenha o dispositivo aberto):

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

Terminal 2:

```bash
% sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

Verifique o dmesg:

```bash
% dmesg | tail -n 2
myfirst0: Cannot detach: device is open
```

Perfeito! Seu `detach()` recusa corretamente a descarga enquanto o dispositivo está em uso.

**9. Feche e tente descarregar novamente:**

Terminal 1: Aguarde o `sleep 30` terminar (ou pressione Ctrl+C para interromper).

Terminal 2:

```bash
% sudo kldunload myfirst
(succeeds)
```

**10. Registre os resultados:**

Caderno de laboratório:

```text
Lab 7.3 Completed:
- Opened device with cat, verified open/close logs
- Opened device with echo, verified counter increment
- Exclusive access enforced (second open returned EBUSY)
- Detach refused while device open
- Detach succeeded after close
```

**Critérios de sucesso:**

- [ ] A abertura aciona o handler `open()` (registrado em log)
- [ ] O fechamento aciona o handler `close()` (registrado em log)
- [ ] O contador via sysctl incrementa a cada abertura
- [ ] A segunda abertura retorna `EBUSY`
- [ ] O detach retorna `EBUSY` enquanto o dispositivo está aberto
- [ ] O detach é bem-sucedido após o fechamento

**Solução de problemas:**

- Se você não vir os logs "Device opened", verifique se `device_printf()` está presente no seu handler `open()`.
- Se o acesso exclusivo não estiver sendo aplicado, verifique a condição `if (sc->is_open) return (EBUSY)` em `open()`.

---

### Lab 7.4: Simular falha no attach e verificar o desfazimento

**Objetivo:** Injetar uma falha deliberada em `attach()` e verificar se a limpeza está correta (sem vazamentos, sem panics).

**Habilidades praticadas:**

- Testar caminhos de erro
- Depurar falhas de attach
- Verificar a limpeza de recursos

**Instruções:**

**1. Adicionar uma falha simulada:**

Edite `myfirst.c` e adicione o trecho abaixo após a inicialização do mutex em `attach()`:

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

**3. Tentar carregar:**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

**4. Verificar o dmesg:**

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulating attach failure for testing
```

**5. Verificar ausência de vazamentos:**

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

Perfeito! O dispositivo falhou ao realizar o attach e nenhum recurso foi deixado para trás.

**6. Tentar carregar novamente:**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

Ainda falha de forma limpa (sem panics de dupla inicialização).

**7. Remover a falha simulada:**

Edite `myfirst.c` e delete ou comente o bloco de falha simulada.

**8. Recompilar e carregar normalmente:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -n 5
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

Sucesso!

**9. Testar outro ponto de falha:**

Injete uma falha após a criação do nó de dispositivo:

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

**10. Recompilar e testar:**

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

**11. Verificar se o nó em `/dev` foi destruído:**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

Perfeito! Mesmo tendo sido criado, o nó foi destruído pelo caminho de erro.

**12. Remover a simulação e restaurar a operação normal:**

Delete a segunda falha simulada, recompile e carregue normalmente.

**13. Registrar os resultados:**

Diário de laboratório:

```text
Lab 7.4 Completed:
- Simulated failure after mutex init: cleanup correct
- Simulated failure after dev node creation: cleanup correct
- Verified no leaks in either case
- Verified no panics on repeated load attempts
- Restored normal operation
```

**Critérios de sucesso:**

- [ ] Falha simulada após o mutex: sem vazamentos
- [ ] Falha simulada após o nó de dispositivo: nó destruído
- [ ] Múltiplas tentativas de carregamento não causam panic
- [ ] Operação normal restaurada após remover a simulação

**Solução de problemas:**

- Se você observar um panic, há um bug no seu caminho de erro. Verifique se cada `goto` salta para o label correto.
- Se houver vazamento de recursos, certifique-se de que cada label de limpeza está acessível e correto.

---

### Labs Concluídos!

Você agora:

- Navegou pela árvore de código-fonte do FreeBSD (Lab 7.1)
- Construiu, carregou e verificou seu driver (Lab 7.2)
- Testou open/close e acesso exclusivo (Lab 7.3)
- Verificou o desfazimento de erros com falhas simuladas (Lab 7.4)

**Dê-se os parabéns.** Você passou de ler sobre drivers para **construir e testar um por conta própria**. Este é um marco importante.

---

## Exercícios Curtos

Estes exercícios reforçam os conceitos deste capítulo. São **opcionais, mas recomendados** se você quiser aprofundar o entendimento antes de avançar.

### Exercício 7.1: Adicionar um Flag via Sysctl

**Tarefa:** Adicionar um novo sysctl somente leitura que mostre se o dispositivo está atualmente aberto.

**Passos:**

1. Em `attach()`, adicione:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "is_open", CTLFLAG_RD,
    &sc->is_open, 0, "1 if device is currently open");
```

2. Reconstrua, carregue e teste:

```bash
% sysctl dev.myfirst.0.stats.is_open
dev.myfirst.0.stats.is_open: 0

% sudo sh -c 'exec 3<>/dev/myfirst0; sysctl dev.myfirst.0.stats.is_open; exec 3<&-'
dev.myfirst.0.stats.is_open: 1
```

**Resultado esperado:** O flag mostra `1` enquanto aberto, `0` após o fechamento.

---

### Exercício 7.2: Registrar a Primeira e a Última Abertura

**Tarefa:** Modificar `open()` para registrar apenas a **primeira** abertura e `close()` para registrar apenas o **último** fechamento.

**Dicas:**

- Verifique `sc->open_count` antes e depois de incrementar.
- Em `close()`, decremente um contador e verifique se ele chega a zero.

**Comportamento esperado:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
myfirst0: Device opened for the first time
myfirst0: Device closed (no more openers)

% sudo sh -c 'cat < /dev/myfirst0'
(no log: not the first open)
myfirst0: Device closed (no more openers)
```

---

### Exercício 7.3: Adicionar um Sysctl de "Resetar Estatísticas"

**Tarefa:** Adicionar um sysctl somente escrita que redefina `open_count` e `bytes_read` para zero.

**Passos:**

1. Defina um handler:

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

2. Registre-o:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "reset_stats", CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, sysctl_reset_stats, "I",
    "Write 1 to reset statistics");
```

3. Teste:

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

### Exercício 7.4: Testar Carga/Descarga 100 Vezes

**Tarefa:** Escrever um script que carrega e descarrega seu driver 100 vezes, verificando se ocorre alguma falha ou vazamento.

**Script (~/drivers/myfirst/stress_test.sh):**

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

**Execute:**

```bash
% chmod +x stress_test.sh
% ./stress_test.sh
```

**Resultado esperado:** Todas as iterações são concluídas com sucesso, sem erros.

**Se falhar:** Verifique `dmesg` em busca de mensagens de panic ou recursos vazados.

---

### Exercício 7.5: Comparar Seu Driver com null.c

**Tarefa:** Abrir `/usr/src/sys/dev/null/null.c` lado a lado com seu `myfirst.c`. Liste 5 semelhanças e 5 diferenças.

**Exemplos de observações:**

**Semelhanças:**

1. Ambos usam `cdevsw` para operações de dispositivo de caracteres.
2. Ambos criam nós em `/dev`.
3. Ambos têm handlers de `open` e `close`.
4. Ambos retornam EOF na leitura.
5. Ambos registram eventos de attach/detach.

**Diferenças:**

1. `null.c` usa um handler `MOD_LOAD`; `myfirst` usa Newbus.
2. `null.c` não tem softc; `myfirst` tem.
3. `null.c` cria múltiplos dispositivos (`null`, `zero`, `full`); `myfirst` cria apenas um.
4. `null.c` não usa sysctls; `myfirst` usa.
5. `null.c` não mantém estado; `myfirst` rastreia contadores.

---

## Desafios Opcionais

Estes são **exercícios avançados** para leitores que querem ir além do básico. Não os tente antes de concluir todos os labs e exercícios.

### Desafio 7.1: Implementar um Buffer de Leitura Simples

**Objetivo:** Em vez de retornar EOF imediatamente, retornar uma string fixa em `read()`.

**Passos:**

1. Adicione um buffer ao softc:

```c
        char    read_buffer[64];  /* Data to return on read */
        size_t  read_len;         /* Length of valid data */
```

2. Em `attach()`, preencha o buffer:

```c
        snprintf(sc->read_buffer, sizeof(sc->read_buffer),
            "Hello from myfirst driver!\n");
        sc->read_len = strlen(sc->read_buffer);
```

3. Em `myfirst_read()`, copie os dados para o espaço do usuário:

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

4. Teste:

```bash
% sudo cat /dev/myfirst0
Hello from myfirst driver!
```

**Comportamento esperado:** A leitura retorna a string uma vez, depois EOF nas leituras subsequentes.

---

### Desafio 7.2: Permitir Múltiplos Abridores

**Objetivo:** Remover a verificação de acesso exclusivo para que vários programas possam abrir o dispositivo simultaneamente.

**Passos:**

1. Remova a verificação `if (sc->is_open) return (EBUSY)` em `open()`.
2. Use um **contador de referências** em vez de um flag booleano:

```c
        int     open_refcount;  /* Number of current openers */
```

3. Em `open()`:

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount++;
        mtx_unlock(&sc->mtx);
```

4. Em `close()`:

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount--;
        mtx_unlock(&sc->mtx);
```

5. Em `detach()`, recuse se `open_refcount > 0`:

```c
        if (sc->open_refcount > 0) {
                device_printf(dev, "Cannot detach: device has %d openers\n",
                    sc->open_refcount);
                return (EBUSY);
        }
```

6. Teste com dois terminais:

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

### Desafio 7.3: Adicionar um Contador de Escrita

**Objetivo:** Rastrear quantos bytes foram escritos no dispositivo.

**Passos:**

1. Adicione ao softc:

```c
        uint64_t        bytes_written;
```


**Nota:** Este é um handler de escrita descartável exclusivo do Capítulo 7. Deliberadamente não usamos `uiomove()` nem armazenamos dados do usuário aqui. A movimentação completa de dados, o buffering e o uso de `uiomove()` são abordados no Capítulo 9.

2. Em `myfirst_write()`:

```c
        size_t len = uio->uio_resid;

        mtx_lock(&sc->mtx);
        sc->bytes_written += len;
        mtx_unlock(&sc->mtx);

        uio->uio_resid = 0;
        return (0);
```

3. Exponha via sysctl:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, ..., "bytes_written", ...);
```

4. Teste:

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
% sysctl dev.myfirst.0.stats.bytes_written
dev.myfirst.0.stats.bytes_written: 5
```

---

### Desafio 7.4: Criar um Segundo Dispositivo (myfirst1)

**Objetivo:** Criar manualmente uma segunda instância de dispositivo para testar o suporte a múltiplos dispositivos.

**Dica:** No momento, seu driver cria `myfirst0` automaticamente. Para testar múltiplos dispositivos, você precisaria acionar um segundo ciclo de probe/attach. Isso é complexo (requer manipulação no nível de barramento ou clonagem), então considere isto **apenas para pesquisa**.

**Abordagem alternativa:** Estude `/usr/src/sys/net/if_tuntap.c` para ver como ele lida com dispositivos clone (criando novas instâncias sob demanda).

---

### Desafio 7.5: Implementar Log com Taxa Limitada

**Objetivo:** Adicionar um log com taxa limitada para eventos de abertura (registrar no máximo uma vez por segundo).

**Passos:**

1. Adicione ao softc:

```c
        time_t  last_open_log;
```

2. Em `open()`:

```c
        time_t now = time_second;

        if (now - sc->last_open_log >= 1) {
                device_printf(sc->dev, "Device opened (count: %lu)\n",
                    (unsigned long)sc->open_count);
                sc->last_open_log = now;
        }
```

3. Teste abrindo rapidamente:

```bash
% for i in $(seq 1 10); do sudo sh -c 'cat < /dev/myfirst0'; done
```

**Comportamento esperado:** Apenas algumas mensagens de log aparecem (com taxa limitada).

---

## Árvore de Decisão para Problemas e Depuração

Mesmo com código cuidadoso, você vai encontrar problemas. Esta seção fornece uma **árvore de decisão** para diagnosticar rapidamente as questões mais comuns.

### Sintoma: O Driver Não Carrega

**Verifique:**

- [ ] O resultado de `freebsd-version -k` corresponde à versão em `/usr/src`?
  - **Não:** Reconstrua o kernel ou re-clone `/usr/src` para a versão correta.
  - **Sim:** Continue.

- [ ] O `make` conclui sem erros?
  - **Não:** Leia as mensagens de erro do compilador. Causas comuns:
    - Ponto e vírgula ausente
    - Chaves sem par correspondente
    - Funções não definidas (includes faltando)
  - **Sim:** Continue.

- [ ] O `kldload` falha com "Exec format error"?
  - **Sim:** Incompatibilidade kernel/módulo. Reconstrua com as fontes corretas.
  - **Não:** Continue.

- [ ] O `kldload` falha com "No such file or directory"?
  - **Sim:** Verifique o caminho do módulo (`./myfirst.ko` vs `/boot/modules/myfirst.ko`).
  - **Não:** Continue.

- [ ] Verifique o `dmesg` em busca de erros no attach:

```bash
% dmesg | tail -n 10
```

Procure mensagens de erro do seu driver.

---

### Sintoma: O Nó de Dispositivo Não Aparece

**Verifique:**

- [ ] O `attach()` foi concluído com sucesso?

```bash
% dmesg | grep myfirst
```

Procure pela mensagem "Attached successfully".

- [ ] Se o attach falhou, o tratamento de erros foi executado?

Procure por mensagens de erro no dmesg.

- [ ] O `make_dev_s()` foi concluído com sucesso?

Adicione logging:

```c
device_printf(dev, "make_dev_s returned: %d\n", error);
```

- [ ] O nome do nó de dispositivo está correto?

```bash
% ls -l /dev/myfirst*
```

Verifique a ortografia e o número de unidade.

---

### Sintoma: Kernel Panic ao Carregar

**Verifique:**

- [ ] Você se esqueceu de `mtx_init()`?

Panic durante `mtx_lock()` indica que o mutex não foi inicializado.

- [ ] Você desreferenciou um ponteiro NULL?

Panic com "NULL pointer dereference" indica que o valor retornado por `device_get_softc()` não foi verificado.

- [ ] Você corrompeu a memória?

Habilite WITNESS e INVARIANTS na configuração do seu kernel e reconstrua:

```text
options WITNESS
options INVARIANTS
```

Reinicie e recarregue seu driver. O WITNESS vai detectar violações de lock.

---

### Sintoma: Kernel Panic ao Descarregar

**Verifique:**

- [ ] Você se esqueceu de `destroy_dev()`?

O descarregamento causa panic quando o usuário tenta acessar o nó de dispositivo.

- [ ] Você se esqueceu de `mtx_destroy()`?

Kernels com WITNESS habilitado causarão panic no descarregamento se os locks não forem destruídos.

- [ ] Você se esqueceu de `sysctl_ctx_free()`?

Vazamentos de OID de sysctl podem causar panics no recarregamento.

- [ ] Ainda há código em execução quando você descarrega?

Verifique:
  - Nós de dispositivo abertos (`sc->is_open` deve ser falso)
  - Timers ou callbacks ativos (não usados neste capítulo, mas comuns nos seguintes)

---

### Sintoma: "Device Busy" ao Descarregar

**Verifique:**

- [ ] O dispositivo ainda está aberto?

```bash
% fstat | grep myfirst
```

Se algum processo tiver o dispositivo aberto, o descarregamento vai falhar.

- [ ] Você retornou `EBUSY` em `detach()`?

Verifique a lógica de `detach()`:

```c
if (sc->is_open) {
        return (EBUSY);
}
```

---

### Sintoma: Sysctls Não Aparecem

**Verifique:**

- [ ] O `sysctl_ctx_init()` foi executado?

- [ ] O `SYSCTL_ADD_NODE()` foi concluído com sucesso?

Adicione logging:

```c
if (sc->sysctl_tree == NULL) {
        device_printf(dev, "sysctl tree creation failed\n");
}
```

- [ ] O caminho do sysctl está correto?

```bash
% sysctl dev.myfirst
```

Verifique se há erros de digitação no nome do driver ou no número de unidade.

---

### Sintoma: Open/Close Não Geram Log

**Verifique:**

- [ ] Você adicionou `device_printf()` aos handlers?

- [ ] O cdevsw foi registrado corretamente?

Verifique se `args.mda_devsw = &myfirst_cdevsw` está configurado antes de `make_dev_s()`.

- [ ] O `si_drv1` foi configurado corretamente?

```c
args.mda_si_drv1 = sc;
```

Se for NULL, `open()` vai falhar.

---

### Sintoma: O Módulo Carrega Mas Não Faz Nada

**Verifique:**

- [ ] O `attach()` foi executado?

```bash
% dmesg | grep myfirst
```

Se você não vê nenhuma saída e seu driver se conecta ao `nexus`, a causa mais provável é a ausência do método `identify`. Sem ele, o nexus não tem nenhum dispositivo `myfirst` para fazer o probe e seu código nunca é chamado. Releia a seção **Passo 6: Implementar o Identify** acima e confirme que `DEVMETHOD(device_identify, myfirst_identify)` está presente na sua tabela de métodos.

- [ ] O dispositivo está no barramento correto?

Para pseudo-dispositivos, use `nexus` (e lembre-se de fornecer `identify`). Para PCI, use `pci`. Para USB, use `usbus`.

- [ ] O `probe()` retornou uma prioridade sem erro?

Se `probe()` retornar `ENXIO`, o driver não vai executar o attach. Para nosso pseudo-dispositivo, `BUS_PROBE_DEFAULT` é o valor correto.

---

### Dicas Gerais de Depuração

**Habilite o boot detalhado:**

```bash
% sudo sysctl boot.verbose=1
```

Recarregue seu driver para ver mensagens mais detalhadas.

**Use depuração com printf:**

Adicione declarações `device_printf()` em pontos-chave para rastrear o fluxo de execução.

**Verifique o estado dos locks:**

Se estiver usando WITNESS, verifique a ordem dos locks:

```bash
% sysctl debug.witness.fullgraph
```

**Salve o dmesg após um panic:**

```bash
% sudo dmesg -a > panic.log
```

Analise o log em busca de pistas.

---

## Rubrica de Autoavaliação

Use esta rubrica para avaliar seu entendimento antes de avançar para o Capítulo 8.

### Conhecimento Central

**Avalie-se (1-5, onde 5 = totalmente confiante):**

- [ ] Consigo explicar o que é o softc e por que ele existe. (Nota: __/5)
- [ ] Entendo o ciclo de vida probe/attach/detach. (Nota: __/5)
- [ ] Consigo criar um nó de dispositivo usando `make_dev_s()`. (Nota: __/5)
- [ ] Consigo implementar handlers básicos de open/close. (Nota: __/5)
- [ ] Consigo adicionar um sysctl somente leitura. (Nota: __/5)
- [ ] Entendo o padrão de desfazimento com rótulo único. (Nota: __/5)
- [ ] Consigo testar caminhos de erro com falhas simuladas. (Nota: __/5)

**Nota Total: __/35**

**Interpretação:**

- **30-35:** Excelente. Você está pronto para o Capítulo 8.
- **25-29:** Bom. Revise as áreas mais fracas antes de prosseguir.
- **20-24:** Adequado. Reveja os labs e os exercícios.
- **<20:** Dedique mais tempo a este capítulo.

---

### Habilidades Práticas

**Você consegue fazer isso sem consultar anotações?**

- [ ] Construir um módulo do kernel com `make`.
- [ ] Carregar um módulo com `kldload`.
- [ ] Verificar mensagens de attach no `dmesg`.
- [ ] Consultar sysctls com `sysctl`.
- [ ] Abrir um nó de dispositivo com `cat` ou redirecionamento de shell.
- [ ] Descarregar um módulo com `kldunload`.
- [ ] Simular uma falha no attach.
- [ ] Verificar a limpeza após uma falha.

**Pontuação:** 1 ponto por habilidade. **Meta:** 7/8 ou mais.

---

### Leitura de Código

**Você consegue reconhecer estes padrões em código real do FreeBSD?**

- [ ] Identificar uma estrutura `cdevsw`.
- [ ] Localizar os métodos `probe()`, `attach()`, `detach()`.
- [ ] Encontrar a definição da estrutura softc.
- [ ] Identificar a macro `DRIVER_MODULE()`.
- [ ] Reconhecer o desfazimento de erros com rótulos goto.
- [ ] Encontrar chamadas a `make_dev()` ou `make_dev_s()`.
- [ ] Identificar a criação de sysctls (`SYSCTL_ADD_*`).

**Pontuação:** 1 ponto por padrão. **Meta:** 6/7 ou mais.

---

### Entendimento Conceitual

**Verdadeiro ou Falso:**

1. O softc é alocado pelo driver em `attach()`. (**Falso**. O Newbus o aloca a partir do tamanho declarado em `driver_t`.)
2. `probe()` deve alocar recursos. (**Falso**. `probe()` apenas inspeciona o dispositivo e decide se vai reivindicá-lo; `attach()` faz a alocação.)
3. `detach()` deve desfazer tudo o que `attach()` fez. (**Verdadeiro**.)
4. Os rótulos de erro devem estar em ordem inversa à da inicialização. (**Verdadeiro**.)
5. Você pode omitir `mtx_destroy()` se o módulo estiver sendo descarregado. (**Falso**. Todo `mtx_init()` precisa de um `mtx_destroy()` correspondente.)
6. Os sysctls são limpos automaticamente quando o módulo é descarregado. (**Falso**. Somente se você chamar `sysctl_ctx_free()` no contexto por dispositivo que você inicializou.)
7. `make_dev_s()` é mais segura do que `make_dev()`. (**Verdadeiro**. Ela retorna um erro explícito e evita a condição de corrida em que `make_dev()` poderia falhar sem uma forma clara de reportar o problema.)
8. Um pseudo-dispositivo que faz attach no nexus deve fornecer um método identify. (**Verdadeiro**. Sem ele, o barramento não tem nenhum dispositivo para fazer probe.)

**Pontuação:** 1 ponto por resposta correta. **Meta:** 7/8 ou mais.

---

### Avaliação Geral

Some seus pontos:

- Conhecimento Fundamental: __/35
- Habilidades Práticas: __/8
- Leitura de Código: __/7
- Compreensão Conceitual: __/8

**Total: __/58**

**Nota:**

- **51-58:** A (Domínio excelente)
- **44-50:** B (Boa compreensão)
- **35-43:** C (Adequado, mas revise as áreas mais fracas)
- **<35:** Revise o capítulo antes de avançar

---

## Encerrando e Próximos Passos

Parabéns! Você concluiu o Capítulo 7 e construiu seu primeiro driver FreeBSD do zero. Vamos refletir sobre o que você conquistou e dar uma prévia do que vem a seguir.

### O Que Você Construiu

O seu driver `myfirst` é mínimo, mas completo:

- **Disciplina de ciclo de vida:** probe/attach/detach limpos, sem vazamentos de recursos.
- **Interface para o usuário:** nó `/dev/myfirst0` que abre e fecha de forma confiável.
- **Observabilidade:** sysctls somente leitura exibindo o tempo de attach, a contagem de aberturas e os bytes lidos.
- **Tratamento de erros:** padrão de desenrolamento com rótulo único que se recupera com elegância de falhas.
- **Logging:** uso correto de `device_printf()` para eventos do ciclo de vida e erros.

Isso não é um brinquedo. É uma **estrutura de qualidade de produção**, a mesma base com que todo driver FreeBSD começa.

### O Que Você Aprendeu

Você agora compreende:

- Como o Newbus descobre drivers e realiza o attach
- O papel do softc (estado por dispositivo)
- Como criar e destruir nós de dispositivo
- Como expor métricas via sysctls
- Como tratar erros sem vazar recursos
- Como testar os caminhos do ciclo de vida (carga/descarga, abertura/fechamento, falhas simuladas)

Essas habilidades são **transferíveis**. Seja para escrever um driver PCI, um driver USB ou uma interface de rede, você usará esses mesmos padrões.

### O Que Ainda Falta (E Por Quê)

Seu driver ainda não faz muita coisa:

- **Semântica de leitura/escrita:** apenas esboçada, sem implementação real. (**Capítulos 8 e 9**)
- **Buffering:** sem filas, sem ring buffers. (**Capítulo 10**)
- **Interação com hardware:** sem registradores, sem DMA, sem interrupções. (**Parte 4**)
- **Concorrência:** mutex presente, mas não exercitado. (**Parte 3**)
- **I/O real:** sem bloqueio, sem poll/select. (**Capítulo 10**)

Isso foi intencional. **Domine a estrutura antes da complexidade.** Você não aprenderia carpintaria construindo um arranha-céu no primeiro dia. Começaria com uma bancada de trabalho, exatamente como fez aqui.

### O Que Vem a Seguir

**Capítulo 8, Trabalhando com Arquivos de Dispositivo.** Permissões e proprietários no devfs, nós persistentes e as sondas em espaço do usuário que você usará para inspecionar e testar seu dispositivo.

**Capítulos 9 e 10, Lendo e Escrevendo em Dispositivos, e Tratando Entrada e Saída com Eficiência.** Implemente leitura e escrita com `uiomove`, introduza buffering e controle de fluxo, e defina semântica de bloqueio, não bloqueio e poll ou `kqueue` com tratamento de erros adequado.

---

### Seus Próximos Passos

**Antes de avançar para o Capítulo 8:**

1. **Conclua todos os laboratórios** deste capítulo, caso ainda não o tenha feito.
2. **Tente pelo menos dois exercícios** para reforçar os padrões.
3. **Teste seu driver com rigor:** carregue e descarregue 10 vezes, abra e feche 10 vezes, simule mais uma falha.
4. **Faça commit do seu código no Git:** este é um marco.

```bash
% cd ~/drivers/myfirst
% git add myfirst.c Makefile
% git commit -m "Chapter 7 complete: minimal driver with lifecycle discipline"
```

5. **Descanse um pouco.** Você merece. Programação de kernel é intensa, e o tempo de consolidação importa.

**Quando estiver pronto para o Capítulo 8:**

- Você vai estender este mesmo driver (sem recomeçar do zero).
- A estrutura que você construiu aqui será aproveitada adiante.
- Os conceitos serão construídos de forma incremental, sem reiniciar do início.

### Uma Palavra Final

Construir um driver do zero pode parecer avassalador no começo. Mas veja o que você conquistou:

- Você começou com nada além de um Makefile e um arquivo `.c` em branco.
- Você construiu um driver que compila, carrega, faz o attach, opera, faz o detach e descarrega de forma limpa.
- Você testou os caminhos de erro e verificou a limpeza dos recursos.
- Você expôs o estado via sysctls e criou um nó de dispositivo acessível pelo usuário.

**Isso não é sorte de principiante. Isso é competência.** A maioria dos desenvolvedores de software de propósito geral nunca toca em um módulo do kernel, muito menos constrói um com caminhos disciplinados de attach e detach. Você acabou de fazer exatamente isso.

A jornada de "hello module" até "driver de produção" é longa, mas você deu o passo mais difícil: **começar**. Cada capítulo daqui em diante adiciona mais uma camada de capacidade, mais uma ferramenta no seu kit.

Mantenha seu caderno de laboratório atualizado. Continue experimentando. Continue perguntando "por quê?" quando algo não fizer sentido. E, o mais importante, **continue construindo**.

Bem-vindo ao mundo do desenvolvimento de drivers FreeBSD. Você conquistou seu lugar aqui.

### Até o Capítulo 8

No próximo capítulo, vamos dar vida ao seu nó de dispositivo implementando semântica real de arquivo: gerenciamento de estado por abertura, tratamento de acesso exclusivo versus compartilhado e preparação do terreno para I/O de verdade.

Até lá, aproveite seu sucesso. Você construiu algo real, e isso merece ser celebrado.

*"O especialista em qualquer coisa já foi um iniciante um dia." - Helen Hayes*
