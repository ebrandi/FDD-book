---
title: "Depuração e Rastreamento"
description: "O Capítulo 23 abre a Parte 5 ensinando como depurar e rastrear drivers de dispositivo FreeBSD de forma disciplinada e reproduzível. O capítulo explica por que a depuração no kernel difere da depuração no userland e o que essa diferença exige do leitor; como usar `printf()` e `device_printf()` de forma eficaz e onde a saída realmente vai parar; como dmesg, o buffer de mensagens do kernel, `/var/log/messages` e syslog se combinam para formar o pipeline de logging do qual o driver depende; como construir e executar um kernel de depuração com DDB, KDB, INVARIANTS, WITNESS e opções relacionadas, e o que cada opção realmente verifica; como o DTrace expõe uma visão ao vivo da atividade do kernel por meio de providers, probes e scripts curtos; como ktrace e kdump revelam a fronteira usuário-kernel do ponto de vista do processo do usuário; como diagnosticar os bugs que os drivers produzem repetidamente, incluindo vazamentos de memória, condições de corrida, uso incorreto de bus_space e bus_dma, e acessos após o detach; e como refatorar o logging e o rastreamento em um subsistema limpo, ativável e de fácil manutenção. O driver myfirst evolui da versão 1.5-power para a 1.6-debug, ganha myfirst_debug.c e myfirst_debug.h, ganha probes SDT que o leitor pode inspecionar com dtrace, ganha um documento DEBUG.md e encerra o Capítulo 23 com um driver que informa o que está fazendo sempre que você quiser."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 23
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Depuração e Rastreamento

## Orientação ao Leitor e Objetivos

O Capítulo 22 encerrou a Parte 4 com um driver que sobrevive a suspend, resume e shutdown. O driver `myfirst` na versão `1.5-power` faz attach em um dispositivo PCI, aloca vetores MSI-X, executa um pipeline DMA, cumpre o contrato kobj de gerenciamento de energia, expõe contadores via sysctls e passa por um script de regressão que exercita attach, detach, suspend, resume e PM em tempo de execução. Para um driver que inicia, executa, suspende, acorda e eventualmente é descarregado, a mecânica está completa. O que o driver ainda não tem é a capacidade de dizer ao seu desenvolvedor o que está acontecendo internamente quando algo dá errado.

O Capítulo 23 adiciona essa capacidade. A Parte 5 tem o título *Depuração, Ferramentas e Práticas do Mundo Real*, e o Capítulo 23 a abre ensinando a disciplina de observabilidade e diagnóstico que transforma um driver recém-escrito em um driver que você consegue manter por anos. O leitor passará este capítulo aprendendo como a depuração em espaço do kernel difere da depuração em espaço do usuário, como usar `printf()` e `device_printf()` com intenção em vez de por hábito, como acompanhar mensagens de log desde o momento em que são escritas até o momento em que o usuário as lê com `dmesg`, como construir um kernel de debug que captura erros que o kernel normal não detecta, como pedir ao DTrace que mostre o comportamento do driver em tempo real sem precisar recompilar nada, como usar `ktrace` e `kdump` para acompanhar um programa do usuário enquanto ele atravessa o kernel pela interface do driver, como reconhecer os padrões característicos de bugs comuns em drivers na primeira vez que um sintoma aparece, e como refatorar o suporte de depuração do driver para que continue útil sem transformar o código-fonte em uma pilha de chamadas `printf` espalhadas.

O escopo do Capítulo 23 é precisamente este. Ele ensina o kit de ferramentas de depuração do kernel que um desenvolvedor de drivers FreeBSD usa no trabalho de laboratório, em testes de regressão e ao responder a relatórios de bugs. Ensina DTrace o suficiente para instrumentar um driver e medir seu comportamento. Ensina o modelo mental por trás das opções de debug do kernel para que o leitor possa escolher o conjunto certo para o problema que tem diante de si. Mostra onde na árvore de código-fonte essas facilidades estão e como ler sua documentação diretamente. Não ensina a mecânica mais profunda da análise post-mortem de crashes do kernel, porque esse material pertence a um tópico especializado mais adiante neste livro. Não ensina arquitetura de drivers nem integração com subsistemas específicos, porque o Capítulo 24 trata disso. Não ensina ajuste avançado de desempenho nem profiling em larga escala, porque a Parte 6 cobre ajuste orientado a hardware em profundidade. A disciplina que o Capítulo 23 introduz é a fundação que os capítulos posteriores assumem: você não pode ajustar o que não consegue observar, e não pode corrigir o que não consegue reproduzir.

A Parte 5 é onde o driver conquista as qualidades que separam um protótipo funcional de um software de produção. O Capítulo 22 fez o driver sobreviver a mudanças de estado de energia. O Capítulo 23 faz o driver dizer o que está fazendo. O Capítulo 24 fará o driver se integrar corretamente com os subsistemas do kernel que os usuários realmente acessam a partir de seus programas. O Capítulo 25 tornará o driver resiliente sob stress. Cada capítulo adiciona mais uma qualidade. O Capítulo 23 adiciona observabilidade, que acaba sendo a qualidade da qual todos os tópicos posteriores dependem.

### Por Que a Depuração do Kernel Merece um Capítulo Próprio

Antes da primeira linha de código, vale entender por que DTrace, `dmesg` e as opções de kernel de debug recebem um capítulo inteiro. Você pode já estar se perguntando por que isso é mais difícil do que depurar um programa de usuário: adicionar alguns `printf`, executar com `gdb`, percorrer o código, pronto. Esse modelo mental é razoável no espaço do usuário e catastrófico no espaço do kernel, por três razões interligadas.

A primeira é que **um bug em um driver é um bug no kernel**. Quando um programa em espaço do usuário tem um bug, o sistema operacional protege o resto da máquina contra ele: o kernel mata o processo, o shell exibe uma mensagem, o usuário tenta de novo. Nenhuma dessas proteções se aplica dentro do kernel. Um único ponteiro inválido, um único lock esquecido, uma única alocação não liberada, uma única condição de corrida entre um handler de interrupção e uma task, podem causar um panic no sistema inteiro. Um driver que causa um crash durante os testes derruba a VM em execução. Um driver que vaza memória eventualmente consome toda a memória da máquina. Um driver que corrompe memória destrói quaisquer dados que tiveram o azar de estar próximos ao bug. O Capítulo 23 existe porque essas falhas não se parecem nem se comportam como falhas no espaço do usuário, e as ferramentas para encontrá-las também não se parecem nem se comportam como ferramentas do espaço do usuário.

A segunda é que **a visibilidade é limitada e custosa**. Em um depurador de espaço do usuário, o programa é parado para você. Você pode avançar passo a passo, imprimir variáveis, definir breakpoints condicionais, voltar no tempo com `rr` ou `gdb --record`. No kernel, nada disso está geralmente disponível em tempo de execução sem preparação especial. Você não pode parar um kernel em execução da mesma forma que para um processo, porque o kernel também é o que executa todo o resto, incluindo o teclado, o terminal, a rede e o disco. Cada parte de visibilidade que você adiciona tem um custo: um `printf` é barato mas lento; uma probe SDT é rápida, mas precisa estar compilada e habilitada; um script DTrace é flexível, mas executa no próprio kernel e toca cada probe que dispara. O Capítulo 23 existe porque você precisa gastar o orçamento de visibilidade de forma deliberada, e saber o que cada ferramenta custa faz parte de saber como usá-la.

A terceira é que **os ciclos de feedback são mais longos**. No espaço do usuário, o ciclo de escrever, compilar e executar leva segundos. No trabalho com o kernel, o ciclo é o mesmo nas melhores condições, mas a consequência de um erro é um reboot ou um rollback do snapshot da VM. Um bug que leva uma tentativa para ser reproduzido no espaço do usuário pode exigir dez suspensões e retomadas no kernel antes que o contador interno do driver dê a volta e o sintoma apareça. Um bug que se reproduz em toda execução com `gdb` pode se reproduzir apenas com o kernel sem carga, em uma carga específica, ou em uma CPU específica. O Capítulo 23 existe porque depurar o kernel só é tolerável se o ciclo de feedback for tão curto quanto possível, e ciclos curtos exigem a combinação certa de ferramentas para cada classe de bug.

O Capítulo 23 merece seu lugar ensinando essas três realidades juntas, de forma concreta, com o driver `myfirst` como exemplo contínuo. Um leitor que terminar o Capítulo 23 conseguirá adicionar saída de debug disciplinada e togável a qualquer driver FreeBSD; ler e interpretar `dmesg`, `/var/log/messages` e saídas do DTrace sem ajuda; construir um kernel de debug e saber o que cada opção oferece; encontrar e corrigir as classes de bugs comuns em drivers na primeira vez que um sintoma aparece; e transformar um relato vago ("minha máquina às vezes trava quando o driver carrega") em um caso de teste reproduzível, uma hipótese, uma correção e um teste de regressão.

### Onde o Capítulo 22 Deixou o Driver

Alguns pré-requisitos para verificar antes de começar. O Capítulo 23 estende o driver produzido ao final do Estágio 4 do Capítulo 22, marcado como versão `1.5-power`. Se algum dos itens abaixo estiver incerto, volte ao Capítulo 22 e corrija-o antes de iniciar este capítulo, pois os tópicos de depuração pressupõem que o driver tem uma linha de base estável para depurar.

- Seu driver compila sem erros e se identifica como `1.5-power` em `kldstat -v`.
- O driver aloca um ou três vetores MSI-X, registra um pipeline de interrupção do tipo filter-task, vincula cada vetor a uma CPU e exibe um banner de interrupção durante o attach.
- O driver aloca uma tag `bus_dma` e um buffer DMA de 4 KB, expõe o endereço de barramento em `dev.myfirst.N.dma_bus_addr` e libera os recursos DMA no detach.
- O driver implementa `DEVICE_SUSPEND`, `DEVICE_RESUME` e `DEVICE_SHUTDOWN`. Um `devctl suspend myfirst0` seguido de `devctl resume myfirst0` tem sucesso sem erros, e uma transferência DMA subsequente funciona corretamente.
- Contadores para transições de suspend, resume, shutdown e PM em tempo de execução aparecem como sysctls em `dev.myfirst.N.`.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md` e `POWER.md` estão atualizados na sua árvore de trabalho.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados no seu kernel de teste.

Esse é o driver que o Capítulo 23 estende. As adições são modestas em linhas de código-fonte, mas significativas em manutenibilidade: um novo arquivo `myfirst_debug.c`, um header `myfirst_debug.h` correspondente, um banco de macros de logging com toggles em tempo de compilação e em tempo de execução, um conjunto de pontos de probe DTrace definidos estaticamente, um knob sysctl de modo verbose, um conjunto de contadores de debug que distinguem operação normal de eventos anômalos, um bump de versão para `1.6-debug`, um documento `DEBUG.md` que explica o subsistema, e uma pequena coleção de scripts auxiliares que transformam a saída bruta de log e rastreamento em algo que um humano consegue ler.

### O Que Você Aprenderá

Ao final deste capítulo você será capaz de:

- Descrever o que torna a depuração do kernel diferente da depuração no espaço do usuário, em termos concretos, e explicar como essa diferença molda o restante do kit de ferramentas do capítulo.
- Usar `printf()`, `device_printf()`, `device_log()` e `log()` corretamente em código de driver, com entendimento de quando cada um é apropriado e o que os níveis de prioridade de log significam no FreeBSD.
- Ler e filtrar `dmesg`, acompanhar o buffer de mensagens do kernel ao longo do seu ciclo de vida, correlacionar mensagens do boot e em tempo de execução, e saber quando as mensagens que você quer estão em `dmesg`, quando estão em `/var/log/messages` e quando estão nos dois lugares.
- Construir um kernel de debug personalizado com `DDB`, `KDB`, `KDB_UNATTENDED`, `INVARIANTS`, `WITNESS` e opções relacionadas, e saber o que cada opção realmente faz em tempo de execução, o que custa e quando você a quer.
- Entrar no `ddb` deliberadamente durante um teste, executar os comandos básicos de inspeção (`show`, `ps`, `bt`, `trace`, `show alllocks`, `show pcpu`, `show mbufs`), sair do `ddb` corretamente e entender o que você está observando em cada caso.
- Entender o que é um crash dump do kernel, como `dumpdev` e `savecore(8)` produzem um, e como `kgdb` abre um dump para análise post-mortem a um nível suficiente para abri-lo e inspecionar alguns frames.
- Explicar o que é DTrace, carregar o módulo `dtraceall`, listar os providers que o kernel expõe, escrever um script curto que rastreia uma função do kernel, e usar os providers `fbt`, `syscall`, `sched`, `io`, `vfs` e `sdt` para perguntas comuns sobre drivers.
- Adicionar probes SDT (Statically Defined Tracing) a um driver usando `SDT_PROVIDER_DEFINE`, `SDT_PROBE_DEFINE` e as macros `SDT_PROBE`, e usar `dtrace -n 'myfirst:::entry'` para observá-las.
- Executar `ktrace` contra um programa do usuário que se comunica com o driver, ler a saída com `kdump` e mapear syscalls como `open`, `ioctl`, `read` e `write` de volta aos pontos de entrada do driver.
- Reconhecer os padrões característicos de bugs em drivers: alocações não liberadas visíveis em `vmstat -m`, violações de ordem de lock relatadas pelo `WITNESS`, acesso via `bus_space` além do final de uma BAR, chamadas `bus_dmamap_sync` esquecidas antes ou após uma transferência, acesso a um dispositivo após a execução do seu `DEVICE_DETACH`, e os sintomas que cada um produz.
- Refatorar o suporte de depuração do driver em um par dedicado `myfirst_debug.c` e `myfirst_debug.h` com saída verbose togável em tempo de execução, macros ENTRY e EXIT para rastreamento em nível de função, macros ERROR e WARN para relato estruturado de problemas, probes SDT para rastreamento externo, e uma subárvore sysctl para os controles de debug.
- Ler código de debug real de drivers na árvore de código-fonte do FreeBSD, como a família `DPRINTF` em `/usr/src/sys/dev/ath/if_ath_debug.h`, o uso de `bootverbose` em `/usr/src/sys/dev/virtio/block/virtio_blk.c`, as probes SDT em `/usr/src/sys/dev/virtio/virtqueue.c`, e os sysctls de debug em drivers como `/usr/src/sys/dev/iwm/if_iwm.c`.

A lista é longa porque depuração é, ela própria, uma família de habilidades. Cada item é específico e ensinável. O trabalho do capítulo é a composição.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 23 permaneça focado na disciplina de depuração do dia a dia.

- **Scripts avançados para `kgdb` e extensões Python**. O capítulo mostra como abrir um crash dump e inspecionar alguns frames. Construir helpers em Python sobre o `kgdb`, percorrer estruturas de dados do kernel com comandos definidos pelo usuário e automatizar a análise de dumps pertencem a um capítulo avançado posterior.
- **`pmcstat` e contadores de desempenho de hardware**. Essas são ferramentas de ajuste de desempenho, não de depuração no sentido estrito. A Parte 6 retorna a elas.
- **Profiling de locks além do `WITNESS`**. O caminho do `options LOCK_PROFILING` e o provider DTrace `lockstat` são mencionados no contexto, mas o fluxo completo de ajuste para análise de contenção pertence aos capítulos de desempenho da Parte 6.
- **Rastreamento detalhado de eventos com `KTR`**. O capítulo menciona `KTR` em contraste com `ktrace` para que o leitor não confunda os dois, mas o buffer de eventos pesado dentro do kernel e seu fluxo de log circular são material avançado que raramente exige a atenção de um driver.
- **Frameworks de logging com limitação de taxa**. O Capítulo 25 aborda em profundidade as boas práticas de logging, contadores estáticos e limitação de taxa baseada em tempo. O Capítulo 23 introduz informalmente a necessidade de limitação de taxa e passa o assunto adiante.
- **O backend `bhyve debug`, o modo de servidor GDB e breakpoints ativos no nível do kernel**. Esses recursos são possíveis e ocasionalmente úteis, mas representam um nicho avançado e não pertencem ao primeiro capítulo de depuração.
- **Frameworks de injeção de erros e testes de falhas**. `fail(9)`, `fail_point(9)` e recursos relacionados são apresentados brevemente no contexto, mas deixados para o Capítulo 25 e o material de testes que se segue.

Manter-se dentro dessas linhas garante que o Capítulo 23 seja um capítulo sobre como encontrar bugs, e não sobre todos os lugares em que o kernel já tentou ajudá-lo a encontrá-los.

### Tempo de Investimento Estimado

- **Somente leitura**: quatro a cinco horas. As ideias do Capítulo 23 são conceitualmente mais leves do que as do Capítulo 21 ou do Capítulo 22, mas se conectam a muitas ferramentas, e uma primeira passagem pelo levantamento de ferramentas leva tempo.
- **Leitura mais digitação dos exemplos trabalhados**: dez a doze horas ao longo de duas ou três sessões. O driver evolui em três estágios: primeiro o logging baseado em macros e o modo verbose, depois as SDT probes, e por fim a refatoração em um par de arquivos próprio. Cada estágio é curto, e cada estágio vem acompanhado de um laboratório curto que confirma o funcionamento.
- **Leitura mais todos os laboratórios e desafios**: quinze a dezoito horas ao longo de três ou quatro sessões. Os laboratórios incluem uma sessão intencional no `ddb`, um exercício de DTrace que mede a latência do driver, um passo a passo de `ktrace`/`kdump`, um vazamento de memória deliberado que o leitor precisa encontrar com `vmstat -m`, e uma violação intencional de ordem de lock que o leitor precisa reproduzir sob o `WITNESS`.

As Seções 4 e 5 são as mais densas em termos de vocabulário novo. Se os comandos do `ddb` ou a sintaxe do DTrace parecerem opacos na primeira passagem, isso é normal. Pare, execute o exercício correspondente no driver simulado e volte quando a forma estiver assimilada. Depuração é uma daquelas habilidades em que a prática concreta consolida as ideias mais rapidamente do que a leitura adicional.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- Seu código-fonte do driver corresponde ao Estágio 4 do Capítulo 22 (`1.5-power`). O ponto de partida pressupõe todos os primitivos do Capítulo 22: os métodos `device_suspend`, `device_resume` e `device_shutdown`; o campo `suspended` do softc e os contadores de energia; o documento POWER.md; e o script de teste de regressão. O Capítulo 23 constrói sobre tudo isso.
- Sua máquina de laboratório roda FreeBSD 14.3 com `/usr/src` em disco e compatível com o kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` e `DDB_CTF` está compilado, instalado e inicializando corretamente. Essas são exatamente as opções que o Capítulo 22 já solicitou; a Seção 4 deste capítulo explica cada uma delas em detalhe para que você saiba o que fazem.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível. Os laboratórios do Capítulo 23 rodam deliberadamente em uma VM. Vários deles podem provocar um panic no kernel (essa é a lição), e um snapshot de VM é a ferramenta certa para manter o ciclo de feedback curto.
- Os seguintes comandos do espaço do usuário estão no seu path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `vmstat`, `ktrace`, `kdump`, `dtrace`, `procstat`, `savecore`, `kgdb`.
- Você criou um snapshot da sua VM no estado `1.5-power` e nomeou o snapshot de forma clara. Vários laboratórios neste capítulo provocam intencionalmente um panic no kernel; você vai querer conseguir reverter para um estado bom conhecido em menos de um minuto.

Se algum item acima estiver pendente, resolva agora. Depuração, ao contrário de muitos tópicos neste livro, é uma habilidade que recompensa a preparação e pune os atalhos. Uma sessão de laboratório que começa sem snapshot é uma sessão que termina com uma hora de recuperação; um exercício de DTrace que começa sem `KDTRACE_HOOKS` é um exercício que falha silenciosamente.

### Como Aproveitar ao Máximo Este Capítulo

Cinco hábitos compensam mais neste capítulo do que em qualquer capítulo anterior.

Primeiro, mantenha `/usr/src/sys/kern/subr_prf.c`, `/usr/src/sys/kern/kern_ktrace.c`, `/usr/src/sys/kern/subr_witness.c` e `/usr/src/sys/sys/sdt.h` marcados como favoritos. O primeiro define as implementações de `printf`, `log`, `vprintf` e `tprintf` do kernel e é a resposta canônica para qualquer pergunta sobre o que acontece quando você os chama. O segundo é a implementação do `ktrace`. O terceiro é o `WITNESS`, o verificador de ordem de lock que o Capítulo 23 usa intensamente. O quarto contém as macros de SDT probe. Ler cada um deles uma vez no início da seção correspondente é a coisa mais eficaz que você pode fazer para ganhar fluência. Nenhum desses arquivos é longo; o mais extenso tem alguns milhares de linhas e a maior parte é comentário.

Segundo, mantenha três exemplos reais de driver à mão: `/usr/src/sys/dev/ath/if_ath_debug.h`, `/usr/src/sys/dev/virtio/virtqueue.c` e `/usr/src/sys/dev/iwm/if_iwm.c`. O primeiro ilustra o padrão clássico de macro `DPRINTF` do kernel com alternâncias em tempo de compilação e em tempo de execução. O segundo contém SDT probes reais. O terceiro mostra como um driver conecta um sysctl de depuração por meio de `device_get_sysctl_tree()`. O Capítulo 23 faz referência a cada um no momento certo. Lê-los uma vez agora, sem tentar memorizar, fornece ao restante do capítulo âncoras concretas onde as ideias podem se apoiar.

> **Uma nota sobre números de linha.** Quando esses drivers reais forem referenciados mais adiante, a referência estará sempre ancorada em um símbolo nomeado: uma definição de `DPRINTF`, uma macro de SDT probe, um callback específico de sysctl. Esses nomes se manterão válidos nas futuras versões de ponto do FreeBSD 14.x; as linhas onde estão não. Vá até o símbolo no seu editor e confie no que ele reporta; a saída de `WITNESS` de exemplo mais adiante no capítulo cita localizações de código-fonte pelo mesmo motivo: como ponteiros, não como endereços fixos.

Terceiro, digite cada alteração de código no driver `myfirst` à mão. O suporte a depuração é o tipo de código que é fácil de colar e difícil de lembrar depois. Digitar as macros, as definições SDT, o sysctl de modo verbose e a refatoração `myfirst_debug.c` constrói uma familiaridade que uma passagem de copiar e colar não produz. O objetivo não é ter o código; o objetivo é ser a pessoa que conseguiria escrevê-lo novamente do zero em vinte minutos.

Quarto, trabalhe dentro de uma VM, mantenha um snapshot e não tenha medo de provocar um panic no kernel intencionalmente. Vários dos laboratórios do capítulo pedem que você entre no `ddb` em um sistema em execução. Vários outros pedem que você construa um driver que usa um lock de forma incorreta de propósito, e então observe o `WITNESS` pegá-lo. Um panic real em uma VM real é, para prática de depuração, barato e seguro. Um panic real no laptop de desenvolvimento do leitor é caro e irritante. O snapshot é o caminho barato.

Quinto, após concluir a Seção 4, releia as notas de depuração do Capítulo 22 sobre suspend e resume. Os caminhos de suspend e resume são alvos clássicos para o conjunto de ferramentas do Capítulo 23: problemas de lock aparecem no `WITNESS`, erros de estado de DMA aparecem como falhas de `KASSERT` sob o `INVARIANTS`, e condições de corrida aparecem como falhas intermitentes de `devctl suspend`. Ver o material do Capítulo 22 à luz das ferramentas do Capítulo 23 reforça ambos os capítulos.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **Por que a depuração do kernel é desafiadora.** O modelo mental: o que torna os bugs do espaço do kernel diferentes; por que as ferramentas do userland não se transferem; por que mudanças controladas e incrementais são a resposta certa de início; por que você sempre depura a partir de uma posição de reprodutibilidade.
2. **Logging com `printf()` e `device_printf()`.** A ferramenta do dia a dia. O printf do kernel; `device_printf` como a forma preferida para drivers de dispositivo; a família `log(9)` com níveis de prioridade; convenções de formatação; para onde a saída realmente vai; o que registrar e o que omitir. O Estágio 1 do driver do Capítulo 23 estende as linhas de log do `myfirst` para usar `device_printf` de forma consistente e prepara o terreno para as macros de depuração da Seção 8.
3. **Usando `dmesg` e Syslog para diagnóstico.** Acompanhando a saída. O buffer de mensagens do kernel; `dmesg` e seus filtros; `/var/log/messages` e `newsyslog`; a relação entre a saída do console, o `dmesg` e o syslog; o que significa uma mensagem ser "perdida"; como correlacionar mensagens ao longo de um ciclo de boot e de um recarregamento de módulo.
4. **Builds de depuração do kernel e opções.** Os invariantes que o kernel impõe. `options KDB`, `DDB`, `KDB_UNATTENDED`, `KDB_TRACE`; `INVARIANTS` e `INVARIANT_SUPPORT`; `WITNESS` e suas variantes; `KDTRACE_HOOKS` e `DDB_CTF`; um levantamento dos comandos do DDB que um autor de driver usa com mais frequência; uma breve introdução ao `kgdb` em um crash dump. Esta é a seção mais longa do capítulo porque o kernel de depuração é a base sobre a qual o restante se apoia.
5. **Usando DTrace para inspeção ao vivo do kernel.** O bisturi. O que é DTrace; providers e probes; como listá-los com `dtrace -l`; scripts curtos usando `fbt`, `syscall`, `sched`, `io` e `sdt`; como adicionar SDT probes ao driver `myfirst`. O Estágio 2 do driver do capítulo adiciona SDT probes.
6. **Rastreando a atividade do kernel com `ktrace` e `kdump`.** A janela do lado do usuário. O que o `ktrace` registra, o que não registra, como invocá-lo, como interpretar a saída do `kdump`, como acompanhar um programa do usuário enquanto ele cruza para dentro do driver por meio de `open`, `ioctl`, `read` e `write`.
7. **Diagnosticando bugs comuns de driver.** O guia de campo. Vazamentos de memória e `vmstat -m`; violações de ordem de lock e `WITNESS`; padrões de uso incorreto de `bus_space` e `bus_dma`; acesso após o detach; erros no handler de interrupção; erros de ciclo de vida; o checklist de depuração para executar antes de registrar um bug.
8. **Refatorando e versionando com pontos de rastreamento.** A casa arrumada. A divisão final em `myfirst_debug.c` e `myfirst_debug.h`; modo verbose alternável em tempo de execução por meio de sysctl; macros ENTRY/EXIT/ERROR; um pequeno banco de SDT probes; o documento `DEBUG.md`; o incremento de versão para `1.6-debug`.

Após as oito seções, há uma análise estendida dos padrões reais de depuração de driver em `if_ath_debug.h`, `virtqueue.c` e `if_re.c`, um conjunto de laboratórios práticos, um conjunto de exercícios desafio, uma referência de resolução de problemas, um Encerrando que conclui a história do Capítulo 23 e abre o Capítulo 24, uma ponte, um cartão de referência rápida e o glossário habitual de novos termos.

Se esta é sua primeira passagem, leia de forma linear e faça os laboratórios em ordem. Se você estiver revisitando, as Seções 4 e 7 são independentes e fazem boas leituras em uma única sessão. A Seção 5 pressupõe as Seções 1 a 4; não leia DTrace de forma isolada na primeira passagem.



## Seção 1: Por Que a Depuração do Kernel É Desafiadora

Antes das ferramentas, a mentalidade. A Seção 1 descreve o que torna a depuração do kernel diferente do que o leitor talvez já conheça. Um leitor que escreveu scripts de shell, pequenos programas em C ou mesmo um servidor de aplicação multithread provavelmente já usou `printf`, `gdb`, `strace`, `valgrind`, `lldb` ou as ferramentas de desenvolvedor do navegador. Essas ferramentas são excelentes no espaço do usuário, e o modelo mental que elas incentivam é razoável no espaço do usuário. No espaço do kernel, o modelo mental precisa mudar. A Seção 1 descreve essa mudança, nomeia as dificuldades específicas que os autores de driver continuam encontrando e prepara o terreno para o trabalho ferramenta por ferramenta que vem a seguir.

### A Natureza do Código do Kernel

Um driver é, na declaração mais simples possível, código C que roda no espaço de endereçamento do kernel. Esse único fato é responsável pela maior parte das diferenças entre a depuração do kernel e a depuração no userland.

O espaço de endereçamento do kernel contém tudo: o código que o próprio kernel executa, as estruturas de dados que o kernel usa para gerenciar processos, arquivos, memória e dispositivos, as tabelas de páginas de cada processo no sistema, os buffers de cada pacote de rede pendente, as filas de cada requisição de disco e a memória por trás dos registradores mapeados de cada dispositivo. Cada ponteiro que o driver mantém pode potencialmente alcançar qualquer outra parte do kernel. Não há separação como aquela de que os programas do espaço do usuário desfrutam, onde um processo só pode tocar seu próprio espaço de endereçamento e o sistema operacional impõe esse limite.

A consequência é imediata: um único ponteiro errado em um driver pode corromper qualquer estrutura de dados do kernel, não apenas as do próprio driver. Um driver que escreve um byte além do final do seu softc pode acabar escrevendo na tabela de processos do kernel, ou no bitmap de blocos livres do sistema de arquivos, ou na tabela de vetores de interrupção de outro driver. A corrupção frequentemente não produz um panic imediato; ela produz um panic cinco minutos depois, quando outro trecho de código lê a estrutura que o driver silenciosamente danificou. Esse atraso entre causa e sintoma é a razão mais comum pela qual os bugs do kernel parecem mais difíceis do que os bugs do userland. Você não pega o ponteiro errado no momento em que acontece; você pega outro trecho de código tropeçando na bagunça que foi deixada para trás.

O código do kernel também roda sem as redes de proteção habituais. No espaço do usuário, desreferenciar um ponteiro nulo produz uma falha de segmentação, o kernel encerra o processo e o usuário vê uma mensagem. No espaço do kernel, desreferenciar um ponteiro nulo produz uma falha de página, o próprio kernel falha e o tratador dessa falha decide se o sistema consegue continuar. Se a falha de página ocorrer em um ponto onde ela deixaria o kernel em estado inconsistente (dentro de um tratador de interrupção, dentro de uma seção crítica, dentro de `malloc`), o kernel entra em pânico para evitar danos maiores. Em um kernel de desenvolvimento com `INVARIANTS` habilitado, o kernel entra em pânico de forma mais agressiva do que em um kernel de release, porque esse é exatamente o propósito da escolha: fazer o bug se manifestar de forma barulhenta durante o desenvolvimento, em vez de deixá-lo se propagar silenciosamente.

Também não existe um fluxo de saída padrão da forma como os programas do espaço do usuário têm. Um programa C em espaço do usuário chama `printf` e a saída aparece no terminal porque o C runtime conecta essa chamada a uma cadeia que, no final, escreve em um descritor de arquivo que o shell associou ao terminal. No kernel, não há shell, não há terminal no sentido do usuário e não há C runtime. Quando um driver chama `printf`, ele alcança a implementação própria do kernel para `printf`, que escreve no buffer de mensagens do kernel e, dependendo das configurações, no console do sistema. A Seção 2 cobre isso em detalhes; o que importa por ora é que o pipeline de logging do kernel não é o mesmo que os programas do espaço do usuário utilizam, e presumir familiaridade leva à confusão mais adiante. Um driver que "simplesmente chama `printf`" está invocando uma função diferente daquela que um programa userland chama, com semântica diferente e um caminho de saída diferente.

### Riscos de Depuração no Espaço do Kernel

As consequências de uma mudança de depuração incorreta no espaço do usuário costumam ser pequenas. Um `printf` que imprime a variável errada é um inconveniente. Um `printf` dentro de um loop que roda um bilhão de vezes deixa o programa mais lento, mas não causa dano nenhum. Um uso incorreto de `malloc` em um trecho de depuração vaza alguns bytes, e o processo eventualmente termina, levando o vazamento consigo.

As consequências de uma mudança de depuração incorreta no espaço do kernel podem ser muito maiores. Um `printf` no caminho errado pode provocar um deadlock no kernel, se o caminho do printf precisar de um lock que o contexto chamador já adquiriu. Um `printf` em um handler de interrupção que chama código que pode dormir pode causar um panic com `INVARIANTS`. Uma mensagem de depuração que chama um alocador que dorme (`malloc(..., M_WAITOK)` em vez de `M_NOWAIT`) enquanto se mantém um spinlock vai deixar o `WITNESS` muito insatisfeito, e se `WITNESS_KDB` estiver habilitado, vai derrubar a máquina no kernel debugger. Um contador global usado para fins de depuração que é incrementado sem sincronização adequada é, por si só, uma fonte de bugs. Cada um desses é um erro real que autores de drivers cometem; cada um é uma lição que este capítulo vai ensinar.

A nuance é que código de depuração é, estritamente falando, código, e precisa ser tão correto quanto qualquer outro código do driver. "Vou limpar isso depois" não é uma atitude segura no contexto do kernel. Uma mudança de depuração feita às pressas pode introduzir um novo bug mais difícil de encontrar do que o bug original. O Capítulo 23 ensina hábitos que evitam essa armadilha: use macros que são removidas na compilação quando desabilitadas; use a própria facilidade de log do kernel em vez de inventar uma; envolva blocos condicionais para que o código no modo verboso só seja executado quando esse modo estiver ativo; e teste o código de depuração com o mesmo cuidado que o código de produção.

Outro risco é observacional: adicionar saída de depuração pode alterar o comportamento que você está tentando observar. Uma condição de corrida que se reproduz de forma confiável sem saída de depuração pode desaparecer completamente quando você adiciona chamadas a `printf`, porque os printfs serializam através de um lock e acabam ocultando a janela da corrida. Um bug de desempenho pode parecer diferente após a instrumentação, porque a própria instrumentação adiciona custo. Desenvolvedores do kernel chamam isso de problema de Heisenberg da depuração do kernel. As ferramentas deste capítulo foram escolhidas em parte porque minimizam esse problema: DTrace, probes SDT e KTR adicionam pouquíssimo overhead quando inativos; `device_printf` é verboso, mas consistente. Espalhar `printf("HERE\n")` pelo código é sempre uma opção, mas como hábito custa mais do que ajuda.

### Diferenças em Relação à Depuração no Userland

Vale a pena detalhar, ferramenta por ferramenta, a diferença entre a depuração no espaço do kernel e no espaço do usuário.

Um usuário pode executar um programa sob o `gdb`, definir um breakpoint e ver o programa parar. Não é possível fazer isso diretamente com um kernel em execução, porque um kernel pausado significa um sistema pausado. O equivalente aproximado é o `kgdb` operando sobre um crash dump, que permite inspecionar o estado do kernel no momento do panic, mas não consegue avançar passo a passo. O outro equivalente aproximado é o `ddb`, o debugger embutido no kernel, que permite pausar um kernel em execução, mas que roda como parte do próprio kernel e possui um conjunto limitado de comandos. Ambos são úteis; nenhum se comporta da forma como o `gdb` se comporta em um processo em execução.

Um usuário pode usar o `strace` no Linux ou o `truss` no FreeBSD para observar as syscalls de um programa. O `ktrace` do FreeBSD é o equivalente mais próximo; ele registra um trace das syscalls, sinais, eventos namei, I/O e trocas de contexto de um processo em um arquivo que o `kdump` consegue ler. Mas o `ktrace` enxerga o lado do usuário da interface do driver, não os internos do driver. Ele pode mostrar que o processo do usuário chamou `ioctl(fd, CMD_FOO, ...)`, mas não o que aconteceu dentro da implementação do `ioctl` no driver. A Seção 6 dedica tempo ao `ktrace` justamente porque é a ferramenta que mostra essa fronteira com mais clareza.

