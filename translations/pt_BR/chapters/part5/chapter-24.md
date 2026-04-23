---
title: "Integrando com o Kernel"
description: "O Capítulo 24 estende o driver myfirst da versão 1.6-debug para a 1.7-integration. Ele ensina o que significa para um driver deixar de ser um módulo do kernel autocontido e passar a se comportar como cidadão do kernel FreeBSD mais amplo. O capítulo explica por que a integração importa; como um driver vive dentro do devfs e como os nós em /dev aparecem, recebem permissões, são renomeados e desaparecem; como implementar uma interface ioctl() com a qual o espaço do usuário possa contar, incluindo a codificação _IO/_IOR/_IOW/_IOWR e a camada automática de copyin/copyout do kernel; como expor métricas, contadores e parâmetros ajustáveis do driver por meio de árvores sysctl dinâmicas enraizadas sob dev.myfirst.N.; como pensar em conectar um driver à pilha de rede por meio do ifnet(9) em nível introdutório, usando if_tuntap.c como referência; como pensar em conectar um driver ao subsistema de armazenamento CAM em nível introdutório, usando cam_sim_alloc e xpt_bus_register; como organizar o registro, o attach, o teardown e a limpeza para que os caminhos integrados possam ser carregados e descarregados de forma limpa mesmo sob carga; e como refatorar o driver em um pacote versionado e de fácil manutenção que os capítulos futuros possam continuar expandindo. O driver ganha myfirst_ioctl.c, myfirst_ioctl.h, myfirst_sysctl.c e um pequeno programa de teste complementar; ganha um nó /dev/myfirst0 com suporte a clone e uma subárvore sysctl por instância; e encerra o Capítulo 24 como um driver com o qual outros softwares podem se comunicar da maneira nativa do FreeBSD."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 24
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Integrando com o Kernel

## Orientações para o Leitor e Resultados Esperados

O Capítulo 23 encerrou com um driver que finalmente consegue se explicar. O driver `myfirst` na versão `1.6-debug` sabe como registrar mensagens estruturadas por meio de `device_printf`, como condicionar a saída detalhada por uma máscara sysctl em tempo de execução, como expor pontos de sondagem estáticos ao DTrace, e como deixar um rastro de auditoria que um operador pode consultar depois. Combinado com a disciplina de gerenciamento de energia adicionada no Capítulo 22, o pipeline de DMA adicionado no Capítulo 21, e a maquinaria de interrupções adicionada nos Capítulos 19 e 20, o driver agora é uma unidade completa em si mesmo: ele inicializa, executa, se comunica com um dispositivo PCI real, sobrevive a suspend e resume, e informa ao desenvolvedor o que está fazendo ao longo do caminho.

O que o driver ainda não faz é comportar-se como um cidadão integrante do kernel mais amplo. Há ainda muito pouco em `myfirst` que um programa externo possa ver, controlar ou medir. O driver cria exatamente um nó de dispositivo quando o módulo é carregado. Não há como uma ferramenta do espaço do usuário pedir ao driver que se reinicialize, que altere um parâmetro de configuração em tempo de execução, ou que leia um contador. Não há métricas na árvore sysctl do sistema que um operador possa enviar para um sistema de monitoramento. Não há como fornecer ao driver várias instâncias de forma organizada. Não há integração com a pilha de rede, com a pilha de armazenamento, ou com qualquer um dos subsistemas do kernel que os usuários normalmente acessam a partir de seus próprios programas. Em todo sentido significativo, o driver ainda está parado sozinho num canto. O Capítulo 24 é o capítulo que o traz para dentro da sala.

O Capítulo 24 ensina integração com o kernel no nível adequado para este estágio do livro. O leitor passará este capítulo aprendendo o que integração realmente significa, por que ela importa mais do que parece à primeira vista, e como cada superfície de integração é construída. O capítulo começa com a história conceitual: a diferença entre um driver funcional e um driver integrado, e o custo de tratar a integração como algo secundário. Em seguida, dedica a maior parte do tempo às quatro interfaces com as quais um driver FreeBSD típico sempre se integra: o sistema de arquivos de dispositivos `devfs`, o canal `ioctl(2)` controlado pelo usuário, a árvore `sysctl(8)` de todo o sistema, e os hooks de ciclo de vida do kernel para attach, detach e descarregamento de módulo de forma limpa. Depois dessas quatro, vêm dois capítulos opcionais em miniatura: um para drivers cujo hardware é um dispositivo de rede, e outro para drivers cujo hardware é um controlador de armazenamento. Ambos são introduzidos em nível conceitual para que o leitor os reconheça quando aparecerem mais tarde na Parte 6 e na Parte 7, e nenhum deles é ensinado por completo porque cada um acabará merecendo seu próprio capítulo. O capítulo então recua para discutir a disciplina de registro e desmontagem em todas essas superfícies: a ordem importa, os caminhos de falha importam, os casos extremos sob estresse importam, e um driver que acerta as interfaces de integração mas erra o ciclo de vida ainda é um driver frágil. Por fim, o capítulo encerra com uma refatoração que divide o novo código em seus próprios arquivos, incrementa o driver para a versão `1.7-integration`, atualiza o banner de versão, e deixa a árvore de código-fonte organizada para tudo o que vem a seguir.

O arco da Parte 5 continua aqui. O Capítulo 22 fez o driver sobreviver a uma mudança de estado de energia. O Capítulo 23 fez o driver contar o que está fazendo. O Capítulo 24 faz o driver se encaixar naturalmente no resto do sistema, para que as ferramentas e os hábitos que os usuários do FreeBSD já conhecem se apliquem ao seu driver sem surpresas. O Capítulo 25 continuará o arco ensinando a disciplina de manutenção que mantém um driver legível, ajustável e extensível conforme ele evolui, e a Parte 6 então iniciará os capítulos específicos de transporte que se apoiam em todas as qualidades que os capítulos da Parte 5 construíram.

### Por que a Integração com devfs, ioctl e sysctl Merece um Capítulo Próprio

Uma preocupação que surge aqui é se conectar `devfs`, `ioctl` e `sysctl` realmente merece um capítulo completo. O driver já tem um nó `cdev` simples de um capítulo muito anterior. Adicionar um ioctl parece pequeno. Adicionar um sysctl parece ainda menor. Por que distribuir o trabalho por um capítulo longo, quando cada interface parece composta de algumas dezenas de linhas de código?

A resposta é que a visão de "algumas dezenas de linhas" é a parte fácil. Cada interface tem um conjunto de convenções e armadilhas que não são óbvias ao ler a API uma única vez, e o custo de errar não é pago pelo desenvolvedor, mas pelo operador que tenta monitorar o driver, pelo usuário que tenta reinicializar o dispositivo, pelo empacotador que tenta carregar e descarregar o módulo sob carga, e pelo próximo desenvolvedor que tenta estender o driver seis meses depois. O Capítulo 24 dedica seu tempo a essas convenções e armadilhas porque é aí que está o valor.

O primeiro motivo pelo qual este capítulo justifica seu lugar é que **as interfaces de integração são como todo o resto acessa o driver**. Um leitor que acompanhou o livro até aqui construiu um driver que realiza trabalho interessante, mas apenas o próprio kernel sabe como pedir ao driver que realize esse trabalho. Quando o driver tiver uma interface ioctl, um script shell poderá controlá-lo. Quando o driver tiver uma árvore sysctl, um sistema de monitoramento poderá observá-lo. Quando o driver criar nós `/dev` que seguem as convenções padrão, um empacotador pode distribuir regras no estilo udev para ele, o administrador do sistema pode escrever regras `/etc/devfs.rules` para ele, e outro driver pode construir sobre ele por meio de `vop_open` ou de `ifnet`. Nada disso depende do que o driver faz; tudo depende de se a integração foi feita corretamente.

O segundo motivo é que **as escolhas de integração se manifestam em modos de falha em produção**. Um driver que chama `make_dev` a partir do contexto errado pode entrar em deadlock durante o carregamento do módulo. Um driver que omite a disciplina de `_IO`, `_IOR`, `_IOW`, `_IOWR` força cada chamador a inventar uma convenção particular para quem copia o quê através da fronteira usuário-kernel, e pelo menos um desses chamadores errará. Um driver que esquece de chamar `sysctl_ctx_free` no detach vaza OIDs, e o próximo carregamento de módulo que usa o mesmo nome falha com uma mensagem confusa. Um driver que destrói seu `cdev` antes de esvaziar os file handles abertos produz panics de use-after-free. O Capítulo 24 dedica parágrafos a cada um desses casos porque cada um é um bug real que a comunidade FreeBSD teve que rastrear ao longo dos anos, e o momento certo para aprender a disciplina é antes de escrever a primeira linha de código de integração, não depois.

O terceiro motivo é que **o código de integração é o primeiro lugar onde o design de um driver se torna visível para alguém além de seu autor**. Até o Capítulo 24, o driver era uma caixa-preta com uma tabela de métodos e um nó de dispositivo. A partir do Capítulo 24, o driver tem uma superfície pública. Os nomes de seus sysctls aparecem em gráficos de monitoramento. Os números de seus ioctls aparecem em scripts shell e em bibliotecas do espaço do usuário. O layout de seus nós `/dev` aparece na documentação de pacotes e nos runbooks de administradores. Uma vez que uma superfície pública existe, alterá-la tem um custo. O capítulo, portanto, se preocupa em ensinar as convenções que mantêm essa superfície estável conforme o driver evolui. O incremento de versão para `1.7-integration` é também a primeira versão do driver que tem uma face pública real; tudo antes disso era um marco interno.

O Capítulo 24 justifica seu lugar ensinando essas três ideias juntas, de forma concreta, com o driver `myfirst` como exemplo contínuo. Um leitor que termina o Capítulo 24 pode integrar qualquer driver FreeBSD às interfaces padrão do sistema, conhece as convenções e armadilhas de cada superfície de integração, consegue ler o código de integração de outro driver e identificar o que é normal e o que é incomum, e tem um driver `myfirst` com o qual outros softwares finalmente conseguem se comunicar.

### Onde o Capítulo 23 Deixou o Driver

Um breve ponto de verificação antes do trabalho de verdade começar. O Capítulo 24 estende o driver produzido no final do Capítulo 23, marcado como versão `1.6-debug`. Se algum dos itens abaixo estiver incerto, volte ao Capítulo 23 e corrija-o antes de iniciar este capítulo, porque os tópicos de integração assumem que a disciplina de depuração já existe, e várias das novas superfícies de integração farão uso dela.

- Seu driver compila sem erros e se identifica como `1.6-debug` em `kldstat -v`.
- O driver ainda faz tudo o que fazia em `1.5-power`: ele realiza attach em um dispositivo PCI (ou PCI simulado), aloca vetores MSI-X, executa um pipeline de DMA, e sobrevive a `devctl suspend myfirst0` seguido de `devctl resume myfirst0`.
- O driver tem um par `myfirst_debug.c` e `myfirst_debug.h` em disco. O cabeçalho define `MYF_DBG_INIT`, `MYF_DBG_OPEN`, `MYF_DBG_IO`, `MYF_DBG_IOCTL`, `MYF_DBG_INTR`, `MYF_DBG_DMA`, `MYF_DBG_PWR` e `MYF_DBG_MEM`. A macro `DPRINTF(sc, MASK, fmt, ...)` está no escopo a partir de qualquer arquivo-fonte do driver.
- O driver tem três probes SDT chamados `myfirst:::open`, `myfirst:::close` e `myfirst:::io`. O simples one-liner DTrace `dtrace -n 'myfirst::: { @[probename] = count(); }'` retorna contagens quando o dispositivo é exercitado.
- O softc carrega um campo `uint32_t sc_debug`, e `sysctl dev.myfirst.0.debug.mask` lê e escreve nele.
- O driver tem um documento `DEBUG.md` ao lado do código-fonte. `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md` e `POWER.md` também estão atualizados em sua árvore de trabalho dos capítulos anteriores.
- Seu kernel de teste ainda tem `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` e `DDB_CTF` habilitados. Os laboratórios do Capítulo 24 usam o mesmo kernel.

Esse é o driver que o Capítulo 24 estende. As adições são maiores do que as do Capítulo 23 em linhas de código, mas menores em superfície conceitual. As novas partes são: um nó `/dev/myfirst0` mais rico que pode clonar instâncias sob demanda, um pequeno conjunto de ioctls bem tipados com um cabeçalho público que programas do espaço do usuário podem incluir, uma subárvore sysctl por instância sob `dev.myfirst.N.` expondo algumas métricas e um parâmetro modificável, uma refatoração que divide o novo código em `myfirst_ioctl.c` e `myfirst_sysctl.c` com cabeçalhos correspondentes, um pequeno programa companheiro no espaço do usuário chamado `myfirstctl` que exercita as novas interfaces, um documento `INTEGRATION.md` ao lado do código-fonte, um teste de regressão atualizado, e um incremento de versão para `1.7-integration`.

### O que Você Aprenderá

Quando terminar este capítulo, você será capaz de:

- Explicar o que integração com o kernel significa em termos concretos no FreeBSD, distinguir um driver autocontido de um driver integrado e nomear os benefícios específicos visíveis ao usuário que cada superfície de integração oferece.
- Descrever o que é `devfs`, como ele difere dos esquemas estáticos mais antigos de `/dev` e como os nós de dispositivo passam a existir e deixam de existir sob ele. Usar `make_dev`, `make_dev_s`, `make_dev_credf` e `destroy_dev` corretamente. Escolher o conjunto correto de flags, propriedade e modo para um nó.
- Inicializar e preencher uma `struct cdevsw` com o campo `D_VERSION` moderno, o conjunto mínimo de callbacks e os callbacks opcionais (`d_kqfilter`, `d_mmap_single`, `d_purge`).
- Usar os campos `cdev->si_drv1` e `si_drv2` para associar estado do driver por nó e recuperar esse estado de dentro dos callbacks cdevsw.
- Criar mais de um nó de dispositivo a partir de uma única instância de driver e escolher entre nós de nome fixo, nós indexados e nós clonáveis por meio do manipulador de eventos `dev_clone`.
- Definir permissões e propriedade por nó no momento da criação e ajustá-las após a criação por meio de `devfs.rules`, para que os administradores possam conceder acesso sem recompilar o driver.
- Explicar o que é `ioctl(2)`, como o kernel codifica comandos ioctl usando `_IO`, `_IOR`, `_IOW` e `_IOWR`, o que cada macro significa em relação à direção do fluxo de dados e por que acertar a codificação é importante para a portabilidade entre o espaço do usuário de 32 bits e de 64 bits.
- Definir um header ioctl público para um driver, escolher uma letra mágica livre e documentar cada comando para que os chamadores no espaço do usuário possam contar com a interface ao longo das versões.
- Implementar um manipulador `d_ioctl` que despacha com base na palavra de comando, executa a lógica por comando com segurança e retorna o errno correto em cada caminho de falha.
- Ler e entender a camada automática `copyin`/`copyout` do kernel para dados ioctl e reconhecer os casos em que o driver ainda precisa copiar memória por conta própria: payloads de tamanho variável, ponteiros do usuário embutidos e estruturas cujo layout exige alinhamento explícito.
- Explicar `sysctl(9)`, distinguir OIDs estáticos de OIDs dinâmicos e percorrer o padrão com `device_get_sysctl_ctx` e `device_get_sysctl_tree` que concede a cada dispositivo sua própria subárvore.
- Adicionar contadores somente leitura usando `SYSCTL_ADD_UINT` e `SYSCTL_ADD_QUAD`, adicionar parâmetros graváveis com flags de acesso adequadas e adicionar OIDs procedurais personalizados com `SYSCTL_ADD_PROC` para os casos em que o valor precisa ser calculado no momento da leitura.
- Gerenciar tunables que um usuário pode definir em `/boot/loader.conf` com `TUNABLE_INT_FETCH` e combinar tunables e sysctls para que o mesmo parâmetro de configuração possa ser definido no boot ou ajustado em tempo de execução.
- Reconhecer o formato introdutório da integração de rede do FreeBSD: como `if_alloc`, `if_initname`, `if_attach`, `bpfattach`, `if_detach` e `if_free` se compõem; o que é um `if_t` em nível conceitual; e que papel os drivers desempenham na infraestrutura ifnet mais ampla. Entender que o Capítulo 28 retorna a esse assunto em profundidade.
- Reconhecer o formato introdutório da integração de armazenamento do FreeBSD: o que é CAM, como `cam_sim_alloc`, `xpt_bus_register`, o callback `sim_action` e `xpt_done` se compõem; o que é um CCB em nível conceitual; e por que o CAM existe. Entender que os drivers de armazenamento do Capítulo 27 e o material GEOM do Capítulo 27 retornam a esse assunto em profundidade.
- Aplicar uma disciplina de registro e teardown que seja robusta sob `kldload`/`kldunload` repetidos, sob falha de attach no meio da inicialização, sob falha de detach quando usuários com operações em andamento ainda mantêm descritores de arquivo abertos e sob remoção surpresa genuína do dispositivo subjacente.
- Refatorar um driver que acumulou várias superfícies de integração em uma estrutura sustentável: um arquivo separado por aspecto de integração, um header público para o espaço do usuário, um header privado para uso interno do driver e um sistema de build atualizado que compila todas as peças em um único módulo do kernel.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 24 permaneça focado na disciplina de integração.

- **A implementação completa de um driver de rede ifnet**, incluindo filas de transmissão e recepção, coordenação de múltiplas filas via `iflib(9)`, integração com BPF além da chamada introdutória `bpfattach`, eventos de estado de link e o ciclo de vida completo de um driver Ethernet. O Capítulo 28 é o capítulo dedicado a drivers de rede e assume que a disciplina de integração do Capítulo 24 já está assimilada.
- **A implementação completa de um driver de armazenamento CAM**, incluindo o modo target, o conjunto completo de tipos CCB, notificações assíncronas via `xpt_setup_ccb` e `xpt_async`, e a apresentação de geometria por meio de `disk_create` ou GEOM. O Capítulo 27 cobre a pilha de armazenamento em profundidade.
- **Integração com GEOM**, incluindo provedores, consumidores, classes, `g_attach`, `g_detach` e o mecanismo de eventos do GEOM. GEOM é um subsistema próprio com suas próprias convenções; o Capítulo 27 o cobre.
- **Concorrência baseada em `epoch(9)`**, que é o padrão moderno de locking para hot paths ifnet. O Capítulo 24 o menciona apenas em contexto. O Capítulo 28 (drivers de rede) retoma o assunto ao lado de `iflib(9)`, onde a concorrência no estilo epoch é necessária na prática.
- **Integração com `mac(9)` (Mandatory Access Control)**, que adiciona hooks de política em torno das superfícies de integração. O framework MAC é um tópico especializado que ainda não se aplica ao driver simples `myfirst`.
- **Integração com `vfs(9)`**, que é o que os sistemas de arquivos fazem. Um driver de caracteres não interage com o VFS na camada de `vop_open` ou `vop_read`; ele interage com `cdevsw` e devfs. O capítulo tem o cuidado de não confundir os dois.
- **Interfaces entre drivers via `kobj(9)` e interfaces personalizadas declaradas com o mecanismo de build `INTERFACE`**. É assim que as pilhas de rede e armazenamento definem seus contratos internos. Elas são mencionadas em contexto na Seção 7, mas o tratamento aprofundado pertence a um capítulo posterior e mais avançado.
- **A nova interface `netlink(9)` que kernels recentes do FreeBSD expõem para parte do tráfego de gerenciamento de rede**. O Netlink é usado atualmente pelo subsistema de roteamento, e não por drivers de dispositivos individuais; o lugar certo para ensiná-lo é junto ao capítulo de rede.
- **Módulos de protocolo personalizados via `pr_protocol_init`**, que se destinam a novos protocolos de transporte, e não a drivers de dispositivos.

Manter-se dentro dessas fronteiras garante que o Capítulo 24 seja um capítulo sobre como um driver se integra ao kernel, e não um capítulo sobre todos os subsistemas do kernel que um driver pode eventualmente tocar.

### Investimento de Tempo Estimado

- **Somente leitura**: de quatro a cinco horas. As ideias do Capítulo 24 são, em sua maioria, extensões conceituais de coisas que o leitor já encontrou. O novo vocabulário (devfs, cdev, ioctl, sysctl, ifnet, CAM) é em grande parte familiar pelo nome a partir de capítulos anteriores; o trabalho do capítulo é dar a cada um deles uma forma concreta.
- **Leitura mais digitação dos exemplos trabalhados**: de dez a doze horas ao longo de duas ou três sessões. O driver evolui por três superfícies de integração em sequência (devfs, ioctl, sysctl), cada uma com seu próprio estágio curto. Cada estágio é curto e autocontido; os testes são o que leva mais tempo, pois as superfícies de integração se verificam melhor escrevendo um pequeno programa em espaço do usuário que as aciona.
- **Leitura mais todos os laboratórios e desafios**: de quinze a dezoito horas ao longo de três ou quatro sessões. Os laboratórios incluem um experimento devfs com clone-aware, um roundtrip completo de ioctl com `myfirstctl`, um exercício de monitoramento de contador via sysctl, um laboratório de disciplina de limpeza que quebra intencionalmente o teardown para expor o padrão de falha, e um pequeno desafio com stub ifnet para leitores que queiram uma prévia do Capítulo 28.

As Seções 3 e 4 são as mais densas em termos de vocabulário novo. As macros de ioctl e as assinaturas de callback de sysctl são as únicas APIs verdadeiramente novas no capítulo; o restante é composição. Se as macros parecerem opacas numa primeira leitura, isso é normal. Pare, execute o exercício de correspondência no driver e volte quando a forma tiver se assentado.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 3 do Capítulo 23 (`1.6-debug`). O ponto de partida pressupõe todos os primitivos do Capítulo 23: a macro `DPRINTF`, as probes SDT, o sysctl de máscara de debug e o par de arquivos `myfirst_debug.c`/`myfirst_debug.h`. O Capítulo 24 constrói novo código de integração que utiliza cada um deles nos momentos apropriados.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco e compatível com o kernel em execução.
- Um kernel de debug com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` e `DDB_CTF` está compilado, instalado e inicializando corretamente.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível e você possui um snapshot de VM utilizável no estado `1.6-debug`. Os laboratórios do Capítulo 24 incluem cenários de falha intencional para a seção de disciplina de limpeza, e um snapshot torna a recuperação simples.
- Os seguintes comandos em espaço do usuário estão no seu path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `cc`, `make`, `dtrace`, `dd`, `head`, `cat`, `chown`, `chmod` e `truss`. Os laboratórios do Capítulo 24 fazem uso leve de `truss`, o equivalente FreeBSD do `strace` do Linux, para verificar que programas em espaço do usuário realmente chegam ao driver pelos novos ioctls.
- Você se sente confortável para escrever um programa C curto usando os cabeçalhos `libc` do FreeBSD. O capítulo apresenta o lado em espaço do usuário dos novos ioctls por meio de um pequeno programa chamado `myfirstctl`.
- Um conhecimento funcional de `git` é útil, mas não obrigatório. O capítulo recomenda que você faça commit entre os estágios, de modo que cada versão do driver tenha um ponto recuperável.

Se algum item acima estiver frágil, corrija-o agora. O código de integração é, no geral, menos perigoso que o trabalho em modo kernel dos capítulos anteriores, pois a maioria dos modos de falha é capturada no carregamento do módulo ou na invocação em espaço do usuário, em vez de produzir kernel panics. Mas as lições se acumulam: um erro em `make_dev` com clone-aware neste capítulo produzirá diagnósticos feios no Capítulo 28, quando o driver de rede também quiser seus próprios nós, e um vazamento de OID sysctl neste capítulo produzirá falhas confusas no carregamento do módulo no Capítulo 27, quando o driver de armazenamento tentar registrar nomes que já existem.

### Como Aproveitar ao Máximo Este Capítulo

Cinco hábitos rendem mais neste capítulo do que em qualquer um dos capítulos anteriores da Parte 5.

Primeiro, mantenha `/usr/src/sys/dev/null/null.c`, `/usr/src/sys/sys/conf.h`, `/usr/src/sys/sys/ioccom.h` e `/usr/src/sys/sys/sysctl.h` nos favoritos. O primeiro é o driver de caracteres não trivial mais curto da árvore de código-fonte do FreeBSD e é o exemplo canônico do padrão `cdevsw`/`make_dev`/`destroy_dev`. O segundo declara a estrutura `cdevsw`, a família `make_dev` e os bits de flag `MAKEDEV_*` que o capítulo usa repetidamente. O terceiro define as macros de codificação de ioctl (`_IO`, `_IOR`, `_IOW`, `_IOWR`) e contém as constantes `IOC_VOID`, `IOC_IN`, `IOC_OUT` e `IOC_INOUT` que o kernel usa para decidir se copia dados automaticamente. O quarto define as macros de OID sysctl, a convenção de chamada `SYSCTL_HANDLER_ARGS` e as interfaces de OID estáticas e dinâmicas. Nenhum desses arquivos é longo; o maior tem algumas milhares de linhas e a maior parte são comentários. Ler cada um uma vez no início da seção correspondente é a coisa mais eficaz que você pode fazer para ganhar fluência.

Segundo, mantenha três exemplos reais de drivers à mão: `/usr/src/sys/dev/null/null.c`, `/usr/src/sys/net/if_tuntap.c` e `/usr/src/sys/dev/virtio/block/virtio_blk.c`. O primeiro é o exemplo mínimo de cdevsw. O segundo é o exemplo canônico de clone-aware com `dev_clone`, usado pela Seção 5 para apresentar ifnet. O terceiro ilustra uma árvore sysctl dinâmica completa baseada em `device_get_sysctl_ctx` e um callback `SYSCTL_ADD_PROC` que alterna um parâmetro em tempo de execução. O Capítulo 24 aponta de volta para cada um no momento certo. Lê-los uma vez agora, sem tentar memorizar, dá ao restante do capítulo âncoras concretas nas quais pendurar suas ideias.

> **Uma nota sobre números de linha.** As referências a `null.c`, `if_tuntap.c` e `virtio_blk.c` ao longo do capítulo estão ancoradas em símbolos nomeados: uma chamada específica a `make_dev`, um handler `SYSCTL_ADD_PROC`, um `cdevsw` particular. Esses nomes persistem nas versões futuras do FreeBSD 14.x. A linha específica em que cada nome se encontra, não. Quando o texto citar uma localização, abra o arquivo e pesquise pelo símbolo em vez de rolar até o número.

Terceiro, digite cada alteração de código no driver `myfirst` à mão. O código de integração é o tipo de código que é fácil de copiar e muito difícil de lembrar depois. Digitar a tabela `cdevsw`, as definições de comando ioctl, a construção da árvore sysctl e o programa `myfirstctl` em espaço do usuário constrói o tipo de familiaridade que o copiar e colar não consegue. O objetivo não é ter o código; o objetivo é ser a pessoa que poderia escrevê-lo novamente, do zero, em vinte minutos quando um bug futuro exigir.

Quarto, compile o programa em espaço do usuário em cada estágio. Muitas das lições do capítulo são visíveis apenas pelo lado do usuário. Se o kernel copiou um payload de ioctl corretamente, se um sysctl é legível mas não gravável, se um nó `/dev` tem as permissões que você definiu, se um clone produz um nó de dispositivo utilizável, todas essas perguntas são respondidas com `cat`, `dd`, `chmod`, `sysctl`, `truss` e o pequeno programa companheiro `myfirstctl`. Um driver testado apenas pelos seus contadores no lado do kernel foi testado pela metade.

Quinto, após terminar a Seção 4, releia a disciplina de debug do Capítulo 23. Cada superfície de integração no Capítulo 24 é envolvida em um `DPRINTF` do Capítulo 23. Cada caminho de ioctl dispara linhas de log `MYF_DBG_IOCTL`. Cada caminho sysctl pode ser observado pelo mecanismo SDT. Ver como as ferramentas do Capítulo 23 servem às interfaces do Capítulo 24 reforça ambos os capítulos e prepara o leitor para o Capítulo 25, onde o mesmo padrão continua.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **Por que a Integração Importa.** A história conceitual. Do módulo independente ao componente do sistema; o custo de deixar a integração como algo secundário; as quatro interfaces visíveis ao usuário que todo driver integrado acaba precisando; os ganchos de subsistema opcionais que o capítulo apresenta, mas não conclui.

2. **Trabalhando com devfs e a Árvore de Dispositivos.** A visão do kernel sobre `/dev`. O que é devfs e como ela difere das tabelas estáticas de dispositivos de sistemas mais antigos; o ciclo de vida de um `cdev`; `make_dev` e funções relacionadas em detalhes; a estrutura `cdevsw` e seus callbacks; os campos `si_drv1`/`si_drv2`; permissões e propriedade; nós clonáveis por meio do manipulador de eventos `dev_clone`. O Estágio 1 do driver do Capítulo 24 substitui a criação ad-hoc original de nós por um padrão limpo com suporte a clonagem.

3. **Implementando Suporte a `ioctl()`.** A interface de controle orientada pelo usuário. O que são ioctls; a codificação `_IO`/`_IOR`/`_IOW`/`_IOWR`; a camada automática de copyin/copyout do kernel; como escolher uma letra e um número mágicos; como estruturar um cabeçalho ioctl público; como escrever um callback `d_ioctl` que despacha com base na palavra de comando; armadilhas comuns (dados de comprimento variável, ponteiros embutidos, evolução de versão). O Estágio 2 adiciona `myfirst_ioctl.c` e `myfirst_ioctl.h` e um pequeno programa de espaço do usuário `myfirstctl`.

4. **Expondo Métricas por meio de `sysctl()`.** A interface de monitoramento e ajuste. O que são sysctls; OIDs estáticos versus dinâmicos; o padrão `device_get_sysctl_ctx`/`device_get_sysctl_tree`; contadores, parâmetros ajustáveis e callbacks procedurais; a família `SYSCTL_ADD_*`; tunables em `/boot/loader.conf`; controle de acesso e unidades. O Estágio 3 adiciona `myfirst_sysctl.c` e uma subárvore de métricas por instância.

5. **Integração com o Subsistema de Rede (Opcional).** Uma visão breve e conceitual do ifnet. O que é `if_t`; o esboço de `if_alloc`/`if_initname`/`if_attach`/`bpfattach`/`if_detach`/`if_free`; como `tun(4)` e `tap(4)` se organizam em torno dele; qual é o papel do driver dentro do stack de rede mais amplo. A seção é deliberadamente curta; o Capítulo 28 é o capítulo sobre drivers de rede.

6. **Integração com o Subsistema de Armazenamento CAM (Opcional).** Uma visão breve e conceitual do CAM. O que é CAM; o que são um SIM e um CCB; o esboço de `cam_sim_alloc`/`xpt_bus_register`/`xpt_action`/`xpt_done`; como um pequeno disco de memória somente leitura poderia ser exposto por meio dele. A seção é deliberadamente curta; o Capítulo 27 é o capítulo sobre drivers de armazenamento.

7. **Registro, Desmontagem e Disciplina de Limpeza.** O tópico transversal. Manipuladores de eventos de módulo (`MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`); falha no attach com limpeza parcial; falha no detach quando usuários ainda mantêm handles abertos; o padrão de limpeza em caso de falha; ordenação entre as superfícies de integração; o que `bus_generic_attach`, `bus_generic_detach` e `device_delete_children` fazem por você; SYSINIT e EVENTHANDLER para registros transversais.

8. **Refatoração e Versionamento de um Driver Integrado.** A casa em ordem. A divisão final em `myfirst.c`, `myfirst_debug.c`/`.h`, `myfirst_ioctl.c`/`.h` e `myfirst_sysctl.c`/`.h`; o cabeçalho público `myfirst.h` para chamadores do espaço do usuário; o documento `INTEGRATION.md`; o incremento de versão para `1.7-integration`; as adições de testes de regressão; o commit e a tag.

Após as oito seções, vêm um conjunto de laboratórios práticos que exercitam cada superfície de integração de ponta a ponta, um conjunto de exercícios desafio que ampliam os horizontes do leitor sem introduzir novas bases, um guia de resolução de problemas para os sintomas que a maioria dos leitores encontrará, um Encerrando que fecha a história do Capítulo 24 e abre a do Capítulo 25, uma ponte para o próximo capítulo e o cartão de referência rápida e o glossário habituais.

Se esta é sua primeira leitura, avance linearmente e faça os laboratórios em ordem. Se você está revisitando o capítulo, as Seções 2 e 3 são independentes e funcionam bem como leituras em uma única sessão. As Seções 5 e 6 são curtas e conceituais e podem ser puladas em uma primeira leitura sem perder o fio condutor do capítulo, para serem retomadas antes de iniciar o Capítulo 26 ou o Capítulo 27.

Uma observação antes de o trabalho técnico começar. O capítulo frequentemente pede que você compile um pequeno programa de espaço do usuário, rode-o contra o driver, observe o resultado e então retorne ao lado do kernel para ver o que aconteceu. Esse ritmo é deliberado. A integração não é uma propriedade exclusiva do driver; é uma propriedade da relação entre o driver e o restante do sistema. Os programas de espaço do usuário são curtos, mas é por meio deles que o capítulo verifica se cada superfície de integração realmente funciona.

## Seção 1: Por Que a Integração É Importante

Antes do código, o contexto. A Seção 1 explica o que muda quando um driver se torna integrado. O leitor que acompanhou a Parte 4 e os primeiros capítulos da Parte 5 construiu um driver funcional. O que significa dizer que esse driver ainda não está *integrado*, e que qualidades específicas a integração acrescenta?

Esta seção responde a essa pergunta com cuidado e em detalhes, porque o restante do capítulo é a implementação dessas qualidades. Um leitor que entende claramente *por que* cada superfície de integração existe achará muito mais fácil o trabalho de implementação nas Seções 2 a 8. Um leitor que pular o contexto passará o capítulo se perguntando por que o driver precisa de um `ioctl` quando um sysctl interno poderia fazer o mesmo trabalho; essa pergunta tem uma resposta real, e a Seção 1 é onde essa resposta vive.

### De Módulo Standalone a Componente do Sistema

Um driver standalone é um módulo do kernel que faz seu trabalho corretamente quando chamado pelo kernel e se mantém discreto no restante do tempo. O driver `myfirst` atual, na versão `1.6-debug`, é exatamente esse tipo de módulo. Ele possui um único nó `cdev`, nenhum ioctl público, nenhum sysctl publicado além de alguns knobs internos de debug, nenhum relacionamento com qualquer subsistema do kernel fora de seu pequeno canto, e nenhuma expectativa de que algum programa em espaço do usuário vá acessá-lo para dar ordens. Ele funciona, e funciona de forma isolada.

Um componente do sistema, ao contrário, é um módulo do kernel cujo valor depende de seus relacionamentos com o restante do sistema. O mesmo driver `myfirst`, integrado, apresenta um nó `/dev/myfirst0` com as permissões corretas para o papel de seu hardware, expõe uma pequena interface ioctl que programas em espaço do usuário podem usar para reinicializar o dispositivo ou consultar seu status, publica uma árvore sysctl por instância que ferramentas de monitoramento podem consultar, registra-se no subsistema do kernel apropriado caso o hardware seja um dispositivo de rede ou de armazenamento, e libera seus recursos corretamente ao ser descarregado. Cada uma dessas interfaces é pequena. Juntas, elas são a diferença entre um driver que roda na máquina de laboratório de um desenvolvedor e um driver que é distribuído com o FreeBSD.

A transição de standalone para integrado não é uma mudança de uma linha no driver. É uma série de decisões conscientes, cada uma das quais expande a superfície pública do driver e cada uma das quais tem um custo de manutenção. Decisões que parecem pequenas no momento, como a escolha de uma letra mágica para um ioctl ou o nome de um OID sysctl, tornam-se contratos de longa duração. Um driver que escolheu a letra `M` para seus ioctls em 2010 ainda tem esses números em seu cabeçalho público hoje, porque mudá-los quebraria todos os programas em espaço do usuário que já os chamaram.

Uma forma útil de visualizar essa transição é imaginar dois tipos de leitores diante do driver. O primeiro é o desenvolvedor que escreveu o driver: ele sabe tudo sobre ele, pode mudar qualquer coisa nele e pode reconstruí-lo sempre que quiser. O segundo é o administrador de sistemas que instala o módulo a partir de um pacote e nunca lê seu código-fonte: ele só vê o driver através de seus nós `/dev`, seus sysctls, seus ioctls e suas mensagens de log. Um driver standalone é aquele projetado para o primeiro leitor; um driver integrado é aquele projetado para ambos.

O Capítulo 24 ensina o leitor a projetar pensando no segundo leitor. Esse é o trabalho conceitual que o capítulo realiza, e o trabalho de código nas Seções 2 a 8 é a realização prática disso.

### Alvos Comuns de Integração

Um driver FreeBSD tipicamente se integra com quatro superfícies do lado do kernel e, dependendo do hardware, com um de dois subsistemas. Nomear os alvos claramente desde o início ajuda o leitor a manter a estrutura do capítulo em mente.

O primeiro alvo é o **`devfs`**. Todo driver de caracteres cria um ou mais nós `/dev` através de `make_dev` (ou uma de suas variantes) e os remove através de `destroy_dev`. A forma e a nomenclatura desses nós é como o restante do sistema endereça o driver. Um nó chamado `/dev/myfirst0` permite que o administrador o abra com `cat /dev/myfirst0`, que um script o inclua em um `find /dev -name 'myfirst*'`, e que o próprio kernel despache chamadas de open, read, write e ioctl para o driver. A Seção 2 é dedicada ao devfs.

O segundo alvo é o **`ioctl(2)`**. As syscalls `read(2)` e `write(2)` movem bytes; elas não controlam o driver. Qualquer operação de controle que não se encaixa no modelo de fluxo de dados de leitura e escrita vive no `ioctl(2)`. Um programa em espaço do usuário chama `ioctl(fd, MYF_RESET)` para pedir ao driver que reinicialize seu hardware, ou `ioctl(fd, MYF_GET_STATS, &stats)` para ler um snapshot de contadores. Cada ioctl é um ponto de entrada pequeno e bem tipado com um número, uma direção e um payload. A Seção 3 é dedicada a ioctls.

O terceiro alvo é o **`sysctl(8)`**. Contadores, estatísticas e parâmetros ajustáveis vivem na árvore sysctl de todo o sistema, acessível a partir do espaço do usuário com `sysctl(8)` e a partir de C com `sysctlbyname(3)`. Um driver coloca seus OIDs sob `dev.<driver>.<unit>.<name>` para que `sysctl dev.myfirst.0` liste cada métrica e knob que o dispositivo expõe. A interface sysctl é o lugar certo para contadores somente leitura e para knobs que mudam lentamente; o ioctl é o lugar certo para ações rápidas e para dados tipados. A Seção 4 é dedicada a sysctls.

O quarto alvo são os **hooks de ciclo de vida do kernel**. O handler de eventos do módulo (`MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`), os métodos attach e detach da árvore de dispositivos e os registros transversais através de `EVENTHANDLER(9)` e `SYSINIT(9)` definem juntos o que acontece quando o driver entra e sai do kernel. Um driver que acerta as interfaces de integração mas erra o ciclo de vida vaza recursos, causa deadlock ao descarregar ou gera um panic no terceiro ciclo de `kldload`/`kldunload`. A Seção 7 é dedicada à disciplina de ciclo de vida.

Além disso, drivers cujo hardware é um dispositivo de rede se integram com o subsistema **`ifnet`**, e drivers cujo hardware é um dispositivo de armazenamento se integram com o subsistema **`CAM`**. Ambos são conceitualmente grandes o suficiente para que um tratamento aprofundado exija seu próprio capítulo (o Capítulo 28 para ifnet, o Capítulo 27 para CAM e GEOM). A Seção 5 e a Seção 6 deste capítulo os apresentam no nível necessário para reconhecer sua forma e saber que tipo de trabalho eles envolvem.

Cada alvo merece ser nomeado por uma razão específica. `devfs` é o nome que um usuário casual digita no shell. `ioctl` é o ponto de entrada para ações tipadas que um programa precisa emitir contra um dispositivo. `sysctl` é o lugar onde uma ferramenta de monitoramento procura por números. Os hooks de ciclo de vida são o lugar que um empacotador e um administrador de sistemas atingem quando fazem `kldload` e `kldunload`. Os quatro juntos cobrem as quatro faces do driver que importam para qualquer pessoa além de seu autor.

### Benefícios de uma Integração Adequada

A integração não é um fim em si mesma. É um meio para um pequeno conjunto de resultados práticos aos quais o capítulo continuará retornando.

O primeiro resultado é o **monitoramento**. Um driver cujos contadores são visíveis através de `sysctl` pode ser consultado pelo `prometheus-fbsd-exporter`, por verificações do Nagios, por um pequeno script shell que roda a cada minuto. Um driver cujos contadores só são visíveis através de `device_printf` para o `dmesg` só pode ser inspecionado lendo o arquivo de log manualmente. As duas realidades operacionais são muito diferentes, e a diferença é determinada inteiramente pela escolha de integração que o desenvolvedor fez quando o driver foi escrito.

O segundo resultado é o **gerenciamento**. Um driver que expõe um ioctl chamado `MYF_RESET` permite que o administrador inclua um ciclo de reinicialização em uma janela de manutenção via script. Um driver sem essa interface força o administrador a fazer `kldunload` e `kldload` do módulo, que é uma operação muito mais pesada e que descarta todos os descritores de arquivo abertos, podendo não ser aceitável enquanto tráfego de produção está fluindo pelo dispositivo.

O terceiro resultado é a **automação**. Um driver que emite nós `/dev` bem formados com nomes previsíveis permite que `devd(8)` reaja a eventos de attach e detach, execute scripts no hotplug e integre o driver aos fluxos de inicialização, desligamento e recuperação do sistema maior. Um driver que emite um único nó opaco e nunca informa ninguém sobre seu ciclo de vida não pode ser automatizado sem recorrer à análise de logs do `dmesg`, o que é frágil.

O quarto resultado é a **reutilização**. Um driver cuja interface ioctl é bem documentada pode servir de base para bibliotecas de nível mais alto. O daemon `bsnmp`, por exemplo, usa interfaces bem definidas do kernel para expor contadores do driver via SNMP sem tocar no código-fonte do driver. Um driver que projetou suas interfaces corretamente desde o início obtém esses benefícios sem nenhum trabalho adicional.

Esses quatro resultados (monitoramento, gerenciamento, automação, reutilização) são a razão prática para tudo o que se segue. Cada seção do capítulo entrega uma parte de um desses resultados, e a refatoração final na Seção 8 é o que torna o pacote todo apresentável ao restante do sistema.

### Um Breve Tour pelas Ferramentas do Sistema que Dependem de Integração

Um exercício útil antes do trabalho técnico é observar as ferramentas em espaço do usuário que existem precisamente porque os drivers se integram com os subsistemas do kernel descritos acima. Nenhuma dessas ferramentas funciona com um driver standalone. Todas elas funcionam, automaticamente, com um driver integrado.

`devinfo(8)` percorre a árvore de dispositivos do kernel e imprime o que encontra. Funciona porque todo dispositivo na árvore foi registrado através da interface newbus e todo dispositivo tem um nome, um número de unidade e um pai. O administrador executa `devinfo -v` e vê a hierarquia completa de dispositivos, incluindo a instância `myfirst0`.

`sysctl(8)` lê e escreve na árvore sysctl do kernel. Funciona porque todo contador e knob no kernel é acessível através da hierarquia de OIDs, incluindo os OIDs que o driver registrou através de `device_get_sysctl_tree`.

`devctl(8)` permite que o administrador manipule dispositivos individuais: `devctl detach`, `devctl attach`, `devctl suspend`, `devctl resume`, `devctl rescan`. Funciona porque todo dispositivo implementa os métodos kobj que a maquinaria da árvore de dispositivos do kernel espera. O Capítulo 22 já usou `devctl suspend myfirst0` e `devctl resume myfirst0`.

`devd(8)` observa o canal de eventos de dispositivos do kernel e executa scripts em resposta a eventos de attach, detach, hotplug e similares. Funciona porque o kernel emite eventos estruturados para cada operação newbus. Um driver que segue o padrão newbus é automaticamente visível para o `devd`.

`ifconfig(8)` configura interfaces de rede. Funciona porque todo driver de rede se registra no subsistema ifnet e aceita um conjunto padrão de ioctls (a Seção 5 apresenta isso).

`camcontrol(8)` controla dispositivos SCSI e SATA através do CAM. Funciona porque todo driver de armazenamento registra um SIM e processa CCBs (a Seção 6 apresenta isso).

`gstat(8)` mostra estatísticas GEOM em tempo real, `geom(8)` lista a árvore GEOM, `top -H` mostra o uso de CPU por thread. Cada uma dessas ferramentas depende de uma superfície de integração específica com a qual os drivers se registram. O driver que ignora essas superfícies não obtém nenhum dos benefícios.

Uma forma simples de confirmar o ponto é executar, em sua máquina de laboratório, os seguintes exercícios:

```sh
# Tour the device tree
devinfo -v | head -40

