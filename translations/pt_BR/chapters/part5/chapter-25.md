---
title: "Tópicos Avançados e Dicas Práticas"
description: "O Capítulo 25 encerra a Parte 5 ensinando os hábitos de engenharia que transformam um driver FreeBSD funcional e integrado em um software de kernel robusto e de fácil manutenção. Ele aborda o logging do kernel com limitação de taxa e as boas práticas de logging; um vocabulário disciplinado de valores de errno e convenções de retorno para callbacks de read, write, ioctl, sysctl e ciclo de vida; configuração do driver por meio de tunables em `/boot/loader.conf` e sysctls graváveis; estratégias de versionamento e compatibilidade para ioctls, sysctls e comportamentos visíveis ao usuário; gerenciamento de recursos em caminhos de falha usando o padrão de limpeza com goto rotulado; modularização do driver em arquivos de código-fonte logicamente separados; a disciplina de preparar um driver para uso em produção com `MODULE_DEPEND`, `MODULE_PNP_INFO` e empacotamento adequado; e os mecanismos SYSINIT / SYSUNINIT / EVENTHANDLER que estendem o ciclo de vida de um driver além dos simples `MOD_LOAD` e `MOD_UNLOAD`. O driver myfirst evolui da versão 1.7-integration para a 1.8-maintenance: ele ganha `myfirst_log.c` e `myfirst_log.h` com uma macro `DLOG_RL` apoiada por `ppsratecheck`, uma divisão entre `myfirst_cdev.c` e `myfirst_bus.c` para que os callbacks de cdev fiquem separados do mecanismo de attach do Newbus, um documento `MAINTENANCE.md`, um manipulador de eventos `shutdown_pre_sync`, um ioctl `MYFIRSTIOC_GETCAPS` que permite ao espaço do usuário negociar bits de funcionalidades, e um script de regressão com versão atualizada. O capítulo encerra a Parte 5 de forma completa: o driver ainda pode ser compreendido, ainda pode ser ajustado sem necessidade de recompilação, e agora está preparado para absorver o próximo ano de manutenção sem se tornar ilegível."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 25
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "pt-BR"
---
# Tópicos Avançados e Dicas Práticas

## Orientação ao Leitor e Objetivos

O Capítulo 24 encerrou com um driver com o qual o restante do sistema consegue se comunicar. O driver `myfirst` na versão `1.7-integration` possui um nó `/dev/myfirst0` criado de forma limpa via `make_dev_s`, um header de ioctl público compartilhado entre o kernel e o espaço do usuário, uma subárvore sysctl por instância em `dev.myfirst.0`, um tunable de inicialização para a máscara de depuração e um despachante de ioctl que obedece à regra de fallback `ENOIOCTL`, de modo que helpers do kernel como `FIONBIO` ainda chegam corretamente à camada cdev. O driver compila, carrega, executa sob estresse e sobrevive a ciclos repetidos de `kldload` e `kldunload` sem vazar OIDs ou nós cdev. Em todo sentido observável, o driver funciona.

O Capítulo 25 trata da diferença entre um driver que *funciona* e um driver que é *manutenível*. Essas duas qualidades não são a mesma coisa, e a diferença aparece aos poucos. Um driver que funciona passa pela primeira rodada de testes, conecta-se corretamente ao seu hardware e entra em uso. Um driver manutenível também faz isso e, além disso, absorve o próximo ano de correções de bugs, adições de funcionalidades, mudanças de portabilidade, novas revisões de hardware e a rotatividade de APIs do kernel sem desabar lentamente sob seu próprio peso. O primeiro dá ao desenvolvedor um bom dia. O segundo dá ao driver uma boa década.

O Capítulo 25 é o capítulo de encerramento da Parte 5. Onde o Capítulo 23 ensinou observabilidade e o Capítulo 24 ensinou integração, o Capítulo 25 ensina os hábitos de engenharia que preservam ambas as qualidades ao longo do tempo. A Parte 6 começa logo em seguida com drivers específicos de transporte (USB no Capítulo 26, armazenamento no Capítulo 27, rede no Capítulo 28 e além), e cada um desses capítulos pressupõe a disciplina introduzida aqui. Sem logging com taxa limitada, uma tempestade de hotplug USB enche o buffer de mensagens. Sem uma convenção de erros consistente, um driver de armazenamento e seu periférico em CAM divergem sobre o que `EBUSY` significa. Sem tunables do loader, um driver de rede com uma profundidade de fila padrão subótima não pode ser ajustado em uma máquina de produção sem uma recompilação. Sem disciplina de versionamento, uma ferramenta em espaço do usuário escrita para esta versão do driver interpreta silenciosamente de forma errada um novo campo adicionado dois meses depois. Cada hábito é pequeno. Juntos, eles são o que transforma um driver em uma peça de longa vida no FreeBSD, em vez de um experimento de laboratório de curta duração.

O exemplo recorrente do capítulo continua sendo o driver `myfirst`. No início do capítulo, ele está na versão `1.7-integration`. Ao final do capítulo, está na versão `1.8-maintenance`, dividido em mais arquivos do que antes, registrando logs sem inundar o buffer de mensagens, retornando erros de um vocabulário consistente, configurável a partir de `/boot/loader.conf`, distribuído com um documento `MAINTENANCE.md` que explica o contrato de manutenção contínua, anunciando seus eventos pelo canal `devctl` e conectado aos eventos de desligamento e de pouca memória do kernel por meio de `EVENTHANDLER(9)`. Nenhuma dessas adições exige novos conhecimentos de hardware. Todas elas exigem disciplina mais apurada.

A Parte 5 encerra aqui com os hábitos que mantêm o driver coerente à medida que ele cresce. O Capítulo 22 fez o driver sobreviver a uma mudança de estado de energia. O Capítulo 23 fez o driver contar o que está fazendo. O Capítulo 24 fez o driver se encaixar no restante do sistema. O Capítulo 25 faz o driver manter todas essas qualidades à medida que evolui. O Capítulo 26 abrirá então a Parte 6 colocando essas qualidades em prática diante de um transporte real, o Universal Serial Bus, onde cada atalho no logging ou no tratamento de caminhos de falha fica exposto pela velocidade e variedade do tráfego USB.

### Por Que a Disciplina de Manutenção Merece um Capítulo Próprio

Antes de prosseguirmos, vale a pena parar e refletir se logging com taxa limitada, vocabulário de errno e loader tunables realmente merecem um capítulo completo. Os capítulos anteriores já ensinaram tanto. Adicionar uma macro de log sustentada por `ppsratecheck(9)` parece pequeno. Padronizar códigos de erro parece ainda menor. Por que distribuir o trabalho em um capítulo longo quando cada hábito parece uma mão cheia de linhas?

A resposta é que cada hábito é pequeno, mas a ausência de cada hábito é grande. Um driver que registra logs sem limitação de taxa funciona bem no laboratório e é catastrófico em produção na primeira vez que um cabo instável dispara dez mil reenumerações por segundo. Um driver que retorna `EINVAL` quando deveria retornar `ENXIO`, e `ENXIO` quando deveria retornar `ENOIOCTL`, funciona bem quando o autor é o único chamador, mas é um relatório de bug esperando para acontecer quando um segundo desenvolvedor escreve o primeiro helper em espaço do usuário. Um driver que deixa cada padrão de configuração ser uma constante em tempo de compilação funciona bem para uma pessoa e é inviável para uma equipe que mantém o mesmo módulo em vários servidores de produção com cargas de trabalho diferentes. O Capítulo 25 dedica tempo a cada um desses hábitos porque o valor não é medido no laboratório, mas no custo de manutenção de dois anos que cada hábito reduz.

O primeiro motivo pelo qual o capítulo merece seu lugar é que **esses hábitos moldam como a base de código do driver se parece à medida que cresce**. Um leitor que acompanhou os Capítulos 23 e 24 já viu o driver dividido em múltiplos arquivos: `myfirst.c`, `myfirst_debug.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`. Essa foi uma modularização feita em pequena escala, uma superfície de cada vez. O Capítulo 25 revisita a modularização com a pergunta que a maioria dos leitores ainda não fez: *como é uma organização de código-fonte manutenível quando o driver tem uma dúzia de arquivos e três desenvolvedores?* O capítulo responde a essa pergunta com uma divisão intencional entre a camada de attach do Newbus, a camada cdev, a camada de ioctl, a camada sysctl e a camada de logging, e então usa essa divisão para sustentar todos os outros hábitos que o capítulo ensina.

O segundo motivo é que **esses hábitos determinam se o driver pode ser depurado em produção**. Um driver que registra logs criteriosamente e retorna erros informativos dá ao operador informações suficientes para registrar um relatório de bug útil. Um driver que registra logs em excesso ou de menos, ou que inventa suas próprias convenções de errno, força o operador a raciocinar apenas a partir de sintomas, e o desenvolvedor acaba perseguindo problemas intermitentes às cegas. O kit de ferramentas de depuração do Capítulo 23 é eficaz, mas depende da cooperação do driver. A cooperação é construída aqui.

O terceiro motivo é que **esses hábitos tornam o driver extensível sem quebrar seus chamadores**. O header `myfirst_ioctl.h` do Capítulo 24 já é um contrato entre o driver e o espaço do usuário. O Capítulo 25 ensina o leitor a evoluir esse contrato, adicionar um novo ioctl que programas mais antigos em espaço do usuário possam ignorar com segurança, aposentar um sysctl depreciado sem quebrar os scripts dos administradores e incrementar a versão do driver de um jeito que consumidores externos possam verificar em tempo de execução. Sem esses hábitos, o primeiro v2 do driver força a reescrita de todos os chamadores. Com eles, o driver pode adicionar funcionalidades por uma década e ainda executar os helpers em espaço do usuário que foram compilados na primeira semana em que o driver foi lançado.

O Capítulo 25 merece seu lugar ao ensinar essas três ideias juntas, de forma concreta, com o driver `myfirst` como exemplo recorrente. Quem termina o Capítulo 25 consegue deixar qualquer driver FreeBSD pronto para manutenção de longo prazo, consegue ler os padrões de hardening de produção de outro driver e reconhecer quais são fundamentados em princípios e quais são ad hoc, consegue negociar compatibilidade com ferramentas existentes em espaço do usuário e tem um driver `myfirst` na versão `1.8-maintenance` claramente pronto para iniciar a Parte 6.

### Onde o Capítulo 24 Deixou o Driver

Um breve resumo de onde você deve estar. O Capítulo 25 estende o driver produzido ao final do Estágio 3 do Capítulo 24, marcado como versão `1.7-integration`. Se algum dos itens abaixo for incerto, volte ao Capítulo 24 e corrija-o antes de começar este capítulo, pois o novo material pressupõe que todos os primitivos do Capítulo 24 estão funcionando.

- Seu driver compila sem erros e se identifica como `1.7-integration` na saída de `kldstat -v`.
- Um nó `/dev/myfirst0` existe após o `kldload`, tem proprietário `root:wheel` e modo `0660`, e desaparece corretamente no `kldunload`.
- O módulo exporta quatro ioctls: `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG` e `MYFIRSTIOC_RESET`. O pequeno programa `myfirstctl` em espaço do usuário do Capítulo 24 exercita cada um e retorna sucesso em todos os quatro.
- A subárvore sysctl `dev.myfirst.0` lista pelo menos `version`, `open_count`, `total_reads`, `total_writes`, `message`, `message_len`, `debug.mask` e `debug.classes`.
- `sysctl dev.myfirst.0.debug.mask=0xff` ativa todas as classes de depuração, e a saída de log subsequente do driver exibe as tags esperadas.
- O tunable de inicialização `hw.myfirst.debug_mask_default`, colocado em `/boot/loader.conf`, é aplicado antes do attach e define o valor inicial do sysctl.
- Repetições de `kldload` e `kldunload` em loop por um minuto não deixam OIDs residuais, nem cdev órfão, nem memória vazada conforme reportado por `vmstat -m | grep myfirst`.
- Sua árvore de trabalho contém `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, `POWER.md`, `DEBUG.md` e `INTEGRATION.md` dos capítulos anteriores.
- Seu kernel de teste tem `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` e `DDB_CTF` ativados. Os laboratórios do Capítulo 25 dependem de `WITNESS` e `INVARIANTS` com a mesma intensidade que os do Capítulo 24.

Esse driver é o que o Capítulo 25 estende. As adições são menores em linhas de código do que em qualquer capítulo anterior da Parte 5, mas maiores em superfície conceitual. As novas peças são: um par `myfirst_log.c` e `myfirst_log.h` construído em torno de `ppsratecheck(9)`, uma cadeia de limpeza com goto rotulado em `myfirst_attach`, um vocabulário de erros refinado em todo o despachante, um par de hooks `SYSINIT`/`SYSUNINIT` para inicialização global do driver, um handler de evento `shutdown_pre_sync`, um novo ioctl `MYFIRSTIOC_GETCAPS` que permite ao espaço do usuário consultar bits de funcionalidades, uma modesta refatoração que separa o attach do Newbus de `myfirst.c` para `myfirst_bus.c` e os callbacks cdev para `myfirst_cdev.c`, um documento `MAINTENANCE.md` que explica a política de incremento de versão, um script de regressão atualizado e um incremento de versão para `1.8-maintenance`.

### O Que Você Vai Aprender

Ao final deste capítulo, você será capaz de:

- Explicar por que o log irrestrito no kernel é um risco em produção, descrever como `ppsratecheck(9)` limita eventos por segundo, escrever uma macro de log com limitação de taxa que coopera com a máscara de depuração do Capítulo 23, e reconhecer as três classes de mensagens de log que merecem estratégias de throttling diferentes.
- Auditar os caminhos de `read`, `write`, `ioctl`, `open`, `close`, `attach`, `detach` e dos handlers de sysctl de um driver para verificar o uso correto de errno. Distinguir `EINVAL` de `ENXIO`, `ENOIOCTL` de `ENOTTY`, `EBUSY` de `EAGAIN`, `EPERM` de `EACCES` e `EIO` de `EFAULT`, e saber quando cada um é a resposta correta.
- Adicionar tunables do loader através de `TUNABLE_INT_FETCH`, `TUNABLE_LONG_FETCH`, `TUNABLE_BOOL_FETCH` e `TUNABLE_STR_FETCH`, e combiná-los com sysctls graváveis para que um único parâmetro possa ser definido no boot ou ajustado em tempo de execução. Entender como `CTLFLAG_TUN` coopera com os fetchers de tunables.
- Expor a configuração como uma superfície pequena e bem documentada, em vez de uma pilha de variáveis de ambiente ad hoc. Escolher entre tunables por driver e por instância com disciplina. Documentar a unidade, o intervalo e o valor padrão de cada tunable.
- Versionar a interface visível ao usuário de um driver com uma divisão estável em três partes: a string de versão legível em `dev.myfirst.0.version`, o inteiro `MODULE_VERSION` usado pela maquinaria de dependências de módulos do kernel, e o inteiro de formato wire `MYFIRST_IOCTL_VERSION` gravado no cabeçalho público.
- Adicionar um novo ioctl a um cabeçalho público existente sem quebrar chamadores mais antigos, descontinuar um ioctl obsoleto após o período de depreciação correto, e fornecer um bitmask de capacidades através de `MYFIRSTIOC_GETCAPS` para que programas em espaço do usuário possam detectar a disponibilidade de funcionalidades sem tentativa e erro.
- Estruturar os caminhos de attach e detach de um driver com o padrão `goto fail;` de forma que cada alocação tenha exatamente um ponto de limpeza, cada limpeza ocorra na ordem inversa da alocação e um attach parcial nunca deixe para trás um recurso que o caminho de detach não liberará.
- Dividir um driver em arquivos de código-fonte lógicos por área de responsabilidade, em vez de por tamanho de arquivo. Escolher entre um único arquivo grande, uma pequena coleção de arquivos por tema e uma árvore de subsistema completa, e saber quando cada abordagem é adequada.
- Preparar um driver para uso em produção com `MODULE_DEPEND`, `MODULE_PNP_INFO`, um handler de `modevent` bem-comportado que aceita `MOD_QUIESCE` quando o driver pode pausar de forma limpa, um sistema de build pequeno que distribui tanto o módulo quanto sua documentação, e um padrão compatível com `devd(8)` para anunciar eventos do driver através de `devctl_notify`.
- Usar `SYSINIT(9)` e `SYSUNINIT(9)` para conectar a inicialização e o encerramento de todo o driver em estágios específicos do subsistema do kernel, e entender a diferença entre handlers de eventos de módulo e hooks de inicialização em nível de subsistema.
- Registrar e cancelar o registro de callbacks em eventos conhecidos do kernel através de `EVENTHANDLER(9)`: `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, `power_suspend_early` e `power_resume`. Saber como escolher uma prioridade e como garantir o cancelamento do registro no detach.

A lista é longa porque a disciplina de manutenção toca muitas superfícies pequenas ao mesmo tempo. Cada item é focado e ensinável. O trabalho do capítulo é torná-los um hábito.

### O Que Este Capítulo Não Aborda

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 25 permaneça focado na disciplina de manutenção no nível adequado para um leitor que está concluindo a Parte 5.

- **Padrões de produção específicos de transporte**, como tempestades de hotplug USB, eventos de estado de link SATA e tratamento de mudança de mídia Ethernet, pertencem à Parte 6, onde cada transporte é ensinado por completo. O Capítulo 25 ensina os *hábitos gerais*; o Capítulo 26 e os seguintes os aplicam ao USB especificamente.
- **Design completo de framework de testes**, incluindo harnesses de regressão que rodam em várias configurações de kernel e cenários de injeção de falhas, pertence às seções de testes sem hardware dos Capítulos 26, 27 e 28. O Capítulo 25 acrescenta mais uma linha ao script de regressão existente; ele não introduz um harness completo.
- **`fail(9)` e `fail_point(9)`**, as facilidades de injeção de erros do kernel, são adiados para o Capítulo 28, junto com o trabalho no driver de armazenamento onde são mais frequentemente usados.
- **Integração contínua, assinatura de pacotes e distribuição** são preocupações operacionais do projeto que distribui o driver, não do código-fonte do driver em si. O capítulo diz apenas o suficiente sobre empacotamento para tornar o driver reproduzível.
- **Hooks de `MAC(9)` (Mandatory Access Control)** constituem um framework especializado e são mais bem introduzidos em um capítulo posterior voltado à segurança.
- **Estabilidade de `kbi(9)` e congelamento de ABI** são decisões de engenharia de release tomadas pelo projeto FreeBSD, não pelo autor do driver. O capítulo observa as implicações de ABI das funções exportadas pelo kernel, mas não aborda engenharia de release em profundidade.
- **`capsicum(4)`**: a integração com o modo capability para auxiliares em espaço do usuário é um tópico de segurança do espaço do usuário, não do driver em si. O `myfirstctl` do capítulo continua sendo uma ferramenta UNIX tradicional.
- **Padrões avançados de concorrência** como `epoch(9)`, locks de leitura predominante e filas sem lock. Esses são mencionados apenas de passagem; o único mutex do softc do driver continua sendo suficiente nesta etapa.

Manter-se dentro dessas fronteiras faz do Capítulo 25 um capítulo sobre *disciplina de manutenção*, não um capítulo sobre todas as técnicas que um desenvolvedor sênior de kernel poderia usar em um problema sênior de kernel.

### Estimativa de Investimento de Tempo

- **Somente leitura**: três a quatro horas. As ideias do Capítulo 25 são conceitualmente mais leves que as do Capítulo 24, e boa parte do vocabulário já é familiar. A função do capítulo é transformar primitivas conhecidas em disciplina.
- **Leitura mais digitação dos exemplos trabalhados**: oito a dez horas distribuídas em duas ou três sessões. O driver evolui por quatro etapas curtas (logging com limitação de taxa, auditoria de erros, disciplina de tunáveis e versões, SYSINIT e EVENTHANDLER), cada uma menor que uma única etapa do Capítulo 24. O refatoramento da Seção 6 toca vários arquivos, mas muda pouco código; a maior parte do trabalho consiste em mover código existente para seu novo lugar.
- **Leitura mais todos os laboratórios e desafios**: doze a quinze horas distribuídas em três ou quatro sessões. Os laboratórios incluem uma reprodução e correção de inundação de log, uma auditoria de errno com `truss`, um laboratório de tunável que inicializa uma VM duas vezes com valores diferentes de `/boot/loader.conf`, um laboratório de falha intencional no attach que exercita cada rótulo na cadeia `goto fail;`, um laboratório de `shutdown_pre_sync` que confirma que o callback realmente roda no momento certo, e um passo a passo do script de regressão que amarra tudo junto.

A Seção 5 (gerenciamento de caminhos de falha) é a mais densa em nova disciplina, e não em novo vocabulário. O padrão `goto fail;` em si é mecânico; o truque é ler uma função attach real do FreeBSD e enxergar cada alocação como uma candidata a um novo rótulo. Se o padrão parecer mecânico na primeira leitura, esse é o sinal de que se tornou um hábito.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 3 do Capítulo 24 (`1.7-integration`). Toda primitiva do Capítulo 24 é assumida: a criação de cdev baseada em `make_dev_s`, o dispatcher `myfirst_ioctl.c`, a construção da árvore `myfirst_sysctl.c`, o triplo `MYFIRST_VERSION`, `MODULE_VERSION` e `MYFIRST_IOCTL_VERSION`, e o padrão de `sysctl_ctx_free` por dispositivo.
- Sua máquina de laboratório roda FreeBSD 14.3 com `/usr/src` em disco e correspondendo ao kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` e `DDB_CTF` está construído, instalado e inicializando corretamente.
- Um snapshot do estado `1.7-integration` está salvo na sua VM. Os laboratórios do Capítulo 25 incluem cenários de falha intencional no attach, e um snapshot torna a recuperação barata.
- Os seguintes comandos de espaço do usuário estão no seu path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `devd`, `cc`, `make`, `dtrace`, `truss`, `ktrace`, `kdump` e `procstat`.
- Você está confortável editando `/boot/loader.conf` e reiniciando uma VM para carregar novos tunáveis.
- Você tem o programa companheiro `myfirstctl` do Capítulo 24 construído e funcionando.

Se algum item acima estiver vacilante, resolva agora. A disciplina de manutenção é mais fácil de aprender em um driver que já obedece às regras dos capítulos anteriores do que em um que ainda tem problemas não resolvidos de etapas anteriores.

### Como Aproveitar ao Máximo Este Capítulo

Cinco hábitos compensam mais neste capítulo do que em qualquer um dos capítulos anteriores da Parte 5.

Primeiro, mantenha quatro arquivos curtos de páginas de manual abertos em uma aba do navegador ou em um painel do terminal: `ppsratecheck(9)`, `style(9)`, `sysctl(9)` e `module(9)`. O primeiro é a documentação canônica da API de verificação de taxa. O segundo é o estilo de codificação do FreeBSD. O terceiro explica o framework sysctl. O quarto é o contrato do handler de eventos do módulo. Nenhum deles é longo; cada um vale a pena ser percorrido uma vez no início do capítulo e consultado quando o texto disser "consulte a página de manual para detalhes."

Segundo, mantenha três drivers reais à mão. `/usr/src/sys/dev/mmc/mmcsd.c` mostra `ppsratecheck` usado para limitar um `device_printf` em produção. `/usr/src/sys/dev/virtio/block/virtio_blk.c` mostra uma cadeia `goto fail;` limpa em seu caminho de attach e um conjunto de tunáveis de qualidade de produção. `/usr/src/sys/dev/e1000/em_txrx.c` mostra como um driver complexo divide logging, tunáveis e dispatch em vários arquivos. O Capítulo 25 aponta para cada um deles no momento certo; lê-los uma vez agora dá ao restante do capítulo âncoras concretas.

> **Uma nota sobre números de linha.** Quando o capítulo aponta para um lugar específico em `mmcsd.c`, `virtio_blk.c` ou `em_txrx.c`, o ponteiro é um símbolo nomeado, não um número de linha. `ppsratecheck`, os rótulos `goto fail;` em `virtio_blk_attach` e as chamadas `TUNABLE_*_FETCH` permanecem localizáveis por esses nomes em futuras revisões da árvore, mesmo que as linhas ao redor se movam. Os exemplos de auditoria que você verá mais adiante no capítulo usam notação `file:line` puramente como transcrição de ferramenta de amostra e carregam a mesma ressalva.

Terceiro, digite cada mudança no driver `myfirst` à mão. As adições do Capítulo 25 são o tipo de mudança que um desenvolvedor faz por reflexo após um ano de trabalho de manutenção. Digitá-las agora constrói o reflexo; colá-las pula a lição.

Quarto, após o material de tunáveis na Seção 3, reinicie sua VM pelo menos uma vez com uma nova configuração em `/boot/loader.conf` e observe o driver carregá-la durante o attach. Tunáveis são uma daquelas funcionalidades que parecem abstratas até você ver um valor real fluindo do bootloader pelo kernel até o seu softc. Dois reboots e um comando `sysctl` é tudo que é necessário.

Quinto, quando a seção sobre `goto fail;` pedir que você introduza uma falha deliberada em `myfirst_attach`, realmente faça isso. Injetar um único `return (ENOMEM);` no meio do attach e observar a cadeia de limpeza se desfazer corretamente é a melhor maneira de internalizar o padrão. O capítulo sugere um lugar específico para injetar, e o script de regressão confirma que a limpeza realmente rodou.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **Limitação de Taxa e Etiqueta de Logging.** Por que o logging descontrolado no kernel é um risco de produção; as três classes de mensagens de log de um driver (ciclo de vida, erro, debug); `ppsratecheck(9)` e `ratecheck(9)` como a resposta do FreeBSD à inundação de log; uma macro `DLOG_RL` com limitação de taxa que coopera com a máscara de debug do Capítulo 23; níveis de prioridade de `log(9)` e sua relação com `device_printf` e `printf`; o que o buffer de mensagens do kernel realmente custa e como não gastá-lo descuidadamente.
2. **Relatório de Erros e Convenções de Retorno.** Por que a disciplina de errno é um contrato com cada chamador; o pequeno vocabulário de errnos de kernel que um driver usa rotineiramente; quando cada um é apropriado; `ENOIOCTL` versus `ENOTTY` e por que o driver nunca deve retornar `EINVAL` no caso padrão do ioctl; códigos de retorno de handlers sysctl; códigos de retorno de handlers de eventos de módulo; uma lista de verificação que o leitor pode aplicar a todo driver que escrever daqui em diante.
3. **Configuração de Driver via Tunáveis do Loader e sysctl.** A diferença entre tunáveis de `/boot/loader.conf` e sysctls em tempo de execução; a família `TUNABLE_*_FETCH` e o flag `CTLFLAG_TUN`; tunáveis por driver versus por instância; como documentar um tunável para que os operadores possam confiar nele; um laboratório trabalhado que inicializa uma VM com um tunável em três posições diferentes e observa o efeito na árvore sysctl do driver.
4. **Estratégias de Versionamento e Compatibilidade.** A divisão de versão em três vias (inteiro `MODULE_VERSION`, string legível `MYFIRST_VERSION`, inteiro de formato de wire `MYFIRST_IOCTL_VERSION`); como cada um é usado; como adicionar um novo ioctl sem quebrar chamadores mais antigos; como retirar um ioctl obsoleto; `MYFIRSTIOC_GETCAPS` e a ideia de bitmask de capacidades; como um driver pode descontinuar graciosamente um OID sysctl na ausência de um flag dedicado do kernel; como `MODULE_DEPEND` impõe uma versão mínima de um módulo de dependência.
5. **Gerenciamento de Recursos em Caminhos de Falha.** O problema de limpeza em caso de falha no `myfirst_attach`; o padrão `goto fail;` e por que o desenrolamento linear supera cadeias de `if` aninhados; convenções de nomenclatura de rótulos (`fail_mtx`, `fail_cdev`, `fail_sysctl`); erros comuns (cair para o próximo caso após sucesso, rótulo faltando, adicionar um recurso sem adicionar sua limpeza); uma disciplina de funções auxiliares que reduz a duplicação; um laboratório de falha deliberada que testa toda a cadeia.
6. **Modularização e Separação de Responsabilidades.** Dividir um driver em arquivos ao longo dos eixos de responsabilidade; a divisão canônica para um driver de caracteres (`myfirst.c`, `myfirst_bus.c`, `myfirst_cdev.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`, `myfirst_debug.c`, `myfirst_log.c`); headers públicos versus privados; como organizar o `Makefile` para que todos esses arquivos construam um único `.ko`; quando a modularização ajuda e quando atrapalha; como uma equipe de desenvolvedores usa a divisão para reduzir conflitos de merge.
7. **Preparando para Uso em Produção.** `MODULE_DEPEND` e imposição de dependências; `MODULE_PNP_INFO` para carregamento automático; `MOD_QUIESCE` e o contrato de pausa antes do descarregamento; um padrão de sistema de build que instala o módulo e sua documentação; uma regra de `devd(8)` que reage aos eventos do driver; um pequeno documento `MAINTENANCE.md` que declara por escrito o contrato de manutenção do driver.
8. **SYSINIT, SYSUNINIT e EVENTHANDLER.** A maquinaria de ciclo de vida mais ampla do kernel além de `MOD_LOAD` e `MOD_UNLOAD`; `SYSINIT(9)` e `SYSUNINIT(9)` com IDs de subsistema e constantes de ordem; exemplos reais do FreeBSD de cada um; `EVENTHANDLER(9)` para notificações transversais (`shutdown_pre_sync`, `vm_lowmem`, `power_resume`); como registrar e desregistrar de forma limpa; como um driver usa os três mecanismos sem derivar para excesso de engenharia.

Após as oito seções vêm um conjunto de laboratórios práticos que exercitam cada disciplina, um conjunto de exercícios desafio que desafiam o leitor sem introduzir novas fundações, uma referência de solução de problemas para os sintomas que a maioria dos leitores encontrará, um Encerrando que fecha a história do Capítulo 25 e abre a do Capítulo 26, uma ponte para o próximo capítulo, um cartão de referência rápida e um glossário.

Se esta é a sua primeira leitura, leia de forma linear e faça os laboratórios em ordem. Se você está revisitando o capítulo, as Seções 1 e 5 são independentes e podem ser lidas em uma única sessão. A Seção 8 é uma breve recompensa conceitual no final do capítulo; ela depende levemente do material de uso em produção da Seção 7 e pode ser facilmente guardada para uma segunda sessão.

Uma pequena observação antes de o trabalho técnico começar. O Capítulo 25 é o último capítulo da Parte 5. Suas adições são menores do que as do Capítulo 24, mas tocam em quase todos os arquivos do driver. Espere gastar mais tempo relendo seu próprio código anterior do que escrevendo código novo. Isso também é disciplina de manutenção. Um driver que você relê com paciência é um driver que você pode modificar com confiança; um driver que você modifica com confiança é um driver que você consegue manter vivo.

## Seção 1: Limitação de Taxa e Etiqueta de Log

A primeira disciplina que este capítulo ensina é a disciplina de não falar demais. O driver `myfirst` do final do Capítulo 24 registra mensagens quando se conecta, quando se desconecta, quando um cliente abre ou fecha o dispositivo, quando uma leitura ou escrita atravessa a fronteira, quando um ioctl é despachado e quando a máscara de debug é ajustada. Cada uma dessas linhas de log foi introduzida por um bom motivo, e cada uma delas é útil quando um único evento acontece. O que nenhuma das linhas de log do Capítulo 24 leva em conta é o que acontece quando o mesmo evento dispara cem mil vezes por segundo.

Esta seção explica por que essa questão importa mais do que parece, apresenta as três categorias de mensagem de log de driver que se comportam de forma diferente sob pressão, ensina as primitivas de verificação de taxa do FreeBSD (`ratecheck(9)` e `ppsratecheck(9)`), e mostra como construir uma macro pequena e disciplinada sobre elas que coopera com o mecanismo de máscara de debug existente do Capítulo 23. Ao final da seção, o driver `myfirst` possui um par `myfirst_log.c` e `myfirst_log.h`, e seu buffer de mensagens não vira ruído sob estresse.

### O Problema com Log Irrestrito

Uma mensagem do kernel é barata para escrever e cara para carregar. `device_printf(dev, "something happened\n")` é uma única chamada de função, dezenas de nanossegundos em uma CPU moderna, e retorna quase imediatamente. O custo não está na chamada; o custo está em tudo que acontece com os bytes depois. A string formatada é copiada para o buffer de mensagens do kernel, uma área circular da memória do kernel cujo tamanho é fixado no boot. Ela é entregue ao dispositivo de console, se o console estiver conectado (frequentemente uma porta serial em uma VM, com uma taxa de bits finita). É enviada ao daemon syslog rodando no espaço do usuário pelo caminho `log(9)` se o driver usar esse caminho, e depois pelo `newsyslog(8)` para `/var/log/messages` em disco. Cada um desses passos tem um custo, e todos eles são síncronos no momento em que o driver escreve a linha.

Quando o driver escreve uma linha, nada disso importa. Quando o driver escreve um milhão de linhas por segundo, tudo isso importa. O buffer de mensagens do kernel se enche, e as mensagens mais antigas são sobrescritas antes que alguém as leia. O console, tipicamente rodando a 115200 baud, fica para trás e não consegue se recuperar, o que por sua vez empurra pressão de volta ao caminho do kernel que escreveu a linha, que é o caminho crítico do seu driver. O daemon syslog acorda, faz trabalho e volta a dormir muitas vezes por segundo, roubando ciclos de outros processos. O disco onde `/var/log/messages` reside se enche a uma taxa previsível, e um driver que registra dez mil linhas por segundo pode encher uma partição de tamanho razoável em uma tarde.

Nenhum desses sintomas é causado por um bug na lógica do driver. Eles são causados pelo *volume de log* do driver, que por sua vez é causado pelo driver disparando uma linha de log razoável em cada evento. Linhas de log razoáveis são perfeitamente adequadas enquanto os eventos são raros. Elas se tornam um risco quando os eventos são frequentes. Toda a arte da etiqueta de log está em saber, no momento em que você escreve uma linha de log, se o evento por trás dela é raro ou comum, e escrever o código de forma que a repetição descontrolada não possa transformar um log de evento raro em um log de evento comum.

Um exemplo concreto de um driver real ilustra o ponto. Considere um controlador de SSD PCIe que notifica seu driver sobre uma condição recuperável de fila cheia. Em um sistema saudável, essa condição é rara o suficiente para que registrar cada ocorrência seja útil. Em um sistema problemático, ela pode acontecer centenas de vezes por segundo até que alguém substitua o hardware. Se o driver escreve uma linha a cada vez, o buffer de mensagens se enche com linhas quase idênticas, todas as mensagens anteriores daquele boot são sobrescritas e perdidas, e o operador que tenta diagnosticar o problema lendo o `dmesg` vê apenas a última página do dilúvio. O comportamento real do hardware fica obscurecido pela reação do driver a ele. Uma linha de log com limitação de taxa teria mostrado as primeiras ocorrências, a taxa, e depois um lembrete periódico; o contexto anterior no `dmesg` teria sobrevivido; o operador teria tido algo com que trabalhar.

A lição se generaliza. A disciplina certa de log não é "logar menos" nem "logar mais", mas "logar a uma taxa que permaneça útil independentemente de quantas vezes o evento subjacente dispare." O restante desta seção ensina essa disciplina de forma concreta.

### Três Categorias de Mensagem de Log de Driver

Antes de escolher a política de throttling correta, é útil nomear as três categorias de mensagem de log que um driver tipicamente emite. Cada categoria tem uma estratégia de throttling diferente.

A primeira categoria são **eventos de ciclo de vida**. São as mensagens que marcam attach, detach, suspend, resume, carga e descarga de módulo. Elas ocorrem uma vez por transição de ciclo de vida, tipicamente algumas poucas vezes durante o tempo de vida de um módulo. Nenhum throttling é necessário; o volume é naturalmente baixo. Limitar a taxa de mensagens de ciclo de vida seria um erro porque ocultaria transições de estado importantes.

A segunda categoria são **mensagens de erro e aviso**. São mensagens que relatam algo que o driver considera errado. Por construção, cada uma dessas deveria ser rara; se um aviso está disparando cem vezes por segundo, o aviso está dizendo algo sobre a taxa dos eventos subjacentes, e essa informação vale a pena preservar mesmo quando o evento se repete. Mensagens de erro e aviso se beneficiam fortemente da limitação de taxa, mas o limite de taxa deve preservar pelo menos uma mensagem por burst e deve tornar a própria *taxa* visível.

A terceira categoria são **mensagens de debug e rastreamento**. São as mensagens sob as macros `DPRINTF` do Capítulo 23. Elas são intencionalmente verbosas quando a máscara de debug está ativa e silenciosas quando a máscara está desativada. Aplicar throttling nelas no ponto de emissão adiciona ruído ao que já é um caminho de baixo sinal; a disciplina mais adequada é evitar emiti-las quando a máscara está desativada, o que já é o que o `DPRINTF` existente faz. Mensagens de debug e rastreamento não precisam de limitação de taxa adicional, mas precisam que o usuário consiga desativá-las completamente com um único comando `sysctl`. A infraestrutura do Capítulo 23 já fornece isso.

Com as três categorias nomeadas, o restante da seção foca na segunda. Mensagens de ciclo de vida estão bem como estão. Mensagens de debug são tratadas pela máscara existente. Mensagens de erro e aviso são onde a disciplina real entra.

### Apresentando `ratecheck` e `ppsratecheck`

O kernel do FreeBSD fornece duas primitivas intimamente relacionadas para saída com limitação de taxa. Ambas residem em `/usr/src/sys/kern/kern_time.c` e são declaradas em `/usr/src/sys/sys/time.h`.

`ratecheck(struct timeval *lasttime, const struct timeval *mininterval)` é a mais simples das duas. O chamador mantém uma `struct timeval` lembrando quando o evento disparou pela última vez, junto com um intervalo mínimo entre impressões permitidas. A cada chamada, `ratecheck` compara o tempo atual com `*lasttime`, e se `mininterval` tiver passado, atualiza `*lasttime` e retorna 1. Caso contrário, retorna 0. O código chamador imprime apenas quando o retorno é 1. O resultado é um piso simples na taxa de impressões: no máximo uma impressão por `mininterval`.

`ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)` é a forma mais comumente usada em drivers. Seu nome é um legado do caso de uso de telemetria de pulso por segundo para o qual foi originalmente escrita. O kernel a expõe por meio de um `#define` em `/usr/src/sys/sys/time.h`:

```c
int    eventratecheck(struct timeval *, int *, int);
#define ppsratecheck(t, c, m) eventratecheck(t, c, m)
```

A chamada aceita um ponteiro para um timestamp, um ponteiro para um contador de eventos na janela de um segundo atual, e o máximo de eventos permitidos por segundo. A cada chamada, se o segundo ainda não virou, o contador é incrementado. Se o contador exceder `maxpps`, a função retorna 0 e o chamador suprime sua saída. Quando um novo segundo começa, o contador é reiniciado para 1 e a função retorna 1, permitindo uma impressão para o novo segundo. Um valor especial de `maxpps == -1` desabilita a limitação de taxa completamente (útil para caminhos de debug).

Ambas as primitivas são baratas: uma comparação e uma atualização aritmética, sem locks. Ambas são seguras para chamar de qualquer contexto onde o driver atualmente chama `device_printf`, incluindo handlers de interrupção, desde que o armazenamento que elas acessam seja estável naquele contexto. Na prática, os drivers mantêm a `struct timeval` e o contador dentro de seu softc, protegidos pelo mesmo lock que protege o ponto de log, ou usam estado por CPU onde isso for conveniente.