Um usuário pode usar o `valgrind` para detectar erros de memória e vazamentos em seu programa. Não existe equivalente direto para o kernel. Os análogos mais próximos são o `vmstat -m` para resumir alocações do kernel por tipo, a opção `WITNESS` para erros de ordem de lock, `INVARIANTS` para verificações de consistência que o desenvolvedor inseriu no código, e `MEMGUARD` para um alocador de memória com depuração mais pesada. A Seção 7 percorre os padrões práticos.

Um usuário pode perfilar um programa com `perf` no Linux ou `gprof` no FreeBSD. O FreeBSD oferece `pmcstat` e DTrace para perfilamento. O DTrace é a ferramenta mais flexível para o tipo de perguntas que um desenvolvedor de drivers faz, e o Capítulo 23 se concentra em DTrace em vez de `pmcstat`.

Um usuário muitas vezes pode simplesmente reexecutar um programa para ver se o bug ocorre novamente. Desenvolvedores do kernel frequentemente não podem; um kernel que entrou em panic precisa ser reinicializado, e bugs que aparecem apenas sob carga, ou que dependem de timing, podem levar horas para se reproduzir. A resposta a essa dificuldade é incorporar reprodutibilidade ao processo: tirar um snapshot da VM, criar um script para o caso de teste e registrar tudo. Cada ferramenta deste capítulo existe para apoiar essa resposta.

Uma última diferença é cultural. A comunidade de desenvolvimento do kernel construiu um vocabulário específico em torno da depuração que vale a pena aprender por si só. Termos como "lock order", "critical section", "preemption disabled", "epoch", "giant lock", "sleepable context", "non-sleepable context" e "interrupt context" têm significados precisos no FreeBSD, e usá-los corretamente faz parte de ser fluente em depuração do kernel. O Capítulo 23 os utiliza ao longo do texto; o glossário ao final reúne os que aparecem com mais frequência.

### A Importância de Mudanças Controladas e Incrementais

No espaço do usuário, quando um programa se comporta de forma inesperada, o primeiro impulso costuma ser mudar várias coisas ao mesmo tempo e ver se o comportamento muda. No espaço do kernel, essa é uma estratégia perdedora. Cada mudança é potencialmente um novo bug. Cada recompilação exige uma reinicialização da VM. Cada ciclo de teste consome tempo. Um desenvolvedor de driver que muda cinco coisas entre compilações passa horas tentando descobrir qual mudança causou o novo sintoma, porque nenhuma alteração foi isolada.

O padrão correto é a **mudança controlada e incremental**. Faça uma mudança. Recompile. Teste. Observe o efeito. Anote o que aconteceu. Decida a próxima mudança com base nas evidências. Avance. Isso é mais lento por mudança e mais rápido por correção que funciona, porque você nunca precisa desfazer uma mudança combinada quando uma das cinco correções introduziu um novo bug. A disciplina parece artificial na primeira vez. Depois de uma dúzia de sessões, ela deixa de parecer artificial e começa a parecer a única forma de progredir.

Uma disciplina relacionada é **sempre começar de um estado reconhecidamente bom**. Não depure em um driver que já está em um estado que você não verificou. Não inicie uma sessão aplicando seis mudanças locais e depois executando um teste. Confirme (commit) seu baseline, verifique que ele funciona, e só então comece o novo trabalho. Se o novo trabalho der errado, `git checkout .` o devolve ao estado bom conhecido, e você pode começar de novo. Um autor de driver sem esse hábito passa metade de cada sessão de depuração se perguntando se o baseline ou a nova mudança é que está com defeito.

Uma terceira disciplina é **reproduzir antes de corrigir**. Um bug que não pode ser reproduzido não pode ser comprovadamente corrigido. O primeiro trabalho de uma sessão de depuração não é corrigir o bug; é produzir uma forma curta e confiável de acionar o bug, idealmente em um script. Uma vez que o gatilho é confiável, a correção se torna direta: aplique o candidato, re-execute o gatilho, observe se o bug desapareceu. Sem um gatilho confiável, você está apenas chutando.

Uma quarta disciplina é **casos de reprodução pequenos e com script**. A diferença entre um bug que leva uma hora para ser corrigido e um bug que leva uma semana é frequentemente o tamanho do script de reprodução. Um bug que você pode reproduzir com cinco linhas de shell é um bug que você pode bisectar com `git bisect`. Um bug que só se reproduz após três horas de uso complexo é um bug que você vai ter medo de sequer começar a depurar. O tempo gasto reduzindo uma reprodução é tempo bem gasto.

Os laboratórios do Capítulo 23 são deliberadamente pequenos. Os panics são acionados com três linhas. Os scripts DTrace têm cinco linhas. As demonstrações com `ktrace` usam um programa C de uma linha. Isso não é acidental: é o mesmo padrão que uma boa depuração de drivers usa em qualquer escala. Reproduções mínimas e mudanças mínimas escalam para bugs reais.

### A Mudança de Mentalidade

O que muda quando um desenvolvedor passa do espaço do usuário para o espaço do kernel não são apenas as ferramentas; são as premissas. No espaço do usuário, é razoável assumir que o kernel vai capturar seus erros, que o runtime vai limpar depois de você, e que o pior caso geralmente é um programa que travou e você pode reiniciar. No espaço do kernel, essas premissas estão erradas. O kernel não captura nada; o runtime é a coisa que você está escrevendo; o pior caso é um panic que derruba tudo.

Isso não é motivo para ter medo. É motivo para ser cuidadoso. Cuidadoso, aqui, tem um significado específico: um autor de driver escreve código que poderia ser depurado por outra pessoa, em seis meses, apenas a partir dos logs e do código-fonte. Código cuidadoso usa nomes de funções claros. Código cuidadoso registra o que faz nos níveis de detalhe que são úteis. Código cuidadoso verifica seus próprios invariantes. Código cuidadoso é estruturado de forma que, quando quebra, a quebra é óbvia. Código cuidadoso não tenta ser esperto; código esperto é código que você não consegue depurar sob pressão.

O Capítulo 23 ensina as ferramentas de depuração do kernel, mas o hábito que ele realmente ensina é o cuidado. Cada ferramenta do capítulo é mais útil quando aplicada por um desenvolvedor cuidadoso em código cuidadoso. A mesma ferramenta aplicada a código descuidado produz informação que você não consegue interpretar. O ponto não é memorizar a sintaxe do DTrace ou os comandos do `ddb`; o ponto é desenvolver o hábito de depurar a partir de evidências, em pequenos passos, contra um baseline conhecido, com bons logs, de uma forma que qualquer colaborador possa acompanhar.

### Encerrando a Seção 1

A Seção 1 estabeleceu o contorno do problema. O código do kernel roda em um único espaço de endereçamento compartilhado, sem as redes de segurança em que o userland confia, com modos de falha que danificam o sistema inteiro. As ferramentas de depuração do kernel são diferentes, mais custosas e menos interativas do que suas equivalentes no userland. A disciplina de mudança controlada, incremental e reproduzível não é opcional; é a única estratégia que funciona. A mudança de mentalidade exigida é real, mas é aprendível, e é o alicerce sobre o qual as demais ferramentas do capítulo se sustentam.

A Seção 2 inicia o levantamento de ferramentas com a mais simples e universal que todo driver acaba usando: a família `printf` do kernel.



## Seção 2: Registro com `printf()` e `device_printf()`

A família `printf` do kernel é a ferramenta de depuração mais humilde do conjunto. É também a mais usada, com larga margem. Quase todo driver do FreeBSD em `/usr/src/sys/dev` chama `device_printf` em algum lugar. Quase toda mensagem de boot que um usuário já viu foi escrita por um `printf` do kernel. Antes do DTrace, antes do `ddb`, antes das probes SDT, antes dos kernels de depuração, um autor de driver recorre ao `device_printf` porque ele está sempre disponível, é sempre seguro (sujeito às ressalvas que esta seção vai detalhar) e é compreendido por todos os outros autores de drivers no mundo.

Esta seção ensina como usar essa família de funções de forma deliberada. O objetivo não é apenas mostrar como chamar `device_printf`, porque essa parte é trivial. O objetivo é construir o hábito de usar o registro como uma funcionalidade projetada do driver, e não como um conjunto disperso de notas de depuração que se acumulam ao longo do tempo e nunca são removidas.

### Fundamentos do `printf()` no Contexto do Kernel

O `printf` do kernel do FreeBSD vive em `/usr/src/sys/kern/subr_prf.c`. Sua assinatura é familiar:

```c
int printf(const char *fmt, ...);
```

Ela recebe uma string de formato e uma lista de argumentos variáveis, e escreve o texto formatado no buffer de mensagens do kernel. O buffer é uma área circular de memória, geralmente com cerca de 192 KB em um sistema FreeBSD moderno, na qual cada mensagem do kernel é gravada. O kernel não mantém um log permanente por conta própria; é o daemon `syslogd(8)` do espaço do usuário que lê o buffer e grava as linhas selecionadas em `/var/log/messages`. A seção 3 aborda o pipeline completo.

A string de formato compreende a maioria das mesmas conversões que o `printf` do userland compreende: `%d`, `%u`, `%x`, `%s`, `%c`, `%p`, entre outros. Ela também compreende alguns extras específicos do kernel, como `%D` para imprimir um dump hexadecimal de um buffer de bytes (com um argumento separador). Não compreende conversões de ponto flutuante como `%f` e `%g`, porque o kernel não usa aritmética de ponto flutuante por padrão e o código de conversão está intencionalmente ausente. Tentar imprimir um `double` ou `float` dentro do kernel é quase sempre um erro, e o compilador geralmente emite um aviso sobre isso.

O `printf` do kernel tem propriedades de segurança diferentes das do `printf` do userland. O `printf` do userland pode alocar memória, pode adquirir locks dentro do runtime C e pode bloquear em I/O. O `printf` do kernel não pode alocar; não pode bloquear; deve ser seguro de chamar de praticamente qualquer contexto. A implementação em `subr_prf.c` é deliberadamente simples: ela formata o texto em um pequeno buffer na pilha, copia o resultado para o buffer de mensagens, acorda o `syslogd` quando apropriado e retorna. Essa simplicidade é parte do motivo pelo qual `printf` é a ferramenta de fallback universal.

Há alguns contextos em que mesmo o `printf` do kernel não é seguro. Handlers de interrupção rápida, por exemplo, são executados antes de o kernel ter estabilizado o estado do console, e um `printf` chamado de dentro de uma interrupção rápida pode causar um travamento ou um deadlock em algumas plataformas. Caminhos de execução que mantêm um lock que o próprio caminho do `printf` também tenta adquirir podem causar deadlock igualmente. Na prática, autores de drivers evitam o problema usando `printf` apenas nos pontos de entrada normais do driver (attach, detach, ioctl, read, write, suspend, resume) e em tasks de interrupção (a metade diferida de um pipeline de interrupção filter-mais-task, conforme o Capítulo 19 apresentou), e não a partir do próprio filter. O driver `myfirst` já segue esse padrão; o Capítulo 23 o preserva.

### Uso Preferencial: `device_printf()` para Logging Estruturado com Identificação de Dispositivo

O `printf` simples é genérico. Um desenvolvedor de drivers pode fazer melhor. O kernel fornece `device_printf`, declarado em `/usr/src/sys/sys/bus.h`:

```c
int device_printf(device_t dev, const char *fmt, ...);
```

A função prefixa cada mensagem com o nome do dispositivo, normalmente o nome do driver concatenado com o número de unidade: `myfirst0: `, `em1: `, `virtio_blk0: `. Esse prefixo não é apenas cosmético. É a informação mais importante do log do kernel em um sistema com múltiplos dispositivos. Sem ele, uma mensagem como "interrupt received" não tem nenhuma utilidade em uma máquina com oito placas de rede e três controladores de armazenamento. Com ele, "myfirst0: interrupt received" pode ser localizado imediatamente em qualquer ferramenta que leia logs.

A regra é simples: **em código de driver, prefira `device_printf` em vez de `printf`**. Em todo lugar onde o driver tiver um `device_t` em escopo, use `device_printf`. Em todo lugar onde o driver não tiver um `device_t` disponível (uma função de inicialização global, por exemplo), use `printf` com um prefixo explícito:

```c
printf("myfirst: module loaded\n");
```

Esse é o padrão que torna o log do kernel legível em qualquer sistema. É também o padrão seguido por quase todos os drivers do FreeBSD. Abra qualquer arquivo em `/usr/src/sys/dev/` e o padrão ficará visível já na primeira função attach.

O driver `myfirst` já usa `device_printf` na maior parte dos lugares, pois o Capítulo 18 introduziu essa convenção quando o driver se tornou um dispositivo PCI pela primeira vez. A Seção 8 deste capítulo envolve `device_printf` em macros que adicionam severidade, categoria e um controle de modo detalhado, mas a função subjacente permanece a mesma. Nada no Capítulo 23 substitui `device_printf`; o capítulo apenas o aprofunda.

### `device_log()` e Níveis de Prioridade de Log

`device_printf` é simples: ele escreve a mensagem na prioridade de log padrão do kernel (LOG_INFO, em termos de syslog). Às vezes, um driver precisa escrever com uma prioridade mais alta, para sinalizar um problema grave, ou com uma prioridade mais baixa, para marcar uma mensagem informativa de rotina que normalmente deveria ser filtrada.

O FreeBSD 14 expõe `device_log` para esse fim, também em `/usr/src/sys/sys/bus.h`:

```c
int device_log(device_t dev, int pri, const char *fmt, ...);
```

O argumento `pri` é uma das constantes de prioridade do syslog definidas em `/usr/src/sys/sys/syslog.h`:

- `LOG_EMERG` (0): sistema inutilizável
- `LOG_ALERT` (1): ação imediata necessária
- `LOG_CRIT` (2): condições críticas
- `LOG_ERR` (3): condições de erro
- `LOG_WARNING` (4): condições de aviso
- `LOG_NOTICE` (5): condição normal, mas significativa
- `LOG_INFO` (6): informativo
- `LOG_DEBUG` (7): mensagens de nível de depuração

Para o desenvolvimento de drivers, as quatro prioridades que importam na prática são `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE` e `LOG_INFO`. Um driver raramente precisa de `LOG_EMERG`, `LOG_ALERT` ou `LOG_CRIT`, pois a maioria das falhas de driver é local ao próprio driver, não ao sistema como um todo. `LOG_DEBUG` é útil durante a depuração ativa; o Capítulo 23 o utiliza para a saída no modo detalhado.

A função subjacente `log(9)`, também em `subr_prf.c`, é mais simples: `void log(int pri, const char *fmt, ...)`. `device_log` envolve `log` e adiciona o prefixo com o nome do dispositivo.

A diferença prática entre `printf` e `log` está no destino da saída quando o kernel está configurado de forma conservadora. Por padrão, o FreeBSD envia tudo com prioridade igual ou superior a `LOG_NOTICE` para o console; `LOG_INFO` e abaixo vão para o buffer de mensagens do kernel, mas não para o console. Isso importa em dois momentos. Primeiro, durante o boot: uma mensagem de alta prioridade é visível na tela durante o boot; uma mensagem `LOG_INFO` não é. Segundo, durante a operação: um erro grave em um sistema em produção deve aparecer no console (para que um sysadmin que esteja monitorando o arquivo `messages` com `tail` o veja imediatamente), enquanto uma mensagem de rotina de probe ou attach não deve poluir a tela.

O driver `myfirst` do Capítulo 23 usa esta convenção:

- Sucesso no probe e no attach: `device_printf` (que equivale a `LOG_INFO`).
- Transições de suspend, resume e desligamento: `device_printf` (rotina).
- Erros de hardware, contagens inesperadas de interrupções, falhas de DMA e estados inválidos do dispositivo: `device_log(dev, LOG_ERR, ...)` ou `device_log(dev, LOG_WARNING, ...)`.
- Saída no modo detalhado (habilitada por um sysctl): `device_log(dev, LOG_DEBUG, ...)` dentro das macros de depuração.

### O Que Registrar e O Que Omitir

Saber o que não registrar importa tanto quanto saber o que registrar. Um driver com saída excessiva polui o `dmesg`, desperdiça espaço no buffer de mensagens e empurra mensagens informativas mais antigas para fora do buffer circular antes que alguém as leia. Um driver com saída insuficiente fica silencioso quando algo dá errado e não oferece ao desenvolvedor nenhum ponto de partida para investigação.

A regra geral para o driver `myfirst` do Capítulo 23 é:

**Sempre registre, no nível `device_printf` (LOG_INFO):**

- Um banner de uma linha no attach, informando o nome do driver, a versão e os recursos principais alocados (tipo de interrupção, quantidade de vetores MSI-X, tamanho do buffer de DMA). O leitor deve ser capaz de ver de relance o que o driver fez.
- Uma nota de uma linha no detach. O detach normalmente é silencioso, mas registrar sua conclusão é útil quando um driver trava ao ser descarregado e o desenvolvedor precisa saber se o detach ao menos foi iniciado.
- Uma nota de uma linha em cada suspend, resume e desligamento. Transições de energia são importantes, e o desenvolvedor frequentemente precisa correlacionar um bug com uma transição que o antecedeu.

**Sempre registre, usando `device_log` com `LOG_ERR` ou `LOG_WARNING`:**

- Qualquer falha na alocação de um recurso (interrupção, tag de DMA, buffer de DMA, mutex). O caminho de erro deve sempre indicar o que falhou.
- Qualquer erro de hardware detectado pelo driver: uma resposta inesperada do dispositivo, um timeout, um valor de registrador que indica falha no hardware.
- Qualquer inconsistência de estado detectada pelo driver: uma conclusão de DMA para uma transferência que não foi iniciada, um suspend que encontra o dispositivo já suspenso, um detach que encontra o dispositivo ainda com uma referência de cliente ativa.

**Nunca registre no caminho normal de operação:**

- Cada interrupção. Mesmo que o dispositivo esteja ocioso, um driver que registra cada interrupção inunda o log em questão de segundos sob carga real. Use DTrace ou contadores para observabilidade por interrupção.
- Cada transferência de DMA. Pelo mesmo motivo.
- Cada chamada `read` ou `write`. Pelo mesmo motivo.
- Entrada e saída de funções internas em produção. Use as macros de modo detalhado (Seção 8) para que o desenvolvedor possa ativar ou desativar o rastreamento de entrada e saída.

**Registre no modo detalhado (habilitado pelas macros da Seção 8 e pelo sysctl de depuração):**

- Entrada e saída das funções principais, com um breve resumo dos argumentos.
- Eventos individuais de interrupção com o valor do registrador de status de interrupção.
- Submissões e conclusões de transferências de DMA.
- Operações de sysctl e ioctl e os valores que retornaram.

A linha entre "sempre" e "modo detalhado" é uma decisão de julgamento, e diferentes drivers a traçam de formas distintas. `/usr/src/sys/dev/re/if_re.c` registra muito pouco em produção e usa um sysctl `re_debug` para habilitar saída detalhada. `/usr/src/sys/dev/virtio/block/virtio_blk.c` usa `bootverbose` para controlar detalhes extras no momento do attach. `/usr/src/sys/dev/ath/if_ath_debug.h` define uma máscara sofisticada por subsistema que habilita seletivamente diferentes categorias de saída. O driver `myfirst` do Capítulo 23 adota um padrão mais simples, suficiente para fins didáticos e extensível quando o leitor começar a trabalhar em um driver real.

### Rastreando o Fluxo de Funções Sem Poluir o Log

Um dos idiomas de depuração que parece atraente, mas que causa mais mal do que bem, é inserir um `device_printf` no início e no fim de cada função. A ideia é válida: se você consegue ver a sequência de chamadas, muitas vezes consegue deduzir o que está acontecendo. A prática, porém, é inadequada, porque a sequência produz uma quantidade avassaladora de saída, e essa saída é serializada pelo caminho do printf, o que altera o timing do código sendo testado.

O padrão correto é usar um **modo detalhado controlado por um sysctl**. Defina um par de macros:

```c
#define MYF_LOG_ENTRY(sc)       \
    do {                        \
        if ((sc)->debug_verbose) \
            device_printf((sc)->dev, "ENTRY: %s\n", __func__); \
    } while (0)

#define MYF_LOG_EXIT(sc, err)   \
    do {                        \
        if ((sc)->debug_verbose) \
            device_printf((sc)->dev, "EXIT: %s err=%d\n", __func__, (err)); \
    } while (0)
```

Em seguida, no topo de cada função que o driver deseja rastrear, chame `MYF_LOG_ENTRY(sc);`. Em cada retorno, chame `MYF_LOG_EXIT(sc, err);`. Quando o flag `debug_verbose` é zero (o padrão), as macros compilam para um branch que nunca é executado e têm custo essencialmente nulo. Quando o flag é um (habilitado em tempo de execução com `sysctl dev.myfirst.0.debug_verbose=1`), as macros produzem um rastreamento que qualquer desenvolvedor de drivers consegue ler.

O flag booleano mostrado acima é deliberadamente simples. Ele introduz a ideia de um controle ajustável em tempo de execução sem exigir que o leitor aprenda um novo layout ao mesmo tempo. A Seção 8 deste capítulo desenvolve esse idioma em um banco completo de macros, substitui o booleano simples por uma máscara de bits de 32 bits (para que o operador possa habilitar apenas o rastreamento de interrupções, ou apenas o rastreamento de I/O, em vez de tudo ao mesmo tempo), move toda a estrutura para `myfirst_debug.h` e adiciona probes SDT junto. Por enquanto, o padrão a internalizar é que a saída de nível de rastreamento deve estar **desabilitada por padrão** e ser **ajustável em tempo de execução**. Flags de depuração em tempo de compilação (o antigo estilo `#ifdef DEBUG`) são uma opção inferior: é preciso recompilar o módulo para alterá-los. Os controles em tempo de execução permitem que um desenvolvedor habilite o modo detalhado em um sistema em funcionamento exatamente quando precisar e o desabilite no momento em que as evidências forem coletadas.

### Redirecionando Logs para `dmesg` e `/var/log/messages`

O `printf` do kernel escreve no buffer de mensagens do kernel. Esse buffer é o que `dmesg(8)` lê quando o usuário o executa. A Seção 3 aborda `dmesg` em detalhes; a versão resumida é que o buffer é circular, portanto mensagens antigas acabam sendo sobrescritas por novas à medida que o buffer se enche.

O daemon `syslogd(8)` lê as novas mensagens do buffer e as grava no arquivo de syslog apropriado, geralmente `/var/log/messages`. O mapeamento entre prioridade e arquivo é configurado em `/etc/syslog.conf`. Por padrão, mensagens com prioridade `LOG_INFO` ou superior vão para `/var/log/messages`; mensagens `LOG_DEBUG` são descartadas, a menos que explicitamente habilitadas. `/var/log/messages` é rotacionado pelo `newsyslog(8)` de acordo com as regras em `/etc/newsyslog.conf`; essa rotação é o que produz os arquivos `messages.0.gz`, `messages.1.gz`, e assim por diante, que o leitor encontrará no diretório de logs de um sistema em funcionamento por longo tempo.

A consequência prática é que a saída de log de um driver existe em dois lugares ao mesmo tempo. Uma mensagem recente está no buffer do kernel (acessível via `dmesg`) e em `/var/log/messages` (acessível por qualquer ferramenta de leitura de arquivos). Uma mensagem mais antiga pode ter saído do buffer, mas ainda estar em `/var/log/messages` ou nos seus arquivos de rotação. Uma mensagem muito antiga pode ter sido rotacionada para fora de `/var/log/messages` e estar em um arquivo `messages.N.gz`, podendo ser lida com `zcat`, `zgrep` ou `zless`. A Seção 3 percorre essas ferramentas.

Há uma sutileza que vale mencionar aqui, pois ela costuma pegar novos desenvolvedores de drivers de surpresa: **uma mensagem `LOG_DEBUG` produzida pelo kernel não é gravada em `/var/log/messages` por padrão**. Se a saída de depuração do leitor estiver em `LOG_DEBUG` e ele não conseguir encontrá-la no arquivo de log, a causa geralmente é o `/etc/syslog.conf`, não o driver. Um teste rápido: execute o driver e depois execute `dmesg | grep myfirst`. Se a mensagem aparecer no `dmesg` mas não em `/var/log/messages`, ela foi produzida mas filtrada pelo syslog. Habilitar `LOG_DEBUG` em `/etc/syslog.conf` (se o syslog estiver configurado para processar mensagens de depuração do kernel) é uma solução; usar `device_printf` ou `device_log(dev, LOG_INFO, ...)` é a solução mais comum, pois contorna o problema usando uma prioridade mais alta.

### Uma Demonstração Concreta: Logging no Driver `myfirst`

O driver `myfirst` no início do Capítulo 23 já possui linhas de log nos lugares óbvios: o attach imprime um banner, o detach confirma a conclusão, o suspend e o resume registram suas transições, e os erros de DMA registram um aviso. O Estágio 1 do Capítulo 23 aprimora isso de três formas específicas.

Primeiro, todo `printf` dentro do driver que tem um `device_t` em escopo se transforma em `device_printf`. Um grep rápido encontra cerca de oito linhas que ainda usam `printf` simples nos handlers `MOD_LOAD` e `MOD_UNLOAD` do módulo. Os que ficam no nível do módulo permanecem como `printf` (eles não têm um `device_t`), mas usam o prefixo literal `"myfirst: "` para que o leitor ainda possa encontrá-los no log. Um driver que chama `printf` sem prefixo em qualquer lugar é um driver que produz linhas de log impossíveis de rastrear.

Segundo, todo caminho de erro se torna um `device_log` com uma prioridade explícita:

```c
/* Before */
if (error) {
    device_printf(dev, "bus_dma_tag_create failed: %d\n", error);
    return (error);
}

/* After */
if (error) {
    device_log(dev, LOG_ERR, "bus_dma_tag_create failed: %d\n", error);
    return (error);
}
```

A mudança é pequena, e o comportamento é o mesmo na maioria dos kernels de teste. O valor está na prioridade. Um sistema em produção monitorando mensagens `LOG_ERR` passará a ver os erros do driver; um ambiente de desenvolvimento filtrando em `LOG_INFO` verá tudo.

Terceiro, um novo padrão de logging para mudanças de estado "raras mas importantes" é introduzido. Especificamente, o driver do Capítulo 23 registra em `LOG_NOTICE` três eventos que o leitor pode querer acompanhar, mas para os quais não quer ver cada interrupção individual: a primeira transferência DMA bem-sucedida após o attach, uma transição para runtime suspend detectada por inatividade, e o primeiro resume bem-sucedido após um suspend. Esses não são erros, mas também não são eventos rotineiros; são exatamente o que um desenvolvedor lendo o log quer ver destacado.

As mudanças exatas de código para o Estágio 1 estão detalhadas no exercício prático da Seção 2, ao final desta seção, e os arquivos correspondentes estão em `examples/part-05/ch23-debug/stage1-logging/`.

### Uma Nota sobre `uprintf` e `tprintf`

Duas funções irmãs aparecem em `subr_prf.c` que autores de drivers ocasionalmente veem, mas raramente utilizam.

`uprintf(const char *fmt, ...)` escreve uma mensagem no terminal controlador do processo do usuário atual, caso exista um, *em vez de* no buffer de mensagens do kernel. É útil em situações raras em que o handler de `ioctl` de um driver precisa produzir uma mensagem de erro que apenas o usuário que executa o comando verá, sem poluir o log do sistema. Na prática, a maioria dos drivers retorna um código de erro pelo mecanismo habitual de `errno` e não utiliza `uprintf`. A árvore `/usr/src/sys/kern/` tem alguns poucos chamadores, principalmente em lugares onde o kernel quer alertar um usuário sobre sua própria ação (um evento com relevância para a segurança, por exemplo).

`tprintf(struct proc *p, int pri, const char *fmt, ...)` escreve no terminal de um processo específico. É ainda mais raro. O Capítulo 23 não utiliza nenhuma das duas funções diretamente. O material de logging do Capítulo 25 retorna a elas no contexto de mensagens de diagnóstico voltadas ao usuário.

### Convenções de Formatação

Alguns pequenos hábitos de formatação tornam a saída de log muito mais fácil de ler:

1. **Termine toda linha com `\n`**. O `printf` do kernel não adiciona uma quebra de linha. Uma mensagem sem quebra de linha se fundirá com a próxima em `dmesg`, produzindo saída como `myfirst0: interrupt receivedmyfirst0: interrupt received`, que é quase impossível de interpretar visualmente.

2. **Use hexadecimal para valores de registradores e decimal para contadores**. `status=0x80a1` é um registrador de dispositivo; `count=27` é um contador. Misturar as duas convenções em um único driver produz saída difícil de ler de relance. As conversões `%x` e `%d` são a maneira mais simples de mantê-los visualmente distintos.

3. **Coloque os valores das variáveis à direita, não à esquerda**. `myfirst0: dma transfer failed (err=%d)` é mais fácil de escanear do que `myfirst0: %d was the err from dma transfer failed`. Leitores humanos leem da esquerda para a direita, e colocar os dados variáveis no final da linha segue esse fluxo natural.

4. **Use tags curtas para categorias de mensagens repetidas**. `INFO:`, `WARN:`, `ERR:`, `DEBUG:` no início do conteúdo após o prefixo do dispositivo transformam um log em algo que um `grep` consegue filtrar. A Seção 8 usa essa convenção em suas macros.

5. **Não inclua timestamps na mensagem**. O kernel e o syslog adicionam seus próprios timestamps; um timestamp manual produz informação duplicada e ruído visual.

6. **Não inclua o nome da função em mensagens de produção**. Nomes de funções são úteis na saída em modo verbose (onde as macros de entrada e saída os imprimem automaticamente), mas desperdiçam espaço em cada mensagem de produção. Uma mensagem como `myfirst0: DMA completed` é mais legível do que `myfirst0: myfirst_dma_completion_handler: DMA completed`. Se você quiser nomes de funções, ative o modo verbose.

7. **Siga o estilo do driver**. Se o restante do driver usa "DMA" em maiúsculas, use "DMA" em cada linha de log. Se o restante usa "dma" em minúsculas, mantenha essa escolha. Consistência é mais fácil de filtrar com `grep` do que um estilo misto.

Esses são pequenos hábitos, mas compensam na primeira vez que um log se torna a única evidência de um bug que ocorreu em um sistema ao qual você não tem acesso interativo.

### Exercício: Adicione `device_printf()` em Pontos-Chave do Seu Driver

O exercício prático que acompanha a Seção 2 é curto. Você deve:

1. Abrir `myfirst_pci.c`, `myfirst_sim.c`, `myfirst_intr.c`, `myfirst_dma.c` e `myfirst_power.c`, um por um.

2. Para cada chamada a `printf` que tenha um `device_t` ou um softc (que contém um `device_t`) no escopo, substituí-la por `device_printf(dev, ...)`.

3. Para cada mensagem em caminho de erro, alterar a chamada para `device_log(dev, LOG_ERR, ...)` se a condição for um erro real, ou `device_log(dev, LOG_WARNING, ...)` se a condição for recuperável. Manter `device_printf` para mensagens informativas de rotina.

4. Para o banner de attach, adicionar uma única linha que nomeia a versão do driver: `device_printf(dev, "myfirst PCI driver %s attached\n", MYFIRST_VERSION);`. Definir `MYFIRST_VERSION` no topo de `myfirst_pci.c` como um literal de string. O Capítulo 23 vai alterá-lo de `"1.5-power"` para `"1.6-debug"` na refatoração final da Seção 8; por enquanto, mantenha-o em `"1.5-power-stage1"`.

5. Reconstruir o módulo, carregá-lo e verificar que `dmesg | grep myfirst0` exibe o banner de attach. Gerar uma falha de DMA forçada (o sysctl `sim_force_dma_error` do simulador é uma maneira; o Capítulo 17 introduziu esse gancho) e verificar que a mensagem de erro aparece com a prioridade `LOG_ERR`.

6. Fazer commit das alterações com uma mensagem como `"Chapter 23 Section 2: use device_printf consistently"`.

Sinta-se à vontade para conferir seu trabalho comparando com `examples/part-05/ch23-debug/stage1-logging/myfirst_pci.c` e os demais arquivos do stage1, mas o aprendizado acontece na digitação, não na verificação.

### Encerrando a Seção 2

A Seção 2 estabeleceu as regras básicas para logging. Use `device_printf` quando você tiver um `device_t`. Use `device_log` com uma prioridade explícita quando a prioridade importar. Logue o suficiente para tornar o log legível, mas não tanto que ele se torne ruído. Encaminhe informações de rotina com `LOG_INFO`, erros reais com `LOG_ERR` e rastreamento verbose com `LOG_DEBUG` controlado por um sysctl.

A Seção 3 agora acompanha a saída de log desde o momento em que o driver a escreve até o momento em que um usuário a lê, passando pelo buffer de mensagens do kernel, pelo `/var/log/messages` e pelo pipeline do syslog. O trabalho do driver termina quando ele chama `device_printf`; as ferramentas que você usa para ver o que o driver disse começam a partir daí.



## Seção 3: Usando `dmesg` e Syslog para Diagnósticos

A Seção 2 terminou com a chamada `device_printf` do driver. A Seção 3 parte do outro extremo: o momento em que um usuário executa `dmesg`, abre `/var/log/messages` ou pesquisa em arquivos de syslog. No meio ficam o buffer de mensagens do kernel, o daemon `syslogd`, o rotacionador `newsyslog` e um pequeno conjunto de arquivos de configuração que determinam silenciosamente quais mensagens aparecem onde. Entender esse pipeline é o que faz um autor de driver parar de se preocupar com "por que minhas mensagens não aparecem?" e começar a depurar o driver em si.

### O Buffer de Mensagens do Kernel

Cada mensagem do kernel que o driver produz vai primeiro para o **buffer de mensagens do kernel**. Trata-se de uma única região circular de memória que o kernel aloca durante o boot, com tamanho padrão de cerca de 192 KB no FreeBSD 14 para amd64. O tamanho é definido pelo parâmetro ajustável `msgbufsize`; o tamanho atual é visível como:

```sh
sysctl kern.msgbufsize
```

O buffer é "circular" no sentido de que, quando se enche, novas mensagens sobrescrevem as mais antigas. Isso importa para quem está caçando um bug antigo: se você reiniciou o sistema cinco horas atrás, gerou bastante saída de log do driver e só agora foi procurá-la, a saída pode já ter sido sobrescrita. A suposição segura é que o buffer retém vários dias de mensagens de rotina em um sistema tranquilo e algumas horas de mensagens em um sistema movimentado.

Como o buffer é um único bloco de memória, lê-lo é uma operação barata. O comando `dmesg(8)` funciona chamando `sysctl(8)` para ler `kern.msgbuf` e imprimir o conteúdo. O binário `dmesg` é pequeno; seu código-fonte está em `/usr/src/sbin/dmesg/dmesg.c` e vale uma rápida olhada se você sentir curiosidade.

A parte do buffer correspondente ao boot é preservada com mais cuidado do que a parte de tempo de execução. Durante o boot, o kernel escreve cada mensagem de autoconf, o banner de attach de cada driver e cada nota de detecção de hardware no buffer. O arquivo `/var/run/dmesg.boot`, produzido pelo script de inicialização `/etc/rc.d/dmesg`, captura o conteúdo do buffer no momento em que a sequência de boot termina e antes que a saída de tempo de execução comece a sobrescrevê-lo. Ler `dmesg.boot` é a forma de ver mensagens de boot depois que o sistema em execução há muito as substituiu no buffer ativo:

```sh
cat /var/run/dmesg.boot | grep myfirst
```

Para um autor de driver, `dmesg.boot` é a resposta para "meu driver fez attach no boot?" quando essa resposta é necessária horas depois. O `dmesg` ativo é a resposta para "o que meu driver disse desde então?".

### `dmesg` na Prática

As invocações mais comuns valem a pena memorizar.

`dmesg` sem argumentos imprime o conteúdo atual do buffer do kernel em ordem do mais antigo para o mais recente. Em um sistema que já está rodando há algum tempo, isso é bastante saída:

```sh
dmesg
```

Redirecione para `less` para navegar ou para `grep` para filtrar:

```sh
dmesg | less
dmesg | grep myfirst
dmesg | grep -i error
dmesg | tail -50
```

`dmesg -a` imprime todas as mensagens, incluindo as de prioridades que o kernel está configurado para suprimir da saída normal. Em algumas configurações isso traz à tona detalhes adicionais de baixa prioridade. Na maioria das configurações, a saída é a mesma do `dmesg` simples.

`dmesg -M /var/crash/vmcore.0 -N /var/crash/kernel.0` lê o buffer de mensagens de um dump de crash salvo, em vez do kernel ativo. É assim que se visualizam as últimas mensagens que um kernel produziu antes de entrar em pânico, quando o próprio pânico impediu o caminho normal de logging de funcionar. O material sobre `kgdb` da Seção 4 retorna a isso.

A forma `dmesg -c` (que em alguns sistemas limpa o buffer após a leitura) não está disponível na maneira como o kernel FreeBSD expõe o buffer, porque ele não é esvaziado pela leitura; ler produz uma cópia, e o buffer ativo continua recebendo novas mensagens. Usuários vindos do Linux às vezes esperam por `dmesg -c` e se surpreendem com isso.

Um hábito útil durante o desenvolvimento de drivers é executar `dmesg | grep myfirst | tail` após cada carregamento de módulo, cada teste e cada evento interessante. A repetição cria o reflexo de "o que o driver acabou de dizer?" que compensa na primeira vez que algo inesperado acontece.

### Logs Permanentes em `/var/log/messages`

O buffer de mensagens do kernel é volátil; um reboot o esvazia. Para qualquer coisa de mais longo prazo, o FreeBSD usa o `syslogd(8)`, o daemon syslog padrão do UNIX, que lê novas mensagens à medida que o kernel as produz e grava as selecionadas em arquivos no disco.

O arquivo canônico, em quase todo sistema FreeBSD, é `/var/log/messages`. Ele recebe a maioria das mensagens do kernel com prioridade `LOG_INFO` ou superior, além da maioria das mensagens de daemons do userland. O mapeamento entre prioridade e arquivo de destino fica em `/etc/syslog.conf`. Um trecho relevante se parece com:

```text
*.notice;kern.debug;lpr.info;mail.crit;news.err		/var/log/messages
```

A sintaxe usa pares "facility.priority". `kern.debug` significa "todas as mensagens do kernel com prioridade `LOG_DEBUG` ou superior". `*.notice` significa "todas as facilities com prioridade `LOG_NOTICE` ou superior". Vale saber que essa sintaxe existe; entendê-la é útil quando um tipo específico de mensagem precisa ir para um arquivo específico.

Como `/var/log/messages` é um arquivo de texto comum, todas as ferramentas do seu arsenal funcionam com ele:

```sh
tail -f /var/log/messages
tail -50 /var/log/messages
grep myfirst /var/log/messages
less /var/log/messages
wc -l /var/log/messages
```

Particularmente útil durante o desenvolvimento de drivers:

```sh
tail -f /var/log/messages | grep myfirst
```

Isso fornece um fluxo ao vivo apenas da saída do driver, que é o que a maioria das sessões de depuração realmente precisa.

Quando as mensagens envelhecem, elas migram para `/var/log/messages.0`, depois `.1`, depois `.N.gz` à medida que a rotação ocorre. A rotação é feita pelo `newsyslog(8)`, invocado por um job periódico do `cron` e que lê suas regras de `/etc/newsyslog.conf`. Uma regra típica para `/var/log/messages` rotaciona o arquivo quando ele atinge um limite de tamanho, mantém um certo número de arquivos comprimidos e reinicia o `syslogd` para reabrir o novo arquivo.

Para pesquisar nos arquivos rotacionados, `zgrep` é a ferramenta indicada:

```sh
zgrep myfirst /var/log/messages.*.gz
```

Ou, para todo o histórico incluindo o arquivo atual:

```sh
grep myfirst /var/log/messages && zgrep myfirst /var/log/messages.*.gz
```

Quem está caçando um bug que aconteceu dias atrás recorre a esses comandos com frequência.

### Saída para o Console Versus Saída Armazenada no Buffer

Uma nuance confunde muitos autores de drivers na primeira vez que a encontram. O kernel tem dois destinatários para suas mensagens: o buffer na memória e o console do sistema. O console é o terminal físico de um laptop (ou o console VGA em uma VM, ou o console serial em uma máquina sem monitor). Uma mensagem pode ir para um, para o outro ou para ambos, dependendo de sua prioridade e do nível de log atual do console.

O nível de log do console é controlado por `kern.consmsgbuf_size` (que define quanta memória o console utiliza) e, mais importante, por `kern.msgbuflock` e pelo filtro de prioridade do console. O ajuste mais simples é:

```sh
sysctl -d kern.log_console_level
```

No FreeBSD, mensagens de alta prioridade (LOG_WARNING e acima) são sempre exibidas no console. Mensagens de prioridade inferior são armazenadas no buffer, mas não exibidas. É por isso que uma mensagem de panic aparece no console independentemente do runlevel: ela tem prioridade `LOG_CRIT` ou superior, e o caminho de saída do console no kernel contorna o buffering habitual.

Para o autor de um driver, a consequência prática é que `device_log(dev, LOG_ERR, ...)` tem mais chance de ser percebido por um humano do que `device_printf(dev, ...)`, porque o primeiro chega ao console enquanto o segundo vai apenas para o buffer. A Seção 2 do Capítulo 23 apontou esse detalhe; a Seção 3 o reforça com a mecânica por trás dele.

Um driver que produz uma torrente de mensagens LOG_ERR inunda o console e, em casos extremos, pode desacelerar o sistema, pois o caminho de saída do console não é barato. A limitação de taxa é o remédio; o Capítulo 25 ensina isso adequadamente. Por ora, a lição é que a prioridade importa.

### Revisando Logs na Prática

O padrão mais comum para ler logs de drivers durante o desenvolvimento é o seguinte:

1. Carregue o módulo.
2. Execute o teste.
3. Descarregue o módulo.
4. Copie a saída do `dmesg` para um arquivo para análise.

A sequência em forma de shell:

```sh
# Clean slate
sudo kldunload myfirst 2>/dev/null
sudo kldload ./myfirst.ko

# Test
dev_major=$(stat -f '%Hr' /dev/myfirst0)
echo "Running driver test..."
some_test_program

# Capture
dmesg | tail -200 > /tmp/myfirst-test-$(date +%Y%m%d-%H%M%S).log

# Clean slate again
sudo kldunload myfirst

# Now inspect the log at leisure
less /tmp/myfirst-test-*.log
```

Alguns hábitos que esse padrão incentiva merecem ser enunciados explicitamente:

**Slate limpo primeiro, sempre.** Descarregue qualquer instância existente do módulo antes de carregar a que você quer testar. Caso contrário, você pode estar executando acidentalmente uma compilação mais antiga e se perguntar por que o seu novo código não tem o efeito esperado. Um descarregamento também deixa o buffer do `dmesg` mais legível, porque sua nova sessão começa a partir de um estado conhecido.

**Capture toda sessão interessante.** Um log salvo em arquivo é um log que você pode comparar com diff, filtrar com grep, compartilhar e consultar na semana seguinte. Um log deixado no buffer ativo é um log que pode ser descartado antes de você voltar a ele.

**Use timestamps nos nomes dos arquivos.** A convenção `date +%Y%m%d-%H%M%S` do trecho produz nomes como `myfirst-test-20260419-143027.log`. Quando você acumula uma dúzia de arquivos de log em um dia de testes, essa é a única forma de encontrar o que procura.

**Limite o `tail -N` a uma janela razoável.** O padrão é 10 linhas, o que quase nunca é suficiente para um teste de driver. De 200 a 500 é geralmente o tamanho certo: longo o bastante para capturar um ciclo completo de teste, curto o suficiente para ler em uma única passagem. Se o seu teste produz mais saída do que isso, ou o teste é grande demais ou o logging está verboso demais.

### Filtrando a Saída

O conjunto de ferramentas `grep` é seu aliado. Alguns padrões aparecem com frequência suficiente para valer a pena incorporá-los à memória muscular.

**Mostrar apenas a saída do driver:**

```sh
dmesg | grep '^myfirst'
```

A âncora `^myfirst` corresponde apenas às linhas que começam com o nome do dispositivo. Sem a âncora, você também corresponderia a linhas que por acaso contêm "myfirst" no texto (por exemplo, mensagens do kernel sobre o descritor de arquivo do módulo).

**Mostrar apenas erros:**

```sh
dmesg | grep -iE 'error|fail|warn|fault'
```

Sem diferenciação de maiúsculas, com uma expressão regular estendida que corresponde a várias palavras de erro comuns.

**Mostrar um intervalo específico:**

```sh
awk '/START OF TEST/,/END OF TEST/' /var/log/messages
```

Isso depende do harness de testes do leitor imprimir os marcadores "START OF TEST" e "END OF TEST" nas fronteiras. Um script que faz isso é um script cujos logs são fáceis de fatiar.

**Extrair apenas os timestamps e a primeira palavra:**

```sh
awk '{print $1, $2, $3, $5}' /var/log/messages | grep myfirst
```

Para uma varredura de alto nível em um log movimentado.

**Contar quantas ocorrências de cada tipo de mensagem:**

```sh
dmesg | grep myfirst | awk '{print $2}' | sort | uniq -c | sort -rn
```

Isso conta as mensagens agrupadas pela segunda palavra, que (com a convenção da Seção 2) costuma ser a categoria como INFO, WARN, ERR ou DEBUG. Útil para identificar situações como "por que meu driver está produzindo 30.000 mensagens DEBUG e apenas 2 mensagens INFO?"

### Configuração Permanente: `/etc/syslog.conf` e `/etc/newsyslog.conf`

Para a maior parte do trabalho com drivers, a configuração padrão do syslog é suficiente. O Capítulo 23 não pede ao leitor que a altere. Mas vale saber que os dois arquivos existem e o que fazem em linhas gerais.

`/etc/syslog.conf` é lido pelo `syslogd` na inicialização e ao receber `SIGHUP`. Ele contém regras que mapeiam "facilidade e prioridade" para "destino". Facilidade é o tipo de produtor (`kern`, `auth`, `mail`, `daemon`, `local0` a `local7`), e prioridade é o nível de prioridade do syslog. As mensagens de um driver sempre têm facilidade `kern` (porque vêm do kernel) e a prioridade que o driver especificou.

Um experimento útil, depois que o leitor tiver lido o Capítulo 23, é adicionar uma linha como:

```text
kern.debug					/var/log/myfirst-debug.log
```

ao `/etc/syslog.conf`, tocar o arquivo e recarregar o syslogd com `service syslogd reload`. Agora toda mensagem do kernel com nível debug vai para um arquivo separado. Se o leitor então habilitar o modo verbose no driver `myfirst` (o sysctl da Seção 8), o arquivo separado captura apenas a saída verbose e o arquivo principal `messages` permanece limpo. Esse é um truque que vale conhecer: ele permite habilitar um logging verbose profundo sem inundar `/var/log/messages` com tudo.

`/etc/newsyslog.conf` é lido pelo `newsyslog` quando ele realiza a rotação dos logs. Uma regra típica tem o seguinte aspecto:

```text
/var/log/messages    644  5    100    *     JC
```

Os campos, da esquerda para a direita: caminho do arquivo, modo, número de rotações a manter, tamanho máximo em KB, horário de rotação, flags. Para o arquivo `myfirst-debug.log` que o leitor acabou de adicionar, uma regra adequada no newsyslog impede que os logs cresçam sem limite:

```text
/var/log/myfirst-debug.log  644  3  500  *  JC
```

Mais uma vez, isso não é um requisito do capítulo; é um padrão que vale conhecer.

### Correlacionando Mensagens ao Longo de um Ciclo de Boot

Uma das tarefas de depuração mais comuns é "o driver funcionou após o último boot, mas agora não funciona". O leitor quer comparar o que aconteceu no último boot com o que está acontecendo agora. Dois caminhos:

**Caminho um:** ler `/var/run/dmesg.boot`, que capturou as mensagens do boot, e comparar com o `dmesg` atual:

```sh
diff <(grep myfirst /var/run/dmesg.boot) <(dmesg | grep myfirst)
```

Isso mostra exatamente quais mensagens são novas desde o boot. Útil para encontrar surpresas em tempo de execução.

**Caminho dois:** filtrar os arquivos de histórico em `/var/log/messages`:

```sh
zgrep myfirst /var/log/messages.*.gz | head -100
```

Isso mostra o histórico do driver ao longo dos últimos dias, através de reboots. Útil para encontrar padrões como "começou a falhar na terça-feira".

Um driver que registra sua versão no momento do attach torna tudo isso muito mais fácil. Se `dmesg.boot` mostra `myfirst0: myfirst PCI driver 1.5-power attached` e o boot atual mostra `myfirst0: myfirst PCI driver 1.6-debug attached`, o leitor sabe imediatamente que se trata de versões diferentes e que houve uma mudança no código entre elas. O exercício da Etapa 1 do Capítulo 23 adiciona exatamente essa linha de versão no attach por esse motivo.

### Exercício: Gerar uma Mensagem Conhecida do Driver e Confirmar que Ela Aparece

Um pequeno laboratório prático para a Seção 3. O leitor deve:

1. Recompilar e carregar o driver `1.5-power-stage1` do exercício da Seção 2. Confirmar que o banner de attach aparece no `dmesg`.

2. Acionar o sysctl `sim_force_dma_error=1` do simulador para fazer a próxima transferência DMA falhar. Executar uma transferência DMA. Confirmar que a mensagem de erro aparece tanto no `dmesg` quanto em `/var/log/messages`.

3. Produzir uma linha informacional verbose: definir o sysctl `debug_verbose` (que ainda não existe) para 1. Como o sysctl ainda não existe, essa etapa vai falhar, e o leitor deve anotar de que forma falhou. Isso é intencional; a Seção 8 adiciona o sysctl.

4. Para as linhas que apareceram, usar `awk` ou `grep` para extrair apenas as que têm o prefixo `myfirst0:` e contá-las.

5. Criar um arquivo `/var/log/myfirst-debug.log` adicionando uma linha `kern.debug` ao `/etc/syslog.conf` conforme esboçado acima. Recarregar o `syslogd`. Confirmar que o arquivo é criado. Confirmar que nenhuma mensagem aparece nele ainda (porque o driver `myfirst` ainda não produz saída com `LOG_DEBUG`). Fazer uma anotação para retornar a isso na Seção 8.

O exercício é curto. Seu valor está no fato de que o leitor agora usou de verdade `dmesg`, `/var/log/messages`, `grep` e `syslog.conf` com saída real de um driver. Os capítulos futuros vão assumir que essas ferramentas são familiares.

### Encerrando a Seção 3

A Seção 3 acompanhou o log desde o momento em que o driver chama `device_printf` ou `device_log` até o momento em que o usuário vê a saída. As mensagens vão primeiro para o buffer circular de mensagens do kernel, acessível via `dmesg`. Um snapshot do momento do boot é preservado em `/var/run/dmesg.boot`. O daemon `syslogd` lê as novas mensagens e as grava em `/var/log/messages` de acordo com as regras em `/etc/syslog.conf`. O daemon `newsyslog` realiza a rotação dos arquivos de acordo com `/etc/newsyslog.conf`. A saída no console é automática para mensagens de alta prioridade e bloqueada para as de baixa prioridade.

Com o pipeline de logging claro, a Seção 4 se volta para a outra peça essencial da fundação de depuração: o kernel de debug. Um driver cujos bugs sempre causam panic silenciosamente e não produzem nada para o log é um driver que não pode ser depurado. Um kernel de debug torna esses bugs silenciosos ruidosos.



## Seção 4: Builds de Debug do Kernel e Suas Opções

Um kernel de debug não é um kernel diferente; é o mesmo kernel com verificações adicionais, recursos de debug adicionais e um pequeno conjunto de funcionalidades compiladas que um kernel de lançamento não possui. O código-fonte do driver não muda. As ferramentas em espaço do usuário não mudam. O que muda é que o kernel em execução agora detecta mais de seus próprios bugs, preserva mais contexto quando um panic acontece e oferece mais formas de inspecionar a si mesmo.

Esta seção é a mais longa do capítulo porque o kernel de debug é o alicerce sobre o qual o restante do capítulo se apoia. Cada ferramenta posterior depende de ter as opções certas configuradas. DTrace depende de `KDTRACE_HOOKS`. O `ddb` depende de `DDB` e `KDB`. `INVARIANTS` detecta bugs que de outra forma produziriam corrupção silenciosa de memória. `WITNESS` detecta violações de ordem de lock que de outra forma produziriam panics intermitentes semanas depois. Cada opção tem um custo, um benefício e um contexto em que é apropriada; a Seção 4 percorre cada uma delas.

### As Opções de Debug Fundamentais

O kernel do FreeBSD é configurado por meio de um arquivo de configuração do kernel, como `/usr/src/sys/amd64/conf/GENERIC`. O arquivo de configuração é uma lista de linhas `options` que habilitam ou desabilitam funcionalidades em tempo de compilação. Um kernel de debug é produzido partindo do `GENERIC` (ou de outra configuração base), adicionando opções de debug, reconstruindo, instalando e fazendo o boot com o resultado.

As opções que importam para a depuração de drivers se dividem em quatro grupos: o depurador do kernel, as verificações de consistência, a infraestrutura de debug de locks e a infraestrutura de rastreamento.

**Grupo do depurador do kernel.** Estas habilitam o depurador em kernel `ddb` e a infraestrutura que o suporta.

- `options KDB`: habilita o framework Kernel Debugger. Esta é a opção guarda-chuva que permite que qualquer backend (`DDB`, GDB por serial, etc.) se conecte.
- `options DDB`: habilita o depurador DDB em kernel. DDB é o backend que a maioria dos autores de drivers utiliza. Ele executa dentro do kernel e compreende as estruturas de dados do kernel.
- `options DDB_CTF`: habilita o carregamento de dados CTF (Compact C Type Format) no kernel, o que permite ao `ddb` e ao DTrace imprimir informações com reconhecimento de tipos sobre estruturas do kernel. Requer `makeoptions WITH_CTF=1` na configuração.
- `options DDB_NUMSYM`: habilita pesquisas de símbolo por número dentro do DDB. Útil para inspecionar endereços específicos.
- `options KDB_UNATTENDED`: em um panic, não pausar para entrar no `ddb`. Em vez disso, o kernel despeja o core (se `dumpdev` estiver configurado), reinicia e continua. Essa é a configuração certa para VMs de laboratório onde o leitor não quer perder o kernel para uma sessão do depurador a cada panic.
- `options KDB_TRACE`: em qualquer entrada no depurador (por panic ou de outra forma), imprimir automaticamente um stack trace. Economiza um passo em quase toda sessão de diagnóstico.
- `options BREAK_TO_DEBUGGER`: permite que um break no console serial ou USB abra o `ddb`. Útil em sistemas de teste sem monitor.
- `options ALT_BREAK_TO_DEBUGGER`: combinação de teclas alternativa para o mesmo efeito.

Uma configuração típica de kernel de debug inclui todas essas opções. O kernel `GENERIC` no amd64 já habilita `KDB`, `KDB_TRACE` e `DDB`; as opções adicionais que o leitor habilita são `KDB_UNATTENDED`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS` e afins.

**Grupo de verificações de consistência.** Estas habilitam asserções extras em tempo de execução no código do kernel.

- `options INVARIANTS`: habilita as asserções no kernel. Quase todos os arquivos em `/usr/src/sys/*` contêm chamadas a `KASSERT`, e essas chamadas só fazem algo quando `INVARIANTS` está ativo. Sem `INVARIANTS`, um `KASSERT(ptr != NULL, ("oops"))` compila para nada. Com `INVARIANTS`, ele causa um panic no kernel em caso de falha. O custo é mensurável (talvez 10% do tempo de CPU do kernel em um sistema ocupado), mas normalmente é desprezível para o desenvolvimento de drivers, porque o kernel gasta menos tempo por operação do que a aplicação que o utiliza. O benefício é que bugs que, de outra forma, corrompem dados silenciosamente são capturados exatamente no momento em que ocorrem.
- `options INVARIANT_SUPPORT`: a infraestrutura que o próprio `INVARIANTS` utiliza. Obrigatório quando `INVARIANTS` está ativo; também obrigatório separadamente se algum módulo carregável foi compilado com `INVARIANTS`. Esquecer essa opção produz erros de "unresolved symbol" ao carregar o módulo.
- `options DIAGNOSTIC`: verificações adicionais de baixo custo, menos agressivas do que `INVARIANTS`. Alguns subsistemas usam essa opção para checagens baratas o suficiente para rodar em produção, mas lentas demais para laços internos críticos.

**Grupo de debug de locks.** Essas opções ativam a infraestrutura que verifica a disciplina de sincronização do driver.

- `options WITNESS`: habilita o verificador de ordem de aquisição de locks. O WITNESS rastreia cada lock que o kernel adquire, registra a ordem em que os locks são obtidos, e reclama em voz alta quando uma thread adquire locks em uma ordem que poderia causar deadlock se outra thread os mantivesse na ordem inversa. Um driver com um bug de ordem de locks vai gerar um relatório do WITNESS na primeira vez que a ordem incorreta for exercitada, mesmo que nenhum deadlock real ocorra. Isso representa um ganho enorme na depuração, pois captura o bug antes que o deadlock se manifeste.
- `options WITNESS_SKIPSPIN`: ignora a verificação de ordem de locks para spin locks. Spin locks têm suas próprias restrições, para as quais o verificador geral não foi projetado, e o verificador pode produzir falsos positivos com eles. Ativar `WITNESS_SKIPSPIN` mantém o verificador útil para o caso comum de mutex.
- `options WITNESS_KDB`: em caso de violação de ordem de locks, entra imediatamente no `ddb` em vez de apenas registrar o evento. Comportamento agressivo; adequado para uma VM onde uma sessão manual no `ddb` é simples.
- `options DEBUG_LOCKS`: depuração adicional da API genérica de locks (separada do WITNESS). Detecta uso de locks não inicializados, locks adquiridos em contexto incorreto e problemas relacionados.
- `options LOCK_PROFILING`: instrumentação que permite ao `lockstat` medir a contenção de locks. Não é estritamente uma opção de debug (é uma opção de profiling) e tem custo elevado; use-a apenas quando estiver investigando contenção, não por padrão.
- `options DEBUG_VFS_LOCKS`: depuração de locks específica do VFS. Útil apenas para o desenvolvimento de drivers de sistema de arquivos.

**Grupo de infraestrutura de rastreamento.** Essas opções habilitam os frameworks DTrace e KTR.

- `options KDTRACE_HOOKS`: os hooks de DTrace em todo o kernel. Sem isso, o DTrace não tem onde se ancorar. O custo é baixo mesmo quando nenhum script DTrace está rodando, pois os hooks são basicamente slots de ponteiro de função que não fazem nada.
- `options KDTRACE_FRAME`: informações de frame de desenrolamento de pilha que o DTrace usa para as ações `stack()`. Necessário para obter stack traces significativos dentro de scripts DTrace.
- `makeoptions WITH_CTF=1`: uma linha `makeoptions` (e não `options`) que habilita a geração de CTF para o kernel. CTF são os dados de tipo que o DTrace e o DDB usam para entender o layout das estruturas do kernel.
- `options KTR`: o tracer de ring buffer de eventos dentro do kernel. Diferente do `ktrace`; o KTR é um buffer interno de alto desempenho para eventos do kernel, e é mais útil para desenvolvedores de nível mais baixo do kernel. A maioria dos autores de drivers não precisa do `KTR` e pode deixá-lo desativado; o Capítulo 23 o menciona apenas para nomeá-lo corretamente.
- `options KTR_ENTRIES`, `KTR_COMPILE`, `KTR_MASK`: os parâmetros de configuração do KTR. Consulte `/usr/src/sys/conf/NOTES` para a lista completa.

Para o Capítulo 23, o leitor precisa de `KDB`, `DDB`, `DDB_CTF`, `KDB_UNATTENDED`, `KDB_TRACE`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, `WITNESS_SKIPSPIN`, `KDTRACE_HOOKS` e `makeoptions DEBUG=-g` para os símbolos de depuração. Se o leitor seguiu os pré-requisitos do Capítulo 22, essas opções já estão habilitadas.

### Construindo um Kernel de Debug

Construir um kernel personalizado é uma operação padrão no FreeBSD. O fluxo de trabalho:

1. Copie a configuração `GENERIC` para um novo nome:

   ```sh
   cd /usr/src/sys/amd64/conf
   sudo cp GENERIC MYDEBUG
   ```

2. Edite `MYDEBUG` para adicionar as opções de debug. Uma adição mínima:

   ```text
   ident MYDEBUG

   options INVARIANTS
   options INVARIANT_SUPPORT
   options WITNESS
   options WITNESS_SKIPSPIN
   options KDB_UNATTENDED

   makeoptions DEBUG=-g
   makeoptions WITH_CTF=1
   ```

   `DDB`, `KDB`, `KDTRACE_HOOKS` e `DDB_CTF` já estão presentes em `GENERIC` no amd64 a partir do FreeBSD 14, portanto não precisam ser adicionados novamente. Se você estiver em uma arquitetura não-x86, verifique o arquivo `conf/GENERIC` correspondente para saber o que já está incluído.

   Um ponto de confusão pequeno, mas frequente: `/usr/src/sys/amd64/conf/` contém os *arquivos de configuração do kernel*. O diretório `/usr/src/sys/amd64/compile/` que alguns tutoriais mais antigos mencionam era um diretório de saída de build que o `config(8)` historicamente populava; em um sistema FreeBSD moderno, os produtos de build ficam sob `/usr/obj/usr/src/amd64.amd64/sys/<KERNCONF>/`, e o caminho `compile/` não é mais o lugar onde você edita ou procura o código-fonte.

3. Construa o kernel:

   ```sh
   cd /usr/src
   sudo make -j4 buildkernel KERNCONF=MYDEBUG
   ```

   Em uma VM razoável, isso leva de dez a vinte minutos. Em uma VM mais lenta, meia hora. O build é o passo isolado mais longo no fluxo de debug, mas ele é executado uma vez por mudança de configuração, não uma vez por teste.

4. Instale o novo kernel:

   ```sh
   sudo make installkernel KERNCONF=MYDEBUG
   ```

   Isso copia os arquivos do kernel para `/boot/kernel` e faz o backup do kernel anterior em `/boot/kernel.old`. Se o novo kernel falhar na inicialização, você pode selecionar `kernel.old` no menu do boot loader e continuar.

5. Reinicie.

6. Verifique:

   ```sh
   uname -a
   ```

   A saída deve mencionar `MYDEBUG` na string de identificação do kernel.

7. Confirme que as opções de debug estão ativas:

   ```sh
   sysctl debug.witness.watch
   sysctl kern.witness
   ```

   Em um kernel com `WITNESS` ativado, esses comandos produzem saída. Em um kernel sem `WITNESS`, esses sysctls não existem.

O tempo total entre "quero um kernel de debug" e "estou executando um kernel de debug" normalmente é inferior a uma hora. O tempo investido é recuperado na primeira sessão de depuração, porque o kernel de debug é o kernel que captura os bugs.

### Quando Usar `INVARIANTS`

`INVARIANTS` é a opção de debug mais importante para um desenvolvedor de drivers. É também a mais incompreendida.

Quando um programador do kernel escreve `KASSERT(condition, ("message %d", val))`, a intenção é: esta condição deve ser verdadeira neste ponto; se em algum momento for falsa, algo deu errado de um modo do qual o código não consegue se recuperar. Sem `INVARIANTS`, `KASSERT` compila para nada; a condição não é verificada. Com `INVARIANTS`, `KASSERT` avalia a condição e chama `panic` se ela falhar, imprimindo a mensagem.

Considere um exemplo concreto do driver `myfirst`. O caminho de submissão de DMA (Capítulo 21) mantém um lock de buffer, define um flag de transferência em andamento e chama `bus_dmamap_sync`. Um bug que aciona o caminho de submissão duas vezes sem aguardar a conclusão é um bug de dupla submissão, e pode corromper o DMA. Um `KASSERT` no início do caminho de submissão o captura:

```c
static int
myfirst_dma_submit(struct myfirst_softc *sc, ...)
{
    KASSERT(!sc->dma_in_flight,
        ("myfirst_dma_submit: previous transfer still in flight"));
    ...
}
```

Sem `INVARIANTS`, essa linha não faz nada. Com `INVARIANTS`, na primeira vez em que um bug submete uma segunda transferência antes que a primeira seja concluída, o kernel entra em panic com a mensagem. Você olha o backtrace, identifica a dupla submissão e corrige o bug.

É assim que o valor de `INVARIANTS` se manifesta: ele transforma corrupção silenciosa em panics barulhentos. A contrapartida é que um autor de driver que não escreve nenhum `KASSERT` não obtém nenhum benefício de `INVARIANTS`; a opção só é útil se o código contra o qual ela é executada contém assertivas. O driver `myfirst` na Etapa 1 tem algumas; a Seção 7 adiciona mais; a Seção 8 as move para o cabeçalho de debug.

Outros auxiliares de assertiva existem além do `KASSERT`:

- `MPASS(expr)`: uma forma compacta de `KASSERT(expr, (...))`. Expande para a mesma verificação com uma mensagem fornecida pelo compilador. Útil quando a condição é autoexplicativa.
- `VNASSERT(expr, vp, msg)`: assertiva especializada para vnodes. Autores de drivers raramente a utilizam diretamente.
- `atomic_testandset_int`, `atomic_cmpset_int`: não são auxiliares de assertiva, mas operações comuns na sincronização de drivers que possuem suas próprias variantes com consciência de debug.

Leia `KASSERT` em seu contexto: ele é tanto um auxiliar de documentação quanto de debug. Quem olha o código-fonte de uma função desconhecida encontra os `KASSERT`s no início e imediatamente sabe quais invariantes o código espera. Um driver que acumula `KASSERT`s ao longo do tempo torna-se autodocumentado de uma forma que a prosa simples não consegue igualar.

### Quando Usar `WITNESS` e `DEBUG_LOCKS`

`WITNESS` é a outra metade do par de debug para drivers. Onde `INVARIANTS` captura bugs de lógica, `WITNESS` captura bugs de sincronização.

A ideia básica: cada lock tem um identificador, o kernel registra o conjunto de locks que cada thread mantém e, quando uma thread tenta adquirir um lock, `WITNESS` verifica se a ordem de aquisição é consistente com aquisições anteriores no histórico do kernel. Se a thread A adquire o lock X e depois o lock Y, e posteriormente a thread B adquire o lock Y e depois o lock X, as duas threads formam um potencial deadlock: se A mantém X aguardando por Y enquanto B mantém Y aguardando por X, nenhuma das duas pode prosseguir. `WITNESS` sinaliza a inconsistência na segunda vez que uma thread tenta adquirir os locks na ordem errada, mesmo que o deadlock real não ocorra durante o teste.

Uma reclamação do `WITNESS` tem aparência semelhante a esta:

```text
lock order reversal:
 1st 0xfffff80001a23200 myfirst_sc (myfirst_sc) @ /usr/src/sys/dev/myfirst/myfirst_pci.c:523
 2nd 0xfffff80001a23240 some_other_lock (some_other_lock) @ /some/other/file.c:789
witness_order_list_add: lock order reversal
```

A mensagem identifica ambos os locks, seus endereços, os arquivos e números de linha onde foram adquiridos e a inversão. Um autor de driver lê isso e imediatamente sabe onde procurar.

`WITNESS` interage com as demais opções de debug. `WITNESS_KDB` entra no `ddb` em caso de violação em vez de apenas registrar o evento. `WITNESS_SKIPSPIN` ignora os spinlocks, que têm regras diferentes (não podem dormir, possuem tratamento de prioridade e a disciplina de ordenação é ligeiramente diferente). `DEBUG_LOCKS` adiciona verificações extras sobre `WITNESS`: detecção de double-unlock, detecção de unlock pela thread errada e detecção de uso de mutexes não inicializados. Para o desenvolvimento de drivers, habilitar os três (`WITNESS`, `WITNESS_SKIPSPIN`, `DEBUG_LOCKS`) é a recomendação padrão.

O custo de `WITNESS` é mensurável. Cada aquisição de lock adiciona algumas dezenas de instruções e um acesso a uma linha de cache de memória. Em uma carga de trabalho intensa em drivers, isso pode significar 20 ou 30% mais lento do que um kernel sem debug. Para desenvolvimento e testes, isso é aceitável. Para um kernel em produção, `WITNESS` normalmente fica desativado.

O guia de bugs comuns da Seção 7 inclui um laboratório com `WITNESS`: você introduz deliberadamente uma violação de ordem de lock no driver `myfirst`, reconstrói, recarrega, aciona o caminho e observa o `WITNESS` capturá-la. Esse laboratório é a forma mais rápida de internalizar como a ferramenta funciona.

### Quando Usar `DEBUG_MEMGUARD` e `MEMGUARD`

`MEMGUARD` é um alocador de debug de memória mais pesado. Ele aloca memória com páginas de guarda em ambos os lados, detecta escritas além do limite da alocação e detecta uso após liberação (use-after-free). Ele é habilitado com `options DEBUG_MEMGUARD` e configurado em tempo de execução por meio de `sysctl vm.memguard.desc`.

MEMGUARD é custoso: usa muito mais memória do que um alocador normal por causa das páginas de guarda, e seu caminho de alocação é mais lento. O uso correto é direcionado: você o ativa para um tipo malloc específico que o driver usa intensamente, não para o kernel inteiro. Se o driver `myfirst` possui um tipo malloc `M_MYFIRST`, você pode ativar o MEMGUARD apenas para esse tipo:

```sh
sysctl vm.memguard.desc="M_MYFIRST"
```

Após uma reinicialização (MEMGUARD precisa inicializar sua arena no boot), as alocações de memória `M_MYFIRST` passarão pelo MEMGUARD. Uma escrita além do final de uma alocação causa panic imediatamente. Um uso após liberação causa panic imediatamente. Você pode executar testes e ter o kernel capturando bugs de heap na memória do driver sem desacelerar o restante do sistema.

O Capítulo 23 não usa MEMGUARD intensamente, porque os padrões de memória do driver `myfirst` são simples. A Seção 7 o menciona como uma ferramenta no kit para drivers que fazem alocações mais complexas. Para quem eventualmente escrever um driver com filas dinâmicas, pools de buffer ou alocações de longa duração, vale a pena revisitar o MEMGUARD.

### Compensações: Desempenho vs. Visibilidade de Debug

A tentação, ao aprender as opções pela primeira vez, é habilitar tudo. Essa é a escolha certa durante o desenvolvimento. É a escolha errada para produção.

Um kernel com `INVARIANTS`, `WITNESS`, `DEBUG_LOCKS`, `KDTRACE_HOOKS`, `DDB` e `MEMGUARD` ativados é visivelmente mais lento do que o `GENERIC`: talvez 20% mais lento no geral, 50% mais lento em cargas com muitos locks, dependendo do que a máquina está fazendo. Em uma estação de desenvolvimento, 20% mais lento é aceitável. Em um servidor em produção, 20% mais lento significa 20% mais hardware para realizar o mesmo trabalho.

A distinção entre kernels de debug e kernels de produção é clara na prática. Desenvolvedores executam kernels de debug, porque se preocupam em encontrar bugs. Usuários executam kernels de release, porque se preocupam com desempenho. Um driver que está correto sob `WITNESS` e `INVARIANTS` está correto, ponto final; essas opções não alteram o comportamento do driver, apenas o verificam. Um driver que passa por um laboratório com `WITNESS` executado em um kernel de debug também passará em um kernel de release. Esse é o invariante que faz o padrão funcionar: você desenvolve contra um kernel rigoroso e implanta contra um permissivo, e as verificações extras do kernel rigoroso são o que mantém o kernel permissivo correto.

O conselho prático: execute um kernel de debug na VM que você usa para trabalho com drivers, o tempo todo. Não troque por um kernel de release no desenvolvimento diário. O retorno sobre os erros do driver vale o custo de desempenho centenas de vezes. Quando você estiver pronto para testar o driver em um kernel estilo produção (para medições de tempo, para testes de integração), inicialize um kernel de release para aquele teste específico, confirme o comportamento correto e então volte ao kernel de debug.

### Um Panorama dos Comandos do `ddb`

DDB é o depurador em kernel. Quando o kernel entra em panic em um sistema onde `DDB` está compilado, em vez de reinicializar imediatamente, o sistema cai em um prompt do depurador no console:

```text
KDB: enter: panic
[ thread pid 42 tid 100049 ]
Stopped at:  kdb_enter+0x3b:  movq    $0,kdb_why
db>
```

Nesse prompt, você pode inspecionar o estado do kernel. Os comandos são concisos e diretos. Um guia completo pertence a uma referência separada; o que se segue é a lista que um autor de drivers usa com mais frequência.

**Comandos de inspeção:**

- `bt` ou `backtrace`: mostra o backtrace da thread atual. Este é o primeiro comando a ser executado em todo panic.
- `ps`: mostra a tabela de processos. Útil para ver o que estava em execução.
- `show thread <tid>`: mostra detalhes de uma thread específica.
- `show proc <pid>`: mostra detalhes de um processo específico.
- `show pcpu`: mostra o estado por CPU, incluindo a thread atual em cada CPU.
- `show allpcpu`: mostra todo o estado por CPU de uma vez.
- `show lockchain <addr>`: mostra a cadeia de threads bloqueadas em um lock específico.
- `show alllocks`: mostra todos os locks atualmente mantidos, em todas as threads. Útil para diagnosticar deadlocks aparentes.
- `show mbufs`: mostra estatísticas de mbuf, se a pilha de rede estiver envolvida.
- `show malloc`: mostra estatísticas de malloc do kernel, agrupadas por tipo. Útil para encontrar um driver que vazou memória antes do panic.
- `show registers`: mostra os registradores da CPU atual.

**Comandos de navegação:**

- `x/i <addr>`: desmonta (disassemble) a partir de um endereço. Útil para examinar a instrução onde ocorreu um page fault.
- `x/xu <addr>`: despeja memória como bytes hexadecimais.
- `x/sz <addr>`: despeja memória como uma string terminada em null.

**Comandos de controle:**

- `continue`: retoma a execução. Se o kernel estava estável o suficiente para entrar no `ddb`, isso às vezes permite que a máquina continue normalmente. Com frequência, a escolha mais segura é `panic`.
- `reset`: reinicia a máquina imediatamente.
- `panic`: força um panic, que aciona um crash dump se `dumpdev` estiver configurado.
- `call <func>`: chama uma função do kernel pelo nome. Avançado; raramente útil na depuração de drivers.

**Comandos de navegação por threads:**

- `show all procs`: lista todos os processos.
- `show sleepq`: mostra as filas de espera (sleeping queues).
- `show turnstile`: mostra o estado do turnstile (usado para locks bloqueantes).
- `show sema`: mostra os semáforos.

O autor de um driver não precisa memorizar essa lista. O padrão é: ao entrar no `ddb`, execute `bt` primeiro, depois `ps`, depois `show alllocks`, depois `show mbufs` e por fim `show malloc`. Esses cinco comandos cobrem 80% do que a maioria dos panics de driver precisa.

Um hábito útil é imprimir os comandos e mantê-los ao lado do teclado durante as primeiras sessões no `ddb`. A primeira sessão parece estranha porque a memória muscular ainda não está formada. Na terceira sessão, os comandos já fluem naturalmente.

### Uma Breve Introdução ao `kgdb`

`ddb` é o depurador em tempo real. `kgdb` é o depurador post-mortem. Quando o kernel entra em pânico com `dumpdev` configurado, a rotina de pânico grava a imagem completa da memória do kernel no dispositivo de dump. No próximo boot, `savecore(8)` copia o dump do dispositivo de dump para `/var/crash`. O leitor pode então abri-lo com `kgdb`:

```sh
sudo kgdb /boot/kernel/kernel.debug /var/crash/vmcore.0
```

(Observação: `kernel.debug` é a versão com símbolos de depuração do kernel, produzida quando o kernel foi compilado com `makeoptions DEBUG=-g`. Sem `DEBUG=-g`, o `kgdb` ainda consegue abrir o dump, mas produz uma saída menos útil.)

Dentro do `kgdb`, o leitor tem algo próximo a uma interface normal do `gdb`. Os comandos são os mesmos: `bt`, `frame N`, `info locals`, `print variable`, `list`, e assim por diante. A diferença é que a máquina não está em execução; o kernel está pausado para sempre no momento do pânico, e toda inspeção é feita contra esse estado congelado. Você pode percorrer a pilha, examinar estruturas de dados, seguir ponteiros e raciocinar sobre o que levou ao pânico. Você não consegue avançar.

Uma sessão típica do `kgdb` em um pânico tem esta aparência:

```text
(kgdb) bt
#0  doadump (...)
#1  kern_reboot (...)
#2  vpanic (...)
#3  panic (...)
#4  witness_checkorder (...)
...

(kgdb) frame 10
#10 myfirst_pci_detach (dev=0xfffff80001a23000) at myfirst_pci.c:789
789            mtx_destroy(&sc->lock);

(kgdb) list
784         mtx_lock(&sc->lock);
785         sc->detaching = true;
786         /* wait for all users */
787         while (sc->refs > 0)
788             cv_wait(&sc->cv, &sc->lock);
789            mtx_destroy(&sc->lock);
790         return (0);
791     }