# Tour the sysctl tree, just the dev branch
sysctl dev | head -40

# See what the live network interfaces look like
ifconfig -a

# See what storage looks like through CAM
camcontrol devlist

# See what GEOM sees
geom -t

# Watch the device event channel for a few seconds
sudo devd -d -f /dev/null &
DEVDPID=$!
sleep 5
kill $DEVDPID
```

Cada comando existe porque os drivers se integram. Leia a saída e perceba quanto do que está visível veio de drivers que fizeram o trabalho de integração. O driver `myfirst`, no início do Capítulo 24, contribui quase nada para essa saída. Ao final do Capítulo 24, ele contribuirá com sua subárvore `dev.myfirst.0` para o `sysctl`, seu dispositivo `myfirst0` para o `devinfo -v`, e seus nós `/dev/myfirst*` para o sistema de arquivos. Cada passo é pequeno. O agregado é a diferença entre um driver de laboratório de uso único e uma parte real do FreeBSD.

### O Que "Opcional" Significa Neste Capítulo

A Seção 5 (redes) e a Seção 6 (armazenamento) estão rotuladas como opcionais. O rótulo merece uma definição cuidadosa.

Opcional não significa sem importância. As duas seções se tornarão leitura essencial mais adiante no livro: o material sobre redes antes do Capítulo 27 e o material sobre armazenamento antes do Capítulo 26 e do Capítulo 28. Opcional significa que, para um leitor que está acompanhando o driver PCI `myfirst` como exemplo contínuo, os hooks de rede e de armazenamento não serão exercitados neste capítulo, porque `myfirst` não é um dispositivo de rede nem um dispositivo de armazenamento. O capítulo apresenta a forma conceitual desses hooks para que o leitor os reconheça quando aparecerem, e para que as decisões estruturais da Seção 7 e da Seção 8 os levem em consideração.

Um leitor em uma primeira leitura com pouco tempo disponível pode pular as Seções 5 e 6. As demais seções não dependem delas. Um leitor que pretende acompanhar o Capítulo 26 ou o Capítulo 27 deve lê-las, pois elas introduzem o vocabulário que esses capítulos irão pressupor.

O capítulo é honesto quanto à profundidade do que ensina nessas seções. O subsistema de rede é composto por vários milhares de linhas de código-fonte em `/usr/src/sys/net`. O subsistema CAM é composto por vários milhares de linhas de código-fonte em `/usr/src/sys/cam`. Cada um levou anos para ser projetado e ainda está em evolução. O Capítulo 24 os apresenta no nível de *aqui está a forma*, *aqui estão as chamadas que um driver tipicamente realiza*, *aqui está um driver real que usa cada uma delas*. A mecânica completa pertence a outros capítulos.

### Armadilhas no Caminho da Integração

Três armadilhas derrubam a maioria dos integradores de primeira viagem.

A primeira armadilha é **adicionar superfícies de integração ad hoc**. Um driver que ganhou um ioctl numa terça-feira porque o desenvolvedor precisava de uma forma rápida de testar o dispositivo, e um sysctl na semana seguinte porque queria ver um contador, e mais um ioctl no mês seguinte por causa de um bug relatado, termina com uma superfície pública inconsistente em estilo, inconsistente em nomenclatura, inconsistente no tratamento de erros e inconsistente na documentação. O padrão correto é projetar a superfície pública de forma deliberada, usar nomenclatura consistente e documentar cada ponto de entrada em um arquivo de cabeçalho antes de escrever a implementação. A Seção 3 e a Seção 8 retomam essa disciplina.

A segunda armadilha é **misturar responsabilidades dentro dos métodos do dispositivo**. Um driver cujo `device_attach` realiza o trabalho do kobj, a alocação de recursos, a criação do nó devfs, a construção da árvore sysctl e a configuração voltada ao espaço do usuário em uma única função longa rapidamente se torna ilegível. O capítulo recomenda separar essas responsabilidades em funções auxiliares em um primeiro momento, e em arquivos de código-fonte separados na Seção 8. O par `myfirst_debug.c` e `myfirst_debug.h` do Capítulo 23 foi o primeiro passo nessa direção; os novos arquivos `myfirst_ioctl.c` e `myfirst_sysctl.c` deste capítulo continuam o padrão.

A terceira armadilha é **não testar a superfície pública a partir do espaço do usuário**. Um driver que o desenvolvedor testou apenas exercitando-o pelo lado do kernel passará em todos os testes do lado do kernel e ainda assim falhará no momento em que um programa real do espaço do usuário o chamar, porque o desenvolvedor assumiu algo sobre a convenção de chamada que não se sustenta na prática. O capítulo, portanto, insiste em construir o pequeno programa complementar `myfirstctl` assim que o driver tiver quaisquer ioctls, e em testar cada sysctl por meio do `sysctl(8)` em vez de apenas ler o contador interno do driver diretamente. Os testes do espaço do usuário são os únicos que confirmam que a integração de fato funciona.

Essas armadilhas não são exclusivas do FreeBSD. Elas aparecem em todo sistema operacional que possui uma fronteira kernel-usuário. As ferramentas do FreeBSD tornam mais fácil do que na maioria dos sistemas fazer a coisa certa, porque as convenções estão bem documentadas nas páginas de manual (`devfs(5)`, `ioctl(2)`, `sysctl(9)`, `style(9)`) e porque o próprio kernel é distribuído com centenas de drivers integrados que o leitor pode estudar. O capítulo se apoia nessas convenções e faz referência aos drivers reais correspondentes ao longo do texto.

### Um Modelo Mental para o Capítulo

Antes de avançar para a Seção 2, é útil fixar uma única imagem na mente. O driver, ao final do Capítulo 24, terá a seguinte aparência visto de fora:

```text
Userland tools                          Kernel
+----------------------+                +----------------------+
| myfirstctl           |  ioctl(2)      | d_ioctl callback     |
| sysctl(8)            +--------------->| sysctl OID tree      |
| cat /dev/myfirst0    |  read/write    | d_read, d_write      |
| chmod, chown         |  fileops       | devfs node lifecycle |
| devinfo -v           |  newbus query  | device_t myfirst0    |
| dtrace -n 'myfirst:::'|  SDT probes   | sc_debug, DPRINTF    |
+----------------------+                +----------------------+
```

Cada entrada à esquerda é uma ferramenta que um usuário real do FreeBSD já conhece. Cada entrada à direita é uma parte da integração que o capítulo ensina você a escrever. As setas no meio são o que cada seção do capítulo implementa.

Mantenha essa imagem em mente enquanto o trabalho técnico começa. O objetivo de cada seção que se segue é adicionar uma dessas setas ao driver, com a disciplina que permite que a seta permaneça confiável à medida que o driver cresce.

### Encerrando a Seção 1

A integração é a disciplina de tornar um driver visível, controlável e observável a partir de fora do seu próprio código-fonte. As quatro superfícies de integração primárias no FreeBSD são devfs, ioctl, sysctl e os hooks do ciclo de vida do kernel. Dois hooks de subsistema opcionais são ifnet para dispositivos de rede e CAM para dispositivos de armazenamento. Juntos, é assim que um driver deixa de ser um projeto de um único desenvolvedor e se torna parte do FreeBSD. As seções restantes deste capítulo implementam cada superfície, com o driver `myfirst` como exemplo contínuo e o incremento de versão de `1.6-debug` para `1.7-integration` como o marco visível.

Na próxima seção, voltamos nossa atenção para a primeira e mais fundamental superfície de integração: `devfs` e o sistema de arquivos de dispositivos que dá a cada driver de caracteres sua presença em `/dev`.

## Seção 2: Trabalhando com devfs e a Árvore de Dispositivos

A primeira superfície de integração que todo driver de caracteres atravessa é o `devfs`, o sistema de arquivos de dispositivos. O leitor vem criando `/dev/myfirst0` desde os capítulos mais iniciais, mas a chamada a `make_dev` sempre foi apresentada como uma única linha de código padrão sem muita explicação. A Seção 2 preenche essa lacuna. Ela explica o que o devfs realmente é, percorre o ciclo de vida de um `cdev`, examina em detalhes as variantes de `make_dev` e a tabela de callbacks `cdevsw`, mostra como anexar estado por nó por meio de `si_drv1` e `si_drv2`, ensina o padrão moderno com suporte a clone para drivers que desejam um nó por instância sob demanda, e termina mostrando como um administrador pode ajustar permissões e propriedade sem recompilar o driver.

### O que é o devfs

O `devfs` é um sistema de arquivos virtual que expõe o conjunto de dispositivos de caracteres registrados pelo kernel como uma árvore de arquivos sob `/dev`. É virtual no mesmo sentido em que `procfs` e `tmpfs` são virtuais: não há armazenamento em disco que o suporte. Cada arquivo em `/dev` é a projeção de uma estrutura `cdev` pelo kernel no namespace do sistema de arquivos. Quando um programa do espaço do usuário chama `open("/dev/null", O_RDWR)`, o kernel pesquisa o caminho no devfs, encontra o `cdev` correspondente, segue o ponteiro até a tabela `cdevsw` e despacha a abertura por meio dela.

Sistemas UNIX mais antigos usavam uma árvore de dispositivos estática. Um administrador executava um programa como o `MAKEDEV` ou editava `/dev` diretamente, e o sistema de arquivos continha nós de dispositivos independentemente de o hardware correspondente estar presente ou não. A abordagem estática tinha dois problemas bem conhecidos. Primeiro, o administrador precisava saber antecipadamente quais dispositivos eram possíveis e criar os nós correspondentes manualmente, com os números major e minor corretos. Segundo, o sistema de arquivos continha nós órfãos para hardware que o sistema não possuía de fato, o que era confuso.

O `devfs` do FreeBSD, introduzido como padrão nas primeiras versões da série 5.x, substituiu o esquema estático por um dinâmico. O próprio kernel decide quais nós de dispositivos existem, com base em quais drivers chamaram `make_dev`. Quando o driver chama `make_dev`, o nó aparece em `/dev`. Quando o driver chama `destroy_dev`, o nó desaparece. O administrador não precisa mais manter entradas de dispositivos manualmente, e não há nós órfãos para hardware que não está presente.

A troca que o devfs introduz é que o ciclo de vida de um nó `/dev` agora é controlado pelo kernel e não pelo sistema de arquivos. Um driver que falha em remover seu nó ao ser desanexado o deixa visível até que o próprio kernel o remova (o que eventualmente acontecerá, mas não tão imediatamente quanto o driver faria). Um driver que cria acidentalmente o mesmo nó duas vezes provoca um panic pela verificação de nome duplicado. Um driver que cria um nó a partir do contexto errado pode causar deadlock no kernel. O capítulo ensina os padrões que evitam cada um desses problemas.

Um detalhe útil para entender o devfs é que o kernel mantém um namespace global único de nós de dispositivos, e `make_dev` registra nesse namespace. O administrador pode montar instâncias adicionais de devfs dentro de jails ou chroots; cada instância projeta uma visão filtrada do namespace global, controlada por meio de `devfs.rules(8)`. O próprio driver não precisa saber sobre essas projeções. Ele simplesmente registra seu `cdev` uma vez, e o kernel e o sistema de regras juntos decidem quais visões podem vê-lo.

### O Ciclo de Vida de um `cdev`

Todo `cdev` passa por cinco estágios. Conhecer esses estágios pelo nome facilita o acompanhamento do restante da seção.

O primeiro estágio é o **registro**. O driver chama `make_dev` (ou uma de suas variantes) a partir de `device_attach` (ou de um handler de eventos de módulo, dependendo se o dispositivo está ligado ao barramento ou é um pseudo-dispositivo). A chamada retorna um `struct cdev *` que o driver armazena em seu softc. A partir desse momento, o nó está visível em `/dev`.

O segundo estágio é o **uso**. Programas do espaço do usuário podem abrir o nó e chamar read, write, ioctl, mmap, poll ou kqueue contra ele. Cada chamada passa pelo callback cdevsw correspondente que o driver instalou no momento do registro. Um `cdev` pode ter muitos descritores de arquivo abertos contra ele a qualquer momento, e os callbacks do driver devem ser seguros para chamadas concorrentes entre si e com o próprio trabalho interno do driver (interrupções, callouts, taskqueues).

O terceiro estágio é a **solicitação de destruição**. O driver chama `destroy_dev` (ou `destroy_dev_sched` para a variante assíncrona). O nó é desvinculado de `/dev` imediatamente, de modo que nenhuma nova abertura pode ser bem-sucedida. As aberturas existentes não são fechadas nesse momento.

O quarto estágio é a **drenagem** (drain). `destroy_dev` bloqueia até que todo descritor de arquivo aberto para o cdev tenha passado por `d_close` e toda chamada em andamento no driver tenha retornado. Os callbacks do driver têm a garantia de não serem mais chamados após o retorno de `destroy_dev`.

O quinto estágio é a **liberação**. Após o retorno de `destroy_dev`, o driver pode liberar o softc, liberar quaisquer recursos que os callbacks do cdev estavam usando e descarregar o módulo. O próprio `struct cdev *` é de propriedade do kernel e é liberado pelo kernel quando sua última referência desaparece; o driver não o libera.

O estágio de drenagem é o que pega os drivers de primeira viagem com mais frequência. Um driver ingênuo faz o equivalente a "destroy_dev; free(sc);" e então um descritor de arquivo aberto mantido chama o cdevsw e desreferencia o softc já liberado, o que causa um panic. O capítulo ensina como lidar com isso corretamente: coloque a chamada a destroy_dev antes de qualquer liberação de estado no caminho de detach, e confie no kernel para drenar as chamadas em andamento antes que o destroy retorne.

### A Família `make_dev`

O FreeBSD fornece diversas variantes de `make_dev`, cada uma com uma combinação diferente de opções. Elas estão definidas em `/usr/src/sys/sys/conf.h` e `/usr/src/sys/fs/devfs/devfs_devs.c`. O capítulo apresenta as quatro mais úteis.

A forma mais simples é o próprio **`make_dev`**:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
    int perms, const char *fmt, ...);
```

Essa chamada cria um nó de propriedade de `uid:gid` com permissões `perms`, nomeado de acordo com o formato no estilo `printf`. Ela usa `M_WAITOK` internamente e pode dormir, portanto deve ser chamada a partir de um contexto que possa dormir (tipicamente `device_attach` ou um handler de carregamento de módulo, nunca um handler de interrupção). Ela não pode falhar: se não puder alocar memória, dorme até conseguir. O leitor vem usando essa forma desde os capítulos iniciais.

A forma mais rica é **`make_dev_credf`**:

```c
struct cdev *
make_dev_credf(int flags, struct cdevsw *devsw, int unit,
    struct ucred *cr, uid_t uid, gid_t gid, int mode,
    const char *fmt, ...);
```

Essa variante recebe um argumento `flags` explícito e uma credencial explícita. A credencial é usada pelo framework MAC ao verificar se o dispositivo pode ser criado com o proprietário especificado. As flags selecionam recursos como `MAKEDEV_ETERNAL` (o kernel nunca destrói esse nó automaticamente) e `MAKEDEV_ETERNAL_KLD` (o mesmo, mas permitido apenas dentro de um módulo carregável). O driver `null(4)` usa essa forma, conforme citado na lista de referências da Seção 1.

A forma recomendada para novos drivers é **`make_dev_s`**:

```c
int
make_dev_s(struct make_dev_args *args, struct cdev **cdev,
    const char *fmt, ...);
```

Essa variante recebe uma estrutura de argumentos em vez de uma longa lista de argumentos. A estrutura é inicializada com `make_dev_args_init(&args)` antes de seus campos serem preenchidos. A vantagem de `make_dev_s` é que ela pode falhar em vez de dormir, e a falha é reportada por meio de um valor de retorno em vez de um sono. Ela também tem um parâmetro de saída para o `cdev *`, o que significa que o chamador não precisa se lembrar qual retorno posicional representa o quê. Novo código deve preferir `make_dev_s` porque o caminho de falha é mais limpo.

A estrutura de argumentos tem a seguinte aparência:

```c
struct make_dev_args {
    size_t        mda_size;
    int           mda_flags;
    struct cdevsw *mda_devsw;
    struct ucred  *mda_cr;
    uid_t         mda_uid;
    gid_t         mda_gid;
    int           mda_mode;
    int           mda_unit;
    void          *mda_si_drv1;
    void          *mda_si_drv2;
};
```

O campo `mda_size` é usado pelo kernel para detectar incompatibilidades de ABI; `make_dev_args_init` o define corretamente. Os campos `mda_si_drv1` e `mda_si_drv2` permitem que o driver anexe dois ponteiros próprios ao cdev no momento da criação; o capítulo usa `mda_si_drv1` para anexar um ponteiro ao softc.

Os bits de flag `MAKEDEV_*` relevantes para a maioria dos drivers são:

| Flag                    | Significado                                                                    |
|-------------------------|--------------------------------------------------------------------------------|
| `MAKEDEV_REF`           | O cdev retornado está referenciado; equilibre com `dev_rel`.                   |
| `MAKEDEV_NOWAIT`        | Não dormir; retornar falha se a chamada tivesse que dormir.                    |
| `MAKEDEV_WAITOK`        | A chamada pode dormir (padrão para `make_dev`).                                |
| `MAKEDEV_ETERNAL`       | O kernel não destrói esse nó automaticamente.                                  |
| `MAKEDEV_ETERNAL_KLD`   | Igual a ETERNAL, mas permitido dentro de um módulo carregável.                 |
| `MAKEDEV_CHECKNAME`     | Valida o nome em relação ao conjunto de caracteres do devfs.                   |

A variante `make_dev_p` é semelhante a `make_dev_s`, mas recebe uma lista de argumentos posicionais. É mais antiga, ainda suportada e utilizada por alguns drivers na árvore; novos drivers podem ignorá-la em favor de `make_dev_s`.

### A Estrutura `cdevsw`

A tabela `cdevsw` é a tabela de despacho para os callbacks de dispositivos de caracteres. O leitor já instalou uma nas versões anteriores do driver, mas esta seção examina cada campo individualmente.

Um cdevsw mínimo moderno tem a seguinte forma:

```c
static struct cdevsw myfirst_cdevsw = {
    .d_version = D_VERSION,
    .d_flags   = D_TRACKCLOSE,
    .d_name    = "myfirst",
    .d_open    = myfirst_open,
    .d_close   = myfirst_close,
    .d_read    = myfirst_read,
    .d_write   = myfirst_write,
    .d_ioctl   = myfirst_ioctl,
};
```

`d_version` deve ser definido como `D_VERSION`. O kernel usa esse campo para detectar drivers construídos contra uma versão mais antiga do layout do cdevsw. Um `d_version` ausente ou incorreto é uma fonte comum de falhas confusas ao carregar módulos; sempre defina-o explicitamente.

`d_flags` controla um pequeno conjunto de comportamentos opcionais. Os flags mais comuns são:

| Flag             | Significado                                                                                       |
|------------------|---------------------------------------------------------------------------------------------------|
| `D_TRACKCLOSE`   | Chama `d_close` no último fechamento de cada fd, e não em todo fechamento.                        |
| `D_NEEDGIANT`    | Adquire o lock Giant global do kernel em torno do despacho (raro no código moderno).              |
| `D_NEEDMINOR`    | Aloca um número de minor (legado; raramente necessário hoje).                                     |
| `D_MMAP_ANON`    | O driver suporta `mmap` anônimo através do `dev_pager`.                                           |
| `D_DISK`         | O cdev é o ponto de entrada para um dispositivo semelhante a um disco.                            |
| `D_TTY`          | O cdev é um dispositivo terminal; afeta o roteamento da disciplina de linha.                      |

Para o driver `myfirst`, `D_TRACKCLOSE` é o único flag que vale a pena definir. Ele faz com que o kernel chame `d_close` exatamente uma vez por descritor de arquivo, no último fechamento desse descritor, em vez de em todo fechamento. Sem `D_TRACKCLOSE`, um driver que deseja contar os descritores de arquivo abertos precisa lidar com o mesmo `d_close` sendo chamado várias vezes, o que é inconveniente.

`d_name` é o nome que o kernel usa em algumas mensagens de diagnóstico. Por convenção, é o mesmo que o nome do driver.

Os campos de callback são ponteiros para as funções que implementam cada operação. Um driver só precisa instalar os callbacks que realmente suporta; callbacks ausentes assumem stubs seguros que retornam `ENODEV` ou `EOPNOTSUPP`. A combinação mais comum para um dispositivo de caracteres é `d_open`, `d_close`, `d_read`, `d_write` e `d_ioctl`. Drivers que suportam polling adicionam `d_poll`. Drivers que suportam kqueue adicionam `d_kqfilter`. Drivers que mapeiam memória no espaço do usuário adicionam `d_mmap` ou `d_mmap_single`. Drivers que emulam um disco adicionam `d_strategy`.

O callback `d_purge` é raro, mas vale a pena conhecer. O kernel o chama quando o cdev está sendo destruído e o driver deve liberar qualquer I/O pendente. A maioria dos drivers não precisa dele porque o `d_close` já cuida dessa liberação.

A assinatura do callback `d_open` é:

```c
int myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td);
```

`dev` é o cdev sendo aberto. `oflags` é a união dos flags de `open(2)` (`O_RDWR`, `O_NONBLOCK`, etc.). `devtype` carrega o tipo do dispositivo e raramente é útil para um dispositivo de caracteres. `td` é a thread que está realizando o open. O callback retorna `0` em caso de sucesso ou um errno em caso de falha. Um padrão típico armazena o ponteiro softc encontrado em `dev->si_drv1` em uma estrutura privada por abertura que chamadas subsequentes podem recuperar.

A assinatura de `d_close` é paralela:

```c
int myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td);
```

As assinaturas de `d_read` e `d_write` usam a maquinaria `uio` que o leitor conheceu no Capítulo 8:

```c
int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
```

A assinatura de `d_ioctl` é:

```c
int myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td);
```

`cmd` é a palavra de comando do ioctl. `data` aponta para o buffer de dados do ioctl (que o kernel já copiou para comandos `IOC_IN` e copiará de volta para comandos `IOC_OUT`, como a Seção 3 discutirá em detalhes). `fflag` são os flags do arquivo provenientes da chamada de abertura. `td` é a thread chamadora.

### Estado Por Cdev Através de `si_drv1`

O cdev é o identificador do kernel para o dispositivo, e o driver quase sempre precisa de uma forma de encontrar seu próprio softc a partir de um ponteiro de cdev. O mecanismo padrão é `cdev->si_drv1`. O driver define esse campo quando cria o cdev (seja na chamada a `make_dev` ou escrevendo no campo posteriormente) e o lê em cada callback do cdevsw.

O padrão tem esta forma no attach:

```c
sc->sc_cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev),
    UID_ROOT, GID_WHEEL, 0660, "myfirst%d", device_get_unit(dev));
sc->sc_cdev->si_drv1 = sc;
```

E esta forma em cada callback:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d flags=%#x\n",
        td->td_proc->p_pid, oflags);
    /* ... rest of open ... */
    return (0);
}
```

`si_drv2` é um segundo ponteiro que o driver pode usar como quiser. Alguns drivers o utilizam para um cookie por instância; outros não o utilizam e o deixam como `NULL`. O driver `myfirst` usa apenas `si_drv1`.

A variante `make_dev_s` é mais limpa porque define `si_drv1` no momento da criação:

```c
struct make_dev_args args;
make_dev_args_init(&args);
args.mda_devsw = &myfirst_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_WHEEL;
args.mda_mode = 0660;
args.mda_si_drv1 = sc;
args.mda_unit = device_get_unit(dev);
error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d", device_get_unit(dev));
if (error != 0) {
    device_printf(dev, "make_dev_s failed: %d\n", error);
    goto fail;
}
```

A forma `make_dev_s` tem a vantagem adicional de que `si_drv1` é definido antes que o cdev fique visível em `/dev`, o que fecha uma pequena mas real janela de condição de corrida na qual um programa rápido ao abrir poderia chamar um cdevsw cujo `si_drv1` ainda era `NULL`. Novos drivers devem preferir esta forma.

### A Referência `null(4)`

O exemplo mais limpo e compacto de cdevsw e `make_dev_credf` na árvore de código-fonte é `/usr/src/sys/dev/null/null.c`. Os trechos relevantes são curtos o suficiente para serem lidos de uma vez. As declarações do cdevsw (uma separada para `/dev/null` e outra para `/dev/zero`):

```c
static struct cdevsw null_cdevsw = {
    .d_version = D_VERSION,
    .d_read    = (d_read_t *)nullop,
    .d_write   = null_write,
    .d_ioctl   = null_ioctl,
    .d_name    = "null",
};
```

O manipulador de eventos do módulo que cria e destrói os nós:

```c
static int
null_modevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
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

DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

Alguns detalhes merecem atenção. O flag `MAKEDEV_ETERNAL_KLD` informa ao kernel que, mesmo que o módulo seja descarregado em circunstâncias incomuns, o cdev não deve ser silenciosamente invalidado. O modo `0666` significa que qualquer pessoa pode ler e escrever, o que é correto para `/dev/null`. O número de unit é `0` porque existe sempre apenas um de cada. O ramo `MOD_LOAD` é executado no carregamento do módulo e cria os nós; o ramo `MOD_UNLOAD` é executado no descarregamento do módulo e os destrói; `MOD_SHUTDOWN` é executado durante a sequência de encerramento do sistema e não faz nada aqui, pois nenhum trabalho de encerramento é necessário para esses pseudo-dispositivos.

Essa é a forma canônica de um pseudo-dispositivo que vive no escopo do módulo em vez de no escopo da árvore de dispositivos. O driver `myfirst`, em contraste, cria seu cdev em `device_attach` porque o tempo de vida do cdev está vinculado ao tempo de vida de um dispositivo PCI específico, e não ao tempo de vida do módulo. Os dois padrões são diferentes, mas coexistem confortavelmente.

### Múltiplos Nós e Nós Clonáveis

Um único driver pode criar mais de um nó. Três padrões são comuns.

O primeiro é o de **nós com nomes fixos**. O driver sabe de antemão quantos nós precisa e os cria com nomes fixos: `/dev/myfirst0`, `/dev/myfirst-status`, `/dev/myfirst-config`. Esse é o padrão correto quando cada nó tem uma função diferente e o número deles é conhecido.

O segundo é o de **nós indexados**. O driver cria um nó por unit, com o nome `/dev/myfirstN` onde `N` é o número de unit. Esse é o padrão correto quando cada nó representa uma instância separada do mesmo tipo de objeto, como uma por placa PCI conectada.

O terceiro é o de **nós clonáveis**. O driver registra um manipulador de clonagem que cria um novo nó sob demanda sempre que um usuário abre um nome que corresponde a um padrão. O leitor de `tun(4)` abre `/dev/tun` e obtém `/dev/tun0`; abrir `/dev/tun` novamente resulta em `/dev/tun1`; o kernel aloca o próximo unit livre a cada abertura. Esse é o padrão correto para pseudo-dispositivos que o usuário deseja "criar" simplesmente abrindo-os.

O mecanismo de clonagem é o manipulador de eventos `dev_clone`. Ele está definido em `/usr/src/sys/sys/conf.h`:

```c
typedef void (*dev_clone_fn)(void *arg, struct ucred *cred, char *name,
    int namelen, struct cdev **result);

EVENTHANDLER_DECLARE(dev_clone, dev_clone_fn);
```

O driver registra um manipulador com `EVENTHANDLER_REGISTER`, e o kernel o chama sempre que um caminho de abertura em `/dev` não corresponde a um nó existente. O manipulador decide se o nome pertence ao seu driver, aloca um novo unit se for o caso, chama `make_dev` para criar o nó com esse nome e armazena o ponteiro do cdev resultante pelo argumento `result`. O kernel então reabre o nó recém-criado e continua com a chamada de abertura do usuário.

O driver `tun(4)` mostra esse padrão. Em `/usr/src/sys/net/if_tuntap.c`:

```c
static eventhandler_tag clone_tag;

static int
tuntapmodevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
        clone_tag = EVENTHANDLER_REGISTER(dev_clone, tunclone, 0, 1000);
        if (clone_tag == NULL)
            return (ENOMEM);
        ...
        break;
    case MOD_UNLOAD:
        EVENTHANDLER_DEREGISTER(dev_clone, clone_tag);
        ...
    }
}

static void
tunclone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
    /* If *dev != NULL, another handler already created the cdev. */
    if (*dev != NULL)
        return;

    /* Examine name; if it matches our pattern, allocate a unit */
    /* and call make_dev to populate *dev. */
    ...
}
```

Algumas regras se aplicam aos manipuladores de clonagem. O manipulador não deve assumir que é o único registrado; múltiplos subsistemas podem se registrar em `dev_clone`, e cada manipulador deve verificar se `*dev` já é não-NULL antes de fazer qualquer trabalho. O manipulador é executado em um contexto que pode dormir, portanto pode chamar `make_dev` diretamente. O manipulador deve validar o nome com cuidado, pois ele é fornecido pelo espaço do usuário.