Um exemplo curto da árvore do FreeBSD mostra o padrão em uso real. O driver de cartão MMC SD, `/usr/src/sys/dev/mmc/mmcsd.c`, limita a taxa de uma reclamação sobre erros de escrita para que um cartão com defeito não inunde o log:

```c
if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
        device_printf(dev, "Error indicated: %d %s\n",
            err, mmcsd_errmsg(err));
```

O driver armazena `log_time` e `log_count` em seu softc, escolhe um `LOG_PPS` razoável (tipicamente de 5 a 10), e envolve a chamada `device_printf` na verificação de taxa. Os primeiros erros em qualquer segundo produzem linhas de log; os próximos várias centenas no mesmo segundo não produzem nada.

Essa é a ideia toda. Tudo que segue nesta seção é sobre fazer a mesma coisa com mais estrutura, mais disciplina e menos repetição.

### Uma Macro de Log com Limitação de Taxa Simples

O objetivo é uma macro que o driver possa usar no lugar do `device_printf` nu em qualquer caminho de erro ou aviso onde o evento possa se repetir. A macro deve:

1. Descartar silenciosamente a saída quando o limite de taxa for excedido.
2. Permitir uma taxa diferente por ponto de chamada, ou pelo menos por categoria.
3. Cooperar com o mecanismo de máscara de debug existente do Capítulo 23 para que a saída de debug permaneça controlada pela máscara e não pelo limitador de taxa.
4. Ser compilada fora de builds sem debug, se o driver assim escolher, sem custo em tempo de execução.

Uma implementação mínima tem a seguinte aparência. Em um novo `myfirst_log.h`:

```c
#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
        struct timeval rl_lasttime;
        int            rl_curpps;
};

/*
 * Default rate for warning messages: at most 10 per second per call
 * site.  Chosen to keep the log readable under a burst while still
 * showing the rate itself.
 */
#define MYF_RL_DEFAULT_PPS  10

/*
 * DLOG_RL - rate-limited device_printf.
 *
 * rlp must point at a per-call-site struct myfirst_ratelimit stored in
 * the driver (typically in the softc).  pps is the maximum allowed
 * prints per second.  The remaining arguments match device_printf.
 */
#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
        if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps))  \
                device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)

#endif /* _MYFIRST_LOG_H_ */
```

No softc, reserve uma ou mais estruturas de limitação de taxa:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_ratelimit sc_rl_ioerr;
        struct myfirst_ratelimit sc_rl_short;
};
```

Em cada ponto de erro, substitua o `device_printf` nu por `DLOG_RL`:

```c
/* Old:
 * device_printf(sc->sc_dev, "I/O error on read, ENXIO\n");
 */
DLOG_RL(sc, &sc->sc_rl_ioerr, MYF_RL_DEFAULT_PPS,
    "I/O error on read, ENXIO\n");
```

A macro usa um operador vírgula dentro de um bloco `do { ... } while (0)` para que se encaixe em qualquer lugar onde uma instrução se encaixa, incluindo dentro de corpos de `if` e `else` sem chaves. A chamada a `ppsratecheck` é barata; quando o limite de taxa é excedido, o `device_printf` simplesmente não é chamado. Quando o limite de taxa não é excedido, o comportamento é idêntico a um `device_printf` direto.

Um ponto pequeno, mas importante: cada ponto de chamada deve ter sua própria `struct myfirst_ratelimit`. Compartilhar uma única estrutura entre múltiplos pontos de chamada não relacionados significa que o primeiro caminho que disparar em cada segundo suprime todos os outros caminhos pelo restante daquele segundo. Em um driver com alguns erros raros, mas possíveis, reserve uma estrutura de limitação de taxa por categoria, nomeie-a de acordo com a categoria e use-a de forma consistente.

### Cooperando com a Máscara de Debug do Capítulo 23

A macro com limitação de taxa resolve o caso de erros e avisos. O caso de debug já tem seu próprio mecanismo do Capítulo 23:

```c
DPRINTF(sc, MYF_DBG_IO, "read: %zu bytes requested\n", uio->uio_resid);
```

A macro `DPRINTF` se expande para nada quando o bit correspondente em `sc_debug` está limpo, portanto a saída de debug com a máscara silenciosa (`mask = 0`) não tem custo em tempo de execução. Não há necessidade de limitar a taxa da saída de debug: o operador a ativa quando quer vê-la e a desativa quando não quer. Se o operador ativar `MYF_DBG_IO` em um dispositivo ocupado e ver um dilúvio de saída, esse é o comportamento pretendido; ele queria o dilúvio. A macro de limitação de taxa e a macro de debug servem a propósitos diferentes e não devem ser combinadas.

Onde as duas se encontram é na linha de log ocasional que é conceitualmente um aviso, mas que o desenvolvedor quer ter a possibilidade de silenciar completamente. Para esses casos, o padrão correto é condicionar a chamada de `DLOG_RL` a um bit de debug:

```c
if ((sc->sc_debug & MYF_DBG_IO) != 0)
        DLOG_RL(sc, &sc->sc_rl_short, MYF_RL_DEFAULT_PPS,
            "short read: %d bytes\n", n);
```

A limitação de taxa dispara sob a máscara de debug, e a saída é tanto opcional quanto limitada. Este é um padrão minoritário; a maioria dos avisos deve disparar incondicionalmente com um limite de taxa, e a maioria das impressões de debug deve ser controlada pela máscara sem limite de taxa.

### Níveis de Prioridade de `log(9)`

Uma terceira primitiva de logging merece ser mencionada aqui: `log(9)`. Ao contrário de `device_printf`, que sempre encaminha as mensagens para o buffer de mensagens do kernel, `log` encaminha pelo caminho do syslog com uma prioridade de syslog. A função está em `/usr/src/sys/kern/subr_prf.c` e recebe uma prioridade definida em `/usr/src/sys/sys/syslog.h`:

```c
void log(int level, const char *fmt, ...);
```

As prioridades mais comuns são: `LOG_EMERG` (0) para condições em que o sistema está inutilizável, `LOG_ALERT` (1) para situações que exigem ação imediata, `LOG_CRIT` (2) para condições críticas, `LOG_ERR` (3) para condições de erro, `LOG_WARNING` (4) para avisos, `LOG_NOTICE` (5) para condições notáveis mas normais, `LOG_INFO` (6) para mensagens informativas e `LOG_DEBUG` (7) para mensagens de nível de depuração. Um driver que usa `log(LOG_WARNING, ...)` em seu caminho de avisos, em vez de `device_printf`, ganha a capacidade de ser filtrado pelo `syslog.conf(5)` para um arquivo de log separado, sem que o autor do driver precise fazer mais nada.

A contrapartida é que `log(9)` não acrescenta o nome do dispositivo automaticamente. Um driver que usa `log` precisa formatar o nome do dispositivo na mensagem manualmente, o que é verboso. A maioria dos drivers FreeBSD prefere, portanto, `device_printf` para mensagens específicas do dispositivo e reserva `log` para notificações transversais. O driver `myfirst` segue a mesma convenção: `device_printf` para tudo que o operador deve ler com o `dmesg`, e `log` para nada neste estágio.

Uma diretriz prática: use `device_printf` quando a mensagem for *sobre este dispositivo*. Use `log(9)` quando a mensagem for *sobre uma condição transversal* que a infraestrutura do syslog é o lugar adequado para exibir, como um evento de autenticação ou uma violação de política. Código de driver raramente precisa do segundo tipo.

### O Buffer de Mensagens do Kernel e Seu Custo

Mais um detalhe técnico antes de encerrar a seção. O buffer de mensagens do kernel (`msgbuf`) é um buffer circular de tamanho fixo dentro do kernel, alocado no boot. Seu tamanho é controlado pelo tunable `kern.msgbufsize`, que por padrão é de 96 KiB em amd64 e pode ser aumentado em `/boot/loader.conf`. Cada chamada a `printf`, `device_printf` e `log` passa pelo buffer. Quando o buffer está cheio, as mensagens mais antigas são sobrescritas. O conteúdo do buffer é o que `dmesg` imprime.

Duas consequências práticas decorrem disso. Primeira: uma enxurrada de mensagens curtas pode expulsar as mensagens anteriores que um operador precisa. Uma linha que diz "hello" ocupa algumas dezenas de bytes; um buffer de 96 KiB comporta cerca de três mil dessas linhas; um loop que imprime a dez mil linhas por segundo apaga todo o log de boot em menos de meio segundo. Segunda: produzir uma mensagem formatada não é gratuito. A formatação no estilo `printf` consome CPU, e dentro de um handler de interrupção ou em um caminho crítico (hot path) esse custo aparece diretamente nos números de latência. O macro de taxa limitada ajuda com a primeira consequência. A segunda é a razão pela qual mensagens de debug são controladas por máscara: um `DPRINTF` com máscara zero compila para uma instrução vazia em tempo de execução, dispensando tanto a formatação quanto o armazenamento.

Aumentar `kern.msgbufsize` é uma resposta razoável em uma máquina que perde logs de boot repetidamente, mas não é substituto para a limitação de taxa. Um buffer maior apenas compra mais espaço antes que a enxurrada expulse as mensagens antigas; a limitação de taxa reduz a própria enxurrada. Ambas valem a pena. `kern.msgbufsize=262144` em `/boot/loader.conf` é uma escolha comum de operadores em máquinas de produção. Isso não é uma ação do Capítulo 25, pois o driver não pode alterar o tamanho do buffer em tempo de execução.

### Um Exemplo Prático: O Caminho de Leitura do `myfirst`

Juntando as peças, considere o callback `myfirst_read` existente. Uma versão simplificada do Capítulo 24 tinha esta aparência:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        int error = 0;

        mtx_lock(&sc->sc_mtx);
        if (uio->uio_resid == 0) {
                device_printf(sc->sc_dev, "read: empty request\n");
                goto out;
        }
        /* copy bytes into user space, update counters ... */
out:
        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

Esse código tem um problema latente de inundação de taxa. Sob pressão, um programa em espaço do usuário com defeito ou malicioso pode chamar `read(fd, buf, 0)` em um loop apertado e encher o buffer de mensagens com linhas "empty request". O evento não é um erro no driver; é um padrão de syscall estranho, mas válido. Registrá-lo é discutível, mas se o driver registrar, a linha de log deve ter taxa limitada.

Após a refatoração, o mesmo caminho fica assim:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        int error = 0;

        mtx_lock(&sc->sc_mtx);
        if (uio->uio_resid == 0) {
                DLOG_RL(sc, &sc->sc_rl_short, MYF_RL_DEFAULT_PPS,
                    "read: empty request\n");
                goto out;
        }
        /* copy bytes into user space, update counters ... */
out:
        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

A mudança é de três linhas. O efeito é que o log não pode mais ser inundado, e a primeira ocorrência em qualquer segundo ainda produz uma linha para o operador notar. O softc ganha um campo `struct myfirst_ratelimit sc_rl_short`; nenhum outro código é alterado.

Aplique a mesma transformação a cada `device_printf` em cada caminho de erro ou aviso, reserve uma `struct myfirst_ratelimit` por categoria, e o driver terá taxa limitada. O diff é mecânico; a disciplina é o que torna o diff possível.

### Erros Comuns e Como Evitá-los

Três erros são comuns ao aplicar limitação de taxa pela primeira vez. Cada um é fácil de identificar uma vez que se sabe o que procurar.

O primeiro erro é **compartilhar uma única estrutura de limitação de taxa entre call sites não relacionados**. Se o site A e o site B usam ambos `sc->sc_rl_generic`, uma explosão no site A silencia o site B pelo resto do segundo, e o operador vê apenas uma categoria. A disciplina correta é uma estrutura de limitação de taxa por categoria lógica. Duas ou três categorias por driver é o usual; dez é sinal de que o driver está registrando tipos de eventos em excesso.

O segundo erro é **aplicar limitação de taxa em mensagens de ciclo de vida**. O driver carrega e imprime um banner. Esse banner dispara uma vez. Envolvê-lo com `ppsratecheck` adiciona ruído sem ganho, e em um segundo carregamento durante uma fronteira de segundo infeliz pode até omitir o banner. Reserve a limitação de taxa para mensagens que realmente podem se repetir.

O terceiro erro é **esquecer que o contador de limitação de taxa vive no softc**. Um call site que dispara antes de attach ser concluído, ou depois que detach começa, pode acessar um softc cuja estrutura de limitação de taxa ainda não foi inicializada (ou foi zerada por `bzero(sc, sizeof(*sc))`). O `struct timeval` e o `int` são ambos tipos por valor; uma estrutura inicializada com zero funciona bem para a primeira chamada, pois `ppsratecheck` trata corretamente o caso `lasttime == 0`. Mas uma alocação na heap não inicializada que depois contém lixo não funciona, pois o campo `lasttime` pode conter um valor grande que faz o código pensar que o último evento ocorreu em um futuro distante, e cada chamada subsequente retorna 0 até que o relógio do kernel passe esse tempo futuro, o que pode nunca acontecer. A correção é garantir que o softc seja inicializado com zero, o que no `myfirst` já ocorre (o newbus aloca o softc com `MALLOC(... M_ZERO)`). Um driver que aloca seu próprio estado com `M_NOWAIT` sem `M_ZERO` deve chamar `bzero` explicitamente.

### Quando Não Usar Limitação de Taxa

A limitação de taxa é uma disciplina para caminhos que podem disparar com frequência. Alguns caminhos não podem. Uma falha de `KASSERT` causa panic no kernel, portanto limitar a taxa da mensagem pré-panic é esforço desperdiçado. Um erro que interrompe o carregamento de um módulo encerra o carregamento, de modo que apenas uma cópia da mensagem pode aparecer. Um `device_printf` no momento do attach dispara no máximo uma vez por instância. Para todos esses casos, o `device_printf` simples é correto e o wrapper extra é ruído desnecessário.

Uma regra prática útil: se o call site é executado depois que o `attach` foi concluído e antes que o `detach` seja executado, e se o evento pode ser causado por algo externo ao driver (um programa em espaço do usuário com mau comportamento, um dispositivo instável, um kernel sobrecarregado), então aplique limitação de taxa. Caso contrário, não aplique.

### O Que o Driver `myfirst` Contém Agora

Após esta seção, a árvore de trabalho do driver `myfirst` tem dois novos arquivos:

```text
myfirst_log.h   - the DLOG_RL macro and struct myfirst_ratelimit definition
myfirst_log.c   - any non-trivial rate-check helpers (empty for now)
```

O cabeçalho `myfirst.h` ainda contém o softc. O softc ganha dois ou três campos `struct myfirst_ratelimit`, nomeados de acordo com as categorias de call site que os utilizam. Os caminhos de `read`, `write` e `ioctl` substituem suas chamadas simples a `device_printf` nos sites de erro por `DLOG_RL`. Os caminhos de attach, detach, open e close mantêm suas chamadas simples a `device_printf`, pois essas são mensagens de ciclo de vida e não se repetem.

O `Makefile` ganha uma linha:

```makefile
SRCS= myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c \
      myfirst_log.c
```

O módulo compila, carrega e se comporta exatamente como antes no caso comum. Sob pressão, o driver não inunda mais o buffer de mensagens. Essa é a contribuição completa da Seção 1.

### Encerrando a Seção 1

Um driver que registra sem disciplina é um driver que falha graciosamente no laboratório e falha ruidosamente em produção. Os primitivos de verificação de taxa do FreeBSD `ratecheck(9)` e `ppsratecheck(9)` são pequenos o suficiente para serem compreendidos em uma hora e eficazes o suficiente para compensar seu custo pelo resto da vida do driver. Combinados com o mecanismo de máscara de debug existente do Capítulo 23, eles dão ao driver `myfirst` uma história de registro limpa em três dimensões: mensagens de ciclo de vida passam pelo `device_printf` simples, mensagens de erro e aviso passam pelo `DLOG_RL`, e mensagens de debug passam pelo `DPRINTF` sob a máscara.

Na próxima seção, passamos do que o driver diz para o que ele retorna. Uma linha de log é para o operador; um errno é para o chamador. Um driver que diz a coisa certa ao operador mas a coisa errada ao chamador ainda é um driver com defeito.

## Seção 2: Reporte de Erros e Convenções de Retorno

A segunda disciplina que o capítulo ensina é a disciplina de retornar o errno correto. Um errno é um número pequeno. O conjunto de errnos possíveis é definido em `/usr/src/sys/sys/errno.h`, e no momento da escrita o FreeBSD define menos de uma centena deles. Um driver descuidado quanto ao errno que retorna parece correto no momento, porque o chamador geralmente apenas verifica se o valor de retorno foi diferente de zero, e qualquer valor diferente de zero passa nesse teste. Parece muito menos correto alguns meses depois, quando o primeiro helper em espaço do usuário tenta distinguir *por que* uma chamada falhou, e as escolhas de errno do driver acabam sendo inconsistentes. Esta seção ensina o pequeno vocabulário de errnos que um driver usa rotineiramente, mostra como escolher entre eles e percorre uma auditoria dos caminhos existentes do driver `myfirst`.

### Por Que a Disciplina de Errno Importa

O retorno de errno de um driver é um contrato com cada chamador. Programas em espaço do usuário usam errnos por meio de `strerror(3)` e por comparação direta (`if (errno == EBUSY)`). O código do kernel que invoca callbacks do driver usa o valor de retorno para decidir o que fazer a seguir: um `d_open` que retorna `EBUSY` faz o kernel falhar a syscall `open(2)` com `EBUSY`; um `d_ioctl` que retorna `ENOIOCTL` faz o kernel passar para a camada ioctl genérica; um `device_attach` que retorna um valor diferente de zero faz o Newbus desfazer o attach e desassociar o dispositivo. Cada um desses consumidores espera que um valor específico signifique uma coisa específica. Um driver que retorna `EINVAL` onde `ENXIO` era esperado não necessariamente falha; muitas vezes apenas engana, e o errno enganoso aparece como um diagnóstico misterioso em algum lugar que o autor do driver nunca verá.

A disciplina tem custo baixo. Os custos de ignorá-la se acumulam com o tempo. Um driver que escolheu seus errnos bem desde o início produz páginas de manual precisas, mensagens de erro precisas nos helpers em espaço do usuário e relatórios de bugs precisos. Um driver descuidado com errnos começa a produzir saída de `strerror` ligeiramente errada em muitos lugares, e o lado do espaço do usuário do ecossistema herda essa falta de cuidado.

### O Pequeno Vocabulário

A lista completa de errnos é longa. O subconjunto que um driver de caracteres típico usa é pequeno. A tabela abaixo é o vocabulário mais frequentemente necessário em um driver FreeBSD, agrupado por quando cada um é apropriado.

| Errno | Valor numérico | Quando retornar |
|-------|---------------|-----------------|
| `0` | 0 | Sucesso. O único retorno sem erro. |
| `EPERM` | 1 | O chamador não tem privilégio para a operação solicitada, mesmo que a chamada em si seja bem formada. Exemplo: um usuário não-root solicitando um ioctl privilegiado. |
| `ENOENT` | 2 | O objeto solicitado não existe. Exemplo: uma busca por nome ou ID que não encontra nada. |
| `EIO` | 5 | Erro de I/O genérico do hardware. Use quando o hardware retornou uma falha e não há errno mais específico. |
| `ENXIO` | 6 | O dispositivo foi removido, desassociado ou está inacessível por outro motivo. Exemplo: um ioctl em um descritor de arquivo cujo dispositivo subjacente foi removido. Diferente de `ENOENT`: o objeto existia e agora não existe mais. |
| `EBADF` | 9 | O descritor de arquivo não está aberto corretamente para a operação. Exemplo: uma chamada `MYFIRSTIOC_SETMSG` feita em um descritor de arquivo aberto somente para leitura. |
| `ENOMEM` | 12 | Alocação falhou. Use para falhas de `malloc(M_NOWAIT)` e similares. |
| `EACCES` | 13 | O chamador não tem permissão no nível do sistema de arquivos. Diferente de `EPERM`: `EACCES` é sobre permissões de arquivo, `EPERM` é sobre privilégio. |
| `EFAULT` | 14 | Um ponteiro de espaço do usuário é inválido. Retornado por falhas em `copyin` ou `copyout`. Drivers devem encaminhar falhas de `copyin`/`copyout` sem modificação. |
| `EBUSY` | 16 | O recurso está em uso. Use para `detach` que não pode prosseguir porque um cliente ainda mantém o dispositivo aberto, ou para tentativas de aquisição semelhantes a mutex que não podem aguardar. |
| `EINVAL` | 22 | Os argumentos são reconhecidos, mas inválidos. Use quando o driver entendeu a requisição, mas as entradas são malformadas. |
| `EAGAIN` | 35 | Tente novamente mais tarde. Retornado de I/O não bloqueante quando a operação bloquearia, ou de falhas de alocação que podem ter êxito em uma nova tentativa. |
| `EOPNOTSUPP` | 45 | A operação não é suportada por este driver. Use quando a chamada é bem formada, mas o driver não tem código para tratá-la. |
| `ETIMEDOUT` | 60 | Uma espera expirou. Use para comandos de hardware que não foram concluídos dentro do orçamento de timeout do driver. |
| `ENOIOCTL` | -3 | O comando ioctl é desconhecido para este driver. **Use isso para o caso padrão em `d_ioctl`; o kernel o traduz para `ENOTTY` para o espaço do usuário.** |
| `ENOSPC` | 28 | Sem espaço disponível, seja no dispositivo, em um buffer ou em uma tabela interna. |

Três pares nessa tabela são notoriamente fáceis de confundir: `EPERM` versus `EACCES`, `ENOENT` versus `ENXIO`, e `EINVAL` versus `EOPNOTSUPP`. Cada um merece ser analisado por sua vez.

`EPERM` versus `EACCES`. `EPERM` diz respeito a privilégio: o chamador não possui privilégios suficientes para realizar a operação. `EACCES` diz respeito a permissão: a ACL do sistema de arquivos ou os bits de modo proíbem o acesso. Um usuário não-root que tenta escrever em `/dev/myfirst0` quando o modo do nó é `0600 root:wheel` recebe `EACCES` do kernel antes mesmo de o driver ser consultado. Um usuário root que tenta chamar um ioctl privilegiado e é rejeitado pelo driver porque o chamador não pertence a um jail específico recebe `EPERM` do driver. A distinção importa porque o remédio do administrador é diferente: `EACCES` pede ao administrador que ajuste as permissões do dispositivo, enquanto `EPERM` pede que ele ajuste os privilégios do chamador.

`ENOENT` versus `ENXIO`. `ENOENT` significa *não existe tal objeto*. `ENXIO` significa *o objeto desapareceu, ou o dispositivo está inacessível*. Em uma busca na tabela interna de um driver, `ENOENT` é a resposta certa quando a chave solicitada não está presente. Em uma operação contra um dispositivo que foi desanexado ou que sinalizou uma condição de remoção surpresa, `ENXIO` é a resposta certa. A distinção importa porque as ferramentas operacionais tratam esses códigos de forma diferente: `ENOENT` sugere que o chamador forneceu a chave errada; `ENXIO` sugere que o dispositivo precisa ser reanexado.

`EINVAL` versus `EOPNOTSUPP`. `EINVAL` significa *entendi o que você pediu, mas os argumentos estão errados*. `EOPNOTSUPP` significa *não suporto o que você pediu*. Uma chamada a `MYFIRSTIOC_SETMSG` com um buffer longo demais resulta em `EINVAL`. Uma chamada a `MYFIRSTIOC_SETMODE` para um modo que o driver nunca implementa resulta em `EOPNOTSUPP`. A distinção importa porque `EOPNOTSUPP` diz ao chamador que use uma abordagem diferente, enquanto `EINVAL` diz ao chamador que corrija os argumentos e tente novamente.

Uma quarta confusão merece um parágrafo próprio: `ENOIOCTL` versus `ENOTTY`. `ENOIOCTL` é um valor negativo (`-3`) definido para o caminho de código do ioctl dentro do kernel. O caso default do `d_ioctl` de um driver retorna `ENOIOCTL` para dizer ao kernel: "Não reconheço este comando; repasse o controle para a camada genérica." A camada genérica trata de `FIONBIO`, `FIOASYNC`, `FIOGETOWN`, `FIOSETOWN` e ioctls semelhantes comuns a vários tipos de dispositivo. Se a camada genérica também não reconhecer o comando, ela traduz `ENOIOCTL` para `ENOTTY` (valor positivo 25) para entrega ao espaço do usuário. O erro comum é retornar `EINVAL` no caso default de um switch em `d_ioctl`, o que suprime completamente o fallback genérico. O driver do Capítulo 24 já retorna `ENOIOCTL` corretamente; a auditoria do Capítulo 25 confirma isso e verifica todos os demais errnos do driver em busca de problemas semelhantes.

### A Auditoria do Dispatcher de Ioctl

A primeira passagem de auditoria tem como alvo `myfirst_ioctl.c`. Cada caso no switch produz no máximo um retorno diferente de zero. A auditoria analisa cada um e verifica se o errno retornado está correto.

Caso `MYFIRSTIOC_GETVER`: retorna 0 em caso de sucesso, nunca falha. Nada a auditar.

Caso `MYFIRSTIOC_GETMSG`: retorna 0 em caso de sucesso. O código atual não rejeita com base no `fflag` porque a mensagem é pública. Isso é uma escolha de projeto, não um bug. Se o driver quisesse restringir `GETMSG` a leitores (ou seja, exigir `FREAD`), ele retornaria `EBADF` na verificação do fflag, de forma consistente com os casos `SETMSG` e `RESET`.

Caso `MYFIRSTIOC_SETMSG`: retorna `EBADF` quando o file descriptor não possui `FWRITE`, o que está correto. A segunda questão de auditoria é o que acontece quando a entrada não é terminada em NUL: o `strlcpy` no kernel tolera isso (copia até `MYFIRST_MSG_MAX - 1` bytes e encerra a string), portanto o driver não precisa verificar. A terceira questão é se o comprimento deve ser validado antes da cópia. O `copyin` automático do kernel já impôs o comprimento fixo definido na codificação do ioctl, portanto não há buffer do espaço do usuário a validar; o valor já está em `data` e já foi copiado.

Caso `MYFIRSTIOC_RESET`: retorna `EBADF` quando o file descriptor não possui `FWRITE`. A auditoria do Capítulo 25 levanta uma segunda questão: o reset deve ser privilegiado? Um driver que permite que qualquer escritor chame `RESET` e zere estatísticas está expondo uma pequena superfície de ataque de negação de serviço. A correção simples é verificar `priv_check(td, PRIV_DRIVER)` antes de executar o reset:

```c
case MYFIRSTIOC_RESET:
        if ((fflag & FWRITE) == 0) {
                error = EBADF;
                break;
        }
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
                break;
        /* ... existing reset body ... */
        break;
```

Se `priv_check` falhar, o errno é `EPERM` (o kernel retorna `EPERM` em vez de `EACCES` porque a verificação é sobre privilégio, não sobre permissões do sistema de arquivos). O programa `myfirstctl` rodando como root recebe 0; um programa sem privilégios de root rodando como o usuário `_myfirst` recebe `EPERM`.

Caso padrão: retorna `ENOIOCTL`, o que está correto. Não é preciso alterar.

### A Auditoria dos Caminhos de Leitura e Escrita

A segunda passagem de auditoria tem como alvo os callbacks de leitura e escrita.

Para `myfirst_read`, o código atual retorna 0 em caso de sucesso, `EFAULT` quando `uiomove` falha, e 0 quando `uio_resid == 0`. O retorno 0 para uma requisição vazia é o comportamento padrão do UNIX (um `read` de zero bytes é permitido e retorna 0 bytes) e está correto. Nenhuma alteração de errno é necessária.

Para `myfirst_write`, de forma semelhante: 0 em caso de sucesso, `EFAULT` em caso de falha no `uiomove`, 0 em caso de escrita de zero bytes. Correto.

Nenhum dos callbacks precisa de `EIO`: o driver não realiza I/O de hardware neste ponto, portanto não há falha de hardware a propagar. Uma versão futura do driver que controla hardware real retornaria `EIO` no callback de leitura ou escrita quando o hardware indicasse uma falha de transporte. Adicionar esse retorno agora seria prematuro; é o tipo de situação que o trabalho com armazenamento do Capítulo 28 tratará de forma concreta.

### A Auditoria dos Caminhos de Open e Close

O callback de open atualmente retorna 0 incondicionalmente. A questão de auditoria é se ele deveria falhar em algum momento. Três modos de falha são convencionalmente possíveis: o dispositivo está com abertura exclusiva e já possui um usuário (`EBUSY`), o dispositivo está desligado e não é aceitável abri-lo no momento (`ENXIO`), ou o driver está sendo detachado neste momento (`ENXIO`). O driver simples `myfirst` não impõe abertura exclusiva e sempre aceita aberturas, exceto durante o detach. Durante o detach, o kernel destrói o cdev antes que o detach retorne, então qualquer abertura que chegue após o início de `destroy_dev` é rejeitada pelo próprio kernel antes que o `d_open` do driver seja chamado. O driver `myfirst` portanto não precisa de lógica explícita de `ENXIO`. Manter o callback de open retornando 0 está correto.

O callback de close retorna 0 incondicionalmente. Isso está correto. A única razão concebível para `d_close` retornar diferente de zero é uma operação de hardware durante o fechamento que falhou; como o driver `myfirst` não realiza tal operação, 0 é o retorno correto.

### A Auditoria dos Caminhos de Attach e Detach

Attach e detach são os callbacks que o Newbus invoca. Seus valores de retorno informam ao Newbus se deve reverter ou prosseguir.

O retorno diferente de zero de `myfirst_attach` significa "attach falhou; por favor, reverta." Todo caminho de erro em attach deve retornar um errno positivo. O código atual retorna o valor `error` de `make_dev_s`, que é positivo em caso de falha; isso está correto. As adições da Seção 5 deste capítulo introduzirão mais caminhos de erro com gotos rotulados; cada um deles usará o errno correto para a etapa que falhou (`ENOMEM` para falha de alocação, `ENXIO` para falha de alocação de recurso, etc.).

O retorno diferente de zero de `myfirst_detach` significa "não é possível fazer o detach agora; por favor, mantenha o dispositivo anexado." O código atual retorna `EBUSY` quando `sc_open_count > 0`, o que está correto. O Newbus traduz `EBUSY` do detach em uma falha de `devctl detach` com o mesmo errno, que é o comportamento correto visível ao usuário.

O handler de eventos do módulo (`myfirst_modevent`) retorna diferente de zero para rejeitar o evento. `MOD_UNLOAD` que não pode prosseguir porque alguma instância de dispositivo ainda está em uso retorna `EBUSY`. `MOD_LOAD` que não pode prosseguir devido a uma falha de verificação de sanidade retorna um errno apropriado (`ENOMEM`, `EINVAL`, etc.). O código atual está correto.

### A Auditoria do Handler de Sysctl

Os handlers de sysctl têm suas próprias convenções de errno. O driver do Capítulo 24 possui um handler personalizado, `myfirst_sysctl_message_len`. Seu corpo é:

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

O handler lê sua entrada com `sysctl_handle_int`, que retorna 0 em caso de sucesso e um errno positivo em caso de falha. O handler repassa esse errno sem alteração, o que está correto. Nenhuma mudança de auditoria é necessária.

Um handler de sysctl que escreve (em vez de apenas ler) deve verificar `req->newptr` para distinguir uma leitura de uma escrita, e deve retornar `EPERM` se a escrita for tentada em um OID somente leitura. O OID `debug.mask` existente é declarado com `CTLFLAG_RW`, portanto o kernel permite escritas automaticamente; o handler não precisa de verificação de privilégio porque o OID já é restrito ao root pelas permissões do MIB do sysctl. O driver do Capítulo 25 não adiciona mais handlers de sysctl personalizados nesta etapa.

### Mensagens nos Caminhos de Erro

Retornar o errno correto é metade do contrato. Emitir a mensagem de log correta é a outra metade. A disciplina combina o logging com limite de taxa da Seção 1 com o vocabulário de errno da Seção 2. Um caminho de aviso tem esta aparência:

```c
if (input_too_large) {
        DLOG_RL(sc, &sc->sc_rl_inval, MYF_RL_DEFAULT_PPS,
            "ioctl: SETMSG buffer too large (%zu > %d)\n",
            length, MYFIRST_MSG_MAX);
        error = EINVAL;
        break;
}
```

Três propriedades fazem deste um bom caminho de erro. Primeiro, a mensagem de log nomeia a chamada ("ioctl: SETMSG"), o motivo ("buffer too large") e os valores numéricos envolvidos. Segundo, o errno retornado é `EINVAL`, que é o valor correto para "entendi, mas o argumento está errado." Terceiro, todo o caminho tem limite de taxa para que um programa do espaço do usuário com bugs que chame o ioctl em um loop fechado não consiga inundar o buffer de mensagens.

Um caminho de erro ruim tem esta aparência:

```c
if (input_too_large) {
        device_printf(sc->sc_dev, "ioctl failed\n");
        return (-1);
}
```

Três propriedades fazem deste um caminho de erro ruim. A mensagem de log não é informativa: "ioctl failed" não diz nada que o chamador já não soubesse. O valor de retorno é `-1`, que não é um errno válido no kernel. E a linha de log não tem limite de taxa, então um chamador com mau comportamento pode encher o buffer de mensagens com ruído.

O caminho bom ocupa nove linhas e o caminho ruim ocupa três, o que é uma boa troca. Uma linha de log de erro só é impressa quando algo está errado; dedicar alguns segundos extras para torná-la informativa quando ela aparece vale a pena.

### Convenções do Handler de Eventos do Módulo

O handler de eventos do módulo tem sua própria convenção de errno. A assinatura do handler é:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
        switch (what) {
        case MOD_LOAD:
                /* Driver-wide init. */
                return (0);
        case MOD_UNLOAD:
                /* Driver-wide teardown. */
                return (0);
        case MOD_QUIESCE:
                /* Pause and prepare for unload. */
                return (0);
        case MOD_SHUTDOWN:
                /* System shutting down. */
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}
```

Cada caso retorna 0 em caso de sucesso, ou um errno positivo para rejeitar o evento. Os errnos específicos por caso:

- `MOD_LOAD` retorna `ENOMEM` se uma alocação global falhou, `ENXIO` se o driver não é compatível com o kernel atual, ou `EINVAL` se o valor de um tunable está fora do intervalo permitido.
- `MOD_UNLOAD` retorna `EBUSY` se o driver não pode ser descarregado agora porque alguma instância ainda está em uso. O kernel respeita isso e mantém o módulo carregado.
- `MOD_QUIESCE` retorna `EBUSY` se o driver não pode pausar. Um driver que não suporta quiescência simplesmente retorna 0 neste caso, porque quiescência é uma funcionalidade opcional e retornar sucesso significa "estou pausado" no sentido trivial de não ter trabalho em andamento.
- `MOD_SHUTDOWN` raramente falha; retorna 0 a menos que o driver tenha um motivo específico para se opor ao desligamento. Um driver que quer persistir estado usa um `EVENTHANDLER` em `shutdown_pre_sync` em vez de rejeitar `MOD_SHUTDOWN`.
- O caso padrão retorna `EOPNOTSUPP` para indicar que o driver não reconhece o tipo de evento. Isso não é um erro; é a forma padrão de dizer "não implemento este evento."

### Uma Lista de Verificação de Errno

Para encerrar a seção, uma lista de verificação que você pode aplicar a qualquer driver que escrever. Cada item é uma pergunta cuja resposta deve ser sim.

1. Todo retorno diferente de zero em um callback é um errno positivo de `errno.h`, exceto para `d_ioctl`, que pode retornar `ENOIOCTL` (um valor negativo).
2. Falhas de `copyin` e `copyout` propagam seu errno sem alteração (tipicamente `EFAULT`).
3. O caso padrão de `d_ioctl` retorna `ENOIOCTL`, não `EINVAL`.
4. `d_detach` retorna `EBUSY` se o dispositivo ainda está em uso, não `ENXIO` ou algum outro valor.
5. `d_open` retorna `ENXIO` se o hardware subjacente desapareceu ou se o driver está sendo detachado, não `EIO`.
6. `d_write` retorna `EBADF` se o file descriptor não possui `FWRITE`, não `EPERM`.
7. Todo caminho de erro registra uma mensagem que nomeia a chamada, o motivo e os valores relevantes, usando a macro com limite de taxa.
8. Nenhum caminho de erro registra *e* retorna um errno genérico. Se o driver tem contexto suficiente para registrar o motivo específico, também tem contexto suficiente para retornar um errno específico.
9. O driver distingue `EINVAL` (argumentos incorretos) de `EOPNOTSUPP` (funcionalidade ausente) de forma consistente.
10. O driver distingue `ENOENT` (chave inexistente) de `ENXIO` (dispositivo inacessível) de forma consistente.

Um driver que passa nessa lista de verificação tem uma superfície de errno consistente, e a superfície é pequena o suficiente para que a página de manual possa listar cada errno que o driver retorna e dizer exatamente quando cada um ocorre.

### Encerrando a Seção 2

Os errnos são um vocabulário pequeno e um contrato. O descuido com qualquer um deles se manifesta como comportamento enigmático no espaço do usuário; a disciplina com ambos se manifesta como diagnósticos precisos e relatórios de bugs mais curtos. Combinado com o logging com limite de taxa da Seção 1, o driver `myfirst` agora se comunica cuidadosamente tanto com o operador (por meio de linhas de log) quanto com o chamador (por meio de errnos).

Na próxima seção, veremos o terceiro público ao qual o driver deve um contrato: o administrador que configura o driver por meio de `/boot/loader.conf` e `sysctl`. A configuração é um terceiro tipo de contrato, e a disciplina com ela é o que mantém o driver útil em diferentes cargas de trabalho sem necessidade de recompilação.

## Seção 3: Configuração do Driver via Loader Tunables e sysctl

A terceira disciplina é a disciplina de externalizar decisões. Todo driver tem valores que alguém pode razoavelmente querer alterar sem recompilar o módulo: um timeout, uma contagem de tentativas, um tamanho de buffer interno, um nível de verbosidade, um seletor de funcionalidade. Um driver que fixa esses valores no código-fonte força cada alteração a passar por um ciclo completo de compilação, instalação e reinicialização. Um driver que os expõe como loader tunables e sysctls permite que um operador ajuste o comportamento na inicialização ou em tempo de execução com uma única edição ou um único comando. O custo de oferecer esses controles é pequeno; o custo de não oferecê-los é pago pelo operador.

Esta seção apresenta os dois mecanismos do FreeBSD para externalizar a configuração: loader tunables (lidos de `/boot/loader.conf` e aplicados antes de o kernel chegar ao `attach`) e sysctls (lidos e gravados em tempo de execução por meio do `sysctl(8)`). A seção explica como esses mecanismos cooperam por meio da flag `CTLFLAG_TUN`, mostra como escolher entre tunables por driver e por instância, percorre a família `TUNABLE_*_FETCH` e encerra com um laboratório prático em que o driver `myfirst` ganha três novos tunables e o leitor faz o boot da VM com cada um deles.

### A Diferença Entre um Tunable e um Sysctl

Um tunable e um sysctl parecem semelhantes para um operador. Ambos são strings em um namespace como `hw.myfirst.debug_mask_default` ou `dev.myfirst.0.debug.mask`. Ambos recebem valores definidos pelo operador. Ambos terminam na memória do kernel. A diferença está em quando e como isso ocorre.