(kgdb) print sc->refs
$1 = 0

(kgdb) print sc->lock
$2 = {
   ...
}
```

O leitor percorre a pilha para encontrar o frame do driver, examina o que o driver estava fazendo e identifica o bug. Neste exemplo, o driver estava destruindo um mutex que ainda estava bloqueado, porque a linha 789 chama `mtx_destroy` sem desbloquear antes. O bug é imediatamente visível.

O Capítulo 23 não faz uso intenso do `kgdb`, pois os laboratórios são projetados para ser diagnosticáveis a partir do `ddb` e do `dmesg` sozinhos. A Seção 7 menciona o `kgdb` no guia de bugs comuns para uma classe de bugs em que a inspeção post-mortem é a ferramenta adequada. O leitor que quiser se aprofundar deve ler a página de manual `kgdb(1)` e experimentar em um pânico conhecido.

### Exercício: Compilar um Kernel de Depuração e Confirmar que os Símbolos Estão Presentes com `kgdb`

Um exercício curto que fundamenta a seção.

1. Se ainda não tiver feito, compile e instale um kernel `MYDEBUG` conforme descrito acima, com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN` e `makeoptions DEBUG=-g`. Reinicie.

2. Confirme que o kernel em execução é o build de depuração:

   ```sh
   uname -v | grep MYDEBUG
   sysctl debug.witness.watch
   ```

3. Configure um dispositivo de dump. Normalmente, é uma partição de swap:

   ```sh
   sudo dumpon /dev/adaNpY
   ```

   onde `adaNpY` é sua partição de swap. Em uma VM típica, é `ada0p3`.

4. Acione um pânico deliberado para produzir um crash dump. A maneira mais segura é executar `sysctl debug.kdb.panic=1` como root:

   ```sh
   sudo sysctl debug.kdb.panic=1
   ```

   O sistema entra no `ddb` (porque `KDB_UNATTENDED` não foi configurado para este exercício; se tivesse sido configurado, o sistema gravaria um dump e reiniciaria). Execute `bt`, `ps` e `show alllocks`, depois digite `panic` para permitir que o dump prossiga. O sistema grava no dispositivo de dump e reinicia.

5. Após o reinício, verifique que o `savecore(8)` copiou o dump:

   ```sh
   ls /var/crash
   ```

   Você deve ver `vmcore.0`, `info.0` e `kernel.0`.

6. Abra o dump com `kgdb`:

   ```sh
   sudo kgdb /var/crash/kernel.0 /var/crash/vmcore.0
   ```

7. Dentro do `kgdb`, execute `bt`. Você deve ver a pilha do pânico. Percorra alguns frames com `frame N`. Confirme que as linhas do código-fonte e os argumentos das funções estão visíveis; isso é o que os símbolos de depuração proporcionam.

8. Saia do `kgdb` com `quit`.

O exercício não se trata de corrigir um bug; trata-se de provar que a infraestrutura de depuração funciona. A partir deste ponto, qualquer pânico real que o leitor causar é completamente depurável: o crash é preservado, os símbolos estão presentes, o depurador abre o dump. Essa é a base. Sem ela, depurar é principalmente adivinhar.

### Encerrando a Seção 4

A Seção 4 transformou o kernel do leitor em um kernel de depuração. As opções ativam asserções, verificação de ordem de locks, hooks do DTrace, símbolos de depuração e o depurador interno do kernel. O custo é um sistema mais lento; o benefício é um sistema que detecta seus próprios bugs de forma ruidosa. O `ddb` é a ferramenta de inspeção em tempo real; o `kgdb` é a ferramenta de inspeção post-mortem. Ambos funcionam com o mesmo kernel de depuração que o leitor está executando agora.

Com o pipeline de logging (Seção 3) e o kernel de depuração (Seção 4) instalados, o leitor tem a infraestrutura que todas as ferramentas posteriores assumem como dada. A Seção 5 apresenta o DTrace, a ferramenta que transforma o kernel de depuração em uma plataforma de rastreamento e medição em tempo real.



## Seção 5: Usando o DTrace para Inspeção ao Vivo do Kernel

DTrace é o bisturi do kit de ferramentas de depuração do FreeBSD. Onde o `printf` é um instrumento grosseiro e o `ddb` é um martelo, o DTrace é um bisturi: ele permite que você faça perguntas específicas e precisas sobre o comportamento em tempo real do kernel e obtenha respostas sem modificar o código-fonte, recompilar o kernel ou parar o sistema. Um autor de driver que aprende o DTrace atinge um novo nível de produtividade: a capacidade de dizer "quero saber com que frequência a interrupção do driver dispara" e obter uma resposta em dez segundos sem alterar uma linha de código.

Esta seção apresenta o DTrace no nível de detalhe que um autor de driver precisa. Ela não pretende ser uma referência completa do DTrace; a excelente página de manual `dtrace(1)`, o DTrace Guide disponível online e o livro *Illumos DTrace Toolkit* são as referências mais aprofundadas. O que a Seção 5 oferece ao leitor é DTrace suficiente para instrumentar o driver `myfirst`, medir seu comportamento e entender o que o DTrace pode e não pode fazer.

### O que é o DTrace?

DTrace é um **framework de rastreamento dinâmico** (dynamic tracing framework). A palavra "dinâmico" é importante: ele não exige que o kernel tenha sido compilado com conhecimento de cada ponto de rastreamento; não exige que o código em execução seja corrigido; não exige que o leitor recompile e reinicie. Você carrega o módulo do DTrace, solicita um probe, e o framework anexa instrumentação ao código do kernel em execução. Quando o código instrumentado é executado, a instrumentação dispara, e o DTrace registra o evento. Quando você termina, a instrumentação é desanexada e removida.

O framework originou-se no Solaris e foi portado para o FreeBSD. Ele reside em `/usr/src/cddl/` (a parte do código-fonte licenciada sob CDDL), pois herda o licenciamento do Solaris. A ferramenta em espaço do usuário é `dtrace(1)`; a infraestrutura do lado do kernel é um conjunto de módulos que podem ser carregados e descarregados conforme necessário.

O DTrace tem três conceitos organizadores: **providers**, **probes** e **actions**.

Um **provider** é uma fonte de pontos de rastreamento. Exemplos de providers que o FreeBSD inclui: `fbt` (rastreamento de fronteira de função, com um probe por entrada e saída de cada função do kernel), `syscall` (um probe por entrada e retorno de system call), `sched` (eventos do escalonador), `io` (eventos de I/O de blocos), `vfs` (eventos de sistema de arquivos), `proc` (eventos de processo), `vm` (eventos de memória virtual), `sdt` (rastreamento estaticamente definido, para probes compilados no kernel) e `lockstat` (operações de lock). Alguns providers (`fbt`, `syscall`) estão sempre disponíveis após o módulo ser carregado; outros (`sdt`) só existem se o código do kernel define probes explicitamente.

Um **probe** é um ponto específico que o leitor pode rastrear. Os probes têm nomes com quatro partes no formato `provider:module:function:name`. Por exemplo, `fbt:kernel:myfirst_pci_attach:entry` é o probe de entrada do provider `fbt` na função `myfirst_pci_attach` no módulo `kernel` (o binário principal do kernel, não um módulo carregável). O curinga `*` corresponde a qualquer segmento: `fbt::myfirst_*:entry` corresponde a toda função cujo nome começa com `myfirst_`, em qualquer módulo, na entrada da função.

Uma **action** é o que o DTrace faz quando um probe dispara. A ação mais simples é `{ trace(...) }`, que registra o argumento. Ações mais interessantes agregam dados: `@counts[probefunc] = count()` conta quantas vezes o probe de cada função disparou. A linguagem D (a linguagem de script do DTrace) é semelhante a C, mas mais segura: não possui loops, chamadas de função nem alocação de memória, e é executada no kernel sob restrições de segurança estritas.

O poder do DTrace vem da combinação: você ativa um probe específico, anexa uma action específica, observa apenas os eventos que lhe interessam e faz tudo isso sem tocar no código-fonte.

### Ativando o Suporte ao DTrace

As opções do kernel necessárias para o DTrace foram abordadas na Seção 4: `options KDTRACE_HOOKS`, `options KDTRACE_FRAME`, `options DDB_CTF` e `makeoptions WITH_CTF=1`. Se o leitor compilou o kernel `MYDEBUG` com essas opções, o DTrace está pronto para uso.

O lado do espaço do usuário precisa que o módulo do DTrace seja carregado. O módulo guarda-chuva é o `dtraceall`, que carrega todos os providers:

```sh
sudo kldload dtraceall
```

Alternativamente, providers individuais podem ser carregados:

```sh
sudo kldload dtrace          # core framework
sudo kldload dtraceall       # all providers (recommended for development)
sudo kldload fbt             # just function boundary tracing
sudo kldload systrace        # just syscall tracing
```

Para desenvolvimento, `dtraceall` é a escolha mais simples: todos os providers que o leitor possa precisar ficam disponíveis.

Para confirmar que o DTrace está funcionando:

```sh
sudo dtrace -l | head -20
```

Isso lista os primeiros vinte probes que o DTrace conhece. Em um kernel FreeBSD 14 padrão com `dtraceall` carregado, a saída inclui centenas de milhares de probes, a maioria proveniente do provider `fbt` (cada função do kernel tem um probe de entrada e retorno). Uma contagem rápida:

```sh
sudo dtrace -l | wc -l
```

Um número típico é de 40.000 a 80.000 probes, dependendo de quais módulos estão carregados.

Para listar apenas os probes nas funções do driver `myfirst`:

```sh
sudo dtrace -l -n 'fbt::myfirst_*:'
```

Se o driver estiver carregado, isso produz uma lista de cada probe de entrada e retorno de cada função `myfirst_*`. Se o driver não estiver carregado, a lista fica vazia: os probes do `fbt` só existem para código que o kernel carregou.

### Escrevendo Scripts Simples de DTrace

A invocação mais simples do DTrace rastreia um único probe e imprime quando ele dispara:

```sh
sudo dtrace -n 'fbt::myfirst_pci_attach:entry'
```

A sintaxe é `provider:module:function:name`. O duplo dois-pontos em `fbt::myfirst_pci_attach:entry` usa o módulo vazio padrão (que corresponde a qualquer módulo). O DTrace imprime uma linha por disparo de probe:

```text
CPU     ID                    FUNCTION:NAME
  2  28791         myfirst_pci_attach:entry
```

Isso confirma que a função foi chamada, na CPU 2, com o ID interno de probe 28791. Para uma pergunta pontual "esta função foi executada?", isso já é suficiente.

Um script um pouco mais útil rastreia a entrada e o retorno:

```sh
sudo dtrace -n 'fbt::myfirst_pci_attach:entry,fbt::myfirst_pci_attach:return'
```

Cada linha mostra quando a entrada e o retorno dispararam. Os dois juntos informam ao leitor que a função foi executada e retornou sem entrar em pânico.

Para rastrear várias funções de uma vez, use um curinga:

```sh
sudo dtrace -n 'fbt::myfirst_*:entry'
```

A entrada de cada função `myfirst_*` produz uma linha. Para o leitor depurando um caminho de attach, essa é uma maneira simples de ver a sequência de chamadas em tempo real.

Para capturar argumentos, adicione uma action na linguagem D:

```sh
sudo dtrace -n 'fbt::myfirst_pci_suspend:entry { printf("%d: suspending %s", timestamp, stringof(args[0]->name)); }'
```

`args[0]` refere-se ao primeiro argumento da função rastreada. Para probes do FBT, `args[0]` é o primeiro argumento da função. Para `myfirst_pci_suspend`, o primeiro argumento é um `device_t`, e `args[0]->name` (se o layout de `device_t` do FreeBSD tiver um campo `name` acessível ao DTrace) exibe o nome do dispositivo.

Observação: `device_t` é, na verdade, um ponteiro para uma estrutura opaca, e o DTrace só pode seguir cadeias de ponteiros com conhecimento de tipo se os dados CTF estiverem carregados. É por isso que `options DDB_CTF` e `makeoptions WITH_CTF=1` importam: sem eles, o DTrace vê `args[0]` como um inteiro e não consegue desreferenciá-lo de forma significativa. Com eles, o DTrace sabe que `args[0]` é um `device_t` (que é um `struct _device *`) e consegue percorrer a estrutura.

Um script mais interessante conta quantas vezes cada função `myfirst_*` dispara:

```sh
sudo dtrace -n 'fbt::myfirst_*:entry { @counts[probefunc] = count(); }'
```

Deixe isso rodar por um minuto enquanto o driver é exercitado. Pressione Ctrl-C. O DTrace imprime um histograma:

```text
  myfirst_pci_attach                                                1
  myfirst_pci_detach                                                1
  myfirst_pci_suspend                                              42
  myfirst_pci_resume                                               42
  myfirst_intr_filter                                           10342
  myfirst_rx_task                                                5120
  myfirst_dma_submit                                             1024
```

O leitor vê, de relance, a distribuição de chamadas do driver: dois attach/detach, quarenta e dois ciclos de suspend/resume, dez mil filtros de interrupção, cinco mil tarefas rx, mil submissões DMA. Qualquer número inesperado é um sinal para investigar. Se o leitor esperava uma relação de um para um entre interrupções e tarefas, e vê dois para um, há um bug.

### Medindo a Latência de Funções

Um dos idiomas mais úteis do DTrace é medir quanto tempo uma função leva. O padrão é capturar o timestamp na entrada, o timestamp no retorno e subtrair:

```sh
sudo dtrace -n '
fbt::myfirst_pci_suspend:entry { self->start = timestamp; }
fbt::myfirst_pci_suspend:return /self->start/ {
    @times = quantize(timestamp - self->start);
    self->start = 0;
}'
```

A primeira cláusula armazena o timestamp de entrada em uma variável thread-local `self->start`. A segunda cláusula, condicionada ao fato de `self->start` ser diferente de zero, computa o delta, adiciona-o a uma agregação quantize (um histograma logarítmico) e limpa a variável. Deixe isso rodar enquanto o leitor exercita os ciclos de suspend, pressione Ctrl-C, e o DTrace imprime o histograma:

```text
           value  ------------- Distribution ------------- count
            1024 |                                         0
            2048 |@@@@                                     4
            4096 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@             28
            8192 |@@@@@@@@                                 8
           16384 |@@                                       2
           32768 |                                         0
```

Os números estão em nanossegundos. Isso indica: a maioria das chamadas de suspend levou entre 4.096 e 8.192 nanossegundos (4 a 8 microssegundos), com uma cauda chegando a 16.384 nanossegundos (16 microssegundos). Para uma chamada de suspend que deveria ser rápida, esse é o tipo de medição que diz ao leitor se a disciplina do driver está funcionando.

Para um tipo diferente de visão, use as agregações `avg()` ou `max()`:

```sh
sudo dtrace -n '
fbt::myfirst_pci_suspend:entry { self->start = timestamp; }
fbt::myfirst_pci_suspend:return /self->start/ {
    @avg = avg(timestamp - self->start);
    @max = max(timestamp - self->start);
    self->start = 0;
}'
```

Isso imprime o tempo médio e o tempo máximo de suspend ao final. Útil para relatórios de resumo.

### Usando o Provider `syscall`

O provider `syscall` exibe todas as chamadas de sistema. Isso é especialmente útil no desenvolvimento de drivers, porque as chamadas feitas pelo espaço do usuário ao driver (`open`, `read`, `write`, `ioctl`) passam primeiro pela camada de syscall. Um script que monitora chamadas de sistema em um descritor de arquivo específico é uma forma leve de ver como os programas de usuário utilizam o driver:

```sh
sudo dtrace -n 'syscall::ioctl:entry /execname == "myftest"/ { printf("%s ioctl on fd %d", execname, arg0); }'
```

Isso imprime toda chamada de sistema `ioctl` feita por um processo chamado `myftest`. A variável `execname` é uma variável embutida do DTrace que contém o nome do executável do processo atual. Executar o programa de teste `myftest` (um pequeno programa de espaço do usuário que exercita o driver) enquanto esse script está ativo oferece uma visão em tempo real de cada ioctl que o programa emite.

### Usando o Provider `sched`

O provider `sched` rastreia eventos do escalonador: trocas de thread, acordadas, enfileiramentos. É útil para compreender a concorrência no kernel, incluindo o tratamento de interrupções no driver:

```sh
sudo dtrace -n 'sched:::on-cpu /execname == "myftest"/ { @[cpu] = count(); }'
```

Isso conta, por CPU, quantas vezes o programa `myftest` foi escalonado em cada CPU durante o rastreamento. Útil para entender se o escalonador está mantendo o programa em um único núcleo ou distribuindo-o entre núcleos, o que afeta o comportamento de cache do driver.

### Usando o Provider `io`

O provider `io` exibe eventos de I/O de bloco. Não é diretamente relevante para o `myfirst` (um driver PCI genérico), mas é importante conhecê-lo. Um script para exibir todo I/O de disco:

```sh
sudo dtrace -n 'io:::start { printf("%s %d", execname, args[0]->bio_bcount); }'
```

Para drivers de armazenamento, o `io` é essencial. Para o `myfirst`, é periférico. O Capítulo 23 não o explora em profundidade.

### Rastreamento Estaticamente Definido (SDT)

O provider `fbt` instrumenta toda entrada e retorno de função. Isso é flexível, porém abrangente demais: você obtém probes de entrada e retorno para cada função, quando muitas vezes o que você quer são probes em pontos específicos e interessantes dentro de uma função.

O SDT (Statically Defined Tracing, ou rastreamento estaticamente definido) resolve esse problema. O SDT permite que o autor do driver adicione probes com nome em locais específicos do código-fonte, e o DTrace os torna visíveis. Quando o DTrace não está monitorando, os probes são essencialmente gratuitos: compilam para uma única instrução no-op. Quando o DTrace se conecta, o no-op é substituído por uma trap que dispara o probe.

A maquinaria do SDT está em `/usr/src/sys/sys/sdt.h`. As macros básicas são:

- `SDT_PROVIDER_DEFINE(name)`: declara um provider. Normalmente feito uma vez por driver, no arquivo-fonte principal.
- `SDT_PROBE_DEFINEN(provider, module, function, name, "arg1-type", "arg2-type", ...)`: declara um probe com N argumentos tipados.
- `SDT_PROBEN(provider, module, function, name, arg1, arg2, ...)`: dispara o probe com os argumentos fornecidos.

Um exemplo mínimo para o driver `myfirst`. Em `myfirst_pci.c`, próximo ao início:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE2(myfirst, , attach, entry,
    "struct myfirst_softc *", "device_t");