O driver `myfirst` usa inicialmente um padrão de nós indexados (um nó por dispositivo PCI conectado, `/dev/myfirst0`, `/dev/myfirst1`, etc.), e o laboratório da Seção 2 demonstra como adicionar um manipulador de clonagem para que o usuário possa abrir `/dev/myfirst-clone` e obter um novo unit sob demanda. O padrão de clonagem é mais útil para pseudo-dispositivos sem hardware subjacente; para drivers que dependem de hardware, raramente é necessário.

### Permissões, Propriedade e `devfs.rules`

`make_dev` recebe o UID do proprietário inicial, o GID do grupo e as permissões do nó. Esses valores são definidos no momento da criação. Eles são visíveis no espaço do usuário através de `ls -l /dev/myfirst0` e determinam quais programas podem abrir o nó.

Para um driver com suporte em hardware, os padrões corretos dependem da função. Um dispositivo que deve ser acessível apenas ao root usa `UID_ROOT, GID_WHEEL, 0600`. Um dispositivo que qualquer usuário administrativo deve poder acessar usa `UID_ROOT, GID_OPERATOR, 0660`. Um dispositivo que qualquer usuário pode ler, mas apenas root pode escrever, usa `UID_ROOT, GID_WHEEL, 0644`. O driver `myfirst` usa `UID_ROOT, GID_WHEEL, 0660` por padrão; espera-se que o usuário seja root ou que receba acesso via `devfs.rules(8)` se precisar dele.

O administrador pode substituir esses padrões em tempo de execução através de `devfs.rules(8)`. Um arquivo de regras típico tem esta forma:

```text
[localrules=10]
add path 'myfirst*' mode 0660
add path 'myfirst*' group operator
```

O administrador ativa as regras adicionando ao `/etc/rc.conf`:

```text
devfs_system_ruleset="localrules"
```

Após `service devfs restart` (ou uma reinicialização), as regras se aplicam aos novos nós de dispositivo que aparecem. Esse mecanismo permite que o administrador conceda acesso ao driver sem recompilar o módulo, o que representa a divisão correta de responsabilidades: o desenvolvedor escolhe padrões seguros, e o administrador os relaxa quando necessário.

Um erro comum é tornar um dispositivo acessível a todos por padrão porque "facilita o teste". Um driver distribuído com permissões `0666` para um dispositivo que controla hardware é um problema de segurança. A recomendação deste capítulo é usar `0660` com `GID_WHEEL` como padrão e instruir os leitores em `INTEGRATION.md` sobre como usar `devfs.rules(8)` para alterá-lo se necessário.

### Reunindo Tudo: O Driver Stage 1

O driver Stage 1 do Capítulo 24 substitui a criação ad-hoc original do nó pelo padrão moderno `make_dev_s`, define `si_drv1` no momento da criação, usa o flag `D_TRACKCLOSE` e prepara o terreno para os callbacks de ioctl que serão adicionados na Seção 3. A seguir, o trecho relevante da nova função de attach. Digite-o manualmente; o ponto central do capítulo é exatamente a transição cuidadosa da forma ad-hoc antiga para a nova forma disciplinada.

```c
static int
myfirst_attach(device_t dev)
{
    struct myfirst_softc *sc;
    struct make_dev_args args;
    int error;

    sc = device_get_softc(dev);
    sc->sc_dev = dev;

    /* ... earlier attach work: PCI resources, MSI-X, DMA, sysctl tree
     * stub, debug subtree.  See Chapters 18-23 for these.  ... */

    /* Build the cdev for /dev/myfirstN. */
    make_dev_args_init(&args);
    args.mda_devsw = &myfirst_cdevsw;
    args.mda_uid = UID_ROOT;
    args.mda_gid = GID_WHEEL;
    args.mda_mode = 0660;
    args.mda_si_drv1 = sc;
    args.mda_unit = device_get_unit(dev);
    error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
        device_get_unit(dev));
    if (error != 0) {
        device_printf(dev, "make_dev_s failed: %d\n", error);
        DPRINTF(sc, MYF_DBG_INIT, "cdev creation failed (%d)\n", error);
        goto fail;
    }
    DPRINTF(sc, MYF_DBG_INIT, "cdev created at /dev/myfirst%d\n",
        device_get_unit(dev));

    /* ... rest of attach: register callouts, finalise sysctl OIDs, etc. */

    return (0);

fail:
    /* Unwind earlier resources in reverse order. */
    /* See Section 7 for the discipline that goes here. */
    return (error);
}
```

O cdevsw correspondente com `D_TRACKCLOSE`:

```c
static d_open_t myfirst_open;
static d_close_t myfirst_close;
static d_read_t myfirst_read;
static d_write_t myfirst_write;
static d_ioctl_t myfirst_ioctl;

static struct cdevsw myfirst_cdevsw = {
    .d_version = D_VERSION,
    .d_flags   = D_TRACKCLOSE,
    .d_name    = "myfirst",
    .d_open    = myfirst_open,
    .d_close   = myfirst_close,
    .d_read    = myfirst_read,
    .d_write   = myfirst_write,
    .d_ioctl   = myfirst_ioctl,
};
```

O open e o close correspondentes, com as consultas a `si_drv1` e as atualizações do contador por abertura:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    SDT_PROBE2(myfirst, , , open, sc, oflags);

    mtx_lock(&sc->sc_mtx);
    sc->sc_open_count++;
    mtx_unlock(&sc->sc_mtx);

    DPRINTF(sc, MYF_DBG_OPEN,
        "open: pid=%d flags=%#x open_count=%u\n",
        td->td_proc->p_pid, oflags, sc->sc_open_count);
    return (0);
}

static int
myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    SDT_PROBE2(myfirst, , , close, sc, fflags);

    mtx_lock(&sc->sc_mtx);
    KASSERT(sc->sc_open_count > 0,
        ("myfirst_close: open_count underflow"));
    sc->sc_open_count--;
    mtx_unlock(&sc->sc_mtx);

    DPRINTF(sc, MYF_DBG_OPEN,
        "close: pid=%d flags=%#x open_count=%u\n",
        td->td_proc->p_pid, fflags, sc->sc_open_count);
    return (0);
}
```

E a atualização correspondente no detach, com `destroy_dev` antes de qualquer liberação do estado do softc:

```c
static int
myfirst_detach(device_t dev)
{
    struct myfirst_softc *sc = device_get_softc(dev);

    /* Refuse detach while users still hold the device open. */
    mtx_lock(&sc->sc_mtx);
    if (sc->sc_open_count > 0) {
        mtx_unlock(&sc->sc_mtx);
        device_printf(dev, "detach refused: %u open(s) outstanding\n",
            sc->sc_open_count);
        return (EBUSY);
    }
    mtx_unlock(&sc->sc_mtx);

    /* Destroy the cdev first.  The kernel drains any in-flight
     * callbacks before destroy_dev returns. */
    if (sc->sc_cdev != NULL) {
        destroy_dev(sc->sc_cdev);
        sc->sc_cdev = NULL;
        DPRINTF(sc, MYF_DBG_INIT, "cdev destroyed\n");
    }

    /* ... rest of detach: tear down DMA, MSI-X, callouts, sysctl ctx,
     * etc., in reverse order of attach.  See Section 7. */

    return (0);
}
```

Este é o marco do Stage 1. O driver agora possui uma entrada devfs limpa, moderna e acessível para iniciantes. Os próximos estágios adicionarão ioctls e sysctls sobre essa base.

### Um Walkthrough Concreto: Carregando e Inspecionando o Driver Stage 1

Construa, carregue e inspecione o driver Stage 1:

```sh
cd ~/myfirst-1.7-integration/stage1-devfs
make
sudo kldload ./myfirst.ko

# Confirm the device exists and has the expected attributes.
ls -l /dev/myfirst0

# Read its sysctl debug subtree (still from Chapter 23).
sysctl dev.myfirst.0

# Open it with cat to confirm the cdevsw read and close paths fire.
sudo cat /dev/myfirst0 > /dev/null
dmesg | tail -20
```

A saída esperada para `ls -l /dev/myfirst0`:

```text
crw-rw----  1 root  wheel  0x71 Apr 19 16:30 /dev/myfirst0
```

O `crw-rw----` indica dispositivo de caracteres, leitura e escrita para o proprietário, leitura e escrita para o grupo, sem permissão para outros. O proprietário é root. O grupo é wheel. O número de minor é `0x71` (alocado pelo kernel; o valor varia entre sistemas). O tamanho é o número de unit com o qual `make_dev_s` foi chamado.

A saída esperada para o trecho do `dmesg`:

```text
myfirst0: cdev created at /dev/myfirst0
myfirst0: open: pid=4321 flags=0x1 open_count=1
myfirst0: close: pid=4321 flags=0x1 open_count=0
```

(Supondo que o debug mask tenha `MYF_DBG_INIT` e `MYF_DBG_OPEN` ativados. Se a máscara for zero, as linhas ficam silenciosas; ative-as com `sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF` conforme a disciplina de debug do Capítulo 23.)

Se as linhas de open e close não aparecerem, verifique o debug mask. Se a linha de open aparecer mas não a de close, você esqueceu `D_TRACKCLOSE` e o kernel está chamando close a cada fechamento de fd em vez de somente no último; ative `D_TRACKCLOSE` ou espere múltiplas linhas de close por open. Se o contador de open ficar negativo, você tem um bug real: um close foi chamado sem um open correspondente.

### Erros Comuns com devfs

Cinco erros explicam a maioria dos problemas com devfs que desenvolvedores encontram ao integrar um driver pela primeira vez. Nomeá-los desde o início poupa uma sessão de depuração no futuro.

O primeiro erro é **chamar `make_dev` a partir de um contexto que não permite sleep**. `make_dev` pode dormir. Se for chamada a partir de um handler de interrupção, de um callout, de dentro de um spinlock, ou de dentro de qualquer contexto em que o sleep seja proibido, o kernel entrará em panic com `WITNESS` ou `INVARIANTS` reclamando de uma função que pode dormir sendo usada em um contexto que não o permite. A correção é chamar `make_dev` a partir de `device_attach` ou de um handler de evento de módulo (ambos contextos seguros), ou usar `make_dev_s` com `MAKEDEV_NOWAIT` (que pode falhar, e o chamador deve tratar a falha).

O segundo erro é **esquecer de definir `si_drv1`**. Os callbacks do cdevsw então desreferenciam um ponteiro NULL e causam panic no kernel. A correção é definir `si_drv1` imediatamente após `make_dev` (ou, melhor ainda, usar `make_dev_s` e definir `mda_si_drv1` nos argumentos, o que fecha a janela de condição de corrida entre a criação e a atribuição).

O terceiro erro é **chamar `destroy_dev` após liberar o softc**. Os callbacks do cdevsw podem ainda estar em execução quando `destroy_dev` é chamada; o kernel os drena antes de `destroy_dev` retornar. Se o softc já foi liberado, os callbacks desreferenciam memória inválida. A correção é chamar `destroy_dev` primeiro e depois liberar o softc, nessa ordem estrita.

O quarto erro é **criar dois cdevs com o mesmo nome**. O kernel verifica nomes duplicados e a segunda chamada a `make_dev` causa panic ou retorna um erro, dependendo da variante. A correção é compor o nome a partir do número de unidade, ou usar um clone handler.

O quinto erro é **não tratar o caso em que o dispositivo está aberto durante o detach**. Um driver ingênuo simplesmente chama `destroy_dev` e libera o softc, o que funciona apenas se nenhum usuário tiver o dispositivo aberto. Um usuário que mantiver `/dev/myfirst0` aberto com `cat` derrota essa abordagem. A correção é recusar o detach quando `open_count > 0`, ou usar o mecanismo `dev_ref`/`dev_rel` do kernel para coordenar. O padrão deste capítulo é a recusa simples (`return (EBUSY)`) porque ela fornece o erro mais claro para o usuário.

### Armadilhas Específicas de Drivers com Múltiplas Instâncias

Drivers que criam mais de um cdev por dispositivo anexado, ou um cdev por canal dentro de um dispositivo multicanal, encontram algumas armadilhas adicionais.

A primeira é **vazar nós em caso de falha parcial**. Se o driver cria três cdevs e a terceira chamada falha, ele deve destruir os dois primeiros antes de retornar. O padrão de limpeza em caso de falha da Seção 7 é a solução canônica. O laboratório deste capítulo nesta seção percorre o padrão com uma injeção deliberada de falha.

A segunda é **esquecer que cada cdev precisa do seu próprio `si_drv1`**. Um driver que cria nós por canal normalmente quer que `si_drv1` aponte para o canal em vez de para o softc. Os callbacks do cdevsw então navegam do canal para o softc conforme necessário. Misturar os dois faz com que os canais corrompam o estado um do outro.

A terceira é **visibilidade de nó livre de condições de corrida**. Entre o retorno de `make_dev` (ou `make_dev_s`) e o driver concluindo o restante do attach, um usuário rápido já pode abrir o nó. O driver deve estar preparado para tratar opens que chegam antes que o attach esteja completamente concluído. O padrão mais simples é adiar `make_dev` para o final do attach, de modo que o nó só seja visível depois que todo o restante do estado estiver pronto. O driver `myfirst` segue esse padrão.

Essas armadilhas não surgem com frequência em drivers com cdev único como `myfirst`, mas reconhecê-las agora significa que você não será surpreendido quando aparecerem no Capítulo 27 (drivers de armazenamento podem ter muitos cdevs, um por LUN) ou no Capítulo 28 (drivers de rede podem ter muitos cdevs, um por canal de comando).

### Encerrando a Seção 2

A Seção 2 tornou a presença do driver em `/dev` de primeira classe. O cdev agora é criado com o padrão moderno `make_dev_s`, o cdevsw está completamente populado com `D_TRACKCLOSE` e um conjunto de callbacks amigável para depuração, o estado por cdev está conectado via `si_drv1`, e o caminho de detach drena e destrói o cdev de forma limpa. O driver ainda realiza o mesmo trabalho que `1.6-debug`, mas agora expõe esse trabalho por meio de um nó `/dev` corretamente construído que um administrador pode configurar com chmod e chown, monitorar com `ls` e raciocinar a partir de fora do kernel.

Na próxima seção, voltamos nossa atenção para a segunda superfície de integração: a interface de controle acionada pelo usuário que permite a um programa dizer ao driver o que fazer. O vocabulário é `ioctl(2)`, `_IO`, `_IOR`, `_IOW`, `_IOWR` e um pequeno header público que programas no espaço do usuário incluem.

## Seção 3: Implementando Suporte a `ioctl()`

### O Que é `ioctl(2)` e Por Que Drivers Precisam Dele

As chamadas de sistema `read(2)` e `write(2)` são excelentes para mover fluxos de bytes entre um programa do usuário e um driver. No entanto, elas não são adequadas para controle. Um `read` não pode perguntar ao driver "qual é o seu estado atual?" sem sobrecarregar o significado dos bytes retornados. Um `write` não pode pedir ao driver "por favor, redefina suas estatísticas" sem inventar um vocabulário de comandos privado dentro do fluxo de bytes. A chamada de sistema que preenche essa lacuna é `ioctl(2)`, a chamada de controle de entrada/saída.

`ioctl` é um canal lateral para comandos. A assinatura no espaço do usuário é direta: `int ioctl(int fd, unsigned long request, ...);`. O primeiro argumento é um descritor de arquivo (no nosso caso, um `/dev/myfirst0` aberto). O segundo é um código de requisição numérico que diz ao driver o que fazer. O terceiro é um ponteiro opcional para uma estrutura que carrega os parâmetros da requisição, seja de entrada, de saída, ou ambos. O kernel roteia a chamada para o cdevsw do cdev que suporta esse descritor de arquivo, para a função apontada por `d_ioctl`. O driver examina o código de requisição, executa a ação correspondente e retorna 0 em caso de sucesso ou um `errno` positivo em caso de falha.

Quase todo driver que expõe uma interface de controle para o espaço do usuário usa `ioctl` para isso. Drivers de disco usam `ioctl` para informar tamanho de setor, tabelas de partição e destinos de dump. Drivers de fita usam `ioctl` para comandos de rebobinagem, ejeção e tensão. Drivers de rede usam `ioctl` para mudanças de mídia (`SIOCSIFFLAGS`), atualizações de endereço MAC e comandos de probe de barramento. Drivers de som usam `ioctl` para negociação de taxa de amostragem, número de canais e tamanho de buffer. O vocabulário é tão universal que aprendê-lo uma vez desbloqueia todas as categorias de driver na árvore.

Para o driver `myfirst`, o `ioctl` nos permite adicionar comandos que não têm expressão limpa como bytes. Podemos permitir que o operador consulte o comprimento da mensagem na memória sem ter que lê-la. Podemos permitir que o operador redefina a mensagem e os contadores de abertura sem ter que escrever um sentinela especial. Podemos expor o número de versão do driver para que ferramentas no espaço do usuário possam detectar a API com a qual estão se comunicando. Cada um desses é uma mudança de uma linha para o operador e uma mudança de meia página para o driver, e cada um é um caso de livro didático para `ioctl`.

Esta seção percorre todo o pipeline de ioctl: a codificação de códigos de requisição, o `copyin` e `copyout` automáticos do kernel, o design de um header público, a implementação do dispatcher, a construção de um pequeno programa complementar no espaço do usuário e as armadilhas mais comuns. Ao final da seção, o driver estará na versão `1.7-integration-stage-2` e suportará quatro comandos ioctl: `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` e `MYFIRSTIOC_RESET`.

### Como os Números de `ioctl` São Codificados

Um código de requisição de ioctl não é um inteiro arbitrário. É um valor de 32 bits compactado que codifica quatro informações em campos de bits fixos, definidos em `/usr/src/sys/sys/ioccom.h`. O header começa com um comentário mostrando o layout, que vale a pena ler antes de prosseguir.

```c
/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 *
 *       31 29 28                     16 15            8 7             0
 *      +---------------------------------------------------------------+
 *      | I/O | Parameter Length        | Command Group | Command       |
 *      +---------------------------------------------------------------+
 */
```

Os quatro campos são:

Os **bits de direção** (bits 29 a 31) dizem ao kernel se o terceiro argumento de `ioctl` é puramente de saída (`IOC_OUT`, o kernel copiará o resultado de volta para o espaço do usuário), puramente de entrada (`IOC_IN`, o kernel copiará os dados do usuário para o kernel antes que o dispatcher execute), bidirecional (`IOC_INOUT`, ambas as direções), ou ausente (`IOC_VOID`, a requisição não recebe argumento de dados). O kernel usa esses bits para decidir quais operações `copyin` e `copyout` realizar automaticamente. O próprio driver nunca precisa chamar `copyin` ou `copyout` para um ioctl corretamente codificado.

O **tamanho do parâmetro** (bits 16 a 28) codifica o tamanho em bytes da estrutura passada como terceiro argumento, com limite máximo de `IOCPARM_MAX = 8192`. O kernel usa esse tamanho para alocar um buffer temporário no kernel, realizar o `copyin` ou `copyout` apropriado e apresentar o buffer ao dispatcher como o argumento `caddr_t data`. Um driver que precisa passar mais de 8192 bytes por um único ioctl deve incorporar um ponteiro em uma estrutura menor (com o custo de fazer seu próprio `copyin`), ou usar um mecanismo diferente como `mmap` ou `read`.

O **grupo de comandos** (bits 8 a 15) é um único caractere que nomeia a família de ioctls relacionados. É convencionalmente uma das letras ASCII imprimíveis e identifica o subsistema. `'d'` é usado pelos ioctls de disco do GEOM (`DIOCGMEDIASIZE`, `DIOCGSECTORSIZE`). `'i'` é usado por `if_ioctl` (`SIOCSIFFLAGS`). `'t'` é usado por ioctls de terminal (`TIOCGPTN`). Você deve escolher uma letra que ainda não esteja em uso por algo com que o driver possa coexistir. Para o driver `myfirst` usaremos `'M'`.

O **número do comando** (bits 0 a 7) é um pequeno inteiro que identifica o ioctl específico dentro do grupo. A numeração geralmente começa em 1 e aumenta monotonicamente conforme os comandos são adicionados. Reutilizar um número é um risco para a compatibilidade retroativa, portanto um driver que aposenta um comando deve deixar o número reservado em vez de reciclá-lo.

As macros em `ioccom.h` constroem essas codificações para você. Elas são a única forma correta de construir números de ioctl:

```c
#define _IO(g,n)        _IOC(IOC_VOID, (g), (n), 0)
#define _IOR(g,n,t)     _IOC(IOC_OUT,  (g), (n), sizeof(t))
#define _IOW(g,n,t)     _IOC(IOC_IN,   (g), (n), sizeof(t))
#define _IOWR(g,n,t)    _IOC(IOC_INOUT,(g), (n), sizeof(t))
```

`_IO` declara um comando que não recebe argumento. `_IOR` declara um comando que retorna um resultado do tamanho de `t` para o espaço do usuário. `_IOW` declara um comando que aceita um argumento do tamanho de `t` do espaço do usuário. `_IOWR` declara um comando que aceita um argumento do tamanho de `t` e escreve um resultado do tamanho de `t` de volta pelo mesmo buffer. O `t` é um tipo, não um ponteiro; as macros usam `sizeof(t)` para calcular o campo de comprimento.

Alguns exemplos do mundo real tornam o padrão concreto. De `/usr/src/sys/sys/disk.h`:

```c
#define DIOCGSECTORSIZE _IOR('d', 128, u_int)
#define DIOCGMEDIASIZE  _IOR('d', 129, off_t)
```

Esses comandos são requisições somente leitura para o tamanho do setor (retornado como `u_int`) e o tamanho da mídia (retornado como `off_t`). A letra de grupo `'d'` e os números 128 e 129 são reservados para o subsistema de disco.

O próprio driver nunca precisa decodificar o layout de bits. O código de comando é opaco para o dispatcher, que o compara com constantes nomeadas:

```c
switch (cmd) {
case MYFIRSTIOC_GETVER:
        ...
        break;
case MYFIRSTIOC_RESET:
        ...
        break;
default:
        return (ENOIOCTL);
}
```

O kernel usa o layout de bits ao configurar a chamada (para alocar o buffer e realizar `copyin`/`copyout`), e o espaço do usuário o utiliza implicitamente por meio das macros. Entre esses dois pontos, o código de requisição é simplesmente um rótulo.

### Escolhendo uma Letra de Grupo

A escolha de uma letra de grupo importa porque conflitos são silenciosos. Dois drivers que escolhem a mesma letra de grupo e o mesmo número de comando verão requisições de ioctl destinadas ao outro driver se o operador confundir os dois. O kernel não impõe unicidade entre drivers, em parte porque nenhuma autoridade central atribui letras e em parte porque a maioria das letras é de fato reservada por tradição, e não por registro formal.

Uma abordagem defensiva é seguir estas convenções:

Use **letras minúsculas** (`'d'`, `'t'`, `'i'`) apenas quando estender um subsistema bem conhecido cuja letra você já conhece. As letras minúsculas são amplamente usadas por drivers de base e é fácil colidir com elas.

Use **letras maiúsculas** (`'M'`, `'X'`, `'Q'`) para drivers novos que precisam de seu próprio namespace de ioctl. Há 26 letras maiúsculas e muito menos colisões na árvore.

Evite **dígitos** por completo. Eles são reservados, por convenção histórica, para subsistemas antigos, e um driver novo que utilize um dígito parecerá fora de lugar para qualquer revisor.

Para o driver `myfirst` usamos `'M'`. É a primeira letra do nome do driver, está em maiúsculo (o que evita colisões com qualquer subsistema base) e torna os códigos de requisição autodocumentados em stack traces e na saída do `ktrace`: um hex dump de um número de `ioctl` com `0x4d` (o valor ASCII de `'M'`) no campo de grupo é inequivocamente um comando `myfirst`.

### A Assinatura do Callback `d_ioctl`

A função apontada por `cdevsw->d_ioctl` tem o tipo `d_ioctl_t`, definido em `/usr/src/sys/sys/conf.h`. A assinatura é:

```c
typedef int d_ioctl_t(struct cdev *dev, u_long cmd, caddr_t data,
                      int fflag, struct thread *td);
```

Os cinco argumentos merecem uma leitura pausada.

`dev` é o cdev que sustenta o file descriptor sobre o qual o usuário chamou `ioctl`. O driver usa `dev->si_drv1` para recuperar seu softc. Esse é o mesmo padrão utilizado por todos os callbacks de cdevsw que já vimos.

`cmd` é o código da requisição, o valor que o usuário passou como segundo argumento para `ioctl`. O driver o compara com as constantes nomeadas no seu cabeçalho público.

`data` é a cópia local do kernel do terceiro argumento. Como o kernel realizou o copyin e realizará o copyout (para requisições `_IOR`, `_IOW` e `_IOWR`), `data` é sempre um ponteiro do kernel. O driver o desreferencia diretamente sem chamar `copyin`. Para requisições `_IO`, `data` é indefinido e não deve ser desreferenciado.

`fflag` são os flags de arquivo provenientes da chamada open: `FREAD`, `FWRITE` ou ambos. O driver pode usar `fflag` para impor acesso somente leitura ou somente escrita a comandos específicos. Um comando que redefine o estado, por exemplo, pode exigir `FWRITE` e retornar `EBADF` caso contrário.

`td` é a thread chamadora. O driver pode usar `td` para extrair as credenciais do chamador (`td->td_ucred`), para realizar verificações de privilégio (`priv_check_cred(td->td_ucred, PRIV_DRIVER, 0)`), ou simplesmente para registrar o pid do chamador. Na maioria dos comandos, `td` não é utilizado.

O valor de retorno é 0 em caso de sucesso, ou um valor positivo de `errno` em caso de falha. O valor especial `ENOIOCTL` (definido em `/usr/src/sys/sys/errno.h`) informa ao kernel que o driver não reconhece o comando, e o kernel então encaminhará o comando ao handler genérico de ioctl da camada do sistema de arquivos. Retornar `EINVAL` em vez de `ENOIOCTL` para um comando desconhecido é um bug sutil: diz ao kernel "reconheci o comando, mas os argumentos estão incorretos", o que suprime o fallback genérico. Use sempre `ENOIOCTL` no caso padrão.

### Como o Kernel Realiza o Copyin e o Copyout

Antes de o dispatcher ser executado, o kernel inspeciona os bits de direção e o comprimento do parâmetro codificados em `cmd`. Se `IOC_IN` estiver definido, o kernel lê o comprimento do parâmetro, aloca um buffer temporário no kernel com esse tamanho, copia o argumento do espaço do usuário para ele (`copyin`) e passa o buffer do kernel como `data`. Se `IOC_OUT` estiver definido, o kernel aloca um buffer, chama o dispatcher com o buffer (não inicializado) como `data` e, em caso de retorno 0, copia o buffer de volta para o espaço do usuário (`copyout`). Se ambos os bits estiverem definidos (`IOC_INOUT`), o kernel realiza o copyin e o copyout em torno da chamada ao dispatcher. Se nenhum estiver definido (`IOC_VOID`), nenhum buffer é alocado e `data` é indefinido.

Essa automação tem duas consequências que valem a pena lembrar.

Primeira: o dispatcher escreve e lê de `data` usando desreferenciamento C normal. O driver nunca chama `copyin` ou `copyout` para um ioctl corretamente codificado. Essa é uma das razões pelas quais uma interface ioctl bem projetada é muito mais simples de implementar do que, por exemplo, um protocolo de escrita e leitura que simula um canal de controle.

Segunda: o tipo do parâmetro codificado em `_IOW` ou `_IOR` deve corresponder ao que o dispatcher efetivamente lê ou escreve. Se o cabeçalho do espaço do usuário declara `_IOR('M', 1, uint32_t)` mas o dispatcher escreve um `uint64_t` em `*(uint64_t *)data`, o dispatcher vai ultrapassar os 4 bytes do buffer do kernel e corromper a memória adjacente na pilha, causando pânico no kernel em um build com `WITNESS` habilitado e corrompendo o estado silenciosamente em um build de produção. O cabeçalho é o contrato; o dispatcher deve honrá-lo byte a byte.

Para ioctls com ponteiros embutidos (uma struct que contém um `char *buf` apontando para um buffer separado), o kernel não consegue realizar o copyin ou o copyout do buffer porque ele não faz parte da estrutura. O driver deve fazer seu próprio `copyin` e `copyout` para o conteúdo do buffer, enquanto o kernel cuida da struct envolvente. Esse padrão é necessário para dados de comprimento variável e é abordado na subseção de armadilhas abaixo.

### Projetando um Cabeçalho Público

Um driver que expõe ioctls deve publicar um cabeçalho que declare os códigos de requisição e as estruturas de dados. Programas do espaço do usuário incluem esse cabeçalho para construir chamadas corretas. O cabeçalho fica fora do módulo do kernel: é parte do contrato do driver com o espaço do usuário e deve ser instalável no sistema (por exemplo, em `/usr/local/include/myfirst/myfirst_ioctl.h`).

A convenção é colocar o cabeçalho no diretório de código-fonte do driver com um sufixo `.h` que corresponda ao nome público. Para o driver `myfirst`, o cabeçalho é `myfirst_ioctl.h`. Suas responsabilidades são limitadas: declarar os números de ioctl, declarar as estruturas usadas como argumentos de ioctl, declarar quaisquer constantes relacionadas (como o comprimento máximo do campo de mensagem) e nada mais. Ele não deve incluir cabeçalhos exclusivos do kernel, não deve declarar tipos exclusivos do kernel e deve compilar sem erros quando incluído por um programa do espaço do usuário.

Aqui está o cabeçalho completo para o driver de estágio 2 do capítulo:

```c
/*
 * myfirst_ioctl.h - public ioctl interface for the myfirst driver.
 *
 * This header is included by both the kernel module and any user-space
 * program that talks to the driver. Keep it self-contained: no kernel
 * headers, no kernel types, no inline functions that pull kernel state.
 */

#ifndef _MYFIRST_IOCTL_H_
#define _MYFIRST_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

/*
 * Maximum length of the in-driver message, including the trailing NUL.
 * The driver enforces this on SETMSG; user-space programs that build
 * larger buffers will see EINVAL.
 */
#define MYFIRST_MSG_MAX 256

/*
 * The interface version. Bumped when this header changes in a way that
 * is not backward-compatible. User-space programs should call
 * MYFIRSTIOC_GETVER first and refuse to operate on an unexpected
 * version.
 */
#define MYFIRST_IOCTL_VERSION 1

/*
 * MYFIRSTIOC_GETVER - return the driver's interface version.
 *
 *   ioctl(fd, MYFIRSTIOC_GETVER, &ver);   // ver = 1, 2, ...
 *
 * No FREAD or FWRITE flag is required.
 */
#define MYFIRSTIOC_GETVER  _IOR('M', 1, uint32_t)

/*
 * MYFIRSTIOC_GETMSG - copy the current in-driver message into the
 * caller's buffer. The buffer must be MYFIRST_MSG_MAX bytes; the
 * message is NUL-terminated.
 */
#define MYFIRSTIOC_GETMSG  _IOR('M', 2, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_SETMSG - replace the in-driver message. The buffer must
 * be MYFIRST_MSG_MAX bytes; the kernel takes the prefix up to the
 * first NUL or to MYFIRST_MSG_MAX - 1 bytes.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_SETMSG  _IOW('M', 3, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_RESET - reset all per-instance counters and clear the
 * message. Returns 0 on success.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_RESET   _IO('M', 4)

#endif /* _MYFIRST_IOCTL_H_ */
```

Alguns detalhes neste cabeçalho merecem atenção.

O uso de `uint32_t` e `sys/types.h` (em vez de `u_int32_t` e `sys/cdefs.h`) mantém o cabeçalho portável tanto na base do FreeBSD quanto em qualquer programa que siga POSIX. O kernel e o espaço do usuário concordam sobre o tamanho de `uint32_t`, portanto o comprimento codificado no código de requisição corresponde à visão do dispatcher sobre os dados.

O comprimento máximo de mensagem, `MYFIRST_MSG_MAX = 256`, está bem abaixo de `IOCPARM_MAX = 8192`, então o kernel realizará o copyin e o copyout da mensagem sem reclamações. Um driver que precisasse mover mensagens maiores poderia elevar o limite (até 8192) ou adotar o padrão de ponteiro embutido.

A constante `MYFIRST_IOCTL_VERSION` oferece ao espaço do usuário uma forma de detectar mudanças na API. O primeiro ioctl que qualquer programa deve emitir é `MYFIRSTIOC_GETVER`; se a versão retornada não for aquela para a qual o programa foi compilado, o programa deve se recusar a emitir ioctls adicionais e exibir um erro claro. Essa é a prática padrão para drivers que se espera evoluir.

O tipo de argumento `char[MYFIRST_MSG_MAX]` é incomum, mas legal em `_IOR` e `_IOW`. A macro toma `sizeof(t)`, e `sizeof(char[256]) == 256`, portanto o comprimento codificado é exatamente o tamanho do array. Essa é a forma mais limpa de expressar um buffer de tamanho fixo em um cabeçalho público de ioctl.

### Implementando o Dispatcher

Com o cabeçalho em mãos, o dispatcher é um switch statement que lê o código do comando, executa a ação e retorna 0 (sucesso) ou um errno positivo (falha). O dispatcher fica em `myfirst_ioctl.c`, um novo arquivo fonte adicionado ao driver no estágio 2.

O dispatcher completo:

```c
/*
 * myfirst_ioctl.c - ioctl dispatcher for the myfirst driver.
 *
 * The d_ioctl callback in myfirst_cdevsw points at myfirst_ioctl.
 * Per-command argument layout is documented in myfirst_ioctl.h, which
 * is shared with user space.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

SDT_PROBE_DEFINE3(myfirst, , , ioctl,
    "struct myfirst_softc *", "u_long", "int");

int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        SDT_PROBE3(myfirst, , , ioctl, sc, cmd, fflag);
        DPRINTF(sc, MYF_DBG_IOCTL, "ioctl: cmd=0x%08lx fflag=0x%x\n",
            cmd, fflag);

        mtx_lock(&sc->sc_mtx);

        switch (cmd) {
        case MYFIRSTIOC_GETVER:
                *(uint32_t *)data = MYFIRST_IOCTL_VERSION;
                break;

        case MYFIRSTIOC_GETMSG:
                /*
                 * Copy the current message into the caller's buffer.
                 * The buffer is MYFIRST_MSG_MAX bytes; we always emit
                 * a NUL-terminated string.
                 */
                strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
                break;

        case MYFIRSTIOC_SETMSG:
                if ((fflag & FWRITE) == 0) {
                        error = EBADF;
                        break;
                }
                /*
                 * The kernel has copied MYFIRST_MSG_MAX bytes into
                 * data. Take the prefix up to the first NUL.
                 */
                strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
                sc->sc_msglen = strlen(sc->sc_msg);
                DPRINTF(sc, MYF_DBG_IOCTL,
                    "SETMSG: new message is %zu bytes\n", sc->sc_msglen);
                break;

        case MYFIRSTIOC_RESET:
                if ((fflag & FWRITE) == 0) {
                        error = EBADF;
                        break;
                }
                sc->sc_open_count = 0;
                sc->sc_total_reads = 0;
                sc->sc_total_writes = 0;
                bzero(sc->sc_msg, sizeof(sc->sc_msg));
                sc->sc_msglen = 0;
                DPRINTF(sc, MYF_DBG_IOCTL,
                    "RESET: counters and message cleared\n");
                break;

        default:
                error = ENOIOCTL;
                break;
        }

        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

Diversas escolhas disciplinadas estão presentes neste dispatcher e valem a pena ser destacadas antes que você escreva o seu próprio.

O dispatcher adquire o mutex do softc uma única vez no início e o libera uma única vez no final. Cada comando é executado sob o mutex. Isso evita que um `read` dispute com um `SETMSG` (do contrário, o read poderia ver um buffer de mensagem parcialmente substituído) e evita que duas chamadas simultâneas de `RESET` corrompam os contadores. O mutex é o mesmo `sc->sc_mtx` introduzido anteriormente na Parte IV; estamos simplesmente estendendo seu escopo para cobrir a serialização de ioctls.

A primeira ação do dispatcher após adquirir o mutex é uma sonda SDT e um `DPRINTF`. Ambos reportam o comando e os flags de arquivo. A sonda SDT permite que um script DTrace rastreie cada ioctl em tempo real; o `DPRINTF` permite que um operador habilite `MYF_DBG_IOCTL` e observe o mesmo fluxo pelo `dmesg`. Ambos utilizam a infraestrutura de depuração introduzida no Capítulo 23 sem nenhuma maquinaria adicional.