Um **tunable** é uma variável definida no ambiente do bootloader. O bootloader (`loader(8)`) lê `/boot/loader.conf`, coleta seus pares `key=value` em um ambiente e repassa esse ambiente ao kernel no momento em que ele inicializa. O kernel expõe esse ambiente por meio da família `getenv(9)` e das macros `TUNABLE_*_FETCH`. Os tunables são lidos durante o boot, geralmente antes de o driver correspondente executar o attach. Eles não podem ser alterados em tempo de execução (modificar `/boot/loader.conf` requer um reboot para que a alteração entre em vigor). Eles são adequados para valores que precisam ser conhecidos antes de `attach` ser executado: o tamanho de uma tabela alocada estaticamente, um flag de funcionalidade que controla quais caminhos de código são compilados no caminho de attach, o valor inicial de uma máscara de debug.

Um **sysctl** é uma variável na árvore hierárquica de configuração do kernel, acessível em tempo de execução por meio da syscall `sysctl(2)` e da ferramenta `sysctl(8)`. Sysctls podem ser somente leitura (`CTLFLAG_RD`), leitura-escrita (`CTLFLAG_RW`), ou somente leitura com permissão de escrita para root (diversas combinações de flags). Eles são adequados para valores que faz sentido alterar após o driver ter executado o attach: um nível de verbosidade, uma taxa de limitação, um comando de reset de contador, um controle de status com suporte a escrita.

O recurso interessante é que os dois mecanismos podem compartilhar uma variável. Um sysctl declarado com `CTLFLAG_TUN` instrui o kernel a ler um tunable de mesmo nome durante o boot e usar seu valor como valor inicial do sysctl. O operador pode então ajustar o sysctl em tempo de execução, e o tunable persiste entre reboots como valor padrão. O driver `myfirst` já utiliza esse padrão para sua máscara de debug: `debug.mask` é um sysctl com `CTLFLAG_RW | CTLFLAG_TUN`, e `hw.myfirst.debug_mask_default` é o tunable correspondente em `/boot/loader.conf`. A Seção 3 generaliza esse padrão para todos os controles de configuração que o driver deseja expor.

### A Família `TUNABLE_*_FETCH`

O FreeBSD fornece uma família de macros para ler tunables do ambiente do bootloader. Cada macro lê o tunable pelo nome, converte o valor para o tipo C correto e armazena o resultado. Se o tunable não estiver definido, a variável mantém seu valor existente; o chamador deve, portanto, inicializar a variável com o valor padrão correto antes de invocar a macro de fetch.

As macros, declaradas em `/usr/src/sys/sys/kernel.h`:

```c
TUNABLE_INT_FETCH(path, pval)        /* int */
TUNABLE_LONG_FETCH(path, pval)       /* long */
TUNABLE_ULONG_FETCH(path, pval)      /* unsigned long */
TUNABLE_INT64_FETCH(path, pval)      /* int64_t */
TUNABLE_UINT64_FETCH(path, pval)     /* uint64_t */
TUNABLE_BOOL_FETCH(path, pval)       /* bool */
TUNABLE_STR_FETCH(path, pval, size)  /* char buffer of given size */
```

Cada uma expande para a chamada `getenv_*` correspondente. Para `TUNABLE_INT_FETCH`, por exemplo, a expansão é `getenv_int(path, pval)`, que lê o ambiente do bootloader e interpreta o valor como um inteiro.

O caminho é uma string, convencionalmente no formato `hw.<driver>.<knob>` para tunables por driver e `hw.<driver>.<unit>.<knob>` para tunables por instância. O prefixo `hw.` é uma convenção para tunables relacionados a hardware; outros prefixos (`kern.`, `net.`) existem para diferentes subsistemas, mas são menos comuns em código de driver.

Um exemplo prático do driver `myfirst` ilustra o padrão:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int error;

        /* Initialise defaults. */
        sc->sc_debug = 0;
        sc->sc_timeout_sec = 30;
        sc->sc_max_retries = 3;

        /* Read tunables.  The variables keep their default values if
         * the tunables are not set. */
        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);
        TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
        TUNABLE_INT_FETCH("hw.myfirst.max_retries", &sc->sc_max_retries);

        /* ... rest of attach ... */
}
```

O operador define os tunables em `/boot/loader.conf`:

```ini
hw.myfirst.debug_mask_default="0xff"
hw.myfirst.timeout_sec="15"
hw.myfirst.max_retries="5"
```

Após o reboot, cada instância de `myfirst` executa o attach com `sc_debug=0xff`, `sc_timeout_sec=15`, `sc_max_retries=5`. Nenhuma recompilação foi necessária; os valores residem fora do código-fonte do driver.

### Tunables por Driver Versus por Instância

Um driver que pode executar o attach mais de uma vez precisa de uma decisão: seus tunables devem se aplicar a todas as instâncias, ou cada instância deve ter os seus próprios?

A forma por driver utiliza um caminho no formato `hw.myfirst.debug_mask_default`. Cada instância de `myfirst` lê essa única variável durante o attach, de modo que todas as instâncias iniciam com o mesmo valor padrão. Essa é a forma mais simples e é correta quando o tunable tem o mesmo significado em todas as instâncias.

A forma por instância utiliza um caminho no formato `hw.myfirst.0.debug_mask_default`, onde `0` é o número da unidade. Cada instância lê sua própria variável, de modo que a instância 0 e a instância 1 podem ter valores padrão diferentes. Essa é a forma adequada quando o hardware por trás de cada instância pode razoavelmente precisar de configurações diferentes, como dois adaptadores PCI no mesmo sistema com cargas de trabalho distintas.

A decisão é uma escolha de projeto, não uma questão de correção. A maioria dos drivers usa a forma por driver para a maioria dos tunables, com formas por instância reservadas para os poucos casos em que a configuração por instância realmente importa. Para `myfirst`, um pseudo-dispositivo fictício, a forma por driver é o padrão correto para todos os tunables. O driver do Capítulo 25 portanto adiciona três tunables por driver (`timeout_sec`, `max_retries`, `log_ratelimit_pps`) e mantém o `debug_mask_default` por driver existente.

Um padrão que combina ambas as formas, caso o driver precise, é ler o tunable por driver primeiro como linha de base e depois ler o tunable por instância como sobreposição:

```c
int defval = 30;

TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &defval);
sc->sc_timeout_sec = defval;
TUNABLE_INT_FETCH_UNIT("hw.myfirst", unit, "timeout_sec",
    &sc->sc_timeout_sec);
```

O FreeBSD não possui uma macro `TUNABLE_INT_FETCH_UNIT` por padrão; um driver que precise disso deve compor o caminho com `snprintf` e então chamar `getenv_int` manualmente. O esforço é pequeno, mas a necessidade é rara, então `myfirst` não segue esse caminho.

### O Flag `CTLFLAG_TUN`

A segunda parte da história de externalização é que um tunable, por si só, é lido apenas durante o boot. Para tornar o mesmo valor ajustável em tempo de execução, o driver declara o sysctl correspondente com `CTLFLAG_TUN`:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "debug.mask",
    CTLFLAG_RW | CTLFLAG_TUN,
    &sc->sc_debug, 0,
    "Bitmask of enabled debug classes");
```

`CTLFLAG_TUN` instrui o kernel a usar o valor inicial desse sysctl a partir da variável de ambiente do bootloader de mesmo nome, utilizando o nome do OID como chave. A correspondência é textual e automática; o driver não precisa chamar `TUNABLE_INT_FETCH` separadamente.

Há uma regra sutil sobre quando `CTLFLAG_TUN` é respeitado. O flag se aplica ao valor *inicial* do OID, que é lido do ambiente quando o sysctl é criado. Se o driver chama `TUNABLE_INT_FETCH` explicitamente antes de criar o sysctl, o fetch explícito tem precedência e `CTLFLAG_TUN` se torna efetivamente redundante. Se o driver não chama `TUNABLE_INT_FETCH` e depende apenas de `CTLFLAG_TUN`, o valor inicial do sysctl é obtido do ambiente automaticamente.

Na prática, o driver `myfirst` utiliza ambos os mecanismos por clareza. O `TUNABLE_INT_FETCH` explícito no attach torna a intenção do driver visível no código-fonte; o `CTLFLAG_TUN` no sysctl fornece ao operador uma indicação clara na documentação do sysctl de que o OID respeita um tunable do loader. Qualquer um dos mecanismos isoladamente funcionaria; usar os dois é uma pequena duplicação que se justifica em legibilidade.

### Declarando um Tunable como um Sysctl Estático

Para sysctls de escopo global do driver que não pertencem a uma instância específica, o FreeBSD oferece macros em tempo de compilação que vinculam um sysctl a uma variável estática e leem seu valor padrão do ambiente em uma única declaração. A forma canônica:

```c
SYSCTL_NODE(_hw, OID_AUTO, myfirst, CTLFLAG_RW, NULL,
    "myfirst pseudo-driver");

static int myfirst_verbose = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, verbose,
    CTLFLAG_RWTUN, &myfirst_verbose, 0,
    "Enable verbose driver logging");
```

O `SYSCTL_NODE` declara um novo nó pai `hw.myfirst`. O `SYSCTL_INT` declara um OID inteiro `hw.myfirst.verbose` com `CTLFLAG_RWTUN` (que combina `CTLFLAG_RW` e `CTLFLAG_TUN`). A variável `myfirst_verbose` é o nível de verbosidade global do driver. O operador define `hw.myfirst.verbose=1` em `/boot/loader.conf` para habilitar a saída detalhada durante o boot, ou executa `sysctl hw.myfirst.verbose=1` para alterá-lo em tempo de execução.

A declaração estática é adequada para o estado de escopo global do driver. O estado por instância (`sc_debug`, contadores) continua residindo em `dev.myfirst.<unit>.*` e é declarado dinamicamente por meio de `device_get_sysctl_ctx`.

### Uma Nota Sobre `SYSCTL_INT` Versus `SYSCTL_ADD_INT`

A forma estática `SYSCTL_INT(parent, OID_AUTO, ...)` é uma declaração em tempo de compilação. A forma dinâmica `SYSCTL_ADD_INT(ctx, list, OID_AUTO, ...)` é uma chamada em tempo de execução. Ambas produzem um OID de sysctl. A forma estática é adequada para sysctls de escopo global do driver cuja existência não depende de executar o attach em hardware. A forma dinâmica é adequada para sysctls por instância criados no attach e destruídos no detach.

Um erro comum de iniciante é usar a forma dinâmica para sysctls de escopo global do driver, o que funciona, mas exige um `sysctl_ctx_list` de escopo global que precisa ser inicializado em `MOD_LOAD` e liberado em `MOD_UNLOAD`. A forma estática evita tudo isso: o sysctl existe desde o momento em que o módulo é carregado até o momento em que é descarregado, e o kernel cuida do registro e do cancelamento do registro automaticamente.

### Documentando um Tunable

Um tunable que o operador não conhece é um tunable que não é utilizado. A disciplina é documentar todos os tunables que o driver expõe, em três lugares.

Primeiro, a declaração do tunable no código-fonte deve incluir uma string de descrição de uma linha. Para `SYSCTL_ADD_UINT` e funções semelhantes, o último argumento é a descrição:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "timeout_sec",
    CTLFLAG_RW | CTLFLAG_TUN,
    &sc->sc_timeout_sec, 0,
    "Timeout in seconds for hardware commands (default 30, min 1, max 3600)");
```

A string de descrição é o que `sysctl -d` exibe quando um operador solicita documentação. Uma boa descrição indica a unidade, o valor padrão e o intervalo aceitável.

Segundo, o `MAINTENANCE.md` do driver (apresentado na Seção 7) deve listar cada tunable com um parágrafo dedicado. O parágrafo explica o que o tunable faz, quando alterá-lo, qual é o valor padrão e quais efeitos colaterais sua definição causa.

Terceiro, a página de manual do driver (tipicamente `myfirst(4)`) deve listar cada tunable em uma seção `LOADER TUNABLES` e cada sysctl em uma seção `SYSCTL VARIABLES`. O driver `myfirst` ainda não possui uma página de manual; o capítulo trata a página de manual como uma preocupação futura. O documento `MAINTENANCE.md` carrega a documentação completa nesse interim.

### Um Exemplo Prático: `hw.myfirst.timeout_sec`

O driver `myfirst` não possui hardware real neste estágio, mas o capítulo apresenta um controle fictício `timeout_sec` que os capítulos futuros utilizarão. O fluxo de trabalho completo é:

1. Em `myfirst.h`, adicione o campo ao softc:
   ```c
   struct myfirst_softc {
           /* ... existing fields ... */
           int   sc_timeout_sec;
   };
   ```

2. Em `myfirst_bus.c` (o novo arquivo apresentado na Seção 6, que contém o attach e o detach), inicialize o valor padrão e leia o tunable:
   ```c
   sc->sc_timeout_sec = 30;
   TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
   ```

3. Em `myfirst_sysctl.c`, exponha o controle como um sysctl em tempo de execução:
   ```c
   SYSCTL_ADD_INT(ctx, child, OID_AUTO, "timeout_sec",
       CTLFLAG_RW | CTLFLAG_TUN,
       &sc->sc_timeout_sec, 0,
       "Timeout in seconds for hardware commands");
   ```

4. Em `MAINTENANCE.md`, documente o tunable:
   ```
   hw.myfirst.timeout_sec
       Timeout in seconds for hardware commands.  Default 30.
       Acceptable range 1 through 3600.  Values below 1 are
       clamped to 1; values above 3600 are clamped to 3600.
       Adjustable at run time via sysctl dev.myfirst.<unit>.
       timeout_sec.
   ```

5. No script de regressão, adicione uma linha que verifique se o tunable assume seu valor padrão:
   ```
   [ "$(sysctl -n dev.myfirst.0.timeout_sec)" = "30" ] || fail
   ```

O driver agora possui um controle de timeout que o operador pode definir durante o boot por meio de `/boot/loader.conf`, ajustar em tempo de execução por meio de `sysctl`, e encontrar documentado em `MAINTENANCE.md`. Todo capítulo futuro que apresentar um novo valor configurável seguirá o mesmo fluxo de trabalho de cinco etapas.

### Verificações de Intervalo e Validação

Um tunable que o operador pode definir para qualquer valor é um tunable que pode ser definido para um valor fora do intervalo, seja por acidente (um erro de digitação em `/boot/loader.conf`) ou por uma tentativa equivocada de ajuste. O driver deve validar o valor que lê e limitá-lo ou rejeitá-lo.

Para tunables lidos durante o boot com `TUNABLE_INT_FETCH`, a validação ocorre de forma inline:

```c
sc->sc_timeout_sec = 30;
TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
if (sc->sc_timeout_sec < 1 || sc->sc_timeout_sec > 3600) {
        device_printf(dev,
            "tunable hw.myfirst.timeout_sec out of range (%d), "
            "clamping to default 30\n",
            sc->sc_timeout_sec);
        sc->sc_timeout_sec = 30;
}
```

Para sysctls com suporte a escrita em tempo de execução, a validação ocorre no handler. Um sysctl simples com `CTLFLAG_RW` em uma variável int aceita qualquer int; para rejeitar escritas fora do intervalo, o driver declara um handler personalizado:

```c
static int
myfirst_sysctl_timeout(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int v;
        int error;

        v = sc->sc_timeout_sec;
        error = sysctl_handle_int(oidp, &v, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        if (v < 1 || v > 3600)
                return (EINVAL);
        sc->sc_timeout_sec = v;
        return (0);
}
```

O handler lê o valor atual, chama `sysctl_handle_int` para realizar o I/O efetivo e aplica o novo valor apenas se estiver dentro do intervalo. Uma escrita de 0 ou 7200 retorna `EINVAL` ao operador sem alterar o valor do sysctl. Esse é o comportamento correto: o operador recebe um retorno claro de que a escrita foi rejeitada.

O driver `myfirst` neste estágio não valida seus sysctls inteiros porque nenhum deles pode significativamente sair do intervalo (a máscara de debug é um bitmask, e qualquer valor de 32 bits é uma máscara válida). Drivers futuros que apresentarem timeouts, contagens de tentativas e tamanhos de buffer utilizarão o padrão de handler personalizado de forma consistente.

### Quando Expor um Tunable e Quando Mantê-lo Interno

Expor um tunable é um compromisso. Assim que o operador define `hw.myfirst.timeout_sec=15` em `/boot/loader.conf`, o driver assume a promessa de que o significado daquele parâmetro não mudará em uma versão futura. Remover o tunable quebra implantações em produção. Alterar sua interpretação silenciosamente é ainda pior.

A disciplina correta é expor um valor como tunable apenas quando as três condições a seguir forem verdadeiras:

1. O valor tem um caso de uso operacional. Alguém pode razoavelmente precisar alterá-lo em uma implantação real.
2. O intervalo de valores razoáveis é conhecido. O driver pode documentá-lo em `MAINTENANCE.md`.
3. O custo de manter o parâmetro pelo tempo de vida do driver vale o benefício operacional que ele oferece.

Um driver que faz essas três perguntas e responde sim a todas elas expõe um conjunto pequeno e intencional de tunables. Um driver que expõe cada constante interna como tunable porque "os operadores podem querer ajustá-la" acaba com uma superfície de configuração enorme que ninguém consegue documentar e ninguém consegue testar em toda a sua amplitude.

Para o `myfirst`, o conjunto inicial de tunables é deliberadamente pequeno: `debug_mask_default`, `timeout_sec`, `max_retries`, `log_ratelimit_pps`. Cada um tem um caso operacional claro, um valor padrão claro e um intervalo claro. O driver não tenta expor cada campo int do softc como tunable; ele expõe apenas aqueles que um operador pode realmente querer ajustar.

### Uma Nota de Cautela sobre `CTLFLAG_RWTUN` para Strings

A macro `TUNABLE_STR_FETCH` lê uma string do ambiente do bootloader para um buffer de tamanho fixo. O flag correspondente, `CTLFLAG_RWTUN` em um `SYSCTL_STRING`, funciona, mas tem uma armadilha: o armazenamento da string deve ser um buffer estático, não um campo `char[]` por instância no softc. Um sysctl de string que grava em um campo do softc pode sobreviver ao softc se o framework de sysctl não cancelar o registro do OID antes de o softc ser liberado, o que leva a bugs de use-after-free.

O padrão mais seguro é expor strings como somente leitura e tratar gravações por meio de um handler personalizado que copia o novo valor para o softc sob um lock. O driver `myfirst` segue esse padrão: `dev.myfirst.0.message` é exposto apenas com `CTLFLAG_RD`, e as gravações passam pelo ioctl `MYFIRSTIOC_SETMSG`. O caminho pelo ioctl adquire o mutex do softc, copia o novo valor e libera o lock; não há problema de tempo de vida com OIDs de sysctl.

Tunables de string e sysctls de string são úteis o suficiente em alguns drivers para justificar o cuidado necessário, mas o driver do Capítulo 25 não precisa deles. O princípio merece ser mencionado porque a armadilha aparece mais adiante em drivers reais.

### Tunables versus Módulos do Kernel: Onde Eles Vivem

Dois detalhes pequenos, mas importantes, sobre o ambiente do loader merecem ser destacados.

Primeiro, um tunable em `/boot/loader.conf` se aplica a partir do momento em que o kernel inicia. Ele está disponível para qualquer módulo que chame `TUNABLE_*_FETCH` ou tenha um sysctl `CTLFLAG_TUN`, mesmo que o módulo não seja carregado no boot. Um módulo carregado posteriormente com `kldload` ainda enxerga o valor do tunable. Isso é conveniente: o operador define o tunable uma vez e não precisa mais se preocupar com ele até que o módulo seja carregado.

Segundo, um tunable é lido do ambiente, mas não pode ser gravado de volta. Alterar `hw.myfirst.timeout_sec` em tempo de execução (com `kenv`) não afeta nenhum driver que já o tenha lido; a variável no softc é o que importa, não o ambiente. Para alterar um valor em tempo de execução, o operador usa o sysctl correspondente.

Esses dois detalhes juntos explicam por que `CTLFLAG_TUN` é o formato adequado para a maioria dos parâmetros de configuração: o tunable define o padrão no boot, o sysctl cuida do ajuste em tempo de execução, e as ferramentas do operador (`/boot/loader.conf` mais `sysctl(8)`) funcionam como esperado.

### Encerrando a Seção 3

Configuração é uma conversa com o operador. Um driver que externaliza os valores certos por meio de tunables e sysctls pode ser ajustado sem recompilação; um driver que esconde todos os valores dentro do código-fonte força uma recompilação a cada mudança. A família `TUNABLE_*_FETCH` e o flag `CTLFLAG_TUN` juntos cobrem o ajuste no boot e em tempo de execução, e a escolha entre por driver e por instância adapta o driver à sua realidade operacional. O driver `myfirst` agora tem três novos tunables além do `debug_mask_default` já existente, cada um com um intervalo documentado e cada um com um sysctl correspondente.

Na próxima seção, passamos do que o driver expõe para como o driver evolui. Um parâmetro de configuração que funciona hoje precisa continuar funcionando amanhã quando o driver mudar. A disciplina de versionamento é o que mantém essa promessa.

## Seção 4: Estratégias de Versionamento e Compatibilidade

A quarta disciplina é a disciplina de evoluir sem quebrar contratos. Toda superfície pública que o driver `myfirst` oferece, o nó `/dev/myfirst0`, a interface de ioctl, a árvore de sysctl, o conjunto de tunables, é um contrato com alguém. Uma mudança que altera silenciosamente o significado de qualquer um desses é uma mudança incompatível, e mudanças incompatíveis que passam despercebidas pelo desenvolvedor são a origem de uma quantidade desproporcional de bugs em drivers reais. Esta seção ensina como versionar deliberadamente a superfície pública de um driver, de modo que as mudanças sejam visíveis para quem as consome e que consumidores mais antigos continuem funcionando quando o driver adiciona novas funcionalidades.

O capítulo usa três números de versão distintos para o driver `myfirst`. Cada um tem um propósito específico. Confundi-los é uma fonte de confusão que vale a pena evitar antes que ela se instale.

### Os Três Números de Versão

O driver `myfirst` tem três identificadores de versão, introduzidos ao longo dos Capítulos 23, 24 e 25. Cada um vive em um lugar diferente e muda por um motivo diferente.

O primeiro é a **string de versão legível por humanos**. Para o `myfirst`, trata-se de `MYFIRST_VERSION`, definida em `myfirst_sysctl.c` e exposta pelo sysctl `dev.myfirst.0.version`. Seu valor atual é `"1.8-maintenance"`. A string de versão é para pessoas: um operador que executa `sysctl dev.myfirst.0.version` vê um rótulo curto que identifica um ponto específico da história do driver. A string de versão não é interpretada por programas; ela é lida por pessoas. Ela muda sempre que o driver atinge um novo marco que o autor quer registrar, o que neste livro ocorre ao final de cada capítulo.

O segundo é o **inteiro de versão do módulo do kernel**. Trata-se de `MODULE_VERSION(myfirst, N)`, onde `N` é um inteiro usado pela maquinaria de dependências do kernel. Outro módulo que declare `MODULE_DEPEND(other, myfirst, 1, 18, 18)` exige que o `myfirst` esteja presente na versão 18 ou superior (e menor ou igual a 18, o que nessa declaração significa exatamente 18). O inteiro de versão do módulo muda apenas quando um consumidor interno do módulo precisaria recompilar, por exemplo quando a assinatura de um símbolo exportado muda. Para um driver que não exporta símbolos públicos para o kernel (como o `myfirst`), o número de versão do módulo é majoritariamente simbólico; o capítulo o incrementa a cada marco para manter o modelo mental do leitor alinhado entre os três identificadores de versão.

O terceiro é o **inteiro de versão da interface de ioctl**. Para o `myfirst`, trata-se de `MYFIRST_IOCTL_VERSION` em `myfirst_ioctl.h`. Seu valor atual é 1. A versão da interface de ioctl muda quando o cabeçalho de ioctl muda de um modo que um programa em espaço do usuário compilado contra a versão anterior interpretaria incorretamente. Um comando de ioctl renumerado, um layout de payload alterado, uma semântica modificada de um ioctl existente: cada um desses é uma mudança incompatível com a interface de ioctl e deve incrementar a versão. Adicionar um novo comando de ioctl, estender um payload com um campo no final sem reinterpretar os campos existentes, adicionar uma funcionalidade que não afeta os comandos antigos: essas são mudanças compatíveis e não requerem incremento.

Uma regra prática simples mantém os três separados. A string de versão é o que o operador lê. O inteiro de versão do módulo é o que outros módulos verificam. O inteiro de versão de ioctl é o que programas em espaço do usuário verificam. Cada um avança em seu próprio ritmo.

### Por que os Usuários Precisam Consultar a Versão

Um programa em espaço do usuário que se comunica com o driver por meio de ioctls tem um problema. O cabeçalho `myfirst_ioctl.h` define um conjunto de comandos, layouts e semânticas da versão 1. Uma nova versão do driver pode adicionar comandos, alterar layouts ou alterar semânticas. Quando o programa em espaço do usuário é executado em um sistema com um driver mais novo ou mais antigo do que aquele contra o qual foi compilado, ele não tem como saber a versão real do driver a menos que pergunte.

A solução é um ioctl cujo único propósito é retornar a versão de ioctl do driver. O driver `myfirst` já possui um: `MYFIRSTIOC_GETVER`, definido como `_IOR('M', 1, uint32_t)`. Um programa em espaço do usuário chama esse ioctl imediatamente após abrir o dispositivo, compara a versão retornada com a versão contra a qual foi compilado e decide se pode prosseguir com segurança.

O padrão em espaço do usuário:

```c
#include "myfirst_ioctl.h"

int fd = open("/dev/myfirst0", O_RDWR);
uint32_t ver;
if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
        err(1, "getver");
if (ver != MYFIRST_IOCTL_VERSION)
        errx(1, "driver version %u, tool expects %u",
            ver, MYFIRST_IOCTL_VERSION);
```

A ferramenta se recusa a executar se as versões não coincidirem. Essa é uma política possível. Uma política mais tolerante permitiria que a ferramenta rodasse contra um driver mais novo se os novos ioctls do driver forem superconjuntos dos antigos, e permitiria que rodasse contra um driver mais antigo se a ferramenta puder recorrer ao conjunto de comandos da versão anterior. Uma política mais rígida exigiria correspondência exata. O autor da ferramenta escolhe entre essas opções com base no quanto vale a pena investir em compatibilidade retroativa.

### Adicionando um Novo Ioctl Sem Quebrar Consumidores Mais Antigos

O caso mais comum é adicionar uma nova funcionalidade ao driver, o que geralmente significa adicionar um novo ioctl. A disciplina é direta, desde que duas regras sejam seguidas.

Primeiro, **não reutilize um número de ioctl existente**. Cada comando de ioctl tem um par único `(magic, número)` codificado por `_IO`, `_IOR`, `_IOW` ou `_IOWR`. As atribuições atuais em `myfirst_ioctl.h`:

```c
#define MYFIRSTIOC_GETVER   _IOR('M', 1, uint32_t)
#define MYFIRSTIOC_GETMSG   _IOR('M', 2, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_SETMSG   _IOW('M', 3, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_RESET    _IO('M', 4)
```

Um novo ioctl usa o próximo número disponível sob a mesma letra magic: `MYFIRSTIOC_GETCAPS = _IOR('M', 5, uint32_t)`. O número 5 nunca foi usado antes e não pode conflitar com o binário compilado de um programa mais antigo. Um programa mais antigo compilado contra uma versão sem `GETCAPS` simplesmente nunca envia esse ioctl, portanto não é afetado pela adição.

Segundo, **não incremente `MYFIRST_IOCTL_VERSION` por uma adição pura**. Um novo ioctl que não altera o significado dos existentes é uma mudança compatível. Um programa em espaço do usuário mais antigo que nunca ouviu falar do novo ioctl ainda fala a mesma linguagem; o inteiro de versão não deve ser alterado. Incrementar a versão a cada adição forçaria todos os consumidores a recompilar sempre que o driver ganhasse um novo comando, o que derrota o propósito do versionamento.

Um novo ioctl que substitui um existente com semântica diferente requer um incremento. Se o driver adicionar `MYFIRSTIOC_SETMSG_V2` com um novo layout e aposentar `MYFIRSTIOC_SETMSG`, programas mais antigos que chamam o comando aposentado observam um comportamento alterado (o driver pode retornar `ENOIOCTL` ou se comportar de forma diferente). Isso é uma mudança incompatível, e o incremento a sinaliza.

### Aposentando um Ioctl Deprecado

A aposentadoria é a forma de remoção gerenciada com cortesia. Quando um comando está para ser removido, o driver anuncia a intenção, mantém o comando funcionando por um período de transição e o remove em uma versão posterior. Uma sequência típica de deprecação:

- Versão N: anunciar a deprecação em `MAINTENANCE.md`. O comando ainda funciona.
- Versão N+1: o comando funciona, mas registra um aviso com taxa limitada cada vez que é usado. Os usuários veem o aviso e sabem que precisam migrar.
- Versão N+2: o comando retorna `EOPNOTSUPP` e registra um erro com taxa limitada. A maioria dos usuários já migrou; os poucos restantes são forçados a fazê-lo.
- Versão N+3: o comando é removido do cabeçalho. Programas que ainda o referenciam não compilam mais.

O período de transição deve ser medido em versões (tipicamente uma ou duas versões maiores) e não em tempo calendário. Um driver que mantém seu contrato de deprecação previsível oferece aos consumidores um alvo estável para apontar.

Para o `myfirst` neste capítulo, nenhum comando está ainda deprecado. O capítulo introduz o padrão para uso futuro. A mesma disciplina se aplica à árvore de sysctl: um aviso com taxa limitada no handler do OID informa os operadores que o nome está a caminho de ser removido, e uma nota em `MAINTENANCE.md` registra a data de remoção planejada.

### O Padrão de Bitmask de Capacidades

Para drivers que evoluem ao longo de várias versões, um único inteiro de versão informa aos consumidores com qual versão estão falando, mas não quais funcionalidades específicas essa versão suporta. Um driver rico em funcionalidades se beneficia de um mecanismo mais granular: um bitmask de capacidades.

A ideia é simples. O driver define um conjunto de bits de capacidade em `myfirst_ioctl.h`:

```c
#define MYF_CAP_RESET       (1U << 0)
#define MYF_CAP_GETMSG      (1U << 1)
#define MYF_CAP_SETMSG      (1U << 2)
#define MYF_CAP_TIMEOUT     (1U << 3)
#define MYF_CAP_MAXRETRIES  (1U << 4)
```

Um novo ioctl, `MYFIRSTIOC_GETCAPS`, retorna um `uint32_t` com os bits definidos para as funcionalidades que este driver realmente suporta:

```c
#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)
```

No kernel:

```c
case MYFIRSTIOC_GETCAPS:
        *(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
            MYF_CAP_SETMSG;
        break;
```

Em espaço do usuário:

```c
uint32_t caps;
ioctl(fd, MYFIRSTIOC_GETCAPS, &caps);
if (caps & MYF_CAP_TIMEOUT)
        set_timeout(fd, 60);
else
        warnx("driver does not support timeout configuration");
```

O bitmask de capacidades permite que um programa em espaço do usuário descubra funcionalidades sem tentativa e erro. Se o consumidor quiser saber se uma funcionalidade existe, ele verifica o bit; se o bit estiver definido, o consumidor sabe que o driver suporta a funcionalidade e os ioctls relevantes. Um driver mais antigo que não define o bit não finge suportar uma funcionalidade que nunca conheceu.

O padrão escala bem conforme o driver cresce. Cada versão adiciona novos bits para novas funcionalidades. Funcionalidades descontinuadas mantêm seu bit reservado como não utilizado; reutilizar um bit com um novo significado seria uma mudança incompatível. A própria bitmask é um `uint32_t`, dando ao driver 32 funcionalidades antes de precisar adicionar uma segunda palavra. Se o driver chegar a 32 funcionalidades, adicionar uma segunda palavra é uma mudança compatível (os novos bits ficam em um novo campo, e programas mais antigos que leem apenas a primeira palavra enxergam os mesmos bits).

O Capítulo 25 adiciona `MYFIRSTIOC_GETCAPS` ao driver `myfirst` com três bits definidos: `MYF_CAP_RESET`, `MYF_CAP_GETMSG` e `MYF_CAP_SETMSG`. O programa `myfirstctl` em espaço do usuário é estendido para consultar as capacidades na inicialização e para recusar a invocação de uma funcionalidade não suportada.

### Depreciação de Sysctl

O FreeBSD não oferece um flag `CTLFLAG_DEPRECATED` dedicado na árvore de sysctl. O flag relacionado `CTLFLAG_SKIP`, definido em `/usr/src/sys/sys/sysctl.h`, oculta um OID das listagens padrão (ele ainda pode ser lido se nomeado explicitamente), mas é usado principalmente para fins diferentes do anúncio de retirada. A forma correta de aposentar um OID de sysctl é, portanto, substituir seu handler por um que faça o trabalho pretendido *e* registre um aviso com taxa limitada nas primeiras vezes em que o OID for acessado.

```c
static int
myfirst_sysctl_old_counter(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;

        DLOG_RL(sc, &sc->sc_rl_deprecated, MYF_RL_DEFAULT_PPS,
            "sysctl dev.myfirst.%d.old_counter is deprecated; "
            "use new_counter instead\n",
            device_get_unit(sc->sc_dev));
        return (sysctl_handle_int(oidp, &sc->sc_old_counter, 0, req));
}
```

O operador verá o aviso no `dmesg` nas primeiras vezes em que o OID for lido, o que é uma forte indicação para migrar. O sysctl ainda funciona, portanto scripts que o referenciam explicitamente não quebram durante a transição. Após uma versão ou duas, o próprio OID é removido. Uma nota em `MAINTENANCE.md` registra a intenção e a versão-alvo.

Para o `myfirst`, nenhum sysctl está ainda depreciado. O driver do Capítulo 25 introduz o padrão na documentação e o mantém pronto para uso futuro.

### Mudanças de Comportamento Visíveis ao Usuário

Nem toda mudança incompatível é uma renomeação ou uma renumeração. Às vezes o driver mantém o mesmo ioctl, o mesmo sysctl, o mesmo tunable, e silenciosamente muda o que a operação faz. Um `MYFIRSTIOC_RESET` que antes zerava contadores mas agora também limpa a mensagem é uma mudança de comportamento. Um sysctl que antes reportava o total de bytes escritos mas agora reporta quilobytes é uma mudança de comportamento. Um tunable que antes era um valor absoluto e agora é um multiplicador é uma mudança de comportamento.

Mudanças de comportamento são as mudanças incompatíveis mais difíceis de detectar, porque não aparecem em diffs de arquivos de cabeçalho ou em listagens de sysctl. A disciplina é documentar toda mudança de comportamento em `MAINTENANCE.md` sob uma seção "Change Log", incrementar o inteiro de versão da interface ioctl quando a semântica de um ioctl muda, e anunciar mudanças de semântica de sysctl na própria string de descrição.

Um bom padrão para mudanças de comportamento é introduzir um novo comando nomeado ou um novo sysctl em vez de redefinir um já existente. `MYFIRSTIOC_RESET` mantém a semântica antiga. `MYFIRSTIOC_RESET_ALL` é um novo comando com a nova semântica. O comando antigo é eventualmente depreciado. O custo é uma superfície pública ligeiramente maior durante o período de transição; o benefício é que nenhum chamador é quebrado por uma mudança silenciosa de comportamento.

### `MODULE_DEPEND` e Compatibilidade entre Módulos

A macro `MODULE_DEPEND` declara que um módulo depende de outro e requer um intervalo específico de versões:

```c
MODULE_DEPEND(myfirst, dependency, 1, 2, 3);
```

Os três inteiros são as versões mínima, preferida e máxima de `dependency` com as quais `myfirst` é compatível. O kernel recusa carregar `myfirst` se `dependency` não estiver presente ou estiver fora do intervalo.

Para drivers que não publicam símbolos no kernel, `MODULE_DEPEND` é usado com mais frequência para depender de um módulo de subsistema padrão:

```c
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
```

Isso declara que a versão USB do `myfirst` precisa da pilha USB exatamente na versão 1. Os números de versão para módulos de subsistema são gerenciados pelos autores do subsistema; o autor de um driver encontra os valores atuais no cabeçalho do subsistema (para USB, `/usr/src/sys/dev/usb/usbdi.h`) ou em outro driver que já depende dele.

Para o `myfirst` ao final do Capítulo 25, nenhum `MODULE_DEPEND` é necessário porque o pseudo-driver não requer um subsistema. O Capítulo 26, sobre USB, adicionará o primeiro `MODULE_DEPEND` real quando o driver for transformado em uma versão conectada via USB.

### Um Exemplo Prático: A Transição de 1.7 para 1.8

O driver do Capítulo 25 incrementa três identificadores de versão ao final do capítulo:

- `MYFIRST_VERSION`: de `"1.7-integration"` para `"1.8-maintenance"`.
- `MODULE_VERSION(myfirst, N)`: de 17 para 18.
- `MYFIRST_IOCTL_VERSION`: permanece em 1, porque as adições de ioctl neste capítulo são puramente aditivas (novos comandos, sem remoção, sem mudanças semânticas).

O ioctl `GETCAPS` é adicionado com o número de comando 5, que estava previamente sem uso. Binários antigos de `myfirstctl`, compilados contra a versão do cabeçalho do Capítulo 24, não conhecem o `GETCAPS` e não o enviam; continuam funcionando sem alterações. Novos binários de `myfirstctl`, compilados contra o cabeçalho do Capítulo 25, consultam o `GETCAPS` na inicialização e se comportam de acordo.

O documento `MAINTENANCE.md` ganha uma entrada no Change Log para a versão 1.8:

```text
## 1.8-maintenance

- Added MYFIRSTIOC_GETCAPS (command 5) returning a capability
  bitmask.  Compatible with all earlier user-space programs.
- Added tunables hw.myfirst.timeout_sec, hw.myfirst.max_retries,
  hw.myfirst.log_ratelimit_pps.  Each has a matching writable
  sysctl under dev.myfirst.<unit>.
- Added rate-limited logging through ppsratecheck(9).
- No breaking changes from 1.7.
```

Um usuário do driver que leia `MAINTENANCE.md` vê de relance o que mudou e pode avaliar se precisa atualizar suas ferramentas. Um usuário que não leia `MAINTENANCE.md` ainda pode consultar as capacidades em tempo de execução e descobrir as novas funcionalidades de forma programática.

### Erros Comuns em Versionamento

Três erros são comuns quando se aplica a disciplina de versionamento pela primeira vez. Vale a pena nomear cada um deles.

O primeiro erro é **reutilizar um número de ioctl**. Um número que já foi atribuído e depois aposentado permanece aposentado. Um novo comando recebe o próximo número disponível, não o número de um comando aposentado. Reutilizar um número quebra silenciosamente chamadores mais antigos que compilaram o significado anterior; o compilador não tem como detectar o conflito porque o cabeçalho do comando aposentado foi removido.

O segundo erro é **incrementar o inteiro de versão a cada mudança**. Se cada patch incrementa `MYFIRST_IOCTL_VERSION`, as ferramentas de espaço do usuário precisam ser recompiladas constantemente ou a verificação de versão falha. O inteiro deve mudar apenas para mudanças genuinamente incompatíveis. Adições puras deixam esse valor intocado.

O terceiro erro é **tratar a string de release como uma versão semântica**. A string de release é para humanos; pode ser qualquer coisa. O inteiro de versão do módulo e o inteiro de versão do ioctl são analisados por programas e devem seguir uma disciplina (monotonicamente crescente, incrementado apenas por razões específicas). Confundir os dois leva a números de versão confusos.

### Encerrando a Seção 4

Versionamento é a disciplina de evoluir sem quebrar. Um driver que mantém seus três identificadores de versão distintos, suas adições de ioctl compatíveis, suas depreciações anunciadas e seus bits de capacidade precisos oferece a seus chamadores um alvo estável ao longo da longa vida do driver. O driver `myfirst` agora tem um ioctl `GETCAPS` funcionando, uma política de depreciação documentada em `MAINTENANCE.md`, e três identificadores de versão que cada um muda por sua própria razão. Tudo o que um desenvolvedor futuro precisa para adicionar uma funcionalidade ou aposentar um comando já está em vigor.

Na próxima seção, passamos da superfície pública do driver para a disciplina de recursos privados. Um driver que trava no attach em caso de falha é um driver que não consegue se recuperar de nenhum erro. O padrão de goto rotulado é como os drivers do FreeBSD tornam toda alocação reversível.

## Seção 5: Gerenciando Recursos em Caminhos de Falha

Toda rotina de attach é uma sequência ordenada de aquisições. Ela aloca um lock, cria um cdev, instala uma árvore de sysctl no dispositivo, talvez registre um handler de eventos ou um timer, e em drivers mais complexos aloca recursos de barramento, mapeia janelas de I/O, associa uma interrupção e configura DMA. Cada aquisição pode falhar. E cada aquisição que teve sucesso antes da falha deve ser liberada na ordem inversa, ou o kernel vaza memória, vaza um lock, vaza um cdev e, no pior caso, mantém um nó de dispositivo vivo com um ponteiro obsoleto dentro dele.

O driver `myfirst` vem expandindo seu attach path uma seção por vez desde o Capítulo 17. O attach começou pequeno: um lock e um cdev. O Capítulo 24 adicionou a árvore de sysctl. O Capítulo 25 está prestes a adicionar estado de controle de taxa, padrões obtidos de tunables e um ou dois contadores. A ordem em que esses recursos são adquiridos agora importa para o caminho de limpeza. Cada nova aquisição precisa saber em que posição da ordem de desfazimento ela se encaixa, e o próprio desfazimento precisa ser estruturado de forma que adicionar um novo recurso na semana seguinte não force uma reescrita da função de attach.

O Capítulo 20 introduziu o padrão de forma informal; esta seção lhe dá um nome, um vocabulário e uma disciplina robusta o suficiente para sobreviver à forma completa do `myfirst_attach` no Capítulo 25.

### O Problema: Caminhos `if` Aninhados Não Escalam

A forma ingênua de uma rotina de attach é uma escada de instruções `if` aninhadas. Cada condição de sucesso contém o próximo passo. Cada falha retorna. O problema é que toda falha precisa desfazer o que os passos anteriores já fizeram, e o código de desfazimento é duplicado em cada nível da escada:

```c
/*
 * Naive attach.  DO NOT WRITE DRIVERS THIS WAY.  This example shows
 * how the nested-if pattern forces duplicated cleanup at every level
 * and why it becomes unmaintainable as soon as a fourth resource is
 * added to the chain.
 */
static int
myfirst_attach_bad(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error == 0) {
		myfirst_sysctl_attach(sc);
		if (myfirst_log_attach(sc) == 0) {
			/* all resources held; we succeeded */
			return (0);
		} else {
			/* log allocation failed: undo sysctl and cdev */
			/* but wait, sysctl is owned by Newbus, so skip it */
			destroy_dev(sc->sc_cdev);
			mtx_destroy(&sc->sc_mtx);
			return (ENOMEM);
		}
	} else {
		mtx_destroy(&sc->sc_mtx);
		return (error);
	}
}
```

Mesmo neste pequeno exemplo, a lógica de desfazimento aparece em dois lugares diferentes, o leitor precisa percorrer um branch para saber quais recursos foram adquiridos em cada ponto, e adicionar um quarto recurso força mais um nível de aninhamento e mais um bloco de limpeza duplicado. Drivers reais têm sete ou oito recursos. Um driver como o `if_em` em `/usr/src/sys/dev/e1000/if_em.c` tem mais de uma dúzia. O aninhamento com `if` não é uma opção nesse caso.

Os modos de falha do padrão aninhado não são teóricos. Um padrão de bug comum em drivers mais antigos do FreeBSD era um `mtx_destroy` ausente ou um `bus_release_resource` ausente em um dos branches de limpeza: um branch destruía o lock, outro esquecia. Cada branch era uma oportunidade de cometer um erro, e o bug só aparecia quando aquela falha específica ocorria, o que significava que muitas vezes não aparecia até que um cliente reportasse um panic em um dispositivo que falhou ao fazer attach.

### O Padrão `goto fail;`

A resposta do FreeBSD para o problema de limpeza aninhada é o padrão de goto rotulado. A função de attach é escrita como uma sequência linear de aquisições. Cada aquisição que pode falhar é seguida por um teste que ou prossegue em caso de sucesso ou salta para um rótulo de limpeza em caso de falha. Os rótulos de limpeza são ordenados da aquisição mais recente de volta para a mais antiga. Cada rótulo libera os recursos que estavam retidos naquele ponto e então passa para o próximo rótulo. A função termina com um único `return (0)` em caso de sucesso e um único `return (error)` ao final da cadeia de limpeza:

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/* Resource 1: softc basics.  Cannot fail. */
	sc->sc_dev = dev;

	/* Resource 2: the lock.  Cannot fail on DEF mutex. */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/* Resource 3: the cdev.  Can fail. */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/* Resource 4: the sysctl tree.  Cannot fail (Newbus owns it). */
	myfirst_sysctl_attach(sc);

	/* Resource 5: the log state.  Can fail. */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	/* All resources held.  Announce and return. */
	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance ready\n");
	return (0);

fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

Leia a função de cima para baixo. Cada passo é uma aquisição de recurso. Cada verificação de falha é um bloco de duas linhas: se a aquisição falhou, salte para o rótulo com o nome do recurso adquirido anteriormente. Os rótulos no final liberam os recursos em ordem inversa e passam para o próximo rótulo. O `return (error)` final retorna o errno da aquisição que falhou.

Essa forma escala. Adicionar um sexto recurso significa acrescentar um bloco de aquisição no topo, um alvo `goto` no final e uma linha de código de limpeza. Sem aninhamento, sem duplicação, sem árvore de branches. A mesma regra que governa o attach governa toda adição futura a ele: adquira, teste, vá para o rótulo anterior, libere na ordem inversa.

### Por Que o Desfazimento Linear É a Forma Correta

O valor do padrão de goto rotulado não é puramente estilístico. Ele se mapeia diretamente sobre a propriedade estrutural de que o attach é uma pilha de recursos, e a limpeza é a operação de pop dessa pilha.

Uma pilha tem três propriedades fáceis de enunciar e fáceis de violar. Primeiro, os recursos são liberados na ordem inversa de aquisição. Segundo, uma aquisição que falhou não adiciona um recurso à pilha, portanto a limpeza começa a partir do recurso adquirido anteriormente, não do que acabou de falhar. Terceiro, todo recurso na pilha é liberado exatamente uma vez: nem zero vezes, nem duas.

Cada uma dessas propriedades tem um correlato visível no padrão `goto fail;`. Os rótulos de limpeza aparecem no arquivo na ordem inversa das aquisições: o rótulo de limpeza da última aquisição está no topo da cadeia de limpeza. Uma aquisição que falhou salta para o rótulo com o nome da aquisição anterior, não de si mesma; o nome do rótulo é literalmente o nome do recurso que agora precisa ser desfeito. E como cada rótulo passa para o próximo, e cada recurso aparece em exatamente um rótulo, todo recurso é liberado exatamente uma vez em todo caminho de falha.

A disciplina de pilha é o que torna o padrão robusto. Se um leitor quiser auditar o caminho de limpeza para verificar sua correção, não precisa percorrer branches. Basta contar os rótulos, contar as aquisições e comparar.

### Convenções de Nomenclatura para Labels

Labels em drivers FreeBSD tradicionalmente começam com `fail_` seguido do nome do recurso que está prestes a ser desfeito. O nome do recurso corresponde ao nome do campo no softc ou ao nome da função chamada para adquiri-lo. Padrões comuns encontrados ao longo da árvore:

- `fail_mtx` desfaz `mtx_init`
- `fail_sx` desfaz `sx_init`
- `fail_cdev` desfaz `make_dev_s`
- `fail_ires` desfaz `bus_alloc_resource` para um IRQ
- `fail_mres` desfaz `bus_alloc_resource` para uma janela de memória
- `fail_intr` desfaz `bus_setup_intr`
- `fail_dma_tag` desfaz `bus_dma_tag_create`
- `fail_log` desfaz uma alocação privada do driver (o bloco de limite de taxa em `myfirst`)

Alguns drivers mais antigos utilizam labels numeradas (`fail1`, `fail2`, `fail3`). Labels numeradas são válidas, mas inferiores: adicionar um recurso no meio da sequência obriga a renumerar todas as labels após o ponto de inserção, e os números não dizem ao leitor qual recurso está sendo liberado. Labels nomeadas sobrevivem bem a inserções e se documentam sozinhas.

Qualquer que seja a convenção adotada pelo driver, ela deve ser consistente em todos os seus arquivos. `myfirst` utiliza a convenção `fail_<recurso>` em todas as funções attach a partir deste capítulo em diante.

### A Regra do Fall-Through

A única regra que toda cadeia de limpeza deve obedecer é que cada label de limpeza caia para a próxima. Um `return` perdido no meio da cadeia, ou uma label ausente, pula a limpeza dos recursos que deveriam ter sido liberados. O compilador não avisa sobre nenhum dos dois erros.

Considere o que acontece quando um desenvolvedor edita a cadeia de limpeza e acidentalmente escreve o seguinte:

```c
fail_cdev:
	destroy_dev(sc->sc_cdev);
	return (error);          /* BUG: skips mtx_destroy. */
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

O primeiro `return` impede que `mtx_destroy` seja executado no caminho `fail_cdev`. O lock vaza. O código witness do kernel não reclamará, porque o lock vazado nunca é adquirido novamente. O vazamento persiste até que a máquina reinicialize. Ele é invisível na operação normal e só aparece como um lento crescimento de memória em um sistema onde o driver faz attach e falha repetidamente (um dispositivo com hot-plug, por exemplo).

A forma de prevenir esse tipo de bug é escrever a cadeia de limpeza com um único `return` no final e nenhum retorno intermediário. As labels do meio contêm apenas a chamada de limpeza para seu respectivo recurso. O fall-through é o comportamento padrão e esperado:

```c
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

Um leitor auditando a cadeia a lê como uma lista simples: destruir cdev, destruir lock, retornar. Não há ramificações a seguir, e adicionar uma label significa adicionar uma única linha de código de limpeza e, opcionalmente, um único novo alvo.

### Como Fica o Caminho de Sucesso

A função attach conclui com sucesso com um único `return (0)`, colocado imediatamente antes da primeira label de limpeza. Esse é o ponto em que todas as aquisições foram bem-sucedidas e nenhuma limpeza é necessária. O `return (0)` separa visualmente a cadeia de aquisição da cadeia de limpeza: tudo acima dele é aquisição, tudo abaixo é limpeza.

Alguns drivers esquecem essa separação e deixam o controle cair do último passo de aquisição direto para a primeira label de limpeza, liberando recursos que acabaram de ser adquiridos. Um `return (0)` ausente por descuido é a forma mais simples de produzir esse bug:

```c
	/* Resource N: the final acquisition. */
	...

	/* Forgot to put a return here. */

fail_cdev:
	destroy_dev(sc->sc_cdev);
```

Sem o `return (0)`, o controle cai em `fail_cdev` após cada attach bem-sucedido, destruindo o cdev no caminho de sucesso. O driver então reporta a falha de attach porque `error` é zero e o kernel enxerga o retorno bem-sucedido, mas o cdev que acabou de ser criado já não existe. O resultado é um nó de dispositivo que desaparece segundos depois de aparecer. Depurar isso exige perceber que a mensagem de attach é impressa, mas o dispositivo não responde — não é um bug fácil de encontrar em um log movimentado.

A defesa é disciplina. Toda função attach termina sua cadeia de aquisição com `return (0);` em sua própria linha, seguida de uma linha em branco, seguida das labels de limpeza. Sem exceções. Uma ferramenta como `igor` ou o olhar de um revisor detecta violações rapidamente quando o formato é sempre o mesmo.

### Quando uma Aquisição Não Pode Falhar

Algumas aquisições não podem falhar. `mtx_init` para um mutex de estilo padrão não pode retornar um erro. `sx_init` tampouco. `callout_init_mtx` tampouco. Chamadas `SYSCTL_ADD_*` não podem retornar um erro que o driver deva verificar (uma falha ali é um problema interno do kernel, não um problema do driver).

Para aquisições que não podem falhar, não há goto. A aquisição é seguida pelo próximo passo sem nenhum teste. A label de limpeza para essa aquisição ainda é necessária, porque a cadeia de limpeza precisa liberar o recurso caso uma aquisição posterior falhe:

```c
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	error = make_dev_s(&args, &sc->sc_cdev, ...);
	if (error != 0)
		goto fail_mtx;       /* undoes the lock. */
```

`fail_mtx` existe mesmo que o próprio `mtx_init` não tivesse caminho de falha, porque o lock ainda precisa ser destruído se algo abaixo dele falhar.

O padrão se mantém: todo recurso adquirido tem uma label, independentemente de sua aquisição poder ou não falhar.

### Helpers para Reduzir Duplicação

Quando várias aquisições compartilham a mesma forma (alocar, verificar, ir para um goto em caso de erro), é tentador escondê-las dentro de uma função auxiliar. O papel do helper é consolidar a aquisição e a verificação; o chamador vê apenas uma única linha `if (error != 0) goto fail_X;`. Isso é aceitável, desde que o helper siga a mesma disciplina: em caso de falha, ele não libera nada que tenha adquirido parcialmente, e retorna um errno significativo para que o alvo do goto do chamador possa confiar nele.

Em `myfirst`, os exemplos complementares da Seção 5 introduzem um helper chamado `myfirst_log_attach` que aloca o estado de limite de taxa, inicializa seus campos e retorna 0 em caso de sucesso ou um errno diferente de zero em caso de falha. A função attach o chama com uma única linha:

```c
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;
```

O próprio helper segue o mesmo padrão internamente. Se ele aloca dois recursos e o segundo falha, o helper desfaz o primeiro antes de retornar. O chamador enxerga o helper como uma aquisição atômica única: ou ele teve sucesso completo ou falhou completamente, e o chamador nunca precisa se preocupar com o estado intermediário do helper.

Helpers que tentam simplificar demais, porém, quebram o padrão. Um helper que aloca um recurso e o armazena no softc é adequado. Um helper que aloca um recurso, o armazena no softc e também o libera em caso de erro não é adequado: a label de limpeza do chamador também tentará liberá-lo, levando a um double-free. A regra é que helpers de aquisição ou têm sucesso e deixam o recurso no softc, ou falham e deixam o softc inalterado. Eles não têm sucesso pela metade.

### Detach como o Espelho do Attach

A rotina detach é a cadeia de limpeza de um attach bem-sucedido. Ela precisa liberar exatamente os recursos que o attach adquiriu, na ordem inversa. A forma da função detach é a forma da cadeia de limpeza sem as labels e sem as aquisições:

```c
static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Check for busy first. */
	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0) {
		mtx_unlock(&sc->sc_mtx);
		return (EBUSY);
	}
	mtx_unlock(&sc->sc_mtx);

	/* Release resources in the reverse order of attach. */
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	/* Sysctl is cleaned up by Newbus after detach returns. */
	mtx_destroy(&sc->sc_mtx);

	return (0);
}
```

Lidos lado a lado com a função attach, a correspondência é exata. Todo recurso nomeado no attach tem uma liberação no detach. Toda nova aquisição adicionada ao attach tem uma adição correspondente no detach. Um revisor auditando um patch que adiciona um novo recurso ao driver deve ser capaz de encontrar ambas as adições no diff, uma na cadeia do attach e outra na cadeia do detach; um diff que adiciona apenas ao attach está incompleto.

Uma disciplina útil ao modificar a cadeia de attach é abrir a função detach em um buffer de editor adjacente e adicionar a liberação imediatamente após adicionar a aquisição. Essa é a forma mais simples de garantir que as duas funções permaneçam sincronizadas: elas são editadas juntas como uma única operação.

### Injeção Deliberada de Falhas para Testes

Uma cadeia de limpeza está correta apenas se toda label for alcançável. A única maneira de ter certeza disso é disparar cada caminho de falha propositalmente e observar que o driver é descarregado de forma limpa em seguida. Esperar que falhas reais de hardware exercitem os caminhos não é uma estratégia: a maioria desses caminhos nunca é exercitada na prática.

A ferramenta para esse tipo de teste é a injeção deliberada de falhas. O desenvolvedor adiciona um `goto` temporário ou um retorno antecipado temporário no meio da cadeia de attach e confirma que os recursos do driver são todos liberados quando a falha injetada é disparada.

Um padrão mínimo para `myfirst`:

```c
#ifdef MYFIRST_DEBUG_INJECT_FAIL_CDEV
	error = ENOMEM;
	goto fail_cdev;