SDT_PROBE_DEFINE2(myfirst, , attach, return,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE3(myfirst, , dma, submit,
    "struct myfirst_softc *", "bus_addr_t", "size_t");
SDT_PROBE_DEFINE2(myfirst, , dma, complete,
    "struct myfirst_softc *", "int");
```

Em seguida, no código do driver:

```c
int
myfirst_pci_attach(device_t dev)
{
    struct myfirst_softc *sc = device_get_softc(dev);
    int error;

    SDT_PROBE2(myfirst, , attach, entry, sc, dev);
    ...
    error = ...;  /* real attach code */
    ...
    SDT_PROBE2(myfirst, , attach, return, sc, error);
    return (error);
}
```

No espaço do usuário, o leitor agora pode observar os probes do `myfirst` pelo DTrace:

```sh
sudo dtrace -n 'myfirst:::' | head
```

Isso exibe todos os probes do myfirst. Para observar apenas eventos de DMA:

```sh
sudo dtrace -n 'myfirst:::dma-submit'
sudo dtrace -n 'myfirst:::dma-complete'
```

Para contar submissões por tamanho de DMA:

```sh
sudo dtrace -n 'myfirst:::dma-submit { @[args[2]] = count(); }'
```

Isso imprime um histograma dos tamanhos de transferência DMA ao final do rastreamento. Em uma sessão de depuração de driver, esse é o tipo de dado que transforma especulação em evidência.

O Estágio 2 do Capítulo 23 adiciona exatamente esses probes ao driver `myfirst`. O exercício ao final da Seção 5 percorre esse processo de adição. Os arquivos correspondentes estão em `examples/part-05/ch23-debug/stage2-sdt/`.

Drivers reais que usam SDT valem a pena ser estudados. `/usr/src/sys/dev/virtio/virtqueue.c` define:

```c
SDT_PROVIDER_DEFINE(virtqueue);
SDT_PROBE_DEFINE6(virtqueue, , enqueue_segments, entry, "struct virtqueue *",
    ...);
SDT_PROBE_DEFINE1(virtqueue, , enqueue_segments, return, "uint16_t");
```

e os dispara em `virtqueue_enqueue`:

```c
SDT_PROBE6(virtqueue, , enqueue_segments, entry, vq, desc, head_idx, ...);
...
SDT_PROBE1(virtqueue, , enqueue_segments, return, idx);
```

O leitor pode executar `sudo dtrace -l -n 'virtqueue:::'` em um sistema com dispositivos respaldados por virtio (comum em VMs) e ver esses probes imediatamente. Rastreá-los oferece uma visão ao vivo da atividade da virtqueue que seria impossível de reproduzir com `printf`.

### Providers Úteis para o Desenvolvimento de Drivers

Um resumo dos providers que um autor de driver usa com mais frequência:

- **`fbt`**: rastreamento de fronteiras de função. Cada função do kernel tem probes de entrada e retorno. Ideal para "essa função foi executada?" e "quais argumentos ela recebeu?".
- **`sdt`**: rastreamento estaticamente definido. Probes que o driver explicitamente adiciona. Ideal para observar eventos específicos do driver em pontos precisos.
- **`syscall`**: rastreamento de chamadas de sistema. Ideal para ver o que os programas de usuário estão pedindo ao kernel.
- **`sched`**: eventos do escalonador. Ideal para entender concorrência e uso de CPU.
- **`io`**: eventos de I/O de bloco. Ideal para trabalho com armazenamento.
- **`vfs`**: eventos de sistema de arquivos. Ideal para desenvolvimento de drivers de sistema de arquivos.
- **`proc`**: eventos do ciclo de vida de processos. Ideal para criação de processos, encerramento e entrega de sinais.
- **`lockstat`**: operações de lock. Ideal para análise de contenção de locks; tem custo elevado quando ativo.
- **`priv`**: eventos relacionados a privilégios. Útil para auditoria de segurança.

A maior parte do desenvolvimento de drivers usa `fbt`, `sdt` e `syscall` intensamente, e os demais de forma ocasional.

### Armadilhas Comuns no DTrace

Alguns padrões que pegam usuários novos de surpresa.

**Buffer pequeno demais.** O DTrace tem buffers por CPU; se um script dispara probes mais rápido do que o DTrace consegue esvaziá-los, eventos são descartados. O leitor verá uma mensagem como `dtrace: X drops on CPU N`. A correção normalmente consiste em tornar o predicado mais restritivo (disparar menos vezes) ou aumentar o tamanho do buffer com `-b 16m`.

**Esquecer de limpar variáveis locais de thread.** No padrão de medição de latência, o script define `self->start` na entrada e verifica `self->start != 0` no retorno. Se o leitor esquecer de limpá-la (`self->start = 0;` após o uso), a variável acumula estado e rastreamentos posteriores ficam confusos. Sempre emparelhe a definição com a limpeza.

**Usar `trace()` quando `printf()` seria mais claro.** `trace(x)` imprime o valor em um formato conciso que é difícil de ler. `printf("x=%d", x)` costuma ser melhor.

**Executar sem privilégios de root.** O DTrace exige root. Executar `dtrace` como um usuário comum falha com um erro de permissão. Sempre use `sudo dtrace`.

**Probes que não existem.** Se o nome de um probe estiver digitado incorretamente ou o módulo não estiver carregado, o `dtrace` imprime um erro. A correção é confirmar que o probe existe com `dtrace -l` antes de tentar usá-lo.

**A armadilha do `stringof()`.** Ao imprimir um ponteiro de string do kernel, o leitor frequentemente precisa de `stringof(ptr)`, e não de `ptr`, porque o DTrace não segue ponteiros do kernel automaticamente. Esquecer isso produz saída ininteligível.

### Exercício: Use o DTrace para Medir Quanto Tempo uma Operação de Leitura do Driver Leva

Um complemento prático para a Seção 5.

1. Certifique-se de que o kernel `MYDEBUG` está em execução e que o `dtraceall` está carregado.

2. Escreva um programa de usuário que leia de `/dev/myfirst0` cem vezes. O programa pode ser tão simples quanto:

   ```c
   #include <fcntl.h>
   #include <unistd.h>
   int main(void) {
       int fd = open("/dev/myfirst0", O_RDWR);
       char buf[4096];
       for (int i = 0; i < 100; i++)
           read(fd, buf, sizeof(buf));
       close(fd);
       return 0;
   }
   ```

   Compile com `cc -o myftest myftest.c`.

3. Em um terminal, execute o script de latência do DTrace no caminho de `read` do driver. Supondo que a função de leitura do driver seja `myfirst_read`:

   ```sh
   sudo dtrace -n '
   fbt::myfirst_read:entry { self->start = timestamp; }
   fbt::myfirst_read:return /self->start/ {
       @times = quantize(timestamp - self->start);
       self->start = 0;
   }'
   ```

4. Em outro terminal, execute o programa de usuário:

   ```sh
   ./myftest
   ```

5. Pressione Ctrl-C no script do DTrace. Leia o histograma. A maioria das leituras deve estar na faixa de poucos microssegundos (alguns milhares de nanossegundos).

6. Tire um screenshot ou salve a saída. Compare com sua expectativa. Se uma leitura estiver demorando mais do que o esperado, esse é o ponto de partida de uma sessão de depuração.

Opcional: execute o mesmo script em `fbt::myfirst_intr_filter:entry` e observe a distribuição de latência de interrupção.

### Encerrando a Seção 5

A Seção 5 apresentou o DTrace como o bisturi do kit de ferramentas de depuração. O DTrace é dinâmico: os probes se conectam ao código em execução sem necessidade de recompilação. É seguro: a linguagem D não tem laços nem alocação de memória. É abrangente: os providers expõem dezenas de milhares de pontos de observação em todo o kernel. Para o desenvolvimento de drivers, a combinação de `fbt` (probes automáticos em cada função) e `sdt` (probes personalizados em pontos específicos) dá ao leitor a capacidade de responder a quase qualquer pergunta do tipo "o que o driver está fazendo?" sem modificar o código-fonte.

O custo do DTrace quando inativo é essencialmente zero: os probes existem no kernel como instruções no-op ou slots de ponteiro de função. O custo quando ativo depende do que o script faz: uma contagem simples é barata, um histograma complexo é mais caro, e um script que imprime a cada probe pode saturar o buffer. Compreender essa troca faz parte de escrever scripts DTrace eficazes.

Com logging (Seção 2), pipelines de log (Seção 3), kernels de depuração (Seção 4) e DTrace (Seção 5) em uso, o leitor tem quatro visões complementares do comportamento do kernel. A Seção 6 adiciona a última visão essencial para o desenvolvimento de drivers: o `ktrace`, a ferramenta que mostra o que um programa de usuário está fazendo ao cruzar para o kernel pela interface do driver.



## Seção 6: Rastreando Atividade do Kernel com `ktrace` e `kdump`

O DTrace rastreia o kernel pelo lado do kernel: quais funções foram executadas, quanto tempo levaram, qual estado foi acessado. O `ktrace` rastreia a mesma atividade pelo lado do usuário: quais chamadas de sistema um processo fez, quais argumentos passou, quais valores de retorno obteve, quais sinais recebeu. Juntas, as duas ferramentas oferecem uma visão completa. O DTrace mostra o que o kernel fez; o `ktrace` mostra o que o programa pediu.

Para o desenvolvimento de drivers, o `ktrace` é a ferramenta de escolha quando a questão é "o que o programa de usuário está fazendo?". Um driver que recebe argumentos estranhos pode ser rastreado: talvez o programa esteja chamando `ioctl` com o número de comando errado, ou `write` com um buffer de comprimento zero, ou `open` com as flags erradas. O `ktrace` responde a essas perguntas registrando cada syscall que o programa emite.

### O que é o `ktrace`?

`ktrace(1)` é uma ferramenta de espaço do usuário que instrui o kernel a registrar um rastreamento das atividades de um ou mais processos em um arquivo. Os eventos registrados incluem:

- Toda entrada e retorno de chamada de sistema, com argumentos e valor de retorno.
- Todo sinal entregue ao processo.
- Todo evento `namei` (busca de nome de caminho).
- Todo buffer de I/O passado para leitura ou escrita.
- Trocas de contexto, em nível grosseiro.

O rastreamento é gravado em um arquivo binário (por padrão `ktrace.out`). Para lê-lo, o leitor executa `kdump(1)`, que traduz o rastreamento binário em texto legível por humanos.

O `ktrace` é uma ferramenta simples, muito mais antiga que o DTrace, e isso aparece em algumas interfaces. Mas para a questão central de "o que esse processo pediu ao kernel para fazer?", ele é ideal.

A implementação está em `/usr/src/sys/kern/kern_ktrace.c` e o cabeçalho voltado ao espaço do usuário está em `/usr/src/sys/sys/ktrace.h`. As ferramentas de espaço do usuário ficam em `/usr/src/usr.bin/ktrace/` e `/usr/src/usr.bin/kdump/`.

O kernel precisa de `options KTRACE` para suportar o `ktrace`. Essa opção está habilitada em `GENERIC` por padrão em todas as arquiteturas; o leitor não precisa recompilar um kernel para usar o `ktrace`.

### Iniciando um Rastreamento

A invocação mais simples se conecta a um processo em execução pelo PID:

```sh
sudo ktrace -p 12345
```

Isso instrui o kernel a começar a registrar o processo com PID 12345. O kernel grava os eventos em `ktrace.out` no diretório atual. Quando o leitor terminar, ele para o rastreamento:

```sh
sudo ktrace -C
```

Ou, de forma mais precisa, para o rastreamento de um PID específico:

```sh
sudo ktrace -c -p 12345
```

Para executar um comando desde o início com o rastreamento habilitado:

```sh
sudo ktrace /path/to/program arg1 arg2
```

Isso inicia o programa e o rastreia desde a primeira syscall. Quando o programa encerra, o arquivo de rastreamento permanece para o leitor examinar.

Flags úteis:

- `-d`: rastreia os descendentes também. Se o programa fizer fork, os processos filhos também serão rastreados.
- `-i`: herda o rastreamento através de `exec`. Se o programa executar outro programa via `exec`, o rastreamento continua. Sem `-i`, o rastreamento para no exec.
- `-t <mask>`: seleciona quais tipos de eventos registrar. A máscara é um conjunto de códigos de letras, como `cnios` (calls, namei, I/O, signals). O padrão inclui a maioria dos tipos.
- `-f <file>`: grava em um arquivo específico em vez de `ktrace.out`.
- `-C`: limpa todos os rastreamentos (para todos).

Uma combinação comum:

```sh
sudo ktrace -di -f mytrace.out -p 12345
```

Rastreia o processo, todos os descendentes, através de exec, no arquivo `mytrace.out`.

### Lendo a Saída com `kdump`

O arquivo de rastreamento binário é inútil por si só. O `kdump` o traduz:

```sh
kdump -f mytrace.out
```

A saída tem o seguinte aspecto:

```text
 12345 myftest  CALL  open(0x8004120a0,0x0)
 12345 myftest  NAMI  "/dev/myfirst0"
 12345 myftest  RET   open 3
 12345 myftest  CALL  ioctl(0x3,0x20006601,0x7fffffffe8c0)
 12345 myftest  RET   ioctl 0
 12345 myftest  CALL  read(0x3,0x7fffffffe900,0x1000)
 12345 myftest  GIO   fd 3 read 4096 bytes
                 "... data bytes ..."
 12345 myftest  RET   read 4096/0x1000
```

Cada linha contém um PID, o nome do processo, um tipo de evento e dados específicos do evento:

- Linhas `CALL` mostram a entrada de uma syscall, com os argumentos como números hexadecimais.
- Linhas `NAMI` mostram uma resolução de nome de caminho, com a string resolvida.
- Linhas `RET` mostram o retorno de uma syscall, com o valor retornado.
- Linhas `GIO` mostram buffers de I/O genérico, seguidos de um dump do conteúdo do buffer.
- Linhas `PSIG` mostram a entrega de um sinal.

A saída é densa, mas se torna legível com a prática. Um padrão comum para o trabalho com drivers:

1. Execute o programa do usuário sob `ktrace`.
2. Procure pelo `open` de `/dev/myfirst0`. Anote o descritor de arquivo que o kernel retornou.
3. Filtre as linhas seguintes para as operações nesse fd: `ioctl`, `read`, `write`, `close`.
4. Observe os argumentos exatos, especialmente para `ioctl`, onde o número do comando codifica qual operação o driver foi solicitado a executar.

Por exemplo, a linha `ioctl(0x3,0x20006601,0x7fffffffe8c0)` acima mostra o programa emitindo um ioctl no fd 3 com o comando `0x20006601`. O comando segue a codificação `_IO`/`_IOR`/`_IOW`/`_IOWR`: os 16 bits menos significativos representam o número do comando e os bits mais altos indicam a direção e o tamanho. Um autor de driver que esteja depurando um problema de ioctl pode examinar esse comando, compará-lo com as definições no cabeçalho do driver e confirmar que o programa está enviando exatamente o comando que o driver espera.

### Filtros Úteis do `kdump`

O `kdump` aceita diversos flags para filtrar a saída:

- `-t <mask>`: filtra por tipo, usando a mesma máscara que `ktrace -t`.
- `-E`: exibe o tempo decorrido desde o evento anterior.
- `-R`: exibe timestamps relativos em vez de absolutos.
- `-l`: exibe o PID e o nome do processo em cada linha.
- `-d`: faz hex-dump dos buffers de I/O (ativado por padrão nas linhas `GIO`).
- `-H`: hex dump com formatação mais legível.

Um refinamento comum:

```sh
kdump -f mytrace.out -E -t cnri
```

Tempo decorrido, entrada/retorno de syscall, namei e I/O.

Redirecionar para `grep` também é útil:

```sh
kdump -f mytrace.out | grep -E 'myfirst|ioctl|open|read|write|close' | less
```

Isso exibe apenas as linhas relevantes para o driver.

### Identificando Syscalls Relacionadas ao Driver

Um programa de usuário se comunica com um driver por meio de quatro syscalls principais: `open`, `ioctl`, `read`, `write` e `close`. Um autor de driver que rastreia um programa de usuário presta atenção nestes pontos:

- **`open("/dev/myfirst0", ...)`**: o programa abre o dispositivo. O valor de retorno é um file descriptor que as syscalls subsequentes utilizam.
- **`ioctl(fd, cmd, arg)`**: o programa emite um comando específico do dispositivo. O `cmd` codifica a operação; `arg` é geralmente um ponteiro para uma estrutura.
- **`read(fd, buf, size)`**: o programa lê `size` bytes para dentro de `buf`.
- **`write(fd, buf, size)`**: o programa escreve `size` bytes a partir de `buf`.
- **`close(fd)`**: o programa fecha o file descriptor.

Um trace que exibe essas syscalls em ordem representa a visão do lado do usuário da interface do driver. Um bug em que o programa passa um tamanho errado para `read` aparece imediatamente como `read(fd, buf, 0)` (tamanho zero) no trace. Um bug em que o programa esquece de chamar `close` aparece como a ausência de uma chamada a `close` antes do encerramento. Um bug em que o programa usa um comando `ioctl` indefinido aparece como um valor hexadecimal incomum no argumento `cmd`.

### Comparando `ktrace` e DTrace

As duas ferramentas se sobrepõem em alguns aspectos e se complementam em outros. Um guia resumido:

Use `ktrace` quando:

- Você quiser ver o que um programa de usuário está fazendo, da perspectiva do próprio programa.
- Você quiser um registro que possa salvar e revisar depois.
- Você quiser o mínimo de configuração (sem recompilar o kernel, sem escrever scripts).
- Você quiser capturar os argumentos das syscalls, incluindo strings apontadas por ponteiros e buffers de I/O.

Use DTrace quando:

- Você quiser ver o que está acontecendo dentro do kernel.
- Você quiser medir latência, distribuição ou contenção.
- Você quiser agregar dados ao longo de muitos eventos.
- Você precisar rastrear sem modificar o programa ou executá-lo sob um wrapper.

Para a maior parte do trabalho com drivers, você usará ambas as ferramentas. O `ktrace` responde à pergunta "o que o programa está solicitando?". O DTrace responde à pergunta "o que o driver está fazendo a respeito?". A combinação das duas fornece o quadro completo.

### Uma Nota sobre `KTR` vs. `ktrace`

Os nomes confundem novos autores de drivers. `ktrace` (em minúsculas) é a ferramenta de rastreamento em espaço do usuário descrita acima. `KTR` (em maiúsculas) é o framework de rastreamento de eventos dentro do kernel, habilitado com `options KTR`. São coisas distintas.

`KTR` é um buffer circular de alto desempenho para eventos do kernel, projetado para desenvolvedores do kernel que depuram o próprio kernel. Autores de drivers podem adicionar macros `CTR` (categorias 0, 1, etc.) ao seu código e observá-las pelo comando `show ktr` do `ddb`. Na prática, `KTR` é utilizado por alguns subsistemas centrais do kernel (escalonador, locking, alguns subsistemas de dispositivos) e raramente por drivers individuais. O Capítulo 23 não aprofunda `KTR`; o leitor curioso pode consultar `/usr/src/sys/sys/ktr.h` para as definições das macros e `/usr/src/sys/conf/NOTES` para as opções de configuração.

### Exercício: Rastrear um Programa Simples de Usuário Acessando Seu Driver

Um laboratório prático para a Seção 6.

1. Escreva um pequeno programa de usuário `myftest.c` que abre `/dev/myfirst0`, emite um ioctl, lê 4 KB, escreve 4 KB e fecha. A versão mais simples:

   ```c
   #include <sys/ioctl.h>
   #include <fcntl.h>
   #include <unistd.h>
   #include <stdio.h>

   int
   main(void)
   {
       int fd, error;
       char buf[4096];

       fd = open("/dev/myfirst0", O_RDWR);
       if (fd < 0) { perror("open"); return 1; }

       error = ioctl(fd, 0x20006601 /* placeholder */, NULL);
       printf("ioctl: %d\n", error);

       error = read(fd, buf, sizeof(buf));
       printf("read: %d\n", error);

       error = write(fd, buf, sizeof(buf));
       printf("write: %d\n", error);

       close(fd);
       return 0;
   }
   ```

   Compile: `cc -o myftest myftest.c`.

2. Rastreie o programa:

   ```sh
   sudo ktrace -di -f mytrace.out ./myftest
   ```

3. Leia o trace:

   ```sh
   kdump -f mytrace.out | less
   ```

4. Encontre as linhas relacionadas a `/dev/myfirst0`:

   ```sh
   kdump -f mytrace.out | grep -E 'myfirst0|CALL|RET|NAMI' | less
   ```

5. Confirme que cada syscall está presente, com os argumentos esperados. Para o ioctl, decodifique manualmente o número do comando: `0x20006601` significa direção IOC_OUT (byte alto 0x20), tamanho 0 (próximos 14 bits), tipo 'f' (0x66), comando 01. Um driver que definiu `MYFIRST_IOC_RESET = _IO('f', 1)` verá exatamente esse comando.

6. (Opcional) Execute o programa sob DTrace em um segundo terminal, contando as funções do driver:

   ```sh
   sudo dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }' &
   ./myftest
   ```

   Compare a contagem do DTrace com a sequência do ktrace. Você deverá ver que cada syscall do usuário disparou funções específicas do driver: `open` → `myfirst_open`, `ioctl` → `myfirst_ioctl`, `read` → `myfirst_read`, e assim por diante.

O exercício ancora as ferramentas em um trace específico e concreto. Todo autor de driver carrega esse padrão no seu conjunto de ferramentas pelo resto da carreira: trace do lado do usuário com `ktrace`, trace do lado do kernel com DTrace, correlacionados por tempo e PID.

### Encerrando a Seção 6

A Seção 6 apresentou o `ktrace` e o `kdump`, o par de rastreamento em espaço do usuário que complementa o DTrace. O `ktrace` registra as syscalls que um processo realiza; o `kdump` traduz o registro para texto; a combinação exibe a interface do driver da perspectiva do programa de usuário. Para um autor de driver, essa visão é essencial para depurar bugs que se originam no lado do usuário da fronteira do driver.

Com logging, análise de logs, kernels de debug, DTrace e `ktrace` em mãos, você dispõe das cinco ferramentas que resolvem a maioria das questões de depuração de drivers. A Seção 7 volta a atenção para as classes de bugs que essas ferramentas mais frequentemente encontram, com o sintoma característico de cada uma e a ferramenta mais indicada para diagnosticá-los.

## Seção 7: Diagnosticando Bugs Comuns em Drivers

As seis seções anteriores construíram uma caixa de ferramentas: logging, inspeção de logs, kernels de debug, DTrace e `ktrace`. A Seção 7 usa essa caixa de ferramentas nos bugs que você realmente vai encontrar. Todo autor experiente de drivers FreeBSD já se deparou com cada um deles pelo menos uma vez. O objetivo desta seção é descrever os sintomas, explicar a causa subjacente e associar cada classe de bug à ferramenta que o diagnostica com mais eficiência.

Nenhum dos bugs abaixo é exótico. Eles surgem de erros de codificação comuns, de uma compreensão incompleta do ambiente do kernel, ou de suposições que funcionam em uma plataforma mas não em outra. O leitor que aprende a reconhecer os sintomas economizará horas de frustração, porque o primeiro passo em direção a uma correção é sempre a classificação correta.

### 7.1 Vazamentos de Memória: o Crescimento Silencioso

Um vazamento de memória em um módulo do kernel é mais difícil de detectar do que um em um programa de usuário. Não há um equivalente ao `valgrind` que funcione de forma transparente, e o kernel não encerra quando o módulo é descarregado, de modo que um vazamento continua consumindo memória mesmo depois que o driver é removido. Em sistemas com longa execução, um pequeno vazamento é fatal: o pool do qual ele aloca cresce indefinidamente, outros subsistemas começam a falhar e, eventualmente, a máquina fica sem memória do kernel e entra em pânico.

O sintoma característico é simples: um pool `malloc` que cresce, mas nunca diminui. A ferramenta que expõe esse sintoma é `vmstat -m`:

```sh
vmstat -m | head -1
vmstat -m | grep -E 'Type|myfirst'
```

A saída se parece com:

```text
         Type InUse MemUse Requests  Size(s)
      myfirst    12     3K       48  256
```

As três colunas que importam são `InUse`, `MemUse` e `Requests`. `InUse` é o número de alocações desse pool que estão atualmente em uso. `MemUse` é a memória total ocupada por essas alocações, em kilobytes. `Requests` é o número total de alocações feitas desde que o pool foi criado, incluindo as que já foram liberadas.

Um pool saudável tem `InUse` aproximadamente constante sob uma carga de trabalho estável. Um pool com vazamento tem `InUse` que cresce sem limite.

Para confirmar um vazamento, execute a carga de trabalho que exercita o driver e tire dois snapshots de `vmstat -m` com alguns minutos de intervalo:

```sh
vmstat -m | grep myfirst
# run the workload for 5 minutes
vmstat -m | grep myfirst
```

Se o primeiro mostrar `InUse=12` e o segundo mostrar `InUse=4800`, e a carga de trabalho não era esperada para adicionar quatro mil alocações, o driver está vazando.

O segundo passo é identificar o caminho problemático. Cada chamada a `malloc(9)` é emparelhada com uma chamada esperada a `free(9)`. O vazamento está no caminho que aloca sem liberar. Procure no driver por `malloc(..., M_MYFIRST, ...)` e por `free(..., M_MYFIRST)`, e verifique se toda alocação tem um free correspondente em todos os caminhos de saída.

Os padrões de vazamento mais comuns em código de driver são:

1. **Vazamentos em caminhos de erro.** Uma alocação tem sucesso, uma etapa posterior falha, e o retorno de erro pula o `free`. A correção é um único caminho de limpeza com rótulos `goto fail;`, do tipo explorado no Capítulo 18.

2. **Vazamentos condicionais.** A memória é liberada somente quando um flag está ativo, e às vezes o flag não está. A correção é liberar de forma incondicional ou rastrear a posse com mais cuidado.

3. **Destrutor de contexto esquecido.** Um objeto é alocado por file descriptor aberto e armazenado em `si_drv1` ou similar, mas o handler `d_close` não o libera. A correção é tratar o `d_close` (ou o callback de destruição do `cdev`) como a contraparte simétrica do `d_open`.

4. **Vazamento em timer ou taskqueue.** Uma tarefa é agendada, sua rotina de limpeza aloca memória para processamento diferido, mas a tarefa é cancelada antes de executar e o buffer alocado nunca é liberado.

Uma vez identificado o caminho, adicione o `free` ausente, recarregue o módulo, reexecute a carga de trabalho e confirme com `vmstat -m` que `InUse` agora se estabiliza. Um pequeno logging adicional costuma ser útil: um `device_printf` no caminho de alocação e outro no caminho de liberação expõem rapidamente a proporção de alocações para liberações no `dmesg`. Para um driver que usa uma macro `DPRINTF`, uma classe dedicada `MYF_DBG_MEM` torna o rastreamento de memória opcional e configurável em tempo de execução, que é exatamente o que a Seção 8 implementará para o driver `myfirst`.

O DTrace também pode ajudar aqui, especialmente em kernels com `KDTRACE_HOOKS`. Um one-liner simples conta as chamadas a `malloc` e `free` do driver e exibe qualquer desequilíbrio:

```sh
dtrace -n 'fbt::malloc:entry /execname == "kernel"/ { @["malloc"] = count(); }
           fbt::free:entry   /execname == "kernel"/ { @["free"]   = count(); }'
```

Uma abordagem ainda mais específica para o driver é adicionar probes DTrace ao próprio driver, uma em cada alocação e outra em cada liberação, e deixar o DTrace contar a proporção. Essa é a técnica que a Seção 8 demonstra para o driver `myfirst`.

### 7.2 Condições de Corrida: o Bug Raro e Destrutivo

Condições de corrida são os bugs de driver mais difíceis de reproduzir e os mais fáceis de ignorar. Uma corrida acontece quando duas threads acessam o mesmo estado sem sincronização correta, e o comportamento resultante depende do timing relativo das duas threads. Sob carga leve, a corrida pode nunca se manifestar; sob carga pesada, pode ocorrer centenas de vezes por segundo.

Os sintomas de condições de corrida variam bastante:
- panics esporádicos em lugares imprevisíveis
- corrupção de dados (valores que jamais deveriam existir)
- falhas de asserção ocasionais relacionadas a locks, como "mutex myfirst_lock not owned"
- stacks com aparência impossível, em que duas threads parecem estar dentro da mesma região protegida por mutex

Nenhum sintoma isolado prova uma corrida, mas qualquer padrão de bugs que aparece apenas sob alta carga deve levantar suspeitas.

A ferramenta FreeBSD mais eficaz para encontrar corridas é o `WITNESS`, conforme introduzido na Seção 4. Um kernel compilado com `options WITNESS` examina cada operação de lock e entra em pânico diante de qualquer violação da ordem de locks declarada, de qualquer tentativa de adquirir um spin lock enquanto se mantém um sleep lock, de qualquer chamada a uma função que dorme enquanto se mantém um spin lock, e de muitos outros erros relacionados.

Quando o `WITNESS` entra em pânico ou imprime uma violação, ele produz um stack trace mostrando:
- qual lock estava sendo adquirido
- quais locks já estavam sendo mantidos
- a ordem declarada que a aquisição violaria
- ambas as stacks (o lock atual e o que conflita)

A correção é geralmente uma das seguintes:

1. **Reordene as aquisições.** Se o código adquire o lock A e depois o lock B enquanto outro caminho adquire B e depois A, um deles precisa ser alterado.
2. **Adicione um lock ausente.** Se um estado é acessado sem nenhum mutex, adicione o mutex `MTX_DEF` apropriado ao redor do acesso.
3. **Remova um lock redundante.** Às vezes dois locks protegem estados sobrepostos e um deles pode ser eliminado.
4. **Troque os tipos de lock.** Spin locks e sleep locks têm regras diferentes. Uma região que precisa dormir deve usar um sleep lock. Uma região chamada a partir do contexto de interrupção frequentemente deve usar um spin lock.

Um driver que não apresenta nenhuma violação de `WITNESS` não está livre de condições de corrida, porque o `WITNESS` detecta apenas problemas de ordenação de locks. Corridas sobre estados acessados sem nenhum lock exigem uma abordagem diferente: leitura cuidadosa do código, probes DTrace nos pontos suspeitos para confirmar o timing e, ocasionalmente, asserções `mtx_assert(&sc->sc_mtx, MA_OWNED)` espalhadas pelas seções críticas. `INVARIANTS` habilita essas asserções, o que é mais um motivo para executar um kernel de depuração durante o desenvolvimento.

Entre `WITNESS`, `INVARIANTS`, `mtx_assert` e contadores DTrace nos caminhos onde há suspeita de contenção, a maioria das corridas pode ser localizada em uma ou duas horas. Corridas que sobrevivem a esse arsenal são raras e quase sempre envolvem estruturas de dados sem lock, operações atômicas ou suposições sobre ordenação de memória que exigem uma revisão cuidadosa com um engenheiro sênior.

### 7.3 Uso Incorreto de bus_space e bus_dma

Um novo autor de driver frequentemente se depara com bugs de acesso ao barramento que compartilham uma família característica de sintomas:

- leituras retornam 0xFF, 0xFFFFFFFF, ou outras constantes suspeitas
- escritas parecem ter êxito, mas o dispositivo não reage
- a máquina funciona por segundos ou minutos e então trava ou entra em pânico
- o comportamento está correto em uma arquitetura (amd64) e errado em outra (arm64, riscv64)

Cada um desses sintomas aponta para um uso incorreto de `bus_space` ou `bus_dma`.

O primeiro erro é ignorar o handle. `bus_space_read_4()` recebe uma `bus_space_tag_t` e uma `bus_space_handle_t`, obtidas de `rman_get_bustag()` e `rman_get_bushandle()` no recurso alocado durante o attach. Usar o endereço físico bruto do registrador, ou um ponteiro obtido por meio de um cast direto do recurso, contorna a abstração de barramento da plataforma. No amd64 o programa pode aparentemente funcionar (o kernel mapeia a região MMIO de forma a tolerar isso), mas no arm64 o acesso ao registrador falha.

A correção é mecânica: sempre utilize `bus_space_read_N()` / `bus_space_write_N()` ou os helpers mais novos baseados em recursos `bus_read_N()` / `bus_write_N()`, nunca desreferencie ponteiros brutos para memória de dispositivo.

O segundo erro é o tamanho incorreto. `bus_space_read_4` lê um valor de 32 bits. Se o registrador é de 16 bits e o código lê 4 bytes, ele acaba lendo também o registrador adjacente, e o valor desse registrador passa a aparecer nos bits 16 a 31 do valor retornado. Pior ainda, há dispositivos que não toleram um tamanho incorreto e respondem com erro. A correção é usar a variante correta (`read_1`, `read_2`, `read_4` ou `read_8`) para a largura de cada registrador, conforme documentado no datasheet do dispositivo.

O terceiro erro é o offset incorreto. O handle do `bus_space` aponta para a base da região mapeada; o offset é somado para calcular o endereço do registrador. Um erro de digitação no offset faz o código ler um registrador diferente. Por exemplo, ler o offset `0x18` em vez de `0x10` produz um valor inesperado, e a lógica subsequente do driver fica baseada em uma leitura falsa. A correção é definir cada offset como uma constante nomeada em um arquivo de cabeçalho e referenciar a constante em vez do número: `#define MYFIRST_REG_STATUS 0x10`, `#define MYFIRST_REG_CONFIG 0x14`, e assim por diante.

Para DMA, o erro mais comum é liberar ou reutilizar um buffer enquanto o dispositivo ainda está lendo ou escrevendo nele. O sintoma característico é corrupção intermitente de dados, às vezes apenas sob alta carga. A causa é a ausência de chamadas a `bus_dmamap_sync`, um buffer `bus_dmamem` liberado enquanto o descriptor ainda está na fila, ou uma direção incorreta em `bus_dmamap_sync`.

A abordagem de diagnóstico consiste em registrar cada operação de mapeamento, sincronização e desmapeamento de DMA, e então cruzar esse log com o estado do anel de descriptors. Um one-liner de DTrace nos caminhos de DMA do driver normalmente é suficiente para identificar o erro:

```sh
dtrace -n 'fbt::myfirst_dma_sync:entry { printf("%s dir=%d", probefunc, arg1); }'
```

Para um tratamento completo das regras e armadilhas de DMA, o Capítulo 21 é a referência. O papel deste capítulo de depuração é ajudar o leitor a reconhecer os sintomas e identificar a causa.

### 7.4 Use-After-Detach

Uma classe mais sutil de bug ocorre quando o caminho de detach do driver retorna, o softc do driver é liberado, mas alguma outra parte do kernel ainda mantém uma referência à memória liberada. Exemplos incluem:

- uma interrupção que chega após `bus_release_resource`, mas antes que o handler seja desmontado
- um callout que dispara após o detach, desreferenciando um softc já liberado
- uma probe DTrace no driver que dispara a partir de uma task ainda em execução no momento do descarregamento
- um nó de dispositivo de caracteres mantido aberto por um processo do usuário, recebendo I/O enquanto o driver está sendo descarregado

Os sintomas são quase sempre fatais: page faults em código do driver acessado após o detach, kernel panics com pilhas corrompidas, ou dados espúrios no buffer de mensagens do kernel logo antes do panic.

A correção tem vários componentes, cada um dos quais o caminho de detach deve implementar em uma ordem específica:

1. Primeiro, impeça novas entradas: defina um flag no softc (`sc->sc_detaching = 1`), ou adquira um write lock que todos os pontos de entrada verifiquem, de modo que novas chamadas percebam que o driver está sendo encerrado.

2. Aguarde a conclusão dos chamadores em andamento. `bus_teardown_intr` drena o interrupt handler. `callout_drain` aguarda a conclusão de um callout pendente. `taskqueue_drain` drena qualquer task adiada. `destroy_dev_sched_cb` aguarda o fechamento dos descritores de arquivo abertos.

3. Somente após todos os chamadores externos terem sido drenados, libere os recursos que o driver alocou no attach: libere memória, libere IRQs, libere recursos de memória, destrua mutexes.

O princípio é simples: o detach é a imagem espelhada do attach, e todo recurso alocado no attach deve ser liberado no detach na ordem inversa. Violações desse princípio produzem bugs de use-after-detach.

A ferramenta de depuração preferida é o próprio kernel de depuração. Um kernel com `DEBUG_MEMGUARD` habilitado pode ser configurado para envenenar a memória liberada, de modo que um acesso a um softc liberado produza um page fault imediato com uma pilha clara, em vez de uma corrupção sutil que pode levar horas para se manifestar.

### 7.5 Erros em Interrupt Handlers

Autores de drivers inexperientes com o kernel às vezes tratam interrupt handlers como funções comuns e cometem erros frequentes, mas prejudiciais:

- chamar uma função que dorme (`malloc(..., M_WAITOK, ...)`, `tsleep`, `mtx_lock` de um sleep lock, `copyin`, `uiomove`) a partir de um interrupt handler de nível filter
- tentar manter um sleep lock por um período prolongado
- ler ou escrever um grande bloco de dados dentro do interrupt handler em vez de usar um `taskqueue`
- não fazer o ack da interrupção, fazendo com que o handler dispare indefinidamente em loop

Cada um desses erros tem um sintoma distinto. Dormir em contexto de interrupção produz uma violação de `WITNESS` ou, em kernels de produção, uma corrupção silenciosa do estado do escalonador. Manter um sleep lock por tempo demais faz outras threads girarem ou dormirem, e a latência dispara. Fazer trabalho pesado no handler bloqueia interrupções subsequentes e degrada a responsividade de todo o sistema. Não fazer o ack da interrupção prende uma CPU a 100% no tratamento de interrupções indefinidamente.

A solução é sempre a mesma: mantenha o interrupt handler curto e atômico. Ele deve:
1. Ler o registrador de status para determinar a causa.
2. Reconhecer a interrupção escrevendo no registrador de status.
3. Repassar qualquer trabalho substancial para um `taskqueue` ou `ithread`, conforme explorado no Capítulo 19.
4. Retornar.

Processamento complexo, alocação de memória e operações de longa duração pertencem ao caminho adiado, não ao próprio handler.

DTrace é particularmente útil para diagnosticar o desempenho de interrupt handlers. O provider `intr`, ou uma probe `fbt` na entrada do handler, pode medir quanto tempo cada invocação leva:

```sh
dtrace -n 'fbt::myfirst_intr:entry { self->t = timestamp; }
           fbt::myfirst_intr:return /self->t/ {
               @ = quantize(timestamp - self->t); self->t = 0; }'
```

Um handler saudável retorna em alguns microssegundos. Se a quantização mostrar invocações na casa de centenas de microssegundos ou milissegundos, o handler está fazendo trabalho demais e deve ser refatorado.

### 7.6 Erros de Sequenciamento no Ciclo de Vida

O ciclo de vida do driver passa por probe, attach, open, close, detach e unload. Cada método tem regras sobre o que pode fazer e o que deve ter acontecido antes. Violar essas regras produz bugs característicos:

- chamar `bus_alloc_resource_any` durante o probe (o que deve ocorrer no attach) resulta em alocações parciais e confusão na lógica de ordenação do probe
- fazer trabalho substancial no probe retarda significativamente o boot, porque o mesmo probe é executado para cada dispositivo candidato
- criar o `cdev` antes de o hardware ser inicializado permite que processos do usuário abram o dispositivo e recebam I/O em um estado não inicializado
- destruir o `cdev` no detach enquanto outras threads ainda o mantêm aberto corrompe o estado do devfs
- fazer trabalho significativo no unload do módulo em vez de no detach deixa recursos alocados por attach sem ser liberados

A solução é manter cada método do ciclo de vida focado em sua responsabilidade:

- **probe** apenas identifica o dispositivo e retorna `BUS_PROBE_DEFAULT` ou um erro; sem alocações, sem registro
- **attach** aloca recursos, inicializa o softc, configura interrupções, cria o `cdev`, registra o dispositivo
- **detach** reverte o attach exatamente, na ordem inversa
- **open / close** gerenciam o estado por arquivo sem tocar nos recursos globais do dispositivo
- **unload** realiza apenas a limpeza no nível do módulo; a limpeza por dispositivo pertence ao detach

Quando um driver segue essa estrutura, bugs de ciclo de vida são raros. Quando desvia, eles são comuns e dolorosos.

### 7.7 Erros em Cópias para e do Espaço do Usuário

As funções `copyin(9)`, `copyout(9)` e as relacionadas `fueword` / `suword` transferem dados através da fronteira entre o espaço do usuário e o espaço do kernel. Elas são protegidas pelo sistema de memória virtual: se o endereço no espaço do usuário for inválido, a cópia retorna `EFAULT` em vez de causar um panic. Porém, essa proteção só se aplica se a cópia for feita por meio dessas funções. Um driver que desreferencia um ponteiro de espaço do usuário diretamente entrará em panic no momento em que o ponteiro do usuário for inválido, o que na prática acontece com frequência.

O sintoma é um panic com um endereço de espaço do usuário na instrução que causou a falha, com a pilha apontando para o caminho read/write/ioctl do driver.

A correção é obrigatória: sempre que dados do usuário cruzarem a fronteira, use `copyin`/`copyout` ou um `uiomove` por meio de uma `struct uio`. Nunca faça cast de um ponteiro do usuário para um ponteiro do kernel e o desreferencie. Essa regra é absoluta.

Para ioctl especificamente, o handler `d_ioctl` recebe um ponteiro do kernel, porque o kernel já copiou o argumento de tamanho fixo do espaço do usuário. Mas se o argumento do ioctl é uma estrutura que contém um ponteiro para um buffer maior do usuário, esse ponteiro embutido ainda é do espaço do usuário, e `copyin` é necessário para acessar com segurança o buffer para o qual ele aponta.

### 7.8 Bugs no Nível do Módulo

Alguns bugs afetam o módulo inteiro em vez de um único dispositivo:

- o módulo falha ao carregar: o loader imprime um erro no `dmesg`; leia e corrija o sintoma (dependência ausente, falha na resolução de símbolos, incompatibilidade de versão)
- o módulo falha ao descarregar: geralmente significa que um dispositivo ainda está attached. Desconecte todos os dispositivos (`devctl detach myfirst0`) antes de executar `kldunload`
- o módulo descarrega, mas deixa resíduos: o caminho de unload (handler de eventos `module_t`, ou o unload de `DRIVER_MODULE_ORDERED`) não reverteu o carregamento do módulo. Corrija lendo o caminho de unload e tornando-o simétrico ao load.

As ferramentas de depuração são simples: `dmesg | tail` mostra as mensagens de load/unload, `kldstat -v` mostra o estado do módulo, `vmstat -m` mostra se o pool de malloc do módulo foi drenado no unload.

### 7.9 Uma Lista de Verificação de Depuração

A seguir há uma lista de verificação compacta que relaciona ferramentas a sintomas. Os leitores podem mantê-la à mão durante o desenvolvimento de drivers.

| Sintoma                                              | Primeira ferramenta           | Segunda ferramenta              |
|------------------------------------------------------|-------------------------------|---------------------------------|
| Linha ausente ou incorreta no `dmesg`                | `dmesg`, `/var/log/messages`  | adicionar `device_printf`       |
| `InUse` aumenta em `vmstat -m`                       | `vmstat -m`                   | contagem malloc com DTrace      |
| Panic por ordem de lock                              | trace `WITNESS`               | `ddb> show locks`               |
| Corrupção esporádica sob carga                       | `WITNESS`, `INVARIANTS`       | temporização com DTrace         |
| Leituras de registrador retornam 0xFFFFFFFF          | revisar handle do `bus_space` | datasheet, offset               |
| Máquina trava no uso do dispositivo                  | DTrace `fbt::myfirst_*`       | `ddb> bt`                       |
| Panic após detach                                    | `DEBUG_MEMGUARD`              | auditar a ordem do detach       |
| Sistema lento com driver carregado                   | provider `intr` do DTrace     | encurtar o handler              |
| ioctl com comando incorreto                          | `ktrace` / `kdump`            | decodificar o comando           |
| Panic com endereço de espaço do usuário na falha     | revisar `copyin`/`copyout`    | auditar ponteiro no ioctl       |
| Módulo não descarrega                                | `kldstat -v`                  | `devctl detach`                 |

A tabela não é exaustiva, mas cobre a grande maioria dos bugs reais em drivers. Com ela e as ferramentas das Seções 1 a 6, o leitor dispõe de uma abordagem estruturada para a maioria dos diagnósticos em campo.

### Encerrando a Seção 7

A Seção 7 direcionou o conjunto de ferramentas de logging, análise de logs, kernels de depuração, DTrace e `ktrace` para os bugs com que o leitor tem mais probabilidade de se deparar: vazamentos de memória, condições de corrida, erros de acesso ao barramento, erros do tipo use-after-detach, erros de interrupção, erros de ciclo de vida, bugs na fronteira entre espaço do usuário e espaço do kernel, e problemas em nível de módulo. Para cada classe, a seção descreveu o sintoma, a causa subjacente e a ferramenta que a diagnostica com mais eficiência. O checklist ao final torna o mapeamento explícito, e as próximas sessões no teclado devem permitir que o leitor alcance a ferramenta certa na primeira tentativa, e não na terceira.

A lacuna que ainda resta é o próprio driver. Até aqui neste capítulo, o `myfirst` foi usado principalmente como alvo de ferramentas externas. A Seção 8 fecha essa lacuna refatorando o driver para expor pontos de rastreamento e um controle de verbosidade de depuração próprio, de modo que a instrumentação se torne parte orgânica do código, e não uma providência de última hora. A versão passa para `1.6-debug` e o driver ganha `myfirst_debug.h` e a estrutura que o acompanhará pelo restante do livro.

## Seção 8: Refatoração e Versionamento com Pontos de Rastreamento

As sete seções anteriores deste capítulo foram dedicadas a aprender a usar as ferramentas que já existem no FreeBSD: `printf`, `dmesg`, kernels de debug, DTrace, `ktrace`, e a disciplina de interpretar suas saídas. A Seção 8 se volta para dentro. O driver `myfirst` que chegou do Capítulo 22 na versão `1.5-power` não tem uma infraestrutura de debug própria de verdade. Cada instrução de log é incondicional. Não há nenhum controle que o operador possa ajustar, nenhum sysctl para solicitar saída mais detalhada quando um problema surge, e nenhum ponto de rastreamento estático para conectar ao DTrace. A Seção 8 corrige essa lacuna.

O trabalho nesta seção é pequeno em linhas de código, mas grande em impacto. Ao final, o driver terá:

1. Um header `myfirst_debug.h` que define uma macro `DPRINTF`, bits de verbosidade e pontos de rastreamento SDT.
2. Uma árvore sysctl (`dev.myfirst.0.debug`) que configura a verbosidade em tempo de execução.
3. Um padrão de rastreamento de entrada-saída-erro aplicado de forma consistente em todo o driver.
4. Três probes do provedor SDT que expõem os eventos de abertura, fechamento e I/O.
5. Um documento `DEBUG.md` na árvore de exemplos descrevendo como configurar e interpretar a nova saída.
6. Um incremento de versão para `1.6-debug` registrado em `myfirst_version` e `MODULE_VERSION`.