Os caminhos `MYFIRSTIOC_SETMSG` e `MYFIRSTIOC_RESET` verificam `fflag & FWRITE` antes de alterar o estado. Sem essa verificação, um programa que abrisse o dispositivo somente leitura poderia modificar o estado do driver, o que é um padrão de escalonamento de privilégios em alguns drivers. A verificação retorna `EBADF` (file descriptor inadequado para a operação) em vez de `EPERM` (sem permissão), porque a falha diz respeito aos flags de abertura do arquivo, não à identidade do usuário.

O branch padrão retorna `ENOIOCTL`, nunca `EINVAL`. Essa é a regra da subseção anterior, repetida aqui porque é o bug mais comum em dispatchers escritos à mão.

As chamadas `strlcpy` em `GETMSG` e `SETMSG` são a primitiva de cópia de string segura no kernel do FreeBSD. Elas garantem terminação NUL e nunca ultrapassam o destino. As mesmas chamadas seriam `strncpy` em código mais antigo; `strlcpy` é a forma moderna preferida e é o que `style(9)` recomenda.

### As Adições ao Softc

O estágio 2 estende o softc com dois campos e confirma que os campos existentes ainda estão em uso:

```c
struct myfirst_softc {
        device_t        sc_dev;
        struct cdev    *sc_cdev;
        struct mtx      sc_mtx;

        /* From earlier chapters. */
        uint32_t        sc_debug;
        u_int           sc_open_count;
        u_int           sc_total_reads;
        u_int           sc_total_writes;

        /* New for stage 2. */
        char            sc_msg[MYFIRST_MSG_MAX];
        size_t          sc_msglen;
};
```

O buffer de mensagem é um array de tamanho fixo dimensionado para corresponder ao cabeçalho público. Armazená-lo inline (em vez de como um ponteiro para um buffer alocado separadamente) mantém o tempo de vida simples: o buffer existe exatamente pelo mesmo tempo que o softc. Não há `malloc` para rastrear nem `free` para esquecer.

A inicialização em `myfirst_attach` passa a ser:

```c
strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
sc->sc_msglen = strlen(sc->sc_msg);
```

O driver agora tem uma saudação padrão que sobrevive até o operador alterá-la via `SETMSG`, e sobrevive a ciclos de `unload`/`load` apenas pelo fato de o novo valor ser redefinido a cada carregamento. (Esse é o mesmo tempo de vida de todos os outros campos do softc; persistência entre reboots exigiria um sysctl tunable, que é o assunto da Seção 4.)

### Conectando o Dispatcher ao `cdevsw`

O cdevsw declarado no estágio 1 já tinha um slot `.d_ioctl` aguardando ser preenchido. O estágio 2 o preenche:

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_flags   = D_TRACKCLOSE,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
        .d_ioctl   = myfirst_ioctl,    /* new */
};
```

O kernel lê esta tabela uma única vez, quando o módulo é carregado. Não há etapa de registro em tempo de execução; o cdevsw é parte do estado estático do driver.

### Construindo o Driver de Estágio 2

O `Makefile` para o estágio 2 deve incluir o novo arquivo fonte:

```make
KMOD=   myfirst
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Os comandos de build são os mesmos do estágio 1:

```console
$ make
$ sudo kldload ./myfirst.ko
$ ls -l /dev/myfirst0
crw-rw---- 1 root wheel 0x... <date> /dev/myfirst0
```

Se o build falhar porque `myfirst_ioctl.h` não foi encontrado, verifique se a linha `CFLAGS` inclui `-I${.CURDIR}`. Se o carregamento falhar por causa de um símbolo não resolvido como `myfirst_ioctl`, verifique se `myfirst_ioctl.c` está listado em `SRCS` e se o nome da função corresponde à entrada do cdevsw.

### O Programa Companheiro `myfirstctl` do Espaço do Usuário

Um driver com interface ioctl precisa de um pequeno programa companheiro que o exercite. Sem ele, o operador não tem como chamar os ioctls exceto por meio de um teste escrito à mão ou do pass-through de ioctl do `devctl(8)`, o que é desajeitado para uso rotineiro.

O programa companheiro é `myfirstctl`, um programa C de arquivo único que recebe um subcomando na linha de comando e chama o ioctl correspondente. É intencionalmente pequeno (menos de 200 linhas) e depende apenas do cabeçalho público.

```c
/*
 * myfirstctl.c - command-line front end to the myfirst driver's ioctls.
 *
 * Build:  cc -o myfirstctl myfirstctl.c
 * Usage:  myfirstctl get-version
 *         myfirstctl get-message
 *         myfirstctl set-message "<text>"
 *         myfirstctl reset
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myfirst_ioctl.h"

#define DEVPATH "/dev/myfirst0"

static void
usage(void)
{
        fprintf(stderr,
            "usage: myfirstctl get-version\n"
            "       myfirstctl get-message\n"
            "       myfirstctl set-message <text>\n"
            "       myfirstctl reset\n");
        exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
        int fd, flags;
        const char *cmd;

        if (argc < 2)
                usage();
        cmd = argv[1];

        /*
         * SETMSG and RESET need write access; the others only need
         * read. Open the device with the right flags so the dispatcher
         * does not return EBADF.
         */
        if (strcmp(cmd, "set-message") == 0 ||
            strcmp(cmd, "reset") == 0)
                flags = O_RDWR;
        else
                flags = O_RDONLY;

        fd = open(DEVPATH, flags);
        if (fd < 0)
                err(EX_OSERR, "open %s", DEVPATH);

        if (strcmp(cmd, "get-version") == 0) {
                uint32_t ver;
                if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_GETVER");
                printf("driver ioctl version: %u\n", ver);
        } else if (strcmp(cmd, "get-message") == 0) {
                char buf[MYFIRST_MSG_MAX];
                if (ioctl(fd, MYFIRSTIOC_GETMSG, buf) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_GETMSG");
                printf("%s\n", buf);
        } else if (strcmp(cmd, "set-message") == 0) {
                char buf[MYFIRST_MSG_MAX];
                if (argc < 3)
                        usage();
                strlcpy(buf, argv[2], sizeof(buf));
                if (ioctl(fd, MYFIRSTIOC_SETMSG, buf) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_SETMSG");
        } else if (strcmp(cmd, "reset") == 0) {
                if (ioctl(fd, MYFIRSTIOC_RESET) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_RESET");
        } else {
                usage();
        }

        close(fd);
        return (0);
}
```

Dois detalhes merecem destaque.

O programa abre o dispositivo com os flags mínimos necessários para a operação solicitada. `MYFIRSTIOC_GETVER` e `MYFIRSTIOC_GETMSG` funcionam bem com `O_RDONLY`, mas `MYFIRSTIOC_SETMSG` e `MYFIRSTIOC_RESET` exigem `O_RDWR` porque o dispatcher verifica `fflag & FWRITE`. Um usuário que execute `myfirstctl set-message foo` sem pertencer ao grupo apropriado verá `Permission denied` no `open`; aquele que pertence ao grupo mas ainda assim o dispatcher rejeita verá `Bad file descriptor` no `ioctl`. Ambos os erros são inteligíveis.

A chamada `MYFIRSTIOC_RESET` não passa terceiro argumento porque a macro `_IO` (sem `R` ou `W`) declara um ioctl de dados void. A `ioctl(2)` da biblioteca é variádica, então chamá-la com dois argumentos é legal, mas é preciso cuidado porque um argumento extra seria passado e ignorado. A convenção neste livro é chamar ioctls `_IO` com exatamente dois argumentos para deixar clara a natureza de dados void no código-fonte.

Uma sessão típica se parece com isso:

```console
$ myfirstctl get-version
driver ioctl version: 1
$ myfirstctl get-message
Hello from myfirst
$ myfirstctl set-message "drivers are fun"
$ myfirstctl get-message
drivers are fun
$ myfirstctl reset
$ myfirstctl get-message

$
```

Após o `reset`, o buffer de mensagem está vazio e `myfirstctl get-message` imprime uma linha em branco. Os contadores também são redefinidos, o que a interface sysctl da próxima seção nos permitirá verificar diretamente.

### Armadilhas Comuns com ioctl

A primeira armadilha é a **incompatibilidade de tamanho de tipo entre o cabeçalho e o dispatcher**. Se o cabeçalho declara `_IOR('M', 1, uint32_t)` mas o dispatcher escreve um `uint64_t` em `*(uint64_t *)data`, o kernel alocou um buffer de 4 bytes e o dispatcher escreve 8 bytes nele. Os 4 bytes extras corrompem qualquer coisa que estivesse ao lado do buffer (frequentemente outros argumentos de ioctl ou o stack frame local do dispatcher). Com `WITNESS` e `INVARIANTS` ativados, o kernel pode detectar o estouro e entrar em pânico; em um build de produção, o resultado é corrupção silenciosa. A correção é manter o cabeçalho e o dispatcher em sincronia, de preferência incluindo o mesmo cabeçalho nos dois lugares (o que o padrão deste capítulo faz).

A segunda armadilha são os **ponteiros embutidos**. Uma struct que contém `char *buf; size_t len;` não pode ser transferida com segurança por meio de um único `_IOW`. O kernel fará o copyin da struct (o ponteiro e o comprimento), mas o buffer apontado pelo ponteiro está no espaço do usuário e o dispatcher não pode desreferenciá-lo diretamente. O dispatcher deve chamar `copyin(uap->buf, kbuf, uap->len)` para transferir o conteúdo do buffer por conta própria. Esquecer esse passo faz com que o dispatcher leia memória do espaço do usuário por meio de um ponteiro do kernel, o que a proteção de espaço de endereços do kernel tratará como uma falha. A correção é embutir o buffer na struct (o padrão que este capítulo usa para o campo de mensagem) ou adicionar chamadas explícitas de `copyin`/`copyout` dentro do dispatcher.

A terceira armadilha é **esquecer de tratar `ENOIOCTL` corretamente**. Um driver que retorna `EINVAL` para um comando desconhecido suprime o fallback genérico de ioctl do kernel. O usuário pode ver `Invalid argument` de um comando que deveria ter sido silenciosamente repassado para a camada do sistema de arquivos (como `FIONBIO` para a dica de I/O não bloqueante). A correção é usar `ENOIOCTL` como retorno padrão.

A quarta armadilha é **alterar o formato wire de um ioctl existente**. Uma vez que um programa seja compilado com `MYFIRSTIOC_SETMSG` declarado com um buffer de 256 bytes, recompilar o driver com um buffer de 512 bytes quebra o programa: o comprimento codificado no código da requisição muda, o kernel detecta a incompatibilidade (porque o usuário passou um buffer de 256 bytes com o novo comando de 512 bytes), e a chamada `ioctl` retorna `ENOTTY` ("Inappropriate ioctl for device"). A correção é deixar os ioctls existentes intactos e definir novos comandos com novos números quando o formato precisar mudar. A constante `MYFIRST_IOCTL_VERSION` permite que programas no espaço do usuário detectem essa evolução antes de emitir as chamadas afetadas.

A quinta armadilha é **realizar trabalho demorado no dispatcher enquanto se segura um mutex**. O dispatcher nesta seção mantém `sc->sc_mtx` durante toda a instrução switch, o que é adequado porque cada comando é rápido (um memcpy, um reset de contador, um strlcpy). Um driver real que precisa realizar uma operação de hardware que pode levar milissegundos deve soltar o mutex antes e readquiri-lo depois, ou usar um lock que permita sleep. Manter um mutex não-sleepable durante um `tsleep` ou `msleep` faria o kernel entrar em pânico.

### Encerrando a Seção 3

A Seção 3 completou a segunda superfície de integração: uma interface ioctl projetada adequadamente. O driver agora expõe quatro comandos por meio de `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` e `MYFIRSTIOC_RESET`. A interface é autodescritiva (qualquer programa no espaço do usuário pode chamar `MYFIRSTIOC_GETVER` para detectar a versão da API), a codificação é explícita (os macros `_IOR`/`_IOW`/`_IO` de `/usr/src/sys/sys/ioccom.h`), e o kernel trata o copyin e o copyout automaticamente com base no layout de bits. O programa companion `myfirstctl` demonstra como uma ferramenta no espaço do usuário exercita a interface sem nunca tocar nos bytes do código da requisição em si.

O marco do driver para o estágio 2 é a adição de `myfirst_ioctl.c` e `myfirst_ioctl.h`, ambos integrados de forma limpa com a infraestrutura de debug do Capítulo 23 (o bit de máscara `MYF_DBG_IOCTL` e a sonda SDT `myfirst:::ioctl`). O `Makefile` ganhou uma entrada em `SRCS` e o cdevsw ganhou um callback preenchido. Todo o resto no driver permanece inalterado.

Na Seção 4 nos voltamos para a terceira superfície de integração: knobs somente leitura e de leitura/escrita que um administrador pode consultar e ajustar no shell usando `sysctl(8)`. Onde o ioctl é o canal certo para um programa que já tem o dispositivo aberto, o sysctl é o canal certo para um script ou operador que quer inspecionar ou ajustar o estado do driver sem abrir nada. As duas interfaces se complementam; a maioria dos drivers de produção oferece ambas.

## Seção 4: Expondo Métricas por Meio de `sysctl()`

### O que é `sysctl` e por que os Drivers o Utilizam

`sysctl(8)` é o serviço de nomes hierárquico do kernel do FreeBSD. Cada nome na árvore mapeia para um pedaço do estado do kernel: uma constante, um contador, uma variável ajustável ou um ponteiro de função que produz um valor sob demanda. A árvore tem como raiz `kern.`, `vm.`, `hw.`, `net.`, `dev.` e alguns outros prefixos de nível superior. Qualquer programa com os privilégios adequados pode ler e (para nós graváveis) modificar esses valores por meio da biblioteca `sysctl(3)`, da ferramenta de linha de comando `sysctl(8)` ou da interface de conveniência `sysctlbyname(3)`.

Para drivers, a subárvore relevante é `dev.<driver_name>.<unit>.*`. O subsistema Newbus cria esse prefixo automaticamente para cada dispositivo conectado. Um driver chamado `myfirst` com unidade 0 conectada recebe o prefixo `dev.myfirst.0` de graça, sem necessidade de nenhum código no driver. A única responsabilidade do driver é popular o prefixo com OIDs (identificadores de objeto) nomeados para os valores que deseja expor.

Por que expor estado via sysctl em vez de ioctl? Os dois mecanismos respondem a perguntas diferentes. O ioctl é o canal certo para um programa que já abriu o dispositivo e quer emitir um comando. O sysctl é o canal certo para um operador em um prompt de shell que quer inspecionar ou ajustar o estado sem abrir nada. A maioria dos drivers de produção oferece ambos: a interface ioctl para programas, e a interface sysctl para humanos, scripts e ferramentas de monitoramento.

O padrão comum é que o sysctl expõe:

* **contadores** que resumem a atividade do driver desde a conexão
* **estado somente leitura** como números de versão, identificadores de hardware e estado do link
* **ajustáveis de leitura/escrita** como máscaras de debug, profundidades de fila e valores de timeout
* **ajustáveis de tempo de boot** que são lidos de `/boot/loader.conf` antes do driver se conectar

Ao final desta seção, o driver `myfirst` exporá todas as quatro categorias sob `dev.myfirst.0` e lerá sua máscara de debug inicial de `/boot/loader.conf`. O marco do driver para o estágio 3 é a adição de `myfirst_sysctl.c` e uma pequena árvore de OIDs.

### O Namespace do Sysctl

Um nome completo de sysctl parece um caminho com pontos. O prefixo padrão do Newbus para nosso driver é:

```text
dev.myfirst.0
```

Abaixo desse prefixo, o driver pode adicionar o que quiser. O driver `myfirst` no estágio 3 adicionará:

```text
dev.myfirst.0.%desc            "myfirst pseudo-device, integration version 1.7"
dev.myfirst.0.%driver          "myfirst"
dev.myfirst.0.%location        ""
dev.myfirst.0.%pnpinfo         ""
dev.myfirst.0.%parent          "nexus0"
dev.myfirst.0.version          "1.7-integration"
dev.myfirst.0.open_count       0
dev.myfirst.0.total_reads      0
dev.myfirst.0.total_writes     0
dev.myfirst.0.message          "Hello from myfirst"
dev.myfirst.0.debug.mask       0
dev.myfirst.0.debug.classes    "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ..."
```

Os primeiros cinco nomes (os que começam com `%`) são adicionados pelo Newbus automaticamente e descrevem o relacionamento na árvore de dispositivos. Os nomes restantes são a contribuição do driver. Destes, `version`, `open_count`, `total_reads`, `total_writes` e `debug.classes` são somente leitura; `message` e `debug.mask` são de leitura/escrita. A subárvore `debug` é em si um nó, o que significa que pode conter mais OIDs conforme o driver cresce.

O leitor já pode ver o resultado em um sistema com `myfirst` carregado:

```console
$ sysctl dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
dev.myfirst.0.%parent: nexus0
dev.myfirst.0.%pnpinfo:
dev.myfirst.0.%location:
dev.myfirst.0.%driver: myfirst
dev.myfirst.0.%desc: myfirst pseudo-device, integration version 1.7
```

A ordem das linhas é a ordem de criação dos OIDs, invertida. (O Newbus adiciona os nomes com prefixo `%` por último, então eles aparecem primeiro quando o sysctl percorre a lista de trás para frente. Isso é cosmético e não tem significado semântico.)

### OIDs Estáticos Versus OIDs Dinâmicos

Os OIDs do sysctl existem em dois tipos.

Um **OID estático** é declarado em tempo de compilação com um dos macros `SYSCTL_*` (`SYSCTL_INT`, `SYSCTL_STRING`, `SYSCTL_ULONG` e assim por diante). O macro gera uma estrutura de dados constante que o linker cola em uma seção especial, e o kernel monta a seção na árvore global durante o boot. OIDs estáticos são adequados para valores globais do sistema que existem durante toda a vida útil do kernel: ticks de timer, estatísticas do escalonador e similares.

Um **OID dinâmico** é criado em tempo de execução com uma das funções `SYSCTL_ADD_*` (`SYSCTL_ADD_INT`, `SYSCTL_ADD_STRING`, `SYSCTL_ADD_PROC` e assim por diante). A função recebe um contexto, um pai, um nome e um ponteiro para os dados subjacentes, e insere um novo nó na árvore. OIDs dinâmicos são adequados para valores por instância que aparecem e desaparecem com um dispositivo: um driver os cria em `attach` e os destrói em `detach`.

O código de driver usa OIDs dinâmicos quase exclusivamente. Um driver não existe em tempo de compilação do kernel; ele aparece quando o módulo é carregado, e qualquer subárvore do sysctl que ele possua deve ser construída em tempo de attach e descartada em tempo de detach. O framework Newbus fornece a cada driver um contexto de sysctl por dispositivo e um OID pai especificamente para esse propósito:

```c
struct sysctl_ctx_list *ctx;
struct sysctl_oid *tree;
struct sysctl_oid_list *child;

ctx = device_get_sysctl_ctx(dev);
tree = device_get_sysctl_tree(dev);
child = SYSCTL_CHILDREN(tree);
```

`device_get_sysctl_ctx` retorna o contexto por dispositivo. O contexto rastreia cada OID que o driver cria, de modo que o framework possa liberá-los todos em uma única chamada quando o driver faz detach. O driver não precisa rastreá-los por conta própria.

`device_get_sysctl_tree` retorna o nó da árvore por dispositivo, que é o OID correspondente a `dev.<driver>.<unit>`. A árvore foi criada pelo Newbus quando o dispositivo foi adicionado.

`SYSCTL_CHILDREN(tree)` extrai a lista de filhos do nó da árvore. É isso que o driver passa como argumento pai para chamadas subsequentes de `SYSCTL_ADD_*`.

Com esses três identificadores em mãos, o driver pode adicionar qualquer número de OIDs à sua subárvore:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
    CTLFLAG_RD, &sc->sc_open_count, 0,
    "Number of times the device has been opened");
```

A chamada `SYSCTL_ADD_UINT` adiciona um OID de inteiro sem sinal sob o pai, com o nome `open_count`, com `CTLFLAG_RD` (somente leitura), suportado por `&sc->sc_open_count`, sem valor inicial especial e com uma descrição. A descrição é o que `sysctl -d dev.myfirst.0.open_count` imprimirá. Sempre escreva uma descrição útil; uma vazia é uma lacuna de documentação.

A chamada correspondente para um inteiro de leitura/escrita é idêntica, exceto pela flag:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "debug_mask_simple",
    CTLFLAG_RW, &sc->sc_debug, 0,
    "Simple writable debug mask");
```

A flag `CTLFLAG_RW` informa ao kernel para permitir escritas de usuários privilegiados (root, ou processos com `PRIV_DRIVER`).

Para uma string, o macro é `SYSCTL_ADD_STRING`:

```c
SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
    CTLFLAG_RD, sc->sc_version, 0,
    "Driver version string");
```

O terceiro argumento a partir do final é um ponteiro para o buffer que contém a string, e o segundo a partir do final é o tamanho do buffer (zero significa ilimitado para strings somente leitura).

### OIDs Suportados por Handler

Alguns OIDs precisam de mais lógica do que um simples acesso à memória. Ler o OID pode exigir calcular o valor a partir de vários campos do softc; escrever no OID pode exigir validar o novo valor e atualizar o estado relacionado. Esses OIDs usam uma função handler e o macro `SYSCTL_ADD_PROC`.

Um handler tem a seguinte assinatura:

```c
static int handler(SYSCTL_HANDLER_ARGS);
```

`SYSCTL_HANDLER_ARGS` é um macro que se expande para:

```c
struct sysctl_oid *oidp, void *arg1, intptr_t arg2,
struct sysctl_req *req
```

`oidp` identifica o OID sendo acessado. `arg1` e `arg2` são os argumentos fornecidos pelo usuário registrados quando o OID foi criado (tipicamente `arg1` aponta para o softc e `arg2` não é usado ou contém uma pequena constante). `req` carrega o contexto de leitura/escrita: `req->newptr` é não-NULL para uma escrita (e aponta para o novo valor que o usuário está fornecendo), e o handler deve chamar `SYSCTL_OUT(req, value, sizeof(value))` para retornar um valor em uma leitura.

Um handler típico que expõe um valor calculado:

```c
static int
myfirst_sysctl_message_len(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        u_int len;

        mtx_lock(&sc->sc_mtx);
        len = (u_int)sc->sc_msglen;
        mtx_unlock(&sc->sc_mtx);

        return (sysctl_handle_int(oidp, &len, 0, req));
}
```

O handler calcula o valor (aqui, copiando o comprimento da mensagem sob o mutex) e então delega para `sysctl_handle_int`, que faz o trabalho de bookkeeping para a leitura ou escrita e (para uma escrita) chama de volta o handler com o novo valor já em `*ptr`. O padrão handler-de-handlers é idiomático; usá-lo corretamente evita reimplementar copyin e copyout para cada handler tipado.

O handler é registrado com `SYSCTL_ADD_PROC`:

```c
SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_message_len, "IU",
    "Current length of the in-driver message");
```

Três argumentos merecem atenção. `CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE` é a palavra de tipo e flags. `CTLTYPE_UINT` declara o tipo externo do OID (`unsigned int`); `CTLFLAG_RD` o declara como somente leitura; `CTLFLAG_MPSAFE` declara que o handler é seguro para ser chamado sem o Giant lock. A flag `CTLFLAG_MPSAFE` é obrigatória para código novo; sem ela, o kernel ainda funciona, mas adquire o Giant lock em torno de cada leitura, o que serializa o sistema inteiro a cada acesso via sysctl.

O sétimo argumento é a string de formato. `"IU"` declara um `unsigned int` (`I` para inteiro, `U` para sem sinal). O conjunto completo está documentado em `/usr/src/sys/sys/sysctl.h`: `"I"` para `int`, `"IU"` para `uint`, `"L"` para `long`, `"LU"` para `ulong`, `"Q"` para `int64`, `"QU"` para `uint64`, `"A"` para string, `"S,structname"` para uma struct opaca. O comando `sysctl(8)` usa a string de formato para decidir como exibir o valor quando invocado sem `-x` (a flag de hex bruto).

### A Árvore Sysctl do `myfirst`

A árvore sysctl completa do estágio 3 é construída em uma única função, `myfirst_sysctl_attach`, chamada a partir de `myfirst_attach` após a criação do cdev. A função é curta o suficiente para ser lida de ponta a ponta:

```c
/*
 * myfirst_sysctl.c - sysctl tree for the myfirst driver.
 *
 * Builds dev.myfirst.<unit>.* with version, counters, message, and a
 * debug subtree (debug.mask, debug.classes).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include "myfirst.h"
#include "myfirst_debug.h"

#define MYFIRST_VERSION "1.7-integration"

static int
myfirst_sysctl_message_len(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        u_int len;

        mtx_lock(&sc->sc_mtx);
        len = (u_int)sc->sc_msglen;
        mtx_unlock(&sc->sc_mtx);

        return (sysctl_handle_int(oidp, &len, 0, req));
}

void
myfirst_sysctl_attach(struct myfirst_softc *sc)
{
        device_t dev = sc->sc_dev;
        struct sysctl_ctx_list *ctx;
        struct sysctl_oid *tree;
        struct sysctl_oid_list *child;
        struct sysctl_oid *debug_node;
        struct sysctl_oid_list *debug_child;

        ctx = device_get_sysctl_ctx(dev);
        tree = device_get_sysctl_tree(dev);
        child = SYSCTL_CHILDREN(tree);

        /* Read-only: driver version. */
        SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
            CTLFLAG_RD, MYFIRST_VERSION, 0,
            "Driver version string");

        /* Read-only: counters. */
        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
            CTLFLAG_RD, &sc->sc_open_count, 0,
            "Number of currently open file descriptors");

        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_reads",
            CTLFLAG_RD, &sc->sc_total_reads, 0,
            "Total read() calls since attach");

        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_writes",
            CTLFLAG_RD, &sc->sc_total_writes, 0,
            "Total write() calls since attach");

        /* Read-only: message buffer (no copy through user) */
        SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "message",
            CTLFLAG_RD, sc->sc_msg, sizeof(sc->sc_msg),
            "Current in-driver message");

        /* Read-only handler: message length, computed. */
        SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_message_len, "IU",
            "Current length of the in-driver message in bytes");

        /* Subtree: debug.* */
        debug_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "debug",
            CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
            "Debug controls and class enumeration");
        debug_child = SYSCTL_CHILDREN(debug_node);

        SYSCTL_ADD_UINT(ctx, debug_child, OID_AUTO, "mask",
            CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_debug, 0,
            "Bitmask of enabled debug classes");

        SYSCTL_ADD_STRING(ctx, debug_child, OID_AUTO, "classes",
            CTLFLAG_RD,
            "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) "
            "INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)",
            0, "Names and bit values of debug classes");
}
```

Três detalhes merecem uma atenção especial.

O OID `version` é respaldado por uma constante de string (`MYFIRST_VERSION`), e não por um campo do softc. Um OID de string somente leitura pode apontar para qualquer buffer estável; o kernel nunca escreve por meio do ponteiro. Isso é mais seguro e mais simples do que manter uma cópia da versão por instância de softc, e permite que a versão fique visível via `sysctl` mesmo que o driver falhe no attach parcialmente.

O OID `message` aponta diretamente para o campo `sc_msg` do softc com `CTLFLAG_RD`. Um leitor que chamar `sysctl dev.myfirst.0.message` obterá o valor atual. Como o OID é somente leitura, o sysctl não escreverá no buffer, de modo que não precisamos de um handler de escrita. (Uma versão de leitura e escrita desse OID precisaria de um handler para validar a entrada; o caminho de leitura e escrita passa pela interface ioctl do estágio 2.)

O OID `debug.mask` tem `CTLFLAG_RW | CTLFLAG_TUN`. O flag `RW` permite escritas de um usuário privilegiado. O flag `TUN` instrui o kernel a procurar um tunable correspondente em `/boot/loader.conf` e aplicá-lo antes que o OID se torne acessível. (Configuraremos o hook do loader.conf na próxima subseção.)

### Conectando o Sysctl ao Attach e ao Detach

O caminho de attach agora chama o construtor de sysctl após a criação do cdev:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0) {
                mtx_destroy(&sc->sc_mtx);
                return (error);
        }

        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT, "attach: cdev created and sysctl tree built\n");
        return (0);
}
```

O caminho de detach permanece inalterado: ele não precisa chamar `myfirst_sysctl_detach`. O framework Newbus é dono do contexto sysctl por dispositivo e o destrói automaticamente quando o dispositivo faz o detach. O driver só precisa limpar os recursos que alocou fora do framework (o cdev e o mutex). Essa é uma das razões pequenas mas reais para preferir o contexto por dispositivo em vez de um contexto privado.

### Tunables em Tempo de Boot Via `/boot/loader.conf`

Um driver pode permitir que o operador configure seu comportamento inicial em tempo de boot lendo valores do ambiente do loader. O loader (`/boot/loader.efi` ou `/boot/loader`) analisa o `/boot/loader.conf` antes de o kernel iniciar e exporta as variáveis para um pequeno ambiente que o kernel pode consultar.

A maneira mais simples de ler uma variável do loader é `TUNABLE_INT_FETCH`:

```c
TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);
```

O primeiro argumento é o nome da variável do loader. O segundo é um ponteiro para o destino, que também é o valor padrão caso a variável esteja ausente. A chamada é silenciosa se a variável estiver ausente e escreve o valor analisado caso contrário.

A chamada vai em `myfirst_attach` antes de `myfirst_sysctl_attach`. No momento em que a árvore sysctl é construída, `sc->sc_debug` já possui o valor fornecido pelo loader (ou o padrão em tempo de compilação), e o OID `dev.myfirst.0.debug.mask` o reflete.

Uma entrada representativa em `/boot/loader.conf` para o driver tem a seguinte aparência:

```ini
myfirst_load="YES"
hw.myfirst.debug_mask_default="0x06"
```

A primeira linha instrui o loader a carregar `myfirst.ko` automaticamente. A segunda define a máscara de debug padrão como `MYF_DBG_OPEN | MYF_DBG_IO`. Após o boot, `sysctl dev.myfirst.0.debug.mask` reporta `6`, e o operador pode modificá-lo em tempo de execução sem precisar reiniciar.

A convenção de nomenclatura é flexível, mas segue algumas práticas. Mantenha as variáveis do loader sob `hw.<driver>.<knob>` porque o namespace `hw.` é convencionalmente somente leitura em tempo de execução e não está sujeito a renomeações surpresa. Use `default` no nome da variável quando o valor for o valor inicial de um OID modificável em tempo de execução, para deixar a relação clara. Documente cada variável do loader na página de manual do driver ou no cartão de referência do capítulo (o capítulo tem um ao final).

### Combinando Sysctl com a Máscara de Debug

O leitor vai se lembrar do Capítulo 23 que o driver já tem um campo `sc->sc_debug` e um macro `DPRINTF` que o consulta. Com o estágio 3 implementado, o operador pode agora manipular a máscara a partir do shell:

```console
$ sysctl dev.myfirst.0.debug.mask
dev.myfirst.0.debug.mask: 0
$ sysctl dev.myfirst.0.debug.classes
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
$ sudo sysctl dev.myfirst.0.debug.mask=0xff
dev.myfirst.0.debug.mask: 0 -> 255
$ # now every DPRINTF call inside the driver will print
```

O OID `classes` existe justamente para poupar o operador de ter de memorizar os valores dos bits. O `sysctl` imprime os nomes e os valores juntos, e o operador pode copiar um valor hexadecimal da tela e colá-lo no próximo comando.

O mesmo mecanismo se estende a qualquer outro parâmetro que o driver queira expor. Um driver que tem um timeout configurável adicionaria:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "timeout_ms",
    CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_timeout_ms, 0,
    "Operation timeout in milliseconds");
```

Um driver que queira habilitar ou desabilitar uma funcionalidade por instância adicionaria um `bool` (declarado com `SYSCTL_ADD_BOOL`, que é o tipo moderno preferido para flags booleanos) ou um int com dois valores válidos (0 e 1).

### Compilando o Driver do Estágio 3

O `Makefile` do estágio 3 lista o novo arquivo-fonte:

```make
KMOD=   myfirst
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Após `make` e `kldload`, o operador pode imediatamente percorrer a árvore:

```console
$ sudo kldload ./myfirst.ko
$ sysctl -a dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message_len: 18
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
dev.myfirst.0.%parent: nexus0
dev.myfirst.0.%pnpinfo:
dev.myfirst.0.%location:
dev.myfirst.0.%driver: myfirst
dev.myfirst.0.%desc: myfirst pseudo-device, integration version 1.7
```

Abrir e ler o dispositivo deve incrementar os contadores imediatamente:

```console
$ cat /dev/myfirst0
Hello from myfirst
$ sysctl dev.myfirst.0.total_reads
dev.myfirst.0.total_reads: 1
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 0
```

O `open_count` mostra zero porque o `cat` abre o dispositivo, lê e fecha imediatamente; quando o `sysctl` é executado, a contagem já voltou a zero. Para ver um valor diferente de zero, mantenha o dispositivo aberto em outro terminal:

```console
# terminal 1
$ exec 3< /dev/myfirst0

# terminal 2
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 1

# terminal 1
$ exec 3<&-