#endif
```

Compile o driver com `-DMYFIRST_DEBUG_INJECT_FAIL_CDEV` e carregue-o. O attach retorna `ENOMEM`. `kldstat` não mostra nenhum resíduo. `dmesg` mostra a falha de attach sem queixas do kernel sobre locks ou recursos vazados. Descarregue o módulo, remova o define, recompile e o driver volta ao normal.

Faça isso uma vez para cada label, em sequência:

1. Injete uma falha logo após o lock ser inicializado. Confirme que apenas o lock é liberado.
2. Injete uma falha logo após o cdev ser criado. Confirme que o cdev e o lock são liberados.
3. Injete uma falha logo após a árvore sysctl ser construída. Confirme que o cdev e o lock são liberados, e que os OIDs do sysctl desaparecem.
4. Injete uma falha logo após o estado do log ser inicializado. Confirme que todos os recursos adquiridos até esse ponto são liberados.

Se qualquer injeção deixar um resíduo, a cadeia de limpeza tem um bug. Corrija o bug, reexecute a injeção e siga em frente.

Esse é um trabalho desconfortável na primeira vez, e reconfortante depois. Um driver cujos caminhos de falha foram exercitados ao menos uma vez é um driver cujos caminhos de falha continuarão funcionando à medida que o código evolui. Um driver cujos caminhos de falha nunca foram exercitados é um driver com bugs latentes que aparecerão no pior momento possível.

O exemplo complementar `ex05-failure-injection/` em `examples/part-05/ch25-advanced/` contém uma versão de `myfirst_attach` com cada ponto de injeção de falha marcado por um `#define` comentado. O laboratório ao final do capítulo percorre cada injeção em sequência.

### Um `myfirst_attach` Completo para o Capítulo 25

Reunindo tudo da Seção 5 com as adições do Capítulo 25 (estado do log, leituras de tunables, bitmask de capacidades), a função attach final fica assim:

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/*
	 * Stage 1: softc basics.  Cannot fail.  Recorded for consistency;
	 * no cleanup label is needed because no resource is held yet.
	 */
	sc->sc_dev = dev;

	/*
	 * Stage 2: lock.  Cannot fail on MTX_DEF, but needs a label
	 * because anything below this line can fail and must release it.
	 */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/*
	 * Stage 3: pre-populate the softc with defaults, then allow
	 * boot-time tunables to override.  No allocations here, so no
	 * cleanup is needed.  Defaults come from the Section 3 tunable
	 * set.
	 */
	strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
	sc->sc_msglen = strlen(sc->sc_msg);
	sc->sc_open_count = 0;
	sc->sc_total_reads = 0;
	sc->sc_total_writes = 0;
	sc->sc_debug = 0;
	sc->sc_timeout_sec = 5;
	sc->sc_max_retries = 3;
	sc->sc_log_pps = MYF_RL_DEFAULT_PPS;

	TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default",
	    &sc->sc_debug);
	TUNABLE_INT_FETCH("hw.myfirst.timeout_sec",
	    &sc->sc_timeout_sec);
	TUNABLE_INT_FETCH("hw.myfirst.max_retries",
	    &sc->sc_max_retries);
	TUNABLE_INT_FETCH("hw.myfirst.log_ratelimit_pps",
	    &sc->sc_log_pps);

	/*
	 * Stage 4: cdev.  Can fail.  On failure, release the lock and
	 * return the error from make_dev_s.
	 */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/*
	 * Stage 5: sysctl tree.  Cannot fail.  The framework owns the
	 * context, so no cleanup label is required specifically for it.
	 */
	myfirst_sysctl_attach(sc);

	/*
	 * Stage 6: rate-limit and counter state.  Can fail if memory
	 * allocation fails.  On failure, release the cdev and the lock.
	 */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance complete\n");
	return (0);

fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

Todo recurso está contabilizado. Todo caminho de falha é linear. A função tem um único retorno de sucesso na transição entre aquisições e limpeza, e um único retorno de falha no final da cadeia de limpeza. Adicionar um sétimo recurso no próximo capítulo é uma operação de três linhas: um novo bloco de aquisição, uma nova label, uma nova linha de limpeza.

### Erros Comuns em Caminhos de Falha

Vale a pena nomear alguns erros de caminhos de falha uma vez, para que possam ser reconhecidos quando aparecerem no código de outra pessoa ou em uma revisão.

O primeiro erro é **uma label ausente**. Um desenvolvedor adiciona uma nova aquisição de recurso, mas esquece de adicionar sua label de limpeza. O compilador não avisa; a cadeia parece correta por fora; mas em caso de falha após a nova aquisição, a limpeza de tudo abaixo é ignorada. A regra é que toda aquisição tem uma label. Mesmo que a aquisição não possa falhar, ela ainda precisa de uma label para que aquisições posteriores possam alcançá-la.

O segundo erro é **liberar um recurso duas vezes**. Um desenvolvedor adiciona uma limpeza local dentro de um helper e esquece que a label de limpeza do chamador também libera o recurso. O helper libera uma vez, o chamador libera novamente, e ou o kernel entra em pânico (para memória) ou o código witness reclama (para locks). A regra é que apenas uma parte é responsável pela limpeza de cada recurso. Se o helper adquire o recurso e o armazena no softc, o helper não o libera em nome do chamador; ele ou tem sucesso ou deixa o softc intocado.

O terceiro erro é **depender de testes de `NULL`**. Um desenvolvedor escreve uma cadeia de limpeza assim:

```c
fail_cdev:
	if (sc->sc_cdev != NULL)
		destroy_dev(sc->sc_cdev);
fail_mtx:
	if (sc->sc_mtx_initialised)
		mtx_destroy(&sc->sc_mtx);
```

A lógica é: pular a limpeza caso o recurso não tenha sido de fato adquirido. A intenção é defensiva; o efeito é ocultar bugs. Se a verificação de `NULL` está ali porque a limpeza pode ser alcançada em um estado em que o recurso não foi adquirido, a cadeia está errada: o destino do `goto` deveria ser um label diferente. O comportamento correto é tornar o label de limpeza inalcançável a menos que o recurso tenha sido de fato adquirido. Labels que podem ser alcançados em qualquer um dos estados são sintoma de uma ordem de aquisição confusa, e a verificação de `NULL` apenas mascara o problema.

O quarto erro é **usar `goto` para fluxos que não são de erro**. O `goto` na função attach é estritamente para caminhos de falha. Um `goto` que pula uma seção da cadeia de aquisição por alguma condição que não é de erro viola o invariante de limpeza linear: a cadeia de limpeza assume que cada label corresponde a um recurso que foi adquirido, e um `goto` que ignora uma aquisição quebra essa suposição. Se uma aquisição condicional for necessária, use um `if` em torno da própria aquisição, não um `goto` ao redor dela.

### Encerrando a Seção 5

O attach e o detach são as costuras que prendem um driver ao kernel. Um attach correto é uma pilha linear de aquisições; um detach correto é essa pilha sendo desempilhada na ordem inversa. O padrão de goto rotulado é a forma como os drivers do FreeBSD codificam essa pilha em C sem recorrer a mecanismos de outros sistemas (destrutores de C++, defer do Go, Drop do Rust). Ele é pouco glamouroso e escala: um driver com uma dúzia de recursos se lê exatamente como um driver com dois, e as regras para adicionar um novo recurso são sempre as mesmas.

A função attach do `myfirst` agora tem quatro labels de falha e uma separação clara entre aquisição, retorno de sucesso e limpeza. Todo novo recurso que o Capítulo 26 adicionar caberá nessa forma.

Na próxima seção, saímos do escopo de uma única função e examinamos como um driver em crescimento se distribui entre arquivos. Um único `myfirst.c` com todas as funções nos acompanhou por oito capítulos; chegou a hora de dividi-lo em unidades focadas para que a estrutura do driver seja visível no nível dos arquivos.

## Seção 6: Modularização e Separação de Responsabilidades

Ao final do Capítulo 24, o driver `myfirst` cresceu além do que um único arquivo fonte consegue comportar confortavelmente. A estrutura de arquivos era `myfirst.c` mais `myfirst_debug.c`, `myfirst_ioctl.c` e `myfirst_sysctl.c`; `myfirst.c` ainda carregava o cdevsw, os callbacks de leitura/escrita, os callbacks de abertura/fechamento, as rotinas de attach e detach e o código de inicialização do módulo. Isso era adequado para o ensino, pois cada adição caía em um arquivo pequeno o suficiente para o leitor manter na cabeça. Já não é adequado para um driver que possui uma superfície de ioctl, uma árvore de sysctl, um framework de debug, um helper de logging com limitação de taxa, um bitmask de capacidades, uma disciplina de versionamento e uma rotina de attach com limpeza por labels. Um arquivo com tudo isso se torna difícil de ler, difícil de comparar com diff e difícil de entregar a um novo colaborador.

A Seção 6 trata da direção oposta. Ela não introduz nenhum comportamento novo; toda função existente ao final da Seção 5 ainda está aqui ao final da Seção 6. O que muda é o layout dos arquivos e as linhas divisórias entre as partes. O objetivo é um driver cuja estrutura você entenda com um `ls`, e cujos arquivos individuais respondam, cada um, a uma única pergunta.

### Por Que Dividir em Arquivos

A tentação com um driver autocontido é manter tudo em um único arquivo. Um único `myfirst.c` é fácil de localizar, fácil de fazer grep, fácil de copiar para um tarball. Dividir parece burocracia. O argumento para dividir aparece quando o driver ultrapassa um de três limites.

O primeiro limite é a **compreensão**. Um leitor que abre `myfirst.c` deve conseguir encontrar o que procura em poucos segundos. Um arquivo de 1200 linhas com oito responsabilidades não relacionadas é difícil de navegar; o leitor precisa rolar além do cdevsw para encontrar o sysctl, além do sysctl para encontrar o ioctl, além do ioctl para encontrar a rotina de attach. Cada vez que muda de assunto, precisa recarregar o contexto mental. Com arquivos separados, o assunto é o próprio nome do arquivo: `myfirst_ioctl.c` trata de ioctls, `myfirst_sysctl.c` trata de sysctls, `myfirst.c` trata do ciclo de vida.

O segundo limite é a **independência**. Duas mudanças não relacionadas não devem modificar o mesmo arquivo. Quando um desenvolvedor adiciona um sysctl e outro adiciona um ioctl, os patches deles não devem competir pelas mesmas linhas de `myfirst.c`. Arquivos pequenos e focados permitem que duas mudanças cheguem em paralelo sem conflito de merge e sem risco de que um bug em uma mudança afete acidentalmente a outra.

O terceiro limite é a **testabilidade e reutilização**. A infraestrutura de logging de um driver, seu dispatch de ioctl e sua árvore de sysctl são frequentemente úteis a mais de um driver dentro do mesmo projeto. Mantê-los em arquivos separados com interfaces limpas os torna candidatos a compartilhamento futuro. Um driver que vive em um único arquivo não consegue compartilhar nada com facilidade; extrair código significa copiar e renomear manualmente, o que é uma operação sujeita a erros.

O `myfirst` ao final do Capítulo 25 ultrapassou os três limites. Dividir o arquivo é o ato de manutenção que mantém o driver saudável pelos próximos dez capítulos.

### Um Layout de Arquivos para o `myfirst`

O layout proposto é o que o Makefile no diretório de exemplos do Capítulo 25 final utiliza:

```text
myfirst.h          - public types and constants (softc, SRB, status bits).
myfirst.c          - module glue, cdevsw, devclass, module events.
myfirst_bus.c      - Newbus methods and device_identify.
myfirst_cdev.c     - open/close/read/write callbacks; no ioctl.
myfirst_ioctl.h    - ioctl command numbers and payload structures.
myfirst_ioctl.c    - myfirst_ioctl switch and helpers.
myfirst_sysctl.c   - myfirst_sysctl_attach and handlers.
myfirst_debug.h    - DPRINTF/DLOG/DLOG_RL macros and class bits.
myfirst_debug.c    - debug-class enumeration (if any out-of-line).
myfirst_log.h      - rate-limit state structure.
myfirst_log.c      - myfirst_log_attach/detach and helpers.
```

Sete arquivos `.c` e quatro arquivos `.h`. Cada arquivo `.c` tem um assunto identificado pelo seu nome. Os headers declaram as interfaces que cruzam as fronteiras entre arquivos. Nenhum arquivo importa os internos de outro; toda referência entre arquivos passa por um header.

À primeira vista, isso parece mais arquivos do que o driver precisa. Não é. Cada arquivo tem uma responsabilidade específica, e o header correspondente tem de uma a três dúzias de linhas de declarações. O tamanho total é o mesmo da versão em arquivo único; a estrutura é dramaticamente mais clara.

### A Regra de Responsabilidade Única

A regra que governa a divisão é a regra de responsabilidade única: cada arquivo responde a uma pergunta sobre o driver.

- `myfirst.c` responde: como esse módulo faz attach ao kernel e conecta suas partes?
- `myfirst_bus.c` responde: como o Newbus descobre e instancia meu driver?
- `myfirst_cdev.c` responde: como o driver atende open/close/read/write?
- `myfirst_ioctl.c` responde: como o driver trata os comandos declarados em seu header?
- `myfirst_sysctl.c` responde: como o driver expõe seu estado ao `sysctl(8)`?
- `myfirst_debug.c` responde: como as mensagens de debug são classificadas e limitadas em taxa?
- `myfirst_log.c` responde: como o estado de limitação de taxa é inicializado e liberado?

O teste para verificar se uma mudança pertence a um determinado arquivo é o teste da resposta. Se a mudança não responde à pergunta do arquivo, ela pertence a outro lugar. Um novo sysctl não pertence a `myfirst_ioctl.c`; um novo ioctl não pertence a `myfirst_sysctl.c`; uma nova variante de callback de leitura não pertence a `myfirst.c`. A regra é explícita, e um revisor que a aplica rejeita patches que colocam coisas no arquivo errado.

Aplicar a regra à estrutura existente do Capítulo 24 resulta na estrutura do Capítulo 25.

### Headers Públicos vs. Privados

Os headers carregam a interface entre arquivos. Um driver que divide seus arquivos `.c` precisa decidir, para cada declaração, se ela pertence a um header público ou privado.

**Headers públicos** contêm tipos e constantes visíveis a mais de um arquivo `.c`. `myfirst.h` é o header público principal do driver. Ele declara:

- A definição de `struct myfirst_softc` (todo arquivo `.c` precisa dela).
- Constantes que aparecem em mais de um arquivo (bits de classe de debug, tamanhos de campos do softc).
- Protótipos de funções chamadas entre arquivos (`myfirst_sysctl_attach`, `myfirst_log_attach`, `myfirst_log_ratelimited_printf`, `myfirst_ioctl`).

**Headers privados** carregam declarações necessárias a apenas um arquivo `.c`. `myfirst_ioctl.h` é o exemplo canônico. Ele declara os números de comando e as estruturas de payload; eles são necessários para `myfirst_ioctl.c` e para chamadores no espaço do usuário, mas nenhum outro arquivo no kernel precisa deles. Colocá-los em `myfirst.h` vazaria o formato de wire para cada unidade de compilação.

A distinção importa porque toda declaração pública é um contrato que o driver precisa honrar. Um tipo em `myfirst.h` que muda de tamanho quebra todo arquivo que inclui `myfirst.h`. Um tipo em `myfirst_ioctl.h` que muda de tamanho quebra apenas `myfirst_ioctl.c` e as ferramentas no espaço do usuário que compilaram contra ele.

Para o `myfirst` ao final do Capítulo 25, o header público `myfirst.h` tem a seguinte aparência (reduzido às declarações relevantes para esta seção):

```c
/*
 * myfirst.h - public types and constants for the myfirst driver.
 *
 * Types and prototypes declared here are visible to every .c file in
 * the driver.  Keep this header small.  Wire-format declarations live
 * in myfirst_ioctl.h.  Debug macros live in myfirst_debug.h.  Rate-
 * limit state lives in myfirst_log.h.
 */

#ifndef _MYFIRST_H_
#define _MYFIRST_H_

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include "myfirst_log.h"

struct myfirst_softc {
	device_t       sc_dev;
	struct mtx     sc_mtx;
	struct cdev   *sc_cdev;

	char           sc_msg[MYFIRST_MSG_MAX];
	size_t         sc_msglen;

	u_int          sc_open_count;
	u_int          sc_total_reads;
	u_int          sc_total_writes;

	u_int          sc_debug;
	u_int          sc_timeout_sec;
	u_int          sc_max_retries;
	u_int          sc_log_pps;

	struct myfirst_ratelimit sc_rl_generic;
	struct myfirst_ratelimit sc_rl_io;
	struct myfirst_ratelimit sc_rl_intr;
};

#define MYFIRST_MSG_MAX  256

/* Sysctl tree. */
void myfirst_sysctl_attach(struct myfirst_softc *);

/* Rate-limit state. */
int  myfirst_log_attach(struct myfirst_softc *);
void myfirst_log_detach(struct myfirst_softc *);

/* Ioctl dispatch. */
struct thread;
int  myfirst_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);

#endif /* _MYFIRST_H_ */
```

Nada em `myfirst.h` faz referência a uma constante de formato de wire, a um bit de classe de debug ou aos internos de uma estrutura de limitação de taxa. O softc inclui três campos de limitação de taxa por valor, portanto `myfirst.h` precisa incluir `myfirst_log.h`, mas os internos de `struct myfirst_ratelimit` vivem em `myfirst_log.h` e não são expostos aqui.

### A Anatomia de `myfirst.c` Após a Divisão

O `myfirst.c` após a divisão é o menor arquivo `.c` do driver. Ele contém a tabela cdevsw, o handler de eventos do módulo, a declaração de classe de dispositivo e as rotinas de attach/detach. Todas as demais responsabilidades migraram para outros lugares:

```c
/*
 * myfirst.c - module glue and cdev wiring for the myfirst driver.
 *
 * This file owns the cdevsw table, the devclass, the attach and
 * detach routines, and the MODULE_VERSION declaration.  The cdev
 * callbacks themselves live in myfirst_cdev.c.  The ioctl dispatch
 * lives in myfirst_ioctl.c.  The sysctl tree lives in
 * myfirst_sysctl.c.  The rate-limit infrastructure lives in
 * myfirst_log.c.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

MODULE_VERSION(myfirst, 18);

extern d_open_t    myfirst_open;
extern d_close_t   myfirst_close;
extern d_read_t    myfirst_read;
extern d_write_t   myfirst_write;

struct cdevsw myfirst_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "myfirst",
	.d_open    = myfirst_open,
	.d_close   = myfirst_close,
	.d_read    = myfirst_read,
	.d_write   = myfirst_write,
	.d_ioctl   = myfirst_ioctl,
};

static int
myfirst_attach(device_t dev)
{
	/* Section 5's labelled-cleanup attach goes here. */
	...
}

static int
myfirst_detach(device_t dev)
{
	/* Section 5's mirror-of-attach detach goes here. */
	...
}

static device_method_t myfirst_methods[] = {
	DEVMETHOD(device_probe,   myfirst_probe),
	DEVMETHOD(device_attach,  myfirst_attach),
	DEVMETHOD(device_detach,  myfirst_detach),
	DEVMETHOD_END
};

static driver_t myfirst_driver = {
	"myfirst",
	myfirst_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
```

O arquivo tem um único trabalho: conectar as partes do driver no nível do kernel. Tem algumas centenas de linhas; todos os outros arquivos do driver são menores.

### `myfirst_cdev.c`: Os Callbacks do Dispositivo de Caracteres

Os callbacks de open, close, read e write foram o primeiro código que escrevemos lá no Capítulo 18. Eles cresceram desde então. Extraí-los para `myfirst_cdev.c` os mantém juntos e fora de `myfirst.c`:

```c
/*
 * myfirst_cdev.c - character-device callbacks for the myfirst driver.
 *
 * The open/close/read/write callbacks all operate on the softc that
 * make_dev_s installed as si_drv1.  The ioctl dispatch is in
 * myfirst_ioctl.c; this file intentionally does not handle ioctls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	mtx_lock(&sc->sc_mtx);
	sc->sc_open_count++;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN, "open: count %u\n", sc->sc_open_count);
	return (0);
}

/* close, read, write follow the same pattern. */
```

Cada callback começa com `sc = dev->si_drv1` (o ponteiro por cdev que `make_dev_args` definiu) e opera sobre o softc. Nenhum acoplamento entre arquivos além do header público.

### `myfirst_ioctl.c`: O Switch de Comandos

O `myfirst_ioctl.c` está em seu próprio arquivo desde o Capítulo 22. A adição do Capítulo 25 é o handler de `MYFIRSTIOC_GETCAPS`:

```c
int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	int error = 0;

	switch (cmd) {
	case MYFIRSTIOC_GETVER:
		*(int *)data = MYFIRST_IOCTL_VERSION;
		break;
	case MYFIRSTIOC_RESET:
		mtx_lock(&sc->sc_mtx);
		sc->sc_total_reads  = 0;
		sc->sc_total_writes = 0;
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_GETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_SETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
		sc->sc_msglen = strlen(sc->sc_msg);
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_GETCAPS:
		*(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
		                    MYF_CAP_SETMSG;
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}
```

O switch é toda a superfície pública de ioctl do driver. Adicionar um comando significa adicionar um case; aposentar um comando significa deletar um case e deprecar a constante em `myfirst_ioctl.h`.

### `myfirst_log.h` e `myfirst_log.c`: Logging com Limitação de Taxa

A Seção 1 introduziu a macro de logging com limitação de taxa `DLOG_RL` e o estado `struct myfirst_ratelimit` que ela rastreia. O estado de limitação de taxa foi deixado embutido no softc na Seção 1 porque a abstração ainda não havia sido extraída. A Seção 6 é o momento certo para extraí-la: o código de limitação de taxa é pequeno o suficiente para valer a pena concentrá-lo em um lugar e genérico o suficiente para que outros drivers possam querer usá-lo.

`myfirst_log.h` contém a definição do estado:

```c
#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define MYF_RL_DEFAULT_PPS  10

#endif /* _MYFIRST_LOG_H_ */
```

`myfirst_log.c` contém os helpers de attach e detach:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