O driver resultante será a base para os capítulos restantes do livro. Cada novo subsistema adicionado nas Partes 5, 6 e 7 se conectará a esse framework, de modo que a infraestrutura de rastreamento cresça junto com o driver em vez de ser acoplada como um apêndice no final.

### 8.1 Por Que um Header de Debug

Drivers FreeBSD maiores têm um arquivo header dedicado à infraestrutura de debug. O padrão é visível em `/usr/src/sys/dev/ath/if_ath_debug.h`, `/usr/src/sys/dev/bwn/if_bwn_debug.h`, `/usr/src/sys/dev/iwn/if_iwn_debug.h`, e muitos outros. Cada um desses headers:

1. define uma macro `DPRINTF` que verifica um bitmask de verbosidade no softc
2. declara um conjunto de classes de verbosidade como `#define MYF_DBG_INIT 0x0001`, `MYF_DBG_OPEN 0x0002`, e assim por diante
3. opcionalmente declara probes SDT que mapeiam as fronteiras funcionais do driver

Colocar isso em um header traz dois benefícios. Primeiro, a infraestrutura de debug é uma preocupação separada, extraída de forma limpa do código funcional. Segundo, quando um leitor quer entender a história de rastreamento do driver, o header é o único arquivo que precisa ler. O padrão é maduro, amplamente utilizado, e é o que `myfirst` adotará nesta seção.

O header ficará em `examples/part-05/ch23-debug/stage3-refactor/myfirst_debug.h`, e o código-fonte do driver o incluirá no topo:

```c
#include "myfirst_debug.h"
```

As mudanças complementares no código-fonte do driver são aditivas: as chamadas existentes de `device_printf` podem permanecer, e novas chamadas são adicionadas por meio da macro `DPRINTF`.

### 8.2 Declarando Classes de Debug

O primeiro passo é definir as classes de verbosidade. Uma classe é um único bit em uma máscara de 32 bits. O driver reserva um bit por área funcional. Para `myfirst`, oito classes são mais do que suficientes neste estágio:

```c
/* myfirst_debug.h */
#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/sdt.h>

#define MYF_DBG_INIT    0x00000001  /* probe/attach/detach */
#define MYF_DBG_OPEN    0x00000002  /* open/close lifecycle */
#define MYF_DBG_IO      0x00000004  /* read/write paths */
#define MYF_DBG_IOCTL   0x00000008  /* ioctl handling */
#define MYF_DBG_INTR    0x00000010  /* interrupt handler */
#define MYF_DBG_DMA     0x00000020  /* DMA mapping/sync */
#define MYF_DBG_PWR     0x00000040  /* power-management events */
#define MYF_DBG_MEM     0x00000080  /* alloc/free trace */

#define MYF_DBG_ANY     0xFFFFFFFF
#define MYF_DBG_NONE    0x00000000

#endif /* _MYFIRST_DEBUG_H_ */
```

O valor `MYF_DBG_NONE` é o padrão: nenhuma saída de debug. `MYF_DBG_ANY` habilita todas as classes, o que é útil durante o desenvolvimento. Uma configuração típica de operador pode habilitar apenas `MYF_DBG_INIT | MYF_DBG_OPEN` para obter eventos do ciclo de vida sem o ruído gerado por cada operação de I/O.

Cada bit é declarado como um único dígito hexadecimal ou um par, de modo que o operador pode definir a máscara com um valor simples: `sysctl dev.myfirst.0.debug=0x3` habilita o rastreamento de inicialização e abertura. Os nomes comentados deixam claro o propósito de cada bit.

### 8.3 A Macro DPRINTF

Em seguida, a macro que condiciona o log à máscara:

```c
#ifdef _KERNEL
#define DPRINTF(sc, m, ...) do {                                        \
        if ((sc)->sc_debug & (m))                                        \
                device_printf((sc)->sc_dev, __VA_ARGS__);                \
} while (0)
#endif
```

A macro recebe três argumentos: o ponteiro para o softc, o bitmask e a string de formato mais os argumentos variádicos. Ela se expande para um teste de `sc->sc_debug & m`, seguido de uma chamada a `device_printf` se o teste for bem-sucedido. O padrão `do { ... } while (0)` é o idioma padrão para macros com múltiplas instruções que precisam se comportar como uma única instrução em contextos `if`/`else`.

O custo de uma chamada `DPRINTF` quando o bit está desativado é um único carregamento de `sc->sc_debug`, um AND bit a bit e um branch. O branch quase sempre é predito como "não tomado", portanto o custo na prática é negligenciável. O driver pode distribuir chamadas `DPRINTF` livremente pelo código, e o usuário não paga nada quando o debug está desabilitado.

Quando o bit está ativado, a chamada se torna um `device_printf` normal, aparecendo no `dmesg` como qualquer outro log do kernel. Nada no comportamento difere do caminho de log habitual, exceto que o log agora é condicional.

### 8.4 Adicionando `sc_debug` ao softc

O softc ganha um novo campo, um `uint32_t sc_debug`. Seu valor é manipulado por um sysctl, que a função attach registra. O trecho relevante de `myfirst_debug.c`:

```c
struct myfirst_softc {
        device_t        sc_dev;
        struct mtx      sc_mtx;
        struct cdev    *sc_cdev;
        uint32_t        sc_debug;       /* debug verbosity mask */
        /* other fields as in 1.5-power */
};
```

A função attach inicializa o campo com zero e registra o sysctl:

```c
sc->sc_debug = 0;
sysctl_ctx_init(&sc->sc_sysctl_ctx);
sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
    OID_AUTO, "debug",
    CTLFLAG_RW, 0, "debug verbosity tree");

SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0, "debug class bitmask");
```

Após o attach, o sysctl aparece como:

```sh
sysctl dev.myfirst.0.debug.mask
```

e o operador pode alterá-lo quando quiser:

```sh
sysctl dev.myfirst.0.debug.mask=0x3     # enable INIT + OPEN
sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF   # enable all classes
sysctl dev.myfirst.0.debug.mask=0        # disable
```

A rotina detach destrói o contexto do sysctl da mesma forma que faz atualmente:

```c
sysctl_ctx_free(&sc->sc_sysctl_ctx);
```

Um detalhe importa: colocar o campo do sysctl perto do topo do softc o posiciona na primeira linha de cache, onde o custo de acesso durante o DPRINTF é mínimo. Este é um ponto de desempenho pequeno, mas a ênfase do livro em código bem fundamentado justifica mencioná-lo.

### 8.5 O Padrão de Entrada / Saída / Erro

Com o `DPRINTF` instalado, as funções do driver podem usar um padrão consistente de rastreamento. Cada função substancial registra a entrada, a saída (ou erro) e qualquer estado intermediário relevante. Para uma função pequena como `myfirst_open`, isso se parece com:

```c
static int
myfirst_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d uid=%d flags=0x%x\n",
            td->td_proc->p_pid, td->td_ucred->cr_uid, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count >= MYFIRST_MAX_OPENS) {
                error = EBUSY;
                goto out;
        }
        sc->sc_open_count++;
out:
        mtx_unlock(&sc->sc_mtx);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_OPEN, "open failed: error=%d\n", error);
        else
                DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n",
                    sc->sc_open_count);

        return (error);
}
```

Três princípios orientam o padrão:

1. O log de entrada mostra quem chamou e com quais argumentos. Para open, isso inclui `pid`, `uid` e flags.
2. O log de saída mostra o resultado: "failed: error=N" ou "ok: ..." com qualquer estado relevante.
3. O estado intermediário é registrado quando é relevante. Para open, a nova contagem de aberturas é útil.

O padrão é o mesmo para close, read, write e ioctl. Cada função tem duas ou três chamadas `DPRINTF` delimitando o trabalho.

Essa disciplina se paga na primeira vez que um bug aparece. Sem o padrão, o desenvolvedor precisa adicionar log retroativamente, tentando adivinhar onde está o problema. Com o padrão, ativar um único bit (`sysctl dev.myfirst.0.debug.mask=0x2` para `MYF_DBG_OPEN`) produz um rastreamento completo de cada abertura e fechamento, com argumentos e resultados. O tempo de diagnóstico cai de horas para minutos.

### 8.6 Adicionando Probes SDT

A macro estática `DPRINTF` gera mensagens legíveis por humanos no `dmesg`. Os probes SDT produzem eventos legíveis por máquina que o DTrace pode agregar, filtrar e cronometrar. Ambos têm seu lugar. O driver declara probes SDT para os três eventos mais relevantes, e um usuário criterioso pode anexar scripts personalizados quando quiser.

Em `myfirst_debug.h`, as declarações de probe se parecem com:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);
```

As definições correspondentes ficam no código-fonte do driver (`myfirst_debug.c`):

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
```

As macros `DEFINE` registram os probes na infraestrutura SDT do kernel. `DEFINEn` recebe `n` argumentos, cada um com uma string de tipo no estilo C que descreve o que o script DTrace receberá como `arg0`, `arg1`, etc.

O driver então dispara os probes nos locais apropriados:

```c
/* in myfirst_open */
SDT_PROBE2(myfirst, , , open, sc, flags);

/* in myfirst_close */
SDT_PROBE2(myfirst, , , close, sc, flags);

/* in myfirst_read or myfirst_write */
SDT_PROBE4(myfirst, , , io, sc, is_write, (size_t)uio->uio_resid, uio->uio_offset);
```

O custo de um probe ao qual nenhum script está anexado é um único branch em torno de um no-op, o mesmo custo negligenciável do `DPRINTF` com o bit desativado. Quando um script DTrace se conecta, o branch é tomado, os argumentos são registrados e o script os vê como `args[0]`, `args[1]`, e assim por diante.

Com esses probes, o leitor pode agora executar scripts como:

```sh
dtrace -n 'myfirst::: { @[probename] = count(); }'
```

Isso conta cada evento de probe do `myfirst`. Para ver os bytes transferidos por segundo:

```sh
dtrace -n 'myfirst:::io { @["bytes"] = sum(arg2); }'
```

Ou para rastrear cada abertura com PID e flags:

```sh
dtrace -n 'myfirst:::open { printf("open pid=%d flags=0x%x", pid, arg1); }'
```

O driver agora expõe seu comportamento em duas formas: legível por humanos via `DPRINTF`, e analisável por máquina via SDT. Um operador pode escolher a forma adequada para cada situação.

### 8.7 Escrevendo o DEBUG.md

A árvore de exemplos ganha um documento curto (`examples/part-05/ch23-debug/stage3-refactor/DEBUG.md`) que explica a infraestrutura de debug e rastreamento para o leitor que fizer o download dos arquivos. O documento é curto, mas cobre:

1. O que é `DPRINTF` e como habilitá-lo.
2. A tabela de bits de classe.
3. Comandos `sysctl` de exemplo para habilitar cada classe.
4. A lista de probes SDT e a ordem dos argumentos.
5. Três exemplos de one-liners DTrace.
6. Como combinar `DPRINTF` e SDT para depuração de ponta a ponta.

O documento não é longo: cerca de trinta linhas. Seu propósito é tornar as ferramentas autodocumentadas, de modo que um leitor que retomar o exemplo depois de um ano ainda saiba como usá-las.

### 8.8 Incrementando a Versão

Cada capítulo do livro que altera o comportamento do driver incrementa sua versão. A regra é: a string de versão no driver corresponde à versão exibida no README da árvore de exemplos e no texto do capítulo.

No Capítulo 22, o driver chegou à versão `1.5-power`. A Seção 8 do Capítulo 23 o leva para `1.6-debug`. As mudanças em `myfirst_debug.c` são:

```c
static const char myfirst_version[] = "myfirst 1.6-debug";

MODULE_VERSION(myfirst, 16);
```

O texto acima da constante de versão diz explicitamente o que mudou:

```c
/*
 * myfirst driver - version 1.6-debug
 *
 * Added in this revision:
 *   - DPRINTF macro with 8 verbosity classes
 *   - sysctl dev.myfirst.0.debug.mask for runtime control
 *   - SDT probes for open, close, io
 *   - Entry/exit/error pattern across all methods
 */
```

A função `attach` registra a versão no nível `MYF_DBG_INIT`, para que o operador possa confirmar o driver carregado:

```c
DPRINTF(sc, MYF_DBG_INIT, "attach: %s loaded\n", myfirst_version);
```

Com a verbosidade habilitada, o leitor que carregar o driver verá:

```text
myfirst0: attach: myfirst 1.6-debug loaded
```

o que confirma tanto a identidade do módulo quanto a infraestrutura de debug.

### 8.9 Encerrando a Seção 8

A Seção 8 refatorou o driver `myfirst` para ter sua própria infraestrutura de debug. O driver agora tem uma máscara de verbosidade, um sysctl para defini-la em tempo de execução, um padrão consistente de entrada-saída-erro em cada função e três probes SDT nas fronteiras funcionais. O header de debug pode ser incluído em capítulos futuros, de modo que cada subsistema adicionado posteriormente no livro possa se conectar ao mesmo framework.

A versão do driver avança de `1.5-power` para `1.6-debug`. A árvore de exemplos ganha um diretório `stage3-refactor` com o código-fonte final, um documento `DEBUG.md` e um `README.md` que guia o leitor na construção, no carregamento e no exercício da nova infraestrutura.

Com a Seção 8 concluída, o capítulo cobriu o arco completo: entender por que a depuração do kernel é difícil, usar todas as ferramentas que o FreeBSD oferece, reconhecer os bugs comuns e, por fim, construir o driver de modo que ele suporte sua própria depuração. O material restante no capítulo é a sequência de laboratórios, os desafios e o material de encerramento que conecta este capítulo ao Capítulo 24.

## Laboratórios Práticos

Os laboratórios neste capítulo formam uma progressão. Cada um reforça uma ferramenta específica das Seções 1 a 6, e o laboratório final aplica a refatoração da Seção 8, de modo que o leitor saia com um driver instrumentado e pronto para o restante do livro.

Nenhum desses laboratórios é longo. Todos os cinco podem ser concluídos em uma única noite, embora distribuí-los por duas ou três sessões dê ao leitor tempo para absorver cada ferramenta.

### Laboratório 23.1: Uma Primeira Sessão no DDB

**Objetivo:** Entrar no debugger do kernel deliberadamente, inspecionar o estado do driver e sair com segurança.

**Pré-requisitos:** Um kernel de debug construído conforme descrito na Seção 4, com `options KDB` e `options DDB`. O driver `myfirst` do Capítulo 22 (versão `1.5-power`) carregado.

**Passos:**

1. Confirme que o kernel de debug está em execução:

   ```sh
   sysctl kern.version
   sysctl debug.kdb
   ```

   O segundo comando deve mostrar DDB entre os backends disponíveis.

2. Carregue o driver e confirme o nó de dispositivo:

   ```sh
   sudo kldload myfirst
   ls /dev/myfirst0
   ```

3. Entre no DDB com um keyboard break. No console do sistema, pressione `Ctrl-Alt-Esc` em um console VGA, ou envie o caractere BREAK em um console serial. O prompt aparece:

   ```
   KDB: enter: manual entry
   [thread pid 42 tid 100024 ]
   Stopped at      kdb_enter+0x37: movq    $0,0x158e4fa(%rip)
   db>
   ```

4. Exiba a árvore de dispositivos:

   ```
   db> show devmap
   ```

   Localize `myfirst0` na saída.

5. Imprima o backtrace da thread atual:

   ```
   db> bt
   ```

6. Imprima todos os processos:

   ```
   db> ps
   ```

   Confirme que o próprio shell do leitor está visível na lista.

7. Continue a execução do kernel:

   ```
   db> continue
   ```

O sistema retorna à operação normal.

**O que observar:**

- O kernel parou de forma limpa no breakpoint.
- Todos os dispositivos, processos e threads do kernel estavam inspecionáveis.
- O comando `continue` devolveu o sistema à operação normal sem nenhum efeito colateral.

Registre o tempo gasto e quaisquer anotações no diário de laboratório, sob "Capítulo 23, Laboratório 1". Ao final, você deve ter a compreensão de que o DDB é uma ferramenta segura quando utilizada de forma deliberada.

### Laboratório 23.2: Medindo o Driver com DTrace

**Objetivo:** Usar DTrace para medir as taxas de abertura, fechamento e I/O do driver `myfirst` sob uma carga de trabalho simples.

**Pré-requisitos:** Um kernel com `options KDTRACE_HOOKS` e `makeoptions WITH_CTF=1` carregado. O driver `myfirst` carregado na versão `1.5-power` (as probes SDT ainda não estão presentes nesta etapa; o laboratório usa apenas `fbt`).

**Passos:**

1. Confirme que o DTrace funciona:

   ```sh
   sudo dtrace -l | head -5
   ```

2. Inicie um script DTrace que conta as entradas em cada função do `myfirst`:

   ```sh
   sudo dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'
   ```

3. Em um segundo terminal, exercite o driver:

   ```sh
   for i in $(seq 1 100); do cat /dev/myfirst0 > /dev/null; done
   ```

4. Pare o script DTrace (Ctrl-C no primeiro terminal) e leia a contagem:

   ```
     myfirst_open                                             100
     myfirst_close                                            100
     myfirst_read                                             100
   ```

5. Agora meça o tempo gasto em `myfirst_read`:

   ```sh
   sudo dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
                   fbt::myfirst_read:return /self->t/ {
                       @ = quantize(timestamp - self->t); self->t = 0; }'
   ```

6. Exercite o driver com 1000 leituras, pare o DTrace e leia a quantização.

**O que observar:**

- Cada `cat` produziu uma abertura, uma leitura e um fechamento, correspondendo à contagem.
- O tempo por leitura é pequeno (microssegundos) e concentrado em um único bucket da quantização.
- Nenhuma modificação foi feita no driver; a medição é não-invasiva.

### Laboratório 23.3: Rastreamento no Lado do Usuário com ktrace

**Objetivo:** Rastrear um programa de usuário que exercita o driver `myfirst` e verificar a sequência de syscalls.

**Pré-requisitos:** O driver carregado. O programa de teste `myftest.c` da Seção 6.4 compilado.

**Passos:**

1. Compile o programa de teste (da Seção 6.4):

   ```sh
   cc -o myftest myftest.c
   ```

2. Execute-o sob `ktrace`:

   ```sh
   sudo ktrace -di -f mytrace.out ./myftest
   ```

3. Exiba o trace:

   ```sh
   kdump -f mytrace.out | less
   ```

4. Localize as syscalls relevantes para o driver:

   ```sh
   kdump -f mytrace.out | grep -E 'myfirst0|CALL|RET'
   ```

5. Correlacione o trace com as contagens do DTrace do Laboratório 23.2. Cada syscall no lado do usuário deve corresponder a uma ou mais entradas de função no lado do kernel.

**O que observar:**

- A visão do programa de usuário sobre o driver está completa no trace.
- Argumentos, valores de retorno e códigos de erro são todos visíveis.
- O rastreamento no lado do usuário complementa a visão do DTrace no lado do kernel.

### Laboratório 23.4: Encontrando um Vazamento de Memória com vmstat -m

**Objetivo:** Introduzir um vazamento deliberado, detectá-lo com `vmstat -m` e corrigi-lo.

**Pré-requisitos:** O código-fonte do driver sob controle do leitor. Um módulo compilado e carregável.

**Passos:**

1. Modifique `myfirst_open` para alocar um buffer pequeno e armazená-lo em `si_drv2`, sem liberá-lo em `myfirst_close`:

   ```c
   /* in myfirst_open */
   dev->si_drv2 = malloc(128, M_MYFIRST, M_WAITOK | M_ZERO);
   ```

2. Construa e carregue o driver.

3. Execute uma carga de trabalho que abre e fecha o dispositivo muitas vezes:

   ```sh
   for i in $(seq 1 1000); do cat /dev/myfirst0 > /dev/null; done
   ```

4. Verifique o pool de memória:

   ```sh
   vmstat -m | grep myfirst
   ```

   A coluna `InUse` deve mostrar 1000 (ou mais), e `MemUse` deve mostrar aproximadamente 128 KB.

5. Corrija o vazamento adicionando `free(dev->si_drv2, M_MYFIRST);` em `myfirst_close`.

6. Construa e recarregue o driver. Execute a carga de trabalho novamente.

7. Verifique o pool:

   ```sh
   vmstat -m | grep myfirst
   ```

   `InUse` deve agora estabilizar próximo de zero após a carga de trabalho.

**O que observar:**

- A saída do `vmstat -m` expôs o vazamento imediatamente.
- Sem uma ferramenta especializada, o vazamento teria passado despercebido até o kernel ficar sem memória.
- A correção é mecânica e simples uma vez que o sintoma é identificado.

Não deixe o código que induz o vazamento no driver após o laboratório. Reverta para a versão limpa antes de continuar.

### Laboratório 23.5: Instalando o Refator 1.6-debug

**Objetivo:** Aplicar o refator da Seção 8 ao driver `myfirst` e confirmar que a nova infraestrutura funciona.

**Pré-requisitos:** O código-fonte do driver do Capítulo 22 sob controle do leitor. Um ambiente de desenvolvimento capaz de construir módulos do kernel.

**Passos:**

1. Crie `myfirst_debug.h` exatamente como mostrado na Seção 8.2, e as declarações SDT da Seção 8.6.

2. Atualize o softc para adicionar `uint32_t sc_debug;`.

3. Registre o sysctl em `myfirst_attach` conforme mostrado na Seção 8.4.

4. Substitua chamadas incondicionais a `device_printf` por chamadas `DPRINTF(sc, MYF_DBG_<class>, ...)` onde for apropriado.

5. Defina os providers e probes SDT em `myfirst_debug.c` (ou `myfirst.c`), e dispare-os nos métodos open, close e read/write.

6. Incremente `myfirst_version` para `1.6-debug` e `MODULE_VERSION` para 16.

7. Construa:

   ```sh
   cd /path/to/myfirst-source
   make clean && make
   ```

8. Recarregue:

   ```sh
   sudo kldunload myfirst
   sudo kldload ./myfirst.ko
   ```

9. Confirme que o módulo foi carregado na nova versão:

   ```sh
   kldstat -v | grep myfirst
   ```

10. Confirme que o sysctl está presente:

    ```sh
    sysctl dev.myfirst.0.debug.mask
    ```

11. Habilite verbosidade total e abra o dispositivo:

    ```sh
    sudo sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF
    cat /dev/myfirst0 > /dev/null
    dmesg | tail
    ```

    A saída esperada inclui linhas como:

    ```
    myfirst0: open: pid=1234 uid=1001 flags=0x0
    myfirst0: open ok: count=1
    myfirst0: read: size=4096 off=0
    ```

12. Desabilite a verbosidade e confirme que as mensagens param:

    ```sh
    sudo sysctl dev.myfirst.0.debug.mask=0
    cat /dev/myfirst0 > /dev/null
    dmesg | tail
    ```

13. Anexe o DTrace às probes SDT:

    ```sh
    sudo dtrace -n 'myfirst::: { @[probename] = count(); }'
    ```

    Em um segundo terminal, exercite o driver:

    ```sh
    for i in $(seq 1 100); do cat /dev/myfirst0 > /dev/null; done
    ```

    Pare o DTrace e confirme que as probes dispararam.

**O que observar:**

- O controle em tempo de execução da verbosidade é imediato e responsivo.
- O caminho do DPRINTF produz mensagens legíveis por humanos; as probes SDT produzem eventos legíveis por máquina.
- As duas formas são complementares e independentes.
- Ambas estão desligadas por padrão, de modo que os usuários em produção não pagam nenhum custo em tempo de execução.

O driver está agora na versão `1.6-debug`, com infraestrutura que dará suporte a todos os capítulos seguintes. Registre a atualização no caderno de bordo.

## Exercícios Desafio

Os exercícios abaixo constroem sobre o material do capítulo. Eles são suficientemente abertos para que cada leitor chegue a respostas ligeiramente diferentes. Leve o tempo necessário; trabalhe de forma incremental; use as ferramentas do capítulo a cada etapa.

### Desafio 23.1: Uma Probe SDT em um Driver Real

Escolha um driver em `/usr/src/sys/dev/` que já contenha probes SDT. Bons candidatos incluem `virtqueue.c`, alguns arquivos `ath`, ou `if_re.c`.

Para o driver escolhido:

1. Liste suas probes com `dtrace -l -P <provider>`.
2. Escreva um one-liner que conte eventos agrupados por nome de probe.
3. Escreva um segundo one-liner que agregue por um dos argumentos da probe (por exemplo, um tamanho de pacote ou um número de comando).
4. Explique em três frases o que o autor do driver ganhou ao adicionar as probes.

### Desafio 23.2: Um Script DTrace Personalizado

Escreva um script DTrace que:

1. Anexe às probes SDT do `myfirst` (adicionadas no Laboratório 23.5).
2. Rastreie o tempo de vida de cada file descriptor aberto por pid.
3. Imprima um resumo quando um fd fechar, mostrando: pid, quantas leituras, quantas escritas, total de bytes e o tempo decorrido entre a abertura e o fechamento.

O script deve ter no máximo 50 linhas de código D. Teste-o executando um pequeno programa de usuário que abre, lê, escreve e fecha `/dev/myfirst0` várias vezes.

### Desafio 23.3: Um Experimento com WITNESS

Modifique o driver para conter um erro deliberado de ordenação de locks. Por exemplo, adicione dois mutexes `sc_mtx_a` e `sc_mtx_b`, e organize um caminho de código para adquirir A depois B enquanto outro caminho adquire B depois A.

Construa com `options WITNESS`, carregue e provoque o erro. Capture o panic do kernel ou a saída do WITNESS resultante. Descreva em três frases o que o WITNESS mostrou, por que ele detectou o problema e qual seria a correção.

Certifique-se de reverter o erro após o exercício. Não deixe um driver quebrado carregado.

### Desafio 23.4: Categorização Estendida de Erros

Escolha quatro caminhos de erro no driver `myfirst` (por exemplo, o caminho `EBUSY` em `myfirst_open`, um caminho `EINVAL` em `myfirst_ioctl`, e assim por diante). Para cada um:

1. Identifique a causa subjacente que produziria esse erro no mundo real.
2. Adicione um `DPRINTF(sc, MYF_DBG_<class>, ...)` que capture a causa de forma clara.
3. Escreva uma nota breve em `DEBUG.md` explicando o que procurar no `dmesg` quando o erro ocorrer.

O objetivo é tornar cada erro no driver autoexplicativo, sem exigir que o leitor consulte o código-fonte.

### Desafio 23.5: Debug no Momento do Boot

Modifique o driver para registrar a versão no momento do attach e os detalhes de hardware somente quando `bootverbose` estiver ativo. O efeito deve ser: um boot normal exibe uma linha, um boot verbose (solicitado com `boot -v`) exibe a configuração detalhada.

Leia `/usr/src/sys/sys/systm.h` para a declaração de `bootverbose`, e `/usr/src/sys/kern/subr_boot.c` para exemplos. Descreva em duas frases para que o FreeBSD usa `bootverbose`, e por que ele é melhor do que um flag verbose específico do driver para o caso particular do boot inicial.

## Solução de Problemas e Erros Comuns

Todo leitor vai se deparar com pelo menos alguns dos problemas abaixo. Cada um está registrado com o sintoma, a causa provável e o caminho para a correção.

**"Meu kldload falha com `link_elf: symbol X undefined`."**

- Causa: o módulo depende de um símbolo do kernel que não está no kernel em execução. Normalmente isso significa que o kernel é mais antigo do que o código-fonte do módulo, ou que o módulo foi compilado contra um build de kernel diferente.
- Correção: reconstrua o kernel e o módulo a partir da mesma árvore de código-fonte, usando os mesmos flags de compilação. Confirme que `uname -a` e o diretório de build do módulo correspondem.

**"DTrace diz `invalid probe specifier`."**

- Causa: o nome da probe está digitado incorretamente, ou o campo provider/module/function não existe em tempo de execução.
- Correção: execute `dtrace -l | grep <provider>` para listar as probes disponíveis e escolha um nome da lista. Lembre-se de que wildcards devem corresponder a um nome existente.

**"WITNESS causa panic no boot com `WITNESS_CHECKORDER`."**

- Causa: um driver inicial (frequentemente de terceiros) viola a ordem de locks declarada. Com um kernel padrão a violação era silenciosa; com WITNESS ela causa um panic imediato.
- Correção: desabilite temporariamente o driver problemático, ou defina `debug.witness.watch=0` no momento do boot para desabilitar a verificação, ou reconstrua o driver problemático com uma ordem de locks corrigida.

**"Mensagens de DPRINTF não aparecem no `dmesg`."**

- Causa: o bit correspondente em `sc_debug` não está ativado.
- Correção: confirme o sysctl com `sysctl dev.myfirst.0.debug.mask`. Defina o bit: `sysctl dev.myfirst.0.debug.mask=0xFF`. Tente novamente.

**"DTrace diz `probe description myfirst::: matched 0 probes`."**

- Causa: ou o driver não está carregado, ou o CTF não foi gerado (as probes SDT estão declaradas, mas não são visíveis para o DTrace).
- Correção: execute `kldstat | grep myfirst` e confirme que o módulo está carregado. Se estiver, reconstrua o kernel com `makeoptions WITH_CTF=1` e reinicie.

**"O vazamento de memória persiste após minha correção."**

- Causa: existe mais de um vazamento. A primeira correção tratou de um caminho, mas outro caminho ainda está vazando.
- Correção: reexamine os números do `vmstat -m`. Se ainda crescerem, rastreie o alocador: adicione `device_printf` em cada chamada a `malloc` e `free`, recarregue, exercite o driver e conte as aparições.

**"O driver é descarregado, mas `vmstat -m` ainda mostra InUse diferente de zero."**

- Causa: o caminho de descarregamento não liberou toda a memória. Isso ocorre frequentemente porque algo foi alocado pelo open mas não liberado pelo close, e o caminho de close nunca foi chamado antes do detach.
- Correção: verifique se `destroy_dev_sched_cb` é usado para aguardar aberturas pendentes antes de o detach prosseguir. Todo buffer `si_drv1` ou `si_drv2` alocado por open deve ser liberado no close ou no callback ordenado pelo detach.

**"Minha probe `fbt` tem um nome enganoso."**

- Causa: alguns drivers usam funções inline ou helpers estáticos que o compilador pode ter incorporado (inlined). O binário compilado mostra apenas a função que os contém.
- Correção: compile o módulo com `-O0` ou adicione `__noinline` ao helper. Isso é útil apenas para depuração; builds de produção devem usar o nível de otimização normal.

**"Registros do ktrace são longos demais para ler."**

- Causa: o `ktrace` padrão captura muitas classes de eventos ao mesmo tempo.
- Correção: limite a classes específicas com o flag `-t`: `ktrace -t c ./myftest` captura apenas syscalls, e não I/O ou NAMI.

**"O kernel inicializa, mas não imprime nenhuma mensagem do driver."**

- Causa: o driver não foi compilado no kernel ou o probe do driver retornou um erro diferente de zero.
- Solução: `kldstat -v | grep myfirst` confirma que o módulo está carregado. `dmesg | grep -i 'probe'` exibe as mensagens do probe. Se o módulo estiver carregado mas o driver não estiver anexado, verifique `device_set_desc` e o valor de retorno da função probe.

## Apêndice: Padrões de Debug Reais na Árvore do FreeBSD

Os padrões apresentados nas Seções 7 e 8 são baseados em convenções já utilizadas por drivers em produção. Um leitor que inspecionar a árvore encontrará variações dessas convenções em toda parte. Este apêndice faz um breve tour por alguns exemplos.

**`if_ath_debug.h`** (`/usr/src/sys/dev/ath/if_ath_debug.h`) define aproximadamente 40 classes de verbosidade que cobrem reset, interrupção, RX, TX, beacon, controle de taxa, seleção de canal e muitas outras, cada uma expressa como um bit em uma máscara de 64 bits. Os nomes das classes seguem o padrão `ATH_DEBUG_<subsystem>` (por exemplo, `ATH_DEBUG_RECV`, `ATH_DEBUG_XMIT`, `ATH_DEBUG_RESET`, `ATH_DEBUG_BEACON`), e o macro `DPRINTF` segue o padrão usual. O driver usa `ATH_DEBUG_<class>` em centenas de lugares ao longo do código-fonte, dando aos operadores controle refinado sobre a saída de rastreamento.

**`virtqueue.c`** (`/usr/src/sys/dev/virtio/virtqueue.c`) usa SDT probes de forma intensa. Um único `SDT_PROVIDER_DEFINE(virtqueue)` no topo do arquivo declara o provider, seguido de uma dúzia de chamadas `SDT_PROBE_DEFINEn` para cada evento significativo: enfileiramento, desenfileiramento, notificação e assim por diante. O driver dispara os probes dentro do caminho crítico, e os probes são no-ops quando nenhum script está anexado. Scripts DTrace como `dtrace -n 'virtqueue::: { @[probename] = count(); }'` oferecem visibilidade instantânea sobre a atividade do dispositivo virtio.

**`if_re.c`** (`/usr/src/sys/dev/re/if_re.c`) combina `device_printf`, `printf` e `if_printf`. As chamadas a `device_printf` aparecem nos caminhos de attach e detach, onde a identidade do dispositivo é útil. `if_printf` aparece nos caminhos de pacotes, onde o que importa é a identidade da interface. `printf` aparece nos helpers compartilhados, onde nenhuma das duas identidades está disponível. A divisão de responsabilidades é pragmática, não ideológica.

**`uart_core.c`** (`/usr/src/sys/dev/uart/uart_core.c`) define `UART_DBG_LEVEL` e uma família de macros `UART_DBG` que verificam o nível e registram mensagens via `printf`. O nível é definido em tempo de compilação por meio das opções `options UART_POLL_FREQ` e `options UART_DEV_TOLERANCE_PCT`, entre outras. O design é estático: as decisões de debug são fixadas em tempo de build, e não em tempo de execução. Essa é a escolha certa para um driver que precisa funcionar no início do boot, quando o sysctl ainda não está disponível.

**`random_harvestq.c`** (`/usr/src/sys/dev/random/random_harvestq.c`) usa WITNESS extensivamente. Cada lock no arquivo tem uma verificação `mtx_assert(&lock, MA_OWNED)` no início de cada função que depende do lock. Quando WITNESS está habilitado no build, essas asserções detectam qualquer chamador que tenha esquecido de adquirir o lock. Em builds de produção, a asserção é eliminada pelo compilador e não tem custo algum.

Em toda a árvore, o padrão é consistente: os drivers expõem seus internos por meio de uma combinação de logging de debug estático, SDT probes e macros de asserção. Cada um desses mecanismos não tem custo algum em tempo de execução quando desabilitado, e oferece rica visibilidade quando habilitado. O leitor que adotar esse padrão em seu próprio driver estará escrevendo código que escala graciosamente desde o debug do primeiro dia até o monitoramento em produção.

## Apêndice: Estudos de Caso Práticos em Depuração de Drivers

O material deste capítulo é mais útil quando exercitado contra bugs concretos. Este apêndice percorre três estudos de caso realistas. Cada um começa com um sintoma que o leitor pode encontrar, segue os passos de diagnóstico que as ferramentas deste capítulo permitem, chega à causa raiz e registra a correção. Os três casos juntos cobrem as três grandes categorias de bugs em drivers: um bug de correção no caminho de logging, um bug de ordem de lock, e um bug de desempenho no caminho de interrupção.

Os casos são escritos como narrativa, e não como uma sequência seca de comandos, porque a depuração é um processo narrativo. O desenvolvedor experiente não segue um único algoritmo; ele lê um rastreamento, forma uma hipótese, a testa, a refina e itera. Esses percursos tentam preservar essa textura enquanto mantêm cada passo reproduzível.

### Estudo de Caso 1: A Mensagem que Sumiu

**Sintoma.** Um leitor adicionou uma linha `DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n", sc->sc_open_count)` em `myfirst_open`. Ele define `sysctl dev.myfirst.0.debug.mask=0x2` para habilitar `MYF_DBG_OPEN`, abre o dispositivo e verifica o `dmesg`. Nada aparece.