# terminal 2
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 0
```

O `exec 3< /dev/myfirst0` do shell abre o dispositivo no descritor de arquivo 3 e o mantém aberto até que `exec 3<&-` o feche. Essa é uma técnica útil para inspecionar a métrica de open-count de qualquer driver sem precisar escrever um programa.

### Armadilhas Comuns com Sysctl

A primeira armadilha é **esquecer o `CTLFLAG_MPSAFE`**. Sem o flag, o kernel adquire o giant lock em torno do handler do OID. Para um inteiro somente leitura isso é inofensivo; para um OID muito acessado, isso serializa o kernel inteiro e representa um desastre de latência. O código moderno do kernel usa `CTLFLAG_MPSAFE` em todo lugar; a ausência do flag é um sinal de que o código é anterior à migração para locks de granularidade fina e deve ser revisado quanto à correção de qualquer forma.

A segunda armadilha é **usar um OID estático em código de driver**. Os macros `SYSCTL_INT` e `SYSCTL_STRING` (sem o prefixo `_ADD_`) declaram OIDs estáticos e os colocam em uma seção especial do linker que é processada durante o boot do kernel. Um módulo carregável que usa esses macros instalará os OIDs quando o módulo for carregado, mas os OIDs referenciarão campos por instância que não existiam em tempo de compilação, causando crashes no momento em que o operador os ler. A solução é usar a família `SYSCTL_ADD_*` para todos os OIDs do driver.

A terceira armadilha é **vazar o contexto por driver**. Um driver que usa seu próprio `sysctl_ctx_init` e `sysctl_ctx_free` (em vez do contexto por dispositivo retornado por `device_get_sysctl_ctx`) deve se lembrar de chamar `sysctl_ctx_free` no detach. Esquecer isso vaza cada OID que o driver criou e causa um panic no kernel na próxima vez que o operador ler algum deles. A solução é usar o contexto por dispositivo (que o framework limpa automaticamente) sempre que possível.

A quarta armadilha é **colocar estado por instância em um OID compartilhado entre processos**. Um driver que queira um tunable compartilhado entre todas as suas instâncias pode ser tentado a colocá-lo sob `kern.myfirst.foo` ou sob `dev.myfirst.foo`. O último parece inofensivo, mas quebra: quando a segunda instância faz o attach, o Newbus tenta criar `dev.myfirst.0.foo` e `dev.myfirst.1.foo`, e o `dev.myfirst.foo` existente (sem o número da unidade) deixa de estar no escopo. A solução é usar ou `hw.myfirst.<knob>` para tunables compartilhados ou OIDs por instância para estado por instância, mas não ambos com o mesmo nome.

A quinta armadilha é **alterar o tipo de um OID**. Um OID declarado como `CTLTYPE_UINT` não pode ter seu tipo alterado sem invalidar qualquer programa em espaço do usuário que tenha chamado `sysctlbyname` contra ele. O kernel retorna `EINVAL` se o usuário passa um buffer de tamanho errado. A solução é manter o tipo estável entre versões; se um tipo diferente for necessário, defina um novo nome de OID e marque o antigo como obsoleto.

### Encerrando a Seção 4

A Seção 4 adicionou a terceira superfície de integração: a árvore sysctl sob `dev.myfirst.0`. O driver agora expõe sua versão, contadores, mensagem atual, máscara de debug e enumeração de classes, tudo com texto de ajuda descritivo e tudo construído com o contexto sysctl por dispositivo fornecido pelo Newbus. A máscara de debug pode ser definida em tempo de boot via `/boot/loader.conf` e ajustada em tempo de execução via `sysctl(8)`. Um trecho curto no attach constrói toda a árvore; o detach não faz nada porque o framework limpa automaticamente.

O marco do driver para o estágio 3 é a adição de `myfirst_sysctl.c` e uma pequena extensão para `myfirst_attach`. O cdevsw, o dispatcher de ioctl, a infraestrutura de debug e o restante do driver permanecem inalterados. A árvore sysctl é puramente aditiva.

Na Seção 5, examinamos um alvo de integração opcional, mas ilustrativo: a pilha de rede. A maioria dos drivers nunca se tornará um driver de rede, mas compreender como um driver registra um `ifnet` e participa da API `if_*` oferece ao leitor um exemplo do padrão que o kernel usa para todo "subsistema com uma interface de registro". Se o driver não for um driver de rede, o leitor pode ler a Seção 5 a título de contexto e pular diretamente para a Seção 7.

## Seção 5: Integração com Rede (Opcional)

### Por Que Esta Seção É Opcional

O driver `myfirst` não é um driver de rede e não se tornará um neste capítulo. As interfaces cdevsw, ioctl e sysctl que construímos são suficientes para ele. O leitor que estiver acompanhando o capítulo para integrar um driver que não seja de rede pode avançar com segurança para a Seção 7 sem perder nada essencial.

No entanto, a integração com rede é uma ilustração perfeita de um princípio mais geral: muitos subsistemas do FreeBSD oferecem uma interface de registro que transforma um driver em participante de um framework maior. O padrão é o mesmo quer o framework seja de rede, armazenamento, USB ou som: o driver aloca um objeto definido pelo framework, preenche os callbacks, chama uma função de registro e, a partir desse momento, recebe callbacks do framework. Ler esta seção, mesmo sem escrever um driver de rede, constrói a intuição necessária para toda outra integração de framework ao longo do livro.

O capítulo usa a pilha de rede como exemplo por dois motivos. Primeiro, ela é o framework mais amplamente compreendido, de modo que o vocabulário (`ifnet`, `if_attach`, `bpf`) se conecta a comandos visíveis ao usuário, como `ifconfig(8)` e `tcpdump(8)`. Segundo, a interface de registro da rede é pequena o suficiente para ser percorrida de ponta a ponta sem perder o leitor. A Seção 6 mostra então o mesmo padrão aplicado à pilha de armazenamento CAM.

### O Que É um `ifnet`

`ifnet` é o objeto por interface da pilha de rede. Ele é o equivalente de rede ao `cdev` com o qual trabalhamos na Seção 2. Assim como um `cdev` representa um nó de dispositivo sob `/dev`, um `ifnet` representa uma interface de rede sob `ifconfig`. Cada linha de `ifconfig -a` corresponde a um `ifnet`.

O `ifnet` é opaco de fora da pilha de rede. Os drivers o veem por meio do typedef `if_t` e o manipulam através de funções de acesso (`if_setflags`, `if_getmtu`, `if_settransmitfn`). A opacidade é deliberada: ela permite que a pilha de rede evolua os internos do `ifnet` sem quebrar todos os drivers a cada versão. Novos drivers devem usar a API `if_t` exclusivamente.

O ciclo de vida de um `ifnet` em um driver é:

1. **alocar** com `if_alloc(IFT_<type>)`
2. **nomear** com `if_initname(ifp, "myif", unit)`
3. **preencher os callbacks** para ioctl, transmit, init e similares
4. **fazer o attach** com `if_attach(ifp)`, que torna a interface visível
5. **conectar ao BPF** com `bpfattach(ifp, ...)` para que o `tcpdump` possa ver o tráfego
6. ... a interface vive, recebe tráfego, executa ioctls ...
7. **desconectar do BPF** com `bpfdetach(ifp)`
8. **fazer o detach** com `if_detach(ifp)`, que a remove da lista visível
9. **liberar** com `if_free(ifp)`

O ciclo de vida espelha quase exatamente o ciclo de vida do cdev (alocar, nomear, attach, detach, liberar), o que não é uma coincidência; tanto a pilha de rede quanto o devfs evoluíram a partir do mesmo padrão de interface de registro.

### Um Percurso Usando `disc(4)`

O exemplo mais simples de um driver `ifnet` presente na árvore de código é o `disc(4)`, a interface de descarte. O `disc(4)` aceita pacotes e os descarta silenciosamente; seu código de driver é, portanto, composto principalmente pela estrutura de integração, sem lógica de protocolo que distraia o leitor. O driver completo está em `/usr/src/sys/net/if_disc.c`.

A função relevante é `disc_clone_create`, chamada sempre que o operador executa `ifconfig disc create`:

```c
static int
disc_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
        struct ifnet     *ifp;
        struct disc_softc *sc;

        sc = malloc(sizeof(struct disc_softc), M_DISC, M_WAITOK | M_ZERO);
        ifp = sc->sc_ifp = if_alloc(IFT_LOOP);
        ifp->if_softc = sc;
        if_initname(ifp, discname, unit);
        ifp->if_mtu = DSMTU;
        ifp->if_flags = IFF_LOOPBACK | IFF_MULTICAST;
        ifp->if_drv_flags = IFF_DRV_RUNNING;
        ifp->if_ioctl = discioctl;
        ifp->if_output = discoutput;
        ifp->if_hdrlen = 0;
        ifp->if_addrlen = 0;
        ifp->if_snd.ifq_maxlen = 20;
        if_attach(ifp);
        bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));

        return (0);
}
```

Passo a passo:

`malloc` aloca o softc do driver com `M_WAITOK | M_ZERO`. O flag waitok é permitido porque o clone-create executa em um contexto que admite suspensão. O flag zero inicializa a estrutura com zeros, o que permite ao driver presumir que qualquer campo não definido explicitamente vale zero ou NULL.

`if_alloc(IFT_LOOP)` aloca o `ifnet` do pool da pilha de rede. O argumento `IFT_LOOP` identifica o tipo da interface, que a pilha usa para relatórios no estilo SNMP e para alguns comportamentos padrão. Outros tipos comuns são `IFT_ETHER` (para drivers Ethernet) e `IFT_TUNNEL` (para pseudo-dispositivos de tunelamento).

`if_initname` define o nome visível ao usuário. `discname` é a string `"disc"`, e `unit` é o número de unidade fornecido pelo framework de clonagem. Juntos, formam `disc0`, `disc1`, e assim por diante.

As linhas seguintes preenchem os callbacks e os dados por interface: o MTU, os flags, o handler de ioctl (`discioctl`), a função de saída (`discoutput`), o comprimento máximo da fila de envio, e assim por diante. Isso é o equivalente de rede da tabela `cdevsw` da Seção 2; a diferença é que ela é preenchida em um objeto por interface, em vez de em uma tabela estática.

`if_attach(ifp)` torna a interface visível no espaço do usuário. Após o retorno dessa chamada, `ifconfig disc0` funciona, a interface aparece em `netstat -i`, e os protocolos podem se associar a ela.

`bpfattach(ifp, DLT_NULL, ...)` conecta a interface ao mecanismo BPF (Berkeley Packet Filter), que é o que o `tcpdump` lê. `DLT_NULL` declara o tipo de camada de enlace como "sem camada de enlace", adequado para um loopback. Um driver Ethernet chamaria `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)`. Sem `bpfattach`, o `tcpdump` não consegue ver o tráfego da interface, mesmo que a interface em si funcione normalmente.

O caminho de destruição espelha o caminho de criação, na ordem inversa:

```c
static void
disc_clone_destroy(struct ifnet *ifp)
{
        struct disc_softc *sc;

        sc = ifp->if_softc;

        bpfdetach(ifp);
        if_detach(ifp);
        if_free(ifp);

        free(sc, M_DISC);
}
```

`bpfdetach` primeiro, porque o `tcpdump` pode ter uma referência ativa. `if_detach` em seguida, porque a pilha de rede pode ainda estar enfileirando tráfego para a interface; `if_detach` esvazia a fila e remove a interface da lista visível. `if_free` por último, porque o `ifnet` pode ainda ser referenciado por sockets que as camadas superiores ainda não terminaram de liberar; `if_free` adia a liberação efetiva até que a última referência desapareça.

O driver `disc(4)` tem aproximadamente 200 linhas. Um driver Ethernet real se aproxima de 5000, mas o código padrão de integração (allocate, initname, attach, bpfattach, detach, bpfdetach, free) é idêntico. As 4800 linhas extras são detalhes específicos do protocolo: anéis de descritores, handlers de interrupção, gerenciamento de endereços MAC, filtros de multicast, estatísticas, polling de estado do link, e assim por diante. Cada um segue seu próprio padrão, e o Capítulo 28 os aborda em detalhes. A estrutura de integração apresentada aqui é a base de todos eles.

### Como o Operador Enxerga o Resultado

Assim que `disc_clone_create` retorna com sucesso, o operador pode manipular a interface pelo shell:

```console
$ sudo ifconfig disc create
$ ifconfig disc0
disc0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> metric 0 mtu 1500
$ sudo ifconfig disc0 inet 169.254.99.99/32
$ sudo tcpdump -i disc0 &
$ ping -c1 169.254.99.99
... ping output ...
$ sudo ifconfig disc destroy
```

Cada um desses comandos aciona uma parte diferente da integração:

* `ifconfig disc create` chama `disc_clone_create`, que constrói o `ifnet` e o conecta à pilha.
* `ifconfig disc0` lê as flags e o MTU do `ifnet` por meio dos acessores de `if_t`.
* `ifconfig disc0 inet 169.254.99.99/32` chama `discioctl` com `SIOCAIFADDR`, o ioctl que adiciona um endereço.
* `tcpdump -i disc0` abre o tap BPF que `bpfattach` criou.
* `ping -c1` envia um pacote que é roteado por `discoutput`, descartado e nunca retorna.
* `ifconfig disc destroy` chama `disc_clone_destroy`, que desconecta e libera tudo.

Toda a integração é visível no nível do espaço do usuário. Nenhuma parte da maquinaria de protocolo subjacente precisou mudar para acomodar o novo driver; o framework da pilha de rede já tinha um slot reservado para ele.

### Para Que Este Padrão Se Generaliza

O mesmo padrão de registro se aplica a muitos outros subsistemas:

* A **pilha de som** (`sys/dev/sound`) usa `pcm_register` e `pcm_unregister` para tornar um dispositivo de som visível. O driver preenche callbacks para reprodução de buffer, acesso ao mixer e configuração de canal.
* A **pilha USB** (`sys/dev/usb`) usa `usb_attach` e `usb_detach` para registrar drivers de dispositivos USB. O driver preenche callbacks para configuração de transferência, requisições de controle e desconexão.
* O **framework de I/O GEOM** (`sys/geom`) usa `g_attach` e `g_detach` para registrar provedores e consumidores de armazenamento. O driver preenche callbacks para início de I/O, conclusão e tratamento de orphaning.
* O **framework CAM SIM** (`sys/cam`) usa `cam_sim_alloc` e `xpt_bus_register` para registrar adaptadores de armazenamento. A Seção 6 percorre esse processo com mais detalhes.
* O **sistema de despacho de métodos kobj** (que já vimos por trás de `device_method_t`) é, ele próprio, um framework de registro: o driver declara uma tabela de métodos e o subsistema kobj despacha as chamadas por meio dela.

Em todos os casos os passos são os mesmos: alocar o objeto do framework, preencher os callbacks, chamar a função de registro, processar o tráfego e desregistrar de forma limpa. O vocabulário muda, mas o ritmo não.

### Encerrando a Seção 5

A Seção 5 usou a pilha de rede para ilustrar uma integração baseada em registro. O driver aloca um `ifnet`, nomeia-o, preenche callbacks, conecta-o à pilha, conecta-o ao BPF, processa o tráfego e desfaz tudo em ordem inversa no momento da destruição. O padrão é pequeno e bem delimitado; a maquinaria de protocolo fica por trás dele e é o assunto do Capítulo 27.

O leitor que não estiver escrevendo um driver de rede obtém um benefício real desta seção mesmo sem aplicá-la ao `myfirst`: toda outra integração baseada em registro no kernel FreeBSD segue o mesmo formato. Uma vez internalizado o ritmo de alocar-nomear-preencher-conectar-processar-desconectar-liberar, a pilha de armazenamento da Seção 6 parecerá familiar à primeira vista.

Na Seção 6 aplicamos a mesma perspectiva à pilha de armazenamento CAM. O vocabulário muda (`cam_sim`, `xpt_bus_register`, `xpt_action`, CCBs), mas a forma de registro é a mesma.

## Seção 6: Integração com o Armazenamento CAM (Opcional)

### Por Que Esta Seção É Opcional

`myfirst` não é um adaptador de armazenamento e não se tornará um. O leitor que estiver integrando um driver que não seja de armazenamento deve percorrer esta seção rapidamente para absorver o vocabulário, observar que o formato de registro espelha o da Seção 5 e continuar para a Seção 7.

O leitor que estiver integrando um adaptador de armazenamento (um host bus adapter SCSI, um controlador NVMe, um controlador de armazenamento virtual emulado) encontrará aqui o esqueleto de como o CAM espera que um driver se comunique com ele. A superfície completa do protocolo é grande o suficiente para preencher um capítulo inteiro e é o tema do Capítulo 27; o que cobrimos aqui é apenas o enquadramento da integração, idêntico em espírito ao enquadramento com `if_alloc` / `if_attach` que usamos para a rede.

### O Que É o CAM

CAM (Common Access Method) é o subsistema de armazenamento do FreeBSD acima da camada de driver de dispositivo. Ele gerencia a fila de requisições de I/O pendentes, a noção abstrata de um target e um número de unidade lógica (LUN), a lógica de roteamento de caminho que envia uma requisição ao adaptador correto, e o conjunto de drivers de periféricos genéricos (`da(4)` para discos, `cd(4)` para ópticos, `sa(4)` para fita) que transformam I/O de bloco em comandos específicos de protocolo. O driver fica abaixo do CAM e é responsável apenas pelo trabalho específico do adaptador: enviar comandos ao hardware e reportar as conclusões.

O vocabulário que o CAM usa é pequeno, mas preciso:

* Um **SIM** (SCSI Interface Module) é a visão do framework sobre um adaptador de armazenamento. O driver aloca um com `cam_sim_alloc`, preenche um callback (a função de ação) e o registra com `xpt_bus_register`. O SIM é o análogo do `ifnet` para a pilha de armazenamento.
* Um **CCB** (CAM Control Block) é uma única requisição de I/O. O CAM entrega um CCB ao driver por meio do callback de ação; o driver inspeciona o `func_code` do CCB, executa a ação solicitada, preenche o resultado e devolve o CCB ao CAM com `xpt_done`. Os CCBs são o análogo dos `mbuf`s para a pilha de armazenamento, com a diferença de que um CCB carrega tanto a requisição quanto a resposta.
* Um **path** identifica um destino como um triplo `(bus, target, LUN)`. O driver chama `xpt_create_path` para construir um path que pode usar para eventos assíncronos.
* O **XPT** (Transport Layer) é o mecanismo central de despacho do CAM. O driver chama `xpt_action` para enviar um CCB ao CAM (ou a si mesmo, para ações auto-direcionadas); o CAM eventualmente chama de volta a função de ação do driver para os CCBs de I/O direcionados ao bus do driver.

### O Ciclo de Vida do Registro

Para um adaptador de canal único, os passos de registro são:

1. Alocar uma fila de dispositivos CAM com `cam_simq_alloc(maxq)`.
2. Alocar um SIM com `cam_sim_alloc(action, poll, "name", softc, unit, mtx, max_tagged, max_dev_transactions, devq)`.
3. Bloquear o mutex do driver.
4. Registrar o SIM com `xpt_bus_register(sim, dev, 0)`.
5. Criar um path que o driver possa usar para eventos: `xpt_create_path(&path, NULL, cam_sim_path(sim), CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD)`.
6. Desbloquear o mutex.

A limpeza é feita em ordem inversa:

1. Bloquear o mutex do driver.
2. Liberar o path com `xpt_free_path(path)`.
3. Desregistrar o SIM com `xpt_bus_deregister(cam_sim_path(sim))`.
4. Liberar o SIM com `cam_sim_free(sim, TRUE)`. O argumento `TRUE` instrui o CAM a liberar também o devq subjacente; passe `FALSE` se o driver quiser reter o devq para reutilização.
5. Desbloquear o mutex.

O driver `ahci(4)` em `/usr/src/sys/dev/ahci/ahci.c` é um bom exemplo do mundo real. Seu caminho de attach de canal inclui a sequência canônica:

```c
ch->sim = cam_sim_alloc(ahciaction, ahcipoll, "ahcich", ch,
    device_get_unit(dev), (struct mtx *)&ch->mtx,
    (ch->quirks & AHCI_Q_NOCCS) ? 1 : min(2, ch->numslots),
    (ch->caps & AHCI_CAP_SNCQ) ? ch->numslots : 0,
    devq);
if (ch->sim == NULL) {
        cam_simq_free(devq);
        device_printf(dev, "unable to allocate sim\n");
        error = ENOMEM;
        goto err1;
}
if (xpt_bus_register(ch->sim, dev, 0) != CAM_SUCCESS) {
        device_printf(dev, "unable to register xpt bus\n");
        error = ENXIO;
        goto err2;
}
if (xpt_create_path(&ch->path, NULL, cam_sim_path(ch->sim),
    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
        device_printf(dev, "unable to create path\n");
        error = ENXIO;
        goto err3;
}
```

Os rótulos `goto` (`err1`, `err2`, `err3`) alimentam uma única seção de limpeza que desfaz tudo o que foi alocado até aquele ponto. Esse é o padrão padrão de driver FreeBSD para tratamento de falhas e é exatamente a disciplina que a Seção 7 vai codificar.

### O Callback de Ação

O callback de ação é o coração de um driver CAM. Sua assinatura é `void action(struct cam_sim *sim, union ccb *ccb)`. O driver inspeciona `ccb->ccb_h.func_code` e despacha:

```c
static void
mydriver_action(struct cam_sim *sim, union ccb *ccb)
{
        struct mydriver_softc *sc;

        sc = cam_sim_softc(sim);

        switch (ccb->ccb_h.func_code) {
        case XPT_SCSI_IO:
                mydriver_start_io(sc, ccb);
                /* completion is asynchronous; xpt_done called later */
                return;

        case XPT_RESET_BUS:
                mydriver_reset_bus(sc);
                ccb->ccb_h.status = CAM_REQ_CMP;
                break;

        case XPT_PATH_INQ: {
                struct ccb_pathinq *cpi = &ccb->cpi;

                cpi->version_num = 1;
                cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE;
                cpi->target_sprt = 0;
                cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
                cpi->hba_eng_cnt = 0;
                cpi->max_target = 0;
                cpi->max_lun = 7;
                cpi->initiator_id = 7;
                strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
                strncpy(cpi->hba_vid, "MyDriver", HBA_IDLEN);
                strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
                cpi->unit_number = cam_sim_unit(sim);
                cpi->bus_id = cam_sim_bus(sim);
                cpi->ccb_h.status = CAM_REQ_CMP;
                break;
        }

        default:
                ccb->ccb_h.status = CAM_REQ_INVALID;
                break;
        }

        xpt_done(ccb);
}
```

Três ramificações ilustram os padrões:

`XPT_SCSI_IO` é o caminho de dados. O driver inicia um I/O assíncrono (escrevendo descritores no hardware, programando DMA e assim por diante) e retorna imediatamente sem chamar `xpt_done`. O hardware conclui o I/O alguns milissegundos depois, dispara uma interrupção, o handler de interrupção calcula o resultado, preenche o status do CCB e somente então chama `xpt_done`. O CAM não exige conclusão síncrona; o driver pode levar o tempo que o hardware levar.

`XPT_RESET_BUS` é um controle síncrono. O driver executa o reset, define `CAM_REQ_CMP` e cai diretamente para `xpt_done`. Não há componente assíncrono.

`XPT_PATH_INQ` é a auto-descrição do SIM. Na primeira vez que o CAM sonda o SIM, ele emite `XPT_PATH_INQ` e lê de volta as características do bus: LUN máximo, flags suportadas, identificadores de fabricante e assim por diante. O driver preenche a estrutura e retorna. Sem uma resposta correta a `XPT_PATH_INQ`, o CAM não consegue sondar os targets por trás do SIM e o driver aparece como registrado, mas inerte.

O ramo `default` retorna `CAM_REQ_INVALID` para qualquer código de função que o driver não implemente. O CAM é tolerante quanto a isso; ele simplesmente trata a requisição como não suportada e ou recorre a uma implementação genérica ou propaga o erro para o driver de periférico.

### Como o Operador Enxerga o Resultado

Assim que um driver com CAM chama `xpt_bus_register`, o CAM sonda o bus e o resultado visível ao usuário é uma ou mais entradas em `camcontrol devlist`:

```console
$ camcontrol devlist
<MyDriver Volume 1.0>             at scbus0 target 0 lun 0 (pass0,da0)
$ ls /dev/da0
/dev/da0
$ diskinfo /dev/da0
/dev/da0   512 ... ...
```

O dispositivo `da0` em `/dev` é um driver de periférico CAM (`da(4)`) envolvendo o LUN que o CAM descobriu por trás do SIM. O operador nunca lida diretamente com o SIM; ele vê apenas a interface padrão `/dev/daN` que todo dispositivo de blocos usa. É isso que torna o CAM um alvo de integração tão produtivo: escreva um SIM e ganhe I/O completo no estilo de disco de graça.

### Reconhecendo o Padrão

A esta altura o leitor já deve enxergar o mesmo formato que vimos na Seção 5:

| Etapa                  | Rede                        | CAM                          |
|------------------------|-----------------------------|------------------------------|
| Alocar objeto          | `if_alloc`                  | `cam_sim_alloc`              |
| Nomear e configurar    | `if_initname`, definir callbacks | implícito nos argumentos de `cam_sim_alloc` |
| Conectar ao framework  | `if_attach`                 | `xpt_bus_register`           |
| Tornar detectável      | `bpfattach`                 | `xpt_create_path`            |
| Processar tráfego      | callback `if_output`        | callback de ação             |
| Concluir uma operação  | (síncrono)                  | `xpt_done(ccb)`              |
| Desconectar            | `bpfdetach`, `if_detach`    | `xpt_free_path`, `xpt_bus_deregister` |
| Liberar                | `if_free`                   | `cam_sim_free`               |

Outras interfaces de registro (`pcm_register` para som, `usb_attach` para USB, `g_attach` para GEOM) seguem a mesma estrutura de colunas, cada uma com seu próprio vocabulário. Uma vez que o leitor vê essa tabela pela primeira vez, cada integração subsequente se torna apenas uma questão de buscar os nomes corretos.

### Encerrando a Seção 6

A Seção 6 esboçou a interface de registro para um CAM SIM. O driver aloca um SIM com `cam_sim_alloc`, registra-o com `xpt_bus_register`, cria um path para eventos, recebe I/O pelo callback de ação, conclui o I/O com `xpt_done` e desregistra tudo em ordem inversa no detach. O mesmo padrão de integração baseado em registro que vimos com `ifnet` se aplica aqui, com a mudança óbvia de vocabulário.

O leitor viu agora três superfícies de integração (devfs, ioctl, sysctl) de que quase todo driver precisa, e duas superfícies baseadas em registro (rede e CAM) de que alguns drivers precisam. Na Seção 7 daremos um passo atrás e codificaremos a disciplina de ciclo de vida que mantém tudo unido: a ordem de registro no attach, a ordem de desmontagem no detach, e o pequeno conjunto de padrões que distingue um driver que carrega, executa e descarrega de forma limpa daquele que vaza recursos ou entra em pânico no detach.

## Seção 7: Disciplina de Registro, Desmontagem e Limpeza

### A Regra Fundamental

Um driver que se integra ao kernel por meio de vários frameworks (devfs para `/dev`, sysctl para parâmetros ajustáveis, `ifnet` para rede, CAM para armazenamento, callouts para temporizadores, taskqueues para trabalho diferido e assim por diante) acumula um pequeno zoológico de objetos alocados e callbacks registrados. Todos eles compartilham a mesma propriedade: devem ser liberados na ordem inversa à da criação. Esquecer isso transforma um detach limpo em um kernel panic, vaza recursos no descarregamento do módulo e espalha ponteiros inválidos pelos subsistemas que o driver não mais gerencia.

A regra fundamental da integração é, portanto, muito simples de enunciar, mesmo que aplicá-la de forma limpa exija cuidado:

> **Todo registro bem-sucedido deve ser pareado com um cancelamento de registro. A ordem dos cancelamentos de registro é o inverso da ordem dos registros. Um registro com falha deve disparar o cancelamento de registro de todos os registros anteriores bem-sucedidos antes que a função retorne o erro.**

Essa única frase descreve toda a disciplina de ciclo de vida. O restante desta seção é um percurso guiado de como aplicá-la.

### Por Que Ordem Inversa

A regra da ordem inversa pode parecer arbitrária, mas não é. Cada registro é uma promessa ao framework de que "desde agora até eu chamar o deregistro, você pode fazer callbacks para mim, depender do meu estado ou me entregar trabalho." Um framework que possui um callback para o driver e que mantém trabalho pendente para ele não pode ser desmontado com segurança enquanto outro framework ainda tiver acesso ao mesmo estado.

Por exemplo, suponha que o driver registre um callout, depois um cdev, depois um OID de sysctl. O callback `read` do cdev pode consultar um valor que o callout atualiza; o callout, por sua vez, pode ler um estado que o OID de sysctl expõe. Se o detach desmontar o callout primeiro, então enquanto o cdev está sendo desmontado, uma chamada `read` do espaço do usuário poderia tentar consultar um valor que o callout deveria manter atualizado; o valor estaria desatualizado e o read retornaria lixo. Se o detach desmontar o cdev primeiro, não há como `read` chegar, e o callout pode ser cancelado com segurança. A ordem importa.

A regra geral é: desmonte antes os elementos que podem fazer chamadas de volta para você, e só então desmonte aquilo de que eles dependem.

Para a maioria dos drivers, a cadeia de dependências é a mesma que a ordem de criação:

* O cdev depende do softc (os callbacks do cdev desreferenciam `si_drv1`).
* Os OIDs de sysctl dependem do softc (eles apontam para campos do softc).
* Os callouts e taskqueues dependem do softc (recebem um ponteiro para o softc como argumento).
* Os handlers de interrupção dependem do softc, dos locks e de quaisquer tags de DMA.
* As tags de DMA e os recursos de barramento dependem do dispositivo.

Se o driver os cria nessa ordem, deve destruí-los na ordem exatamente inversa: interrupções primeiro (podem disparar a qualquer momento), depois callouts e taskqueues (executam a qualquer momento), depois cdevs (recebem chamadas do espaço do usuário), depois OIDs de sysctl (o framework os limpa automaticamente), depois DMA, depois recursos de barramento, depois locks. O próprio softc é a última coisa a ser liberada.

### O Padrão `goto err1` no Attach

O lugar mais difícil para aplicar essa regra é no attach, quando uma falha parcial pode deixar o driver parcialmente inicializado. O padrão canônico do FreeBSD é a cadeia de rótulos `goto`, cada um representando a limpeza necessária até aquele ponto:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);

        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0)
                goto fail_mtx;

        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT, "attach: stage 3 complete\n");
        return (0);

fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

O único rótulo de erro aqui existe porque há apenas um ponto em que uma falha real pode ocorrer (a chamada `make_dev_s`). Um driver mais elaborado teria um rótulo por etapa de registro. Por convenção, cada rótulo recebe o nome da etapa que falhou (`fail_mtx`, `fail_cdev`, `fail_sysctl`), e cada rótulo executa a limpeza de todas as etapas **acima** dele na função. O rótulo que trata a última falha possível tem a limpeza mais longa; o que trata a primeira falha tem a mais curta.

Um attach de quatro etapas para um driver de hardware hipotético ficaria assim:

```c
static int
mydriver_attach(device_t dev)
{
        struct mydriver_softc *sc = device_get_softc(dev);
        int error;

        mtx_init(&sc->sc_mtx, "mydriver", NULL, MTX_DEF);

        error = bus_alloc_resource_any(...);
        if (error != 0)
                goto fail_mtx;

        error = bus_setup_intr(...);
        if (error != 0)
                goto fail_resource;

        error = make_dev_s(...);
        if (error != 0)
                goto fail_intr;

        return (0);

fail_intr:
        bus_teardown_intr(...);
fail_resource:
        bus_release_resource(...);
fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

Os rótulos são lidos de cima para baixo na mesma ordem em que as ações de limpeza são executadas. Uma falha em qualquer etapa salta para o rótulo correspondente e desce em cascata pelos rótulos de limpeza de todas as etapas anteriores bem-sucedidas. O padrão é tão comum que ler código de driver sem ele causa estranheza; revisores esperam vê-lo.

### O Espelho do Detach

O detach deve ser o espelho exato de um attach bem-sucedido. Cada registro feito no attach deve ter um deregistro correspondente no detach, em ordem inversa:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0) {
                mtx_unlock(&sc->sc_mtx);
                return (EBUSY);
        }
        mtx_unlock(&sc->sc_mtx);

        DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

        /*
         * destroy_dev drains any in-flight cdevsw callbacks. After
         * this call returns, no new open/close/read/write/ioctl can
         * arrive, and no in-flight callback is still running.
         */
        destroy_dev(sc->sc_cdev);

        /*
         * The per-device sysctl context is torn down automatically by
         * the framework after detach returns successfully. Nothing to
         * do here.
         */

        mtx_destroy(&sc->sc_mtx);
        return (0);
}
```

O detach começa verificando `open_count` sob o mutex; se alguém mantiver o dispositivo aberto, o detach recusa (retornando `EBUSY`) para que o operador receba um erro claro em vez de um panic. Após a verificação, a função desmonta tudo o que o attach alocou, em ordem inversa: cdev primeiro, depois sysctl (automático), depois mutex.

O retorno antecipado de `EBUSY` é o padrão de detach "soft". Ele coloca a responsabilidade de fechar o dispositivo no operador: `kldunload myfirst` falhará até que o operador execute `pkill cat` (ou o que quer que esteja mantendo o dispositivo aberto). A alternativa é o padrão "hard", que recusa o detach apenas se um recurso crítico estiver em uso e aceita que descritores de arquivo comuns são responsabilidade do kernel para drenar. O padrão "hard" é mais complexo (geralmente requer `dev_ref` e `dev_rel`) e é deixado como tópico para a seção de driver CAM do Capítulo 27.

### Handlers de Eventos de Módulo

Até este ponto, discutimos `attach` e `detach`, os hooks de ciclo de vida por dispositivo que o Newbus chama quando uma instância de driver é adicionada ou removida. Há também um ciclo de vida por módulo, controlado pela função registrada por meio de `DRIVER_MODULE` (ou `MODULE_VERSION` mais um `DECLARE_MODULE`). O kernel chama essa função nos eventos `MOD_LOAD`, `MOD_UNLOAD` e `MOD_SHUTDOWN`.

Para a maioria dos drivers, o hook por módulo não é utilizado. `DRIVER_MODULE` aceita um handler de eventos NULL por padrão, e o kernel faz a coisa certa: no `MOD_LOAD` ele adiciona o driver à lista de drivers do barramento, e no `MOD_UNLOAD` ele percorre o barramento e desanexa cada instância. O autor do driver escreve apenas `attach` e `detach`.

Alguns drivers, no entanto, precisam de um hook em nível de módulo. O caso clássico é um driver que precisa configurar um recurso global (uma tabela hash global, um mutex global, um handler de eventos global) compartilhado entre todas as instâncias. O hook para isso é:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
        switch (what) {
        case MOD_LOAD:
                /* allocate global state */
                return (0);
        case MOD_UNLOAD:
                /* free global state */
                return (0);
        case MOD_SHUTDOWN:
                /* about to power off; flush anything important */
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t myfirst_mod = {
        "myfirst", myfirst_modevent, NULL
};
DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(myfirst, 1);
```

O driver `myfirst` neste capítulo não possui estado global e, portanto, não precisa de um `modevent`. A maquinaria padrão do `DRIVER_MODULE` é suficiente. Mencionamos o hook aqui para que o leitor possa reconhecê-lo em drivers maiores.

### `EVENTHANDLER` para Eventos do Sistema

Alguns drivers se interessam por eventos que ocorrem em outras partes do kernel: um processo está sendo bifurcado (fork), o sistema está sendo encerrado, a rede está mudando de estado, e assim por diante. O mecanismo `EVENTHANDLER` permite que um driver registre um callback para um evento nomeado:

```c
static eventhandler_tag myfirst_eh_tag;

static void
myfirst_shutdown_handler(void *arg, int howto)
{
        /* called when the system is shutting down */
}

/* In attach: */
myfirst_eh_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    myfirst_shutdown_handler, sc, EVENTHANDLER_PRI_ANY);

/* In detach: */
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, myfirst_eh_tag);
```

Os nomes de eventos `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final` e `vm_lowmem` são os mais usados em drivers. Cada um é um ponto de hook documentado e cada um tem sua própria semântica sobre o que o driver pode fazer (dormir, alocar memória, adquirir locks, comunicar-se com o hardware) dentro do callback.

A regra cardinal se aplica aos handlers de eventos exatamente como se aplica a todo o resto: todo `EVENTHANDLER_REGISTER` bem-sucedido deve ser pareado com um `EVENTHANDLER_DEREGISTER` na ordem inversa. Esquecer o deregistro deixa um ponteiro de função pendente na tabela de handlers de eventos; na próxima vez que o evento disparar após o descarregamento do módulo, o kernel pulará para memória já liberada e causará um panic.

### `SYSINIT` para Inicialização Única do Kernel

Mais um mecanismo que vale conhecer é o `SYSINIT(9)`, o mecanismo de inicialização única registrado em tempo de compilação do kernel. Uma declaração `SYSINIT` no código do driver:

```c
static void
myfirst_sysinit(void *arg __unused)
{
        /* runs once, very early at kernel boot */
}
SYSINIT(myfirst_init, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    myfirst_sysinit, NULL);
```

declara uma função que é executada em um ponto específico durante a inicialização do kernel, antes que qualquer processo do espaço do usuário exista. `SYSINIT` raramente é necessário em código de driver; a função não é reexecutada quando o módulo é recarregado, portanto não dá ao driver a oportunidade de configurar o estado por carregamento. A maioria dos drivers que acredita precisar de `SYSINIT` na verdade quer um handler de eventos `MOD_LOAD`.

A declaração correspondente `SYSUNINIT(9)`:

```c
SYSUNINIT(myfirst_uninit, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    myfirst_sysuninit, NULL);
```

declara uma função que é executada no ponto de desmontagem correspondente. A ordem de declaração importa: `SI_SUB_DRIVERS` é executado após `SI_SUB_VFS`, mas antes de `SI_SUB_KICK_SCHEDULER`, portanto um `SYSINIT` neste nível já pode usar o sistema de arquivos, mas ainda não pode escalonar processos.

### `bus_generic_detach` e `device_delete_children`

Drivers que são eles próprios barramentos (um driver de ponte PCI para PCI, um driver de hub USB, um driver virtual estilo barramento) têm dispositivos filho anexados a eles. Para desanexar o pai, é necessário primeiro desanexar todos os filhos, na ordem correta. O framework fornece dois auxiliares:

`bus_generic_detach(dev)` percorre os filhos do dispositivo e chama `device_detach` em cada um. Retorna 0 se todos os filhos foram desanexados com sucesso, ou o primeiro código de retorno não nulo se algum filho recusou.

`device_delete_children(dev)` chama `bus_generic_detach` e depois `device_delete_child` para cada filho, liberando as estruturas de dispositivo filho.

O detach de um driver estilo barramento deve sempre começar com um desses dois:

```c
static int
mybus_detach(device_t dev)
{
        int error;

        error = bus_generic_detach(dev);
        if (error != 0)
                return (error);

        /* now safe to tear down per-bus state */
        ...
        return (0);
}
```

Se o driver desmontar seu estado de barramento antes de desanexar os filhos, eles tentarão acessar os recursos do pai que já foram liberados, o que causará um crash. A ordem é, portanto: desanexar os filhos primeiro (bus_generic_detach), depois desmontar o estado por barramento.

### Juntando Tudo

A disciplina do ciclo de vida pode ser resumida em uma pequena lista de verificação que todo driver deve satisfazer:

1. **Toda alocação tem uma liberação correspondente.** Controle isso com uma cadeia `goto err` no attach e uma ordem espelhada no detach.
2. **Todo registro tem um deregistro correspondente.** Isso se aplica igualmente a cdevs, sysctls, callouts, taskqueues, handlers de eventos, handlers de interrupção, tags de DMA e recursos de barramento.
3. **A ordem de desmontagem é o inverso da ordem de configuração.** Um driver que viola isso vazará, causará panic, ou ambos.
4. **A função detach recusa a operação se qualquer recurso visível externamente ainda estiver em uso.** `EBUSY` é o código de retorno correto.
5. **A função detach nunca libera o softc; o framework faz isso automaticamente após o detach retornar com sucesso.**
6. **O cdev é destruído (não liberado) com `destroy_dev`, e `destroy_dev` bloqueia até que os callbacks em andamento retornem.**
7. **O contexto sysctl por dispositivo é desmontado automaticamente; o driver não chama `sysctl_ctx_free` para isso.**
8. **Drivers estilo barramento desanexam seus filhos primeiro com `bus_generic_detach` ou `device_delete_children`, depois desmontam o estado por barramento.**
9. **Um attach que falhou desfaz todas as etapas anteriores antes de retornar o código de falha.**
10. **O kernel nunca vê um driver parcialmente anexado: o attach ou tem sucesso completo ou falha completamente.**