int
myfirst_log_attach(struct myfirst_softc *sc)
{
	/*
	 * The rate-limit state is embedded by value in the softc, so
	 * there is no allocation to do.  This function exists so that
	 * the attach chain has a named label for logging in case a
	 * future version needs per-class allocations.
	 */
	bzero(&sc->sc_rl_generic, sizeof(sc->sc_rl_generic));
	bzero(&sc->sc_rl_io,      sizeof(sc->sc_rl_io));
	bzero(&sc->sc_rl_intr,    sizeof(sc->sc_rl_intr));

	return (0);
}

void
myfirst_log_detach(struct myfirst_softc *sc)
{
	/* Nothing to release; the state is embedded in the softc. */
	(void)sc;
}
```

Hoje, `myfirst_log_attach` não faz nenhuma alocação; ela zera os campos de limitação de taxa e retorna. Amanhã, se o driver precisar de um array dinâmico de contadores por classe, a alocação cabe aqui e a cadeia de attach não precisará mudar. Esse é o valor de extrair o helper antes que seja estritamente necessário: a estrutura está pronta para crescer.

O tamanho do arquivo header importa aqui. `myfirst_log.h` tem menos de 20 linhas. Um header de 20 linhas é barato para incluir em todo lugar, barato para ler e barato para manter sincronizado. Se `myfirst_log.h` crescesse para 200 linhas, o custo de incluí-lo em todos os arquivos `.c` começaria a aparecer nos tempos de compilação e na fricção de revisão; nesse ponto, o próximo passo seria dividi-lo novamente.

### O Makefile Atualizado

O Makefile do driver dividido lista todos os arquivos `.c`:

```makefile
# Makefile for the myfirst driver - Chapter 25 (1.8-maintenance).
#
# Chapter 25 splits the driver into subject-matter files.  Each file
# answers a single question; the Makefile lists them in alphabetical
# order after myfirst.c (which carries the module glue) so the
# reader sees the main file first.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_debug.c myfirst_ioctl.c \
	myfirst_log.c myfirst_sysctl.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
```

`SRCS` lista seis arquivos `.c`, um por assunto. Adicionar um sétimo é uma mudança de uma linha. O sistema de build do kernel processa automaticamente todos os arquivos em `SRCS`; não há etapa de linking manual nem árvore de dependências do Makefile para manter.

### Onde Traçar Cada Fronteira entre Arquivos

A parte mais difícil de dividir um driver não é decidir dividir; é decidir onde vão as fronteiras. A maioria das divisões passa por três fases, e essas fases se aplicam a qualquer driver, não apenas ao `myfirst`.

**A fase um** é o arquivo plano. Tudo está em `driver.c`. Essa é a estrutura certa para as primeiras 300 linhas de um driver. Dividir mais cedo gera mais fricção do que economiza.

**A fase dois** é a divisão por assunto. O dispatch de ioctl vai para `driver_ioctl.c`, a árvore de sysctl vai para `driver_sysctl.c`, a infraestrutura de debug vai para `driver_debug.c`. Cada arquivo tem o nome do assunto que trata. É aqui que o `myfirst` está desde o Capítulo 24.

**A fase três** é a divisão por subsistema. À medida que o driver cresce, um assunto cresce além do que um único arquivo comporta. O arquivo de ioctl se divide em `driver_ioctl.c` (o dispatch) e `driver_ioctl_rw.c` (os helpers de payload de leitura/escrita). O arquivo de sysctl se divide de forma similar. É aqui que um driver completo acaba chegando, frequentemente em sua terceira ou quarta versão principal.

`myfirst` ao final do Capítulo 25 está solidamente na fase dois. A fase três ainda não é necessária, e o Capítulo 26 vai reiniciar o ciclo quando dividir o pseudo-driver em uma variante conectada via USB e deixar `myfirst_core.c` como o núcleo agnóstico em relação ao assunto. Não há valor em antecipar essa divisão para a fase três hoje.

A regra prática para determinar quando passar da fase dois para a fase três é: quando um único arquivo de assunto ultrapassar 1.000 linhas, ou quando duas alterações não relacionadas no mesmo arquivo de assunto causarem um conflito de merge, o assunto está pronto para ser dividido.

### O Grafo de Inclusão e a Ordem de Build

Quando um driver é dividido em vários arquivos, o grafo de inclusão passa a importar. Uma inclusão circular não é um erro grave em C, mas é sinal de uma estrutura de dependências confusa, que vai desorientar quem ler o código. A forma correta é um grafo acíclico dirigido de headers, com raiz em `myfirst.h` e folhas como `myfirst_ioctl.h` e `myfirst_log.h`.

`myfirst.h` é o header mais abrangente. Ele declara o softc e os protótipos que todos os outros arquivos utilizam. Inclui `myfirst_log.h` porque o softc possui campos de rate-limit por valor.

`myfirst_debug.h` é uma folha. Ele declara a família de macros `DPRINTF` e os bits de classe. É incluído por todos os arquivos `.c`, direta ou indiretamente. Não é incluído por `myfirst.h`, pois `myfirst.h` não deve impor as macros de debug a nenhum chamador que não precise delas.

`myfirst_ioctl.h` é uma folha. Ele declara os números de comando, as estruturas de payload e o inteiro de versão do formato de protocolo. É incluído por `myfirst_ioctl.c` (e pelo seu correspondente em espaço do usuário, `myfirstctl.c`).

Nenhum header inclui outro arquivo além dos headers públicos do kernel e dos próprios headers do driver. Nenhum arquivo `.c` inclui outro arquivo `.c`. O grafo de inclusão é raso e fácil de diagramar.

### O Custo da Divisão

Dividir arquivos tem um custo real. Cada divisão acrescenta um header, e cada header precisa ser mantido. Uma função cuja assinatura muda precisa ser atualizada no `.c` e no `.h`, e a mudança precisa se propagar para todos os outros arquivos `.c` que incluem esse header. Um driver com doze arquivos é ligeiramente mais lento de compilar do que um driver com um único arquivo, porque cada `.c` precisa incluir vários headers e o pré-processador tem que analisá-los todos.

Esses custos são reais, mas pequenos. São muito menores do que o custo de um arquivo monolítico que ninguém quer tocar. A regra é dividir quando o custo de não dividir supera o custo de manter a fronteira. Para o `myfirst` ao final do Capítulo 25, esse limiar foi ultrapassado.

### Um Procedimento Prático para Dividir um Driver Real

Dividir um arquivo é uma refatoração de rotina, mas até uma refatoração de rotina pode introduzir bugs se feita descuidadamente. Um procedimento prático para dividir um driver dentro da árvore de código é:

1. **Identifique os assuntos.** Leia o arquivo monolítico do início ao fim e agrupe suas funções por assunto (cdev, ioctl, sysctl, debug, ciclo de vida). Anote o agrupamento num papel ou num bloco de comentários.

2. **Crie os arquivos vazios.** Adicione os novos arquivos `.c` e seus headers à árvore de código-fonte. Compile uma vez para garantir que o sistema de build os enxerga.

3. **Mova um assunto de cada vez.** Mova as funções de ioctl para `driver_ioctl.c`. Mova suas declarações para `driver_ioctl.h`. Atualize `driver.c` para `#include "driver_ioctl.h"`. Compile. Execute o driver pela sua matriz de testes.

4. **Faça commit de cada divisão de assunto.** Cada movimentação de assunto é um único commit. O log do commit deve dizer algo como: "myfirst: split ioctl dispatch into driver_ioctl.c". Um revisor consegue ver a movimentação com clareza; um `git blame` mostra a mesma linha no novo arquivo no mesmo commit.

5. **Verifique o grafo de inclusão.** Após mover todos os assuntos, compile com `-Wunused-variable` e `-Wmissing-prototypes` para detectar funções que deveriam ter protótipos mas não têm. Use `nm` no módulo compilado para confirmar que nenhum símbolo que deveria ser `static` está sendo exportado.

6. **Teste novamente.** Execute a matriz de testes completa do driver. Dividir um arquivo não deve alterar o comportamento; se um teste começar a falhar, a divisão introduziu um bug.

O procedimento para o `myfirst` ao final do Capítulo 25 segue exatamente esses passos. O diretório final em `examples/part-05/ch25-advanced/` é o resultado.

### Erros Comuns ao Dividir

Alguns erros são comuns na primeira vez que um driver é dividido. Estar atento a eles encurta a curva de aprendizado.

O primeiro erro é **colocar declarações no header errado**. Se `myfirst.h` declara uma função que só é chamada por `myfirst_ioctl.c`, todas as outras unidades de tradução pagam o custo de analisar uma declaração de que não precisam. Se `myfirst_ioctl.h` declara uma função chamada tanto por `myfirst_ioctl.c` quanto por `myfirst_cdev.c`, os dois consumidores ficam acoplados pelo header de ioctl e qualquer mudança nesse header recompila ambos os arquivos. A correção é colocar declarações transversais em `myfirst.h` e declarações específicas de assunto nos headers específicos de assunto.

O segundo erro é **esquecer `static` em funções que deveriam ter escopo de arquivo**. Uma função usada apenas dentro de `myfirst_sysctl.c` deve ser declarada `static`. Sem `static`, a função é exportada do arquivo objeto, o que significa que outro arquivo poderia chamá-la acidentalmente, e qualquer renomeação futura no arquivo original passa a ser uma mudança de ABI. A disciplina de usar `static` previne toda essa classe de problema.

O terceiro erro são as **inclusões circulares**. Se `myfirst_ioctl.h` inclui `myfirst.h` e `myfirst.h` inclui `myfirst_ioctl.h`, o driver compila (graças aos include guards), mas o grafo de dependências está errado. Toda edição em qualquer um dos dois arquivos dispara a recompilação de tudo que inclui algum deles. A correção é decidir qual header fica mais alto no grafo e remover a referência inversa.

O quarto erro é **reintroduzir um assunto no arquivo errado**. Seis meses depois da divisão, alguém adiciona um novo ioctl editando `myfirst.c` porque era lá que os ioctls ficavam antes. A regra de responsabilidade única precisa ser aplicada pelos revisores. Um patch que coloca um novo ioctl em `myfirst.c` é rejeitado com um comentário apontando para `myfirst_ioctl.c`.

### Encerrando a Seção 6

Um driver cujos arquivos cada um responde a uma única pergunta é um driver que você pode entregar a um novo colaborador no primeiro dia. Ele lê os nomes dos arquivos, escolhe um assunto e começa a editar exatamente um arquivo. O driver `myfirst` cruzou esse limiar. Seis arquivos `.c` mais seus headers guardam todas as funções que o driver acumulou desde o Capítulo 17, com cada arquivo nomeado pelo que faz.

Na próxima seção, saímos da organização interna e passamos à preparação externa. Um driver pronto para produção tem uma lista curta de propriedades que precisa satisfazer antes de ser instalado em uma máquina que o desenvolvedor não possui. O checklist de prontidão para produção do Capítulo 25 nomeia essas propriedades e conduz o `myfirst` por cada uma delas.

## Seção 7: Preparando para Uso em Produção

Um driver que funciona na sua estação de trabalho não é um driver pronto para produção. Produção é o conjunto de condições que o seu código enfrenta quando instalado em hardware que você não possui, inicializado por operadores que você nunca vai conhecer, e esperado para se comportar de forma previsível por meses ou anos entre reinicializações. A distância entre "funciona pra mim" e "está pronto para distribuir" é medida em hábitos, não em funcionalidades. Esta seção nomeia esses hábitos.

O `myfirst` na sua forma do Capítulo 25 está tão completo em funcionalidades quanto o pseudo-driver vai chegar a ser. O trabalho restante não é adicionar funcionalidades, mas endurecer as bordas para que o driver sobreviva aos ambientes que não consegue controlar.

### A Mentalidade de Prontidão para Produção

A mudança de mentalidade é esta: toda decisão que o driver toma implicitamente no tempo de desenvolvimento precisa ser tomada explicitamente no tempo de produção. Quando um tunable tem um valor padrão, esse padrão precisa ser o padrão correto. Quando um sysctl é gravável, as consequências de uma escrita feita por um operador apavorado às 3 da manhã precisam ser seguras. Quando uma mensagem de log pode disparar, a mensagem precisa ser útil sem a ajuda do desenvolvedor. Quando um módulo depende de outro, a dependência precisa ser declarada para que o loader não os carregue na ordem errada.

Prontidão para produção não é uma ação pontual; é uma atitude que permeia cada decisão. Um driver quase pronto para produção costuma ter uma ou duas lacunas específicas: um tunable sem documentação, uma mensagem de log que dispara a cada microssegundo, um caminho de detach que assume que ninguém está usando o dispositivo. A disciplina de prontidão para produção é encontrar essas lacunas específicas e fechá-las, uma a uma, até que o comportamento do driver seja previsível em uma máquina na qual o desenvolvedor não está presente.

### Declarando Dependências de Módulo

O primeiro hábito de produção é ser explícito sobre o que o módulo precisa. Se `myfirst` chama uma função que reside em outro módulo do kernel, o loader de módulos do kernel precisa conhecer a dependência antes da chamada, caso contrário o kernel carrega `myfirst` e entra em panic na primeira vez que a dependência é usada.

O mecanismo é `MODULE_DEPEND`. A Seção 4 o apresentou como uma ferramenta de compatibilidade; em produção, ele também é uma ferramenta de correção. Um driver sem `MODULE_DEPEND` para suas dependências reais funciona por acidente na maioria das ordens de boot e falha misteriosamente em outras. Um driver com `MODULE_DEPEND` para cada dependência real ou carrega corretamente ou recusa o carregamento com uma mensagem de erro clara.

Para o pseudo-driver `myfirst`, ainda não há dependências reais; o driver usa apenas símbolos do núcleo do kernel, que está sempre presente. A variante USB do Capítulo 26 adicionará o primeiro `MODULE_DEPEND` real:

```c
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
```

Os três números de versão são o mínimo, o preferido e o máximo da versão da pilha USB com a qual `myfirst_usb` é compatível. No momento do carregamento, o kernel verifica a versão instalada da pilha USB em relação a esse intervalo e recusa o carregamento de `myfirst_usb` se a pilha USB estiver ausente ou fora do intervalo.

O hábito de produção é: antes de distribuir, faça grep no driver por cada símbolo que ele chama e confirme que cada símbolo reside no núcleo do kernel ou em um módulo para o qual o driver declara dependência. Um `MODULE_DEPEND` faltando funciona até a ordem de boot mudar, e então o driver entra em panic no hardware de produção.

### Publicando Informações PNP

Para drivers de hardware, o loader de módulos do kernel consulta os metadados PNP de cada módulo para decidir qual driver cuida de qual dispositivo. Um driver USB que não publica informações PNP funciona quando carregado manualmente e falha quando o bootloader tenta carregá-lo automaticamente para um dispositivo recém-conectado. A correção é `MODULE_PNP_INFO`, que o driver usa para declarar os identificadores de fabricante e produto que ele suporta:

```c
MODULE_PNP_INFO("U16:vendor;U16:product", uhub, myfirst_usb,
    myfirst_pnp_table, nitems(myfirst_pnp_table));
```

A primeira string descreve o formato das entradas da tabela PNP. `uhub` é o nome do barramento; `myfirst_usb` é o nome do driver; `myfirst_pnp_table` é um array estático de estruturas, uma por dispositivo que o driver suporta.

O `myfirst` no Capítulo 25 ainda é um pseudo-driver e não tem hardware para corresponder. `MODULE_PNP_INFO` entra em cena no Capítulo 26 com o primeiro attach a hardware real. Para o Capítulo 25, o hábito de produção é simplesmente saber que a macro existe e planejar para usá-la quando o hardware chegar.

### O Evento `MOD_QUIESCE`

O handler de eventos do módulo do kernel é chamado com um de quatro eventos: `MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`, `MOD_QUIESCE`. A maioria dos drivers trata `MOD_LOAD` e `MOD_UNLOAD` explicitamente, e o kernel sintetiza os outros dois. Para drivers de produção, `MOD_QUIESCE` merece atenção.

`MOD_QUIESCE` é a pergunta do kernel "você pode ser descarregado agora?" Ele dispara antes de `MOD_UNLOAD` e dá ao driver a chance de recusar de forma limpa. Um driver que está no meio de uma operação (uma transferência DMA em andamento, um descritor de arquivo aberto, um timer pendente) pode retornar um errno diferente de zero em `MOD_QUIESCE` para recusar o descarregamento; o kernel então não prossegue para `MOD_UNLOAD`.

Para o `myfirst`, a verificação de quiesce já está embutida em `myfirst_detach`: se `sc_open_count > 0`, o detach retorna `EBUSY`. O loader de módulos do kernel propaga esse `EBUSY` de volta ao `kldunload(8)`, e o operador vê "module myfirst is busy". A verificação está no lugar certo, mas vale a pena nomear a disciplina de pensar em `MOD_QUIESCE` separadamente de `MOD_UNLOAD`: `MOD_QUIESCE` é a pergunta "você é seguro para descarregar?" e `MOD_UNLOAD` é o comando "vá em frente e descarregue". Alguns drivers têm estado que é seguro verificar em `MOD_QUIESCE` mas não é seguro adquirir em `MOD_UNLOAD`; separá-los permite ao driver responder à pergunta sem efeitos colaterais.

### Emitindo Eventos `devctl_notify`

Sistemas de produção em execução contínua são monitorados por daemons como `devd(8)`, que observam chegadas, saídas e mudanças de estado de dispositivos. O mecanismo que o kernel usa para notificar o `devd` é o `devctl_notify(9)`: um driver emite um evento estruturado, o `devd` lê esse evento e executa uma ação configurada (rodar um script, registrar uma mensagem, notificar um operador).

O protótipo é:

```c
void devctl_notify(const char *system, const char *subsystem,
    const char *type, const char *data);
```

- `system` é uma categoria de nível superior como `"DEVFS"`, `"ACPI"`, ou uma tag específica do driver.
- `subsystem` é o nome do driver ou do subsistema.
- `type` é um nome curto para o evento.
- `data` é um dado estruturado opcional (pares chave=valor) para o daemon analisar.

Para o `myfirst`, um evento de produção útil é "a mensagem interna do driver foi reescrita":

```c
devctl_notify("myfirst", device_get_nameunit(sc->sc_dev),
    "MSG_CHANGED", NULL);
```

Depois que o operador escreve uma nova mensagem via `ioctl(fd, MYFIRSTIOC_SETMSG, buf)`, o driver emite um evento `MSG_CHANGED`. Uma regra do `devd` pode capturar esse evento e, por exemplo, enviar uma entrada de syslog ou notificar um daemon de monitoramento:

```text
notify 0 {
    match "system"    "myfirst";
    match "type"      "MSG_CHANGED";
    action "logger -t myfirst 'message changed on $subsystem'";
};
```

O bom hábito em produção é perguntar, para cada evento relevante no driver, se um operador poderia querer reagir a ele. Se a resposta for sim, emita um `devctl_notify` com um nome bem escolhido. As ferramentas downstream podem então se basear nesse evento, e o driver não precisa saber quais são essas ferramentas.

### Escrevendo um `MAINTENANCE.md`

Todo driver de produção deve ter um arquivo de manutenção que descreve, em linguagem simples, o que o driver faz, quais tunables ele aceita, quais sysctls ele expõe, quais ioctls ele trata, quais eventos ele emite e qual é o histórico de versões. O arquivo fica junto ao código-fonte no repositório e é lido por operadores, por novos desenvolvedores, por revisores de segurança e pelo próprio autor seis meses depois.

Um esqueleto concreto para `MAINTENANCE.md`:

```text
# myfirst

A demonstration character driver that carries the book's running
example.  This file is the operator-facing reference.

## Overview

myfirst registers a pseudo-device at /dev/myfirst0 and serves a
read-write message buffer, a set of ioctls, a sysctl tree, and a
configurable debug-class logger.

## Tunables

- hw.myfirst.debug_mask_default (int, default 0)
    Initial value of dev.myfirst.<unit>.debug.mask.
- hw.myfirst.timeout_sec (int, default 5)
    Initial value of dev.myfirst.<unit>.timeout_sec.
- hw.myfirst.max_retries (int, default 3)
    Initial value of dev.myfirst.<unit>.max_retries.
- hw.myfirst.log_ratelimit_pps (int, default 10)
    Initial rate-limit ceiling (prints per second per class).

## Sysctls

All sysctls live under dev.myfirst.<unit>.

Read-only: version, open_count, total_reads, total_writes,
message, message_len.

Read-write: debug.mask (mirror of debug_mask_default), timeout_sec,
max_retries, log_ratelimit_pps.

## Ioctls

Defined in myfirst_ioctl.h.  Command magic 'M'.

- MYFIRSTIOC_GETVER (0): returns MYFIRST_IOCTL_VERSION.
- MYFIRSTIOC_RESET  (1): zeros read/write counters.
- MYFIRSTIOC_GETMSG (2): reads the in-driver message.
- MYFIRSTIOC_SETMSG (3): writes the in-driver message.
- MYFIRSTIOC_GETCAPS (5): returns MYF_CAP_* bitmask.

Command 4 was reserved during Chapter 23 draft work and retired
before release.  Do not reuse the number.

## Events

Emitted through devctl_notify(9).

- system=myfirst subsystem=<unit> type=MSG_CHANGED
    The operator-visible message was rewritten.

## Version History

See Change Log below.

## Change Log

### 1.8-maintenance
- Added MYFIRSTIOC_GETCAPS (command 5).
- Added tunables for timeout_sec, max_retries, log_ratelimit_pps.
- Added rate-limited logging via ppsratecheck(9).
- Added devctl_notify for MSG_CHANGED.
- No breaking changes from 1.7.

### 1.7-integration
- First end-to-end integration of ioctl, sysctl, debug.
- Introduced MYFIRSTIOC_{GETVER,RESET,GETMSG,SETMSG}.

### 1.6-debug
- Added DPRINTF framework and SDT probes.
```

O arquivo não tem nada de glamoroso. É uma referência mantida atualizada a cada incremento de versão e que serve como fonte única da verdade para os operadores.

O hábito de produção é: toda mudança na superfície visível do driver (um novo tunable, um novo sysctl, um novo ioctl, um novo evento, uma mudança de comportamento) tem uma entrada correspondente em `MAINTENANCE.md`. O arquivo nunca fica desatualizado em relação ao código. Um driver cujo `MAINTENANCE.md` está desatualizado é um driver cujos usuários ficam no escuro. Um driver cujo `MAINTENANCE.md` está em dia é um driver cujos usuários conseguem se virar sozinhos.

### Um Conjunto de Regras para o `devd`

As regras do `devd(8)` dizem ao daemon como reagir a eventos do kernel. Para uma implantação em produção do `myfirst`, um conjunto mínimo de regras garantiria que eventos importantes cheguem ao operador:

```console
# /etc/devd/myfirst.conf
#
# devd rules for the myfirst driver.  Drop this file into
# /etc/devd/ and restart devd(8) for the rules to take effect.

notify 0 {
    match "system"    "myfirst";
    match "type"      "MSG_CHANGED";
    action "logger -t myfirst 'message changed on $subsystem'";
};

# Future: match attach/detach events once Chapter 26's USB variant
# starts emitting them.
```

O arquivo é curto. Ele declara uma regra, corresponde a um evento específico e executa uma ação específica. Em produção, esses arquivos crescem para corresponder a mais eventos, disparar mais ações e, em algumas implantações, notificar um sistema de monitoramento que observa anomalias no driver.

Incluir um rascunho de `devd.conf` no repositório do driver facilita a adoção pelo operador. Eles copiam o arquivo, ajustam as ações e os eventos do driver ficam integrados ao monitoramento do site no primeiro dia.

### Logs: O Aliado do Engenheiro de Suporte

As mensagens de log de um driver de produção são lidas por engenheiros de suporte que não têm acesso ao código-fonte e não conseguem reproduzir o problema sob demanda. As regras que tornam uma mensagem de log útil para um engenheiro de suporte são diferentes das regras que a tornam útil para um desenvolvedor.

Um desenvolvedor que lê sua própria mensagem de log pode contar com um contexto que o engenheiro de suporte não tem. O engenheiro de suporte não pode perguntar "qual attach?" ou "qual dispositivo?" ou "qual era o valor de `error` quando isso ocorreu?". A resposta já precisa estar na mensagem.

O hábito de produção é auditar cada mensagem de log no driver e fazer três perguntas:

1. **A mensagem identifica seu dispositivo?** `device_printf(dev, ...)` prefixa a saída com o nameunit do dispositivo. `printf` simples não faz isso. Toda mensagem que não vem de `MOD_LOAD` (onde ainda não há dispositivo) deve usar `device_printf`.

2. **A mensagem inclui o contexto numérico relevante?** "Failed to allocate" não é útil. "Failed to allocate: error 12 (ENOMEM)" já é. "Failed to allocate a timer: error 12" é ainda melhor.

3. **A mensagem aparece na frequência adequada?** A seção 1 abordou o rate-limiting. A passagem final consiste em garantir que toda mensagem que pode ser disparada em um loop seja limitada em taxa ou seja demonstravelmente chamada uma única vez.

Uma mensagem de log que satisfaz essas três perguntas chega ao engenheiro de suporte com informação suficiente para registrar um relatório de bug útil. Uma mensagem que falha em qualquer uma delas desperdiça o tempo do operador e do desenvolvedor.

### Lidando com Ciclos de Attach e Detach de Forma Adequada

Drivers de produção, especialmente os que suportam hot-plug, precisam lidar com ciclos repetidos de attach e detach sem vazamentos. A disciplina do padrão de limpeza com rótulos da seção 5 é parte da resposta. A outra parte é confirmar que o attach/detach repetido funciona de fato. O laboratório ao final deste capítulo percorre um script de regressão que carrega, descarrega e recarrega o driver 100 vezes em sequência e verifica que o consumo de memória do módulo não cresce.

Um driver que passa no teste de 100 ciclos é um driver que sobreviverá a um mês de eventos de hot-plug em hardware de produção. Um driver que falha no teste de 100 ciclos tem um vazamento que se manifestará, com o tempo, como crescimento lento de memória ou como o kernel ficando sem algum recurso limitado (OIDs de sysctl, números minor de cdev, entradas de devclass).

O teste é simples de executar e tem valor desproporcional. Faça dele parte da checklist de pré-lançamento do driver.

### Lidando com Ações Inesperadas do Operador

Operadores cometem erros. Eles executam `kldunload myfirst` enquanto um programa de teste está lendo de `/dev/myfirst0`. Eles definem `dev.myfirst.0.debug.mask` para um valor que habilita todas as classes de uma vez. Eles copiam o `MAINTENANCE.md` e ignoram a seção sobre tunables. O driver de produção precisa tolerar essas ações sem travar, corromper estado ou deixar o sistema em uma configuração quebrada.

Para cada interface exposta, o hábito de produção é perguntar: qual é a pior sequência de ações de operador que consigo imaginar, e o driver sobrevive a ela?

- `kldunload` com um descritor de arquivo aberto: `myfirst_detach` retorna `EBUSY`. O operador vê "module busy". O driver permanece inalterado.
- Um sysctl gravável sendo definido para um valor fora do intervalo: o handler do sysctl limita o valor ou retorna `EINVAL`. O estado interno do driver permanece inalterado.
- Um `MYFIRSTIOC_SETMSG` com uma mensagem mais longa que o buffer: `strlcpy` trunca. A cópia está correta. O truncamento é visível em `message_len`.
- Um par concorrente de chamadas `MYFIRSTIOC_SETMSG`: o mutex do softc as serializa. A que executar por último vence. Ambas têm sucesso.

Se qualquer uma dessas ações produzir um crash, uma corrupção ou um estado inconsistente, o driver não está pronto para produção. A correção é sempre a mesma: adicionar o guarda ausente, reiniciar o teste e adicionar um comentário documentando o invariante.

### Uma Checklist de Prontidão para Produção

Os hábitos desta seção cabem em uma checklist curta que um desenvolvedor pode percorrer antes do lançamento:

```text
myfirst production readiness
----------------------------

[  ] MODULE_DEPEND declared for every real dependency.
[  ] MODULE_PNP_INFO declared if the driver binds to hardware.
[  ] MOD_QUIESCE answers "can you unload?" without side effects.
[  ] devctl_notify emitted for operator-relevant events.
[  ] MAINTENANCE.md current: tunables, sysctls, ioctls, events.
[  ] devd.conf snippet included with the driver.
[  ] Every log message is device_printf, includes errno,
     and is rate-limited if it can fire in a loop.
[  ] attach/detach survives 100 load/unload cycles.
[  ] sysctls reject out-of-range values.
[  ] ioctl payload is bounds-checked.
[  ] Failure paths exercised via deliberate injection.
[  ] Versioning discipline: MYFIRST_VERSION, MODULE_VERSION,
     MYFIRST_IOCTL_VERSION each bumped for their own reason.
```

A lista é curta propositalmente. Doze itens, a maioria deles já tratada pelos hábitos introduzidos nas seções anteriores. Um driver que marca todas as caixas está pronto para ser instalado por alguém que jamais vai conhecer você.

### O Que o Driver `myfirst` Cobre

Executar o `myfirst` pela checklist ao final do capítulo 25 fornece o seguinte status.

`MODULE_DEPEND` não é necessário porque o driver não tem dependências de subsistema. Isso está anotado explicitamente no `MAINTENANCE.md`.

`MODULE_PNP_INFO` não é necessário porque o driver não se vincula a hardware. Isso também está anotado no `MAINTENANCE.md`.

`MOD_QUIESCE` é atendido pela verificação de `sc_open_count` em `myfirst_detach`. Um handler dedicado para `MOD_QUIESCE` não é adicionado nesta versão porque a semântica é idêntica.

`devctl_notify` é emitido em `MYFIRSTIOC_SETMSG` com o tipo de evento `MSG_CHANGED`.

`MAINTENANCE.md` é entregue no diretório de exemplos e contém tunables, sysctls, ioctls, eventos e uma entrada no Change Log para 1.8-maintenance.

O trecho de `devd.conf` é entregue junto com `MAINTENANCE.md` e demonstra a regra única de `MSG_CHANGED`.

Toda mensagem de log é emitida via `device_printf` (ou `DPRINTF`, que envolve `device_printf`). Toda mensagem que dispara em um caminho crítico está envolvida em `DLOG_RL`.

O script de regressão de attach/detach (veja os Laboratórios) executa 100 ciclos sem aumentar o consumo de memória do kernel.

Os sysctls para `timeout_sec`, `max_retries` e `log_ratelimit_pps` rejeitam valores fora do intervalo em seus handlers.

Os payloads dos ioctls são verificados quanto aos limites no nível da estrutura pelo framework de ioctl do kernel (`_IOR`, `_IOW`, `_IOWR` declaram tamanhos exatos) e dentro do driver onde o comprimento da string importa.

Os pontos de injeção de falhas são marcados por `#ifdef` condicional nos exemplos. Todo rótulo foi alcançado pelo menos uma vez durante o desenvolvimento.

Os identificadores de versão seguem cada um sua própria regra: string incrementada, inteiro do módulo incrementado, inteiro de ioctl inalterado porque adições são compatíveis com versões anteriores.

Doze verificações, doze resultados. O driver está pronto para o próximo capítulo.

### Encerrando a Seção 7

Produção é o padrão silencioso que separa código interessante de código entregável. As disciplinas listadas aqui não são glamorosas. São as coisas específicas que mantêm um driver funcionando quando ele é implantado longe do desenvolvedor que o escreveu. `myfirst` cresceu ao longo de cinco capítulos de conteúdo instrucional e agora carrega o conjunto de práticas que o deixa sobreviver fora do livro.

Na próxima seção, voltamos nossa atenção para as duas infraestruturas do kernel que permitem a um driver executar código em pontos específicos do ciclo de vida sem fiação manual: `SYSINIT(9)` para inicialização no momento do boot e `EVENTHANDLER(9)` para notificações em tempo de execução. Estas são as duas últimas peças do kit de ferramentas do FreeBSD que o livro apresentará antes que o capítulo 26 aplique tudo a um barramento real.

## Seção 8: SYSINIT, SYSUNINIT e EVENTHANDLER

As rotinas attach e detach de um driver tratam tudo que acontece entre a instanciação e o desmonte, mas há coisas que um driver pode precisar fazer que ficam fora dessa janela. Algum código precisa rodar antes de qualquer dispositivo ser instanciado: carregar tunables do boot, inicializar um lock de todo o subsistema, configurar um pool que o primeiro `attach` consumirá. Outro código precisa rodar em resposta a eventos de todo o sistema que não são específicos de dispositivo: um suspend de todo o sistema, uma condição de memória baixa, um desligamento que está prestes a sincronizar os sistemas de arquivos e encerrar.

O kernel do FreeBSD fornece dois mecanismos para esses casos. `SYSINIT(9)` registra uma função para ser executada em um estágio específico do boot, e seu companheiro `SYSUNINIT(9)` registra uma função de limpeza para ser executada no descarregamento do módulo. `EVENTHANDLER(9)` registra um callback para ser executado sempre que o kernel dispara um evento nomeado.

Ambos os mecanismos estão disponíveis desde as primeiras versões do FreeBSD. São infraestrutura sem glamour. Esse é o seu valor. Um driver que os usa corretamente consegue reagir ao ciclo de vida completo do kernel sem escrever uma única linha de código de registro manual. Um driver que os ignora ou perde seu momento ou reinventa uma versão pior do mesmo mecanismo.

### Por Que o Kernel Precisa de Ordenação no Boot

O kernel do FreeBSD inicializa em uma ordem precisa. O gerenciamento de memória sobe antes de qualquer alocador estar disponível para uso. Os tunables são analisados antes que os drivers possam lê-los. Os locks são inicializados antes que qualquer coisa possa adquiri-los. Os sistemas de arquivos são montados apenas após os dispositivos em que residem serem sondados. Cada uma dessas dependências precisa ser respeitada, ou o kernel entra em pânico antes que `init(8)` inicie.

O mecanismo que impõe a ordenação é `SYSINIT(9)`. Uma macro `SYSINIT` declara que uma determinada função deve ser executada em um determinado ID de subsistema com uma determinada constante de ordem. A sequência de boot do kernel coleta todos os `SYSINIT` na configuração em execução, os ordena por (subsistema, ordem) e os chama nessa sequência. Módulos carregados após o kernel ter inicializado ainda respeitam suas declarações `SYSINIT`: o loader os chama no momento do attach do módulo, na mesma ordem classificada.

Do ponto de vista do driver, um `SYSINIT` é uma forma de dizer "faça isso naquele ponto da sequência de boot, e não me importa qual outro código também está se registrando naquele ponto". O kernel cuida da ordenação. O driver escreve o callback.

### O Espaço de IDs de Subsistema

Os IDs de subsistema são definidos em `/usr/src/sys/sys/kernel.h`. As constantes têm nomes descritivos e valores numéricos que refletem sua ordenação. Um driver escolhe o subsistema que corresponde à finalidade de seu callback:

- `SI_SUB_TUNABLES` (0x0700000): avalia os tunables do boot. É aqui que `TUNABLE_INT_FETCH` e seus equivalentes são executados. O código que consome tunables precisa rodar após este ponto.
- `SI_SUB_KLD` (0x2000000): configuração de módulos do kernel carregáveis. A infraestrutura inicial de módulos é executada aqui.
- `SI_SUB_SMP` (0x2900000): ativa os processadores de aplicação.
- `SI_SUB_DRIVERS` (0x3100000): permite que os drivers se inicializem. Este é o subsistema em que a maioria dos drivers externos se registra quando precisam de código inicial que rode antes de qualquer dispositivo executar seu attach.
- `SI_SUB_CONFIGURE` (0x3800000): configura os dispositivos. Ao final deste subsistema, todo driver compilado no kernel teve a oportunidade de executar seu attach.

Há mais de cem IDs de subsistema em `kernel.h`. Os listados acima são os que um driver de dispositivo de caracteres mais frequentemente utiliza. Os valores numéricos estão ordenados de forma que "número menor" significa "mais cedo no boot".

Dentro de um subsistema, a constante de ordem fornece um controle mais granular sobre a sequência de execução:

- `SI_ORDER_FIRST` (0x0): executa antes da maior parte do código no mesmo subsistema.
- `SI_ORDER_SECOND`, `SI_ORDER_THIRD`: ordenação explícita passo a passo.
- `SI_ORDER_MIDDLE` (0x1000000): executa no meio do processo. A maioria dos `SYSINIT`s no nível de driver usa este valor ou o próximo.
- `SI_ORDER_ANY` (0xfffffff): executa por último. O kernel não garante nenhuma ordem específica entre as entradas que usam `SI_ORDER_ANY`.

O autor do driver escolhe o menor valor de ordem que faça o callback executar após seus pré-requisitos e antes dos módulos que dele dependem. Para a maioria dos casos, `SI_ORDER_MIDDLE` é a escolha adequada.

### Quando um Driver Precisa de `SYSINIT`

A maioria dos drivers de dispositivos de caracteres não precisa de `SYSINIT`. O `DRIVER_MODULE` já registra o driver no Newbus; o método `device_attach` do driver é executado quando um dispositivo compatível aparece. Isso é suficiente para qualquer trabalho que seja por instância.

`SYSINIT` é para trabalho que não é por instância. Alguns motivos pelos quais um driver pode registrar um `SYSINIT`:

- **Inicializar um pool global** do qual cada instância do driver vai consumir. O pool existe uma única vez; ele não pertence a nenhum softc específico.
- **Registrar-se em um subsistema do kernel** que espera que os chamadores se registrem antes de usá-lo. Por exemplo, um driver que deseja receber eventos `vm_lowmem` se registra cedo para que o primeiro evento de pouca memória não o ignore.
- **Processar um tunable complexo** que exige mais trabalho do que um simples `TUNABLE_INT_FETCH`. O código de processamento do tunable é executado durante `SI_SUB_TUNABLES` e preenche uma estrutura global que o código por instância consulta posteriormente.
- **Autoteste** de uma primitiva criptográfica ou de um inicializador de subsistema antes que o primeiro chamador possa utilizá-los.

Para o `myfirst`, nenhum desses casos se aplica por enquanto. O driver é por instância, seus tunables são simples, e ele não usa nenhum subsistema que exija pré-registro. O Capítulo 25 apresenta o `SYSINIT` não porque o `myfirst` precise dele, mas porque o leitor deve conhecer a macro e entender quando uma mudança futura vai exigi-la.

### A Forma de uma Declaração `SYSINIT`

A assinatura da macro é:

```c
SYSINIT(uniquifier, subsystem, order, func, ident);
```

- `uniquifier` é um identificador C que associa o símbolo `SYSINIT` a esta declaração. Ele não aparece em nenhum outro lugar. Por convenção, usa-se um nome curto que corresponda ao subsistema ou à função.
- `subsystem` é a constante `SI_SUB_*`.
- `order` é a constante `SI_ORDER_*`.
- `func` é um ponteiro de função com assinatura `void (*)(void *)`.
- `ident` é um único argumento passado para `func`. Na maioria dos usos, é `NULL`.

A macro de limpeza correspondente é:

```c
SYSUNINIT(uniquifier, subsystem, order, func, ident);
```

`SYSUNINIT` registra uma função de limpeza. Ela é executada no descarregamento do módulo, na ordem inversa das declarações `SYSINIT`. Para código compilado diretamente no kernel (e não como módulo), o `SYSUNINIT` nunca é disparado porque o kernel nunca é descarregado; mesmo assim, a declaração ainda é útil porque compilar o driver como módulo exercita o caminho de limpeza.

### Um Exemplo Prático de `SYSINIT` para `myfirst`