**Primeira hipótese: o bit está errado.** O leitor verifica novamente a máscara. `MYF_DBG_OPEN` é `0x02`, então definir `mask=0x2` deveria habilitá-lo. A hipótese está errada.

**Segunda hipótese: o dispositivo não está sendo aberto.** O leitor executa `cat /dev/myfirst0 > /dev/null` e verifica o `dmesg`. Ainda nada.

**Terceira hipótese: o sysctl não está de fato definindo o campo.** O leitor lê o sysctl de volta:

```sh
sysctl dev.myfirst.0.debug.mask
```

A saída mostra `0x0`. A escrita não surtiu efeito. Esse é o problema real.

**Estreitando o diagnóstico.** O leitor inspeciona o código de attach e encontra:

```c
SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RD, &sc->sc_debug, 0, "debug class bitmask");
```

Observe: `CTLFLAG_RD` (somente leitura), não `CTLFLAG_RW`. O sysctl foi declarado como somente leitura, então o comando `sysctl` pareceu ter sucesso, mas na prática recusou a escrita silenciosamente.

**Correção.** Altere a flag para `CTLFLAG_RW`:

```c
SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0, "debug class bitmask");
```

Recompile, recarregue, defina a máscara, abra o dispositivo e a mensagem aparecerá.

**Lições aprendidas.** Três ferramentas combinadas encontraram esse bug em menos de um minuto: o `dmesg` mostrou o sintoma (mensagem ausente), o `sysctl` revelou o estado real (escrita sem efeito), e o código-fonte do driver tornou a causa visível (`CTLFLAG_RD`). Sem qualquer uma dessas três ferramentas, o leitor poderia ter perdido dez minutos em uma hipótese errada. As ferramentas não substituem o raciocínio; elas fornecem as evidências de que o raciocínio precisa.

A leitura de retorno do sysctl é uma disciplina útil em geral. Sempre que um valor do driver deveria ser gravável, teste a escrita lendo-o de volta. Esse único hábito evitará uma classe de bugs que já pegou todo desenvolvedor FreeBSD pelo menos uma vez.

### Estudo de Caso 2: O Panic Intermitente

**Sintoma.** Com carga leve o driver funciona perfeitamente. Com uma carga de estresse (digamos, cem leitores simultâneos), o kernel entra em panic com:

```text
panic: mutex myfirst_lock recursed on non-recursive mutex myfirst
```

O panic é intermitente: às vezes a carga roda por cinco minutos antes do panic, às vezes entra em panic imediatamente. O leitor confirma que um kernel de debug com `options WITNESS` e `options INVARIANTS` está em uso.

**Primeiro passo: ler o stack.** A mensagem de panic inclui um stack trace. As linhas relevantes apontam para `myfirst_read` como o ponto de recursão, com o próprio `myfirst_read` já presente mais acima na pilha. De alguma forma, `myfirst_read` está sendo chamado enquanto uma chamada anterior a `myfirst_read` ainda está em execução, e ambas as chamadas estão na mesma thread.

**Hipótese.** Algo dentro de `myfirst_read` está disparando uma chamada secundária a `myfirst_read`. O candidato mais provável é um `uiomove`, que aciona o caminho de page fault do programa do usuário, o qual em circunstâncias muito incomuns pode chamar de volta a I/O do dispositivo.

**Verificação.** O leitor adiciona duas chamadas `DPRINTF`, uma na entrada e outra logo antes do `uiomove`, cada uma capturando a thread atual:

```c
DPRINTF(sc, MYF_DBG_IO, "read entry: tid=%d\n", curthread->td_tid);
DPRINTF(sc, MYF_DBG_IO, "read about to uiomove: tid=%d\n", curthread->td_tid);
```

Recompile, recarregue, execute a carga. O log do kernel mostra duas mensagens "read entry" para o mesmo tid antes de qualquer "about to uiomove", o que confirma a hipótese.

**Diagnóstico.** A chamada `mtx_lock(&sc->sc_mtx)` em `myfirst_read` é mantida durante o `uiomove`. O destino do `uiomove` está em uma região mapeada por memória de `/dev/myfirst0`, porque a carga de estresse usava saída por `mmap`. O page fault disparado pelo `uiomove` re-entra em `myfirst_read` para tratar o fault, o que tenta adquirir `sc->sc_mtx` uma segunda vez.

**Correção.** O lock deve ser liberado antes do `uiomove`. Em geral, um sleep lock nunca deve ser mantido durante uma chamada a uma função que possa gerar um page fault.

```c
/* buggy version */
mtx_lock(&sc->sc_mtx);
error = uiomove(sc->sc_buffer, sc->sc_bufsize, uio);
mtx_unlock(&sc->sc_mtx);

/* fixed version */
mtx_lock(&sc->sc_mtx);
/* snapshot the buffer so we can release the lock */
tmp_buffer = sc->sc_buffer;
tmp_bufsize = sc->sc_bufsize;
mtx_unlock(&sc->sc_mtx);
error = uiomove(tmp_buffer, tmp_bufsize, uio);
```

Recompile, recarregue, execute a carga de estresse. O panic desaparece.

**Lições aprendidas.** WITNESS não detectou isso diretamente porque se trata de uma recursão, não de uma violação de ordem de lock. Mas INVARIANTS transformou a recursão em um panic limpo em vez de corrupção silenciosa, e o rastreamento com DPRINTF tornou a cronologia visível. A regra "não mantenha um sleep lock durante `uiomove` ou qualquer função que possa dormir" está documentada em `locking(9)`, mas é fácil de esquecer na prática. Toda aquisição de sleep lock em código de driver merece uma verificação momentânea: "essa chamada pode dormir ou gerar um fault?"

### Estudo de Caso 3: A Interrupção Lenta

**Sintoma.** Um driver funciona corretamente, mas o sistema está lento quando o dispositivo está ativo. A responsividade do shell cai, aplicações interativas travam e o `top` mostra alto consumo de CPU de sistema.

**Primeiro passo: confirmar que o driver é a causa.** O leitor descarrega o driver e compara:

```sh
# before unload
top -aC1 -bn1 | head -20
sudo kldunload myfirst
# after unload
top -aC1 -bn1 | head -20
```

Se a lentidão desaparecer quando o driver for descarregado, o driver é a fonte do problema.

**Segundo passo: identificar o caminho de código caro.** O provider `profile` do DTrace amostra o kernel a uma taxa fixa e mostra onde o tempo está sendo gasto:

```sh
sudo dtrace -n 'profile-1001hz /arg0/ { @[stack(10)] = count(); } tick-10s { exit(0); }'
```

Após dez segundos de carga, a saída mostra as stacks mais quentes. Se `myfirst_intr` dominar as amostras, o handler de interrupção está pesado demais.

**Terceiro passo: medir quanto tempo cada interrupção leva.**

```sh
sudo dtrace -n 'fbt::myfirst_intr:entry { self->t = timestamp; }
                fbt::myfirst_intr:return /self->t/ {
                    @ = quantize(timestamp - self->t); self->t = 0; }'
```

Um handler saudável termina em menos de 10 microssegundos (10.000 nanossegundos). Se a quantização mostrar a maioria das invocações na faixa de 100.000 a 1.000.000 nanossegundos (100 microssegundos a 1 milissegundo), o handler está fazendo trabalho demais.

**Quarto passo: ler o código do handler.** O leitor inspeciona `myfirst_intr` e encontra:

```c
static void
myfirst_intr(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct mbuf *m;

        mtx_lock(&sc->sc_mtx);
        m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
        if (m != NULL) {
                myfirst_process_data(sc, m);
                m_freem(m);
        }
        mtx_unlock(&sc->sc_mtx);
}
```

O handler faz trabalho real: aloca um mbuf, processa dados e libera o mbuf. `myfirst_process_data` inclui vários pacotes de trabalho, cada um com sua própria alocação e processamento. Sob carga, cada interrupção pode manter a CPU ocupada por centenas de microssegundos.

**Correção.** O trabalho pesado é movido para um taskqueue. O próprio handler faz o mínimo absoluto: reconhece a interrupção e sinaliza o taskqueue.

```c
static void
myfirst_intr(void *arg)
{
        struct myfirst_softc *sc = arg;

        /* Acknowledge the interrupt */
        bus_write_4(sc->sc_res, MYFIRST_REG_INTR_STATUS, 0);
        /* Schedule deferred processing */
        taskqueue_enqueue(sc->sc_tq, &sc->sc_process_task);
}

static void
myfirst_process_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        struct mbuf *m;

        mtx_lock(&sc->sc_mtx);
        m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
        if (m != NULL) {
                myfirst_process_data(sc, m);
                m_freem(m);
        }
        mtx_unlock(&sc->sc_mtx);
}
```

O taskqueue é configurado no `attach` conforme explorado no Capítulo 19.

Recompile, recarregue, execute a carga. A responsividade do shell retorna, o `top` mostra o driver contribuindo com uma quantidade normal de CPU de sistema, e a quantização de latência de `myfirst_intr` agora se concentra no intervalo de 1 a 10 microssegundos.

**Lições aprendidas.** Três medições com DTrace guiaram a correção: `profile-1001hz` localizou o código quente, `fbt::myfirst_intr:entry/return` mediu a duração do handler, e a medição final após a correção confirmou a melhoria. Nenhum código-fonte foi modificado durante o diagnóstico; as modificações foram feitas apenas depois que o bug foi compreendido.

A regra de que handlers de interrupção devem ser curtos é familiar aos autores de drivers, mas o cumprimento não é automático. Builds de debug não entram em panic por causa de um handler lento. A única forma de detectar essa categoria de bug é medir, e o DTrace é a ferramenta certa para o trabalho. Uma equipe de drivers madura executa o profiling com `fbt::<driver>_intr:entry/return` como parte de cada ciclo de release; seus handlers se mantêm enxutos porque as medições tornam qualquer desvio visível.

### Estudo de Caso 4: O Dispositivo que Desaparece

**Sintoma.** Após `kldunload myfirst`, um segundo `kldload myfirst` às vezes entra em panic com um page fault em `devfs_ioctl`, com o stack apontando para o antigo `myfirst_ioctl`. O panic não acontece sempre, mas sob carga (por exemplo, se um daemon mantém `/dev/myfirst0` aberto durante o descarregamento) é consistentemente fatal.

**Primeiro passo: ler o panic.** O stack mostra `devfs_ioctl -> myfirst_ioctl -> (address not found)`. O "address not found" é a instrução que falhou. Isso é um use-after-free de código de função: o kernel está tentando chamar uma função cuja memória foi desmapeada.

**Hipótese.** O caminho de unload do driver não esperou que um processo que tinha o dispositivo aberto terminasse. O processo emitiu um ioctl entre o unload e o re-load; o ioctl foi despachado para o `myfirst_ioctl` agora desmapeado.

**Verificação.** O leitor inspeciona `myfirst_detach` e encontra:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        destroy_dev(sc->sc_cdev);
        bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem);
        mtx_destroy(&sc->sc_mtx);
        return (0);
}
```

A chamada `destroy_dev` é o problema: ela destrói imediatamente o nó de dispositivo, mas ioctls em andamento nesse nó podem ainda estar em execução. Quando `destroy_dev` retorna, o driver acredita que é seguro liberar os recursos. Porém, o ioctl ainda está sendo despachado no momento em que esses recursos desaparecem.

**Correção.** Use `destroy_dev_sched_cb` (ou, de forma equivalente, `destroy_dev_sched` seguido de uma espera pela conclusão) para adiar a destruição real até que nenhuma thread esteja dentro dos métodos do dispositivo.

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Tell devfs to destroy the node after all callers finish */
        destroy_dev_sched_cb(sc->sc_cdev, myfirst_detach_cb, sc);
        /* Return success; cleanup happens in the callback */
        return (0);
}

static void
myfirst_detach_cb(void *arg)
{
        struct myfirst_softc *sc = arg;

        bus_release_resource(sc->sc_dev, SYS_RES_MEMORY, 0, sc->sc_mem);
        mtx_destroy(&sc->sc_mtx);
}
```

O detach retorna imediatamente; a liberação real dos recursos acontece no callback, que é agendado para disparar somente quando todos os chamadores dentro dos métodos do `cdev` tiverem saído.

Recompile e recarregue repetidamente sob a carga de trabalho problemática. O panic desapareceu.

**Lições aprendidas.** Este é o clássico bug de use-after-detach descrito na Seção 7.4. A opção `DEBUG_MEMGUARD` do kernel de depuração teria detectado o problema mais cedo, envenenando a memória liberada e transformando a corrupção latente em um page fault imediato na próxima chamada. A correção usa uma primitiva do FreeBSD (`destroy_dev_sched_cb`) que existe exatamente para esse caso. Ler a documentação de gerenciamento de `cdev` em `cdev(9)` é o exercício que previne esse bug em código novo.

### Estudo de Caso 5: A Saída Corrompida

**Sintoma.** Um programa de usuário lê de `/dev/myfirst0` esperando um padrão específico (por exemplo, a sequência `0x01, 0x02, 0x03, ...`). Na maioria das vezes o padrão está correto. Ocasionalmente, em um sistema sobrecarregado, o padrão está errado: um byte ou dois está incorreto, ou a sequência está deslocada. Não há panic, nenhum código de erro, apenas dados sutilmente errados.

**Primeiro passo: reproduzir de forma confiável.** A leitora escreve um pequeno programa de teste que lê repetidamente e compara com o padrão esperado. Sob carga, as divergências ocorrem em aproximadamente uma a cada dez mil leituras. A leitora confirma que descarregar o driver e usar um teste simples com `/dev/zero` não produz divergências, o que descarta o programa de usuário como fonte do bug.

**Hipótese 1: uma condição de corrida no buffer.** Se duas threads estiverem lendo simultaneamente e o buffer for compartilhado sem locking, elas podem corromper os dados uma da outra. A leitora inspeciona o driver e encontra:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;

        /* Generate the pattern into sc->sc_buffer */
        for (int i = 0; i < sc->sc_bufsize; i++)
                sc->sc_buffer[i] = (uint8_t)(i + 1);

        return (uiomove(sc->sc_buffer, sc->sc_bufsize, uio));
}
```

O buffer está no softc, compartilhado entre todas as aberturas. Duas threads chamando `read` simultaneamente escrevem no mesmo buffer; a segunda thread sobrescreve o conteúdo da primeira antes que esta tenha terminado de copiar. O `uiomove` da primeira thread então lê parcialmente do padrão da primeira thread e parcialmente do padrão da segunda thread. O resultado é uma sequência embaralhada.

**Verificação.** A leitora adiciona um `DPRINTF` no início e no fim da leitura, capturando o tid da thread:

```c
DPRINTF(sc, MYF_DBG_IO, "read start: tid=%d\n", curthread->td_tid);
/* ... generate and uiomove ... */
DPRINTF(sc, MYF_DBG_IO, "read end: tid=%d\n", curthread->td_tid);
```

Quando o log é examinado durante um evento de corrupção, duas mensagens `start` aparecem entre um único `start` e seu `end`. A condição de corrida é confirmada.

**Correção.** O buffer deve ser alocado por chamada (alocado a cada leitura) ou o acesso deve ser serializado com um mutex:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t *buf;
        int error;

        buf = malloc(sc->sc_bufsize, M_MYFIRST, M_WAITOK);
        for (int i = 0; i < sc->sc_bufsize; i++)
                buf[i] = (uint8_t)(i + 1);

        error = uiomove(buf, sc->sc_bufsize, uio);
        free(buf, M_MYFIRST);
        return (error);
}
```

Uma alocação por chamada elimina o estado compartilhado por completo, e o bug desaparece. Uma correção baseada em lock é mais rápida para buffers pequenos, mas permite apenas um leitor por vez; a escolha depende do caso de uso do driver.

**Lições aprendidas.** A corrupção é sutil exatamente porque não produz um panic nem um erro. O `WITNESS` não pode ajudar aqui porque não há lock para ordenar. O `INVARIANTS` não pode ajudar porque o estado compartilhado não dispara nenhuma asserção. A única forma de encontrar esse bug é: testar a saída, observar a corrupção, formular uma hipótese sobre condições de corrida, adicionar instrumentação para confirmar a hipótese e corrigir. O padrão de DPRINTF é a instrumentação; um test harness disciplinado é a reprodução. Juntos, eles resolvem o problema.

Essa classe de bug é o argumento mais forte para que autores de drivers escrevam test harnesses desde o início. Um pequeno programa que lê do dispositivo e compara com uma saída esperada detecta em minutos muitos bugs que de outra forma sobreviveriam por meses em produção.

### Resumo dos Cinco Casos

Os cinco estudos de caso cobriram um conjunto deliberadamente diverso de bugs. O Caso 1 foi um bug de corretude em logging, encontrado em segundos assim que o sysctl foi relido. O Caso 2 foi uma recursão de lock disparada por um caminho de paginação, encontrada combinando o rastreamento do panic com DPRINTFs. O Caso 3 foi um bug de desempenho no handler de interrupção, diagnosticado com profiling via DTrace. O Caso 4 foi um uso após detach no caminho do `cdev`, diagnosticado com a pilha do panic e corrigido com a primitiva correta do FreeBSD. O Caso 5 foi uma condição de corrida em estado compartilhado no caminho de leitura, encontrada com um test harness e DPRINTFs.

Três padrões se repetem nos casos:

1. **Ferramentas encontram sintomas; ler o código encontra causas.** Nenhuma ferramenta apontou diretamente para a linha com o bug. Cada ferramenta estreitou a busca: `vmstat -m`, WITNESS, DTrace, DDB. Depois a leitora abriu o código-fonte e encontrou o erro.

2. **Reprodução é mais importante do que ferramentas.** Um bug que se reproduz de forma confiável pode ser diagnosticado com quase qualquer ferramenta. Um bug que não se reproduz precisa primeiro ser reproduzido, geralmente escrevendo scripts de carga que exercitem o caminho suspeito.

3. **Infraestrutura de debug paga seu custo muitas vezes.** O macro `DPRINTF`, as probes SDT, o sysctl `sc_debug`: cada um desses levou cinco minutos para ser adicionado, e cada um economizou horas de depuração depois. A leitora que escreve código instrumentado desde o início é a leitora que evita as longas e dolorosas sessões de debug.

Com esses padrões em mente, a leitora está pronta para levar os métodos do capítulo adiante. Cada nova parte do driver `myfirst` adicionada nos capítulos seguintes será desenvolvida com chamadas `DPRINTF`, probes SDT e test harnesses em funcionamento; a redução na dificuldade de depuração será perceptível a partir do Capítulo 24 em diante.

## Apêndice: Técnicas Avançadas de DTrace para Desenvolvimento de Drivers

Este apêndice estende a Seção 5 com um tour focado nas técnicas de DTrace que surgem com frequência no desenvolvimento de drivers. A Seção 5 apresentou os conceitos básicos; este apêndice mostra como combiná-los de forma produtiva. Leitores que acharem o DTrace útil podem querer manter este apêndice como um guia de referência rápida.

### Predicados e Rastreamento Seletivo

Um predicado restringe uma probe a eventos que correspondem a uma condição. Ele aparece entre `/ ... /`, entre a descrição da probe e a ação:

```sh
dtrace -n 'fbt::myfirst_read:entry /execname == "myftest"/ {
               @ = count(); }'
```

Apenas leituras de processos chamados `myftest` são contabilizadas. Predicados podem referenciar `pid`, `tid`, `execname`, `zonename`, `uid` e os argumentos da probe `arg0`, `arg1`, etc.

Um padrão comum é filtrar pelo ponteiro do softc para se concentrar em uma única instância do dispositivo:

```sh
dtrace -n 'fbt::myfirst_read:entry /args[0] == 0xfffff8000a000000/ {
               @ = count(); }'
```

O endereço literal do softc vem do `devctl` ou da inspeção do sysctl do driver. Quando múltiplos dispositivos `myfirst` estão presentes, isso isola um de cada vez.

### Agregações que Contam uma História

Agregações são os instrumentos estatísticos do DTrace. A Seção 5 apresentou `count()` e `quantize()`; outras funções de agregação são igualmente úteis:

- `sum(x)`: total acumulado de `x`
- `avg(x)`: média acumulada de `x`
- `min(x)`: mínimo de `x` observado
- `max(x)`: máximo de `x` observado
- `lquantize(x, low, high, step)`: quantização linear com limites
- `llquantize(x, factor, low, high, steps)`: quantização logarítmica com limites

Um relatório típico de latência por função usa `quantize` porque revela a distribuição:

```sh
dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
           fbt::myfirst_read:return /self->t/ {
               @ = quantize((timestamp - self->t) / 1000);
               self->t = 0; }'
```

Divida por 1000 para converter nanossegundos em microssegundos, o que torna os intervalos mais significativos. Adicione uma agregação indexada `@buf` para ver a latência por tamanho de buffer:

```sh
dtrace -n '
fbt::myfirst_read:entry {
    self->t = timestamp;
    self->len = args[1]->uio_resid;
}
fbt::myfirst_read:return /self->t/ {
    @["read", self->len] = quantize((timestamp - self->t) / 1000);
    self->t = 0;
}'
```

Isso agrega separadamente para cada valor distinto de `uio_resid`, revelando se leituras grandes são desproporcionalmente lentas.

### Variáveis Locais de Thread

O prefixo `self->` introduz uma variável local de thread: cada thread vê sua própria cópia. É assim que a medição de latência da Seção 5 funciona: a probe de entrada armazena o timestamp em `self->t`, e a probe de retorno o lê. Como ambas as probes disparam na mesma thread, a variável é inequívoca.

Usos comuns:

```sh
self->start    /* time of entry for a specific function */
self->args     /* saved arguments for use in the return probe */
self->error    /* error code captured from return */
```

Variáveis locais de thread são inicializadas com zero por padrão. Use-as livremente; seu custo é de uma palavra de memória por thread ativa.

### Arrays e Acesso Associativo

Arrays do DTrace são arrays associativos esparsos, indexados por qualquer expressão. Agregações os usam implicitamente (a sintaxe `@[chave]`), mas variáveis comuns também podem usá-los:

```sh
dtrace -n 'fbt::myfirst_read:entry {
               counts[args[0], execname]++; }'
```

Na prática, porém, as agregações são preferíveis porque acumulam corretamente em todos os CPUs.

### Rastreamento Especulativo

Para um evento de alta frequência em que a maioria dos casos não é interessante, o rastreamento especulativo do DTrace armazena a saída em buffer até que uma decisão seja tomada:

```sh
dtrace -n '
fbt::myfirst_read:entry {
    self->spec = speculation();
    speculate(self->spec);
    printf("read entry: pid=%d", pid);
}
fbt::myfirst_read:return /self->spec && args[1] == 0/ {
    speculate(self->spec);
    printf("read return: bytes=%d", arg1);
    commit(self->spec);
    self->spec = 0;
}
fbt::myfirst_read:return /self->spec && args[1] != 0/ {
    discard(self->spec);
    self->spec = 0;
}'
```

Apenas leituras bem-sucedidas (`args[1] == 0`) têm sua saída confirmada; leituras com falha são descartadas. Isso produz um log limpo e focado mesmo sob carga pesada.

O rastreamento especulativo é um dos recursos mais eficazes do DTrace e raramente é necessário para drivers pequenos. Mas para um driver com milhares de eventos por segundo, ele faz a diferença entre uma saída útil e uma inundação ilegível.

### Cadeias de Predicados para Rastreamento Preciso

Uma necessidade comum é rastrear a árvore de chamadas de uma função a partir de um ponto de entrada específico. Predicados podem ser encadeados usando flags locais de thread:

```sh
dtrace -n '
syscall::ioctl:entry /pid == $target/ { self->trace = 1; }
fbt::myfirst_*: /self->trace/ { printf("%s: %s", probefunc, probename); }
syscall::ioctl:return /self->trace/ { self->trace = 0; }'
```

Execute com `-p <pid>` para atingir um processo específico. A flag de rastreamento é ativada na entrada da syscall e desativada no retorno, de modo que apenas as funções do driver chamadas durante o ioctl são registradas. Esse padrão é fundamental para isolar uma ação específica do usuário.

### Perfilando o Kernel

O provider `profile` amostra o kernel em uma taxa regular. Probes úteis incluem:

- `profile-97`: amostra 97 vezes por segundo, em todos os CPUs
- `profile-1001hz`: amostra a 1001 Hz, levemente deslocado de qualquer carga de trabalho em segundos inteiros
- `profile-5ms`: amostra a cada 5 ms

Cada amostra registra a pilha atual. Agregar por pilha revela onde o tempo é gasto:

```sh
dtrace -n 'profile-1001hz /arg0/ { @[stack(8)] = count(); }
           tick-10s { exit(0); }'
```

O predicado `arg0` filtra CPUs ociosos. O `stack(8)` registra os 8 frames superiores. O `tick-10s` encerra a execução após 10 segundos.

A saída se parece com:

```text
  kernel`myfirst_process_data+0x120
  kernel`myfirst_intr+0x48
  kernel`ithread_loop+0x180
  kernel`fork_exit+0x7f
  kernel`fork_trampoline+0xe
         15823
```

15.823 amostras naquela pilha, de aproximadamente 100.000 no total (10 segundos * 1001 Hz * alguns CPUs). O caminho de interrupção do driver domina. Combinado com a medição de latência acima, isso é suficiente para justificar uma refatoração.

### Impressão Formatada de Estruturas

Um kernel com CTF habilitado fornece ao DTrace os tipos de toda estrutura do kernel. Isso permite que `print()` exiba campos pelo nome em vez de memória bruta:

```sh
dtrace -n 'fbt::myfirst_read:entry { print(*args[0]); }'
```

A saída mostra todos os campos do `struct myfirst_softc` sobre o qual a leitura está operando, com o nome de cada campo e valores legíveis por humanos.

O recurso depende de CTF ter sido gerado (`makeoptions WITH_CTF=1`). Sem CTF, o DTrace volta à impressão baseada em endereços, que é muito menos legível.

### Saída em Buffer Circular

Para execuções muito longas do DTrace, a saída padrão acaba por preencher a memória. A flag `-b` define um buffer circular que descarta registros antigos quando está cheio:

```sh
dtrace -b 16m -n 'myfirst::: { trace(timestamp); }'
```

Um buffer circular de 16 MB armazena aproximadamente os últimos 16 MB de saída de rastreamento. Dados mais antigos são sobrescritos à medida que dados mais novos chegam, o que geralmente é adequado para diagnóstico: a janela interessante são os últimos segundos antes de um problema, não a hora anterior.

### Encerrando de Forma Limpa

O DTrace acumula agregações na memória até que o script seja encerrado. Para uma execução longa, a leitora deve encerrar de forma limpa para ver as agregações:

```sh
dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }
           tick-60s { printf("%Y\n", walltimestamp); printa(@);
                      clear(@); }'
```

A cada 60 segundos, a agregação é impressa e zerada. A saída é um relatório contínuo, em vez de um único relatório ao sair. Esse padrão é ideal para execuções noturnas: a saída contém snapshots por minuto, e a leitora pode ver como o comportamento do driver evolui.

### Resumo

Essas técnicas juntas cobrem aproximadamente 80% das habilidades de DTrace de que um autor de drivers precisa. Os 20% restantes vêm do DTrace Guide e da leitura de scripts existentes em `/usr/share/dtrace/toolkit`.

Duas regras práticas guiam o uso avançado:

1. **Comece com o script mais simples que possa responder à pergunta.** Adicione predicados apenas quando o script produzir saída em excesso. Adicione agregações apenas quando contagens e tempos forem relevantes.

2. **Valide a saída do DTrace contra uma carga de trabalho conhecida.** Execute primeiro um caso de teste simples (por exemplo, um open, um read, um close), confirme que a saída do DTrace corresponde, e só então confie na ferramenta em uma carga de trabalho complexa.

Com essas regras, o DTrace é a ferramenta de depuração mais poderosa do arsenal do FreeBSD, e a que recompensa cada hora de estudo com meses de tempo de depuração economizado.

## Apêndice: Interpretação de Rastreamentos do WITNESS

O WITNESS é frequentemente o primeiro contato da leitora com a interpretação de um rastreamento de diagnóstico do kernel. Este apêndice percorre o formato de uma mensagem de violação do WITNESS e explica o que cada campo significa.

### O Formato Geral

Uma violação do WITNESS se parece com:

```text
lock order reversal: (non-sleepable after sleepable)
 1st 0xfffff8000a000000 myfirst_lock (myfirst, sleep mutex) @ myfirst.c:123
 2nd 0xfffff8000a001000 myfirst_intr_lock (myfirst, spin mutex) @ myfirst.c:234
lock order myfirst_intr_lock -> myfirst_lock established at:
#0 0xffffffff80abcdef at kdb_backtrace+0x1f
#1 0xffffffff80abcdee at _witness_debugger+0x4f
#2 0xffffffff80abcdaf at witness_checkorder+0x21f
#3 0xffffffff80c00000 at _mtx_lock_flags+0x8f
#4 0xffffffff80ffff00 at myfirst_intr+0x30
#5 ...
panic: witness
```

A mensagem tem três seções: a linha de cabeçalho, a seção de descrição dos locks e o rastreamento de onde a ordem invertida foi estabelecida pela primeira vez.

### O Cabeçalho

```text
lock order reversal: (non-sleepable after sleepable)
```

Isso indica que um spin mutex (não dormível) foi adquirido enquanto um sleep mutex (dormível) já estava sendo mantido. A ordem inversa (spin primeiro, depois sleep) é permitida; esta ordem (sleep primeiro, depois spin) não é.

Variantes comuns do cabeçalho:

- `lock order reversal: (sleepable after non-sleepable)`: significa que a ordem declarada foi posteriormente violada por uma aquisição que ocorre na direção contrária
- `spin lock recursion`: tentativa de adquirir um spinlock que a thread atual já possui, o que não é permitido exceto para spinlocks recursivos
- `spin lock held too long`: um spinlock foi mantido por mais tempo do que o limite estabelecido (geralmente vários segundos)
- `blocking on condition variable with spin lock`: tentativa de dormir enquanto um spinlock estava sendo mantido

### As Descrições de Lock

```text
 1st 0xfffff8000a000000 myfirst_lock (myfirst, sleep mutex) @ myfirst.c:123
 2nd 0xfffff8000a001000 myfirst_intr_lock (myfirst, spin mutex) @ myfirst.c:234
```

- **0xfffff8000a000000**: o endereço do mutex na memória
- **myfirst_lock**: o nome legível por humanos passado para `mtx_init`
- **(myfirst, sleep mutex)**: a classe (geralmente o nome do driver) e o tipo
- **@ myfirst.c:123**: a localização no código-fonte da chamada a `mtx_init` (quando WITNESS está configurado com DEBUG_LOCKS)

Ambos os locks aparecem: primeiro o que já estava sendo mantido, depois o que está sendo adquirido.

### O Backtrace

O backtrace mostra onde a ordem invertida foi registrada pela primeira vez. O WITNESS memoriza a primeira instância de cada par (A, B) que encontra; violações posteriores fazem referência à ocorrência anterior. Se a primeira ocorrência era válida na época (por exemplo, ambos os locks foram adquiridos corretamente), mas um caminho posterior os inverte, o backtrace aponta para a aquisição anterior e correta. O leitor pode precisar buscar no código-fonte o caminho específico de inversão, que geralmente é a stack trace atual (não mostrada neste trecho).

Dois frames a procurar:

1. A função que atualmente mantém o primeiro lock e está tentando adquirir o segundo (ou vice-versa).
2. A função que adquiriu os dois locks pela primeira vez na ordem original.

Comparar os dois revela o conflito.

### Lendo um Exemplo Real

Um exemplo comum em novos drivers:

```text
lock order reversal: (non-sleepable after sleepable)
 1st 0xfff0 myfirst_lock (sleep mutex) @ myfirst.c:100
 2nd 0xfff1 ithread_lock (spin mutex) @ kern_intr.c:234
```

O código do novo driver em `myfirst.c:100` adquire `myfirst_lock` (um sleep mutex). Em algum ponto mais fundo no caminho do código, uma função de agendamento de interrupção tenta adquirir `ithread_lock` (um spin mutex usado pelo subsistema de interrupções). Essa é a violação `sleep-then-spin`, que é sempre um bug: um spin lock nunca deve ser adquirido enquanto um sleep lock está sendo mantido.

A correção: reformule o código para que o spin lock (ou o subsistema que o utiliza) não seja chamado enquanto `myfirst_lock` está sendo mantido. As abordagens comuns incluem liberar `myfirst_lock` antes da chamada e readquiri-lo em seguida, se necessário.

### Afirmando a Posse do Lock

Independentemente do WITNESS, usar `mtx_assert(&lock, MA_OWNED)` no início de cada função que pressupõe que um lock está sendo mantido é um padrão de codificação defensiva robusto. Quando `INVARIANTS` está configurado, a asserção dispara em qualquer violação. Combinadas com o WITNESS, essas duas verificações capturam a grande maioria dos bugs relacionados a locks na primeira ocorrência, muito antes de corromperem dados em produção.

### Quando o WITNESS É Muito Ruidoso

Ocasionalmente, o WITNESS produz violações em código que é, na verdade, correto, geralmente porque a aquisição do lock é protegida por uma verificação em tempo de execução que o WITNESS não consegue ver. Para esses casos, `mtx_lock_flags(&lock, MTX_DUPOK)` ou `MTX_NEW` instruem o WITNESS a permitir a aquisição. Use esses flags com moderação; a maioria das violações do WITNESS é real.

## Apêndice: Construindo um Toolkit de Debug Específico para o Driver

As macros de debug e as probes SDT adicionadas ao `myfirst` na Seção 8 são um ponto de partida. Ao longo da vida de um driver, o toolkit cresce: mais classes, mais probes, sysctls personalizados, talvez um ioctl exclusivo para debug que despeja o estado interno. Este apêndice descreve padrões para estender o toolkit.

### Sysctls Exclusivos para Debug

Um sysctl em `dev.myfirst.0.debug.*` é o lugar natural para controles de debug em tempo de execução. Adicione nós generosamente:

- `dev.myfirst.0.debug.mask`: o bitmask de classe
- `dev.myfirst.0.debug.loglevel`: a prioridade de syslog para a saída do DPRINTF
- `dev.myfirst.0.debug.trace_pid`: um PID para focar o rastreamento
- `dev.myfirst.0.debug.count_io`: um contador de eventos de I/O processados
- `dev.myfirst.0.debug.dump_state`: somente escrita, aciona um dump de estado único

Cada um deles tem custo mínimo em tamanho e é imensamente útil durante a depuração em campo. A regra é: se um pedaço de estado seria útil inspecionar durante a investigação de um bug, exponha-o via sysctl.

### Um Ioctl de Debug

Para estados muito complexos ou grandes para expor como sysctl, um ioctl exclusivo para debug é uma boa escolha. Defina:

```c
#define MYFIRST_IOC_DUMP_STATE  _IOR('f', 100, struct myfirst_debug_state)

struct myfirst_debug_state {
        uint64_t        open_count;
        uint64_t        read_count;
        uint64_t        write_count;
        uint64_t        error_count;
        uint32_t        current_mask;
        /* ... */
};
```

O handler do ioctl copia o estado atual no buffer do usuário. Um pequeno programa em espaço do usuário lê e imprime:

```c
struct myfirst_debug_state s;
int fd = open("/dev/myfirst0", O_RDONLY);
ioctl(fd, MYFIRST_IOC_DUMP_STATE, &s);
printf("opens=%llu, reads=%llu, writes=%llu, errors=%llu\n",
    s.open_count, s.read_count, s.write_count, s.error_count);