O driver `myfirst` do estágio 3 satisfaz todos os itens desta lista; o laboratório da Seção 9 faz o leitor injetar uma falha deliberada para ver o desenrolamento em ação.

### Encerrando a Seção 7

A Seção 7 codificou a disciplina de ciclo de vida que mantém todas as seções anteriores unidas. A cadeia `goto err` no attach e a desmontagem em ordem inversa no detach são os dois padrões que o leitor usará em todo driver que escrever daqui em diante. Hooks em nível de módulo (`MOD_LOAD`, `MOD_UNLOAD`), registro de handlers de eventos (`EVENTHANDLER_REGISTER`) e detach estilo barramento (`bus_generic_detach`) são as variações que alguns drivers precisam; para um pseudo-driver de instância única como `myfirst`, o par attach/detach básico mais a cadeia `goto err` é suficiente.

Na Seção 8, recuamos para o outro meta-tópico do capítulo: como o driver evoluiu da versão `1.0` na Parte II, passando por `1.5-channels` na Parte III, `1.6-debug` no Capítulo 23, e agora `1.7-integration` aqui, e como essa evolução deve ser visível em comentários do código-fonte, na declaração `MODULE_VERSION` e em lugares visíveis ao usuário, como o OID de sysctl `version`. O leitor deixará o Capítulo 24 não apenas com um driver totalmente integrado, mas com uma disciplina sobre como o número de versão de um driver comunica ao leitor o que esperar.

## Seção 8: Refatoração e Versionamento

### Um Driver Tem Uma História

O driver `myfirst` não surgiu completamente formado. Ele começou na Parte II como uma demonstração de arquivo único de como `DRIVER_MODULE` funciona, cresceu na Parte III para suportar múltiplas instâncias e estado por canal, ganhou infraestrutura de debug e rastreamento no Capítulo 23, e adquire sua superfície de integração completa neste capítulo. Cada etapa deixou o código-fonte maior e mais capaz.

Drivers na árvore do FreeBSD têm histórias igualmente longas. `null(4)` remonta a 1982; seu `cdevsw` foi refatorado pelo menos três vezes para acomodar a evolução do kernel, mas seu comportamento visível ao usuário permanece inalterado. `if_ethersubr.c` é anterior ao IPv6 e sua API ganhou novas funções a cada versão enquanto as legadas permaneceram no lugar. A arte da manutenção de drivers é, em parte, saber como estender um driver sem quebrar o que existia antes.

Esta seção é uma breve pausa para conversar sobre três disciplinas intimamente relacionadas: como refatorar um driver à medida que ele cresce, como expressar a versão em que ele se encontra e como decidir o que constitui uma mudança incompatível. O exemplo prático do capítulo é a transição de `1.6-debug` (o final do Capítulo 23) para `1.7-integration` (o final deste capítulo), mas os padrões se generalizam para qualquer projeto de driver.

### De Um Arquivo para Vários

O driver `myfirst` do Capítulo 23 era uma pequena mas real árvore de código-fonte:

```text
myfirst.c          /* probe, attach, detach, cdevsw, read, write */
myfirst.h          /* softc, function declarations */
myfirst_debug.c    /* SDT provider definition */
myfirst_debug.h    /* DPRINTF, debug class bits */
Makefile
```

O estágio 3 deste capítulo adiciona dois novos arquivos-fonte:

```text
myfirst_ioctl.c    /* ioctl dispatcher */
myfirst_ioctl.h    /* PUBLIC ioctl interface for user space */
myfirst_sysctl.c   /* sysctl OID construction */
```

A decisão de dividir cada nova responsabilidade em seu próprio par de arquivos é deliberada. Um único `myfirst.c` com 2000 linhas compilaria, carregaria e funcionaria, mas também seria mais difícil de ler, mais difícil de testar e mais difícil para um co-mantenedor navegar. Dividir ao longo de linhas de responsabilidade (open/close vs ioctl vs sysctl vs debug) permite que cada arquivo caiba em uma tela e que o leitor compreenda uma responsabilidade por vez.

O padrão é aproximadamente o seguinte:

* `<driver>.c` contém probe, attach, detach, a estrutura cdevsw e o pequeno conjunto de callbacks do cdevsw (open, close, read, write).
* `<driver>.h` contém o softc, declarações de funções compartilhadas entre os arquivos e quaisquer constantes privadas. **Não** é incluído pelo espaço do usuário.
* `<driver>_debug.c` e `<driver>_debug.h` contêm o provider SDT, a macro DPRINTF e a enumeração de classes de debug. **Não** são incluídos pelo espaço do usuário.
* `<driver>_ioctl.c` contém o dispatcher de ioctl. `<driver>_ioctl.h` é o cabeçalho **público**, inclui apenas `sys/types.h` e `sys/ioccom.h`, e é seguro para ser incluído por código em espaço do usuário.
* `<driver>_sysctl.c` contém a construção do OID sysctl. **Não** é incluído pelo espaço do usuário.

A divisão entre cabeçalhos públicos e privados importa por dois motivos. Primeiro, cabeçalhos públicos devem compilar de forma limpa sem contexto de kernel (`_KERNEL` não está definido quando o espaço do usuário os inclui); um cabeçalho que carrega `sys/lock.h` e `sys/mutex.h` falhará ao compilar em um build de espaço do usuário. Segundo, cabeçalhos públicos fazem parte do contrato do driver com o espaço do usuário e precisam ser instaláveis em um local de sistema, como `/usr/local/include/myfirst/myfirst_ioctl.h`. Um cabeçalho privado que acidentalmente se torna público é uma armadilha de manutenção: todo programa em espaço do usuário que o inclui fixa o layout interno do driver, e qualquer refatoração futura os quebra.

O cabeçalho `myfirst_ioctl.h` neste capítulo é o único cabeçalho público do driver. Ele é pequeno, autocontido e usa apenas tipos estáveis.

### Strings de Versão, Números de Versão e a Versão da API

Um driver carrega três versões diferentes, cada uma com um significado distinto.

A **versão de release** é a string legível por humanos impressa no `dmesg`, exposta por meio de `dev.<driver>.0.version` e usada em conversas e documentação. O driver `myfirst` usa strings com pontos como `1.6-debug` e `1.7-integration`. O formato é uma convenção; o que importa é que a string seja curta, descritiva e única por release.

A **versão do módulo** é um inteiro declarado com `MODULE_VERSION(<name>, <integer>)`. Ela é usada pelo kernel para impor dependências entre módulos. Um módulo que depende de `myfirst` declara `MODULE_DEPEND(other, myfirst, 1, 1, 1)`, onde os três inteiros são as versões mínima, preferida e máxima aceitáveis. Incrementar a versão do módulo sinaliza: "Quebrei a compatibilidade com versões anteriores; módulos que dependem de mim precisam ser recompilados."

A **versão da API** é o inteiro exposto por meio de `MYFIRSTIOC_GETVER` e armazenado na constante `MYFIRST_IOCTL_VERSION`. Ela é usada por programas em espaço do usuário para detectar divergências na API antes de emitirem ioctls que possam falhar. Incrementar a versão da API sinaliza: "a interface visível ao espaço do usuário mudou de uma forma que programas mais antigos não conseguirão lidar."

As três versões são independentes. O mesmo release pode incrementar apenas a versão da API (porque um novo ioctl foi adicionado) sem incrementar a versão do módulo (porque os dependentes no kernel não são afetados). Por outro lado, uma refatoração que altera o layout de uma estrutura de dados in-kernel exportada pode incrementar a versão do módulo sem incrementar a versão da API, porque o espaço do usuário não percebe nenhuma mudança.

Para o `myfirst`, o capítulo usa estes valores:

```c
/* myfirst_sysctl.c */
#define MYFIRST_VERSION "1.7-integration"

/* myfirst.c */
MODULE_VERSION(myfirst, 1);

/* myfirst_ioctl.h */
#define MYFIRST_IOCTL_VERSION 1
```

O release é `1.7-integration` porque acabamos de incorporar o trabalho de integração. A versão do módulo permanece em `1` porque não existem dependentes no kernel. A versão da API é `1` porque este é o primeiro capítulo que expõe ioctls; o estágio 2 do capítulo introduziu a interface, e qualquer mudança futura no layout do ioctl precisaria incrementá-la.

### Quando Incrementar Cada Uma

A regra para incrementar a **versão de release** é: "toda vez que o driver muda de uma forma que o operador possa se importar." Adicionar uma funcionalidade, mudar o comportamento padrão e corrigir um bug notável são todos casos que se enquadram. A versão de release é para humanos; ela deve mudar com frequência suficiente para que o campo seja informativo.

A regra para incrementar a **versão do módulo** é: "quando usuários do driver no kernel precisariam recompilar para continuar funcionando." Adicionar uma nova função no kernel não é um incremento (os dependentes antigos continuam funcionando). Remover uma função ou alterar sua assinatura é um incremento. Renomear um campo de struct que outros módulos leem é um incremento. Um driver que não exporta nada no kernel pode manter sua versão de módulo em 1 para sempre.

A regra para incrementar a **versão da API** é: "quando um programa existente em espaço do usuário interpretaria incorretamente as respostas do driver ou falharia de uma forma não óbvia." Adicionar um novo ioctl não é um incremento (programas antigos não o utilizam). Alterar o layout da estrutura de argumento de um ioctl existente é um incremento. Renumerar um ioctl existente é um incremento. Um driver que ainda não foi lançado para os usuários pode alterar livremente a versão da API enquanto a interface ainda está sendo projetada; assim que o primeiro usuário tiver feito um release baseado nela, toda mudança é um evento público.

### Shims de Compatibilidade

Um driver amplamente distribuído acumula shims de compatibilidade. A forma clássica é um ioctl de "versão 1" que o driver suporta indefinidamente ao lado de um ioctl de "versão 2" que o substitui. Programas em espaço do usuário que usam a interface v1 continuam funcionando, programas que usam v2 recebem o novo comportamento, e o driver carrega os dois caminhos de código.

O custo dos shims é real. Cada shim é código que precisa ser testado, documentado e mantido. Cada shim também é uma API coberta que restringe refatorações futuras. Um driver com cinco shims é mais difícil de evoluir do que um driver com um.

A disciplina é, portanto, projetar cuidadosamente desde o início para que os shims sejam raros. Três hábitos ajudam:

* **Use constantes nomeadas, não números literais.** Um programa que usa `MYFIRSTIOC_SETMSG` em vez de `0x802004d3` continuará funcionando quando o driver renumerar o ioctl, porque tanto o cabeçalho quanto o programa são reconstruídos com base no novo cabeçalho.
* **Prefira mudanças aditivas em vez de mudanças destrutivas.** Quando o driver precisar expor um novo campo, adicione um novo ioctl em vez de estender uma estrutura existente. O ioctl antigo mantém seu layout; o novo carrega as informações extras.
* **Versione cada estrutura pública.** Um `struct myfirst_v1_args` associado a `MYFIRSTIOC_SETMSG_V1` é uma pequena anotação agora e um grande ganho de compatibilidade no futuro.

O `myfirst` neste capítulo é tão pequeno que ainda não possui nenhum shim. A única concessão do capítulo ao versionamento é o ioctl `MYFIRSTIOC_GETVER`, que oferece a um futuro mantenedor um lugar limpo para adicionar lógica de shim quando chegar a hora.

### Refatoração Guiada: Dividindo o `myfirst.c`

A transição do estágio 3 do Capítulo 23 (debug) para o estágio 3 deste capítulo (sysctl) é em si uma pequena refatoração. O código-fonte inicial tinha um único `myfirst.c` com 1000 linhas e um pequeno `myfirst_debug.c`. O código-fonte final tem o mesmo `myfirst.c` reduzido em cerca de 100 linhas, mais dois novos arquivos (`myfirst_ioctl.c` e `myfirst_sysctl.c`) que absorvem a nova lógica.

Os passos da refatoração foram:

1. Adicionar os dois novos arquivos com a nova lógica.
2. Adicionar as novas declarações de funções em `myfirst.h` para que o cdevsw possa referenciar `myfirst_ioctl`.
3. Atualizar `myfirst.c` para chamar `myfirst_sysctl_attach(sc)` a partir do attach.
4. Atualizar o `Makefile` para listar os novos arquivos em `SRCS`.
5. Construir, carregar, exercitar e verificar que o driver ainda passa por todos os laboratórios do Capítulo 23.
6. Incrementar a versão de release para `1.7-integration`.
7. Adicionar o teste `MYFIRSTIOC_GETVER` aos scripts de verificação do capítulo.

Cada passo é pequeno o suficiente para ser revisado por conta própria. Nenhum deles toca a lógica existente, o que significa que a refatoração dificilmente introduzirá regressões em código que funcionava antes. Esta é a disciplina da refatoração aditiva: fazer o driver crescer para fora adicionando novos arquivos e novas declarações, deixar o código existente no lugar e incrementar a versão quando a poeira baixar.

Uma refatoração mais agressiva (renomear uma função, reorganizar uma estrutura, alterar o conjunto de flags do cdevsw) exigiria uma disciplina diferente: um único commit por mudança, testes de regressão executados após cada um, e uma nota clara no incremento de versão sobre o que foi reorganizado. Drivers amplamente distribuídos usam essa disciplina em cada release; o driver `if_em` da árvore de código-fonte, por exemplo, tem uma refatoração de múltiplos commits em quase cada release menor do FreeBSD, com cada commit sendo aplicado de forma independente e testado isoladamente.

### Três Drivers da Árvore de Código-Fonte Comparados

Três drivers na árvore de código-fonte do FreeBSD ilustram a disciplina de organização do código-fonte em três pontos ao longo do espectro de complexidade. Lê-los como um trio torna os padrões visíveis.

`/usr/src/sys/dev/null/null.c` é o menor. É um único arquivo-fonte de 200 linhas com uma tabela `cdevsw`, um conjunto de callbacks, nenhum cabeçalho separado e nenhuma maquinaria de debug ou sysctl. O driver inteiro cabe em três páginas impressas. Este é o layout para um driver cujo único trabalho é estar presente e absorver (ou gerar) bytes; a integração é apenas na camada cdev.

`/usr/src/sys/net/if_disc.c` é um driver de rede com dois arquivos: `if_disc.c` para o código do driver e o `if.h` implícito do framework. O driver se registra na pilha de rede, mas não possui árvore sysctl, subárvore de debug nem cabeçalho ioctl público (usa o conjunto padrão `if_ioctl` definido pelo framework). Este é o layout para um driver que é uma instância de um framework em vez de algo próprio; o framework define a superfície, e o driver preenche os slots.

`/usr/src/sys/dev/ahci/ahci.c` é um driver com múltiplos arquivos, com arquivos separados para o núcleo AHCI, a cola de attach PCI, a cola de attach FDT da árvore de dispositivos, o código de gerenciamento de enclosure e a lógica específica do barramento. Cada arquivo é dedicado a uma responsabilidade; o arquivo central tem mais de 5000 linhas, mas o tamanho por arquivo é gerenciável. Este é o layout que escala para um driver de produção real: dividido por responsabilidade, colado por cabeçalho, e usando o limite do arquivo como unidade de refatoração.

O driver `myfirst` neste capítulo fica no meio. O estágio 3 tem cinco arquivos-fonte: `myfirst.c` (open/close/read/write e o cdevsw), `myfirst.h` (softc, declarações), `myfirst_debug.c` e `myfirst_debug.h` (debug e SDT), `myfirst_ioctl.c` e `myfirst_ioctl.h` (ioctl, sendo o último público) e `myfirst_sysctl.c` (árvore sysctl). Isso é suficiente para demonstrar o padrão de divisão por responsabilidade sem a sobrecarga cognitiva de um driver com cinquenta arquivos. Um leitor que precisar expandir ainda mais o `myfirst` tem um modelo claro: adicionar um novo par de arquivos para a nova responsabilidade, adicionar o arquivo-fonte em `SRCS`, adicionar o cabeçalho público ao conjunto de instalação se o espaço do usuário precisar dele, e atualizar `MYFIRST_VERSION`.

### Encerrando a Seção 8

A Seção 8 fechou o ciclo do outro tema do capítulo: como o layout do código-fonte de um driver, os números de versão e a disciplina de refatoração acompanham sua evolução. O marco do driver para este capítulo é `1.7-integration`, expresso simultaneamente como a string de release em `MYFIRST_VERSION`, a versão do módulo `1` (inalterada porque não existem dependentes no kernel) e a versão da API `1` (definida pela primeira vez porque este é o primeiro capítulo que expõe uma interface ioctl estável). A refatoração foi mantida aditiva, portanto nenhum shim foi necessário.

Neste ponto, você já conhece toda a superfície de integração: as Seções 2 a 4 cobriram os três universais (devfs, ioctl, sysctl), as Seções 5 e 6 esboçaram as integrações baseadas em registro (rede e CAM), a Seção 7 codificou a disciplina de ciclo de vida e a Seção 8 enquadrou o conjunto como uma evolução que os números de versão do driver devem acompanhar. As seções restantes do capítulo oferecem prática hands-on com o mesmo material.

### Juntando Tudo: O Attach e o Detach Finais

Antes de o capítulo avançar para os laboratórios, vale a pena ver as funções completas de attach e detach do capítulo reunidas em um único lugar. Elas amarram todas as seções anteriores: a construção do cdev da Seção 2, o cabeamento do ioctl da Seção 3, a árvore sysctl da Seção 4, a disciplina de ciclo de vida da Seção 7 e o tratamento de versão da Seção 8.

O attach completo para o estágio 3:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        /* 1. Stash the device pointer and initialise the lock. */
        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

        /* 2. Initialise the in-driver state to its defaults. */
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);
        sc->sc_open_count = 0;
        sc->sc_total_reads = 0;
        sc->sc_total_writes = 0;
        sc->sc_debug = 0;

        /* 3. Read the boot-time tunable for the debug mask. If the
         *    operator set hw.myfirst.debug_mask_default in
         *    /boot/loader.conf, sc_debug now holds that value;
         *    otherwise sc_debug remains zero.
         */
        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

        /* 4. Construct the cdev. The args struct gives us a typed,
         *    versionable interface; mda_si_drv1 wires the per-cdev
         *    pointer to the softc atomically, closing the race window
         *    between creation and assignment.
         */
        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0)
                goto fail_mtx;

        /* 5. Build the sysctl tree. The framework owns the per-device
         *    context, so we do not need to track or destroy it
         *    ourselves; detach below does not call sysctl_ctx_free.
         */
        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT,
            "attach: stage 3 complete, version " MYFIRST_VERSION "\n");
        return (0);

fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

O detach completo para o estágio 3:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 1. Refuse detach if anyone holds the device open. The
         *    chapter's pattern is the simple soft refusal; Challenge 3
         *    walks through the more elaborate dev_ref/dev_rel pattern
         *    that drains in-flight references rather than refusing.
         */
        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0) {
                mtx_unlock(&sc->sc_mtx);
                return (EBUSY);
        }
        mtx_unlock(&sc->sc_mtx);

        DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

        /* 2. Destroy the cdev. destroy_dev blocks until every
         *    in-flight cdevsw callback returns; after this call,
         *    no new open/close/read/write/ioctl can arrive.
         */
        destroy_dev(sc->sc_cdev);

        /* 3. The per-device sysctl context is torn down automatically
         *    by the framework after detach returns successfully.
         *    Nothing to do here.
         */

        /* 4. Destroy the lock. Safe now because the cdev is gone and
         *    no other code path can take it.
         */
        mtx_destroy(&sc->sc_mtx);

        return (0);
}
```

Dois pontos merecem uma nota final.

A ordem das operações é o inverso estrito do attach: lock criado primeiro no attach, lock destruído por último no detach; cdev criado perto do final do attach, cdev destruído perto do início do detach; árvore sysctl criada por último no attach, árvore sysctl desmontada primeiro (pelo framework, automaticamente) no detach. Essa é a regra cardinal da Seção 7 em forma concreta.

O padrão de recusa no detach (a verificação `if (open_count > 0)`) é a escolha do capítulo pela simplicidade. Um driver real pode precisar da maquinaria mais elaborada de `dev_ref`/`dev_rel` para implementar um detach com drenagem; o Desafio 3 percorre essa variante. Para o `myfirst`, a recusa simples fornece ao operador um erro claro e é suficiente.

Na Seção 9 passamos da explicação para a prática. Os laboratórios conduzem o leitor pela construção dos estágios 1, 2 e 3 da integração em sequência, com comandos de verificação e saída esperada para cada um. Após os laboratórios vêm os exercícios desafio (Seção 10), o catálogo de resolução de problemas (Seção 11) e o encerramento e ponte que fecham o capítulo.

## Laboratórios Práticos

Os laboratórios desta seção conduzem o leitor desde uma árvore de trabalho recém-clonada até cada superfície de integração adicionada no capítulo. Cada laboratório é pequeno o suficiente para ser concluído em uma sessão e está acompanhado de um comando de verificação que confirma a mudança. Execute os laboratórios em ordem; os laboratórios posteriores constroem sobre os anteriores.

Os arquivos complementares em `examples/part-05/ch24-integration/` contêm três drivers de referência em estágios (`stage1-devfs/`, `stage2-ioctl/`, `stage3-sysctl/`) que correspondem aos marcos deste capítulo. Os laboratórios assumem que o leitor parte do seu próprio driver do final do Capítulo 23 (versão `1.6-debug`) ou copia o diretório de estágio apropriado para um local de trabalho, faz as alterações lá e consulta o diretório de estágio correspondente caso encontre dificuldades.

Cada laboratório utiliza um sistema FreeBSD 14.3 real. Uma máquina virtual está ótimo; não execute esses laboratórios em um host de produção, pois o carregamento e descarregamento de módulos pode travar ou causar pânico no sistema se o driver tiver bugs.

### Laboratório 1: Compilar e Carregar o Driver do Estágio 1

**Objetivo**: levar o driver da linha de base do Capítulo 23 (`1.6-debug`) até o marco do estágio 1 deste capítulo (um cdev devidamente construído em `/dev/myfirst0`).

**Preparação**:

Parta da sua própria árvore de trabalho do final do Capítulo 23 (o driver na versão `1.6-debug`) ou copie a árvore de referência do último estágio do Capítulo 23 para um diretório de laboratório:

```console
$ cp -r ~/myfirst-1.6-debug ~/myfirst-lab1
$ cd ~/myfirst-lab1
$ ls
Makefile  myfirst.c  myfirst.h  myfirst_debug.c  myfirst_debug.h
```

Se quiser comparar com o ponto de partida já migrado para o estágio 1 do capítulo (com `make_dev_s` já aplicado), consulte `examples/part-05/ch24-integration/stage1-devfs/` como solução de referência, e não como diretório de partida.

**Passo 1**: Abra `myfirst.c` e localize a chamada existente a `make_dev`. O código do Capítulo 23 usa a forma mais antiga de chamada única. Substitua-a pela forma com `make_dev_args` da Seção 2:

```c
struct make_dev_args args;
int error;

make_dev_args_init(&args);
args.mda_devsw = &myfirst_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_WHEEL;
args.mda_mode = 0660;
args.mda_si_drv1 = sc;
args.mda_unit = device_get_unit(dev);

error = make_dev_s(&args, &sc->sc_cdev,
    "myfirst%d", device_get_unit(dev));
if (error != 0) {
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

**Passo 2**: Adicione `D_TRACKCLOSE` às flags do cdevsw (ele já deve ter `D_VERSION`):

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_flags   = D_TRACKCLOSE,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
};
```

**Passo 3**: Confirme que `myfirst_open` e `myfirst_close` usam `dev->si_drv1` para recuperar o softc:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        ...
}
```

**Passo 4**: Compile e carregue:

```console
$ make
$ sudo kldload ./myfirst.ko
```

**Verificação**:

```console
$ ls -l /dev/myfirst0
crw-rw---- 1 root wheel 0x... <date> /dev/myfirst0
$ sudo cat /dev/myfirst0
Hello from myfirst
$ sudo kldstat | grep myfirst
N    1 0xffff... 1...    myfirst.ko
$ sudo dmesg | tail
... (debug messages from MYF_DBG_INIT)
```

Se o `ls` mostrar o proprietário, grupo ou permissões errados, verifique novamente os valores de `mda_uid`, `mda_gid` e `mda_mode`. Se o `cat` retornar uma string vazia, verifique se `myfirst_read` está preenchendo o buffer do usuário a partir de `sc->sc_msg`. Se o carregamento for bem-sucedido mas o dispositivo não aparecer, verifique se o cdevsw é referenciado a partir de `make_dev_args`.

**Limpeza**:

```console
$ sudo kldunload myfirst
$ ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

Um descarregamento bem-sucedido remove o nó de dispositivo. Se o descarregamento falhar com `Device busy`, verifique se nenhum shell ou programa tem o dispositivo aberto (`fstat | grep myfirst0`).

### Laboratório 2: Adicionar a Interface ioctl

**Objetivo**: estender o driver para o estágio 2 adicionando os quatro comandos ioctl da Seção 3.

**Preparação**:

```console
$ cp -r examples/part-05/ch24-integration/stage1-devfs ~/myfirst-lab2
$ cd ~/myfirst-lab2
```

**Passo 1**: Crie `myfirst_ioctl.h` a partir do modelo da Seção 3. Coloque-o no mesmo diretório dos demais arquivos de código-fonte. Inclua `sys/ioccom.h` e `sys/types.h`. Defina `MYFIRST_MSG_MAX = 256` e os quatro números ioctl. Não inclua nenhum cabeçalho exclusivo do kernel.

**Passo 2**: Crie `myfirst_ioctl.c` a partir do modelo da Seção 3. O despachante é uma única função `myfirst_ioctl` com a assinatura padrão `d_ioctl_t`.

**Passo 3**: Adicione `myfirst_ioctl.c` a `SRCS` no `Makefile`:

```make
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c
```

**Passo 4**: Atualize o cdevsw para apontar `.d_ioctl` para o novo despachante:

```c
.d_ioctl = myfirst_ioctl,
```

**Passo 5**: Adicione `sc_msg` e `sc_msglen` ao softc e inicialize-os no attach:

```c
strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
sc->sc_msglen = strlen(sc->sc_msg);
```

**Passo 6**: Compile o componente em espaço do usuário. Coloque `myfirstctl.c` no mesmo diretório e crie um pequeno `Makefile.user`:

```make
CC?= cc
CFLAGS+= -Wall -Werror -I.

myfirstctl: myfirstctl.c myfirst_ioctl.h
        ${CC} ${CFLAGS} -o myfirstctl myfirstctl.c
```

(Observe que o recuo deve ser uma tabulação, não espaços, para que o `make` processe a regra corretamente.)

Compile o módulo do kernel e o componente complementar:

```console
$ make
$ make -f Makefile.user
$ sudo kldload ./myfirst.ko
```

**Verificação**:

```console
$ ./myfirstctl get-version
driver ioctl version: 1
$ ./myfirstctl get-message
Hello from myfirst
$ sudo ./myfirstctl set-message "drivers are fun"
$ ./myfirstctl get-message
drivers are fun
$ sudo ./myfirstctl reset
$ ./myfirstctl get-message

$
```

Se `set-message` retornar `Permission denied`, o problema é que o dispositivo está no modo `0660` e o usuário não pertence ao grupo `wheel`. Execute com `sudo` (como os comandos de verificação acima fazem) ou altere o grupo do dispositivo para um que o usuário pertença, usando `mda_gid`, e recarregue o módulo.

Se `set-message` retornar `Bad file descriptor`, o problema é que `myfirstctl` abriu o dispositivo somente para leitura. Verifique se o programa seleciona `O_RDWR` para `set-message` e `reset`.

Se algum ioctl retornar `Inappropriate ioctl for device`, o problema é uma incompatibilidade entre o tamanho codificado em `myfirst_ioctl.h` e a visão do despachante sobre os dados. Verifique novamente as macros `_IOR`/`_IOW` e o tamanho das estruturas que elas declaram.

**Limpeza**:

```console
$ sudo kldunload myfirst
```

### Laboratório 3: Adicionar a Árvore sysctl

**Objetivo**: estender o driver para o estágio 3 adicionando os OIDs sysctl da Seção 4 e lendo um tunable de `/boot/loader.conf`.

**Preparação**:

```console
$ cp -r examples/part-05/ch24-integration/stage2-ioctl ~/myfirst-lab3
$ cd ~/myfirst-lab3
```

**Passo 1**: Crie `myfirst_sysctl.c` a partir do modelo da Seção 4. A função `myfirst_sysctl_attach(sc)` constrói toda a árvore.

**Passo 2**: Adicione `myfirst_sysctl.c` a `SRCS` no `Makefile`.

**Passo 3**: Atualize `myfirst_attach` para chamar `TUNABLE_INT_FETCH` e `myfirst_sysctl_attach`:

```c
TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

/* ... after make_dev_s succeeds: */
myfirst_sysctl_attach(sc);
```

**Passo 4**: Compile e carregue:

```console
$ make
$ sudo kldload ./myfirst.ko
```

**Verificação**:

```console
$ sysctl -a dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message_len: 18
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
```

Abra o dispositivo uma vez e verifique novamente os contadores:

```console
$ cat /dev/myfirst0
Hello from myfirst
$ sysctl dev.myfirst.0.total_reads
dev.myfirst.0.total_reads: 1
```

Teste o tunable definido em tempo de boot. Edite `/boot/loader.conf` (faça um backup antes):

```console
$ sudo cp /boot/loader.conf /boot/loader.conf.backup
$ sudo sh -c 'echo hw.myfirst.debug_mask_default=\"0x06\" >> /boot/loader.conf'
```

Observe que isso só tem efeito na próxima reinicialização e somente se o módulo for carregado pelo loader (e não pelo `kldload` após o boot). Para um teste interativo sem reinicializar, defina o valor antes de carregar:

```console
$ sudo kenv hw.myfirst.debug_mask_default=0x06
$ sudo kldload ./myfirst.ko
$ sysctl dev.myfirst.0.debug.mask
dev.myfirst.0.debug.mask: 6
```

Se o valor for 0 em vez de 6, verifique se a chamada a `TUNABLE_INT_FETCH` usa a mesma string que o comando `kenv`. A chamada deve ser executada antes de `myfirst_sysctl_attach` para que o valor esteja disponível quando o OID for criado.

**Limpeza**:

```console
$ sudo kldunload myfirst
$ sudo cp /boot/loader.conf.backup /boot/loader.conf
```

### Laboratório 4: Percorrer o Ciclo de Vida Injetando uma Falha

**Objetivo**: ver a cadeia `goto err` no attach realmente desfazer as operações ao forçar deliberadamente a falha em uma das etapas.

**Preparação**:

```console
$ cp -r examples/part-05/ch24-integration/stage3-sysctl ~/myfirst-lab4
$ cd ~/myfirst-lab4
```

**Passo 1**: Abra `myfirst.c` e localize `myfirst_attach`. Insira uma falha deliberada logo após `make_dev_s` ter sucesso:

```c
error = make_dev_s(&args, &sc->sc_cdev,
    "myfirst%d", device_get_unit(dev));
if (error != 0)
        goto fail_mtx;

/* DELIBERATE FAILURE for Lab 4 */
device_printf(dev, "Lab 4: injected failure after make_dev_s\n");
error = ENXIO;
goto fail_cdev;

myfirst_sysctl_attach(sc);
return (0);

fail_cdev:
        destroy_dev(sc->sc_cdev);
fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
```

**Passo 2**: Compile e tente carregar:

```console
$ make
$ sudo kldload ./myfirst.ko
kldload: an error occurred while loading module myfirst. Please check dmesg(8) for more details.
$ sudo dmesg | tail
myfirst0: Lab 4: injected failure after make_dev_s
```

**Verificação**:

```console
$ ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
$ kldstat | grep myfirst
$
```

O cdev foi removido (a limpeza em `goto fail_cdev` o destruiu), o módulo não foi carregado e nenhum recurso vazou. Se o cdev permanecer após a falha, falta uma chamada a `destroy_dev` na limpeza. Se o kernel entrar em pânico na próxima tentativa de carregamento do módulo, a limpeza está liberando ou destruindo algo duas vezes.

**Bônus**: mova a injeção de falha para **antes** de `make_dev_s`. A cadeia de limpeza deve agora pular o rótulo `fail_cdev` e executar apenas `fail_mtx`. Verifique que o cdev nunca foi criado e que o mutex foi destruído:

```console
$ sudo kldload ./myfirst.ko
$ sudo dmesg | tail
... no Lab 4 message because it now runs before make_dev_s ...
```

**Limpeza**:

Remova o bloco de falha deliberada antes de continuar.

### Laboratório 5: Rastrear as Superfícies de Integração com DTrace

**Objetivo**: usar as sondas SDT do Capítulo 23 para rastrear tráfego de ioctl, open, close e read em tempo real.

**Preparação**: driver do estágio 3 carregado conforme o Laboratório 3.

**Passo 1**: Verifique se as sondas estão visíveis para o DTrace:

```console
$ sudo dtrace -l -P myfirst
   ID   PROVIDER      MODULE    FUNCTION   NAME
... id  myfirst       kernel    -          open
... id  myfirst       kernel    -          close
... id  myfirst       kernel    -          io
... id  myfirst       kernel    -          ioctl
```

Se a lista estiver vazia, as sondas SDT não estão registradas. Verifique se `myfirst_debug.c` está em `SRCS` e se `SDT_PROBE_DEFINE*` é chamado a partir desse arquivo.

**Passo 2**: Abra um rastreamento contínuo em um terminal:

```console
$ sudo dtrace -n 'myfirst:::ioctl { printf("ioctl cmd=0x%x flags=0x%x", arg1, arg2); }'
dtrace: description 'myfirst:::ioctl' matched 1 probe
```

**Passo 3**: Exercite o driver em outro terminal:

```console
$ ./myfirstctl get-version
$ ./myfirstctl get-message
$ sudo ./myfirstctl set-message "Lab 5"
$ sudo ./myfirstctl reset
```

O terminal do DTrace deve mostrar uma linha por ioctl com o código do comando e as flags do arquivo.

**Passo 4**: Combine múltiplas sondas em um único script:

```console
$ sudo dtrace -n '
    myfirst:::open  { printf("open  pid=%d", pid); }
    myfirst:::close { printf("close pid=%d", pid); }
    myfirst:::io    { printf("io    pid=%d write=%d resid=%d", pid, arg1, arg2); }
    myfirst:::ioctl { printf("ioctl pid=%d cmd=0x%x", pid, arg1); }
'
```

Em outro terminal:

```console
$ cat /dev/myfirst0
$ ./myfirstctl get-version
$ echo "hello" | sudo tee /dev/myfirst0
```

A saída do DTrace agora mostra o padrão completo de tráfego, com o open e o close em torno de cada operação, a leitura ou escrita dentro, e eventuais ioctls. Esse é o valor de ter sondas SDT integradas com os callbacks do cdevsw: toda superfície de integração que o driver expõe é também uma superfície de sonda para o DTrace.

**Limpeza**:

```console
^C
$ sudo kldunload myfirst
```

### Laboratório 6: Teste de Fumaça de Integração

**Objetivo**: construir um único script de shell que exercite cada superfície de integração em uma execução e produza um resumo verde/vermelho que o leitor possa colar em um relatório de bug ou em uma lista de verificação de prontidão para lançamento.

Um teste de fumaça é uma verificação pequena, rápida e de ponta a ponta de que o driver está vivo e de que cada superfície responde. Ele não substitui testes unitários cuidadosos; ele dá ao leitor uma confirmação de cinco segundos de que nada está obviamente quebrado antes de investir mais tempo. Drivers reais têm testes de fumaça; o capítulo recomenda adicionar um a cada novo driver desde o primeiro dia.

**Preparação**: driver do estágio 3 carregado.

**Passo 1**: Crie `smoke.sh` no diretório de trabalho:

```sh
#!/bin/sh
# smoke.sh - end-to-end smoke test for the myfirst driver.

set -u
fail=0

check() {
        if eval "$1"; then
                printf "  PASS  %s\n" "$2"
        else
                printf "  FAIL  %s\n" "$2"
                fail=$((fail + 1))
        fi
}

echo "=== myfirst integration smoke test ==="

# 1. Module is loaded.
check "kldstat | grep -q myfirst" "module is loaded"

# 2. /dev node exists with the right mode.
check "test -c /dev/myfirst0" "/dev/myfirst0 exists as a character device"
check "test \"\$(stat -f %Lp /dev/myfirst0)\" = \"660\"" "/dev/myfirst0 is mode 0660"

# 3. Sysctl tree is present.
check "sysctl -N dev.myfirst.0.version >/dev/null 2>&1" "version OID is present"
check "sysctl -N dev.myfirst.0.debug.mask >/dev/null 2>&1" "debug.mask OID is present"
check "sysctl -N dev.myfirst.0.open_count >/dev/null 2>&1" "open_count OID is present"