Considere uma melhoria hipotética no `myfirst`: um pool global, de todo o driver, de buffers de log pré-alocados, do qual cada instância pode consumir. O pool é inicializado uma vez a cada carregamento do módulo e destruído uma vez a cada descarregamento. O attach e o detach por instância não manipulam o pool diretamente; eles apenas consomem e devolvem buffers para ele.

A declaração `SYSINIT` tem esta forma:

```c
#include <sys/kernel.h>

static struct myfirst_log_pool {
	struct mtx       lp_mtx;
	/* ... per-pool state ... */
} myfirst_log_pool;

static void
myfirst_log_pool_init(void *unused __unused)
{
	mtx_init(&myfirst_log_pool.lp_mtx, "myfirst log pool",
	    NULL, MTX_DEF);
	/* Allocate pool entries. */
}

static void
myfirst_log_pool_fini(void *unused __unused)
{
	/* Release pool entries. */
	mtx_destroy(&myfirst_log_pool.lp_mtx);
}

SYSINIT(myfirst_log_pool,  SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    myfirst_log_pool_init, NULL);
SYSUNINIT(myfirst_log_pool, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    myfirst_log_pool_fini, NULL);
```

Quando o `myfirst` é carregado, o kernel ordena as entradas `SYSINIT` e chama `myfirst_log_pool_init` durante a fase `SI_SUB_DRIVERS`. O primeiro `myfirst_attach` executado em seguida encontra o pool pronto. Quando o módulo é descarregado, `myfirst_log_pool_fini` é executado depois que todas as instâncias foram desacopladas, dando ao pool a oportunidade de liberar seus recursos.

Isso é um esboço para fins didáticos; o `myfirst` não usa de fato um pool global no código do Capítulo 25 entregue ao leitor. O leitor que eventualmente escrever um driver que precise de um pool global encontrará o padrão aqui.

### A Ordem entre `SYSINIT` e `DRIVER_MODULE`

O `DRIVER_MODULE` é implementado internamente como um `SYSINIT`. Ele registra o driver no Newbus durante uma fase específica do subsistema, e os próprios `SYSINIT`s do Newbus fazem o probe e o attach dos dispositivos em seguida. Um `SYSINIT` personalizado de um driver pode, portanto, ser ordenado em relação ao `DRIVER_MODULE` escolhendo o subsistema e a ordem corretos.

Uma regra geral:

- `SYSINIT` em `SI_SUB_DRIVERS` com `SI_ORDER_FIRST` é executado antes do registro do `DRIVER_MODULE`.
- `SYSINIT` em `SI_SUB_CONFIGURE` com `SI_ORDER_MIDDLE` é executado após a maioria dos attaches de dispositivos, mas antes do passo final de configuração.

Para um pool global do qual o attach depende, `SI_SUB_DRIVERS` com `SI_ORDER_MIDDLE` é geralmente a escolha certa: o pool é inicializado antes que os dispositivos do `DRIVER_MODULE` comecem a fazer attach (pois `SI_SUB_DRIVERS` vem antes de `SI_SUB_CONFIGURE`), e a constante de ordem o mantém afastado dos hooks mais antecipados.

### `EVENTHANDLER`: Reagindo a Eventos em Tempo de Execução

Um `SYSINIT` é disparado uma única vez, em uma fase de boot conhecida. Um `EVENTHANDLER` é disparado zero ou mais vezes, sempre que um evento específico do sistema ocorre. Os dois mecanismos são parentes; eles se complementam.

O kernel define uma série de eventos nomeados. Cada evento tem uma assinatura de callback fixa e um conjunto fixo de circunstâncias em que é disparado. Um driver que se interessa por um evento registra um callback; o kernel invoca o callback toda vez que o evento é disparado; o driver cancela o registro do callback no detach.

Alguns eventos comumente úteis:

- `shutdown_pre_sync`: o sistema está prestes a sincronizar sistemas de arquivos. Drivers com caches na memória os liberam aqui.
- `shutdown_post_sync`: o sistema terminou de sincronizar sistemas de arquivos. Drivers que precisam saber que "o sistema de arquivos está quieto" se registram aqui.
- `shutdown_final`: o sistema está prestes a parar ou reiniciar. Drivers com estado de hardware que deve ser salvo fazem isso aqui.
- `vm_lowmem`: o subsistema de memória virtual está sob pressão. Drivers com caches próprios devem liberar um pouco de memória de volta ao kernel.
- `power_suspend_early`, `power_suspend`, `power_resume`: ciclo de vida de suspensão e retomada.
- `dev_clone`: um evento de clonagem de dispositivo, usado por pseudo-dispositivos que aparecem sob demanda.

A lista não é fixa; novos eventos são adicionados conforme o kernel cresce. Os listados acima são os que um driver genérico mais frequentemente considera.

### A Forma de um Registro de `EVENTHANDLER`

O padrão tem três partes: declarar uma função handler com a assinatura correta, registrá-la no momento do attach e cancelar o registro no momento do detach. O registro retorna uma tag opaca; o cancelamento do registro precisa dessa tag.

Para `shutdown_pre_sync`, a assinatura do handler é:

```c
void (*handler)(void *arg, int howto);
```

`arg` é qualquer ponteiro que o driver passou para o registro; normalmente é o softc. `howto` são os flags de encerramento (`RB_HALT`, `RB_REBOOT`, etc.).

Um handler de encerramento mínimo para o `myfirst`:

```c
#include <sys/eventhandler.h>

static eventhandler_tag myfirst_shutdown_tag;

static void
myfirst_shutdown(void *arg, int howto)
{
	struct myfirst_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	DPRINTF(sc, MYF_DBG_INIT, "shutdown: howto=0x%x\n", howto);
	/* Flush any pending state here. */
	mtx_unlock(&sc->sc_mtx);
}
```

O registro acontece dentro de `myfirst_attach` (ou em uma função auxiliar chamada a partir dele):

```c
myfirst_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
```

O cancelamento do registro acontece dentro de `myfirst_detach`:

```c
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, myfirst_shutdown_tag);
```

O cancelamento do registro é obrigatório. Um driver que faz detach sem cancelar o registro deixa um ponteiro de callback pendente na lista de eventos do kernel. Quando o kernel disparar o evento na próxima vez, chamará uma região de memória que não está mais mapeada, e o sistema entrará em pânico. O pânico ocorre longe da causa, pois o próximo evento pode ser disparado minutos ou horas após o detach.

A tag armazenada em `myfirst_shutdown_tag` é o que vincula o registro ao seu cancelamento. Para um driver com uma única instância, uma variável estática como a do exemplo acima funciona. Para um driver com múltiplas instâncias, a tag deve residir no softc para que o cancelamento de registro de cada instância referencie sua própria tag.

### `EVENTHANDLER` na Cadeia de Attach

Como o registro e o cancelamento do registro são simétricos, eles se encaixam perfeitamente no padrão de limpeza com rótulos da Seção 5. O registro torna-se uma aquisição; seu modo de falha é "o registro retornou erro?" (ele pode falhar em condições de pouca memória); sua limpeza é `EVENTHANDLER_DEREGISTER`.

O fragmento de attach atualizado para um `myfirst` que usa `EVENTHANDLER`:

```c
	/* Stage 7: shutdown handler. */
	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

E o detach correspondente, com o cancelamento de registro em primeiro lugar (ordem inversa de aquisição):

```c
	EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_tag);
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	mtx_destroy(&sc->sc_mtx);
```

`sc->sc_shutdown_tag` fica no softc. Armazená-la ali é importante: o cancelamento do registro precisa saber qual registro específico remover, e o armazenamento por softc mantém as instâncias do driver independentes entre si.

### Prioridade: `SHUTDOWN_PRI_*`

Dentro de um único evento, os callbacks são chamados em ordem de prioridade. A prioridade é o quarto argumento de `EVENTHANDLER_REGISTER`. Para eventos de encerramento, as constantes comuns são:

- `SHUTDOWN_PRI_FIRST`: executado antes da maioria dos outros handlers.
- `SHUTDOWN_PRI_DEFAULT`: executado na ordem padrão.
- `SHUTDOWN_PRI_LAST`: executado após os outros handlers.

Um driver cujo hardware precisa ser desativado antes de os sistemas de arquivos serem liberados pode se registrar com `SHUTDOWN_PRI_FIRST`. Um driver cujo estado depende de os sistemas de arquivos já terem sido liberados (improvável na prática) pode se registrar com `SHUTDOWN_PRI_LAST`. A maioria dos drivers usa `SHUTDOWN_PRI_DEFAULT` e não se preocupa com prioridade.

Constantes de prioridade semelhantes existem para outros eventos (`EVENTHANDLER_PRI_FIRST`, `EVENTHANDLER_PRI_ANY`, `EVENTHANDLER_PRI_LAST`).

### Quando Usar `vm_lowmem`

`vm_lowmem` é o evento que o subsistema de VM dispara quando a memória livre cai abaixo de um limiar. Um driver que mantém um cache próprio (um pool de blocos pré-alocados, por exemplo) pode liberar alguns deles de volta ao kernel em resposta.

O handler é chamado com um único argumento (o ID do subsistema que disparou o evento). Um handler mínimo para um driver com cache:

```c
static void
myfirst_lowmem(void *arg, int unused __unused)
{
	struct myfirst_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	/* Release some entries from the cache. */
	mtx_unlock(&sc->sc_mtx);
}
```

O registro é idêntico ao do evento de encerramento, exceto pelo nome do evento:

```c
sc->sc_lowmem_tag = EVENTHANDLER_REGISTER(vm_lowmem,
    myfirst_lowmem, sc, EVENTHANDLER_PRI_ANY);
```

Um driver que não mantém cache não deve se registrar para `vm_lowmem`. O custo de fazer isso não é zero: o kernel chama todos os handlers registrados em cada evento de memória baixa, e um handler sem operação adiciona latência a essa cadeia de chamadas.

No `myfirst`, não há cache, então `vm_lowmem` não é usado. O padrão é apresentado aqui para o leitor que está prestes a escrever um driver que precise dele.

### `power_suspend_early` e `power_resume`

Suspensão e retomada compõem um ciclo de vida delicado. Entre `power_suspend_early` e `power_resume`, espera-se que os dispositivos do driver estejam quiescentes: sem I/O, sem interrupções, sem transições de estado. Um driver com estado de hardware que precisa ser salvo antes da suspensão e restaurado após a retomada registra handlers para ambos os eventos.

Para drivers de dispositivos de caracteres que não gerenciam hardware, esses eventos normalmente não se aplicam. Para drivers acoplados a um barramento (PCI, USB, SPI), a camada do barramento cuida da maior parte do controle de suspensão e retomada, e o driver só precisa fornecer os métodos `device_suspend` e `device_resume` em sua tabela `device_method_t`. A abordagem com `EVENTHANDLER` é para drivers que querem reagir a uma suspensão de todo o sistema sem estar acoplados a um barramento.

O Capítulo 26 revisitará suspensão e retomada quando o `myfirst` se tornar um driver USB; nesse ponto, o mecanismo da camada do barramento é o preferido.

### O Handler de Eventos do Módulo

Relacionado ao `SYSINIT` e ao `EVENTHANDLER` está o handler de eventos do módulo: o callback que o kernel invoca para `MOD_LOAD`, `MOD_UNLOAD`, `MOD_QUIESCE` e `MOD_SHUTDOWN`. A maioria dos drivers não o substitui; `DRIVER_MODULE` fornece uma implementação padrão que chama `device_probe` e `device_attach` de forma adequada.

Um driver que precisa de comportamento personalizado no carregamento do módulo (além do que o `SYSINIT` pode fazer) pode fornecer seu próprio handler:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		/* Custom load behaviour. */
		return (0);
	case MOD_UNLOAD:
		/* Custom unload behaviour. */
		return (0);
	case MOD_QUIESCE:
		/* Can we be unloaded?  Return errno if not. */
		return (0);
	case MOD_SHUTDOWN:
		/* Shutdown notification; usually no-op. */
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}
```

O handler é conectado por meio de uma estrutura `moduledata_t` em vez de por `DRIVER_MODULE`. As duas abordagens são mutuamente exclusivas para um determinado nome de módulo; o driver escolhe uma ou a outra.

Para a maioria dos drivers, o padrão do `DRIVER_MODULE` é suficiente, e o handler de eventos do módulo não é personalizado. `myfirst` usa `DRIVER_MODULE` em toda a sua implementação.

### Disciplina de Cancelamento de Registro

A regra mais importante ao usar `EVENTHANDLER` é: registre uma vez, cancele o registro uma vez, no attach e no detach, respectivamente. Dois modos de falha surgem quando a regra é violada.

O primeiro modo de falha é o **cancelamento de registro ausente**. O detach é executado, a tag não é cancelada, a lista de eventos do kernel ainda aponta para o handler do softc, e o próximo evento é disparado para uma memória já liberada. O pânico ocorre longe da causa, pois o próximo evento pode ser disparado minutos ou horas após o detach.

A correção é mecânica: todo `EVENTHANDLER_REGISTER` no attach recebe um `EVENTHANDLER_DEREGISTER` correspondente no detach. O padrão de limpeza com rótulos da Seção 5 facilita isso: o registro é uma aquisição com rótulo, e a cadeia de limpeza cancela os registros na ordem inversa.

O segundo modo de falha é o **registro duplo**. Um driver que registra o mesmo handler duas vezes acaba com duas entradas na lista de eventos do kernel; ao executar o detach uma única vez, apenas uma delas é removida. O kernel então mantém uma entrada obsoleta apontando para o softc que acabou de ser desalocado.

A correção também é mecânica: registre exatamente uma vez por attach. Não registre em uma função auxiliar chamada de múltiplos lugares; não registre de forma postergada em resposta ao primeiro evento.

### Um Exemplo Completo do Ciclo de Vida

Reunindo `SYSINIT`, `EVENTHANDLER` e o attach com cleanup rotulado, o ciclo de vida completo de um driver `myfirst` com um pool global e um handler de shutdown funciona da seguinte forma:

No boot do kernel ou no carregamento do módulo:
- `SI_SUB_TUNABLES` dispara. As chamadas `TUNABLE_*_FETCH` no attach verão seus valores.
- `SI_SUB_DRIVERS` dispara. `myfirst_log_pool_init` executa (via `SYSINIT`). O pool global está pronto.
- `SI_SUB_CONFIGURE` dispara. `DRIVER_MODULE` registra o driver. O Newbus realiza o probe; `myfirst_probe` e `myfirst_attach` executam para cada instância.
- Dentro de `myfirst_attach`: lock, cdev, sysctl, estado de log e handler de shutdown são registrados.

Em tempo de execução:
- `ioctl(fd, MYFIRSTIOC_SETMSG, buf)` atualiza a mensagem.
- `devctl_notify` emite `MSG_CHANGED`; o `devd` o registra.

No shutdown:
- O kernel dispara `shutdown_pre_sync`. `myfirst_shutdown` executa para cada handler registrado.
- Os sistemas de arquivos sincronizam.
- `shutdown_final` dispara. A máquina é desligada.

No unload do módulo (antes do shutdown):
- `MOD_QUIESCE` dispara. `myfirst_detach` retorna `EBUSY` se algum dispositivo estiver em uso.
- `MOD_UNLOAD` dispara. `myfirst_detach` executa para cada instância: cancela o registro do handler, libera o estado de log, destrói o cdev e destrói o lock.
- `SYSUNINIT` dispara. `myfirst_log_pool_fini` executa. O pool global é liberado.
- O módulo é desmapeado.

Cada etapa ocupa um lugar bem definido. Cada aquisição tem uma liberação correspondente. Um driver que segue esse padrão com rigor é um driver que o kernel do FreeBSD pode carregar, executar e descarregar quantas vezes for necessário sem acumular estado.

### Decidindo Para Quais Eventos Se Registrar

Um autor de driver que decide se deve registrar um evento deve fazer três perguntas.

Primeiro, **o evento realmente importa para este driver?** `vm_lowmem` importa para um driver com cache; é ruído para um driver sem cache. `shutdown_pre_sync` importa para um driver cujo hardware precisa ser quiesced; é ruído para um pseudo-driver. Um handler que não faz nada útil ainda é chamado em cada evento, tornando o sistema ligeiramente mais lento a cada disparo.

Segundo, **é o evento certo?** O FreeBSD tem vários eventos de shutdown. `shutdown_pre_sync` dispara antes das sincronizações do sistema de arquivos; `shutdown_post_sync` dispara depois; `shutdown_final` dispara imediatamente antes do halt. Um driver registrando na fase errada pode liberar seu cache cedo demais (antes que dados que deveriam ser gravados o sejam) ou tarde demais (depois que os sistemas de arquivos já estão encerrando).

Terceiro, **o evento tem sido estável entre versões do kernel?** `shutdown_pre_sync` é estável há muito tempo e é seguro de usar. Eventos mais recentes ou mais especializados podem ter suas assinaturas alteradas entre releases. Um driver que tem como alvo um release específico do FreeBSD (este livro está alinhado com a versão 14.3) pode confiar nos eventos daquele release; um driver que tem como alvo um intervalo de releases precisa ser mais cuidadoso.

Para o `myfirst`, o Capítulo 25 entregue registra `shutdown_pre_sync` como demonstração. O handler é um no-op: ele apenas registra em log que o shutdown está iniciando. O registro, o cancelamento do registro e a forma de cleanup rotulado são o ponto do exemplo, não o corpo do handler.

### Erros Comuns com `SYSINIT` e `EVENTHANDLER`

Alguns erros se repetem quando esses mecanismos são usados pela primeira vez.

O primeiro erro é **executar código pesado em um `SYSINIT`**. O código em tempo de boot executa em um contexto em que muitos subsistemas do kernel ainda estão sendo inicializados. Um `SYSINIT` que chama um subsistema complexo pode entrar em condição de corrida com a própria inicialização desse subsistema. A regra é: o código de `SYSINIT` deve ser mínimo e autocontido. A inicialização complexa pertence à rotina attach do driver, que executa após todos os subsistemas estarem operacionais.

O segundo erro é **usar `SYSINIT` em vez de `device_attach`**. Um `SYSINIT` executa uma vez por carregamento de módulo, mas `device_attach` executa uma vez por dispositivo. Um driver que inicializa estado por dispositivo em um `SYSINIT` está cometendo um erro de categoria; o estado por dispositivo não existe ainda no momento do `SYSINIT`.

O terceiro erro é **esquecer o argumento de prioridade em `EVENTHANDLER_REGISTER`**. A função recebe quatro argumentos: nome do evento, callback, argumento e prioridade. Alguns drivers esquecem a prioridade e passam um número errado de argumentos; o compilador detecta isso com um erro, mas um driver que passa `0` por acidente registra com a menor prioridade possível, o que pode ser incorreto.

O quarto erro é **não zerar o campo da tag**. Se `sc->sc_shutdown_tag` não estiver inicializado quando `EVENTHANDLER_DEREGISTER` for chamado em um caminho de falha, o cancelamento de registro tenta remover uma tag que nunca foi registrada. O kernel detecta isso (a tag não existe em sua lista de eventos) e o cancelamento se torna um no-op, mas o padrão é frágil. A disciplina mais segura é zerar o softc na alocação (o Newbus faz isso automaticamente via `device_get_softc`, mas drivers que alocam seus próprios softcs precisam fazê-lo manualmente) e nunca alcançar um cancelamento de registro para uma tag que não foi registrada.

### Encerrando a Seção 8

`SYSINIT` e `EVENTHANDLER` são a forma que o kernel tem de permitir que um driver participe de ciclos de vida além da própria janela de attach/detach. `SYSINIT` executa código em uma fase específica do boot; `EVENTHANDLER` executa código em resposta a um evento nomeado do kernel. Juntos, eles cobrem os casos em que o código por dispositivo não é suficiente e o driver precisa interagir com o sistema como um todo.

O `myfirst` ao final do Capítulo 25 usa `EVENTHANDLER_REGISTER` para um handler demonstrativo de `shutdown_pre_sync`; o registro, o cancelamento do registro e a forma de cleanup rotulado estão todos implementados. `SYSINIT` é apresentado, mas não utilizado, porque o `myfirst` não tem um pool global hoje. Os padrões estão plantados; quando o driver de um capítulo futuro precisar deles, o leitor os reconhecerá imediatamente.

Com a Seção 8 concluída, todos os mecanismos que o capítulo se propôs a ensinar estão no driver. O material restante do capítulo aplica esses mecanismos por meio de laboratórios práticos, exercícios desafio e uma referência de resolução de problemas para quando algo der errado.

## Laboratórios Práticos

Os laboratórios desta seção exercitam os mecanismos do capítulo em um sistema FreeBSD 14.3 real. Cada laboratório tem um resultado mensurável específico; após executar o laboratório, você deve ser capaz de descrever o que observou e o que isso significa. Os laboratórios assumem que você tem o diretório complementar `examples/part-05/ch25-advanced/` disponível.

Antes de começar, construa o driver conforme entregue no topo de `ch25-advanced/`:

```console
# cd examples/part-05/ch25-advanced
# make clean
# make
# kldload ./myfirst.ko
# ls /dev/myfirst*
/dev/myfirst0
```

Se qualquer uma dessas etapas falhar, corrija o toolchain ou o código-fonte antes de continuar. Os laboratórios assumem uma linha de base funcional.

### Laboratório 1: Reproduzindo uma Inundação de Log

Objetivo: ver a diferença entre um `device_printf` sem limitação de taxa e o `DLOG_RL` com limitação de taxa quando disparado em um loop intenso.

Código-fonte: `examples/part-05/ch25-advanced/lab01-log-flood/` contém um pequeno programa em espaço do usuário que chama `read()` em `/dev/myfirst0` dez mil vezes na velocidade máxima que o kernel permitir.

Passo 1. Configure temporariamente a máscara de depuração para habilitar a classe de I/O e o printf no caminho de leitura:

```console
# sysctl dev.myfirst.0.debug.mask=0x4
```

O bit de máscara `0x4` habilita `MYF_DBG_IO`, que o callback de leitura utiliza.

Passo 2. Execute a inundação com a versão sem limitação do driver primeiro. Compile e carregue `myfirst-flood-unlimited.ko` de `lab01-log-flood/unlimited/`:

```console
# make -C lab01-log-flood/unlimited
# kldunload myfirst
# kldload lab01-log-flood/unlimited/myfirst.ko
# dmesg -c > /dev/null
# ./lab01-log-flood/flood 10000
# dmesg | wc -l
```

Resultado esperado: aproximadamente dez mil linhas no `dmesg`. O console também pode ser preenchido; o buffer de log do sistema envolve e mensagens anteriores são perdidas.

Passo 3. Descarregue e recarregue a versão com limitação de taxa de `lab01-log-flood/limited/`, que usa `DLOG_RL` com um limite de 10 mensagens por segundo:

```console
# kldunload myfirst
# kldload lab01-log-flood/limited/myfirst.ko
# dmesg -c > /dev/null
# ./lab01-log-flood/flood 10000
# sleep 5
# dmesg | wc -l
```

Resultado esperado: aproximadamente 50 linhas no `dmesg`. A inundação agora emite no máximo 10 mensagens por segundo; a janela de teste de 10 segundos produz cerca de 50 mensagens (o token de burst do primeiro segundo mais as permissões dos segundos subsequentes; a contagem exata pode variar, mas deve estar dentro de dez unidades).

Passo 4. Compare os dois outputs lado a lado. A versão com limitação de taxa é legível; a ilimitada não é. Ambos os drivers tinham comportamento de leitura idêntico; apenas a disciplina de log difere.

Registre: o tempo de relógio que a inundação levou para completar nos dois casos. A versão ilimitada é visivelmente mais lenta porque a própria saída no console é um gargalo. A limitação de taxa tem um benefício de desempenho visível além do benefício de clareza.

### Laboratório 2: Auditoria de errno com `truss`

Objetivo: ver o que `truss(1)` relata quando o driver retorna diferentes valores de errno e calibrar sua intuição sobre qual errno retornar de cada caminho de código.

Código-fonte: `examples/part-05/ch25-advanced/lab02-errno-audit/` contém um programa de usuário que faz uma série de chamadas deliberadamente inválidas e um script que o executa sob `truss`.

Passo 1. Carregue o `myfirst.ko` padrão se ainda não estiver carregado:

```console
# kldload ./myfirst.ko
```

Passo 2. Execute o programa de auditoria sob `truss`:

```console
# truss -f -o /tmp/audit.truss ./lab02-errno-audit/audit
# less /tmp/audit.truss
```

O programa executa as seguintes operações em sequência:
1. Abre `/dev/myfirst0`.
2. Emite um comando ioctl desconhecido (número de comando 99).
3. Emite `MYFIRSTIOC_SETMSG` com argumento NULL.
4. Escreve um buffer de comprimento zero.
5. Escreve um buffer maior do que o driver aceita.
6. Define `dev.myfirst.0.timeout_sec` com um valor acima do permitido.
7. Fecha.

Passo 3. No output do `truss`, encontre cada operação e anote seu errno. Os resultados esperados:

1. `open`: retorna um descritor de arquivo. Sem errno.
2. `ioctl(_IOC=0x99)`: retorna `ENOTTY` (a tradução do kernel para `ENOIOCTL` do driver).
3. `ioctl(MYFIRSTIOC_SETMSG, NULL)`: retorna `EFAULT` (o kernel captura o NULL antes que o handler execute).
4. `write(0 bytes)`: retorna `0` (sem erro, apenas nenhum byte escrito).
5. `write(oversize)`: retorna `EINVAL` (o driver rejeita comprimentos acima do tamanho do seu buffer).
6. `sysctl write out-of-range`: retorna `EINVAL` (o handler do sysctl rejeita o valor).
7. `close`: retorna 0. Sem errno.

Passo 4. Para cada errno observado, localize o código do driver que o retornou. Percorra a cadeia de chamadas do `truss` até o código-fonte do kernel e confirme que o errno que você vê no `truss` é o que o driver retornou. Este exercício calibra seu mapa mental entre "o que o usuário vê" e "o que o driver diz".

### Laboratório 3: Comportamento de Tunable Após Reboot

Objetivo: verificar que um loader tunable realmente altera o estado inicial do driver quando o módulo é carregado pela primeira vez.

Código-fonte: `examples/part-05/ch25-advanced/lab03-tunable-reboot/` contém um script auxiliar `apply_tunable.sh`.

Passo 1. Com o módulo padrão carregado e nenhum tunable definido, confirme o valor inicial do timeout:

```console
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 5
```

O padrão é 5, definido na rotina attach.

Passo 2. Descarregue o módulo, defina o loader tunable, recarregue e confirme o novo valor inicial:

```console
# kldunload myfirst
# kenv hw.myfirst.timeout_sec=12
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 12
```

O tunable definido via `kenv(1)` teve efeito porque `TUNABLE_INT_FETCH` no attach o leu antes da publicação do sysctl.

Passo 3. Altere o sysctl em tempo de execução e confirme que a mudança é aceita, mas não se propaga de volta para o tunable:

```console
# sysctl dev.myfirst.0.timeout_sec=25
dev.myfirst.0.timeout_sec: 12 -> 25
# kenv hw.myfirst.timeout_sec
hw.myfirst.timeout_sec="12"
```

O tunable ainda lê 12; o sysctl lê 25. O tunable é o valor inicial; o sysctl é o valor em tempo de execução. Eles divergem no momento em que o sysctl é escrito.

Passo 4. Descarregue e recarregue. O valor do tunable ainda é 12 (porque está no ambiente do kernel), então o novo sysctl inicia em 12, não em 25. Este é o ciclo de vida: o tunable define o valor inicial, o sysctl define o valor em tempo de execução, o unload perde o valor em tempo de execução, o tunable sobrevive.

Passo 5. Limpe o tunable e recarregue:

```console
# kldunload myfirst
# kenv -u hw.myfirst.timeout_sec
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 5
```

De volta ao padrão em tempo de attach. O ciclo de vida é consistente do início ao fim.

### Laboratório 4: Injeção Deliberada de Falha no Attach

Objetivo: verificar que cada rótulo na cadeia de cleanup é alcançado e que nenhum recurso vaza quando uma falha é injetada no meio do attach.

Código-fonte: `examples/part-05/ch25-advanced/lab04-failure-injection/` contém quatro variantes de build do módulo, cada uma compilando um ponto de injeção de falha diferente:

- `inject-mtx/`: falha logo após a inicialização do lock.
- `inject-cdev/`: falha logo após a criação do cdev.
- `inject-sysctl/`: falha logo após a construção da árvore sysctl.
- `inject-log/`: falha logo após a inicialização do estado de log.

Cada variante define exatamente um dos macros `MYFIRST_DEBUG_INJECT_FAIL_*` da Seção 5.

Passo 1. Construa e carregue a primeira variante. O carregamento deve falhar:

```console
# make -C lab04-failure-injection/inject-mtx
# kldload lab04-failure-injection/inject-mtx/myfirst.ko
kldload: an error occurred while loading module myfirst. Please check dmesg(8) for more details.
# dmesg | tail -3
myfirst0: attach: stage 1 complete
myfirst0: attach: injected failure after mtx_init
device_attach: myfirst0 attach returned 12
```

A função attach retornou `ENOMEM` (errno 12) no ponto de falha injetado. O módulo não está carregado:

```console
# kldstat -n myfirst
kldstat: can't find file: myfirst
```

Passo 2. Repita o processo para as outras três variantes. Cada uma deve falhar no estágio específico que o nome sugere, e cada uma deve deixar o kernel em um estado limpo. Para confirmar esse estado limpo, verifique se há OIDs de sysctl remanescentes, cdevs remanescentes e locks remanescentes:

```console
# sysctl dev.myfirst 2>&1 | head
sysctl: unknown oid 'dev.myfirst'
# ls /dev/myfirst* 2>&1
ls: No match.
# dmesg | grep -i "witness\|leak"
```

Resultado esperado: nenhuma correspondência. Nenhum sysctl, nenhum cdev, nenhuma reclamação do witness. A cadeia de cleanup está funcionando.

Passo 3. Execute o script de regressão combinado que constrói todas as variantes e verifica os resultados automaticamente:

```console
# ./lab04-failure-injection/run.sh
```

O script constrói cada variante, a carrega, confirma que o carregamento falha, confirma que o estado está limpo e exibe um resumo de uma linha por variante. Passar em todas as quatro variantes significa que cada rótulo na cadeia de cleanup foi exercitado em um kernel real e liberou todos os recursos que estavam sendo mantidos naquele rótulo.

### Lab 5: `shutdown_pre_sync` Handler

Propósito: confirmar que o handler de shutdown registrado realmente dispara durante um shutdown real e observar sua ordenação em relação à sincronização do sistema de arquivos.

Fonte: `examples/part-05/ch25-advanced/lab05-shutdown-handler/` contém uma versão de `myfirst.ko` cujo handler `shutdown_pre_sync` imprime uma mensagem distinta no console.

Passo 1. Carregue o módulo e verifique se o handler está registrado lendo o log no attach:

```console
# kldload lab05-shutdown-handler/myfirst.ko
# dmesg | tail -1
myfirst0: attach: shutdown_pre_sync handler registered
```

Passo 2. Execute um reboot. Em uma máquina de teste (não em uma máquina de produção), a forma mais simples é:

```console
# shutdown -r +1 "testing myfirst shutdown handler"
```

Observe o console enquanto a máquina é desligada. A sequência esperada é:

```text
myfirst0: shutdown: howto=0x4
Syncing disks, buffers remaining... 0 0 0
Uptime: ...
```

A linha `myfirst0: shutdown: howto=0x4` aparece **antes** de "Syncing disks", porque `shutdown_pre_sync` dispara antes de os sistemas de arquivos serem sincronizados. Se a mensagem do handler aparecer depois da mensagem de sincronização, o registro foi feito no evento errado (`shutdown_post_sync` ou `shutdown_final`). Se a mensagem nunca aparecer, o handler nunca foi registrado ou nunca foi desregistrado (um double-free causaria um panic, mas a ausência silenciosa sugere um bug de registro).

Passo 3. Após o reboot da máquina, confirme que descarregar o módulo antes do shutdown ainda remove o handler corretamente:

```console
# kldload lab05-shutdown-handler/myfirst.ko
# kldunload myfirst
# dmesg | tail -2
myfirst0: detach: shutdown handler deregistered
myfirst0: detach: complete
```

A mensagem de desregistro confirma que o caminho de limpeza no detach foi executado. O par attach/detach é simétrico; nenhuma entrada na lista de eventos é vazada.

### Lab 6: O Script de Regressão de 100 Ciclos

Propósito: executar um ciclo sustentado de carga e descarga para detectar vazamentos que só aparecem sob attach/detach repetidos. Este é o teste da lista de verificação de produção da Seção 7.

Fonte: `examples/part-05/ch25-advanced/lab06-100-cycles/` contém `run.sh`, que realiza 100 ciclos de kldload / sleep / kldunload e registra o uso de memória do kernel antes e depois.

Passo 1. Registre o uso inicial de memória do kernel:

```console
# vmstat -m | awk '$1=="Solaris" || $1=="kernel"' > /tmp/before.txt
# cat /tmp/before.txt
```

Passo 2. Execute o script de ciclos:

```console
# ./lab06-100-cycles/run.sh
cycle 1/100: ok
cycle 2/100: ok
...
cycle 100/100: ok
done: 100 cycles, 0 failures, 0 leaks detected.
```

Passo 3. Registre o uso final de memória do kernel:

```console
# vmstat -m | awk '$1=="Solaris" || $1=="kernel"' > /tmp/after.txt
# diff /tmp/before.txt /tmp/after.txt
```

Resultado esperado: nenhuma diferença significativa. Se houver uma diferença de mais de alguns kilobytes (a contabilidade interna do kernel flutua), o driver tem um vazamento.

Passo 4. Se o script reportar alguma falha, examine `/tmp/myfirst-cycles.log` (que `run.sh` preenche) em busca do primeiro ciclo com falha. A falha geralmente está na etapa de desregistro: um `EVENTHANDLER_DEREGISTER` ausente ou um `mtx_destroy` ausente.

Uma execução limpa de 100 ciclos é uma das formas mais simples de ganhar confiança na disciplina de ciclo de vida de um driver. Repita-a após cada alteração significativa na cadeia de attach ou detach.

### Lab 7: Descoberta de Capacidades no Espaço do Usuário

Propósito: confirmar que um programa em espaço do usuário consegue descobrir as capacidades do driver em tempo de execução e se comportar de acordo, como projetado na Seção 4.

Fonte: `examples/part-05/ch25-advanced/lab07-getcaps/` contém `mfctl25.c`, uma versão atualizada de `myfirstctl` que emite `MYFIRSTIOC_GETCAPS` antes de cada operação e pula as não suportadas.

Passo 1. Construa `mfctl25`:

```console
# make -C lab07-getcaps
```

Passo 2. Execute contra o driver padrão do Capítulo 25 e observe o relatório de capacidades:

```console
# ./lab07-getcaps/mfctl25 caps
Driver reports capabilities:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

O driver reporta três capacidades. O bit `MYF_CAP_TIMEOUT` está definido mas não está ativado, porque o comportamento de timeout é um sysctl, não um ioctl.

Passo 3. Execute cada operação e confirme que o programa só tenta as suportadas:

```console
# ./lab07-getcaps/mfctl25 reset
# ./lab07-getcaps/mfctl25 getmsg
Current message: Hello from myfirst
# ./lab07-getcaps/mfctl25 setmsg "new message"
# ./lab07-getcaps/mfctl25 timeout
Timeout ioctl not supported; use sysctl dev.myfirst.0.timeout_sec instead.
```

A última linha é o disparo da verificação de capacidades: o programa pediu por `MYF_CAP_TIMEOUT`, o driver não o anunciou, e o programa imprimiu uma mensagem informativa em vez de emitir um ioctl que retornaria `ENOTTY`.

Passo 4. Carregue uma versão mais antiga (o `myfirst.ko` do Capítulo 24 em `lab07-getcaps/ch24/`) e execute novamente:

```console
# kldunload myfirst
# kldload lab07-getcaps/ch24/myfirst.ko
# ./lab07-getcaps/mfctl25 caps
GETCAPS ioctl not supported.  Falling back to default feature set:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

Quando o próprio `GETCAPS` retorna `ENOTTY`, o programa recorre a um conjunto de padrões seguros que corresponde ao comportamento conhecido do Capítulo 24. Este é o padrão de compatibilidade com versões futuras em ação.

Passo 5. Recarregue o driver do Capítulo 25 para restaurar o estado do teste:

```console
# kldunload myfirst
# kldload ./myfirst.ko
```

O exercício demonstra que a descoberta de capacidades permite que um único programa em espaço do usuário funcione corretamente com duas versões do driver, que é exatamente o objetivo do padrão.

### Lab 8: Validação de Intervalo em Sysctls

Propósito: confirmar que todo sysctl gravável que o driver expõe rejeita valores fora do intervalo permitido e deixa o estado interno intacto após a rejeição.

Fonte: `examples/part-05/ch25-advanced/lab08-sysctl-validation/` contém o driver construído com verificações de intervalo e um script de teste `run.sh` que leva cada sysctl aos seus limites.

Passo 1. Carregue o driver e liste seus sysctls graváveis:

```console
# kldload ./myfirst.ko
# sysctl -W dev.myfirst.0 | grep -v "^dev.myfirst.0.debug.classes"
dev.myfirst.0.timeout_sec: 5
dev.myfirst.0.max_retries: 3
dev.myfirst.0.log_ratelimit_pps: 10
dev.myfirst.0.debug.mask: 0
```

Quatro sysctls graváveis. Cada um tem um intervalo válido específico.

Passo 2. Tente definir cada sysctl como zero, como seu máximo permitido e como um valor acima de seu máximo:

```console
# sysctl dev.myfirst.0.timeout_sec=0
sysctl: dev.myfirst.0.timeout_sec: Invalid argument
# sysctl dev.myfirst.0.timeout_sec=60
dev.myfirst.0.timeout_sec: 5 -> 60
# sysctl dev.myfirst.0.timeout_sec=61
sysctl: dev.myfirst.0.timeout_sec: Invalid argument
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 60
```

As tentativas fora do intervalo são rejeitadas com `EINVAL`; o valor interno permanece inalterado. A atribuição válida de 60 é bem-sucedida.

Passo 3. Repita para os outros sysctls:

- `max_retries`: intervalo válido 1-100. Tente 0, 100, 101.
- `log_ratelimit_pps`: intervalo válido 1-10000. Tente 0, 10000, 10001.
- `debug.mask`: intervalo válido 0-0xff (os bits definidos). Tente 0, 0xff, 0x100.

Para cada um, o script reporta aprovação ou falha. Um driver que passa em todos os casos tem a validação em nível de handler correta.

Passo 4. Examine os handlers de sysctl em `examples/part-05/ch25-advanced/myfirst_sysctl.c` e observe o padrão:

```c
static int
myfirst_sysctl_timeout_sec(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int new_val;
	int error;

	mtx_lock(&sc->sc_mtx);
	new_val = sc->sc_timeout_sec;
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val < 1 || new_val > 60)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	sc->sc_timeout_sec = new_val;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}
```

Observe a ordem das operações: copie o valor atual para o lado da leitura, chame `sysctl_handle_int` para tratar a cópia, valide na escrita e confirme sob o lock somente após a validação ser bem-sucedida. Um handler que confirma antes da validação expõe estado inconsistente para leitores concorrentes.

Passo 5. Confirme que a descrição do sysctl é útil (`sysctl -d`):

```console
# sysctl -d dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: Operation timeout in seconds (range 1-60)
```

A descrição informa a unidade e o intervalo. Um usuário que lê o sysctl sem consultar nenhuma documentação ainda consegue defini-lo corretamente.