```

Um ioctl de debug é especialmente útil quando o estado de interesse não é um número simples, mas uma estrutura, e quando as restrições de tipo do sysctl são inconvenientes.

### Contadores para Eventos Importantes

O softc do driver ganha um conjunto de contadores, um por evento significativo. Os contadores são campos `uint64_t` simples incrementados com `atomic_add_64` ou sob o lock do softc. Eles são expostos via sysctl ou pelo ioctl de debug, e podem ser zerados a pedido do leitor:

```c
sc->sc_stats.opens++;       /* in open */
sc->sc_stats.reads++;       /* in read */
sc->sc_stats.errors++;      /* on any error path */
```

Com dez contadores por driver, o custo de memória fica abaixo de 100 bytes, e a visibilidade do comportamento do driver é enorme. Sistemas de monitoramento em produção podem consultar os contadores periodicamente e alertar sobre anomalias.

### Um Ioctl de Autoteste

Para drivers complexos, um ioctl de autoteste pode ser inestimável. Ele executa uma sequência de testes internos (cada um é uma função no driver) e informa quais passaram e quais falharam. Os resultados retornam como uma pequena estrutura. Um operador depurando um problema em campo pode executar o autoteste e saber imediatamente se algum dos subsistemas do driver está com defeito.

O ioctl de autoteste não é um substituto para testes unitários ou de integração. É um atalho de diagnóstico para o campo.

### Integração com Scripts rc

Um script rc com suporte a debug (em `/usr/local/etc/rc.d/myfirst_debug`) pode configurar o debug mask no boot:

```sh
#!/bin/sh
# PROVIDE: myfirst_debug
# REQUIRE: myfirst
# KEYWORD: shutdown

. /etc/rc.subr

name="myfirst_debug"
rcvar="myfirst_debug_enable"
start_cmd="myfirst_debug_start"
stop_cmd="myfirst_debug_stop"

myfirst_debug_start()
{
        echo "Enabling myfirst debug mask"
        sysctl dev.myfirst.0.debug.mask=${myfirst_debug_mask:-0x0}
}

myfirst_debug_stop()
{
        echo "Disabling myfirst debug"
        sysctl dev.myfirst.0.debug.mask=0
}

load_rc_config $name
run_rc_command "$1"
```

Um operador define `myfirst_debug_enable=YES` e `myfirst_debug_mask=0x3` em `/etc/rc.conf`, reinicia, e a infraestrutura de debug é ativada automaticamente. Esse padrão é como os sistemas em produção gerenciam a verbosidade de debug em muitos dispositivos.

### Resumo

A infraestrutura da Seção 8 é suficiente para o exemplo do livro. Drivers reais crescem além disso, adquirindo uma dúzia ou mais de recursos voltados para debug ao longo do tempo. O padrão é sempre o mesmo: tornar a instrumentação barata quando desativada, rica quando ativada, e universalmente controlável pelo operador. Com essa disciplina, um driver permanece diagnosticável durante toda a sua vida.

## Apêndice: Referência de Laboratório para o Capítulo 23

Este último apêndice reúne os laboratórios em uma tabela com estimativas de tempo e dependências, para ajudar o leitor a planejar sua sessão:

| Lab  | Tópico                                        | Tempo est. | Dependências                          |
|------|-----------------------------------------------|------------|---------------------------------------|
| 23.1 | Primeira sessão com DDB                       | 15 min     | Kernel de debug com KDB/DDB           |
| 23.2 | Medição de driver com DTrace                  | 20 min     | KDTRACE_HOOKS, CTF, driver carregado  |
| 23.3 | ktrace do lado do usuário no acesso ao driver | 15 min     | Binário ktrace, driver carregado      |
| 23.4 | Introdução e correção de vazamento de memória | 30 min     | Capacidade de recompilar o módulo     |
| 23.5 | Instalação do refactor 1.6-debug              | 60 min     | Material da Seção 8, configuração de build |

Tempo total concentrado: cerca de duas horas e vinte minutos, mais o tempo para absorver o material. A intenção do livro é que o leitor realize cada laboratório em um ritmo que permita a compreensão genuína, sem pressa.

Exercícios opcionais complementares:

- **23.6 (opcional).** Combine `ktrace` e DTrace no mesmo programa de teste e correlacione o rastreamento do lado do usuário com os eventos do lado do driver.
- **23.7 (opcional).** Adicione um ioctl de debug (conforme descrito no apêndice do Debug Toolkit) e escreva um pequeno programa em espaço do usuário que o acione.

Essas duas extensões consolidam os temas do capítulo e desenvolvem habilidades práticas que todos os capítulos posteriores irão exercitar.

## Apêndice: Listagem Completa e Anotada das Alterações em myfirst_debug.h e myfirst_debug.c

Para consolidar a Seção 8 em uma única referência, este apêndice reúne o texto completo dos arquivos como aparecem após o refactor 1.6-debug, com comentários inline explicando cada bloco. Um leitor que prefere ver o código finalizado em um único lugar pode usar esta seção; a árvore de exemplos em `examples/part-05/ch23-debug/stage3-refactor/` contém o mesmo texto como arquivos para download.

### myfirst_debug.h

```c
/*
 * myfirst_debug.h - debug and tracing infrastructure for the myfirst driver
 *
 * This header is included from the driver's source files. It provides:
 *   - a bitmask of debug verbosity classes
 *   - the DPRINTF macro for conditional device_printf
 *   - declarations for SDT probes that the driver fires at key points
 *
 * The matching SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE calls live in the
 * driver source, which owns the storage for the probe entries.
 */

#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>

/*
 * Debug verbosity classes.  Each class is a single bit in sc->sc_debug.
 * The operator sets sysctl dev.myfirst.0.debug.mask to a combination of
 * these bits to enable the corresponding categories of output.
 *
 * Add new classes here when the driver grows new subsystems.  Use the
 * next unused bit and update DEBUG.md accordingly.
 */
#define MYF_DBG_INIT    0x00000001  /* probe/attach/detach */
#define MYF_DBG_OPEN    0x00000002  /* open/close lifecycle */
#define MYF_DBG_IO      0x00000004  /* read/write paths */
#define MYF_DBG_IOCTL   0x00000008  /* ioctl handling */
#define MYF_DBG_INTR    0x00000010  /* interrupt handler */
#define MYF_DBG_DMA     0x00000020  /* DMA mapping/sync */
#define MYF_DBG_PWR     0x00000040  /* power-management events */
#define MYF_DBG_MEM     0x00000080  /* malloc/free trace */
/* Bits 0x0100..0x8000 reserved for future driver subsystems */

#define MYF_DBG_ANY     0xFFFFFFFF
#define MYF_DBG_NONE    0x00000000

/*
 * DPRINTF - conditionally log a message via device_printf when the
 * given class bit is set in the softc's debug mask.
 *
 * Usage: DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d\n", pid);
 *
 * When the bit is clear, the cost is one load and one branch, which
 * is negligible in practice.  When the bit is set, the cost equals
 * a normal device_printf call.
 */
#ifdef _KERNEL
#define DPRINTF(sc, m, ...) do {                                        \
        if ((sc)->sc_debug & (m))                                        \
                device_printf((sc)->sc_dev, __VA_ARGS__);                \
} while (0)
#endif

/*
 * SDT probe declarations.  The matching SDT_PROBE_DEFINE calls are in
 * myfirst_debug.c (or in the main driver source if preferred).
 *
 * Probe argument conventions:
 *   open  (softc *, flags)            -- entry, before access check
 *   close (softc *, flags)            -- entry, before state update
 *   io    (softc *, is_write, resid, off) -- entry, into read or write
 */
SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);

#endif /* _MYFIRST_DEBUG_H_ */
```

O header tem três seções: o bitmask de classe, a macro DPRINTF e as declarações de probe SDT. Cada uma é pequena, autocontida e projetada para crescer conforme o driver cresce.

### As Definições SDT em myfirst_debug.c

```c
/*
 * myfirst_debug.c - storage for the SDT probe entries.
 *
 * This file exists to hold the SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE
 * declarations.  By convention in the myfirst driver, these live in a
 * dedicated source file to keep the main driver uncluttered.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>
#include "myfirst_debug.h"

/*
 * The provider "myfirst" exposes all of our static probes to DTrace.
 * Scripts select probes with "myfirst:::<name>".
 */
SDT_PROVIDER_DEFINE(myfirst);

/*
 * open: fired on every successful or attempted device open.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags from the open call
 */
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");

/*
 * close: fired on every device close.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags
 */
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");

/*
 * io: fired on every read or write call, at function entry.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int is_write (0 for read, 1 for write)
 *   arg2 = size_t resid (bytes requested)
 *   arg3 = off_t offset
 */
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
```

O arquivo é curto, intencionalmente. Seu único propósito é armazenar as entradas de probe. Se o driver ganhar mais probes, o leitor as adiciona aqui.

### Disparando as Probes no Driver

Dentro de `myfirst.c` (ou qualquer arquivo que implemente cada método), as probes disparam nos pontos de chamada apropriados:

```c
/*
 * myfirst_open: device open method.
 * Called when a user process opens /dev/myfirst0.
 */
static int
myfirst_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d uid=%d flags=0x%x\n",
            td->td_proc->p_pid, td->td_ucred->cr_uid, flags);

        /* Fire the SDT probe at entry, before any state change. */
        SDT_PROBE2(myfirst, , , open, sc, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count >= MYFIRST_MAX_OPENS) {
                error = EBUSY;
                goto out;
        }
        sc->sc_open_count++;
out:
        mtx_unlock(&sc->sc_mtx);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_OPEN, "open failed: error=%d\n", error);
        else
                DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n",
                    sc->sc_open_count);

        return (error);
}

/*
 * myfirst_close: device close method.
 * Called when the last reference to the device is released.
 */
static int
myfirst_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;

        DPRINTF(sc, MYF_DBG_OPEN, "close: pid=%d flags=0x%x\n",
            td->td_proc->p_pid, flags);

        SDT_PROBE2(myfirst, , , close, sc, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0)
                sc->sc_open_count--;
        DPRINTF(sc, MYF_DBG_OPEN, "close ok: count=%d\n", sc->sc_open_count);
        mtx_unlock(&sc->sc_mtx);

        return (0);
}

/*
 * myfirst_read: device read method.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        DPRINTF(sc, MYF_DBG_IO, "read: pid=%d resid=%zu off=%jd\n",
            curthread->td_proc->p_pid,
            (size_t)uio->uio_resid, (intmax_t)uio->uio_offset);

        SDT_PROBE4(myfirst, , , io, sc, 0,
            (size_t)uio->uio_resid, uio->uio_offset);

        error = myfirst_read_impl(sc, uio);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_IO, "read failed: error=%d\n", error);
        return (error);
}

/*
 * myfirst_write: device write method.
 */
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        DPRINTF(sc, MYF_DBG_IO, "write: pid=%d resid=%zu off=%jd\n",
            curthread->td_proc->p_pid,
            (size_t)uio->uio_resid, (intmax_t)uio->uio_offset);

        SDT_PROBE4(myfirst, , , io, sc, 1,
            (size_t)uio->uio_resid, uio->uio_offset);

        error = myfirst_write_impl(sc, uio);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_IO, "write failed: error=%d\n", error);
        return (error);
}
```

Cada método segue o padrão entrada-saída-erro:

1. Um `DPRINTF` na entrada, mostrando quem chamou e com quais argumentos.
2. Um `SDT_PROBE` após o log de entrada, antes do trabalho real começar.
3. Um `DPRINTF` no caminho de erro em qualquer retorno não nulo.
4. Um `DPRINTF` no caminho de sucesso mostrando o resultado quando útil.

### Registrando o Sysctl em myfirst_attach

O sysctl que controla `sc_debug` é registrado durante o attach do dispositivo:

```c
/*
 * Build the debug sysctl tree:  dev.myfirst.N.debug.*
 */
sysctl_ctx_init(&sc->sc_sysctl_ctx);

sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
    OID_AUTO, "debug",
    CTLFLAG_RW, 0, "debug and tracing controls");

SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0,
    "debug class bitmask (see myfirst_debug.h for class definitions)");
```

O caminho de detach desmonta a árvore de sysctl:

```c
sysctl_ctx_free(&sc->sc_sysctl_ctx);
```

### O Incremento de Versão

No início do código-fonte do driver, a string de versão e o MODULE_VERSION são atualizados:

```c
static const char myfirst_version[] = "myfirst 1.6-debug";

/* ...driver methods, attach, detach, etc... */

MODULE_VERSION(myfirst, 16);
```

E próximo ao início de `myfirst_attach`, a versão é registrada:

```c
DPRINTF(sc, MYF_DBG_INIT, "attach: %s loaded\n", myfirst_version);
```

Com o mask configurado para incluir `MYF_DBG_INIT`, o operador vê uma linha limpa de reporte de versão a cada attach.

### Um dmesg de Exemplo com Todas as Classes Habilitadas

Quando todas as classes estão habilitadas e o dispositivo é exercitado, `dmesg` mostra um log abrangente:

```text
myfirst0: attach: myfirst 1.6-debug loaded
myfirst0: open: pid=1234 uid=1001 flags=0x0
myfirst0: open ok: count=1
myfirst0: read: pid=1234 resid=4096 off=0
myfirst0: close: pid=1234 flags=0x0
myfirst0: close ok: count=0
```

Todo evento que interessa ao driver é visível, marcado com o nome do dispositivo, o PID e os argumentos relevantes. Um engenheiro de campo que recebe esse log pode reconstruir a sequência de eventos sem precisar de acesso ao código-fonte.

Com todas as classes desabilitadas, `dmesg` mostra apenas a linha de attach (se `MYF_DBG_INIT` estiver habilitado) ou nada (se o mask for 0). O driver é executado em velocidade máxima sem custo de observabilidade.

### Uma Sessão de Exemplo com DTrace e as Probes

As probes SDT permitem análise legível por máquina:

```sh
$ sudo dtrace -n 'myfirst::: { @[probename] = count(); } tick-10s { exit(0); }'
dtrace: description 'myfirst::: ' matched 3 probes
CPU     ID                    FUNCTION:NAME
  0  49012                        :tick-10s

  close                                                     43
  open                                                      43
  io                                                       120
```

Durante os 10 segundos de amostragem, o driver processou 43 ciclos de abertura/fechamento e 120 eventos de I/O. O agregado é gerado sem qualquer modificação no driver e com custo de tempo de execução essencialmente zero, pois sem nenhum script conectado, as probes ficam inertes.

### Resumo do Refactor

O refactor 1.6-debug adiciona aproximadamente 100 linhas de código distribuídas assim:

- `myfirst_debug.h`: 50 linhas (classes, DPRINTF, declarações SDT)
- `myfirst_debug.c`: 30 linhas (provedor SDT e definições de probe)
- Alterações no código-fonte do driver: 20 linhas (registro de sysctl, chamadas DPRINTF, disparos de probe)

O investimento é pequeno; o retorno é enorme. Todos os capítulos subsequentes do livro herdam essa infraestrutura sem custo adicional, e todo problema em campo no futuro do driver ganha um caminho de diagnóstico claro.

## Apêndice: O Que o Kernel Faz Quando um Driver Causa Panic

Um leitor que acompanhou o capítulo até aqui conhece bem as ferramentas de depuração. O apêndice final aborda o que acontece quando algo dá terrivelmente errado: um panic. Para um autor de driver, compreender o comportamento do kernel durante um panic não é opcional, pois qualquer bug grave o suficiente para causar um panic produz uma sequência específica de eventos sobre a qual o driver deve estar correto.

### A Sequência de Panic

Quando qualquer código do kernel chama `panic()`, o seguinte acontece, em ordem:

1. A mensagem de panic é impressa no console, tipicamente via `printf`.
2. O CPU eleva sua prioridade ao máximo, interrompendo a maior parte da atividade normal do kernel.
3. Se o kernel foi compilado com `options KDB`, e `kdb_unattended` não está definido, o controle passa para o debugger do kernel. O operador vê o prompt `db>`.
4. Se o operador (ou `kdb_unattended=1`) continua o panic, o kernel inicia um crash dump. O dump escreve a imagem da memória atual no dispositivo de crash dump (tipicamente swap).
5. O kernel reinicia (a menos que `panic_reboot_wait_time` esteja definido).

No próximo boot, `savecore(8)` extrai o dump do dispositivo de swap, grava-o em `/var/crash/vmcore.N` e renumera o contador. O leitor pode então executar `kgdb` sobre o dump para inspecionar o estado post-mortem.

### Responsabilidades do Driver

O caminho de panic impõe requisitos específicos aos drivers:

1. **Não provoque panic durante o detach.** Um driver que entra em panic durante o `detach` impede o shutdown limpo do restante do kernel. Todo caminho de detach deve estar livre de panics.

2. **Não aloque memória no handler de panic.** Se o driver registra um shutdown hook que executa durante o panic, esse hook não deve chamar `malloc` nem qualquer função que possa dormir.

3. **Não dependa do espaço do usuário durante o panic.** Os programas do usuário ficam congelados durante o panic; o driver não pode enviar mensagens a eles.

4. **Não aguarde interrupções durante o panic.** As interrupções podem estar desabilitadas durante o caminho de panic; um driver que aguarda uma delas travará.

Essas regras se aplicam aos shutdown hooks e aos raros caminhos que o kernel percorre durante o panic. Em operação normal, os drivers não executam no caminho de panic, mas certos callbacks sim.

### Lendo um Crash Dump

O crash dump é uma imagem completa da memória do kernel no momento do panic. O `kgdb` o abre com informações simbólicas completas se o kernel tiver informações de debug:

```sh
sudo kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

Dentro do `kgdb`, os comandos mais úteis são:

```console
(kgdb) bt            # backtrace of the thread that panicked
(kgdb) info threads  # list all threads
(kgdb) thread N      # switch to thread N
(kgdb) frame N       # show frame N (in the current backtrace)
(kgdb) list          # show source at the current frame
(kgdb) info locals   # show local variables
(kgdb) print var     # show a variable
(kgdb) print *sc     # show the softc pointer contents
(kgdb) ptype struct myfirst_softc  # show the structure type
```

Passos comuns de investigação:

1. `bt` para ver o que causou o panic.
2. `list` para ver a linha do código-fonte.
3. `info locals` para ver o estado das variáveis locais.
4. `print` nos campos de interesse.
5. `thread apply all bt` para ver todas as threads, caso o panic tenha sido disparado pela interação de uma thread com outra.

### Combinando Símbolos do Kernel e do Driver

Se o driver foi compilado como módulo, o `kgdb` precisa carregar seus símbolos também:

```console
(kgdb) add-symbol-file /boot/kernel/myfirst.ko <address>
```

O `<address>` é o endereço de carregamento do módulo, que o `kldstat -v` mostra no sistema em execução ou que aparece na saída do panic. Uma vez carregado, o `kgdb` consegue decodificar os frames dentro do módulo com visibilidade completa do código-fonte.

### Tamanho do Crash Dump

Um crash dump completo tem o tamanho do uso de memória do kernel no momento do panic, tipicamente vários gigabytes. O dump device (geralmente o swap) deve ter espaço suficiente. Sistemas ajustados para recuperação rápida após panics frequentemente usam um dump device dedicado, e não o swap, para evitar as limitações de espaço do swap.

Para depuração focada em drivers, um mini dump captura apenas a memória do próprio kernel, sem a memória do usuário. Defina `dumpdev=none` ou `dumpdev=mini` no `loader.conf` para ajustar isso. Mini dumps são pequenos (dezenas de megabytes) e carregam rapidamente no `kgdb`.

### Resumo

O caminho de panic é raro em produção, mas essencial de compreender. Um driver que trata seus erros de forma limpa raramente dispara um panic; um driver que provoca panic produz um crash dump que o autor pode analisar com o `kgdb`. A combinação de `INVARIANTS`, `WITNESS`, `DEBUG_MEMGUARD` e um pipeline de crash dump funcional transforma panics em eventos diagnósticos valiosos, e não em catástrofes.

## Apêndice: Capítulo 23 em Resumo

Este resumo de uma página é um recapitulativo que o leitor pode manter aberto enquanto trabalha. Cada entrada nomeia um conceito, aponta onde ele foi introduzido e fornece o comando ou código mínimo para exercitá-lo.

### Ferramentas em ordem de primeiro uso

1. **`printf` / `device_printf`** (Seção 2): Logging básico do kernel. Use `device_printf(dev, "msg\n")` dentro dos métodos do driver.
2. **`log(LOG_PRIORITY, ...)`** (Seção 2): Logging roteado pelo syslog, com um nível de prioridade. Use para mensagens que pertencem ao `/var/log/messages`.
3. **`dmesg`** (Seção 3): Lê o buffer de mensagens do kernel. Combine com `grep` e `tail` para saída filtrada.
4. **`/var/log/messages`** (Seção 3): Histórico de log persistente. O `syslogd(8)` escreve nele; o `newsyslog(8)` o rotaciona.
5. **Kernel de debug** (Seção 4): Recompile com `KDB`, `DDB`, `INVARIANTS`, `WITNESS`, `KDTRACE_HOOKS` e `DEBUG=-g`. Mais lento, mas diagnóstico.
6. **`ddb`** (Seção 4): Depurador interativo do kernel. Entre via `sysctl debug.kdb.enter=1` ou um break no console. Use `bt`, `show locks`, `ps`, `continue`.
7. **`kgdb`** (Seção 4): Depurador post-mortem. Abra `/boot/kernel/kernel` e `/var/crash/vmcore.N` juntos.
8. **DTrace** (Seção 5): Rastreamento e medição em tempo real. `dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'`.
9. **Probes SDT** (Seção 5, Seção 8): Pontos de rastreamento estáticos no código-fonte do driver. `dtrace -n 'myfirst::: { @[probename] = count(); }'`.
10. **`ktrace`/`kdump`** (Seção 6): Rastreamento de syscalls no lado do usuário. `sudo ktrace -di -f trace.out ./program; kdump -f trace.out`.
11. **`vmstat -m`** (Seção 7.1): Visão da memória do kernel por pool. Usado para detectar vazamentos.

### Reflexos a desenvolver

- Adicione um `device_printf` antes de suspeitar de um bug, não depois.
- Verifique o `dmesg` antes de tentar adivinhar o comportamento do driver.
- Recompile com `INVARIANTS` e `WITNESS` durante o desenvolvimento.
- Escreva one-liners de DTrace em vez de ciclos de compilação e recarga quando a pergunta for "isso está sendo disparado?".
- Mantenha a estabilidade do `vmstat -m` como um invariante contínuo, não como uma verificação posterior.
- Nunca segure um sleep lock durante uma chamada a `uiomove` ou qualquer outra chamada que possa dormir.
- Mantenha os interrupt handlers curtos. Delegue o trabalho ao `taskqueue`.
- Corresponda cada `malloc` a um `free`. Teste essa correspondência com as ferramentas.

### O refactor 1.6-debug

- Adiciona `myfirst_debug.h` com 8 classes de verbosidade e a macro DPRINTF.
- Adiciona um `sysctl dev.myfirst.0.debug.mask` para controle em tempo de execução.
- Adiciona três probes SDT: `myfirst:::open`, `myfirst:::close`, `myfirst:::io`.
- Segue o padrão de entrada/saída/erro em todos os métodos.
- Incrementa `myfirst_version` para `1.6-debug` e `MODULE_VERSION` para 16.

### Próximos passos recomendados

- Conclua todos os cinco laboratórios se o tempo permitir.
- Tente o Exercício Desafio 23.2, o script personalizado de DTrace.
- Mantenha um registro pessoal de laboratório para este capítulo.
- Leia `/usr/src/sys/dev/ath/if_ath_debug.h` como referência do mundo real.
- Antes de iniciar o Capítulo 24, confirme que o driver está na versão `1.6-debug` e que o debug mask está responsivo.

Isso conclui o material de referência do capítulo. As seções "Encerrando" e de ponte que se seguem preparam o leitor para o Capítulo 24.

## Encerrando

O Capítulo 23 apresentou os métodos de trabalho e as ferramentas de depuração do kernel FreeBSD. A estrutura do capítulo seguiu um arco deliberado: explicar por que a depuração do kernel é diferente, ensinar as ferramentas principais de logging e inspeção, construir um kernel de debug, dominar o DTrace e o ktrace, reconhecer as classes de bugs mais comuns e, finalmente, refatorar o driver `myfirst` para suportar sua própria observabilidade.

Ao longo do caminho, o capítulo ganhou seu lugar no livro. Ele se posiciona entre o trabalho arquitetural da Parte 4 e o trabalho de integração da Parte 5 porque os capítulos restantes adicionarão código novo substancial ao `myfirst` (hooks de devfs, ioctl, sysctl, interfaces de rede, interfaces GEOM, suporte a USB, suporte a virtio) e cada um desses capítulos depende da capacidade de ver o que o driver está fazendo e de corrigir problemas à medida que surgem. As ferramentas e a instrumentação não são um pensamento posterior; são o que torna o restante do livro gerenciável.

O driver em si está agora na versão `1.6-debug`. Seu softc carrega uma máscara `sc_debug`; a árvore tem uma subárvore `sysctl dev.myfirst.0.debug`; a macro DPRINTF controla o logging por classe; e três probes SDT expõem os eventos de open, close e I/O. Todos os capítulos posteriores nas Partes 5, 6 e 7 estenderão o mesmo framework: novas classes aparecerão em `myfirst_debug.h`, e novos probes serão declarados à medida que o driver adquire novas fronteiras funcionais.

### O que o leitor já sabe fazer

- Usar `device_printf`, `log` e o padrão de macro `DPRINTF` para registrar eventos estruturados do kernel.
- Ler o `dmesg` e o `/var/log/messages` para localizar mensagens do driver e correlacioná-las com eventos.
- Construir um kernel de debug com `KDB`, `DDB`, `INVARIANTS`, `WITNESS` e `KDTRACE_HOOKS`.
- Escrever one-liners de DTrace que contam, agregam e cronometram eventos do kernel.
- Usar `ktrace` e `kdump` para observar interações do lado do usuário com o driver.
- Reconhecer os bugs de driver mais comuns e associá-los à ferramenta de debug correta.
- Declarar probes SDT e expô-los a scripts DTrace.
- Incrementar a versão do driver e levar a infraestrutura de debug adiante.

### O que vem a seguir

Com a infraestrutura de debug instalada, o Capítulo 24 (Integrando com o Kernel: devfs, ioctl e sysctl) estenderá a interface do driver para o espaço do usuário. O leitor criará entradas `cdev` mais ricas, definirá ioctls personalizados e registrará sysctls de ajuste para os parâmetros de tempo de execução do driver. Cada uma dessas adições pode agora ser instrumentada e rastreada com o framework deste capítulo, de modo que o trabalho estará fundamentado em um comportamento visível e depurável desde a primeira linha.

O arsenal de debug construído no Capítulo 23 será intensamente utilizado a partir daqui. No Capítulo 24, o leitor adicionará seu primeiro ioctl de verdade, e na primeira vez que um argumento chegar com o valor errado, ele ativará `MYF_DBG_IOCTL`, lerá o log e encontrará o erro em segundos, e não em horas. No Capítulo 25 (Escrevendo Drivers de Caracteres em Profundidade), o leitor usará o DTrace para confirmar que o caminho select/poll do driver está sendo disparado corretamente. No Capítulo 26 e além, cada novo hook de subsistema será registrado pelo DPRINTF e sondado com SDT. O retorno sobre investimento do capítulo de debug é longo e constante.

Um encorajamento final: as ferramentas deste capítulo não são glamourosas. Elas não produzem drivers funcionais por conta própria. Mas são o que transforma um bug frustrante de seis horas em um diagnóstico preciso de vinte minutos. Todo desenvolvedor FreeBSD experiente já rastreou um problema pelo `vmstat -m`, depois pelo DTrace, depois pelo código-fonte e emergiu com uma correção precisa, pelo menos uma dúzia de vezes. O leitor que domina a caixa de ferramentas deste capítulo se junta a esse grupo e está agora pronto para o trabalho de integração mais rico que vem a seguir.

## Ponte para o Capítulo 24

O Capítulo 24 (Integrando com o Kernel: devfs, ioctl e sysctl) abre o próximo arco do livro. Até o Capítulo 23, o driver era um único nó `cdev` com comportamento fixo. O Capítulo 24 torna a interface expressiva: os leitores estenderão o `cdev` em entradas devfs mais ricas, definirão ioctls personalizados para configurar o dispositivo e registrarão nós sysctl para parâmetros de tempo de execução.

A ponte é direta. O debug mask introduzido na Seção 8 é um sysctl. Os ioctls do Capítulo 24 serão rastreados pelo `MYF_DBG_IOCTL`. Os nós devfs personalizados registrarão sua criação e destruição pelo `MYF_DBG_INIT`. Quando o leitor construir novos recursos no Capítulo 24, o framework de debug estará pronto para observá-los.

Até o Capítulo 24.

## Referência: Cartão de Referência Rápida do Capítulo 23

A tabela abaixo resume as ferramentas, comandos e padrões introduzidos no Capítulo 23 para consulta rápida.

### Logging

| Chamada                           | Uso                                                            |
|-----------------------------------|----------------------------------------------------------------|
| `printf("...")`                   | Logging muito básico; sem identidade de dispositivo.           |
| `device_printf(dev, "...")`       | Log padrão do driver; prefixado com o nome do dispositivo.     |
| `log(LOG_PRI, "...")`             | Syslog com prioridade; enviado para `/var/log/messages`.       |
| `DPRINTF(sc, CLASS, "...")`       | Condicional a `sc->sc_debug & CLASS`; macro padrão do driver.  |

### Níveis de prioridade do syslog

| Prioridade   | Significado                                                     |
|--------------|-----------------------------------------------------------------|
| `LOG_EMERG`  | O sistema está inutilizável.                                    |
| `LOG_ALERT`  | Uma ação deve ser tomada imediatamente.                         |
| `LOG_CRIT`   | Condições críticas.                                             |
| `LOG_ERR`    | Condições de erro.                                              |
| `LOG_WARNING`| Condições de aviso.                                             |
| `LOG_NOTICE` | Condição normal, mas significativa.                             |
| `LOG_INFO`   | Informativo.                                                    |
| `LOG_DEBUG`  | Mensagens de nível debug.                                       |

### Opções do kernel para depuração

| Opção                        | Efeito                                                          |
|------------------------------|-----------------------------------------------------------------|
| `options KDB`                | Inclui o framework do debugger do kernel.                       |
| `options DDB`                | Interface interativa do debugger no console.                    |
| `options DDB_CTF`            | Suporte a CTF no DDB para impressão com reconhecimento de tipos.|
| `options KDB_TRACE`          | Backtrace automático na entrada do KDB.                         |
| `options KDB_UNATTENDED`     | Panics reiniciam o sistema em vez de entrar no DDB.             |
| `options INVARIANTS`         | Habilita `KASSERT`, `MPASS` e outras asserções do kernel.       |
| `options WITNESS`            | Rastreia a ordem dos locks e gera panic em violações.           |
| `options WITNESS_KDB`        | Entra no KDB ao detectar uma violação do WITNESS.               |
| `options DEBUG_MEMGUARD`     | Envenena memória liberada; detecta acessos use-after-free.      |
| `options KDTRACE_HOOKS`      | Habilita probes do DTrace.                                      |
| `options KDTRACE_FRAME`      | Gera frame pointers para stack walks do DTrace.                 |
| `makeoptions WITH_CTF=1`     | Gera CTF no kernel e nos módulos.                               |
| `makeoptions DEBUG=-g`       | Inclui DWARF completo no kernel e nos módulos.                  |

### One-liners comuns do DTrace

```sh
# Count every function entry in the driver
dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'

# Measure the time spent in a specific function
dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
           fbt::myfirst_read:return /self->t/ {
               @ = quantize(timestamp - self->t); self->t = 0; }'

# Count SDT probes fired by the driver
dtrace -n 'myfirst::: { @[probename] = count(); }'

# Show syscall frequency for a named process
dtrace -n 'syscall:::entry /execname == "myftest"/ { @[probefunc] = count(); }'

# Aggregate I/O sizes through the driver
dtrace -n 'myfirst:::io { @ = quantize(arg2); }'
```

### Fluxo de trabalho com ktrace / kdump

```sh
# Record a process under ktrace
sudo ktrace -di -f trace.out ./myprogram

# Dump in human-readable form
kdump -f trace.out

# Filter for syscalls only
kdump -f trace.out | grep -E 'CALL|RET|NAMI'
```

### Checklist de depuração (resumido)

1. O `dmesg` está limpo? Se não estiver, leia a última mensagem antes do problema.
2. O `vmstat -m` está estável para o pool do driver? Se não estiver, procure um vazamento.
3. O `WITNESS` gera panic na carga de trabalho? Se sim, a ordem dos locks está errada.
4. Os contadores de `fbt::myfirst_*:entry` correspondem à atividade esperada? Se não, verifique os caminhos.
5. O `ktrace` do lado do usuário exibe a sequência de syscalls esperada? Se não, verifique a configuração do ioctl ou do comando.
6. O driver está na versão esperada? Execute `kldstat -v | grep myfirst` e confirme o `MODULE_VERSION`.

## Referência: Glossário dos Termos do Capítulo 23

**CTF**: Compact C Type Format. Um formato de informação de tipos do kernel que o DDB e o DTrace utilizam para exibir estruturas tipadas. Requer `makeoptions WITH_CTF=1`.

**DDB**: O depurador de kernel embutido do FreeBSD. Interativo, baseado em console. Ativado por `options DDB`.

**DDB_CTF**: Uma opção do DDB que permite ao depurador exibir valores tipados usando informações de tipos CTF.

**device_printf**: A função de log padrão do FreeBSD para mensagens de driver. Prefixa a saída com o nome do dispositivo.

**DPRINTF**: O macro convencional de driver para log condicional, controlado por uma máscara de verbosidade.

**DTrace**: Um framework de rastreamento dinâmico. Utiliza os providers `fbt`, `syscall`, `sdt`, `io`, `sched`, `lockstat` e outros.

**fbt**: Function Boundary Tracing. Um provider do DTrace que rastreia a entrada e o retorno de cada função do kernel.

**INVARIANTS**: Uma opção de build do kernel que habilita `KASSERT`, `MPASS` e asserções relacionadas. Padrão em builds de depuração.

**KASSERT**: Um macro de asserção que é avaliado apenas quando `INVARIANTS` está definido. Gera panic se a condição for falsa.

**KDB**: O framework do depurador de kernel. O `DDB` é um de seus backends.

**KDTRACE_HOOKS**: A opção de build do kernel que habilita as sondas do DTrace.

**kdump**: A ferramenta em espaço do usuário que lê os arquivos de saída do `ktrace` e os exibe em formato legível.

**ktrace**: A ferramenta em espaço do usuário que registra syscalls e outros eventos de um determinado processo.

**log**: A função do kernel para enviar mensagens ao syslog com um nível de prioridade.

**MPASS**: Semelhante ao `KASSERT`, mas com verificação exclusiva em tempo de compilação. Custo zero em builds de produção.

**myfirst_debug.h**: O header de depuração introduzido na Seção 8, que declara classes de verbosidade e estruturas de sondas SDT.

**sc_debug**: O campo `uint32_t` no softc do driver que controla quais categorias de `DPRINTF` estão ativas.

**SDT**: Statically Defined Tracing. Sondas em tempo de compilação às quais o DTrace pode se conectar.

**SDT_PROBE_DEFINE**: Família de macros que registra uma nova sonda SDT no kernel ou no driver.

**syslog**: O subsistema de log do BSD; as prioridades das mensagens são mapeadas para destinos conforme `/etc/syslog.conf`.

**vmstat -m**: Uma ferramenta em espaço do usuário que exibe estatísticas de memória do kernel por pool.

**WITNESS**: Um sistema de verificação de ordem de locks do kernel. Gera panic em violações de ordem de lock quando `options WITNESS` está definido.

**1.6-debug**: A versão do driver após a Seção 8. Inclui a máscara de verbosidade, sysctl, o padrão DPRINTF e as sondas SDT.