# 4. Ioctls work (requires myfirstctl built).
check "./myfirstctl get-version >/dev/null" "MYFIRSTIOC_GETVER returns success"
check "./myfirstctl get-message >/dev/null" "MYFIRSTIOC_GETMSG returns success"
check "sudo ./myfirstctl set-message smoke && [ \"\$(./myfirstctl get-message)\" = smoke ]" "MYFIRSTIOC_SETMSG round-trip works"
check "sudo ./myfirstctl reset && [ -z \"\$(./myfirstctl get-message)\" ]" "MYFIRSTIOC_RESET clears state"

# 5. Read/write basic path.
check "echo hello | sudo tee /dev/myfirst0 >/dev/null" "write to /dev/myfirst0 succeeds"
check "[ \"\$(cat /dev/myfirst0)\" = hello ]" "read returns the previously written message"

# 6. Counters update.
sudo ./myfirstctl reset >/dev/null
cat /dev/myfirst0 >/dev/null
check "[ \"\$(sysctl -n dev.myfirst.0.total_reads)\" = 1 ]" "total_reads incremented after one read"

# 7. SDT probes are registered.
check "sudo dtrace -l -P myfirst | grep -q open" "myfirst:::open SDT probe is visible"

echo "=== summary ==="
if [ $fail -eq 0 ]; then
        echo "ALL PASS"
        exit 0
else
        printf "%d FAIL\n" "$fail"
        exit 1
fi
```

**Passo 2**: Torne-o executável e execute-o:

```console
$ chmod +x smoke.sh
$ ./smoke.sh
=== myfirst integration smoke test ===
  PASS  module is loaded
  PASS  /dev/myfirst0 exists as a character device
  PASS  /dev/myfirst0 is mode 0660
  PASS  version OID is present
  PASS  debug.mask OID is present
  PASS  open_count OID is present
  PASS  MYFIRSTIOC_GETVER returns success
  PASS  MYFIRSTIOC_GETMSG returns success
  PASS  MYFIRSTIOC_SETMSG round-trip works
  PASS  MYFIRSTIOC_RESET clears state
  PASS  write to /dev/myfirst0 succeeds
  PASS  read returns the previously written message
  PASS  total_reads incremented after one read
  PASS  myfirst:::open SDT probe is visible
=== summary ===
ALL PASS
```

Se alguma verificação falhar, a saída do script aponta diretamente para a superfície de integração com problema. Uma falha em `version OID is present` significa que a construção do sysctl não foi executada; uma falha em `MYFIRSTIOC_GETVER` significa que o despachante ioctl não está cabeado corretamente; uma falha em `total_reads incremented` significa que o callback de leitura não está incrementando o contador sob o mutex.

**Verificação**: execute novamente após cada alteração no driver. Um teste de fumaça aprovado antes de um commit é o seguro mais barato possível contra uma regressão que quebre o fluxo básico.

### Laboratório 7: Recarregar Sem Reiniciar Programas em Espaço do Usuário

**Objetivo**: confirmar que o driver pode ser descarregado e recarregado enquanto um programa em espaço do usuário mantém um descritor de arquivo aberto em outro terminal.

Este teste revela bugs de ciclo de vida que o padrão de "detach suave" do capítulo foi projetado para evitar. Um driver que retorna `EBUSY` do detach quando um usuário mantém o dispositivo aberto está se defendendo corretamente; um driver que permite que o detach tenha sucesso e depois entra em pânico quando o usuário emite um ioctl está com defeito.

**Preparação**: driver do estágio 3 carregado.

**Passo 1** (terminal 1): mantenha o dispositivo aberto com um comando de longa duração:

```console
$ sleep 3600 < /dev/myfirst0 &
$ jobs
[1]+ Running                 sleep 3600 < /dev/myfirst0 &
```

**Passo 2** (terminal 2): tente descarregar:

```console
$ sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

Esse é o comportamento esperado. O `myfirst_detach` do capítulo verifica `open_count > 0` e retorna `EBUSY` em vez de desmontar o cdev sob um descritor de arquivo aberto.

**Passo 3** (terminal 2): verifique se o dispositivo ainda está funcional a partir de outro shell:

```console
$ ./myfirstctl get-version
driver ioctl version: 1
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 1
```

O contador de abertura reflete o descritor de arquivo que está sendo mantido.

**Passo 4** (terminal 1): libere o descritor de arquivo:

```console
$ kill %1
$ wait
```

**Passo 5** (terminal 2): o descarregamento agora tem sucesso:

```console
$ sudo kldunload myfirst
$ sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

O OID desapareceu porque o Newbus desmontou o contexto sysctl por dispositivo após o detach retornar com sucesso.

**Verificação**: o descarregamento deve ter sucesso toda vez, sem nenhum panic. Se o kernel entrar em panic durante o passo 5, a causa é quase sempre que os callbacks do cdev ainda estão em execução quando `destroy_dev` retorna. Verifique se o `d_close` do cdevsw libera corretamente tudo que foi adquirido em `d_open`, e verifique se nenhum callout ou taskqueue ainda está agendado.

Uma extensão bônus é escrever um pequeno programa que abre o dispositivo, chama `MYFIRSTIOC_RESET` imediatamente e então fica em loop chamando `MYFIRSTIOC_GETVER` por vários segundos. Enquanto o loop estiver em execução, tente fazer o descarregamento a partir de outro terminal. O descarregamento ainda deve falhar com `EBUSY` e os ioctls em execução não devem corromper nada.

### Encerrando os Laboratórios

Os sete laboratórios conduziram o leitor por toda a superfície de integração, pela disciplina de ciclo de vida, pelo smoke test e pelo contrato de soft detach. O Estágio 1 adicionou o cdev; o Estágio 2 adicionou a interface ioctl; o Estágio 3 adicionou a árvore sysctl; o laboratório de ciclo de vida (Lab 4) confirmou o desenrolar; o laboratório DTrace (Lab 5) confirmou a integração com a infraestrutura de depuração do Capítulo 23; o smoke test (Lab 6) forneceu ao leitor um script de verificação reutilizável; e o laboratório de recarga (Lab 7) confirmou o contrato de soft detach.

Um driver que passa em todos os sete laboratórios está na versão de marco do capítulo, `1.7-integration`, e está pronto para o tópico do próximo capítulo. Os exercícios desafio da Seção 10 oferecem ao leitor trabalho complementar opcional que estende o driver além do que o capítulo cobre.

## Exercícios Desafio

Os desafios abaixo são opcionais e destinam-se ao leitor que deseja levar o driver além do marco do capítulo. Cada desafio tem um objetivo declarado, algumas dicas sobre a abordagem e uma nota sobre quais seções do capítulo contêm o material relevante. Nenhum dos desafios tem uma única resposta correta; os leitores são encorajados a comparar sua solução com um revisor ou com os drivers da árvore de código-fonte citados como referência.

### Desafio 1: Adicionar um Ioctl de Comprimento Variável

**Objetivo**: estender a interface ioctl para que um programa no espaço do usuário possa transferir um buffer maior do que os 256 bytes fixos usados por `MYFIRSTIOC_SETMSG`.

O padrão do capítulo é de tamanho fixo: `MYFIRSTIOC_SETMSG` declara `_IOW('M', 3, char[256])` e o kernel realiza o copyin completo. Para buffers maiores (digamos, até 1 MB), o padrão de ponteiro embutido é necessário:

```c
struct myfirst_blob {
        size_t  len;
        char   *buf;    /* user-space pointer */
};
#define MYFIRSTIOC_SETBLOB _IOW('M', 5, struct myfirst_blob)
```

O dispatcher deve chamar `copyin` para transferir os bytes referenciados pelo ponteiro; a estrutura em si chega pelo copyin automático, como antes. Dicas: imponha um comprimento máximo (1 MB é razoável). Aloque um buffer temporário no kernel com `malloc(M_TEMP, len, M_WAITOK)`; não faça a alocação dentro do mutex do softc. Libere o buffer antes de retornar. Referência: Seção 3, "Armadilhas Comuns com ioctl", segunda armadilha.

Uma extensão bônus é adicionar `MYFIRSTIOC_GETBLOB`, que copia a mensagem atual no mesmo formato de comprimento variável; preste atenção ao caso em que o buffer fornecido pelo usuário é menor do que a mensagem e decida se vai truncar, retornar `ENOMEM` ou gravar de volta o comprimento necessário. Drivers reais (`SIOCGIFCAP`, `KIOCGRPC`) utilizam este último padrão.

### Desafio 2: Adicionar um Contador por Abertura

**Objetivo**: manter um contador por descritor de arquivo (um número para cada abertura de `/dev/myfirst0`) em vez de apenas o contador por instância que temos agora.

O `sc_open_count` do capítulo agrega todas as aberturas. Um contador por abertura permitiria que um programa soubesse quanto leu a partir do seu próprio descritor. Dicas: use `cdevsw->d_priv` para anexar uma estrutura por fd (uma `struct myfirst_fdpriv` contendo um contador). Aloque a estrutura em `myfirst_open` e libere-a em `myfirst_close`. O framework fornece a cada `cdev_priv` um ponteiro único no campo `f_data` do arquivo; as callbacks de leitura e escrita podem então buscar a estrutura por fd através de `devfs_get_cdevpriv()`.

Referência: `/usr/src/sys/kern/kern_conf.c` para `devfs_set_cdevpriv` e `devfs_get_cdevpriv`. O padrão também é usado por `/usr/src/sys/dev/random/random_harvestq.c`.

Uma extensão bônus é adicionar um OID sysctl que informe a soma dos contadores por fd e verificar que ela é sempre igual ao contador agregado existente. Discrepâncias indicam um incremento ausente em algum lugar.

### Desafio 3: Implementar Soft Detach com `dev_ref`

**Objetivo**: substituir o padrão "recusar detach se aberto" do capítulo pelo padrão mais elegante "drenar até o último fechamento e então fazer o detach".

O detach do capítulo retorna `EBUSY` se algum usuário mantiver o dispositivo aberto. Um padrão mais elegante usa `dev_ref`/`dev_rel` para contar as referências pendentes e aguarda que a contagem chegue a zero antes de concluir o detach. Dicas: tome um `dev_ref` em `myfirst_open` e libere-o em `myfirst_close`. No detach, defina uma flag de encerramento e então chame `destroy_dev_drain` (ou escreva um pequeno loop que chame `tsleep` enquanto `dev_refs > 0`) antes de chamar `destroy_dev`. Assim que a contagem chegar a zero e o cdev for destruído, conclua o detach normalmente.

Referência: `/usr/src/sys/kern/kern_conf.c` para o mecanismo `dev_ref`; `/usr/src/sys/fs/cuse` é um driver real que utiliza o padrão de drenagem para o detach com espera.

A extensão bônus é adicionar um OID sysctl que informe a contagem de referências atual e verificar que ela corresponde à contagem de aberturas.

### Desafio 4: Substituir a Letra Mágica Estática

**Objetivo**: substituir a letra mágica `'M'` embutida em `myfirst_ioctl.h` por um nome que não colida com nada mais na árvore de código-fonte.

O capítulo escolheu `'M'` arbitrariamente e alertou sobre o risco de colisões. Um driver mais defensivo usa um identificador mágico mais longo e constrói os números ioctl a partir dele. Dicas: defina `MYFIRST_IOC_GROUP = 0x83` (ou qualquer byte não utilizado por outro driver). O macro `_IOC` então recebe essa constante em vez de um literal de caractere. Documente a escolha com um comentário no header explicando como ela foi feita.

Um bônus é usar grep em `/usr/src/sys` com o padrão `_IO[RW]?\\(.\\?'M'` e produzir uma lista de todos os usos existentes de `'M'`. (Há vários, incluindo ioctls de `MIDI` e outros; a pesquisa em si é instrutiva.)

### Desafio 5: Adicionar um `EVENTHANDLER` para Desligamento

**Objetivo**: fazer o driver se comportar de forma adequada quando o sistema estiver sendo desligado.

O driver do capítulo não tem um handler de desligamento; se o sistema for desligado com `myfirst` carregado, o framework eventualmente chama o detach. Um driver mais polido registra um `EVENTHANDLER` para `shutdown_pre_sync` para que possa liberar qualquer estado em trânsito antes que o sistema de arquivos passe para modo somente leitura.

Dicas: registre o handler no attach com `EVENTHANDLER_REGISTER(shutdown_pre_sync, ...)`. O handler é chamado no estágio de desligamento correspondente. Cancele o registro no detach com `EVENTHANDLER_DEREGISTER`. Dentro do handler, coloque o driver em um estado quiescente (limpe a mensagem, zere os contadores); neste ponto o sistema de arquivos ainda aceita escrita, portanto qualquer feedback ao usuário via `printf` será registrado em `/var/log/messages` após o próximo boot.

Referência: Seção 7, "EVENTHANDLER para Eventos do Sistema" e `/usr/src/sys/sys/eventhandler.h` para a lista completa de eventos nomeados.

### Desafio 6: Uma Segunda Subárvore Sysctl por Driver

**Objetivo**: adicionar uma segunda subárvore sob `dev.myfirst.0` que exponha estatísticas por thread.

A árvore do capítulo tem uma subárvore `debug.`. Um driver completo pode ter também uma subárvore `stats.` (para estatísticas de leitura e escrita discriminadas por descritor de arquivo) ou uma subárvore `errors.` (para contadores de erros). Dicas: use `SYSCTL_ADD_NODE` para criar um novo nó e depois `SYSCTL_ADD_*` para populá-lo sob o `SYSCTL_CHILDREN` do novo nó. O padrão é idêntico ao da subárvore `debug.`, mas com raiz em um nome diferente.

Referência: Seção 4, "A Árvore Sysctl do `myfirst`", para a subárvore `debug.` existente como modelo; `/usr/src/sys/dev/iicbus` para vários drivers que utilizam layouts sysctl com múltiplas subárvores.

### Desafio 7: Dependência entre Módulos

**Objetivo**: construir um segundo módulo pequeno (`myfirst_logger`) que depende de `myfirst` e utiliza sua API interna do kernel.

O driver `myfirst` do capítulo não exporta nenhum símbolo para usuários internos do kernel. Adicionar um segundo módulo que chame funções de `myfirst` exercita o mecanismo `MODULE_DEPEND`. Dicas: declare uma função exportadora de símbolo em `myfirst.h` (por exemplo, `int myfirst_get_message(int unit, char *buf, size_t len)`) e implemente-a em `myfirst.c`. Construa o segundo módulo com `MODULE_DEPEND(myfirst_logger, myfirst, 1, 1, 1)` para que o kernel carregue `myfirst` automaticamente quando `myfirst_logger` for carregado.

Um bônus é incrementar a versão do módulo de `myfirst` para 2, alterar a API interna do kernel de forma não retrocompatível e observar que o segundo módulo falha ao carregar até ser reconstruído contra a nova versão. Referência: Seção 8, "Strings de Versão, Números de Versão e a Versão da API".

### Encerrando os Desafios

Os sete desafios variam de curtos (o Desafio 4 é principalmente uma renomeação e um comentário) a substanciais (o Desafio 3 exige ler e compreender o `dev_ref`). O leitor que completar todos os sete terá familiaridade prática com cada aspecto de integração que o capítulo apenas esboça. O leitor que completar qualquer um deles terá uma compreensão mais profunda da disciplina de integração do que o capítulo sozinho pode proporcionar.

## Solução de Problemas

As superfícies de integração deste capítulo ficam na fronteira entre o kernel e o restante do sistema. Problemas nessa fronteira frequentemente parecem bugs do driver, mas na verdade são sintomas de uma flag ausente, um erro de digitação em um header ou um equívoco sobre quem é dono de quê. O catálogo abaixo reúne os sintomas mais comuns, suas causas prováveis e a correção para cada um.

### `/dev/myfirst0` Não Aparece Após `kldload`

A primeira coisa a verificar é se o módulo foi carregado com sucesso:

```console
$ kldstat | grep myfirst
```

Se o módulo não estiver listado, o carregamento falhou; consulte `dmesg` para obter uma mensagem mais específica. O motivo mais comum é um símbolo não resolvido (com frequência porque o novo arquivo-fonte não está em `SRCS`).

Se o módulo estiver listado mas o nó de dispositivo estiver ausente, a chamada a `make_dev_s` dentro de `myfirst_attach` provavelmente falhou. Adicione um `device_printf(dev, "make_dev_s returned %d\n", error)` ao lado da chamada e tente novamente. O motivo mais comum para um retorno diferente de zero é que outro driver já criou `/dev/myfirst0` (o kernel não sobrescreve silenciosamente um nó existente) ou que `make_dev_s` foi chamado em um contexto que não pode bloquear com `MAKEDEV_NOWAIT`.

Um motivo mais sutil é que `cdevsw->d_version` não é igual a `D_VERSION`. O kernel verifica isso e se recusa a registrar um cdevsw com incompatibilidade de versão. A correção é `static struct cdevsw myfirst_cdevsw = { .d_version = D_VERSION, ... };` exatamente dessa forma.

### `cat /dev/myfirst0` Retorna "Permission denied"

O dispositivo existe, mas o usuário não consegue abri-lo. O modo padrão neste capítulo é `0660` e o grupo padrão é `wheel`. Execute com `sudo`, mude `mda_gid` para o grupo do usuário ou mude `mda_mode` para `0666` (esta última opção é aceitável para um módulo didático, mas é uma escolha ruim para um driver de produção, pois qualquer usuário local poderia abrir o dispositivo).

### `ioctl` Retorna "Inappropriate ioctl for device"

O kernel retornou `ENOTTY`, o que significa que não conseguiu corresponder o código da requisição a nenhum cdevsw. As duas causas mais comuns são:

* O dispatcher do driver retornou `ENOIOCTL` para o comando. O kernel traduz `ENOIOCTL` em `ENOTTY` para o espaço do usuário. A correção é adicionar um caso para o comando no switch do dispatcher.

* O comprimento codificado no código da requisição não corresponde ao tamanho real do buffer usado pelo programa. Isso acontece após uma refatoração de header em que a linha `_IOR` foi editada, mas o programa no espaço do usuário não foi recompilado com o novo header. A correção é recompilar o programa com o header atual e reconstruir o módulo com o mesmo código-fonte.

### `ioctl` Retorna "Bad file descriptor"

O dispatcher retornou `EBADF`, que é o padrão do capítulo para "o arquivo não foi aberto com as flags corretas para este comando". A correção é abrir o dispositivo com `O_RDWR` em vez de `O_RDONLY` para qualquer comando que altere o estado. O programa auxiliar `myfirstctl` já faz isso; um programa customizado pode não fazer.

### `sysctl dev.myfirst.0` Exibe a Árvore mas as Leituras Retornam "operation not supported"

Isso geralmente significa que o OID sysctl foi adicionado com um ponteiro de handler obsoleto ou inválido. Se a leitura retorna imediatamente com `EOPNOTSUPP` (95), a causa é quase sempre que o OID foi registrado com `CTLTYPE_OPAQUE` e um handler que não chama `SYSCTL_OUT`. A correção é usar um dos helpers tipados `SYSCTL_ADD_*` (`SYSCTL_ADD_UINT`, `SYSCTL_ADD_STRING`, `SYSCTL_ADD_PROC` com a string de formato correta) para que o framework saiba o que fazer em uma leitura.

### `sysctl -w dev.myfirst.0.foo=value` Falha com "permission denied"

O OID provavelmente foi criado com `CTLFLAG_RD` (somente leitura) quando a variante gravável `CTLFLAG_RW` era a desejada. Verifique novamente a flag na chamada `SYSCTL_ADD_*` e refaça o build.

Se a flag estiver correta e a falha persistir, o usuário pode não estar executando como root. Escritas via sysctl exigem o privilégio `PRIV_SYSCTL` por padrão; use `sudo` para realizar a escrita.

### `sysctl` Trava ou Causa um Deadlock

O handler do OID está tentando adquirir o giant lock (porque `CTLFLAG_MPSAFE` está ausente) ao mesmo tempo em que outra thread segura o giant lock e chama o driver. A correção é adicionar `CTLFLAG_MPSAFE` ao campo de flags de cada OID. Kernels modernos assumem MPSAFE em todo lugar; a ausência do flag é uma questão de revisão de código.

Uma causa mais sutil é um handler que tenta adquirir o mutex do softc enquanto outra thread já segura esse mutex e está lendo via sysctl. Audite o handler: ele deve calcular o valor com o mutex adquirido, mas chamar `sysctl_handle_*` fora do mutex. O `myfirst_sysctl_message_len` do capítulo segue esse padrão.

### `kldunload myfirst` Falha com "Device busy"

O detach foi recusado porque algum usuário mantém o dispositivo aberto. Localize-o com `fstat | grep myfirst0` e peça que feche o arquivo ou encerre o processo. Assim que o dispositivo for liberado, o descarregamento funcionará.

Se `fstat` não mostrar nada e o descarregamento ainda falhar, a causa mais provável é um `dev_ref` vazado. Verifique se todo caminho de código no driver que chama `dev_ref` também chama `dev_rel`; em especial, qualquer caminho de erro dentro de `myfirst_open` deve liberar toda referência adquirida antes da falha.

### `kldunload myfirst` Causa um Kernel Panic

O detach do driver está destruindo ou liberando algo que o kernel ainda está usando. As duas causas mais comuns são:

* O detach liberou o softc antes de destruir o cdev. Os callbacks do cdev podem ainda estar em execução; eles acessam `si_drv1`, obtêm lixo e causam panic. A correção é seguir a ordem estrita: `destroy_dev` (que aguarda a conclusão de todos os callbacks em andamento) primeiro, depois mutex_destroy e então retornar; o framework libera o softc.

* O detach esqueceu de remover o registro de um event handler. O próximo evento dispara após o descarregamento e salta para memória já liberada. A correção é chamar `EVENTHANDLER_DEREGISTER` para cada `EVENTHANDLER_REGISTER` feito no attach.

As mensagens `Lock order reversal` e `WITNESS` no `dmesg` são diagnósticos úteis para ambos os casos. Um panic com `page fault while in kernel mode` e um valor corrompido em `%rip` corresponde ao segundo padrão; um panic com `lock order reversal` e um stack trace passando pelos dois subsistemas corresponde ao primeiro.

### Probes do DTrace Não Estão Visíveis

`dtrace -l -P myfirst` não retorna nada mesmo com o módulo carregado. A causa é quase sempre que as probes SDT estão declaradas em um header mas não definidas em lugar nenhum. As probes precisam tanto de `SDT_PROBE_DECLARE` (no header, onde os consumidores as enxergam) quanto de `SDT_PROBE_DEFINE*` (em exatamente um arquivo-fonte, que é o dono do armazenamento da probe). O padrão do capítulo coloca as definições em `myfirst_debug.c`. Se esse arquivo não estiver em `SRCS`, as probes não serão definidas e o DTrace não verá nada.

Uma causa mais sutil é a probe SDT ter sido renomeada no header sem que o `SDT_PROBE_DEFINE*` correspondente fosse atualizado. O build ainda terá sucesso porque as duas declarações referenciam símbolos diferentes, mas o DTrace só enxerga o nome definido. Audite o header e o fonte em busca do mesmo nome de probe.

### A Árvore sysctl Sobrevive ao Descarregamento e Trava o Próximo sysctl

Isso acontece quando o driver usa seu próprio contexto sysctl (em vez do contexto por dispositivo) e esquece de chamar `sysctl_ctx_free` no detach. Os OIDs referenciam campos do softc já liberado; a próxima varredura sysctl desreferencia a memória liberada e o kernel ou entra em panic ou retorna lixo. A correção é usar `device_get_sysctl_ctx`, que o framework limpa automaticamente.

### Lista de Verificação de Diagnóstico Geral

Quando algo der errado e a causa não for óbvia, percorra esta lista curta antes de recorrer ao `kgdb`:

1. `kldstat | grep <driver>`: o módulo está realmente carregado?
2. `dmesg | tail`: há mensagens do kernel mencionando o driver?
3. `ls -l /dev/<driver>0`: o nó de dispositivo existe com o modo esperado?
4. `sysctl dev.<driver>.0.%driver`: o Newbus conhece o dispositivo?
5. `fstat | grep <driver>0`: alguém está com o dispositivo aberto?
6. `dtrace -l -P <driver>`: as probes SDT estão registradas?
7. Releia a função attach e verifique se cada etapa tem uma limpeza correspondente no detach.

Os seis primeiros comandos levam dez segundos e eliminam a maioria dos problemas comuns. O sétimo é o demorado, mas é quase sempre a resposta final para qualquer bug que os seis primeiros não revelaram.

### Perguntas Frequentes

As perguntas a seguir aparecem com frequência suficiente durante o trabalho de integração para que o capítulo termine com um pequeno FAQ. Cada resposta é intencionalmente compacta; a seção relevante do capítulo traz a discussão completa.

**P1. Por que usar tanto ioctl quanto sysctl quando eles parecem se sobrepor?**

Eles respondem a perguntas diferentes. O ioctl é para um programa que já abriu o dispositivo e quer emitir um comando (solicitar um estado, enviar um novo estado, disparar uma ação). O sysctl é para um operador no prompt do shell ou um script que quer inspecionar ou ajustar o estado sem abrir nada. O mesmo valor pode ser exposto pelas duas interfaces, e muitos drivers em produção fazem exatamente isso: um `MYFIRSTIOC_GETMSG` para programas e um `dev.myfirst.0.message` para humanos. Cada usuário escolhe o canal que melhor se adapta ao seu contexto.

**P2. Quando devo usar mmap em vez de read/write/ioctl?**

Use `mmap` quando os dados forem grandes, acessados aleatoriamente e residam naturalmente em um endereço de memória (um frame buffer, um anel de descritores DMA, um espaço de registradores mapeados em memória). Use `read`/`write` quando os dados forem sequenciais, orientados a bytes e pequenos por chamada. Use `ioctl` para comandos de controle. Os três não se excluem; muitos drivers expõem os três (como `vt(4)` faz para o console).

**P3. Por que o capítulo usa `make_dev_s` em vez de `make_dev`?**

`make_dev_s` é a forma moderna preferida. Ela retorna um erro explícito em vez de entrar em panic por nome duplicado; aceita uma estrutura de argumentos para que novas opções sejam adicionadas sem grandes mudanças; e é o que a maioria dos drivers atuais usa. O antigo `make_dev` ainda funciona, mas é desencorajado para código novo.

**P4. Preciso declarar `D_TRACKCLOSE`?**

Você precisa dele se o `d_close` do seu driver deve ser chamado apenas no último fechamento de um descritor de arquivo (o significado natural de "fechar"). Sem ele, o kernel chama `d_close` para cada fechamento de cada descritor duplicado, o que surpreende a maioria dos drivers. Defina-o em qualquer cdevsw novo, a menos que tenha uma razão específica para não fazê-lo.

**P5. Quando devo incrementar `MODULE_VERSION`?**

Quando algo na API interna do driver no kernel mudar de forma incompatível. Adicionar novos símbolos exportados está tudo bem; renomear ou removê-los exige incremento. Mudar o layout de uma estrutura publicamente visível exige incremento. Incrementar a versão do módulo força os dependentes (consumidores de `MODULE_DEPEND`) a serem reconstruídos.

**P6. Quando devo incrementar a constante de versão da API no meu header público?**

Quando algo na interface visível ao usuário mudar de forma incompatível. Adicionar um novo ioctl está tudo bem; mudar o layout da estrutura de argumento de um ioctl existente exige incremento. Renumerar um ioctl existente exige incremento. Incrementar a versão da API permite que programas em espaço do usuário detectem incompatibilidade antes de emitir chamadas.

**P7. Devo desanexar meus OIDs em `myfirst_detach`?**

Não, se você usou `device_get_sysctl_ctx` (o contexto por dispositivo). O framework limpa o contexto por dispositivo automaticamente após um detach bem-sucedido. Você só precisa de limpeza explícita se usou `sysctl_ctx_init` para criar seu próprio contexto.

**P8. Por que meu detach entra em panic com "invalid memory access"?**

Quase sempre porque os callbacks do cdev ainda estão em execução quando o driver libera algo que eles referenciam. A correção é chamar `destroy_dev(sc->sc_cdev)` primeiro; `destroy_dev` bloqueia até que todos os callbacks em andamento retornem. Depois que retorna, o cdev foi destruído e nenhum novo callback pode chegar. Só então é seguro liberar o softc, os locks e assim por diante. A ordem estrita não é negociável.

**P9. Qual é a diferença entre `dev_ref` / `dev_rel` e `D_TRACKCLOSE`?**

`D_TRACKCLOSE` é um flag do cdevsw que controla quando o kernel chama `d_close`: com ele, apenas no último fechamento; sem ele, em todo fechamento. `dev_ref`/`dev_rel` é um mecanismo de contagem de referências que permite ao driver adiar o detach até que todas as referências pendentes sejam liberadas. Eles são independentes e complementares. O capítulo usa `D_TRACKCLOSE` no estágio 1; o Desafio 3 demonstra `dev_ref`/`dev_rel`.

**P10. Por que minha escrita via sysctl retorna EPERM mesmo sendo root?**

Três causas possíveis. (a) O OID foi criado apenas com `CTLFLAG_RD`; adicione `CTLFLAG_RW`. (b) O OID tem `CTLFLAG_SECURE` e o sistema está com `securelevel > 0`; reduza o securelevel ou remova o flag. (c) O usuário não é realmente root, mas está em uma jail sem `allow.sysvipc` ou similar; root dentro de uma jail não tem `PRIV_SYSCTL` para OIDs arbitrários.

**P11. Meu handler sysctl está adquirindo o giant lock sem precisar. O que esqueci?**

`CTLFLAG_MPSAFE` no campo de flags. Sem ele, o kernel adquire o giant lock em torno de cada chamada ao handler. Adicione-o em todo lugar; kernels modernos assumem MPSAFE em todo lugar.

**P12. Devo usar letra maiúscula ou minúscula para o grupo do meu ioctl?**

Maiúscula para drivers novos. As letras minúsculas são amplamente usadas pelos subsistemas base (`'d'` para disco, `'i'` para `if_ioctl`, `'t'` para terminal) e a chance de colisão é real. As letras maiúsculas estão em sua maioria livres, e um driver novo deve escolher uma delas.

**P13. Meu ioctl retorna `Inappropriate ioctl for device` e não entendo por quê.**

O kernel retornou `ENOTTY` porque (a) o dispatcher retornou `ENOIOCTL` para o comando (adicione um case para ele) ou (b) o comprimento codificado no código da requisição não corresponde ao buffer que o usuário passou (recompile os dois lados contra o mesmo header).

**P14. Devo usar `strncpy` ou `strlcpy` no kernel?**

`strlcpy`. Ela garante o terminador NUL e nunca ultrapassa o destino. `strncpy` não garante nenhuma das duas coisas e é fonte frequente de bugs sutis. A página de manual `style(9)` do FreeBSD recomenda `strlcpy` para todo código novo.

**P15. Meu módulo carrega mas `dmesg` não mostra mensagens do meu driver. O que está errado?**

A máscara de debug do driver está em zero. A macro `DPRINTF` do capítulo imprime apenas quando o bit da máscara está definido. Defina a máscara antes de carregar (`kenv hw.myfirst.debug_mask_default=0xff`) ou após carregar (`sysctl dev.myfirst.0.debug.mask=0xff`).

**P16. Por que o capítulo menciona DTrace com tanta frequência?**

Porque ele é a ferramenta de depuração mais produtiva no kernel do FreeBSD e porque a infraestrutura de debug do Capítulo 23 foi projetada para se integrar a ele. As probes SDT oferecem ao operador um ponto de acesso em tempo de execução em cada superfície de integração sem precisar reconstruir o driver. Um driver que expõe probes SDT com bons nomes é muito mais fácil de depurar do que um que não o faz.

**P17. Posso usar este driver como modelo para um driver de hardware real?**

A superfície de integração (cdev, ioctl, sysctl) se traduz diretamente. As partes específicas de hardware (alocação de recursos, tratamento de interrupções, configuração de DMA) chegam nos Capítulos 18 a 22 da Parte IV. Um driver PCI real tipicamente combina os padrões estruturais da Parte IV com os padrões de integração deste capítulo para chegar a um driver pronto para produção.

**P18. Como concedo acesso a `/dev/myfirst0` para um usuário não-root sem reconstruir o driver?**

Use `devfs.rules(5)`. Adicione um arquivo de regras em `/etc/devfs.rules` que corresponda ao nome do dispositivo e defina o proprietário, o grupo ou o modo em tempo de execução. Por exemplo, para permitir que o grupo `operator` leia e escreva em `/dev/myfirst*`:

```text
[myfirst_rules=10]
add path 'myfirst*' mode 0660 group operator
```

Habilite o conjunto de regras com `devfs_system_ruleset="myfirst_rules"` em `/etc/rc.conf` e `service devfs restart`. Os campos `mda_uid`, `mda_gid` e `mda_mode` do driver ainda definem os padrões no momento da criação; `devfs.rules` permite que o administrador os substitua sem tocar no código-fonte.

**P19. Minha lista `SRCS` continua crescendo. Isso é um problema?**

Não por si só. A linha `SRCS` no `Makefile` do módulo do kernel lista cada arquivo-fonte que compila no módulo; aumentar essa lista à medida que novas responsabilidades ganham seus próprios arquivos é algo normal e esperado. O driver do Estágio 3 deste capítulo já tem quatro arquivos-fonte (`myfirst.c`, `myfirst_debug.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`), e o Capítulo 25 adicionará mais. O sinal de alerta não é o número de entradas, mas a falta de estrutura: se `SRCS` contém arquivos não relacionados que foram agrupados sem um esquema de nomenclatura, o driver cresceu além da sua organização atual e merece uma pequena refatoração. O Capítulo 25 trata essa refatoração como um hábito fundamental.

**Q20. O que devo fazer a seguir?**

Leia o Capítulo 25 (tópicos avançados e dicas práticas) para transformar este driver integrado em um driver *manutenível*, resolva os desafios do capítulo se quiser prática hands-on, e consulte um dos drivers in-tree citados no cartão de referência para ver um exemplo completamente desenvolvido. O driver `null(4)` é o ponto de entrada mais simples; o driver Ethernet `if_em` é o mais completo; o driver de armazenamento `ahci(4)` mostra os padrões do CAM. Escolha o que estiver mais próximo do que você quer construir e leia-o do início ao fim.

## Encerrando

Este capítulo levou o `myfirst` de um módulo funcional, porém isolado, para um driver FreeBSD totalmente integrado. O arco foi deliberado: cada seção adicionou uma superfície de integração concreta e terminou com o driver mais útil e mais detectável do que estava no início. O leitor que percorreu os laboratórios da Seção 9 tem agora, em disco, um driver que expõe um cdev corretamente construído em `/dev`, quatro ioctls bem projetados sob um header público, uma árvore sysctl autodescritiva em `dev.myfirst.0`, um tunable de boot via `/boot/loader.conf`, e um ciclo de vida limpo que sobrevive a ciclos de carga/descarga sem vazamento de recursos.

Os marcos técnicos ao longo do caminho foram:

* O Estágio 1 (Seção 2) substituiu a chamada mais antiga a `make_dev` pela forma moderna `make_dev_args`, preencheu `D_TRACKCLOSE`, conectou `si_drv1` para o estado por cdev, e percorreu o ciclo de vida do cdev desde a criação, passando pelo drain, até a destruição. A presença do driver em `/dev` tornou-se de primeira classe.

* O Estágio 2 (Seção 3) adicionou os ioctls `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` e `MYFIRSTIOC_RESET`, além do header público correspondente `myfirst_ioctl.h`. O dispatcher reutiliza a infraestrutura de depuração do Capítulo 23 (`MYF_DBG_IOCTL` e o probe SDT `myfirst:::ioctl`). O programa `myfirstctl` do espaço do usuário, fornecido como complemento, demonstrou como uma pequena ferramenta de linha de comando exercita cada ioctl sem jamais decodificar um código de requisição manualmente.

* O Estágio 3 (Seção 4) adicionou a árvore sysctl `dev.myfirst.0.*`, incluindo uma subárvore `debug.` que permite ao operador inspecionar e modificar a máscara de depuração em tempo de execução, um OID `version` que reporta a versão de integração, contadores de atividade de leitura e escrita, e um OID de string para a mensagem atual. O tunable de boot `hw.myfirst.debug_mask_default` permite ao operador pré-carregar a máscara de depuração antes do attach.

* As Seções 5 e 6 esboçaram a mesma integração de estilo de registro aplicada à pilha de rede (`if_alloc`, `if_attach`, `bpfattach`) e à pilha de armazenamento CAM (`cam_sim_alloc`, `xpt_bus_register`, `xpt_action`). O leitor que não está construindo um driver de rede ou armazenamento ainda obteve um padrão útil: todo registro de framework no FreeBSD usa a mesma forma alocar-nomear-preencher-attach-tráfego-detach-liberar.

* A Seção 7 codificou a disciplina de ciclo de vida que une tudo: todo registro bem-sucedido deve ser pareado com um cancelamento de registro na ordem inversa, e um attach que falha deve desfazer cada etapa anterior antes de retornar. A cadeia `goto err` é a codificação canônica dessa regra.

* A Seção 8 enquadrou o capítulo como um passo em um arco mais longo: o `myfirst` começou como uma demonstração de arquivo único, cresceu para um driver de múltiplos arquivos ao longo das Partes II a IV, ganhou depuração e rastreamento no Capítulo 23, e ganhou a superfície de integração aqui. A versão de release, a versão do módulo e a versão da API cada uma rastreia um aspecto diferente dessa evolução; incrementar cada uma no momento certo é a disciplina de versionamento de um driver de longa duração.

Os laboratórios do capítulo (Seção 9) guiaram o leitor por cada marco, os desafios (Seção 10) ofereceram ao leitor motivado trabalho de continuação, e o catálogo de resolução de problemas (Seção 11) reuniu os sintomas e correções mais comuns para consulta rápida.

O resultado é um marco do driver (`1.7-integration`) que o leitor pode levar para o próximo capítulo sem nenhum trabalho de integração inacabado à espreita. Os padrões deste capítulo (construção de cdev, design de ioctl, árvores sysctl, disciplina de ciclo de vida) são também os padrões que o restante da Parte V e a maior parte das Partes VI e VII assumirão que o leitor conhece.

## Ponte para o Capítulo 25

O Capítulo 25 (Tópicos Avançados e Dicas Práticas) encerra a Parte 5 transformando o driver integrado deste capítulo em um driver *sustentável*. Enquanto o Capítulo 24 adicionou as interfaces que permitem ao driver comunicar-se com o restante do sistema, o Capítulo 25 ensina os hábitos de engenharia que mantêm essas interfaces estáveis e legíveis à medida que o driver absorve o próximo ano de correções de bugs, mudanças de portabilidade e solicitações de funcionalidades. O driver cresce de `1.7-integration` para `1.8-maintenance`; as adições visíveis são modestas, mas a disciplina por trás delas é o que separa um driver que sobrevive a um ciclo de desenvolvimento de um que sobrevive a uma década.

A ponte do Capítulo 24 para o Capítulo 25 tem quatro partes concretas.

Primeiro, o logging com limitação de taxa que o Capítulo 25 introduz se apoia diretamente sobre o macro `DPRINTF` do Capítulo 23 e as superfícies de integração que este capítulo adicionou. Um novo macro `DLOG_RL` construído em torno de `ppsratecheck(9)` permite ao driver manter as mesmas classes de depuração que já utiliza, mas sem inundar o `dmesg` durante uma tempestade de eventos. A disciplina é simples: escolha um limite por segundo, aplique-o nos pontos de chamada de depuração existentes, e audite os poucos lugares onde um `device_printf` irrestrito poderia ser executado em loop.

Segundo, os caminhos de ioctl e sysctl que este capítulo construiu serão auditados no Capítulo 25 para um vocabulário de errno consistente. O capítulo distingue `EINVAL` de `ENXIO`, `ENOIOCTL` de `ENOTTY`, `EBUSY` de `EAGAIN`, e `EPERM` de `EACCES`, de modo que cada superfície de integração retorne o código correto em cada falha. O leitor percorre o dispatcher escrito na Seção 3 e os handlers de sysctl escritos na Seção 4, ajustando-os onde o erro incorreto foi retornado.

Terceiro, o tunable de boot `hw.myfirst.debug_mask_default` introduzido na Seção 4 será generalizado no Capítulo 25 em um vocabulário de tunables pequeno, porém disciplinado, por meio de `TUNABLE_INT_FETCH`, `TUNABLE_LONG_FETCH`, `TUNABLE_BOOL_FETCH` e `TUNABLE_STR_FETCH`, cooperando com sysctls graváveis sob `CTLFLAG_TUN`. O mesmo trio `MYFIRST_VERSION`, `MODULE_VERSION` e `MYFIRST_IOCTL_VERSION` que este capítulo estabeleceu será estendido com um ioctl `MYFIRSTIOC_GETCAPS` para que ferramentas do espaço do usuário possam detectar funcionalidades em tempo de execução sem tentativa e erro.

Quarto, a cadeia `goto err` introduzida na Seção 7 será promovida de exercício de laboratório para o padrão de limpeza de produção do driver, e a refatoração do capítulo moverá a lógica de attach do Newbus e os callbacks do cdev para arquivos separados (`myfirst_bus.c` e `myfirst_cdev.c`), ao lado de um `myfirst_log.c` para os novos macros de logging. O Capítulo 25 também introduz `SYSINIT(9)` e `SYSUNINIT(9)` para inicialização em nível de driver e um handler de evento `shutdown_pre_sync` por meio de `EVENTHANDLER(9)`, adicionando mais duas superfícies de estilo de registro às que este capítulo já ensinou.

Avance com a confiança de que o vocabulário de integração está agora estabelecido. O Capítulo 25 pega este driver e o deixa pronto para o longo prazo; a Parte 6 então inicia os capítulos específicos de transporte que se apoiam em cada hábito que a Parte 5 construiu.

## Cartão de Referência e Glossário

As páginas restantes do capítulo constituem uma referência compacta. Elas foram projetadas para serem lidas na íntegra na primeira vez e consultadas diretamente quando o leitor precisar buscar algo. A ordem é: um cartão de referência dos macros, estruturas e flags importantes; um glossário do vocabulário de integração; e um breve diretório dos arquivos complementares fornecidos com o capítulo.

### Referência Rápida: Construção do cdev

| Função | Quando usar |
|----------|-------------|
| `make_dev_args_init(args)` | Sempre antes de `make_dev_s`; zera a struct de args com segurança. |
| `make_dev_s(args, &cdev, fmt, ...)` | A forma moderna preferida. Retorna 0 ou errno. |
| `make_dev(devsw, unit, uid, gid, mode, fmt, ...)` | Forma antiga de chamada única. Desaconselhada para código novo. |
| `make_dev_credf(flags, ...)` | Quando você precisar dos bits de flag `MAKEDEV_*`. |
| `destroy_dev(cdev)` | Sempre no detach; esgota os callbacks em andamento. |
| `destroy_dev_drain(cdev)` | Quando o detach precisa aguardar referências pendentes. |

### Referência Rápida: Flags do cdevsw

| Flag | Significado |
|------|---------|
| `D_VERSION` | Obrigatório; identifica a versão do layout do cdevsw. |
| `D_TRACKCLOSE` | Chama `d_close` apenas no último fechamento. Recomendado. |
| `D_NEEDGIANT` | Adquire o giant lock em torno de cada callback. Desaconselhado. |
| `D_DISK` | O cdev representa um disco; usa bio em vez de uio para I/O. |
| `D_TTY` | O cdev é um terminal; afeta o roteamento da disciplina de linha. |
| `D_MMAP_ANON` | O cdev suporta mmap anônimo. |
| `D_MEM` | O cdev é semelhante a `/dev/mem`; acesso direto à memória. |

### Referência Rápida: Flags de `make_dev` (`MAKEDEV_*`)

| Flag | Significado |
|------|---------|
| `MAKEDEV_REF` | Adquire uma referência extra; o chamador deve invocar `dev_rel` depois. |
| `MAKEDEV_NOWAIT` | Não dorme; retorna `ENOMEM` se não houver memória. |
| `MAKEDEV_WAITOK` | Dormir é permitido (padrão para a maioria dos chamadores). |
| `MAKEDEV_ETERNAL` | O cdev nunca desaparece; certas otimizações se aplicam. |
| `MAKEDEV_ETERNAL_KLD` | Como ETERNAL, mas apenas pelo tempo de vida do kld. |
| `MAKEDEV_CHECKNAME` | Valida o nome; `ENAMETOOLONG` se for muito longo. |

### Referência Rápida: Macros de Codificação de ioctl

Todos em `/usr/src/sys/sys/ioccom.h`.

| Macro | Direção | Argumento |
|-------|-----------|----------|
| `_IO(g, n)` | nenhuma | nenhum |
| `_IOR(g, n, t)` | saída | tipo `t`, tamanho `sizeof(t)` |
| `_IOW(g, n, t)` | entrada | tipo `t`, tamanho `sizeof(t)` |
| `_IOWR(g, n, t)` | entrada e saída | tipo `t`, tamanho `sizeof(t)` |
| `_IOWINT(g, n)` | nenhuma, mas o valor do int é passado | int |

Os argumentos significam:

* `g`: letra de grupo, convencionalmente `'M'` para `myfirst` e similares.
* `n`: número de comando, monotonicamente crescente dentro do grupo.
* `t`: tipo do argumento, usado apenas pelo seu `sizeof`.

O tamanho máximo é `IOCPARM_MAX = 8192` bytes. Para transferências maiores, use o padrão de ponteiro embutido (Desafio 1) ou um mecanismo diferente, como `mmap` ou `read`/`write`.

### Referência Rápida: Assinatura de `d_ioctl_t`

```c
int d_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
            int fflag, struct thread *td);