### Lab 9: Auditoria das Mensagens de Log no Driver

Propósito: inventariar cada mensagem de log no driver e confirmar que cada uma segue as disciplinas da Seção 1 e da Seção 2 (`device_printf`, inclui errno quando relevante, com limitação de taxa quando em um hot path).

Fonte: `examples/part-05/ch25-advanced/lab09-log-audit/` contém um script de auditoria `audit.sh` e um verificador baseado em grep.

Passo 1. Execute o script de auditoria contra o código-fonte do driver:

```console
# cd examples/part-05/ch25-advanced
# ./lab09-log-audit/audit.sh
```

O script faz um grep de cada chamada `printf`, `device_printf`, `log`, `DPRINTF` e `DLOG_RL` na árvore de código-fonte e categoriza cada uma em:

- PASS: usa `device_printf` ou `DPRINTF`, o nome do dispositivo está implícito.
- PASS: usa `DLOG_RL` em um hot path.
- WARN: usa `printf` sem contexto de dispositivo (pode ser legítimo em `MOD_LOAD`).
- FAIL: usa `device_printf` em um hot path sem limitação de taxa.

Saída esperada (para o driver padrão do Capítulo 25):

```text
myfirst.c:    15 log messages - 15 PASS
myfirst_cdev.c:  6 log messages - 6 PASS
myfirst_ioctl.c: 4 log messages - 4 PASS
myfirst_sysctl.c: 0 log messages
myfirst_log.c:   2 log messages - 2 PASS
Total: 27 log messages - 0 WARN, 0 FAIL
```

Passo 2. Quebre intencionalmente uma mensagem (por exemplo, mude um `DPRINTF(sc, MYF_DBG_IO, ...)` no callback de leitura para um `device_printf(sc->sc_dev, ...)` simples) e execute novamente:

```text
myfirst_cdev.c: 6 log messages - 5 PASS, 1 FAIL
  myfirst_cdev.c:83: device_printf on hot path not rate-limited
Total: 27 log messages - 0 WARN, 1 FAIL
```

A auditoria detectou a regressão. Reverta a alteração e execute novamente para confirmar que o contador retorna a zero falhas.

Passo 3. Adicione uma nova mensagem de log em um caminho que não é um hot path (por exemplo, uma mensagem de inicialização única no attach). Confirme que a auditoria a aceita como PASS:

```c
device_printf(dev, "initialised with timeout %u\n",
    sc->sc_timeout_sec);
```

Mensagens únicas no attach não precisam de limitação de taxa porque disparam exatamente uma vez por instância por carga.

Passo 4. Para cada mensagem que a auditoria categoriza como PASS, confirme que a mensagem inclui contexto significativo. Uma mensagem como "error" é um PASS para a ferramenta de auditoria, mas um FAIL para o leitor humano. Uma segunda passagem manual sobre a saída do grep é necessária para confirmar que as mensagens são realmente úteis.

O laboratório demonstra dois pontos. Primeiro, uma auditoria mecânica captura as regras categóricas (limitação de taxa em hot paths, `device_printf` em vez de `printf` simples), mas não consegue julgar a qualidade das mensagens. Segundo, a passagem humana é o que confirma que as mensagens contêm contexto suficiente para diagnóstico. Ambas as passagens juntas fornecem ao driver uma superfície de log que realmente ajudará um futuro engenheiro de suporte.

### Lab 10: Matriz de Compatibilidade entre Versões

Propósito: confirmar que o padrão de descoberta de capacidades introduzido na Seção 4 realmente permite que um único programa em espaço do usuário funcione com três versões diferentes do driver.

Fonte: `examples/part-05/ch25-advanced/lab10-compat-matrix/` contém três arquivos `.ko` pré-construídos correspondentes às versões de driver 1.6-debug, 1.7-integration e 1.8-maintenance, além de um único programa em espaço do usuário `mfctl-universal` que usa `MYFIRSTIOC_GETCAPS` (ou um fallback) para decidir quais operações tentar.

Passo 1. Carregue cada versão do driver por vez e execute `mfctl-universal --caps` contra ela:

```console
# kldload lab10-compat-matrix/v1.6/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.6-debug
GETCAPS ioctl: not supported
Using fallback capability set:
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG

# kldunload myfirst
# kldload lab10-compat-matrix/v1.7/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.7-integration
GETCAPS ioctl: not supported
Using fallback capability set:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG

# kldunload myfirst
# kldload lab10-compat-matrix/v1.8/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.8-maintenance
GETCAPS ioctl: supported
Driver reports capabilities:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

Três versões do driver, um único programa em espaço do usuário, três decisões de capacidade diferentes. O programa funciona com cada versão.

Passo 2. Exercite cada capacidade por vez e confirme que o programa pula as não suportadas:

```console
# kldunload myfirst
# kldload lab10-compat-matrix/v1.6/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal reset
reset: not supported on this driver version (1.6-debug)
# ./lab10-compat-matrix/mfctl-universal getmsg
Current message: Hello from myfirst
```

A operação de reset, adicionada na versão 1.7, é pulada corretamente na versão 1.6. O programa imprime uma mensagem informativa em vez de emitir um ioctl que retornaria `ENOTTY`.

Passo 3. Leia o código-fonte de `mfctl-universal` e observe o fallback em três níveis:

```c
uint32_t
driver_caps(int fd, const char *version)
{
	uint32_t caps;

	if (ioctl(fd, MYFIRSTIOC_GETCAPS, &caps) == 0)
		return (caps);
	if (errno != ENOTTY)
		err(1, "GETCAPS ioctl");

	/* Fallback by version string. */
	if (strstr(version, "1.8-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.7-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.6-") != NULL)
		return (MYF_CAP_GETMSG | MYF_CAP_SETMSG);

	/* Unknown version: use the minimal safe set. */
	return (MYF_CAP_GETMSG);
}
```

O primeiro nível pergunta diretamente ao driver. O segundo nível corresponde a strings de versão conhecidas. O terceiro nível recorre a um conjunto mínimo que todas as versões do driver sempre suportaram.

Passo 4. Reflita sobre o que acontece quando a versão 1.9 é lançada com um novo bit de capacidade. O programa não precisa ser atualizado: `MYFIRSTIOC_GETCAPS` na versão 1.9 reportará o novo bit, o programa o verá e, se o programa conhecer a operação correspondente, a usará. Se o programa não conhecer a operação, o bit será ignorado. De qualquer forma, o programa continuará funcionando.

O laboratório demonstra que a descoberta de capacidades não é um padrão abstrato; é o mecanismo específico que permite que um único programa em espaço do usuário funcione em três versões do driver sem modificação.

## Exercícios Desafio

Os exercícios desafio desta seção vão além dos laboratórios. Cada um pede que você estenda o driver em uma direção que o capítulo apontou mas não concluiu. Trabalhe neles quando estiver pronto; nenhum exige conhecimento de kernel além do que o capítulo cobriu, mas cada um requer uma análise cuidadosa do código existente.

### Desafio 1: Limites de Taxa por Classe

A Seção 1 esboçou três slots de limitação de taxa no softc (`sc_rl_generic`, `sc_rl_io`, `sc_rl_intr`), mas o macro `DLOG_RL` usa um único valor de pps (`sc_log_pps`). Estenda o driver para que cada classe tenha seu próprio limite de pps configurável via sysctl:

- Adicione os campos `sc_log_pps_io` e `sc_log_pps_intr` ao softc ao lado de `sc_log_pps` (que permanece como o limite genérico).
- Adicione sysctls correspondentes em `dev.myfirst.<unit>.log.pps_*` e tunables correspondentes em `hw.myfirst.log_pps_*`.
- Atualize os helpers `DLOG_RL_IO` e `DLOG_RL_INTR` (ou um helper genérico que aceite tanto a classe quanto o valor de pps) para respeitar o limite por classe.

Escreva um programa de teste curto que dispare uma rajada de mensagens em cada classe e confirme pelo `dmesg` que cada classe tem sua taxa limitada de forma independente. O bucket genérico não deve privar o bucket de I/O, e vice-versa.

Dica: o formato mais reutilizável é uma função auxiliar `myfirst_log_ratelimited(sc, class, fmt, ...)` que procura o estado de limitação de taxa correto e o limite de pps correto para o bit de classe fornecido. Os macros `DLOG_RL_*` se tornam wrappers finos sobre a função auxiliar.

### Desafio 2: Um sysctl de String Gravável

A Seção 3 alertou sobre a complicação dos sysctls de string graváveis. Implemente um corretamente. O sysctl deve ser `dev.myfirst.<unit>.message`, com `CTLFLAG_RW`, e deve permitir que um operador reescreva a mensagem interna do driver com uma única chamada `sysctl(8)`.

Requisitos:

1. O handler deve proteger a atualização com o mutex do softc.
2. O handler deve validar o comprimento contra `sizeof(sc->sc_msg)` e rejeitar strings maiores que o limite com `EINVAL`.
3. O handler deve usar `sysctl_handle_string` para a cópia; não reimplemente o acesso ao espaço do usuário.
4. Após uma atualização bem-sucedida, o handler deve emitir um `devctl_notify` para `MSG_CHANGED`, assim como o ioctl faz.

Teste com:

```console
# sysctl dev.myfirst.0.message="hello from sysctl"
# sysctl dev.myfirst.0.message
dev.myfirst.0.message: hello from sysctl
# sysctl dev.myfirst.0.message="$(printf 'A%.0s' {1..1000})"
sysctl: dev.myfirst.0.message: Invalid argument
```

O segundo `sysctl` deve falhar (string muito grande) e a mensagem do driver não deve ter sido alterada.

Reflita: o ioctl e o `sysctl` devem emitir o mesmo evento `MSG_CHANGED`, ou eventos diferentes? Ambos atualizam o mesmo estado subjacente; um único tipo de evento é provavelmente a escolha correta. Documente sua decisão em `MAINTENANCE.md`.

### Challenge 3: Um Handler `MOD_QUIESCE` Separado do Detach

A Seção 7 observou que `MOD_QUIESCE` e `MOD_UNLOAD` são conceitualmente distintos, mas que `myfirst` trata ambos por meio de `myfirst_detach`. Separe-os para que a verificação de quiesce possa ser respondida sem efeitos colaterais.

Requisitos:

1. Adicione uma verificação explícita de `MOD_QUIESCE` em um handler de evento de módulo. O handler retorna `EBUSY` se algum dispositivo estiver aberto, `0` caso contrário.
2. O handler não chama `destroy_dev`, não destrói locks, não altera o estado. Ele apenas lê `sc_open_count`.
3. Para cada instância anexada, itere via `devclass` e verifique cada softc. Use o símbolo `myfirst_devclass` que `DRIVER_MODULE` exporta.

Dica: consulte `/usr/src/sys/kern/subr_bus.c` para `devclass_get_softc` e helpers relacionados. Eles são a forma de enumerar softcs a partir de uma função de nível de módulo que não possui um `device_t`.

Teste: abra `/dev/myfirst0`, tente `kldunload myfirst`, confirme que ele reporta "module busy" e que o driver permanece inalterado. Feche o fd, tente o unload novamente e confirme que ele tem sucesso.

### Challenge 4: Um Detach Baseado em Drain em Vez de `EBUSY`

O padrão de detach do capítulo se recusa a fazer o unload se o driver estiver em uso. Um padrão mais elaborado drena as referências em andamento em vez de recusar. Implemente-o.

Requisitos:

1. Adicione um booleano `is_dying` ao softc, protegido por `sc_mtx`.
2. Em `myfirst_open`, verifique `is_dying` sob o lock e retorne `ENXIO` se for verdadeiro.
3. Em `myfirst_detach`, defina `is_dying` sob o lock. Aguarde `sc_open_count` chegar a zero, usando `mtx_sleep` com uma variável de condição ou um loop de polling simples com timeout.
4. Após `sc_open_count` chegar a zero, prossiga com `destroy_dev` e o restante da cadeia de detach.

Adicione um timeout: se `sc_open_count` não chegar a zero dentro de (digamos) 30 segundos, retorne `EBUSY` de detach. O operador recebe um sinal claro de que o driver não está drenando; ele pode encerrar o processo responsável e tentar novamente.

Teste: abra `/dev/myfirst0` em loop a partir de um shell, chame `kldunload myfirst` a partir de outro shell e observe o comportamento de drain.

### Challenge 5: Uma Verificação de Versão Mínima Dirigida por Sysctl

Escreva um pequeno programa em espaço do usuário que leia `dev.myfirst.<unit>.version`, analise a string de versão e a compare com a versão mínima que o programa requer. O programa deve imprimir "ok" se o driver for suficientemente novo e "driver too old, please update" caso contrário.

Requisitos:

1. Analise a string `X.Y-tag` em inteiros. Rejeite strings malformadas com uma mensagem de erro clara.
2. Compare com o mínimo de `"1.8"`. Um driver reportando `"1.7-integration"` deve falhar na verificação; um driver reportando `"1.8-maintenance"` deve passar; um driver reportando `"2.0-something"` deve passar.
3. Saia com status `0` em caso de sucesso, diferente de zero em caso de falha, para que a verificação possa ser usada em scripts shell.

Reflita: um programa bem projetado poderia depender da string de versão para verificação de compatibilidade, ou o bitmask de capacidades da Seção 4 é o sinal mais adequado? Não existe uma única resposta certa; o exercício é pensar sobre as trocas envolvidas.

### Challenge 6: Adicionar um Sysctl que Enumera Descritores de Arquivo Abertos

Adicione um novo sysctl `dev.myfirst.<unit>.open_fds` que retorne, como string, os PIDs dos processos que atualmente têm o dispositivo aberto. Isso é mais difícil do que parece: o driver normalmente não rastreia qual processo abriu cada fd.

Dica: em `myfirst_open`, armazene o PID da thread chamadora em uma lista encadeada no softc. Em `myfirst_close`, remova a entrada correspondente. No handler do sysctl, percorra a lista sob o mutex do softc e construa uma string de PIDs separados por vírgulas.

Casos extremos:

1. Um processo que está aberto múltiplas vezes (múltiplos fds, filhos criados com fork) deve aparecer uma ou várias vezes? Decida e documente.
2. A lista deve ter comprimento limitado (atacantes poderiam abrir o dispositivo milhões de vezes).
3. O valor do sysctl é somente leitura; o handler não deve modificar a lista.

Reflita: essa informação é realmente útil, ou `fstat(1)` é uma ferramenta melhor para o mesmo trabalho? A resposta depende de se o driver pode fornecer informações que ferramentas em espaço do usuário não conseguem derivar por conta própria.

### Challenge 7: Um Segundo `EVENTHANDLER` para `vm_lowmem`

`myfirst` não possui um cache hoje, mas imagine que tivesse: um pool de buffers de 4 KB pré-alocados usados para operações de leitura e escrita. Sob pressão de baixa memória, o driver deveria liberar alguns dos buffers de volta ao sistema.

Implemente um cache sintético: aloque um array de 64 ponteiros `malloc(M_TEMP, 4096)` no attach. Registre um handler `vm_lowmem` que, quando disparado, libere metade dos buffers em cache. O reattach os realoca.

Requisitos:

1. As alocações do cache ocorrem sob o mutex do softc.
2. O handler `vm_lowmem` adquire o mutex, percorre o array e chama `free()` nos primeiros 32 buffers.
3. Um sysctl `dev.myfirst.<unit>.cache_free` reporta o número atual de slots livres (NULL); um operador pode confirmar que o handler foi disparado.

Teste: use um loop `stress -m 10 --vm-bytes 512M` para colocar o sistema sob pressão de baixa memória e observe o sysctl `cache_free`. Com o tempo, ele deve crescer à medida que `vm_lowmem` dispara repetidamente.

Reflita: é isso para o que o evento foi concebido? Muitos drivers que se registram para `vm_lowmem` possuem caches muito maiores do que 64 buffers; a relação custo-benefício é diferente. Este é um exercício didático; um driver real pensaria com mais cuidado sobre se o seu cache justifica a complexidade.

### Challenge 8: Um Ioctl `MYFIRSTIOC_GETSTATS` Retornando um Payload Estruturado

Até agora, todo ioctl que o driver trata retorna um escalar: um inteiro, um uint32 ou uma string de tamanho fixo. Adicione um ioctl `MYFIRSTIOC_GETSTATS` que retorne um payload estruturado com todos os contadores que o driver mantém.

Requisitos:

1. Defina `struct myfirst_stats` em `myfirst_ioctl.h` com campos para `open_count`, `total_reads`, `total_writes`, `log_drops` (um novo contador que você adiciona) e `last_error_errno` (outro novo contador).
2. Adicione `MYFIRSTIOC_GETSTATS` com o número de comando 6, declarado como `_IOR('M', 6, struct myfirst_stats)`.
3. O handler copia os contadores do softc para o payload sob `sc_mtx` e retorna.
4. Anuncie um novo bit de capacidade `MYF_CAP_STATS` na resposta `GETCAPS`.
5. Atualize `MAINTENANCE.md` para documentar o novo ioctl e a nova capacidade.

Casos extremos:

1. O que acontece se o tamanho da struct mudar posteriormente? O macro `_IOR` incorpora o tamanho ao número do comando. Adicionar um campo altera o número do comando, o que quebra os chamadores antigos. A solução é incluir um campo `version` e um espaço `reserved` na struct desde o início; qualquer adição futura reutiliza o espaço reservado.

2. É seguro retornar todos os contadores de forma atômica, ou eles precisam de locks separados? Manter `sc_mtx` durante toda a cópia é a disciplina mais simples.

Reflita: é aqui que o design de ioctl começa a parecer complexo. Para um snapshot simples de contadores, um sysctl com saída formatada como string pode ser mais fácil do que um ioctl com uma struct versionada. Qual você escolheria, e por quê?

### Challenge 9: Monitoramento ao Vivo Baseado em Devctl

Adicione um segundo evento `devctl_notify` que dispara toda vez que o bucket de rate-limit descarta uma mensagem. O evento deve incluir o nome da classe e o estado atual do bucket como dados no formato `key=value`.

Requisitos:

1. Quando `ppsratecheck` retornar zero (mensagem descartada), incremente um contador de drops por classe e emita um `devctl_notify` com `system="myfirst"`, `type="LOG_DROPPED"` e dados `"class=io drops=42"`.
2. O próprio evento devctl deve ter rate-limit; caso contrário, o ato de reportar drops se torna outro flood. Use um segundo `ppsratecheck` com um cap lento (por exemplo, 1 pps) para as emissões devctl.
3. Escreva uma regra devd que corresponda ao evento e registre um resumo toda vez que ele disparar.

Teste: execute o programa de flood do Lab 1 e confirme que `devctl` emite o relatório de drops sem fazer flood de si mesmo.

## Guia de Resolução de Problemas

Quando os mecanismos deste capítulo se comportam de forma inesperada, os sintomas costumam ser indiretos: uma linha de log silenciosamente ausente, um driver que se recusa a carregar pelo motivo errado, um sysctl que não aparece, um reboot que não chama seu handler. Esta referência mapeia sintomas comuns ao mecanismo mais provavelmente responsável, com os primeiros lugares a verificar no código-fonte do driver.

### Sintoma: `kldload` retorna "Exec format error"

O módulo foi compilado contra uma ABI do kernel que não corresponde ao kernel em execução. A causa típica é uma incompatibilidade entre a versão do kernel em execução e o `SYSDIR` utilizado no momento da compilação.

Verifique: `uname -r` e o valor de `SYSDIR` no Makefile. Se o kernel for 14.3-RELEASE mas o build usou headers de uma árvore 15.0-CURRENT mais recente, a ABI é diferente.

Correção: aponte `SYSDIR` para a árvore de código-fonte que corresponde ao kernel em execução. O Makefile do Capítulo 25 usa `/usr/src/sys` por padrão; em um sistema 14.3 com `/usr/src` correspondente, isso está correto.

### Sintoma: `kldload` retorna "No such file or directory" para um arquivo que parece evidente

O arquivo está presente, mas o loader de módulos do kernel não consegue analisá-lo. Causas comuns: o arquivo é um artefato de build obsoleto de uma máquina diferente, ou o arquivo está corrompido.

Verifique: `file myfirst.ko` deve reportá-lo como um objeto compartilhado ELF 64-bit LSB. Se reportar algo diferente, reconstrua a partir do código-fonte.

### Sintoma: `kldload` tem sucesso, mas `kldstat` não mostra o módulo

O loader decidiu fazer o auto-unload do módulo. Isso acontece quando `MOD_LOAD` retornou zero mas o `device_identify` do `DRIVER_MODULE` não encontrou nenhum dispositivo. Para `myfirst`, que usa `nexus` como pai, isso não deveria acontecer; o pseudo-driver sempre encontra `nexus`.

Verifique: `dmesg | tail -20` para qualquer linha como `module "myfirst" failed to register`. A mensagem aponta para o que deu errado.

### Sintoma: `kldload` reporta "module busy"

Uma instância anterior do driver ainda está carregada e possui um descritor de arquivo aberto em algum lugar. O caminho `MOD_QUIESCE` na instância antiga retornou `EBUSY`.

Verifique: `fstat | grep myfirst` deve mostrar o processo que mantém o fd. Encerre o processo ou feche o fd e tente `kldunload` novamente.

### Sintoma: `sysctl dev.myfirst.0.debug.mask=0x4` retorna "Operation not permitted"

O chamador não é root. Sysctls com `CTLFLAG_RW` normalmente requerem privilégio root, a menos que sejam marcados explicitamente de outra forma.

Verifique: você está executando como root? Use `sudo sysctl ...` ou `su -` primeiro.

### Sintoma: Um novo sysctl não aparece na árvore

`SYSCTL_ADD_*` ou não foi chamado, ou foi chamado com o contexto ou ponteiro de árvore errado. O bug mais comum é usar `SYSCTL_STATIC_CHILDREN` para um OID por dispositivo em vez de `device_get_sysctl_tree`.

Verifique: dentro de `myfirst_sysctl_attach`, confirme que `ctx = device_get_sysctl_ctx(dev)` e `tree = device_get_sysctl_tree(dev)` são utilizados, e que cada chamada `SYSCTL_ADD_*` passa `ctx` como primeiro argumento.

### Sintoma: Um tunable parece estar sendo ignorado

`TUNABLE_*_FETCH` é executado no momento do attach, mas somente se o tunable estiver no ambiente do kernel naquele momento. Os erros mais comuns são: (a) definir o tunable após o módulo ser carregado, (b) digitar o nome incorretamente, (c) esquecer que `kenv` não é persistente.

Verifique:
- `kenv hw.myfirst.timeout_sec` antes de recarregar o módulo. O valor deve ser o esperado.
- A string passada para `TUNABLE_INT_FETCH` deve corresponder exatamente ao que `kenv` exibe. Um erro de digitação em qualquer dos lados é silencioso.
- Definir via `/boot/loader.conf` requer um reboot (ou um `kldunload` seguido de `kldload`, que relê `loader.conf` para os tunables específicos do módulo).

### Sintoma: Uma mensagem de log deveria ser emitida mas não aparece em `dmesg`

Três causas comuns:

1. O bit da classe de debug não está definido. Verifique `sysctl dev.myfirst.0.debug.mask`; o bit da classe deve estar habilitado.
2. O bucket de rate-limit está vazio. Se a mensagem for emitida por meio de `DLOG_RL`, as primeiras disparam e as demais são suprimidas silenciosamente. Defina um cap de pps mais alto via sysctl ou aguarde um segundo para o bucket se reabastecer.
3. A mensagem é emitida mas filtrada pela configuração `sysctl kern.msgbuf_show_timestamp` do sistema ou pelo tamanho do buffer do `dmesg` (`sysctl kern.msgbuf_size`).

Verifique: use `dmesg -c > /dev/null` para limpar o buffer, reproduza a ação e releia o buffer. O buffer esvaziado deve conter apenas a saída do driver.

### Sintoma: Uma mensagem de log aparece uma vez e fica silenciosa para sempre

O bucket de rate-limit nega a mensagem permanentemente. Isso acontece se `rl_curpps` se tornar muito grande e o limite de pps for muito baixo. Verifique se `ppsratecheck` está sendo chamado com um `struct timeval` estável e um `int *` estável (ambos membros do softc); uma variável de pilha por chamada seria zerada a cada chamada e o algoritmo dispararia toda vez.

Verificação: o estado de rate-limit deve estar no softc ou em outro local persistente, não em variáveis locais.

### Sintoma: O attach falha e a cadeia de limpeza não libera um recurso

O goto rotulado está com um passo ausente ou tem um `return` extraviado que curto-circuita a cadeia. Adicione um `device_printf(dev, "reached label fail_X\n")` no topo de cada rótulo e execute novamente o laboratório de injeção de falhas. O rótulo que não imprime é o rótulo que foi ignorado.

Causa comum: um `return (error)` intermediário inserido para depuração e nunca removido. O compilador não avisa porque a cadeia ainda é sintaticamente válida; o comportamento está errado.

### Sintoma: O detach causa um panic com um aviso do "witness"

Um lock foi destruído enquanto ainda estava adquirido, ou um lock foi adquirido após seu dono ser destruído. O subsistema witness captura ambos os casos. O backtrace aponta para o nome do lock, que corresponde ao campo do softc.

Verificação: a cadeia de detach deve ser o inverso exato do attach. Um erro comum é `mtx_destroy(&sc->sc_mtx)` chamado antes de `destroy_dev(sc->sc_cdev)`: os callbacks do cdev ainda podem estar em execução, eles tentam adquirir o lock e o lock já foi destruído. Correção: destrua o cdev primeiro, depois o lock.

### Sintoma: O driver causa um panic no descarregamento do módulo com um ponteiro solto

O `EVENTHANDLER_DEREGISTER` não foi chamado, o kernel disparou o evento e o ponteiro de callback apontava para memória liberada.

Verificação: para cada `EVENTHANDLER_REGISTER` no attach, procure o `EVENTHANDLER_DEREGISTER` no detach. O número deve ser igual. Se o número for igual mas o panic ainda ocorrer, a tag armazenada no softc foi corrompida; audite o caminho de código entre o registro e o cancelamento do registro em busca de escrita indevida na memória.

### Sintoma: `MYFIRSTIOC_GETVER` retorna um valor inesperado

O inteiro de versão do ioctl em `myfirst_ioctl.h` não corresponde ao que `myfirst_ioctl.c` escreve no buffer. Isso acontece se o header for atualizado mas o handler ainda retornar uma constante fixa.

Verificação: o handler deve escrever `MYFIRST_IOCTL_VERSION` (a constante do header), não um inteiro literal.

### Sintoma: Eventos de `devctl_notify` nunca aparecem em `devd.log`

`devd(8)` não está em execução, ou sua configuração não corresponde ao evento.

Verificação:
- `service devd status` confirma que o daemon está em execução.
- `grep myfirst /etc/devd/*.conf` deve encontrar a regra.
- `devd -Df` em primeiro plano imprime cada evento conforme ele chega; reproduza a ação e observe a saída.

### Sintoma: O script de regressão de 100 ciclos aumenta o consumo de memória do kernel

Um recurso está vazando a cada ciclo de carregamento/descarregamento. Culpados comuns: um `malloc` sem um `free` correspondente, um `EVENTHANDLER_REGISTER` sem um cancelamento de registro correspondente, um `sysctl_ctx_init` que o driver chama manualmente sem chamar `sysctl_ctx_free` no detach (o Capítulo 25 usa `device_get_sysctl_ctx`, que é gerenciado pelo Newbus; um driver que aloca seu próprio contexto deve liberá-lo).

Verificação: execute `vmstat -m | grep myfirst` antes e depois para ver o consumo de memória próprio do driver, e `vmstat -m | grep solaris` para estruturas no nível do kernel que o driver possa estar alocando indiretamente.

### Sintoma: Duas chamadas concorrentes a `MYFIRSTIOC_SETMSG` intercalam suas escritas

O mutex do softc não está sendo mantido em torno da atualização. As duas threads estão escrevendo em `sc->sc_msg` simultaneamente, produzindo um resultado corrompido.

Verificação: todo acesso a `sc->sc_msg` e `sc->sc_msglen` no handler de ioctl deve estar dentro de `mtx_lock(&sc->sc_mtx) ... mtx_unlock(&sc->sc_mtx)`.

### Sintoma: Um valor de sysctl é redefinido a cada carregamento do módulo

Este é o comportamento esperado, não um bug. O valor padrão no momento do attach é o que o tunable avalia, que é o padrão do `TUNABLE_INT_FETCH` ou o valor definido via `kenv`. A escrita de sysctl em tempo de execução é perdida no descarregamento. Se você quiser que o valor persista, defina-o via `kenv` ou `/boot/loader.conf`.

### Sintoma: `MYFIRSTIOC_GETCAPS` retorna um valor sem os bits que você acabou de adicionar

O arquivo `myfirst_ioctl.c` foi atualizado mas não recompilado, ou o build incorreto foi carregado. Verifique também se o handler no switch statement usa o operador `|=` ou uma atribuição única que inclua todos os bits.

Verificação: execute `make clean && make` a partir do diretório do exemplo. Execute `kldstat -v | grep myfirst` para confirmar que o caminho do módulo carregado corresponde ao que você construiu.

### Sintoma: Um SYSINIT dispara antes que o alocador do kernel esteja pronto

O SYSINIT está registrado em um subsystem ID muito antecipado. Muitos subsistemas (tunables, locks, initcalls iniciais) não têm permissão de chamar `malloc` com `M_NOWAIT`, muito menos com `M_WAITOK`. Se seu callback chama `malloc` e o kernel causa um panic na inicialização, verifique o subsystem ID.

Verificação: o subsystem ID em `SYSINIT(...)`. Para um callback que aloca memória, use `SI_SUB_DRIVERS` ou posterior; não use `SI_SUB_TUNABLES` ou anterior.

### Sintoma: Um handler registrado com `EVENTHANDLER_PRI_FIRST` ainda executa tarde

`EVENTHANDLER_PRI_FIRST` não é uma garantia firme; é uma prioridade em uma fila ordenada. Se outro handler também estiver registrado com `EVENTHANDLER_PRI_FIRST`, a ordem entre eles é indefinida. As prioridades documentadas são grosseiras; ordenação de granularidade fina não é suportada.

Verificação: aceite que a prioridade é uma dica, não um contrato. Se o driver absolutamente requer execução antes ou depois de um outro handler específico, o design está errado; reestruture o driver para que a ordem não seja relevante.

### Sintoma: `dmesg` não mostra nenhuma saída do driver

O driver está usando `printf` (o similar ao libc) em vez de `device_printf` ou `DPRINTF`. O `printf` do kernel ainda funciona, mas não carrega o nome do dispositivo, o que torna as mensagens difíceis de filtrar.

Verificação: toda mensagem no driver deve passar por `device_printf(dev, ...)` ou `DPRINTF(sc, class, fmt, ...)`. O uso direto de `printf` é geralmente um erro.

## Referência Rápida

A Referência Rápida é um resumo de uma página das macros, flags e funções que o Capítulo 25 introduziu. Use-a no teclado quando o material já lhe for familiar.

### Logging com rate-limit

```c
struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
	if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps)) \
		device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)
```

Use `DLOG_RL` para qualquer mensagem que possa disparar em um loop. Coloque o `struct myfirst_ratelimit` no softc (não na pilha).

### Vocabulário de errno

| Errno | Valor | Uso |
|-------|-------|-----|
| `0` | 0 | sucesso |
| `EPERM` | 1 | operação não permitida (somente root) |
| `ENOENT` | 2 | arquivo inexistente |
| `EBADF` | 9 | descritor de arquivo inválido |
| `ENOMEM` | 12 | não é possível alocar memória |
| `EACCES` | 13 | permissão negada |
| `EFAULT` | 14 | endereço inválido (ponteiro do usuário) |
| `EBUSY` | 16 | recurso ocupado |
| `ENODEV` | 19 | dispositivo inexistente |
| `EINVAL` | 22 | argumento inválido |
| `ENOTTY` | 25 | ioctl inadequado para o dispositivo |
| `ENOTSUP` / `EOPNOTSUPP` | 45 | operação não suportada |
| `ENOIOCTL` | -3 | ioctl não tratado por este driver (interno; o kernel mapeia para `ENOTTY`) |

### Famílias de tunables

```c
TUNABLE_INT_FETCH("hw.myfirst.name",    &sc->sc_int_var);
TUNABLE_LONG_FETCH("hw.myfirst.name",   &sc->sc_long_var);
TUNABLE_BOOL_FETCH("hw.myfirst.name",   &sc->sc_bool_var);
TUNABLE_STR_FETCH("hw.myfirst.name",     sc->sc_str_var,
                                          sizeof(sc->sc_str_var));
```

Chame cada fetch uma vez no attach após o valor padrão ter sido preenchido. O fetch atualiza a variável somente se o tunable estiver presente.

### Resumo de flags do sysctl

| Flag | Significado |
|------|---------|
| `CTLFLAG_RD` | somente leitura |
| `CTLFLAG_RW` | leitura-escrita |
| `CTLFLAG_TUN` | coopera com um loader tunable no momento do attach |
| `CTLFLAG_RDTUN` | atalho para somente leitura + tunable |
| `CTLFLAG_RWTUN` | atalho para leitura-escrita + tunable |
| `CTLFLAG_MPSAFE` | o handler é MPSAFE |
| `CTLFLAG_SKIP` | oculta o OID das listagens padrão do `sysctl(8)` |

### Identificadores de versão

- `MYFIRST_VERSION`: string de release legível por humanos, p.ex. `"1.8-maintenance"`.
- `MODULE_VERSION(myfirst, N)`: inteiro usado por `MODULE_DEPEND`.
- `MYFIRST_IOCTL_VERSION`: inteiro retornado por `MYFIRSTIOC_GETVER`; incrementado somente em mudanças de formato de protocolo.

### Bits de capacidade

```c
#define MYF_CAP_RESET    (1U << 0)
#define MYF_CAP_GETMSG   (1U << 1)
#define MYF_CAP_SETMSG   (1U << 2)
#define MYF_CAP_TIMEOUT  (1U << 3)

#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)
```

### Esqueleto de limpeza com rótulos

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	/* acquire resources in order */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	error = make_dev_s(...);
	if (error != 0)
		goto fail_mtx;

	myfirst_sysctl_attach(sc);

	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

### Organização de arquivos para um driver modular

```text
driver.h           public types
driver.c           module glue, cdevsw, attach/detach
driver_cdev.c      open/close/read/write
driver_ioctl.h     ioctl command numbers
driver_ioctl.c     ioctl dispatch
driver_sysctl.c    sysctl tree
driver_debug.h     DPRINTF macros
driver_log.h       rate-limit structures
driver_log.c       rate-limit helpers
```

### Lista de verificação para produção

```text
[  ] MODULE_DEPEND declared for every real dependency.
[  ] MODULE_PNP_INFO declared if the driver binds to hardware.
[  ] MOD_QUIESCE answers "can you unload?" without side effects.
[  ] devctl_notify emitted for operator-relevant events.
[  ] MAINTENANCE.md current.
[  ] devd.conf snippet included.
[  ] Every log message is device_printf, includes errno,
     and is rate-limited if it can fire in a loop.
[  ] attach/detach survives 100 load/unload cycles.
[  ] sysctls reject out-of-range values.
[  ] ioctl payload is bounds-checked.
[  ] Failure paths exercised via deliberate injection.
[  ] Versioning discipline: three independent version
     identifiers, each bumped for its own reason.
```

### IDs de subsistema do SYSINIT

| Constante | Valor | Uso |
|----------|-------|-----|
| `SI_SUB_TUNABLES` | 0x0700000 | estabelece valores de tunables |
| `SI_SUB_KLD` | 0x2000000 | configuração de KLD e módulos |
| `SI_SUB_SMP` | 0x2900000 | inicializa os APs |
| `SI_SUB_DRIVERS` | 0x3100000 | permite que os drivers se inicializem |
| `SI_SUB_CONFIGURE` | 0x3800000 | configura dispositivos |

Dentro de um subsistema:
- `SI_ORDER_FIRST` = 0x0
- `SI_ORDER_SECOND` = 0x1
- `SI_ORDER_MIDDLE` = 0x1000000
- `SI_ORDER_ANY` = 0xfffffff

### Prioridades de evento de shutdown

- `SHUTDOWN_PRI_FIRST`: executa cedo.
- `SHUTDOWN_PRI_DEFAULT`: padrão.
- `SHUTDOWN_PRI_LAST`: executa tarde.

### Esqueleto de EVENTHANDLER

```c
sc->sc_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    my_handler, sc, SHUTDOWN_PRI_DEFAULT);
/* ... in detach ... */
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_tag);
```

### Hierarquia de nomes de tunables

Tunables e sysctls seguem uma convenção hierárquica de nomes. A tabela abaixo lista os nós que este capítulo introduz:

| Nome | Tipo | Finalidade |
|------|------|---------|
| `hw.myfirst.debug_mask_default` | tunable | máscara de depuração inicial para cada instância |
| `hw.myfirst.timeout_sec` | tunable | timeout de operação inicial em segundos |
| `hw.myfirst.max_retries` | tunable | contagem de tentativas inicial |
| `hw.myfirst.log_ratelimit_pps` | tunable | limite de mensagens por segundo inicial |
| `dev.myfirst.<unit>.version` | sysctl (RD) | string de versão |
| `dev.myfirst.<unit>.open_count` | sysctl (RD) | contagem de fds ativos |
| `dev.myfirst.<unit>.total_reads` | sysctl (RD) | total de chamadas de leitura desde o início |
| `dev.myfirst.<unit>.total_writes` | sysctl (RD) | total de chamadas de escrita desde o início |
| `dev.myfirst.<unit>.message` | sysctl (RD) | conteúdo atual do buffer |
| `dev.myfirst.<unit>.message_len` | sysctl (RD) | comprimento atual do buffer |
| `dev.myfirst.<unit>.timeout_sec` | sysctl (RWTUN) | timeout em tempo de execução |
| `dev.myfirst.<unit>.max_retries` | sysctl (RWTUN) | contagem de tentativas em tempo de execução |
| `dev.myfirst.<unit>.log_ratelimit_pps` | sysctl (RWTUN) | limite de pps em tempo de execução |
| `dev.myfirst.<unit>.debug.mask` | sysctl (RWTUN) | máscara de depuração em tempo de execução |
| `dev.myfirst.<unit>.debug.classes` | sysctl (RD) | nomes de classes e valores de bits |

Leia a tabela como o contrato de interface. A família `hw.myfirst.*` é definida na inicialização; a família `dev.myfirst.*` é ajustada em tempo de execução. Cada entrada gravável tem uma contraparte somente leitura que o operador pode usar para confirmar o valor atual.

### Hierarquia de comandos ioctl

O header de ioctl do Capítulo 25 define estes comandos sob o magic `'M'`:

| Comando | Número | Direção | Finalidade |
|---------|--------|-----------|---------|
| `MYFIRSTIOC_GETVER` | 0 | leitura | retorna `MYFIRST_IOCTL_VERSION` |
| `MYFIRSTIOC_RESET` | 1 | sem dados | zera os contadores de leitura/escrita |
| `MYFIRSTIOC_GETMSG` | 2 | leitura | copia a mensagem atual para fora |
| `MYFIRSTIOC_SETMSG` | 3 | escrita | copia a nova mensagem para dentro |
| (retired) | 4 | n/a | reservado; não reutilizar |
| `MYFIRSTIOC_GETCAPS` | 5 | leitura | bitmask de capacidades |