```

| Argumento | Significado |
|----------|---------|
| `dev` | O cdev que suporta o descritor de arquivo. Use `dev->si_drv1` para obter o softc. |
| `cmd` | O código de requisição do espaço do usuário. Comparado a constantes nomeadas. |
| `data` | Buffer do lado do kernel com os dados do usuário. Desreferenciamento direto; `copyin` não é necessário. |
| `fflag` | Flags de arquivo da chamada de abertura (`FREAD`, `FWRITE`). Verifique antes de modificar. |
| `td` | Thread chamadora. Use `td->td_ucred` para credenciais. |

Retorne 0 em caso de sucesso, um errno positivo em caso de falha, ou `ENOIOCTL` para comandos desconhecidos.

### Referência Rápida: Macros de OID do sysctl

Todos em `/usr/src/sys/sys/sysctl.h`.

| Macro | Adiciona |
|-------|------|
| `SYSCTL_ADD_INT(ctx, parent, nbr, name, flags, ptr, val, descr)` | int com sinal, respaldado por `*ptr`. |
| `SYSCTL_ADD_UINT` | int sem sinal. |
| `SYSCTL_ADD_LONG` / `SYSCTL_ADD_ULONG` | long / unsigned long. |
| `SYSCTL_ADD_S64` / `SYSCTL_ADD_U64` | 64 bits com sinal / sem sinal. |
| `SYSCTL_ADD_BOOL` | Booleano (preferível a int 0/1). |
| `SYSCTL_ADD_STRING(ctx, parent, nbr, name, flags, ptr, len, descr)` | String terminada em NUL. |
| `SYSCTL_ADD_NODE(ctx, parent, nbr, name, flags, handler, descr)` | Nó de subárvore. |
| `SYSCTL_ADD_PROC(ctx, parent, nbr, name, flags, arg1, arg2, handler, fmt, descr)` | OID respaldado por handler. |

### Referência Rápida: Bits de Flag do sysctl

| Flag | Significado |
|------|---------|
| `CTLFLAG_RD` | Somente leitura. |
| `CTLFLAG_WR` | Somente escrita (raro). |
| `CTLFLAG_RW` | Leitura e escrita. |
| `CTLFLAG_TUN` | Tunable do loader; lido no boot de `/boot/loader.conf`. |
| `CTLFLAG_MPSAFE` | O handler é seguro sem o giant lock. **Sempre defina para código novo.** |
| `CTLFLAG_PRISON` | Visível dentro de jails. |
| `CTLFLAG_VNET` | Por VNET (pilha de rede virtualizada). |
| `CTLFLAG_DYN` | OID dinâmico; definido automaticamente por `SYSCTL_ADD_*`. |
| `CTLFLAG_SECURE` | Somente leitura quando `securelevel > 0`. |

### Referência Rápida: Bits de Tipo do sysctl

Combinados com OR no campo de flags para `SYSCTL_ADD_PROC` e similares.

| Flag | Significado |
|------|---------|
| `CTLTYPE_INT` / `CTLTYPE_UINT` | int com sinal / sem sinal. |
| `CTLTYPE_LONG` / `CTLTYPE_ULONG` | long / unsigned long. |
| `CTLTYPE_S64` / `CTLTYPE_U64` | 64 bits com sinal / sem sinal. |
| `CTLTYPE_STRING` | String terminada em NUL. |
| `CTLTYPE_OPAQUE` | Blob opaco; raramente usado em código novo. |
| `CTLTYPE_NODE` | Nó de subárvore. |

### Referência Rápida: Strings de Formato do Handler de sysctl

Usado por `SYSCTL_ADD_PROC` para indicar ao `sysctl(8)` como exibir o valor.

| Formato | Tipo |
|---------|------|
| `"I"` | int |
| `"IU"` | unsigned int |
| `"L"` | long |
| `"LU"` | unsigned long |
| `"Q"` | int64 |
| `"QU"` | uint64 |
| `"A"` | string terminada em NUL |
| `"S,structname"` | struct opaca (raro) |

### Referência Rápida: Modelo de Handler de sysctl

```c
static int
my_handler(SYSCTL_HANDLER_ARGS)
{
        struct my_softc *sc = arg1;
        u_int val;

        /* Read the current value into val under the mutex. */
        mtx_lock(&sc->sc_mtx);
        val = sc->sc_field;
        mtx_unlock(&sc->sc_mtx);

        /* Let the framework do the read or write. */
        return (sysctl_handle_int(oidp, &val, 0, req));
}
```

### Referência Rápida: Contexto de sysctl por Dispositivo

```c
struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
struct sysctl_oid      *tree = device_get_sysctl_tree(dev);
struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
```

O framework é o dono do ctx; o driver não deve chamar `sysctl_ctx_free` nele. O framework limpa tudo automaticamente após um detach bem-sucedido.

### Referência Rápida: Tunáveis do Loader

```c
TUNABLE_INT_FETCH("hw.driver.knob", &sc->sc_knob);
TUNABLE_LONG_FETCH("hw.driver.knob", &sc->sc_knob);
TUNABLE_STR_FETCH("hw.driver.knob", buf, sizeof(buf));
```

O primeiro argumento é o nome da variável do loader. O segundo é um ponteiro para o destino, que também é o valor padrão caso a variável não esteja definida.

### Referência Rápida: Ciclo de Vida do ifnet

| Função | Quando |
|----------|------|
| `if_alloc(IFT_<type>)` | Aloca o ifnet. |
| `if_initname(ifp, name, unit)` | Define o nome visível ao usuário (o `ifconfig` o exibe). |
| `if_setflags(ifp, flags)` | Define as flags `IFF_*`. |
| `if_setsoftc(ifp, sc)` | Associa o softc do driver. |
| `if_setioctlfn(ifp, fn)` | Define o handler de ioctl. |
| `if_settransmitfn(ifp, fn)` | Define a função de transmissão. |
| `if_attach(ifp)` | Torna a interface visível. |
| `bpfattach(ifp, dlt, hdrlen)` | Torna o tráfego visível ao BPF. |
| `bpfdetach(ifp)` | Desfaz o `bpfattach`. |
| `if_detach(ifp)` | Desfaz o `if_attach`. |
| `if_free(ifp)` | Libera o ifnet. |

### Referência Rápida: Ciclo de Vida do CAM SIM

| Função | Quando |
|----------|------|
| `cam_simq_alloc(maxq)` | Aloca a fila do dispositivo. |
| `cam_sim_alloc(action, poll, name, sc, unit, mtx, max_tagged, max_dev_tx, devq)` | Aloca o SIM. |
| `xpt_bus_register(sim, dev, 0)` | Registra o barramento no CAM. |
| `xpt_create_path(&path, NULL, cam_sim_path(sim), targ, lun)` | Cria um path para eventos. |
| `xpt_action(ccb)` | Envia um CCB ao CAM. |
| `xpt_done(ccb)` | Informa ao CAM que o driver concluiu um CCB. |
| `xpt_free_path(path)` | Desfaz o `xpt_create_path`. |
| `xpt_bus_deregister(cam_sim_path(sim))` | Desfaz o `xpt_bus_register`. |
| `cam_sim_free(sim, free_devq)` | Desfaz o `cam_sim_alloc`. Passe `TRUE` para liberar também o devq. |

### Referência Rápida: Ciclo de Vida do Módulo

```c
static moduledata_t mymod = {
        "myfirst",        /* name */
        myfirst_modevent, /* event handler, can be NULL */
        NULL              /* extra data, rarely used */
};
DECLARE_MODULE(myfirst, mymod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(myfirst, 1);
MODULE_DEPEND(myfirst, otherdriver, 1, 1, 1);
```

A assinatura do event handler é `int (*)(module_t mod, int what, void *arg)`. O argumento `what` é um de `MOD_LOAD`, `MOD_UNLOAD`, `MOD_QUIESCE` ou `MOD_SHUTDOWN`. Retorne 0 em caso de sucesso ou um errno positivo.

### Referência Rápida: Event Handler

```c
eventhandler_tag tag;

tag = EVENTHANDLER_REGISTER(event_name, callback,
    arg, EVENTHANDLER_PRI_ANY);

EVENTHANDLER_DEREGISTER(event_name, tag);
```

Nomes de eventos comuns: `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, `power_suspend_early`, `power_resume`.

### Referência Rápida: Convenções de Errno

| Errno | Quando retornar |
|-------|----------------|
| `0` | Sucesso. |
| `EINVAL` | Os argumentos são reconhecidos, mas inválidos. |
| `EBADF` | O descritor de arquivo não foi aberto corretamente para a operação. |
| `EBUSY` | O recurso está em uso (frequentemente retornado por detach). |
| `ENOIOCTL` | O comando ioctl é desconhecido. **Use este para o caso padrão em `d_ioctl`.** |
| `ENOTTY` | A tradução do kernel de `ENOIOCTL` para o espaço do usuário. |
| `ENOMEM` | A alocação falhou. |
| `EAGAIN` | Tente novamente mais tarde (frequentemente retornado em I/O não bloqueante). |
| `EPERM` | O chamador não possui o privilégio necessário. |
| `EOPNOTSUPP` | A operação não é suportada por este driver. |
| `EFAULT` | Um ponteiro do usuário é inválido (retornado por `copyin`/`copyout` com falha). |
| `ETIMEDOUT` | Uma espera expirou. |
| `EIO` | Erro genérico de I/O do hardware. |

### Referência Rápida: Bits de Classe de Debug (do Capítulo 23)

| Bit | Nome | Usado para |
|-----|------|----------|
| `0x01` | `MYF_DBG_INIT` | probe / attach / detach |
| `0x02` | `MYF_DBG_OPEN` | ciclo de vida de open / close |
| `0x04` | `MYF_DBG_IO` | caminhos de read / write |
| `0x08` | `MYF_DBG_IOCTL` | tratamento de ioctl |
| `0x10` | `MYF_DBG_INTR` | handler de interrupção |
| `0x20` | `MYF_DBG_DMA` | mapeamento/sincronização de DMA |
| `0x40` | `MYF_DBG_PWR` | eventos de gerenciamento de energia |
| `0x80` | `MYF_DBG_MEM` | rastreamento de malloc/free |
| `0xFFFFFFFF` | `MYF_DBG_ANY` | todas as classes |
| `0` | `MYF_DBG_NONE` | sem registro |

A máscara é definida por instância via `dev.<driver>.<unit>.debug.mask`, ou globalmente na inicialização via `hw.<driver>.debug_mask_default` em `/boot/loader.conf`.

### Glossário de Vocabulário de Integração

**API version**: Um inteiro exposto pela interface ioctl de um driver (tipicamente por meio de um ioctl `GETVER`) que programas em espaço do usuário podem consultar para detectar mudanças na interface pública do driver. Incrementado apenas quando a interface visível ao usuário muda de forma incompatível.

**bpfattach**: A função que conecta um `ifnet` ao mecanismo BPF (Berkeley Packet Filter) para que `tcpdump` e ferramentas similares possam observar seu tráfego. Deve ser pareada com `bpfdetach`.

**bus_generic_detach**: Uma função auxiliar que desanexa todos os filhos de um driver estilo barramento. Usada como primeiro passo no detach de um driver de barramento para liberar os dispositivos filhos antes que o pai destrua seu próprio estado.

**CAM**: O Common Access Method, o subsistema de armazenamento do FreeBSD acima dos drivers de dispositivo. Gerencia a fila de I/O, a abstração de target/LUN e os drivers periféricos (`da`, `cd`, `sa`).

**CCB**: CAM Control Block. Uma única requisição de I/O estruturada como uma tagged union; o driver inspeciona `ccb->ccb_h.func_code` e despacha conforme o caso. Concluída via `xpt_done`.

**cdev**: Dispositivo de caracteres. O objeto por nó de dispositivo do kernel que sustenta uma entrada em `/dev`. Criado com `make_dev_s`, destruído com `destroy_dev`.

**cdevsw**: Character device switch. A tabela estática de callbacks (`d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, ...) que o kernel invoca em operações sobre um cdev.

**copyin / copyout**: Funções que transferem bytes entre endereços em espaço do usuário e espaço do kernel. O kernel as executa automaticamente para ioctls corretamente codificados; o driver as chama explicitamente apenas em padrões de ponteiro embutido.

**CTLFLAG_MPSAFE**: Uma flag de sysctl que declara o handler do OID como seguro para chamada sem o giant lock. Obrigatória em código novo; sem ela, o kernel adquire o giant lock em cada acesso.

**d_ioctl_t**: O tipo de ponteiro de função para o callback ioctl do cdevsw. Assinatura: `int (*)(struct cdev *, u_long, caddr_t, int, struct thread *)`.

**d_priv**: Um ponteiro privado por descritor de arquivo associado via `devfs_set_cdevpriv`. Usado para estado que deve estar vinculado a um único open, e não à instância do driver como um todo.

**dev_ref / dev_rel**: Um par de funções que incrementam e decrementam a contagem de referências de um cdev. Usadas para coordenar o detach com callbacks em andamento; veja o Exercício Desafio 3.

**devfs**: O sistema de arquivos gerenciado pelo kernel que sustenta `/dev`. O driver cria cdevs e o devfs os torna visíveis.

**devfs.rules(8)**: Um mecanismo de configuração para permissões do devfs em tempo de execução. Aplicado com `service devfs restart` após editar `/etc/devfs.rules`.

**DTrace**: O framework de rastreamento dinâmico. Os drivers expõem pontos de sonda por meio de macros SDT; scripts DTrace se conectam a eles em tempo de execução.

**EVENTHANDLER_REGISTER**: O mecanismo para registrar um callback para um evento nomeado de todo o sistema (`shutdown_pre_sync`, `vm_lowmem`, etc.). Deve ser pareado com `EVENTHANDLER_DEREGISTER`.

**ifnet**: O objeto por interface da pilha de rede. O equivalente de rede ao cdev.

**if_t**: O typedef opaco que a pilha de rede usa para `ifnet`. Os drivers manipulam a interface por meio de funções de acesso em vez de acesso direto a campos.

**IOC_VOID / IOC_IN / IOC_OUT / IOC_INOUT**: Os quatro bits de direção codificados em um código de requisição ioctl. Usados pelo kernel para decidir qual `copyin`/`copyout` executar.

**IOCPARM_MAX**: O tamanho máximo (8192 bytes) da estrutura de argumento de um ioctl conforme codificada no código de requisição. Transferências maiores exigem o padrão de ponteiro embutido.

**kldload / kldunload**: As ferramentas em espaço do usuário que carregam e descarregam módulos do kernel. Ambas invocam o event handler do módulo correspondente (`MOD_LOAD` e `MOD_UNLOAD`).

**make_dev_args**: A estrutura passada para `make_dev_s` para descrever um novo cdev. Inicializada com `make_dev_args_init`.

**make_dev_s**: A função moderna preferida para criar um cdev. Retorna 0 ou um errno positivo; define `*cdev` em caso de sucesso.

**MAKEDEV_***: Bits de flag passados para `make_dev_credf` e similares. Bits comuns: `MAKEDEV_REF`, `MAKEDEV_NOWAIT`, `MAKEDEV_ETERNAL_KLD`.

**MOD_LOAD / MOD_UNLOAD / MOD_SHUTDOWN**: Os eventos entregues ao event handler de um módulo. Retorne 0 para confirmar ou diferente de zero para rejeitar.

**MODULE_DEPEND**: A macro que declara a dependência de um módulo em relação a outro. O kernel usa os argumentos de versão (`min`, `pref`, `max`) para verificar a compatibilidade.

**MODULE_VERSION**: A macro que declara o número de versão de um módulo. Incrementada quando os usuários internos do kernel precisariam recompilar.

**Newbus**: O framework de árvore de dispositivos do FreeBSD. Gerencia o `device_t`, o softc por dispositivo, o contexto de sysctl por dispositivo e o ciclo de vida probe/attach/detach.

**OID**: Object identifier. Um nó na árvore de sysctl. OIDs estáticos são declarados em tempo de compilação; OIDs dinâmicos são adicionados em tempo de execução com `SYSCTL_ADD_*`.

**Path (CAM)**: Uma tripla `(bus, target, LUN)` que identifica um destino no CAM. Criado com `xpt_create_path`.

**Public header**: Um header que programas em espaço do usuário incluem para se comunicar com o driver. Deve compilar sem erros sem `_KERNEL` definido; usa apenas tipos estáveis.

**Registration framework**: Um subsistema do FreeBSD que expõe uma interface do tipo "alocar-nomear-preencher-associar-operar-desassociar-liberar" para drivers. Exemplos: redes (`ifnet`), armazenamento (CAM), som, USB, GEOM.

**Release version**: A string legível por humanos que identifica o release de um driver. Exposta via sysctl como `dev.<driver>.<unit>.version`.

**SDT**: Statically defined tracing. O mecanismo do kernel para pontos de sonda definidos em tempo de compilação, consumíveis pelo DTrace.

**si_drv1 / si_drv2**: Dois campos de ponteiro privados em `struct cdev` disponíveis para uso do driver. Por convenção, `si_drv1` aponta para o softc.

**SIM**: SCSI Interface Module. A visão do CAM sobre um adaptador de armazenamento. Alocado com `cam_sim_alloc`, registrado com `xpt_bus_register`.

**Soft detach**: Um padrão de detach em que o driver aguarda as referências pendentes chegarem a zero em vez de recusar o detach imediatamente. Veja o Exercício Desafio 3.

**Softc**: Software context. O estado por instância do driver. Alocado pelo Newbus e acessado via `device_get_softc(dev)`.

**SYSINIT**: Uma função de inicialização do kernel de execução única registrada em tempo de compilação. Executa em um estágio específico durante o boot. Raramente necessária em código de driver.

**SYSCTL_HANDLER_ARGS**: Uma macro que se expande para a lista de argumentos padrão de um handler de sysctl: `oidp, arg1, arg2, req`.

**TUNABLE_INT_FETCH**: Uma função que lê um valor do ambiente do loader e o grava em uma variável do kernel. A variável mantém seu valor anterior se a variável do loader estiver ausente.

**XPT**: CAM Transport Layer. O mecanismo central de despacho do CAM. O driver chama `xpt_action` para enviar um CCB; o CAM retorna via a função de ação do SIM para CCBs de I/O.

### Inventário de Arquivos Complementares

Os arquivos complementares deste capítulo estão em `examples/part-05/ch24-integration/` no repositório do livro. A estrutura de diretórios é:

```text
examples/part-05/ch24-integration/
├── README.md
├── INTEGRATION.md
├── stage1-devfs/
│   ├── Makefile
│   ├── myfirst.c             (with make_dev_args)
│   └── README.md
├── stage2-ioctl/
│   ├── Makefile
│   ├── Makefile.user         (for myfirstctl)
│   ├── myfirst_ioctl.c
│   ├── myfirst_ioctl.h       (PUBLIC)
│   ├── myfirstctl.c
│   └── README.md
├── stage3-sysctl/
│   ├── Makefile
│   ├── myfirst.c
│   ├── myfirst_sysctl.c
│   └── README.md
└── labs/
    ├── lab24_1_stage1.sh     (verification commands for Lab 1)
    ├── lab24_2_stage2.sh
    ├── lab24_3_stage3.sh
    ├── lab24_4_failure.sh
    ├── lab24_5_dtrace.sh
    ├── lab24_6_smoke.sh
    ├── lab24_7_reload.sh
    └── loader.conf.example
```

O ponto de partida do Laboratório 1 é o driver do próprio leitor ao final do Capítulo 23 (`1.6-debug`); `stage1-devfs/`, `stage2-ioctl/` e `stage3-sysctl/` são soluções de referência que o leitor pode consultar após concluir cada laboratório. O diretório de labs contém pequenos scripts shell que executam os comandos de verificação e que o leitor pode adaptar para seus próprios testes.

O `README.md` na raiz do capítulo descreve como usar o diretório, a ordem dos estágios e a relação entre as árvores de cada estágio. O `INTEGRATION.md` é um documento mais extenso que mapeia cada conceito do capítulo ao arquivo em que ele aparece.

### Onde a Árvore de Código-Fonte do Capítulo Está no FreeBSD Real

Para um leitor que deseja consultar as implementações na árvore de código-fonte referenciadas ao longo do capítulo, aqui está um índice resumido dos arquivos mais importantes:

| Conceito | Arquivo na árvore |
|---------|--------------|
| codificação de ioctl | `/usr/src/sys/sys/ioccom.h` |
| definição de cdevsw | `/usr/src/sys/sys/conf.h` |
| família make_dev | `/usr/src/sys/kern/kern_conf.c` |
| framework sysctl | `/usr/src/sys/sys/sysctl.h`, `/usr/src/sys/kern/kern_sysctl.c` |
| API ifnet | `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if.c` |
| exemplo ifnet | `/usr/src/sys/net/if_disc.c` |
| API CAM SIM | `/usr/src/sys/cam/cam_xpt.h`, `/usr/src/sys/cam/cam_sim.h` |
| exemplo CAM | `/usr/src/sys/dev/ahci/ahci.c` |
| EVENTHANDLER | `/usr/src/sys/sys/eventhandler.h` |
| mecanismo MODULE | `/usr/src/sys/sys/module.h`, `/usr/src/sys/kern/kern_module.c` |
| TUNABLE | `/usr/src/sys/sys/sysctl.h` (procure por `TUNABLE_INT_FETCH`) |
| probes SDT | `/usr/src/sys/sys/sdt.h`, `/usr/src/sys/cddl/dev/sdt/sdt.c` |
| referência ao `null(4)` | `/usr/src/sys/dev/null/null.c` |

Ler esses arquivos junto com o capítulo é o próximo passo para qualquer leitor que queira aprofundar seu conhecimento sobre integração. O driver `null(4)`, em especial, merece uma leitura completa; é pequeno o suficiente para ser absorvido em uma única sessão e demonstra quase todos os padrões abordados neste capítulo.

### O Que Não Entrou no Capítulo

Alguns tópicos de integração pertencem a um conjunto de ferramentas mais amplo do FreeBSD, mas não mereceram uma seção aqui, seja porque são específicos de subsistemas de uma forma que distrairia um iniciante, seja porque são abordados com mais profundidade em um capítulo posterior. Mencioná-los aqui mantém o capítulo honesto quanto ao seu escopo e oferece ao leitor um mapa prospectivo.

A primeira omissão é o `geom(4)`. Um driver que expõe um dispositivo de blocos se conecta ao GEOM em vez de ao CAM. O padrão de registro é semelhante ao padrão cdev (alocar um `g_geom`, preencher os callbacks de `g_class`, chamar `g_attach`), mas o vocabulário é diferente o suficiente para que misturá-lo ao capítulo tivesse obscurecido a distinção entre armazenamento e caracteres. Drivers para discos brutos e alvos pseudo-disco vivem nessa vizinhança; a referência canônica é `/usr/src/sys/geom/geom_disk.c`.

A segunda omissão é o `usb(4)`. Um driver USB se registra no stack USB por meio de `usb_attach` e de uma tabela de métodos específica para USB, em vez de diretamente pelo newbus. As superfícies de integração (devfs, sysctl) são as mesmas após a vinculação do dispositivo, mas a borda superior pertence ao stack USB. As referências canônicas estão em `/usr/src/sys/dev/usb/`.

A terceira omissão é `iicbus(4)` e `spibus(4)`. Drivers que se comunicam com periféricos I2C ou SPI se vinculam como filhos de um driver de barramento e usam rotinas de transferência específicas do barramento. As superfícies de integração permanecem as mesmas, mas a integração com device-tree e FDT que impulsiona SoCs Arm modernos acrescenta um vocabulário que justifica um capítulo próprio. A Parte VI aborda essas superfícies no contexto adequado.

A quarta omissão é a integração com `kqueue(2)` e `poll(2)`. Um driver de caracteres que deseja acordar programas em espaço do usuário bloqueados em `select`, `poll` ou `kqueue` precisa implementar `d_kqfilter` (e opcionalmente `d_poll`), conectar `selwakeup` e `KNOTE` ao caminho de dados e fornecer um pequeno conjunto de operações de filtro. O mecanismo não é difícil, mas é conceitualmente uma camada acima do contrato básico cdev; voltaremos a ele no Capítulo 26.

Um leitor que precisar de qualquer uma dessas superfícies hoje deve tratar o padrão do capítulo como a base e recorrer às referências presentes na árvore de código mencionadas acima. A disciplina (registrar no attach, drenar no detach, manter um único mutex em todos os callbacks que alteram o estado, versionar a superfície pública) é a mesma.

### Autoavaliação do Leitor

Antes de virar a página, o leitor que percorreu o capítulo deve ser capaz de responder às perguntas a seguir sem consultar o texto. Cada pergunta remete a uma seção que introduziu o material subjacente. Se uma pergunta for desconhecida, a seção do capítulo indicada entre parênteses é o lugar certo para revisitar antes de continuar.

1. O que `D_TRACKCLOSE` muda na forma como `d_close` é invocado? (Seção 2)
2. Por que `mda_si_drv1` é preferível a atribuir `si_drv1` após o retorno de `make_dev`? (Seção 2)
3. O que o macro `_IOR('M', 1, uint32_t)` codifica no código de requisição resultante? (Seção 3)
4. Por que o ramo padrão do dispatcher deve retornar `ENOIOCTL` em vez de `EINVAL`? (Seção 3)
5. Qual função do kernel desfaz o contexto sysctl por dispositivo, e quando ela é executada? (Seção 4)
6. Como `CTLFLAG_TUN` coopera com `TUNABLE_INT_FETCH` para aplicar um valor em tempo de boot? (Seção 4)
7. Qual é a diferença entre a string `MYFIRST_VERSION`, o inteiro de `MODULE_VERSION` e o inteiro de `MYFIRST_IOCTL_VERSION`? (Seção 8)
8. Por que a cadeia de limpeza no attach usa `goto`s rotulados em ordem inversa em vez de instruções `if` aninhadas? (Seção 7)
9. Como o padrão de soft-detach do capítulo difere do padrão `dev_ref`/`dev_rel` que o Desafio 3 esboça? (Seções 7 e 10)
10. Quais duas superfícies de integração são necessárias para quase todo driver, e quais duas são necessárias apenas para drivers que se integram a um subsistema específico? (Seções 1, 2, 3, 4, 5, 6)

O leitor que responder à maioria dessas perguntas sem hesitar internalizou o capítulo e está pronto para o que vem a seguir. O leitor que hesitar em mais de duas deve revisitar as seções relevantes antes de enfrentar a disciplina de manutenção do Capítulo 25.

### Palavra Final

A integração é o que transforma um módulo funcional em um driver utilizável. Os padrões deste capítulo não são um acabamento opcional; são a diferença entre um driver que um operador pode adotar e um que um operador precisa combater. Domine-os uma vez, e cada driver subsequente se torna mais fácil de construir, mais fácil de manter e mais fácil de entregar.

O próximo capítulo retoma a disciplina introduzida aqui e a generaliza em um conjunto de hábitos de manutenção: logging com taxa limitada, vocabulário consistente de errno, tunables e versionamento, limpeza em nível de produção e os mecanismos `SYSINIT`/`SYSUNINIT`/`EVENTHANDLER` que estendem o ciclo de vida de um driver além de um simples carregamento e descarregamento. O vocabulário muda, mas o ritmo é o mesmo: registrar, receber tráfego, desregistrar de forma limpa. Com a base do Capítulo 24 estabelecida, o Capítulo 25 parecerá uma extensão natural, não um mundo novo.

Um último pensamento antes de virar a página. As superfícies de integração deste capítulo são deliberadamente pequenas em número. Há devfs, ioctl e sysctl. Há os registros opcionais em subsistemas como ifnet e CAM. Há a disciplina de ciclo de vida que os une. Cinco conceitos, no total.

Quando esses conceitos se tornam familiares, o restante do livro é a aplicação dos mesmos padrões a hardware cada vez mais interessante. O leitor que terminou este capítulo concluiu a metade do livro voltada para a API do kernel; o que resta são os sistemas e a disciplina para dominá-los bem.