Adicionar um novo comando significa escolher o próximo número não utilizado. Aposentar um comando significa deixar seu número na hierarquia com `(retired)` ao lado, não reutilizar o número.

## Walkthroughs de Drivers Reais

O capítulo até agora construiu sua disciplina sobre o pseudo-driver `myfirst`. Esta seção vira a lente e examina como as mesmas disciplinas aparecem em drivers que fazem parte do FreeBSD 14.3. Cada walkthrough começa a partir de um arquivo de código-fonte real em `/usr/src`, nomeia o padrão do Capítulo 25 que está em ação e aponta para as linhas onde o padrão é visível. O objetivo não é documentar os drivers específicos (a própria documentação deles faz isso), mas mostrar que os hábitos do Capítulo 25 não foram inventados: eles são os hábitos que os drivers já integrados à árvore já praticam.

Ler drivers reais com um vocabulário de padrões é a maneira mais rápida de acelerar seu próprio julgamento. Quando você reconhece `ppsratecheck` de imediato, cada driver que o utiliza se torna mais rápido de ler.

### `mmcsd(4)`: Log de Erros com Taxa Limitada em um Trecho Crítico

O driver `mmcsd` em `/usr/src/sys/dev/mmc/mmcsd.c` atende ao armazenamento em cartões MMC e SD. O sistema de arquivos acima dele gera um fluxo contínuo de I/O de bloco, e cada bloco que falha na requisição MMC subjacente produz uma potencial linha de log de erro. Sem limitação de taxa, um cartão lento ou instável inundaria o `dmesg` em segundos.

O driver declara seu estado de limitação de taxa por softc, conforme o capítulo recomenda:

```c
struct mmcsd_softc {
	...
	struct timeval log_time;
	int            log_count;
	...
};
```

`log_time` e `log_count` são o estado do `ppsratecheck`. Todo trecho crítico que emite uma mensagem de log envolve o `device_printf` da mesma forma:

```c
#define LOG_PPS  5 /* Log no more than 5 errors per second. */

...

if (req.cmd->error != MMC_ERR_NONE) {
	if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
		device_printf(dev, "Error indicated: %d %s\n",
		    req.cmd->error,
		    mmcsd_errmsg(req.cmd->error));
	...
}
```

O padrão é exatamente o formato `DLOG_RL` que o capítulo apresentou, com a macro expandida no lugar. `LOG_PPS` está definido como 5 mensagens por segundo, e o estado reside no softc para que chamadas repetidas no trecho crítico compartilhem o mesmo bucket.

Três observações merecem destaque. Primeiro, esse padrão não é teórico: um driver FreeBSD em produção o utiliza em um trecho crítico que pode ser acionado milhares de vezes por segundo. Segundo, a escolha entre macro e chamada inline é uma questão de gosto; `mmcsd.c` escreve a chamada diretamente, e o padrão é igualmente legível. Terceiro, a constante `LOG_PPS` é conservadora (5 por segundo); o autor deu preferência a menos mensagens do que mais. O autor de um driver pode ajustar o limite de pps para corresponder à taxa de erros esperada e à tolerância do operador.

### `uftdi(4)`: Dependências de Módulo e Metadados PNP

O driver `uftdi` em `/usr/src/sys/dev/usb/serial/uftdi.c` se vincula a adaptadores USB-serial baseados em chips FTDI. É um exemplo perfeito de driver que depende de outro módulo do kernel: ele não funciona sem a pilha USB.

Próximo ao fim do arquivo:

```c
MODULE_DEPEND(uftdi, ucom, 1, 1, 1);
MODULE_DEPEND(uftdi, usb, 1, 1, 1);
MODULE_VERSION(uftdi, 1);
```

Duas dependências são declaradas. A primeira é sobre `ucom`, o framework genérico USB-serial no qual `uftdi` se baseia. A segunda é sobre `usb`, o núcleo USB. Ambas estão vinculadas exatamente à versão 1. Carregar `uftdi.ko` em um kernel sem `usb` ou `ucom` falha com uma mensagem de erro clara; carregá-lo em um kernel em que a versão do subsistema avançou além de 1 também falha até que a declaração do próprio `uftdi` seja atualizada.

Os metadados PNP são publicados por meio de uma macro que se expande para `MODULE_PNP_INFO`:

```c
USB_PNP_HOST_INFO(uftdi_devs);
```

`USB_PNP_HOST_INFO` é o helper específico de USB definido em `/usr/src/sys/dev/usb/usbdi.h`. Ele se expande para `MODULE_PNP_INFO` com a string de formato correta para tuplas de vendor/product USB. `uftdi_devs` é um array static de entradas de `struct usb_device_id`, uma por tripla (vendor, product, interface) que o driver trata.

Este é o padrão de prontidão para produção do Capítulo 25, Seção 7, aplicado a um driver de hardware real: dependências declaradas, metadados publicados, inteiro de versão presente. Um novo adaptador serial USB que aparece no sistema faz com que `devd(8)` consulte os metadados PNP, identifique `uftdi` como o driver e o carregue se ainda não estiver carregado. O mecanismo é totalmente automático quando os metadados estão corretos.

A versão de `myfirst` do Capítulo 26 usará o mesmo padrão e o mesmo helper.

### `iscsi(4)`: Handler de Shutdown Registrado no `attach`

O iniciador iSCSI em `/usr/src/sys/dev/iscsi/iscsi.c` mantém conexões abertas com alvos de armazenamento remotos. Quando o sistema desliga, o iniciador deve fechar essas conexões de forma ordenada antes que a camada de rede seja encerrada; caso contrário, o lado remoto fica com sessões obsoletas.

O handler de shutdown é registrado no attach:

```c
sc->sc_shutdown_pre_eh = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    iscsi_shutdown_pre, sc, SHUTDOWN_PRI_FIRST);
```

Dois detalhes são importantes. Primeiro, o tag do registro é armazenado no softc (`sc->sc_shutdown_pre_eh`), para que o posterior cancelamento do registro possa referenciá-lo. Segundo, a prioridade é `SHUTDOWN_PRI_FIRST`, e não `SHUTDOWN_PRI_DEFAULT`: o driver iSCSI quer fechar as conexões antes que qualquer outro subsistema inicie o trabalho de shutdown, porque conexões de armazenamento levam tempo para ser fechadas de forma limpa.

O cancelamento do registro ocorre no caminho de detach:

```c
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_pre_eh);
```

Um registro, um cancelamento. O tag no softc os mantém vinculados.

Para o `myfirst`, a demonstração do capítulo usou `SHUTDOWN_PRI_DEFAULT` porque não há uma boa razão para o pseudo-driver ser executado cedo. Drivers reais escolhem uma prioridade com base em quais dependências existem entre eles: drivers que precisam estar silenciosos antes de outros usam `SHUTDOWN_PRI_FIRST`; drivers que dependem de sistemas de arquivos íntegros usam `SHUTDOWN_PRI_LAST`. A prioridade é uma decisão de projeto, e `iscsi` mostra uma forma de tomá-la.

### `ufs_dirhash`: Liberação de Cache no Evento `vm_lowmem`

O cache de hash de diretório UFS em `/usr/src/sys/ufs/ufs/ufs_dirhash.c` é um acelerador em memória por sistema de arquivos para buscas em diretórios. Em operação normal o cache é benéfico; sob pressão de memória ele se torna um problema, então o subsistema registra um handler `vm_lowmem` que descarta entradas do cache:

```c
EVENTHANDLER_REGISTER(vm_lowmem, ufsdirhash_lowmem, NULL,
    EVENTHANDLER_PRI_FIRST);
```

O quarto argumento é `EVENTHANDLER_PRI_FIRST`, que solicita execução antecipada na lista de handlers `vm_lowmem` registrados. O autor do dirhash escolheu execução antecipada porque o cache é memória puramente recuperável: liberá-la prontamente dá a outros handlers (que podem manter estado mais sujo ou menos facilmente liberável) uma chance maior de sucesso.

O callback em si faz o trabalho real: percorre as tabelas de hash, libera as entradas que podem ser liberadas e contabiliza a memória liberada. O ponto essencial de projeto é que o callback não entra em pânico se não houver nada a liberar; ele simplesmente retorna sem ter feito nada.

Este é o padrão `vm_lowmem` do Capítulo 25 em um subsistema que não é um driver de dispositivo, mas que compartilha a mesma disciplina. As lições se transferem: se `myfirst` vier a ter um cache, o formato já está aqui.

### `tcp_subr`: `vm_lowmem` Sem um softc

O subsistema TCP em `/usr/src/sys/netinet/tcp_subr.c` também se registra para `vm_lowmem`, mas seu registro é diferente de uma forma instrutiva:

```c
EVENTHANDLER_REGISTER(vm_lowmem, tcp_drain, NULL, LOWMEM_PRI_DEFAULT);
```

O terceiro argumento (os dados do callback) é `NULL`, e não um ponteiro para softc. O subsistema TCP não tem um único softc; seu estado está espalhado por muitas estruturas. O callback precisa encontrar seu estado por outros meios (variáveis globais, variáveis por CPU, buscas em tabelas de hash).

Isso levanta uma questão que o capítulo sugeriu: quando é aceitável passar `NULL` como argumento do callback? A resposta é: quando o callback tem outro modo de encontrar seu estado. Para um driver com um softc por dispositivo, passar `sc` é quase sempre a escolha certa. Para um subsistema com estado global, passar `NULL` e deixar o callback usar seus globais conhecidos é perfeitamente aceitável.

`myfirst` sempre passará `sc` porque `myfirst` é um driver por dispositivo. Um leitor que se encontre escrevendo um callback de nível de subsistema deve reconhecer que o padrão muda de formas sutis quando o sujeito é global.

### `if_vtnet`: Log com Taxa Limitada em Torno de uma Falha Específica

O driver de rede VirtIO em `/usr/src/sys/dev/virtio/network/if_vtnet.c` oferece um contraponto mais restrito, porém instrutivo, ao `mmcsd`. Enquanto `mmcsd` envolve cada erro do trecho crítico em log com taxa limitada, `if_vtnet` recorre ao `ppsratecheck` apenas em torno de um comportamento anômalo específico: um pacote TSO com bits ECN definidos que o host VirtIO não negociou. O ponto de chamada é pequeno e autocontido:

```c
static struct timeval lastecn;
static int curecn;
...
if (ppsratecheck(&lastecn, &curecn, 1))
        if_printf(sc->vtnet_ifp,
            "TSO with ECN not negotiated with host\n");
```

Dois detalhes merecem ser destacados. Primeiro, o estado de limitação de taxa é declarado como `static` em escopo de arquivo, e não por softc. O autor decidiu que "o host VirtIO discorda do guest sobre ECN" é uma má configuração em nível de sistema, e não uma falha por interface, então um único bucket compartilhado é suficiente; o estado por softc permitiria que a inundação de uma única interface virtual suprimisse as mensagens de outra. Segundo, o limite é de 1 pps, o que é deliberadamente agressivo: o aviso é informativo e deve aparecer no máximo uma vez por segundo em todo o sistema. Um projetista de driver que esperasse o aviso ser acionado com frequência poderia aumentar o limite.

O FreeBSD também oferece `ratecheck(9)`, o irmão de contagem de eventos do `ppsratecheck`. `ratecheck` dispara quando o tempo desde o último evento permitido excede um limiar; `ppsratecheck` dispara quando a taxa de burst recente está abaixo de um limite. Eles são complementares: `ratecheck` é melhor quando você deseja um intervalo mínimo entre mensagens, enquanto `ppsratecheck` é melhor quando você deseja uma taxa máxima de burst.

A lição do `if_vtnet` é que o estado de limitação de taxa pode ser global ou por instância, e a escolha segue o formato esperado da falha. O Capítulo 25 colocou o estado no softc porque os erros do `myfirst` são por instância; um driver diferente poderia razoavelmente fazer uma escolha diferente.

### `vt_core`: Múltiplos Registros de `EVENTHANDLER` em Sequência

O subsistema de terminal virtual em `/usr/src/sys/dev/vt/vt_core.c` registra múltiplos event handlers em diferentes pontos do seu ciclo de vida. O handler de shutdown para a janela do console é um deles:

```c
EVENTHANDLER_REGISTER(shutdown_pre_sync, vt_window_switch,
    vw, SHUTDOWN_PRI_DEFAULT);
```

`SHUTDOWN_PRI_DEFAULT` é a prioridade neutra: o switch do VT é executado depois de tudo que solicitou `SHUTDOWN_PRI_FIRST` e antes de tudo que solicitou `SHUTDOWN_PRI_LAST`. A escolha é deliberada: o switch de terminal não tem requisito de ordenação em relação a outros subsistemas, então o autor escolheu o padrão em vez de reivindicar uma prioridade que o driver não precisava.

O ponto relevante aqui é que `vt_core` registra este handler uma vez, durante o boot, e nunca o cancela. Um driver cujo tempo de vida é o tempo de vida do kernel não precisa cancelar o registro; o kernel nunca chama o handler depois que ele desaparece. Um driver como `myfirst` que pode ser carregado e descarregado como módulo precisa cancelar o registro, porque descarregar o módulo destrói o código do handler. A regra é: cancele o registro se o seu código pode desaparecer enquanto o kernel continua em execução.

Esta distinção é importante para autores de módulos. Caminhos de código embutidos frequentemente parecem estar sem seu cancelamento de registro, mas não estão; eles simplesmente não precisam disso. Um módulo que segue o padrão embutido ao pé da letra entrará em pânico ao ser descarregado.

### FFS (`ffs_alloc.c`): Buckets de Limitação de Taxa por Condição

O alocador FFS em `/usr/src/sys/ufs/ffs/ffs_alloc.c` é um sistema de arquivos e não um driver de dispositivo, mas enfrenta exatamente o problema de inundação de log sobre o qual o Capítulo 25 trata. Um disco que repetidamente fica sem blocos ou inodes pode emitir um erro por chamada `write(2)` que falha, o que é ilimitado na prática. O alocador recorre ao `ppsratecheck` em quatro pontos distintos, e a forma como ele delimita o estado de limitação de taxa é uma boa lição em projeto de bucket.

Cada sistema de arquivos montado carrega o estado de limitação de taxa em sua estrutura de montagem. Dois buckets separados tratam dois tipos distintos de erro:

```c
/* "Filesystem full" reports: blocks or inodes exhausted. */
um->um_last_fullmsg
um->um_secs_fullmsg

/* Cylinder-group integrity reports. */
um->um_last_integritymsg
um->um_secs_integritymsg
```

O bucket de lotação é compartilhado entre dois caminhos de código (`ffs_alloc` e `ffs_realloccg`, ambos escrevendo uma mensagem do tipo "write failed, filesystem is full") mais o esgotamento de inodes. O bucket de integridade é compartilhado entre duas falhas de integridade distintas (divergência de checkhash de cilindro e divergência de número mágico). Em cada ponto de chamada o formato é idêntico:

```c
if (ppsratecheck(&ump->um_last_fullmsg,
    &ump->um_secs_fullmsg, 1)) {
        UFS_UNLOCK(ump);
        ffs_fserr(fs, ip->i_number, "filesystem full");
        uprintf("\n%s: write failed, filesystem is full\n",
            fs->fs_fsmnt);
        ...
}
```

Três decisões de projeto são visíveis. Primeiro, o estado de limitação de taxa é por ponto de montagem, e não global. Um sistema de arquivos com muitas escritas não deve suprimir mensagens de um sistema de arquivos diferente que também está ficando cheio. Segundo, o limite é de 1 pps: no máximo uma mensagem por segundo por ponto de montagem por bucket. Terceiro, mensagens relacionadas compartilham um bucket (todas as mensagens de lotação; todas as mensagens de integridade), enquanto as não relacionadas têm o seu próprio. O autor de `ffs_alloc.c` decidiu que um operador inundado com mensagens de "filesystem full" não deve também ser inundado com mensagens de "out of inodes" para o mesmo ponto de montagem; os dois são sintomas da mesma condição e uma mensagem por segundo é suficiente.

Um pseudo-driver como `myfirst` pode adotar o padrão diretamente. Se `myfirst` algum dia desenvolver uma classe de erros relacionados à capacidade (por exemplo, "buffer full" para o caminho de escrita e "no free slots" para o caminho de ioctl), esses pertencem ao mesmo bucket. Uma falha completamente diferente (por exemplo, "command version mismatch") merece o seu próprio. A disciplina que `ffs_alloc.c` aplica a um sistema de arquivos se transfere inalterada para um driver de dispositivo.

### Lendo Mais Drivers

Todo driver do FreeBSD é um estudo de caso de como uma equipe específica resolveu os problemas que o Capítulo 25 enumera. Algumas áreas de `/usr/src/sys` são particularmente ricas para a busca por padrões:

- `/usr/src/sys/dev/usb/` para drivers USB: `MODULE_DEPEND` e `MODULE_PNP_INFO` em toda parte.
- `/usr/src/sys/dev/pci/` para drivers PCI: rotinas de attach com limpeza organizada por rótulos em escala industrial.
- `/usr/src/sys/dev/cxgbe/` para um driver moderno e complexo: logging com limitação de taxa, árvores de sysctl com centenas de OIDs, versionamento via ABI de módulo.
- `/usr/src/sys/netinet/` para uso de `EVENTHANDLER` em nível de subsistema.
- `/usr/src/sys/kern/subr_*.c` para exemplos de `SYSINIT` em muitos IDs de subsistema diferentes.

Quando você ler um novo driver, comece encontrando sua função attach. Conte as aquisições e os rótulos; eles devem ser compatíveis. Encontre a função detach. Confirme que ela libera os recursos em ordem inversa. Encontre os caminhos de erro e veja qual errno cada um retorna. Procure `ppsratecheck` ou `ratecheck` perto de qualquer mensagem de log que dispare em um caminho crítico. Procure declarações `MODULE_DEPEND`. Procure `EVENTHANDLER_REGISTER` e confirme que há um cancelamento de registro correspondente.

Cada uma dessas inspeções leva segundos quando os padrões já são familiares. Cada uma fortalece o seu próprio instinto para reconhecer quando um padrão está ou não sendo aplicado corretamente.

### O Que Você Não Verá

Alguns padrões que o Capítulo 25 recomenda não aparecem em todos os drivers, e a ausência deles nem sempre é um erro. Saber quais padrões são opcionais evita que você os espere em todo lugar.

`MAINTENANCE.md` é um hábito recomendado por este livro, não um requisito do FreeBSD. A maioria dos drivers da árvore de código-fonte não acompanha um arquivo de manutenção por driver; em vez disso, a página de manual é a referência voltada ao operador, e as notas de versão carregam o registro de alterações. Ambas as soluções funcionam; a escolha entre elas é uma convenção de projeto.

`devctl_notify` é opcional. Muitos drivers não emitem nenhum evento, e não existe regra que os obrigue a fazê-lo. O padrão é valioso quando há eventos aos quais os operadores realmente precisariam reagir; para drivers com comportamento silencioso e sem mudanças de estado visíveis ao operador, emitir eventos é desnecessário.

`SYSINIT` fora de `DRIVER_MODULE` é incomum em drivers modernos. A maior parte do trabalho em nível de driver acontece em `device_attach`, que é executado por instância. Registros explícitos de `SYSINIT` são mais comuns em subsistemas e no código central do kernel; drivers individuais raramente precisam de um. O Capítulo 25 apresentou o `SYSINIT` porque o leitor vai encontrá-lo eventualmente, não porque a maioria dos drivers o utilize.

O handler de eventos de módulo explícito (uma função `MOD_LOAD`/`MOD_UNLOAD` fornecida pelo driver em vez de `DRIVER_MODULE`) também é incomum. Ele está presente quando um driver precisa de comportamento personalizado no momento do carregamento que não se encaixa no modelo Newbus, mas a maioria dos drivers usa o padrão sem problemas.

Quando você lê um driver que omite um desses padrões, a ausência normalmente reflete uma decisão de projeto específica daquele driver, não uma falha de disciplina. Os padrões são ferramentas; nem toda ferramenta é necessária em todo trabalho.

### Encerrando os Walkthroughs

Cada padrão que o capítulo apresentou está visível em algum lugar da árvore de código-fonte do FreeBSD. O driver `mmcsd` aplica limite de taxa aos seus logs do hot-path. O driver `uftdi` declara suas dependências de módulo e metadados PNP. O driver `iscsi` registra um handler `shutdown_pre_sync` com prioridade definida. O cache de dirhash do UFS libera memória no evento `vm_lowmem`. Cada um desses é uma aplicação real, lançada e testada de uma disciplina do Capítulo 25.

Ler esses drivers com um vocabulário de padrões acelera sua própria intuição mais rapidamente do que qualquer livro de um único autor conseguiria. Os padrões se repetem; os drivers diferem. Assim que você consegue nomear o padrão, todo novo driver que você lê o reforça.

O driver `myfirst` ao final do Capítulo 25 incorpora todas as disciplinas. Você acabou de ver as mesmas disciplinas incorporadas por outros oito drivers. O próximo passo é incorporá-las você mesmo, em um driver que não está neste livro. Esse é o trabalho de uma carreira, e as bases estão agora em suas mãos.

## Encerrando

O Capítulo 25 é o mais longo da Parte 5, e sua extensão é deliberada. Cada seção apresentou uma disciplina que transforma um driver funcional em um driver sustentável. Nenhuma das disciplinas é glamourosa; cada uma é o hábito específico que mantém um driver funcionando quando é usado por pessoas que não são seu autor.

O capítulo começou com log com limite de taxa. Um driver que não consegue encher o console de mensagens é um driver cujas mensagens de log valem a pena ser lidas. `ppsratecheck(9)` e a macro `DLOG_RL` tornam a disciplina mecânica: coloque o estado de limite de taxa no softc, envolva as mensagens do hot-path com a macro e deixe o kernel cuidar da contagem por segundo. Tudo que vem depois do log se beneficia, porque um log que você consegue ler é um log do qual você consegue depurar.

A segunda seção nomeou os valores de errno e os distinguiu. `ENOTTY` não é `EINVAL`; `EPERM` não é `EACCES`; `ENOIOCTL` é o sinal especial do kernel de que um driver não reconheceu um comando ioctl e quer que o kernel tente outro handler antes de desistir. Conhecer o vocabulário transforma relatórios de bugs vagos em precisos, e relatórios de bugs precisos chegam às causas raiz mais rapidamente.

A Seção 3 tratou a configuração como uma preocupação de primeira classe. Um tunable é um valor inicial definido no boot; um sysctl é um handle em tempo de execução. Os dois cooperam por meio de `TUNABLE_*_FETCH` e `CTLFLAG_TUN`. Um driver que expõe exatamente os tunables e sysctls certos dá aos operadores controle suficiente para resolver seus próprios problemas sem precisar modificar o código-fonte. O par `hw.myfirst.timeout_sec` e `dev.myfirst.0.timeout_sec` é agora o modelo que todo tunable futuro seguirá.

A Seção 4 nomeou os três identificadores de versão que um driver precisa (string de lançamento, inteiro de módulo, inteiro de wire do ioctl) e apontou que eles mudam por razões diferentes. O ioctl `GETCAPS` dá ao espaço do usuário um mecanismo de descoberta em tempo de execução que sobrevive à adição e remoção de capacidades ao longo do tempo, e a macro `MODULE_DEPEND` torna explícita a compatibilidade entre módulos. A própria transição do driver `myfirst` da versão 1.7 para 1.8 é um estudo de caso pequeno mas completo.

A Seção 5 nomeou o padrão goto rotulado e o tornou a disciplina incondicional para attach e detach. Cada recurso recebe um rótulo; cada rótulo passa para o próximo; o único `return (0)` separa a cadeia de aquisição da cadeia de limpeza. O padrão escala de dois recursos para uma dúzia sem mudar de forma. A injeção deliberada de falhas confirma que cada rótulo é alcançável.

A Seção 6 dividiu o `myfirst` em arquivos por tema. Um arquivo por responsabilidade, uma regra de responsabilidade única para decidir onde colocar o novo código, e um header público pequeno que carrega apenas as declarações que cada arquivo precisa. O driver está agora na fase dois da divisão típica em múltiplos arquivos, e as regras para alcançar a fase três são nomeadas explicitamente.

A Seção 7 voltou o foco para o exterior. O checklist de prontidão para produção cobriu dependências de módulo, metadados PNP, `MOD_QUIESCE`, `devctl_notify`, `MAINTENANCE.md`, regras de `devd`, qualidade das mensagens de log, o teste de regressão de 100 ciclos, validação de entrada, exercício dos caminhos de falha e disciplina de versionamento. Cada item do checklist é um hábito específico que captura um modo de falha específico. O `myfirst` ao final do Capítulo 25 passa por todos os itens.

A Seção 8 fechou o ciclo com `SYSINIT(9)` e `EVENTHANDLER(9)`. Um driver que precisa participar do ciclo de vida do kernel além de sua própria janela de attach/detach tem um mecanismo limpo para isso. O `myfirst` se registra para `shutdown_pre_sync` como demonstração; outros drivers se registram para `vm_lowmem`, suspend/resume ou eventos personalizados.

O driver cresceu de um único arquivo com um buffer de mensagens para um pseudo-dispositivo modular, observável, com disciplina de versionamento, limite de taxa e pronto para produção. A mecânica da autoria de drivers de caracteres no FreeBSD está agora nas mãos do leitor.

O que ainda não foi abordado é o mundo do hardware. Todos os capítulos até agora usaram um pseudo-driver; o dispositivo de caracteres é apoiado por um buffer de software e um conjunto de contadores. Um driver que realmente se comunica com hardware precisa alocar recursos de barramento, mapear janelas de I/O, conectar handlers de interrupção, programar engines de DMA e sobreviver aos modos de falha específicos de dispositivos reais. O Capítulo 26 começa esse trabalho.

## Checkpoint da Parte 5

A Parte 5 cobriu depuração, rastreamento e as práticas de engenharia que separam um driver que apenas compila de um driver que uma equipe consegue manter. Antes que a Parte 6 mude o terreno ao conectar o `myfirst` a transportes reais, confirme que os hábitos da Parte 5 estão em seus dedos e não apenas em suas anotações.

Ao final da Parte 5, você deve se sentir confortável fazendo cada um dos itens a seguir:

- Investigar o comportamento do driver com a ferramenta certa para a questão: `ktrace` e `kdump` para chamadas de sistema por processo, `ddb` para inspeção do kernel ao vivo ou post-mortem, `kgdb` com um core dump para kernels travados, `dtrace` e probes SDT para rastreamento dentro do kernel sem alterações no código-fonte, e `procstat` junto com `lockstat` para visualizações de estado e contenção.
- Ler uma mensagem de panic e extrair as pistas certas, transformando um stack trace em um caso de teste reproduzível em vez de um jogo de adivinhação.
- Construir e executar a pilha de integração `myfirst` do Capítulo 24: um driver que expõe suas superfícies por meio de ioctl, sysctl, probes do DTrace e `devctl_notify` enquanto exercita um caminho completo de injeção de ciclo de vida.
- Aplicar o checklist de prontidão para produção do Capítulo 25 item por item: `MODULE_DEPEND`, metadados PNP, `MOD_QUIESCE` como um handler separado, `devctl_notify`, `MAINTENANCE.md`, regras de `devd`, qualidade das mensagens de log, o teste de regressão de 100 ciclos, validação de entrada, exercício deliberado dos caminhos de falha e disciplina de versionamento.
- Usar `SYSINIT(9)` e `EVENTHANDLER(9)` quando um driver precisa participar do ciclo de vida do kernel além de sua própria janela de attach e detach, incluindo registro e cancelamento de registro adequados.

Se algum desses ainda parecer instável, os laboratórios a revisitar são:

- Ferramentas de depuração na prática: Laboratório 23.1 (Uma Primeira Sessão no DDB), Laboratório 23.2 (Medindo o Driver com DTrace), Laboratório 23.4 (Encontrando um Vazamento de Memória com vmstat -m) e Laboratório 23.5 (Instalando o Refactor 1.6-debug).
- Integração e observabilidade: Laboratório 1 (Construir e Carregar o Driver do Estágio 1), Laboratório 4 (Percorrer o Ciclo de Vida Injetando uma Falha), Laboratório 5 (Rastrear as Superfícies de Integração com DTrace) e Laboratório 6 (Smoke Test de Integração) no Capítulo 24.
- Disciplina de produção: Laboratório 3 (Comportamento de Tunable no Reboot), Laboratório 4 (Injeção Deliberada de Falha no Attach), Laboratório 6 (O Script de Regressão de 100 Ciclos) e Laboratório 10 (Matriz de Compatibilidade Multi-Versão) no Capítulo 25.

A Parte 6 espera o seguinte como linha de base:

- Confiança para ler um panic e segui-lo até uma causa raiz, já que três novos transportes introduzirão três novas formas de as coisas darem errado.
- Um `myfirst` que passa pelo checklist de produção do Capítulo 25, para que o Capítulo 26 possa adicionar USB sem que a disciplina anterior vacile sob nova carga.
- Consciência de que a Parte 6 muda o padrão do exemplo em execução: o fio condutor do `myfirst` se estenderá pelo Capítulo 26 como `myfirst_usb`, então pausará enquanto os Capítulos 27 e 28 usam demonstrações novas e específicas de cada transporte. A disciplina continua; apenas o artefato de código muda. O Capítulo 29 retorna à forma cumulativa.

Se esses pontos se sustentarem, você está pronto para a Parte 6. Se algum ainda parecer instável, a solução é um único laboratório bem escolhido, e não avançar às pressas.

## Ponte para o Capítulo 26

O Capítulo 26 volta o driver `myfirst` para o exterior. Em vez de servir um buffer na RAM, o driver se conectará a um barramento real e atenderá um dispositivo real. O primeiro alvo de hardware do livro é o subsistema USB: USB é onipresente, bem documentado e tem uma interface de kernel limpa que é mais fácil de começar do que PCI ou ISA.

Os hábitos que você construiu no Capítulo 25 se transferem sem alteração. O padrão goto rotulado no attach escala para alocações de recursos de barramento: `bus_alloc_resource`, `bus_setup_intr`, `bus_dma_tag_create`, cada um se torna mais uma aquisição na cadeia, cada um com seu próprio rótulo. A organização modular de arquivos se estende naturalmente: `myfirst_usb.c` se une a `myfirst.c`, `myfirst_cdev.c` e ao restante. O checklist de produção adiciona dois itens: `MODULE_DEPEND(myfirst_usb, usb, ...)` e `MODULE_PNP_INFO` para os identificadores de vendor/produto que o driver gerencia. A disciplina de versionamento continua; o driver do Capítulo 26 será o 1.9-usb.

O que é genuinamente novo no Capítulo 26 não é a estrutura do driver; são as interfaces entre o driver e o subsistema USB. Você vai conhecer `usb_request_methods`, callbacks de configuração de transferência, a separação entre transferências de controle, bulk, interrupção e isócrona, e o ciclo de vida específico do USB (attach é por dispositivo, assim como no Newbus; detach é por dispositivo, assim como no Newbus; hot-plug é a condição normal de operação, diferente da maioria dos outros barramentos).

Antes de começar o Capítulo 26, pause e confirme que o material do Capítulo 25 está sólido. Os laboratórios são feitos para ser executados; executá-los é a melhor maneira de encontrar as partes do capítulo que não ficaram claras. Se você executou os Laboratórios 1 a 7 e os exercícios desafio estão claros, você está pronto para o hardware.

O Capítulo 26 começa com um dispositivo USB simples (um adaptador serial FTDI, muito provavelmente) e percorre o probe, o attach, a configuração de transferência bulk, o despacho de leitura/escrita e os caminhos de detach e hot-unplug. Ao final do Capítulo 26, `myfirst_usb.ko` estará servindo bytes reais por meio de um cabo real, e a disciplina do Capítulo 25 garantirá que o código permaneça fácil de manter.

## Glossário

Os termos deste glossário aparecem todos no Capítulo 25 e valem a pena ser conhecidos pelo nome. As definições são intencionalmente curtas; o corpo do capítulo é onde cada conceito é desenvolvido.

**Cadeia de attach.** A sequência ordenada de aquisições de recursos no método `device_attach` de um driver. Cada aquisição que pode falhar é seguida de um goto para o rótulo que desfaz os recursos adquiridos anteriormente.

**Bitmask de capacidades.** Um inteiro de 32 bits (ou 64 bits) retornado por `MYFIRSTIOC_GETCAPS`, com um bit por funcionalidade opcional. Permite que o espaço do usuário consulte o driver em tempo de execução sobre quais funcionalidades ele suporta.

**Cadeia de limpeza.** A sequência ordenada de rótulos no final do método `device_attach` de um driver. Cada rótulo libera um recurso e passa para o próximo. É o inverso da ordem de aquisição.

**`CTLFLAG_SKIP`.** Uma flag de sysctl que oculta um OID das listagens padrão do `sysctl(8)`. O OID ainda pode ser lido quando seu nome completo é fornecido explicitamente. Definida em `/usr/src/sys/sys/sysctl.h`.

**`CTLFLAG_RDTUN` / `CTLFLAG_RWTUN`.** Abreviações para `CTLFLAG_RD | CTLFLAG_TUN` e `CTLFLAG_RW | CTLFLAG_TUN`, respectivamente. Declaram um sysctl que coopera com um tunable do loader.

**`devctl_notify`.** Função do kernel que emite um evento estruturado legível pelo `devd(8)`. Permite que um driver notifique daemons no espaço do usuário sobre mudanças de estado relevantes.

**`DLOG_RL`.** Macro que envolve `ppsratecheck` e `device_printf` e limita uma mensagem de log a uma taxa máxima de mensagens por segundo.

**Errno.** Um inteiro positivo pequeno que representa um modo de falha específico. A tabela de errno do FreeBSD está em `/usr/src/sys/sys/errno.h`.

**Handler de eventos.** Um callback registrado com `EVENTHANDLER_REGISTER` que é executado sempre que o kernel dispara um evento nomeado. Cancelado com `EVENTHANDLER_DEREGISTER`.

**`EVENTHANDLER(9)`.** O framework genérico de notificação de eventos do FreeBSD. Define eventos, permite que subsistemas os publiquem e permite que drivers se inscrevam.

**Injeção de falhas.** Uma técnica de teste deliberada que faz um caminho de código falhar para exercitar sua limpeza. Geralmente implementada como um `return` condicional protegido por um `#ifdef`.

**Padrão goto rotulado.** Veja "cadeia de attach" e "cadeia de limpeza". O formato idiomático do FreeBSD para attach e detach que usa `goto label;` para desfazer recursos de forma linear em vez de `if` aninhados.

**`MOD_QUIESCE`.** O evento de módulo que pergunta "você pode ser descarregado agora?". Um driver retorna `EBUSY` para recusar; `0` para aceitar.

**`MODULE_DEPEND`.** Macro que declara uma dependência de outro módulo do kernel. O kernel impõe a ordem de carregamento e a compatibilidade de versão.

**`MODULE_PNP_INFO`.** Macro que publica os identificadores de fornecedor e produto que um driver gerencia. O kernel usa esses metadados para carregar drivers automaticamente quando o hardware correspondente é detectado.

**`MODULE_VERSION`.** Macro que declara o inteiro de versão de um módulo. Usado por `MODULE_DEPEND` para verificações de compatibilidade.

**`myfirst_ratelimit`.** A struct que mantém o estado de rate limit por classe (`lasttime` e `curpps`). Deve viver no softc, não na pilha.

**`MYFIRST_VERSION`.** A string de release legível por humanos do driver, por exemplo `"1.8-maintenance"`. Exposta via `dev.myfirst.<unit>.version`.

**`MYFIRST_IOCTL_VERSION`.** O inteiro de versão do formato wire de ioctl do driver. Retornado por `MYFIRSTIOC_GETVER`. Incrementado apenas para mudanças incompatíveis.

**Pps.** Eventos por segundo. Um limite de taxa expresso em pps (por exemplo, 10 pps = 10 mensagens por segundo).

**`ppsratecheck(9)`.** A primitiva de rate limit do FreeBSD. Recebe uma `struct timeval`, um `int *` e um limite em pps; retorna um valor diferente de zero para permitir o evento.

**Pronto para produção.** Um driver que satisfaz o checklist da Seção 7: dependências declaradas, superfície documentada, logging com rate limit, caminhos de falha exercitados e aprovado no teste de 100 ciclos.

**`SHUTDOWN_PRI_*`.** Constantes de prioridade passadas para `EVENTHANDLER_REGISTER` para eventos de shutdown. `FIRST` é executado cedo; `LAST` é executado tarde; `DEFAULT` é executado no meio.

**`SI_SUB_*`.** Identificadores de subsistema para `SYSINIT`. Os valores numéricos são ordenados pela sequência de boot. Constantes comuns: `SI_SUB_TUNABLES`, `SI_SUB_DRIVERS`, `SI_SUB_CONFIGURE`.

**`SI_ORDER_*`.** Constantes de ordem para `SYSINIT` dentro de um subsistema. `FIRST` é executado primeiro; `MIDDLE` é executado no meio; `ANY` é executado por último (sem ordem garantida entre entradas `ANY`).

**Regra de responsabilidade única.** Cada arquivo fonte responde a uma única pergunta sobre o driver. Violações significam novos ioctls entrando sorrateiramente em `myfirst_sysctl.c` ou novos sysctls entrando em `myfirst_ioctl.c`.

**Softc.** A estrutura de estado por dispositivo. O newbus a aloca, zera e a entrega ao `device_attach` via `device_get_softc`.

**`sysctl(8)`.** O comando em espaço do usuário e a interface do kernel para parâmetros em tempo de execução. Os nomes dos nós ficam sob uma hierarquia fixa (`kern.*`, `hw.*`, `net.*`, `dev.*`, `vm.*`, `debug.*`, etc.).

**`SYSINIT(9)`.** A macro de inicialização em tempo de boot do FreeBSD. Registra uma função para ser executada em um subsistema e ordem específicos durante a inicialização do kernel ou o carregamento de um módulo.

**`SYSUNINIT(9)`.** Companheiro de `SYSINIT`. Registra uma função de limpeza para ser executada no descarregamento do módulo na ordem inversa da ordem de `SYSINIT`.

**Tag (handler de eventos).** O valor opaco retornado por `EVENTHANDLER_REGISTER` e consumido por `EVENTHANDLER_DEREGISTER`. Deve ser armazenado (geralmente no softc) para que o cancelamento do registro possa localizá-lo.

**Tunable.** Um valor extraído do ambiente do kernel durante o boot ou o carregamento de um módulo e consumido via `TUNABLE_*_FETCH`. Define valores iniciais; reside no nível `hw.`, `kern.` ou `debug.`.

**`TUNABLE_INT_FETCH`, `_LONG_FETCH`, `_BOOL_FETCH`, `_STR_FETCH`.** A família de macros que lê um tunable do ambiente do kernel e preenche uma variável. Não faz nada se o tunable estiver ausente.

**Divisão de versões.** A prática de usar três identificadores de versão independentes (string de release, inteiro de módulo, inteiro de ioctl) que mudam por razões diferentes.

**Evento `vm_lowmem`.** Um evento `EVENTHANDLER` disparado quando o subsistema de memória virtual está sob pressão. Drivers com caches podem liberar parte da memória de volta.

**Formato wire.** O layout dos dados que atravessam a fronteira usuário/kernel. O formato wire de ioctl é determinado pelas declarações `_IOR`, `_IOW`, `_IOWR` e pela estrutura de payload. Uma mudança no formato wire é uma mudança incompatível e exige o incremento de `MYFIRST_IOCTL_VERSION`.
