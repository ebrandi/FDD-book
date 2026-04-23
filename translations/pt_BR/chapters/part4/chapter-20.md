---
title: "Tratamento Avançado de Interrupções"
description: "O Capítulo 20 expande o driver de interrupção do Capítulo 19 com suporte a MSI e MSI-X. Ele ensina a diferença entre INTx legado, MSI e MSI-X; como consultar a contagem de capacidades com `pci_msi_count(9)` e `pci_msix_count(9)`; como alocar vetores com `pci_alloc_msi(9)` e `pci_alloc_msix(9)`; como construir a hierarquia de fallback de MSI-X até INTx legado; como registrar handlers de filtro por vetor com funções `driver_filter_t` separadas; como projetar estruturas de dados por vetor seguras para interrupções; como atribuir a cada vetor um papel específico e uma afinidade de CPU específica; e como desmontar com segurança um driver com múltiplos vetores. O driver evolui de 1.2-intr para 1.3-msi, ganha um novo arquivo específico para MSI-X e prepara o terreno para DMA no Capítulo 21."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 20
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 165
language: "pt-BR"
---
# Tratamento Avançado de Interrupções

## Orientação ao Leitor e Objetivos

O Capítulo 19 terminou com um driver que sabia escutar seu dispositivo. O módulo `myfirst` na versão `1.2-intr` tem um filter handler registrado na linha PCI INTx legada, uma task de trabalho diferido em um taskqueue, um sysctl de interrupção simulada para testes no alvo de laboratório bhyve, uma ordem rigorosa de teardown e um novo arquivo `myfirst_intr.c` que mantém o código de interrupção organizado. A disciplina de locking do Capítulo 11 se mantém: atômicos no filter, sleep locks na task, `INTR_MPSAFE` no handler e segurança para IRQ compartilhado via `FILTER_STRAY`. O driver se comporta como um driver real pequeno que, por acaso, tem uma única fonte de interrupções.

O que o driver ainda não faz é tirar proveito de tudo que o PCIe oferece. Um dispositivo PCIe moderno não precisa compartilhar uma única linha com seus vizinhos. Ele pode solicitar uma interrupção dedicada por meio do mecanismo de mensagem sinalizada introduzido pelo PCI 2.2 (MSI) ou pela tabela por função mais rica que o MSI-X adicionou no PCIe. Um dispositivo com várias filas (uma NIC com filas de recepção e transmissão, um controlador NVMe com filas de admin e submissão de I/O, um controlador host USB3 moderno com filas de eventos) normalmente quer uma interrupção por fila, em vez de uma única interrupção compartilhada para o dispositivo inteiro. O Capítulo 20 ensina o driver a fazer essa solicitação.

O escopo do capítulo é precisamente essa transição: o que MSI e MSI-X são no nível do hardware, como o FreeBSD os representa em termos de recursos de IRQ adicionais, como um driver consulta contagens de capacidade e aloca vetores, como a escada de fallback de MSI-X para MSI e depois para INTx legado funciona na prática, como registrar várias funções filter distintas no mesmo dispositivo, como projetar estruturas de dados por vetor para que o handler de cada vetor acesse apenas seu próprio estado, como atribuir a cada vetor uma afinidade de CPU que corresponda ao posicionamento NUMA do dispositivo, como rotular cada vetor com `bus_describe_intr(9)` para que `vmstat -i` informe ao operador qual vetor faz o quê, e como desmontar cada um desses itens na ordem correta. O capítulo para antes de chegar ao DMA, que é o Capítulo 21; os vetores de recepção e transmissão por fila se tornam especialmente valiosos quando um anel de descritores entra em cena, mas ensinar os dois ao mesmo tempo diluiria ambos.

O Capítulo 20 mantém vários tópicos vizinhos a uma distância segura. DMA completo (tags `bus_dma(9)`, anéis de descritores, bounce buffers, coerência de cache) é o Capítulo 21. O framework de múltiplas filas do Iflib, que envolve o MSI-X com uma camada de maquinaria iflib por fila, é um tópico da Parte 6 (Capítulo 28) para leitores que querem o caminho de rede no estilo iflib. As operações mais ricas de tabela de máscara MSI-X por função (direcionamento de endereços de mensagem específicos para CPUs específicas diretamente pela tabela MSI-X) são discutidas, mas não implementadas de ponta a ponta. O remapeamento de interrupções específico de plataforma via IOMMU, o compartilhamento de vetores SR-IOV e a recuperação de interrupções orientada por PCIe AER ficam para capítulos posteriores. O Capítulo 20 se mantém dentro do terreno que pode cobrir bem e faz a transferência explícita quando um tópico merece um capítulo próprio.

O trabalho com múltiplos vetores repousa sobre todas as camadas anteriores da Parte 4. O Capítulo 16 deu ao driver um vocabulário de acesso a registradores. O Capítulo 17 o ensinou a pensar como um dispositivo. O Capítulo 18 o apresentou a um dispositivo PCI real. O Capítulo 19 lhe deu ouvidos em um único IRQ. O Capítulo 20 lhe dá um conjunto de ouvidos, um por conversa que o dispositivo quer ter. O Capítulo 21 ensinará esses ouvidos a cooperar com a própria capacidade do dispositivo de alcançar a RAM. Cada capítulo adiciona uma camada. Cada camada depende das anteriores. O Capítulo 20 é onde o driver para de fingir que o dispositivo tem apenas uma coisa a dizer e começa a tratá-lo como a máquina de múltiplas filas que ele realmente é.

### Por que o MSI-X Merece um Capítulo Próprio

Neste ponto, você pode estar se perguntando por que MSI e MSI-X precisam de um capítulo próprio. O driver do Capítulo 19 tem um interrupt handler funcional na linha de IRQ legada. Se o pipeline filter-mais-task já está correto, por que não continuar usando-o? O MSI-X realmente justifica um capítulo inteiro de material novo?

Três razões.

A primeira é escala. Uma única linha de IRQ em um sistema compartilhado força todos os drivers nessa linha a serializar por meio de um único `intr_event`. Em um host com dezenas de dispositivos PCIe, o mecanismo INTx legado causaria um gargalo no sistema inteiro se fosse a única opção. O MSI-X permite que cada dispositivo (e cada fila dentro de um dispositivo) tenha seu próprio `intr_event` dedicado, atendido por seu próprio ithread ou filter handler, fixado em sua própria CPU. A diferença entre um servidor moderno processando dez milhões de pacotes por segundo com MSI-X e a mesma carga de trabalho em INTx legado é a diferença entre "possível" e "impossível"; é o MSI-X que torna o primeiro uma realidade.

A segunda é localidade. Com uma única linha de interrupção, o kernel tem uma única opção de CPU para rotear a interrupção, e essa escolha é global para o dispositivo. Com MSI-X, cada vetor pode ser fixado em uma CPU diferente, e bons drivers fixam cada vetor em uma CPU que é NUMA-local para a fila que ele serve. As vantagens de cache-line ao fazer isso são reais: uma fila de recepção cuja interrupção dispara na mesma CPU que eventualmente consome o pacote evita o tráfego de cache entre sockets que domina nas configurações legadas.

A terceira é clareza. Mesmo para um driver que não precisa de alta taxa de transferência, MSI ou MSI-X pode simplificar o handler. Com uma linha dedicada, o filter não precisa tratar o caso de IRQ compartilhado. Com um vetor dedicado por classe de evento (admin, recepção, transmissão, erro), cada handler é menor e mais especializado, e o driver inteiro se torna mais fácil de ler. Bons drivers usam MSI-X mesmo quando o desempenho não o exige, porque o código fica melhor.

O Capítulo 20 justifica seu lugar ensinando os três benefícios de forma concreta. Um leitor termina o capítulo capaz de alocar vetores, roteá-los, descrevê-los e desmontá-los, com um driver funcional que demonstra o padrão de ponta a ponta.

### Onde o Capítulo 19 Deixou o Driver

Uma breve recapitulação de onde você deve estar. O Capítulo 20 estende o driver produzido ao final do Estágio 4 do Capítulo 19, marcado como versão `1.2-intr`. Se algum dos itens abaixo parecer incerto, retorne ao Capítulo 19 antes de começar este capítulo.

- Seu driver compila sem erros e se identifica como `1.2-intr` no `kldstat -v`.
- Em um guest bhyve ou QEMU que expõe um dispositivo virtio-rnd, o driver realiza o attach, aloca a BAR 0 como `SYS_RES_MEMORY`, aloca o IRQ legado como `SYS_RES_IRQ` com `rid = 0`, registra um filter handler via `bus_setup_intr(9)` com `INTR_TYPE_MISC | INTR_MPSAFE`, cria `/dev/myfirst0` e suporta o sysctl `dev.myfirst.N.intr_simulate`.
- O filter lê `INTR_STATUS`, incrementa contadores por bit, realiza o acknowledge, enfileira a task diferida para `DATA_AV` e retorna o valor `FILTER_*` correto.
- A task (`myfirst_intr_data_task_fn`) executa em contexto de thread em um taskqueue chamado `myfirst_intr` com prioridade `PI_NET`, lê `DATA_OUT`, atualiza o softc e faz broadcast em `sc->data_cv`.
- O caminho de detach limpa `INTR_MASK`, chama `bus_teardown_intr`, drena e libera o taskqueue, libera o recurso de IRQ, realiza o detach da camada de hardware e libera a BAR.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md` e `INTERRUPTS.md` estão atualizados.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados no seu kernel de teste.

Esse driver é o que o Capítulo 20 estende. As adições são consideráveis em escopo: um novo arquivo (`myfirst_msix.c`), um novo header (`myfirst_msix.h`), vários novos campos no softc para rastrear o estado por vetor, uma nova família de funções filter por vetor, uma nova escada de fallback no helper de configuração, chamadas `bus_describe_intr` por vetor, binding opcional de CPU, uma atualização de versão para `1.3-msi`, um novo documento `MSIX.md` e atualizações no teste de regressão. O modelo mental também cresce: o driver passa a pensar nas interrupções como um vetor de fontes em vez de um único fluxo de eventos.

### O que Você Vai Aprender

Ao passar para o próximo capítulo, você será capaz de:

- Descrever o que é uma interrupção MSI e MSI-X no nível do hardware, como cada uma é sinalizada pelo PCIe (como uma escrita na memória em vez de uma mudança de nível elétrico) e por que os dois mecanismos coexistem com o INTx legado.
- Explicar as principais diferenças entre MSI e MSI-X: a contagem de vetores MSI (de 1 a 32, de um bloco contíguo), a contagem de vetores MSI-X (até 2048 vetores endereçáveis de forma independente) e as capacidades de endereço e máscara por vetor que o MSI-X oferece e o MSI não.
- Consultar as capacidades MSI e MSI-X de um dispositivo via `pci_msi_count(9)` e `pci_msix_count(9)` e saber o que a contagem retornada significa.
- Alocar vetores MSI ou MSI-X via `pci_alloc_msi(9)` e `pci_alloc_msix(9)`, tratar o caso em que o kernel aloca menos vetores do que o solicitado e se recuperar de falhas de alocação.
- Construir uma escada de fallback em três níveis: MSI-X primeiro (se disponível), depois MSI (se MSI-X não estiver disponível ou a alocação falhar), depois INTx legado. Cada nível usa o mesmo padrão `bus_setup_intr` em seu núcleo, mas o rid e a estrutura de handler por vetor diferem.
- Alocar recursos de IRQ por vetor com o rid correto (rid=0 para INTx legado; rid=1, 2, 3, ... para vetores MSI e MSI-X).
- Registrar um filter handler distinto por vetor para que cada vetor tenha seu próprio propósito (admin, fila de recepção-N, fila de transmissão-N, erro).
- Projetar o estado por vetor (contadores por fila, task por fila, lock por fila) de modo que handlers executando concorrentemente em CPUs diferentes não disputem dados compartilhados.
- Descrever cada vetor com `bus_describe_intr(9)` para que `vmstat -i` e `devinfo -v` mostrem cada vetor com um nome significativo.
- Fixar cada vetor em uma CPU específica com `bus_bind_intr(9)` e consultar o conjunto de CPUs NUMA-local do dispositivo com `bus_get_cpus(9)` usando `LOCAL_CPUS` ou `INTR_CPUS`.
- Tratar falhas de alocação parcial: o dispositivo tem oito vetores, o kernel nos deu três; ajustar o driver para usar os três e fazer o trabalho restante via polling ou tasks agendadas.
- Desmontar corretamente um driver com múltiplos vetores: `bus_teardown_intr` por vetor, `bus_release_resource` por vetor e, por fim, um único `pci_release_msi(9)` ao final.
- Registrar uma única linha de resumo clara no dmesg no momento do attach indicando o modo de interrupção (MSI-X / N vetores, MSI / K vetores ou INTx legado), para que o operador veja imediatamente em qual nível o driver acabou operando.
- Separar o código de múltiplos vetores em `myfirst_msix.c`, atualizar a linha `SRCS` do módulo, marcar o driver como `1.3-msi` e produzir `MSIX.md` documentando os propósitos por vetor e os padrões de contadores observados.

A lista é longa; cada item é pontual. O objetivo do capítulo é a composição.

### O que Este Capítulo Não Cobre

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 20 permaneça focado.

- **DMA.** As tags de `bus_dma(9)`, `bus_dmamap_load(9)`, listas scatter-gather, bounce buffers, a coerência de cache em torno dos descritores de DMA e a forma como o dispositivo escreve as conclusões na RAM são tratados no Capítulo 21. O Capítulo 20 fornece ao driver múltiplos vetores; o Capítulo 21 dá ao dispositivo a capacidade de mover dados. Cada metade tem valor independente; juntas, elas formam a espinha dorsal de todo driver de alto desempenho moderno.
- **iflib(9) e o framework de rede multi-fila.** O iflib é um framework robusto e opinativo que envolve o MSI-X com ithreads por fila, pools de DMA por fila e um conjunto de mecanismos que um driver genérico não precisa. O Capítulo 20 ensina o padrão básico; o capítulo de redes da Parte 6 (Capítulo 28) o revisita no vocabulário do iflib.
- **Recuperação por AER do PCIe via vetores MSI-X.** O Advanced Error Reporting pode sinalizar por meio de seu próprio vetor MSI-X em alguns dispositivos. O Capítulo 20 menciona essa possibilidade; o caminho completo de recuperação é um tópico de capítulos posteriores.
- **SR-IOV e interrupções por VF.** Uma função virtual de Single-Root IO Virtualization tem sua própria capacidade MSI-X e seus próprios vetores por VF. O driver do Capítulo 20 é uma função física; a história do VF é uma especialização de capítulos posteriores.
- **Ajuste de prioridade de thread por vetor.** Um driver pode passar uma prioridade diferente para as flags de `bus_setup_intr` de cada vetor ou usar `taskqueue_start_threads` com prioridades distintas por vetor. O Capítulo 20 usa `INTR_TYPE_MISC | INTR_MPSAFE` para cada vetor e não realiza ajuste de prioridades; os capítulos de desempenho da Parte 7 (Capítulo 33) tratam desse tema.
- **Transporte virtio-PCI moderno usando capacidades do PCIe.** O driver `virtio_pci_modern(4)` coloca as notificações de virtqueue dentro de estruturas de capability e usa vetores MSI-X para as conclusões de virtqueue. O driver do Capítulo 20 ainda tem como alvo um BAR legado do virtio-rnd; um leitor que o adaptar para um dispositivo real de produção seguirá o padrão do Capítulo 20, mas lerá a partir do layout moderno do virtio PCI.

Manter-se dentro dessas fronteiras preserva o Capítulo 20 como um capítulo dedicado ao tratamento de interrupções com múltiplos vetores. O vocabulário é o que se transfere; os capítulos específicos que seguem aplicam esse vocabulário a DMA, iflib, AER e SR-IOV.

### Investimento de Tempo Estimado

- **Somente leitura**: quatro a cinco horas. O modelo conceitual de MSI/MSI-X não é complexo, mas a disciplina por vetor, a cadeia de fallback e a questão de afinidade de CPU se beneficiam de uma leitura cuidadosa.
- **Leitura com digitação dos exemplos trabalhados**: dez a doze horas ao longo de duas ou três sessões. O driver evolui em quatro estágios: cadeia de fallback, múltiplos vetores, handlers por vetor, refatoração. Cada estágio é pequeno, mas exige atenção cuidadosa ao estado por vetor.
- **Leitura com todos os laboratórios e desafios**: dezesseis a vinte horas ao longo de quatro ou cinco sessões, incluindo a leitura de drivers reais (`virtio_pci.c`, o código MSI-X de `if_em.c` e a divisão de vetores admin+IO de `nvme.c`), a configuração de um guest bhyve ou QEMU com MSI-X exposto e a execução do teste de regressão do capítulo.

As Seções 3, 5 e 6 são as mais densas. Se o padrão de handler por vetor parecer pouco familiar na primeira leitura, isso é normal. Pare, releia o diagrama da Seção 3 e continue quando a estrutura estiver clara.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 19 (`1.2-intr`). O ponto de partida pressupõe todos os primitivos do Capítulo 19: o pipeline de filtro e tarefa, o sysctl de interrupção simulada, as macros de acesso `ICSR_*` e o teardown limpo.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco, compatível com o kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e inicializando corretamente.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível. Para os laboratórios de MSI-X, o guest deve expor um dispositivo com a capacidade MSI-X habilitada. O `virtio-rng-pci` do QEMU tem MSI-X; o `virtio-rnd` do bhyve usa virtio legado e não expõe MSI-X no driver do host por padrão. O capítulo indica quais laboratórios requerem qual ambiente.
- As ferramentas `devinfo(8)`, `vmstat(8)`, `pciconf(8)` e `cpuset(1)` estão no seu PATH.

Se algum item acima estiver incerto, corrija-o agora. MSI-X tende a expor qualquer fragilidade latente na disciplina de contexto de interrupção do driver, pois múltiplos handlers podem ser executados em múltiplas CPUs ao mesmo tempo. O `WITNESS` do kernel de depuração é especialmente valioso durante o desenvolvimento do Capítulo 20.

### Como Aproveitar ao Máximo Este Capítulo

Quatro hábitos trarão resultados rapidamente.

Primeiro, mantenha `/usr/src/sys/dev/pci/pcireg.h` e `/usr/src/sys/dev/pci/pcivar.h` nos favoritos, junto com os novos arquivos `/usr/src/sys/dev/pci/pci.c` e `/usr/src/sys/dev/virtio/pci/virtio_pci.c`. Os dois primeiros são do Capítulo 18 e definem as constantes de capacidade (`PCIY_MSI`, `PCIY_MSIX`, `PCIM_MSIXCTRL_*`) e os wrappers de acesso. O terceiro é a implementação do kernel dos métodos `pci_msi_count_method`, `pci_alloc_msi_method`, `pci_alloc_msix_method` e `pci_release_msi_method`. O quarto é um exemplo limpo de driver real com a cadeia completa de alocação MSI-X com fallback. Cada arquivo recompensa meia hora de leitura.

Segundo, execute `pciconf -lvc` no host de laboratório e em um guest. O flag `-c` instrui o `pciconf` a imprimir a lista de capacidades de cada dispositivo, e você verá quais dispositivos expõem MSI, MSI-X ou ambos. Examinar sua própria máquina é a maneira mais rápida de entender por que MSI-X é o padrão em todo hardware PCIe moderno.

Terceiro, digite as alterações manualmente e execute cada estágio. O código MSI-X é onde erros sutis por vetor produzem bugs que só aparecem sob carga concorrente. Digitando com atenção, observando o `dmesg` em busca de mensagens de attach e executando o teste de regressão após cada estágio, você captura esses erros no momento em que são fáceis de corrigir.

Quarto, leia a configuração MSI-X em `/usr/src/sys/dev/nvme/nvme_ctrlr.c` (procure por `nvme_ctrlr_allocate_bar` e `nvme_ctrlr_construct_admin_qpair`) após a Seção 5. `nvme(4)` é um exemplo limpo de driver real com o padrão admin-mais-N-filas que o Capítulo 20 ensina. O arquivo é longo, mas o código MSI-X é uma pequena fração dele; o restante da leitura é opcional, porém educativo.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. **O Que São MSI e MSI-X?** O panorama do hardware: como as interrupções sinalizadas por mensagem funcionam sobre PCIe, a diferença entre MSI e MSI-X, e por que os dispositivos modernos as preferem.
2. **Habilitando MSI no Seu Driver.** O mais simples dos dois modos. Consulta de contagem, alocação, registro de um handler. Estágio 1 do driver do Capítulo 20 (`1.3-msi-stage1`).
3. **Gerenciando Múltiplos Vetores de Interrupção.** O núcleo do capítulo. rid por vetor, função de filtro por vetor, estado softc por vetor, `bus_describe_intr` por vetor. Estágio 2 (`1.3-msi-stage2`).
4. **Projetando Estruturas de Dados Seguras para Interrupção.** Por que multi-vetor significa multi-CPU, quais locks o handler de cada vetor pode ou não tocar e como estruturar o estado por fila. Uma disciplina, não uma mudança de estágio.
5. **Usando MSI-X para Alta Flexibilidade.** O mecanismo mais completo. Layout da tabela, vinculação por vetor, posicionamento com consciência de NUMA com `bus_get_cpus`. Estágio 3 (`1.3-msi-stage3`).
6. **Tratando Eventos Específicos por Vetor.** Funções de handler por vetor, trabalho adiado por vetor, um padrão que o driver `nvme(4)` usa em escala.
7. **Teardown e Limpeza com MSI/MSI-X.** Teardown por vetor, seguido de uma única chamada a `pci_release_msi`. As regras de ordenação que mantêm tudo seguro.
8. **Refatorando e Versionando Seu Driver Multi-Vetor.** A divisão final em `myfirst_msix.c`, o novo `MSIX.md`, o incremento de versão para `1.3-msi` e a passagem de regressão. Estágio 4.

Após as oito seções vêm laboratórios práticos, exercícios desafio, uma referência de solução de problemas, um Encerrando que fecha a história do Capítulo 20 e abre a do Capítulo 21, e uma ponte para o Capítulo 21. O material de referência e consulta rápida ao final do capítulo foi concebido para ser relido enquanto você avança pelo Capítulo 21. O vocabulário do Capítulo 20 (vetor, rid, softc por vetor, afinidade, ordenação do teardown) é a base sobre a qual o trabalho com DMA do Capítulo 21 se apoia.

Se esta é sua primeira leitura, avance linearmente e faça os laboratórios em ordem. Se estiver revisitando, as Seções 3 e 5 funcionam de forma independente e são boas leituras em uma única sessão.



## Seção 1: O Que São MSI e MSI-X?

Antes do código do driver, o panorama do hardware. A Seção 1 ensina o que são interrupções sinalizadas por mensagem no nível do barramento PCIe e do controlador de interrupções, sem vocabulário específico do FreeBSD. Um leitor que compreende a Seção 1 pode ler o restante do capítulo tendo o caminho MSI/MSI-X do kernel como um objeto concreto, e não como uma abstração vaga.

### O Problema com o INTx Legado

O Capítulo 19 ensinou o modelo legado de interrupção PCI INTx: cada função PCI tem uma linha de interrupção (geralmente uma entre INTA, INTB, INTC, INTD), a linha é disparada por nível e múltiplos dispositivos na mesma linha física a compartilham. O driver do Capítulo 19 tratou o caso de compartilhamento corretamente, lendo INTR_STATUS primeiro e retornando `FILTER_STRAY` quando nada estava ativo.

INTx funciona. Mas tem três problemas que crescem em importância conforme os sistemas escalam.

O primeiro é a **sobrecarga de compartilhamento**. Uma linha compartilhada com dez dispositivos exige que todo driver seja chamado em cada interrupção, apenas para ler seu próprio registrador de status e descobrir que a interrupção não era para ele. Em um sistema onde a maioria das interrupções é legítima (a linha está ocupada), isso representa algumas chamadas extras a `bus_read_4` por evento. Em um sistema onde um dispositivo dispara incessantemente, o filtro de todos os outros drivers é executado desnecessariamente. O custo de CPU é pequeno por evento, mas se acumula ao longo de milhões de eventos por segundo.

O segundo é a **ausência de separação por fila**. Uma NIC moderna tem quatro, oito, dezesseis ou sessenta e quatro filas de recepção e um número equivalente de filas de transmissão. Cada fila quer sua própria interrupção: quando a fila de recepção 3 tem pacotes, apenas o handler da fila de recepção 3 deve ser executado, em uma CPU próxima à memória que essa fila usa. Com INTx, o dispositivo tem apenas uma linha, então o driver precisa sondar todas as filas a partir de um único handler (caro e lento) ou o dispositivo suporta apenas uma fila (inaceitável para uma NIC de dez gigabits).

O terceiro é a **ausência de afinidade de CPU por tipo de evento**. Uma linha compartilhada dispara em uma única CPU, aquela para a qual o controlador de interrupções a roteia. Em um sistema NUMA onde o dispositivo está conectado ao socket 0, disparar a interrupção em uma CPU do socket 1 é pior do que disparar em uma CPU do socket 0: o código do handler é executado no socket 1, mas a memória do dispositivo reside no socket 0, e toda leitura de registrador atravessa o fabric entre sockets. Com INTx, o driver não pode dizer "dispare esta interrupção na CPU 3"; o kernel decide e o driver não tem influência por tipo de evento.

MSI e MSI-X resolvem os três problemas. O mecanismo é fundamentalmente diferente do INTx: em vez de sinalização elétrica por um fio dedicado, o dispositivo realiza uma escrita na memória em um endereço específico, e o controlador de interrupções da CPU trata essa escrita como uma interrupção. Isso desacopla o número de interrupções do número de fios físicos, permite que cada interrupção sinalizada por mensagem tenha seu próprio endereço de destino (e, portanto, sua própria CPU) e elimina completamente o problema de compartilhamento de linha.

### Como uma Interrupção Disparada por MSI Realmente Funciona

Fisicamente, uma interrupção MSI é uma transação de escrita no fabric PCIe. O dispositivo emite uma escrita de um valor específico em um endereço específico. O controlador de memória reconhece o endereço como pertencente à região MSI do controlador de interrupções e roteia a escrita para o APIC (ou GIC, ou qualquer que seja o controlador de interrupções da plataforma). O controlador de interrupções decodifica o endereço para determinar qual CPU deve receber a interrupção e decodifica o valor escrito para determinar qual vetor (qual entrada na IDT ou equivalente) a CPU deve executar. A CPU então realiza o despacho como faria para qualquer interrupção: salva o estado, salta para o handler do vetor e executa o despacho de interrupção do kernel.

Da perspectiva do driver, o fluxo é quase idêntico ao INTx legado:

1. O dispositivo tem um evento.
2. A interrupção dispara.
3. O kernel chama o handler de filtro do driver.
4. O filtro lê o status, reconhece, trata ou adia, e retorna.
5. Se adiado, a tarefa é executada posteriormente.
6. O teardown prossegue no caminho de detach.

O que difere é o mecanismo no passo 2 e o modelo de alocação no momento da configuração. O dispositivo não está ativando um fio; ele está escrevendo na memória. O kernel não precisa de um fio de destino pré-arranjado; ele tem um pool de endereços de mensagem e valores de mensagem. Cada vetor corresponde a um par (endereço, valor). O dispositivo armazena esses pares em sua estrutura de capacidade MSI e emite uma escrita usando-os quando precisa de uma interrupção.

### MSI: O Mais Simples dos Dois

MSI (Message Signalled Interrupts) é o mais antigo e simples dos dois mecanismos. Introduzido no PCI 2.2 em 1999, o MSI permite que um dispositivo solicite entre 1 e 32 vetores de interrupção, alocados como um bloco contíguo de potência de dois (1, 2, 4, 8, 16 ou 32). O dispositivo tem uma única estrutura de capacidade MSI em seu espaço de configuração, que contém:

- Um registrador de endereço de mensagem (o endereço de destino da escrita, tipicamente a região MSI do APIC).
- Um registrador de dados de mensagem (o valor escrito, que codifica o número do vetor).
- Um registrador de controle de mensagem (bit de habilitação, bit de máscara de função, número de vetores solicitados e assim por diante).

Quando o dispositivo quer sinalizar o vetor N (onde N vai de 0 até contagem-1), ele escreve no endereço de mensagem o resultado do OR entre o valor base do registrador de dados de mensagem e N. O controlador de interrupções demultiplexa o valor escrito para despachar o vetor correto.

Propriedades principais do MSI:

- **Bloco de capacidade único.** O dispositivo tem uma capacidade MSI, não uma por vetor.
- **Vetores contíguos.** O bloco é potência de dois e alocado como uma unidade.
- **Contagem limitada.** Máximo de 32 vetores por função.
- **Sem mascaramento por vetor.** O bloco inteiro é mascarado ou desmascarado como grupo (via o bit de máscara de função, se suportado).
- **Sem endereço por vetor.** Todos os vetores compartilham um único registrador de endereço de mensagem; o número do vetor vai nos bits baixos dos dados escritos.

MSI é uma melhoria significativa em relação ao INTx legado, mas tem limitações: sem mascaramento por vetor e um limite de 32 vetores. A maioria dos drivers que quer múltiplos vetores acaba preferindo MSI-X.

### MSI-X: O Mecanismo Completo

O MSI-X, introduzido no PCI 3.0 em 2004 e estendido no PCIe, elimina as limitações do MSI. O dispositivo possui uma estrutura de capacidade MSI-X, além de uma **tabela** MSI-X (um array de entradas por vetor) e um **array de bits pendentes** (PBA). A estrutura de capacidade aponta para um ou mais dos BARs do dispositivo, onde a tabela e o PBA residem.

Cada entrada da tabela MSI-X contém:

- Um registrador de endereço de mensagem (por vetor).
- Um registrador de dados de mensagem (por vetor).
- Um registrador de controle de vetor (bit de máscara por vetor).

Quando o dispositivo quer sinalizar o vetor N, ele consulta a entrada N na tabela, lê o endereço e os dados daquela entrada e realiza a escrita. O controlador de interrupções despacha com base no que foi escrito.

Propriedades fundamentais do MSI-X:

- **Endereço e dados por vetor.** Cada vetor pode ser roteado para uma CPU diferente programando um endereço diferente.
- **Máscara por vetor.** Vetores individuais podem ser desabilitados sem desabilitar o bloco inteiro.
- **Até 2048 vetores por função.** Um controlador NVMe com muitas filas fica à vontade aqui; um NIC com 64 filas de recepção, 64 filas de transmissão e alguns vetores administrativos se encaixa tranquilamente.
- **Tabela em um BAR.** A localização da tabela é descoberta através dos registradores de capacidade MSI-X; `pci_msix_table_bar(9)` e `pci_msix_pba_bar(9)` retornam qual BAR contém cada uma.
- **Configuração mais complexa.** O driver precisa alocar a tabela, programar cada entrada e só então habilitar.

Na prática, dispositivos PCIe modernos preferem MSI-X para qualquer caso de uso com múltiplos vetores, reservando o MSI para compatibilidade retroativa ou dispositivos simples de vetor único. O kernel cuida da maior parte da programação da tabela internamente; o trabalho do driver é consultar a contagem, alocar e registrar os handlers por vetor.

### Como o FreeBSD Abstrai a Diferença

O kernel oculta a maior parte da diferença entre MSI e MSI-X por trás de um pequeno conjunto de funções de acesso. Em `/usr/src/sys/dev/pci/pcivar.h`:

- `pci_msi_count(dev)` retorna a contagem de vetores MSI que o dispositivo anuncia (0 se não há capacidade MSI).
- `pci_msix_count(dev)` retorna a contagem de vetores MSI-X (0 se não há capacidade MSI-X).
- `pci_alloc_msi(dev, &count)` e `pci_alloc_msix(dev, &count)` alocam vetores. O parâmetro `count` é de entrada e saída: a entrada é a contagem desejada, a saída é a contagem efetivamente alocada.
- `pci_release_msi(dev)` libera tanto os vetores MSI quanto os MSI-X (ela trata ambos os casos internamente).

O driver não interage com a tabela MSI-X diretamente; o kernel faz isso em seu nome. O que o driver enxerga é que, após uma alocação bem-sucedida, o dispositivo passa a ter recursos IRQ adicionais disponíveis através de `bus_alloc_resource_any(9)` com `SYS_RES_IRQ`, usando `rid = 1, 2, 3, ...` para os vetores alocados. O driver então registra um handler de filtro para cada recurso da mesma forma que o Capítulo 19 registrou um para a linha legada.

A simetria é intencional. A mesma chamada `bus_setup_intr(9)` que tratou o IRQ legado em `rid = 0` trata cada vetor MSI ou MSI-X em `rid = 1, 2, 3, ...`. Toda regra `INTR_MPSAFE`, toda convenção de valor de retorno `FILTER_*`, toda disciplina de IRQ compartilhado (para MSI, onde vetores podem tecnicamente compartilhar um `intr_event` em casos extremos) e toda ordenação de desmontagem do Capítulo 19 se aplicam aqui da mesma forma.

### A Escada de Fallback

Um driver robusto tenta os mecanismos em ordem de preferência e recorre ao próximo quando a alocação falha. A escada canônica:

1. **MSI-X primeiro.** Se `pci_msix_count(dev)` for diferente de zero, tente `pci_alloc_msix(dev, &count)`. Se tiver êxito, use MSI-X. Em um dispositivo PCIe moderno, este é o caminho preferido.
2. **MSI em segundo.** Se MSI-X estiver indisponível ou a alocação tiver falhado, verifique `pci_msi_count(dev)`. Se for diferente de zero, tente `pci_alloc_msi(dev, &count)`. Se tiver êxito, use MSI.
3. **INTx legado por último.** Se tanto MSI-X quanto MSI estiverem indisponíveis, recorra ao caminho legado do Capítulo 19 com `rid = 0`.

Drivers reais implementam essa escada para funcionar em qualquer sistema onde possam ser instalados, de um drive NVMe novinho que só suporta MSI-X a um chipset legado que só suporta INTx. O driver do Capítulo 20 faz o mesmo; a Seção 2 escreve o caminho MSI, a Seção 5 escreve o caminho MSI-X e a Seção 8 os une em uma única escada de fallback.

### Exemplos do Mundo Real

Um breve tour por dispositivos que usam MSI e MSI-X.

**NICs modernos.** Um NIC típico de 10 ou 25 Gbps expõe de 16 a 64 vetores MSI-X: um por fila de recepção, um por fila de transmissão e alguns para eventos administrativos, de erro e de estado do link. Os drivers `igc(4)`, `em(4)`, `ix(4)` e `ixl(4)` da Intel seguem esse padrão; `bnxt(4)` da Broadcom, `mlx4(4)` e `mlx5(4)` da Mellanox e `cxgbe(4)` da Chelsio fazem o mesmo. O framework `iflib(9)` encapsula a alocação MSI-X para muitos drivers.

**Controladores de armazenamento NVMe.** Um controlador NVMe possui uma fila de administração e até 65535 filas de I/O. Na prática, os drivers alocam um vetor MSI-X para a fila de administração e um por fila de I/O até o limite de `NCPU`. O driver `nvme(4)` do FreeBSD faz exatamente isso; o código é legível e vale a pena estudar.

**Controladores de host USB modernos.** Um controlador de host xHCI (USB 3) normalmente anuncia um vetor MSI-X para o anel de eventos de conclusão de comandos e mais alguns para anéis de eventos por slot em variantes de alto desempenho. O caminho de configuração do driver `xhci(4)` mostra o padrão de administração mais eventos.

**GPUs.** Uma GPU discreta moderna possui muitos vetores MSI-X: um para o buffer de comandos, um ou mais para exibição, um por engine, um para gerenciamento de energia e outros. Os drivers drm-kmod fora da árvore exercitam extensivamente o MSI-X.

**Dispositivos Virtio em VMs.** Quando um guest FreeBSD roda sob bhyve, KVM ou VMware, o transporte virtio-PCI moderno usa MSI-X: um vetor para eventos de mudança de configuração e um por virtqueue. O driver `virtio_pci_modern(4)` implementa isso.

Cada um desses drivers segue o mesmo padrão que o Capítulo 20 ensina: consultar, alocar, registrar handlers por vetor, associar a CPUs e descrever. Os detalhes diferem (quantos vetores, como são atribuídos a eventos, como são associados a CPUs), mas a estrutura é constante.

### Por Que MSI-X e Não MSI

O leitor pode perguntar: dado que o MSI-X é estritamente mais capaz que o MSI, por que o MSI ainda existe? Por duas razões.

A primeira é a compatibilidade retroativa. Dispositivos e placas-mãe anteriores ao PCI 3.0 podem suportar MSI mas não MSI-X. Um driver que queira funcionar em hardware mais antigo precisa de um fallback para MSI. A maior parte do ecossistema avançou, mas a cauda longa de dispositivos mais antigos ainda existe.

A segunda é a simplicidade. MSI com um ou dois vetores é mais simples de configurar que MSI-X (sem tabela para programar, sem BAR para consultar). Para dispositivos cujas necessidades de interrupção cabem no limite de 32 vetores do MSI e que não precisam de mascaramento por vetor, o MSI é a escolha mais leve. Muitos dispositivos PCIe simples expõem apenas MSI por essa razão.

A resposta prática para o driver do Capítulo 20: tente sempre MSI-X primeiro, recorra ao MSI se MSI-X estiver indisponível e recorra ao INTx legado se nenhum dos dois estiver disponível. Todo driver FreeBSD real escrito na última década usa essa escada.

### Um Diagrama do Fluxo MSI-X

```text
  Device    Config space    MSI-X table (in BAR)     Interrupt controller     CPU
 --------   ------------   ---------------------    --------------------    -----
   |             |                 |                         |                |
   | event N    |                 |                         |                |
   | occurs     |                 |                         |                |
   |            |                 |                         |                |
   | read       |                 |                         |                |
   | entry N   -+---------------->|                         |                |
   | from table |   address_N,    |                         |                |
   |            |   data_N        |                         |                |
   |<-----------+-----------------|                         |                |
   |                              |                         |                |
   | memory-write to address_N                             |                |
   |-----------------------------+------------------------->|                |
   |                              |                         |                |
   |                              |                         | steer to CPU  |
   |                              |                         |-------------->|
   |                              |                         |               | filter_N
   |                              |                         |               | runs
   |                              |                         |               |
   |                              |                         | EOI           |
   |                              |                         |<--------------|
```

O diagrama omite as leituras da tabela MSI-X (que o dispositivo realiza internamente antes de emitir a escrita) e a lógica de demultiplexação do controlador de interrupções, mas captura a essência do mecanismo: o evento do dispositivo aciona uma escrita na memória, a escrita na memória se torna uma interrupção, a interrupção é despachada para um filtro. O filtro faz o mesmo trabalho que o filtro do Capítulo 19 fazia. A única diferença é que, no MSI-X, há um filtro diferente para cada vetor.

### Exercício: Encontrando Dispositivos com Capacidade MSI no Seu Sistema

Antes de avançar para a Seção 2, um exercício rápido para tornar concreta a visão das capacidades.

No seu host de laboratório, execute:

```sh
sudo pciconf -lvc
```

O flag `-c` diz ao `pciconf(8)` para imprimir a lista de capacidades de cada dispositivo. Você verá entradas como:

```text
vgapci0@pci0:0:2:0: ...
    ...
    cap 05[d0] = MSI supports 1 message, 64 bit
    cap 10[a0] = PCI-Express 2 endpoint max data 128(128)
em0@pci0:0:25:0: ...
    ...
    cap 01[c8] = powerspec 2  supports D0 D3  current D0
    cap 05[d0] = MSI supports 1 message, 64 bit
    cap 11[e0] = MSI-X supports 4 messages
```

Cada `cap 05` é uma capacidade MSI. Cada `cap 11` é uma capacidade MSI-X. A descrição após o sinal de igual informa quantas mensagens (vetores) o dispositivo suporta naquele modo.

Escolha três dispositivos da sua saída. Para cada um, anote:

- A contagem MSI (se houver).
- A contagem MSI-X (se houver).
- Qual deles o driver está usando atualmente. (Você pode deduzir isso pelas entradas de `vmstat -i` para o dispositivo: se você ver múltiplas linhas `name:queueN`, o driver está usando MSI-X.)

Um host com poucos dispositivos PCIe pode mostrar apenas capacidades MSI; laptops frequentemente têm uso limitado de MSI-X. Um servidor moderno com múltiplos NICs e um drive NVMe mostra muitas capacidades MSI-X com contagens de vetores altas (64 ou mais em alguns NICs).

Mantenha essa saída aberta enquanto lê a Seção 2. O vocabulário de "cap 11[XX] = MSI-X supports N messages" é o que `pci_msix_count(9)` do kernel retorna ao driver, e o que a escada de alocação consulta no momento do attach.

### Encerrando a Seção 1

MSI e MSI-X são os sucessores modernos, baseados em mensagens, ao INTx legado. O MSI oferece até 32 vetores alocados como um bloco contíguo com um único endereço de destino; o MSI-X oferece até 2048 vetores com endereços por vetor, dados por vetor e mascaramento por vetor. Ambos são sinalizados sobre PCIe como escritas na memória que o controlador de interrupções decodifica em despachos de vetor.

O kernel abstrai a diferença por trás de `pci_msi_count(9)`, `pci_msix_count(9)`, `pci_alloc_msi(9)`, `pci_alloc_msix(9)` e `pci_release_msi(9)`. Cada vetor alocado se torna um recurso IRQ em `rid = 1, 2, 3, ...` para o qual o driver registra um handler de filtro via `bus_setup_intr(9)`, exatamente como o Capítulo 19 fez para o IRQ legado em `rid = 0`.

Um driver robusto implementa uma escada de fallback em três níveis: MSI-X preferido, MSI como fallback, INTx legado como último recurso. A Seção 2 escreve a parte MSI dessa escada. A Seção 5 escreve a parte MSI-X. A Seção 8 monta a escada completa.



## Seção 2: Habilitando MSI no Seu Driver

A Seção 1 estabeleceu o modelo de hardware. A Seção 2 coloca o driver em funcionamento. A tarefa é precisa: estender o caminho de attach do Capítulo 19 para que, antes de recorrer ao IRQ legado em `rid = 0`, o driver tente alocar um vetor MSI. Se a alocação tiver êxito, o driver usa o vetor MSI em vez da linha legada. Se a alocação falhar (seja porque o dispositivo não suporta MSI ou porque o kernel não consegue alocar), o driver recorre ao caminho legado exatamente como o código do Capítulo 19 fazia.

O objetivo da Seção 2 é apresentar a API MSI de forma isolada, antes que as complicações de múltiplos vetores do MSI-X tornem o cenário mais complexo. Um caminho MSI de vetor único é essencialmente igual a um caminho INTx legado de vetor único; apenas a chamada de alocação e o rid mudam. Essa mudança mínima faz um bom primeiro estágio.

### O Que o Estágio 1 Produz

O Estágio 1 estende o driver do Estágio 4 do Capítulo 19 com um fallback em dois níveis: MSI primeiro, INTx legado como fallback. O handler de filtro é o mesmo filtro do Capítulo 19. O taskqueue é o mesmo. Os sysctls são os mesmos. O que muda é o caminho de alocação: `myfirst_intr_setup` primeiro verifica `pci_msi_count(9)` e, se for diferente de zero, chama `pci_alloc_msi(9)` para um vetor. Se isso tiver êxito, o recurso IRQ está em `rid = 1`; se falhar, o driver passa para `rid = 0` para o INTx legado.

O driver também registra o modo de interrupção em uma única linha de `dmesg` para que o operador saiba imediatamente qual nível o driver acabou usando. Este é um recurso de observabilidade pequeno, mas importante, que todo driver FreeBSD real implementa; o Capítulo 20 segue a convenção.

### A Consulta à Contagem MSI

O primeiro passo é perguntar ao dispositivo quantos vetores MSI ele anuncia:

```c
int msi_count = pci_msi_count(sc->dev);
```

O valor de retorno é 0 se o dispositivo não possui capacidade MSI; caso contrário, é o número de vetores que o dispositivo anuncia em seu registrador de controle de capacidade MSI. Valores típicos são 1, 2, 4, 8, 16 ou 32 (o MSI exige uma contagem em potência de dois, até 32).

Um retorno de 0 não significa que o dispositivo não possui interrupções; significa que o dispositivo não expõe MSI. O driver deve passar para o próximo nível.

### A Chamada de Alocação MSI

O segundo passo é pedir ao kernel que aloque os vetores:

```c
int count = 1;
int error = pci_alloc_msi(sc->dev, &count);
```

O `count` é um parâmetro de entrada e saída. Na entrada, ele representa o número de vetores que o driver deseja. Na saída, ele representa o número que o kernel de fato alocou. O kernel pode alocar menos do que o solicitado; um driver que precise de pelo menos uma quantidade específica de vetores deve verificar o valor retornado.

No Estágio 1 do Capítulo 20, o driver solicita um vetor. Se o kernel retornar 1, o driver prossegue. Se o kernel retornar 0 (raro, mas possível em um sistema com recursos disputados) ou retornar um erro, o driver libera qualquer alocação e recorre ao INTx legado.

Um ponto sutil: mesmo quando `pci_alloc_msi` retorna um valor diferente de zero, o driver **deve** chamar `pci_release_msi(dev)` para desfazer a alocação no momento do encerramento. Ao contrário de `bus_alloc_resource_any` / `bus_release_resource`, a família MSI utiliza uma única chamada a `pci_release_msi` que desfaz todos os vetores alocados via `pci_alloc_msi` ou `pci_alloc_msix` no dispositivo.

### Alocação de Recursos por Vetor

Com os vetores MSI alocados no nível do dispositivo, o driver deve agora alocar um recurso `SYS_RES_IRQ` para cada vetor. Para um único vetor MSI, o rid é 1:

```c
int rid = 1;  /* MSI vectors start at rid 1 */
struct resource *irq_res;

irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
if (irq_res == NULL) {
	/* Release the MSI allocation and fall back. */
	pci_release_msi(sc->dev);
	goto fallback;
}
```

Observe duas diferenças em relação à alocação legacy do Capítulo 19:

Primeiro, **o rid é 1, não 0**. Os vetores MSI são numerados a partir de 1, deixando o rid 0 para o INTx legacy. Se o driver usasse ambos (o que não deve acontecer), os rids não se sobreporiam.

Segundo, **`RF_SHAREABLE` não está definido**. Os vetores MSI são por função; eles não são compartilhados com outros drivers. A flag `RF_SHAREABLE` é relevante apenas para INTx legacy. Defini-la em uma alocação de recurso MSI não causa problemas, mas não tem significado.

### O Handler de Filter em um Vetor MSI

A função de handler de filter é idêntica à do Capítulo 19:

```c
int myfirst_intr_filter(void *arg);
```

O kernel chama o filter quando o vetor dispara, exatamente como chamava o filter do Capítulo 19 quando a linha legacy era ativada. O filter lê `INTR_STATUS`, confirma a interrupção, enfileira a task para `DATA_AV` e retorna `FILTER_HANDLED` (ou `FILTER_STRAY` se nenhum bit estiver definido). Nada no corpo do filter precisa ser alterado.

`bus_setup_intr(9)` é chamado de forma idêntica:

```c
error = bus_setup_intr(sc->dev, irq_res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    myfirst_intr_filter, NULL, sc,
    &sc->intr_cookie);
```

A assinatura da função, as flags, o argumento (`sc`) e o out-cookie seguem todos o padrão do Capítulo 19.

Uma pequena melhoria: `bus_describe_intr(9)` pode agora rotular o vetor com um nome específico do modo:

```c
bus_describe_intr(sc->dev, irq_res, sc->intr_cookie, "msi");
```

Após isso, `vmstat -i` exibe o handler como `irq<N>: myfirst0:msi` (para algum N escolhido pelo kernel). O operador vê imediatamente que o driver está usando MSI.

### Construindo o Fallback

Reunindo tudo, o `myfirst_intr_setup` do Stage 1 torna-se uma escada de fallback com dois níveis: tentar MSI primeiro e recorrer ao INTx legacy. O código:

```c
int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error, msi_count, count;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	/*
	 * Tier 1: attempt MSI.
	 */
	msi_count = pci_msi_count(sc->dev);
	if (msi_count > 0) {
		count = 1;
		if (pci_alloc_msi(sc->dev, &count) == 0 && count == 1) {
			sc->irq_rid = 1;
			sc->irq_res = bus_alloc_resource_any(sc->dev,
			    SYS_RES_IRQ, &sc->irq_rid, RF_ACTIVE);
			if (sc->irq_res != NULL) {
				error = bus_setup_intr(sc->dev, sc->irq_res,
				    INTR_TYPE_MISC | INTR_MPSAFE,
				    myfirst_intr_filter, NULL, sc,
				    &sc->intr_cookie);
				if (error == 0) {
					bus_describe_intr(sc->dev,
					    sc->irq_res, sc->intr_cookie,
					    "msi");
					sc->intr_mode = MYFIRST_INTR_MSI;
					device_printf(sc->dev,
					    "interrupt mode: MSI, 1 vector\n");
					goto enabled;
				}
				bus_release_resource(sc->dev,
				    SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
				sc->irq_res = NULL;
			}
			pci_release_msi(sc->dev);
		}
	}

	/*
	 * Tier 2: fall back to legacy INTx.
	 */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate legacy IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		sc->irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (rid=0)\n");

enabled:
	/* Enable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

O código tem três blocos distintos:

1. O bloco de tentativa MSI (linhas dentro do guarda `if (msi_count > 0)`).
2. O bloco de fallback legacy.
3. O bloco de habilitação em `enabled:` que é executado independentemente do nível.

A tentativa MSI executa a sequência completa: consulta de contagem, alocação, alocação do recurso IRQ no rid 1, registro do handler. Se qualquer etapa falhar, o código libera o que foi bem-sucedido (o recurso, se alocado; o MSI, se alocado com êxito) e prossegue.

O fallback legacy é essencialmente a configuração do Capítulo 19, sem alterações.

O bloco `enabled:` escreve `INTR_MASK` no dispositivo. Seja qual for o modo obtido, MSI ou legacy, a máscara do lado do dispositivo é a mesma.

Essa estrutura de fallback é o que os drivers reais fazem. Um leitor que examine o código de configuração de `virtio_pci.c` verá o mesmo padrão em maior escala: várias tentativas com fallbacks sucessivos.

### O Campo intr_mode e o Resumo do dmesg

O softc ganha um novo campo:

```c
enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

struct myfirst_softc {
	/* ... existing fields ... */
	enum myfirst_intr_mode intr_mode;
};
```

O campo registra qual nível o driver acabou utilizando. O `device_printf` no momento do attach o exibe:

```text
myfirst0: interrupt mode: MSI, 1 vector
```

ou:

```text
myfirst0: interrupt mode: legacy INTx (rid=0)
```

Os operadores que leem o `dmesg` veem essa linha e sabem qual caminho está ativo. O leitor que está depurando seu driver também a vê; se o driver está recorrendo ao legacy quando o leitor esperava MSI, a linha sinaliza o problema imediatamente.

O campo `intr_mode` também é exposto por meio de um sysctl somente leitura para que ferramentas no espaço do usuário possam lê-lo:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_mode",
    CTLFLAG_RD, &sc->intr_mode, 0,
    "Interrupt mode: 0=legacy, 1=MSI, 2=MSI-X");
```

Um script que deseja saber se alguma instância de `myfirst` está usando MSI-X pode somar os valores de `intr_mode` em todas as unidades.

### O Que o Teardown Precisa Alterar

O caminho de teardown do Capítulo 19 chamava `bus_teardown_intr`, drenava e liberava o taskqueue e liberava o recurso IRQ. Para o Stage 1, uma chamada adicional é necessária: se o driver usou MSI, ele deve chamar `pci_release_msi` após liberar o recurso IRQ:

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release MSI if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

A chamada a `pci_release_msi` é condicional: só a faça se o driver realmente alocou MSI ou MSI-X. Chamá-la quando o driver usou apenas INTx legacy é um no-op no FreeBSD moderno, mas a condição torna o código mais claro.

Observe a ordem: liberação do recurso IRQ primeiro, depois `pci_release_msi`. Essa ordem é o inverso da ordem de alocação (em que `pci_alloc_msi` precedeu `bus_alloc_resource_any`). A regra é a regra geral de teardown dos Capítulos 18 e 19: desfaça na ordem inversa da configuração.

### Verificando o Stage 1

Em um guest onde o dispositivo suporta MSI (o `virtio-rng-pci` do QEMU suporta; o `virtio-rnd` do bhyve não suporta), o driver do Stage 1 deve fazer attach com MSI:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: BAR0 allocated: 0x20 bytes at 0xfebf1000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: MSI, 1 vector
```

Em um guest onde o dispositivo suporta apenas legacy (o `virtio-rnd` do bhyve, tipicamente):

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: legacy INTx (rid=0)
```

Ambos os casos estão corretos. O driver funciona em qualquer um dos modos; o comportamento (filter, task, contadores, sysctl de interrupção simulada) é idêntico.

`sysctl dev.myfirst.0.intr_mode` retorna 0 (legacy), 1 (MSI) ou 2 (MSI-X, quando a Seção 5 o adicionar). O script de regressão usa isso para verificar o modo esperado.

### O Que o Stage 1 Não Faz

O Stage 1 adiciona MSI com um único vetor, mas ainda não usa o potencial multi-vetor do MSI. Um único vetor MSI é funcionalmente quase idêntico a um único IRQ legacy (os benefícios de escalabilidade só aparecem em sistemas com muitos dispositivos, o que um laboratório de dispositivo único raramente demonstra). O valor do Stage 1 está em introduzir o idioma da escada de fallback e estabelecer a observabilidade do `intr_mode`; o Stage 2 e além usam essas bases para adicionar o tratamento multi-vetor.

### Erros Comuns Nesta Etapa

Uma lista breve.

**Usar rid = 0 para MSI.** O rid do vetor MSI é 1, não 0. Solicitar `rid = 0` em um dispositivo que alocou vetores MSI retorna o recurso INTx legacy, que não é o vetor MSI. O driver acaba com um handler na linha errada. Correção: `rid = 1` para o primeiro vetor MSI ou MSI-X.

**Esquecer `pci_release_msi` no teardown.** O estado de alocação MSI do kernel sobrevive ao `bus_release_resource` no recurso IRQ. Sem `pci_release_msi`, a próxima tentativa de attach falhará porque o kernel ainda acredita que o driver é dono dos vetores MSI. Correção: sempre chame `pci_release_msi` no teardown quando MSI ou MSI-X foi utilizado.

**Esquecer o fallback de INTx.** Um driver que tenta apenas MSI e retorna um erro em caso de falha funciona em sistemas que suportam MSI, mas falha em sistemas mais antigos. Correção: sempre forneça um fallback de INTx legacy.

**Esquecer de restaurar `sc->intr_mode` no teardown.** O campo `intr_mode` registra o nível. Sem a restauração, um reattach futuro poderia ler um valor obsoleto. Não é um bug grave (o attach sempre o define), mas a limpeza do código importa. Correção: redefina para `LEGACY` (ou um valor neutro) no teardown.

**Discrepância na contagem.** `pci_alloc_msi` pode alocar menos vetores do que o solicitado; se o driver assume `count == 1` quando é 0, o código desreferencia um recurso não alocado. Correção: sempre verifique a contagem retornada.

**Chamar `pci_alloc_msi` duas vezes sem liberar.** Apenas uma alocação de MSI (ou MSI-X) pode estar ativa por dispositivo de cada vez. Tentar uma segunda alocação sem liberar a primeira retorna um erro. Correção: se o driver quiser alterar sua alocação (de MSI para MSI-X, por exemplo), chame `pci_release_msi` primeiro.

### Ponto de Verificação: Stage 1 Funcionando

Antes da Seção 3, confirme que o Stage 1 está em funcionamento:

- `kldstat -v | grep myfirst` exibe a versão `1.3-msi-stage1`.
- `dmesg | grep myfirst` exibe o banner de attach com uma linha `interrupt mode:` indicando MSI ou legacy.
- `sysctl dev.myfirst.0.intr_mode` retorna 0 ou 1.
- `vmstat -i | grep myfirst` exibe o handler com `myfirst0:msi` ou `myfirst0:legacy` como descritor.
- `sudo sysctl dev.myfirst.0.intr_simulate=1` ainda aciona o pipeline do Capítulo 19.
- `kldunload myfirst` é executado sem problemas; sem vazamentos.

Se o caminho MSI falhar no seu guest, tente QEMU em vez de bhyve. Se o caminho MSI funcionar em um e não no outro, verifique se a capacidade MSI do dispositivo está exposta por meio de `pciconf -lvc`.

### Encerrando a Seção 2

Habilitar MSI em um driver exige três novas chamadas (`pci_msi_count`, `pci_alloc_msi`, `pci_release_msi`), uma alteração na alocação do recurso IRQ (rid = 1 em vez de 0) e um novo campo no softc (`intr_mode`). A escada de fallback adiciona um segundo nível: tentar MSI e recorrer ao legacy. Cada `bus_setup_intr`, cada filter, cada task do taskqueue e cada etapa de teardown do Capítulo 19 permanecem inalterados.

O Stage 1 trata de um único vetor MSI. A Seção 3 avança para o multi-vetor: várias funções de filter distintas, vários estados de softc por vetor e o início do padrão de handler por fila que os drivers modernos usam amplamente.



## Seção 3: Gerenciando Múltiplos Vetores de Interrupção

O Stage 1 adicionou MSI com um vetor. A Seção 3 estende o driver para tratar múltiplos vetores, cada um com sua própria função. O exemplo motivador é o dispositivo que tem mais de uma coisa a comunicar: uma NIC com uma fila de recepção e uma fila de transmissão, um controlador NVMe com filas admin e de I/O, um UART com eventos de recepção pronta e transmissão vazia.

O driver do Capítulo 20 não tem um dispositivo real com múltiplas filas; o alvo virtio-rnd tem no máximo uma classe de evento por interrupção. Para fins didáticos, simulamos o comportamento multi-vetor da mesma forma que o Capítulo 19 simulou interrupções: a interface sysctl permite ao leitor disparar interrupções simuladas em vetores específicos, e o mecanismo de filter e task do driver demonstra como os drivers reais tratam casos multi-vetor.

Ao final da Seção 3, o driver estará na versão `1.3-msi-stage2` e terá três vetores MSI-X: um vetor admin, um vetor "rx" e um vetor "tx". Cada vetor tem sua própria função de filter, sua própria task adiada e seus próprios contadores. O filter lê `INTR_STATUS` e confirma apenas os bits relevantes para seu vetor; a task executa o trabalho específico do vetor.

Uma observação importante sobre a contagem de três vetores. MSI está limitado a uma contagem de vetores que seja potência de dois (1, 2, 4, 8, 16 ou 32), portanto uma solicitação de exatamente 3 vetores é rejeitada por `pci_alloc_msi(9)` com `EINVAL` (consulte `pci_alloc_msi_method` em `/usr/src/sys/dev/pci/pci.c`). MSI-X não tem essa restrição e aloca 3 vetores sem dificuldade. O nível MSI da escada de fallback, portanto, solicita um único vetor MSI e recorre ao padrão de handler único do Capítulo 19; apenas o nível MSI-X dá ao driver seus três filters por vetor. A Seção 5 torna isso explícito e o refator da Seção 8 mantém o nível MSI simples.

### O Design por Vetor

O design tem três vetores:

- **Vetor admin (vetor 0, rid 1).** Trata eventos `ERROR` e de mudança de configuração. Taxa baixa; executado raramente.
- **Vetor RX (vetor 1, rid 2).** Trata eventos `DATA_AV` (recepção pronta). Executado na taxa do caminho de dados.
- **Vetor TX (vetor 2, rid 3).** Trata eventos `COMPLETE` (transmissão concluída). Executado na taxa do caminho de dados.

Cada vetor tem:

- Um `struct resource *` separado (o recurso IRQ para aquele vetor).
- Um `void *intr_cookie` separado (o identificador opaco do kernel para o handler).
- Uma função de filter separada (`myfirst_admin_filter`, `myfirst_rx_filter`, `myfirst_tx_filter`).
- Um conjunto separado de contadores (para que execuções concorrentes do filter em diferentes CPUs não disputem um único contador compartilhado).
- Um nome `bus_describe_intr` separado (`admin`, `rx`, `tx`).
- Uma task adiada separada (para RX; os vetores admin e TX tratam seu trabalho de forma inline).

O estado por vetor reside em um array de estruturas por vetor dentro do softc:

```c
#define MYFIRST_MAX_VECTORS 3

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX,
	MYFIRST_VECTOR_TX,
};

struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

struct myfirst_softc {
	/* ... existing fields ... */
	struct myfirst_vector	vectors[MYFIRST_MAX_VECTORS];
	int			num_vectors;   /* actually allocated */
};
```

Algumas notas de design que merecem ser detalhadas.

**Ponteiro de retorno `struct myfirst_softc *sc` por vetor.** O argumento que cada filter recebe via `bus_setup_intr` é a estrutura por vetor (`struct myfirst_vector *`), não o softc global. A estrutura por vetor contém um ponteiro de retorno para o softc de modo que o filter possa acessar o estado compartilhado quando necessário. Esse é o padrão que `nvme(4)` usa para vetores por fila e o padrão que todo driver multi-fila segue.

**Contadores por vetor.** Cada vetor tem seu próprio `fire_count` e `stray_count`. Dois filters executando em duas CPUs podem incrementar seus próprios contadores sem contenção em operações atômicas; as operações atômicas ainda são usadas, mas cada uma acessa uma linha de cache diferente.

**Ponteiro de filtro por vetor.** O campo `filter` armazena um ponteiro para a função de filtro do vetor. Isso não é estritamente necessário (poderíamos ter um switch em um único filtro genérico), mas torna explícita a especialização por vetor: o filtro de cada vetor é conhecido estaticamente.

**Tarefa por vetor.** Nem todo vetor precisa de uma tarefa. Admin e TX fazem seu trabalho inline (incrementam um contador, atualizam um flag, talvez acordem um waiter). RX adia o trabalho para uma tarefa porque precisa fazer broadcast em uma variável de condição, o que requer contexto de thread. O flag `has_task` torna explícita a diferença por vetor.

### As Funções de Filtro

Três funções de filtro distintas, uma por vetor:

```c
int
myfirst_admin_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & (MYFIRST_INTR_ERROR)) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_ERROR);
	atomic_add_64(&sc->intr_error_count, 1);
	return (FILTER_HANDLED);
}

int
myfirst_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_DATA_AV) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_DATA_AV);
	atomic_add_64(&sc->intr_data_av_count, 1);
	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}

int
myfirst_tx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_COMPLETE) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);
	atomic_add_64(&sc->intr_complete_count, 1);
	return (FILTER_HANDLED);
}
```

Cada filtro tem o mesmo formato: lê o status, verifica o bit que aquele vetor se preocupa, confirma o recebimento, atualiza os contadores, opcionalmente enfileira uma tarefa e retorna. As diferenças são qual bit cada filtro verifica e quais contadores cada um incrementa.

Alguns detalhes merecem atenção.

**A verificação de stray é por vetor.** Cada filtro verifica seu próprio bit, não qualquer bit. Se o filtro for chamado para um evento que ele não trata (porque o conjunto de bits é para um vetor diferente), o filtro retorna `FILTER_STRAY`. Isso importa menos no MSI-X (onde cada vetor tem sua própria mensagem dedicada, portanto o dispositivo nunca dispara o vetor "errado"), mas importa mais no MSI com múltiplos vetores compartilhando uma única capacidade.

**Compartilhamento de contadores.** Os contadores por vetor (`vec->fire_count`, `vec->stray_count`) são específicos do vetor. Os contadores globais (`sc->intr_data_av_count`, etc.) são compartilhados e ainda são usados para a observabilidade por bit do capítulo. Ter ambos dá ao leitor uma forma de verificação cruzada: o fire count do filtro RX deve ser aproximadamente igual ao `data_av_count` global.

**O filtro não dorme.** Todas as regras de contexto de filtro do Capítulo 19 se aplicam aqui: nenhum sleep lock, nenhum `malloc(M_WAITOK)`, nenhum bloqueio. O filtro usa apenas operações atômicas e acessos diretos à BAR.

### A Tarefa por Vetor

Apenas o RX tem uma tarefa; admin e TX tratam seu trabalho no filtro. A tarefa RX é essencialmente a tarefa do Capítulo 19:

```c
static void
myfirst_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

A tarefa é executada em contexto de thread na taskqueue compartilhada `intr_tq` (o Capítulo 19 a criou com prioridade `PI_NET`). A mesma taskqueue serve a todas as tarefas por vetor; para um driver com trabalho verdadeiramente independente por fila, cada vetor poderia ter sua própria taskqueue, mas o Capítulo 20 usa uma única.

### Alocando Múltiplos Vetores

O código de configuração do Estágio 2 é mais extenso que o do Estágio 1 porque lida com múltiplos vetores:

```c
int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error, wanted, allocated, i;

	TASK_INIT(&sc->vectors[MYFIRST_VECTOR_RX].task, 0,
	    myfirst_rx_task_fn, &sc->vectors[MYFIRST_VECTOR_RX]);
	sc->vectors[MYFIRST_VECTOR_RX].has_task = true;
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_admin_filter;
	sc->vectors[MYFIRST_VECTOR_RX].filter = myfirst_rx_filter;
	sc->vectors[MYFIRST_VECTOR_TX].filter = myfirst_tx_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].name = "admin";
	sc->vectors[MYFIRST_VECTOR_RX].name = "rx";
	sc->vectors[MYFIRST_VECTOR_TX].name = "tx";
	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		sc->vectors[i].id = i;
		sc->vectors[i].sc = sc;
	}

	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	/*
	 * Try to allocate a single MSI vector. MSI requires a power-of-two
	 * count (PCI specification and /usr/src/sys/dev/pci/pci.c's
	 * pci_alloc_msi_method enforces this), so we cannot request the
	 * MYFIRST_MAX_VECTORS = 3 we want; we ask for 1 and fall back to
	 * the Chapter 19 single-handler pattern at rid=1, the same way
	 * sys/dev/virtio/pci/virtio_pci.c's vtpci_alloc_msi() does.
	 *
	 * MSI-X, covered in Section 5, is the tier where we actually
	 * obtain three distinct vectors; MSI-X is not constrained to
	 * power-of-two counts.
	 */
	allocated = 1;
	if (pci_msi_count(sc->dev) >= 1 &&
	    pci_alloc_msi(sc->dev, &allocated) == 0 && allocated >= 1) {
		sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
		sc->vectors[MYFIRST_VECTOR_ADMIN].name = "msi";
		error = myfirst_intr_setup_vector(sc, MYFIRST_VECTOR_ADMIN, 1);
		if (error == 0) {
			sc->intr_mode = MYFIRST_INTR_MSI;
			sc->num_vectors = 1;
			device_printf(sc->dev,
			    "interrupt mode: MSI, 1 vector "
			    "(single-handler fallback)\n");
			goto enabled;
		}
		pci_release_msi(sc->dev);
	}

	/*
	 * MSI allocation failed or was unavailable. Fall back to legacy
	 * INTx with a single vector-0 handler that handles every event
	 * class in one place.
	 */

fallback_legacy:
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid = 0;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = bus_alloc_resource_any(
	    sc->dev, SYS_RES_IRQ,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate legacy IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res);
		sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	sc->num_vectors = 1;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (1 handler for all events)\n");

enabled:
	/* Enable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

O código tem três fases: tentativa MSI, limpeza de fallback MSI e fallback para legado. A tentativa MSI percorre os vetores em loop, chamando um helper (`myfirst_intr_setup_vector`) para alocar e registrar cada um. Em caso de falha em qualquer vetor, o código desfaz em ordem reversa e cai para o legado.

O helper:

```c
static int
myfirst_intr_setup_vector(struct myfirst_softc *sc, int idx, int rid)
{
	struct myfirst_vector *vec = &sc->vectors[idx];
	int error;

	vec->irq_rid = rid;
	vec->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &vec->irq_rid, RF_ACTIVE);
	if (vec->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, vec->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    vec->filter, NULL, vec, &vec->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
	    "%s", vec->name);
	return (0);
}
```

O helper é pequeno e simétrico: aloca o recurso, configura o handler, descreve-o. O argumento para `bus_setup_intr` é a estrutura por vetor (`vec`), não o softc. O filtro recebe `vec` como seu `void *arg` e usa `vec->sc` quando precisa do softc.

O helper de teardown por vetor:

```c
static void
myfirst_intr_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

O teardown é o inverso da configuração: desfaz o handler, libera o recurso.

### O Caminho Completo de Teardown

O teardown de múltiplos vetores chama o helper por vetor para cada vetor ativo e depois libera a alocação MSI uma única vez:

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Per-vector teardown. */
	for (i = 0; i < sc->num_vectors; i++)
		myfirst_intr_teardown_vector(sc, i);

	/* Drain tasks. */
	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release MSI if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

A ordem é a já conhecida: máscara no dispositivo, desmontagem dos handlers, drenagem das tarefas, liberação do MSI. O loop por vetor faz o trabalho por vetor.

### Simulando Interrupções por Vetor

O sysctl de interrupção simulada do Capítulo 19 dispara um handler por vez. O Estágio 2 estende o conceito: um sysctl por vetor, ou um único sysctl com um campo de índice de vetor. O código do capítulo usa a forma mais simples de um sysctl por vetor:

```c
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_admin",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    &sc->vectors[MYFIRST_VECTOR_ADMIN], 0,
    myfirst_intr_simulate_vector_sysctl, "IU",
    "Simulate admin vector interrupt");
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_rx", ...);
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_tx", ...);
```

O handler:

```c
static int
myfirst_intr_simulate_vector_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_vector *vec = arg1;
	struct myfirst_softc *sc = vec->sc;
	uint32_t mask = 0;
	int error;

	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || sc->bar_res == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	/*
	 * Invoke this vector's filter if it has one (MSI-X). On single-
	 * handler tiers (MSI with 1 vector, or legacy INTx) only slot 0
	 * has a registered filter, so we fall through to it. The Chapter 19
	 * myfirst_intr_filter handles all three status bits in one pass.
	 */
	if (vec->filter != NULL)
		(void)vec->filter(vec);
	else if (sc->vectors[MYFIRST_VECTOR_ADMIN].filter != NULL)
		(void)sc->vectors[MYFIRST_VECTOR_ADMIN].filter(
		    &sc->vectors[MYFIRST_VECTOR_ADMIN]);
	return (0);
}
```

Do espaço do usuário:

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2  # ERROR bit, admin vector
sudo sysctl dev.myfirst.0.intr_simulate_rx=1     # DATA_AV bit, rx vector
sudo sysctl dev.myfirst.0.intr_simulate_tx=4     # COMPLETE bit, tx vector
```

Os contadores `intr_count` por vetor incrementam de forma independente. Um leitor pode verificar o comportamento por vetor disparando cada sysctl e observando o contador `vec->fire_count` correspondente aumentar.

### O Que Acontece no Fallback para Legado

Quando o driver recorre ao legado INTx (porque o MSI estava indisponível ou falhou), há apenas um handler para cobrir todas as três classes de eventos. O código atribui o `myfirst_intr_filter` do Capítulo 19 ao slot do vetor admin e usa esse único filtro em `rid = 0`. O filtro do vetor admin torna-se um handler multi-evento que verifica os três bits de status e despacha de acordo.

Esse é um detalhe pequeno, mas importante: o filtro do Capítulo 19 ainda existe e é reutilizado no caminho legado, enquanto os filtros por vetor são usados apenas quando MSI ou MSI-X está disponível. Um leitor que inspeciona o driver vê ambos, e a diferença é explicada nos comentários da Seção 3.

### O Banner dmesg do Estágio 2

Em um convidado onde o driver chega ao nível MSI-X (este é o único nível que entrega três vetores; o nível MSI recorre a uma configuração de handler único pelas razões explicadas anteriormente):

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xfebf1000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: MSI-X, 3 vectors
```

`vmstat -i | grep myfirst` mostra três linhas separadas:

```text
irq256: myfirst0:admin                 12         1
irq257: myfirst0:rx                    98         8
irq258: myfirst0:tx                    45         4
```

(Os números exatos de IRQ variam por plataforma; IRQs alocados por MSI em x86 começam na faixa de 256 quando o intervalo do I/O APIC se esgota.)

Em um convidado onde apenas MSI está disponível, o driver relata um fallback de handler único:

```text
myfirst0: interrupt mode: MSI, 1 vector (single-handler fallback)
```

e `vmstat -i` mostra uma linha, porque o driver usa o padrão do Capítulo 19 naquele único vetor MSI.

A divisão por vetor (três linhas) é o que torna o driver de múltiplos vetores observável. Um operador monitorando os contadores pode dizer qual vetor está ativo e com que frequência.

### Erros Comuns no Estágio 2

Uma lista curta.

**Passar o softc (e não o vetor) como argumento do filtro.** Se você passar `sc` em vez de `vec`, o filtro não consegue saber qual vetor está atendendo. Correção: passe `vec` para `bus_setup_intr`; o filtro acessa `sc` através de `vec->sc`.

**Esquecer de inicializar `vec->sc`.** As estruturas por vetor são inicializadas com zero por `myfirst_init_softc`; `vec->sc` permanece NULL a não ser que seja explicitamente atribuído. Sem ele, o acesso `vec->sc->mtx` no filtro é uma desreferência de ponteiro nulo. Correção: defina `vec->sc = sc` durante a configuração, antes que qualquer handler seja registrado.

**Usar o mesmo rid para múltiplos vetores.** Os rids MSI são 1, 2, 3, ...; reutilizar o rid 1 tanto para o vetor admin quanto para o RX significa que apenas um handler é realmente registrado. Correção: atribua rids em sequência por vetor.

**Handler por vetor que toca estado compartilhado sem locking.** Dois filtros rodando em diferentes CPUs tentam escrever em um único `sc->counter`. Sem operações atômicas ou um spin lock, o incremento perde atualizações. Correção: use contadores por vetor sempre que possível, e operações atômicas para qualquer contador compartilhado.

**Tarefa por vetor armazenada no local errado.** Se a tarefa estiver no softc em vez da estrutura de vetor, dois vetores que enfileiram a "mesma" tarefa entram em conflito. Correção: armazene a tarefa na estrutura de vetor e passe o vetor como argumento da tarefa.

**Teardown por vetor ausente em caso de falha parcial na configuração.** A cascata de goto deve desfazer exatamente os vetores que tiveram sucesso. A falta de limpeza deixa recursos de IRQ alocados. Correção: use o helper de teardown por vetor e itere de trás para frente no caso de falha parcial.

### Encerrando a Seção 3

Gerenciar múltiplos vetores de interrupção é um conjunto de três novos padrões: estado por vetor (em um array de `struct myfirst_vector`), funções de filtro por vetor e nomes por vetor em `bus_describe_intr`. Cada vetor tem seu próprio recurso de IRQ em seu próprio rid, sua própria função de filtro que lê apenas os bits de status relevantes para seu vetor, seus próprios contadores e (opcionalmente) sua própria tarefa diferida. A única chamada de alocação MSI ou MSI-X trata do estado no lado do dispositivo; as chamadas individuais de `bus_alloc_resource_any` e `bus_setup_intr` por vetor tratam de cada handler individualmente.

A escada de fallback da Seção 2 se estende naturalmente: tente primeiro MSI com N vetores; em caso de falha parcial, libere e tente legado INTx com um único handler. O helper de teardown por vetor torna o desfazimento em caso de falha parcial e o teardown limpo simétricos.

A Seção 4 é a seção de locking e estruturas de dados. Ela examina o que acontece quando múltiplos filtros rodam em múltiplas CPUs ao mesmo tempo e qual disciplina de sincronização mantém o estado compartilhado íntegro.



## Seção 4: Projetando Estruturas de Dados Seguras para Interrupções

A Seção 3 adicionou múltiplos vetores, cada um com seu próprio handler. A Seção 4 examina as consequências: múltiplos handlers podem rodar de forma concorrente em múltiplas CPUs, e qualquer dado que eles compartilhem deve ser protegido de forma adequada. A disciplina não é nova; é a especialização multi-CPU do modelo de locking do Capítulo 11. O que é novo é que o driver do Capítulo 20 tem três (ou mais) caminhos concorrentes em contexto de filtro em vez de um.

A Seção 4 é a seção em que múltiplos vetores mudam a forma do estado do driver, não apenas a contagem de seus handlers.

### O Novo Panorama de Concorrência

O driver do Capítulo 19 tinha um filtro e uma tarefa. O filtro rodava em qualquer CPU para qual o kernel roteasse a interrupção; a tarefa rodava na thread trabalhadora da taskqueue. Os dois poderiam em princípio rodar simultaneamente: o filtro na CPU 0 e a tarefa na CPU 3, por exemplo. Os contadores atômicos e o `sc->mtx` (mantido pela tarefa, não pelo filtro) forneciam a sincronização necessária.

O driver de múltiplos vetores do Capítulo 20 tem três filtros e uma tarefa. Em um sistema MSI-X, cada filtro tem seu próprio `intr_event`, de modo que cada um pode disparar em uma CPU diferente de forma independente. Uma rajada de três interrupções chegando em um microssegundo pode fazer com que três filtros rodem em três CPUs ao mesmo tempo. A tarefa única ainda é serializada pela taskqueue, mas os filtros não são.

Os dados que os filtros tocam se enquadram em três categorias:

1. **Estado por vetor.** Os próprios contadores de cada vetor, seu próprio cookie, seu próprio recurso. Sem compartilhamento entre vetores. Nenhuma sincronização necessária.
2. **Contadores compartilhados.** Contadores atualizados por qualquer filtro (o `intr_data_av_count` global, `intr_error_count`, etc.). Devem ser atômicos.
3. **Estado compartilhado do dispositivo.** A própria BAR, o ponteiro `sc->hw` do softc, `sc->pci_attached`, os campos protegidos por mutex. As regras de acesso dependem do contexto.

A disciplina é manter o estado por vetor verdadeiramente por vetor, usar atômicos para contadores compartilhados e obedecer às regras de locking do Capítulo 11 para qualquer coisa que exija um sleep mutex.

### Estado por Vetor: O Padrão Padrão

A sincronização mais fácil é nenhuma sincronização. Se um pedaço de estado é tocado apenas pelo filtro de um único vetor (e por mais nada), nenhum lock é necessário. Esse é o caso para:

- `vec->fire_count`: incrementado apenas pelo filtro deste vetor, lido por handlers de sysctl via o caminho de leitura do sysctl. Um add atômico é suficiente; nenhum lock entre filtro e sysctl porque o sysctl lê atomicamente.
- `vec->stray_count`: mesmo padrão.
- `vec->intr_cookie`: escrito uma vez na configuração, lido no teardown. Único escritor, acesso ordenado.
- `vec->irq_res`: mesmo padrão.

A maior parte do estado por vetor se enquadra nessa categoria. O array `struct myfirst_vector` no softc é o padrão-chave: o estado de cada vetor vive em seu próprio slot, tocado apenas pelo seu próprio filtro.

### Contadores Compartilhados: Operações Atômicas

Os contadores globais por bit (o Capítulo 19 introduziu `sc->intr_data_av_count`, etc.) são atualizados pelo filtro do vetor correspondente. Apenas um filtro atualiza cada contador, portanto tecnicamente eles são por vetor, exceto pelo nome. Mas o leitor pode imaginar um cenário em que um padrão de bit aparece em `INTR_STATUS` exigindo que tanto o vetor RX quanto o admin incrementem contadores compartilhados. A abordagem mais segura: torne cada atualização atômica.

O Capítulo 20 usa `atomic_add_64` ao longo dos caminhos de filtro:

```c
atomic_add_64(&sc->intr_data_av_count, 1);
```

Isso é barato (uma instrução com lock em x86, uma barreira mais add em arm64), e permite que o filtro rode em qualquer CPU sem se preocupar com atualizações perdidas.

O custo de `atomic_add_64` em um contador muito compartilhado é o bouncing de linha de cache: cada incremento de uma CPU diferente invalida a linha de cache nas outras CPUs. Para um contador incrementado um milhão de vezes por segundo por várias CPUs, isso é um impacto de desempenho mensurável. A mitigação é tornar os contadores verdadeiramente por CPU (usando `counter(9)` ou `DPCPU_DEFINE`) e somá-los apenas quando lidos; o driver do Capítulo 20 não está nessa escala, portanto atomics simples são suficientes.

### Estado Compartilhado do Dispositivo: A Disciplina de Mutex

`sc->hw`, `sc->pci_attached`, `sc->bar_res`: esses campos são definidos durante o attach e liberados durante o detach. No estado estável, eles são somente leitura. Os filtros os acessam sem um lock porque a disciplina de tempo de vida (attach antes de habilitar, desabilitar antes do detach) garante que os ponteiros são válidos sempre que o filtro puder ser executado.

A regra: um filtro que acessa `sc->hw` ou `sc->bar_res` sem um lock precisa ter certeza de que a ordenação attach-detach garante a validade do ponteiro. A Seção 7 do Capítulo 20 percorre essa ordenação em detalhes. Para os propósitos da Seção 4, confie na disciplina: quando o filtro é executado, o dispositivo está attached e os ponteiros são válidos.

### O Lock Por Vetor: Quando Você Precisa Dele

Às vezes o estado por vetor é mais rico do que um contador. Um vetor que lê de uma fila de recepção e atualiza uma estrutura de dados por fila (um anel de mbufs, por exemplo) precisa de um spin lock para proteger o anel de dois disparos simultâneos do mesmo vetor. Espere, o mesmo vetor pode disparar duas vezes simultaneamente em um sistema MSI-X?

No MSI-X, o kernel garante que cada `intr_event` entrega para uma CPU de cada vez; um único vetor não entra em si mesmo recursivamente. Dois vetores diferentes podem rodar em duas CPUs ao mesmo tempo, mas o vetor N não pode rodar na CPU 3 e na CPU 5 simultaneamente.

Isso significa: **o estado por vetor não precisa de um lock por vetor** para acesso concorrente a partir do mesmo vetor. Pode ser necessário um lock para a comunicação entre o filtro e a task (a task roda em uma CPU diferente, possivelmente de forma concorrente com o filtro), mas um spin lock é suficiente para isso, e a comunicação geralmente ocorre por meio de operações atômicas de qualquer forma.

Um spin lock se torna útil quando:

- Um driver usa uma única função de filtro para múltiplos vetores, e o kernel pode despachar dois filtros de vetores diferentes de forma concorrente. (O Stage 2 do Capítulo 20 tem filtros separados por vetor, portanto isso não se aplica.)
- Um driver compartilha um anel de recepção entre o filtro (que o preenche) e uma task (que o esvazia). Um spin lock protege o índice do anel; o filtro adquire o spin lock, insere no anel e libera. A task adquire, esvazia e libera.

O driver do Capítulo 20 não usa spin locks no filtro; os contadores por vetor são atômicos e o estado compartilhado é tratado pelo `sc->mtx` existente na task. Drivers reais podem precisar de spin locks em cenários mais ricos.

### Dados Por CPU: A Opção Avançada

Para drivers de altíssima taxa, até mesmo contadores atômicos sobre dados compartilhados se tornam um gargalo. A solução são dados por CPU: cada CPU tem sua própria cópia do contador, o filtro incrementa a cópia da sua própria CPU (sem tráfego entre CPUs), e o leitor de sysctl soma os valores de todas as CPUs.

A API `counter(9)` do FreeBSD fornece isso: um `counter_u64_t` é um handle para um array por CPU, `counter_u64_add(c, 1)` incrementa o slot da CPU atual, e `counter_u64_fetch(c)` soma os slots de todas as CPUs na leitura. A implementação usa regiões de dados por CPU (`DPCPU_DEFINE` internamente) e é tão barata quanto um incremento não-atômico normal no caminho crítico.

O driver do Capítulo 20 não usa `counter(9)`; atomics simples são suficientes para a escala da demonstração. Drivers reais de alto throughput (NICs de dez gigabits, controladores NVMe a um milhão de IOPS) usam `counter(9)` extensivamente. Um leitor que estiver escrevendo tal driver deve estudar `counter(9)` após o Capítulo 20.

### Ordenação de Locks e Complicações com Múltiplos Vetores

O Capítulo 15 estabeleceu a ordem de locks do driver: `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`. O filtro do Capítulo 19 não tomava nenhum lock (apenas atomics); a task tomava `sc->mtx`. Os filtros por vetor do Capítulo 20 ainda não tomam locks (apenas atomics), portanto o caminho do filtro não contribui com novas arestas na ordem de locks. As tasks por vetor ainda tomam `sc->mtx`, assim como a task única do Capítulo 19.

A ordenação de locks com múltiplas tasks rodando de forma concorrente requer uma pequena extensão. Quando a admin task e a RX task adquirem `sc->mtx` ao mesmo tempo, elas se serializam no mutex. Isso é correto, desde que cada task libere o mutex rapidamente; se a admin task mantivesse `sc->mtx` enquanto aguardasse algo lento, a RX task ficaria parada. A regra do Capítulo 15 "sem mutexes mantidos por muito tempo" se aplica aqui também.

O WITNESS captura a maioria dos problemas de ordenação de locks. Para o Capítulo 20, a história de ordenação de locks é essencialmente a mesma do Capítulo 19, pois os caminhos dos filtros são livres de locks (apenas atomics) e os caminhos das tasks adquirem o mesmo `sc->mtx` único.

### O Modelo de Memória: Por Que Atomics Importam

Um ponto sutil que vale a pena tornar explícito. Em um sistema com múltiplas CPUs, escritas de uma CPU não ficam visíveis para outras CPUs instantaneamente. Uma escrita na CPU 0 em `sc->intr_count++` (sem atomics) pode ficar presa no store buffer da CPU 0 e levar nanossegundos ou microssegundos para se propagar até a visão da CPU 3 da mesma memória. Nessa janela, a CPU 3 poderia ler o valor anterior à escrita.

`atomic_add_64` inclui uma barreira de memória que força a escrita a se tornar globalmente visível antes de a instrução retornar. É isso que torna o valor do contador "consistente" entre CPUs: qualquer leitura após o incremento enxerga o novo valor.

Para o estado de contadores, esse nível de consistência é suficiente. O valor absoluto do contador a qualquer instante não é importante; o que importa é que o valor cresça de forma monotônica e chegue ao total correto. `atomic_add_64` garante ambos.

Para estado compartilhado mais rico (por exemplo, um índice de estrutura de dados compartilhada que múltiplos filtros atualizam), o modelo de memória fica mais sutil. O driver precisaria de um spin lock, que fornece tanto exclusão mútua quanto uma barreira de memória. O driver do Capítulo 20 não precisa desse nível de maquinário; a disciplina atômica do Capítulo 19 se mantém.

### Observabilidade: Contadores Por Vetor no sysctl

Cada vetor recebe sua própria subárvore de sysctl para que o operador possa consultá-la:

```c
char name[32];
for (int i = 0; i < MYFIRST_MAX_VECTORS; i++) {
	snprintf(name, sizeof(name), "vec%d_fire_count", i);
	SYSCTL_ADD_U64(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, name,
	    CTLFLAG_RD, &sc->vectors[i].fire_count, 0,
	    "Fire count for this vector");
}
```

A partir do espaço do usuário:

```sh
sysctl dev.myfirst.0 | grep vec
```

```text
dev.myfirst.0.vec0_fire_count: 42    # admin
dev.myfirst.0.vec0_stray_count: 0
dev.myfirst.0.vec1_fire_count: 9876  # rx
dev.myfirst.0.vec1_stray_count: 0
dev.myfirst.0.vec2_fire_count: 4523  # tx
dev.myfirst.0.vec2_stray_count: 0
```

O operador pode ver de imediato quais vetores estão disparando e em que taxas. As contagens de stray devem permanecer em zero no MSI-X (cada vetor tem sua própria mensagem dedicada), mas podem aumentar no MSI ou legacy quando um filtro compartilhado recebe um evento de um vetor diferente.

### Encerrando a Seção 4

Drivers com múltiplos vetores mudam o panorama de concorrência: vários filtros podem rodar em várias CPUs simultaneamente. A disciplina é projetar o estado por vetor sempre que possível, usar atomics para contadores compartilhados e respeitar a ordem de locks do Capítulo 11 para qualquer coisa que precise de um sleep mutex. Contadores por CPU (`counter(9)`) estão disponíveis para drivers de altíssima taxa, mas são excessivos para o Capítulo 20.

A ordem de locks do driver não ganha novas arestas porque o caminho do filtro permanece livre de locks (apenas atomics) e as tasks todas tomam `sc->mtx`. O WITNESS ainda captura problemas de ordenação de locks; a disciplina atômica ainda captura o restante.

A Seção 5 avança para o mecanismo mais capaz: MSI-X. A API é muito similar (`pci_msix_count` + `pci_alloc_msix` em vez do par MSI), mas as opções de escalabilidade e afinidade de CPU são mais ricas.



## Seção 5: Usando MSI-X para Alta Flexibilidade

A Seção 2 introduziu MSI com um único vetor, a Seção 3 estendeu para múltiplos vetores MSI, a Seção 4 percorreu as implicações de concorrência. A Seção 5 passa para MSI-X: o mecanismo mais completo que os dispositivos PCIe modernos usam quando têm mais do que um punhado de interrupções para gerenciar. A API é paralela à do MSI, portanto a mudança no código é pequena; a mudança conceitual é que o MSI-X permite que o driver vincule cada vetor a uma CPU específica, por meio de `bus_bind_intr(9)` e `bus_get_cpus(9)`, e isso importa para o desempenho real.

### A API de Contagem e Alocação do MSI-X

A API espelha a do MSI:

```c
int msix_count = pci_msix_count(sc->dev);
```

`pci_msix_count(9)` retorna o número de vetores MSI-X que o dispositivo anuncia (0 se não houver capacidade MSI-X). A contagem vem do campo `Table Size` da capacidade MSI-X mais um; um dispositivo com `Table Size = 7` anuncia 8 vetores.

A alocação é similar:

```c
int count = desired;
int error = pci_alloc_msix(sc->dev, &count);
```

Mesmo parâmetro `count` de entrada-saída, mesma semântica: o kernel pode alocar menos do que o solicitado. Ao contrário do MSI, o MSI-X permite uma contagem que não seja potência de dois; portanto, se o driver solicitar 3 vetores, o kernel pode fornecer 3.

A mesma chamada `pci_release_msi(9)` libera os vetores MSI-X; não existe um `pci_release_msix` separado. O nome da função é um artefato histórico; ela trata tanto MSI quanto MSI-X.

### A Escada de Fallback Estendida

A escada de fallback completa do driver do Capítulo 20 é:

1. **MSI-X** com a contagem de vetores desejada.
2. **MSI** com a contagem de vetores desejada, se MSI-X não estiver disponível ou se a alocação falhar.
3. **Legacy INTx** com um único handler para tudo, se tanto MSI-X quanto MSI falharem.

A estrutura do código é paralela à escada de dois níveis da Seção 3, estendida com um terceiro nível no topo:

```c
/* Tier 0: MSI-X. */
wanted = MYFIRST_MAX_VECTORS;
if (pci_msix_count(sc->dev) >= wanted) {
	allocated = wanted;
	if (pci_alloc_msix(sc->dev, &allocated) == 0 &&
	    allocated == wanted) {
		for (i = 0; i < wanted; i++) {
			error = myfirst_intr_setup_vector(sc, i, i + 1);
			if (error != 0)
				goto fail_msix;
		}
		sc->intr_mode = MYFIRST_INTR_MSIX;
		sc->num_vectors = wanted;
		device_printf(sc->dev,
		    "interrupt mode: MSI-X, %d vectors\n", wanted);
		myfirst_intr_bind_vectors(sc);
		goto enabled;
	}
	if (allocated > 0)
		pci_release_msi(sc->dev);
}

/* Tier 1: MSI. */
/* ... Section 3 MSI code ... */

fail_msix:
for (i -= 1; i >= 0; i--)
	myfirst_intr_teardown_vector(sc, i);
pci_release_msi(sc->dev);
/* fallthrough to MSI attempt, then legacy. */
```

A estrutura é direta: cada nível tem o mesmo padrão (consultar contagem, alocar, configurar vetores, marcar o modo, descrever). O código segue a bem estabelecida cascata.

### Vinculação de Vetores com bus_bind_intr

Uma vez que o MSI-X é alocado, o driver tem a opção de vincular cada vetor a uma CPU específica. A API é:

```c
int bus_bind_intr(device_t dev, struct resource *r, int cpu);
```

O `cpu` é um inteiro com o ID da CPU, de 0 a `mp_ncpus - 1`. Em caso de sucesso, a interrupção é roteada para aquela CPU. Em caso de falha, a função retorna um errno; o driver trata isso como uma dica não-fatal e continua sem a vinculação.

Para o driver de três vetores do Capítulo 20, uma vinculação razoável é:

- **Vetor Admin**: CPU 0 (trabalho de controle, qualquer CPU serve).
- **Vetor RX**: CPU 1 (benefício de localidade de cache para uma fila RX real).
- **Vetor TX**: CPU 2 (benefício de localidade similar).

Em um sistema com duas CPUs, as vinculações seriam comprimidas; em um sistema com muitas CPUs, o driver deve usar `bus_get_cpus(9)` para consultar quais CPUs são locais ao nó NUMA do dispositivo e distribuir os vetores de acordo.

O helper de vinculação:

```c
static void
myfirst_intr_bind_vectors(struct myfirst_softc *sc)
{
	int i, cpu, ncpus;
	int err;

	if (mp_ncpus < 2)
		return;  /* nothing to bind */

	ncpus = mp_ncpus;
	for (i = 0; i < sc->num_vectors; i++) {
		cpu = i % ncpus;
		err = bus_bind_intr(sc->dev, sc->vectors[i].irq_res, cpu);
		if (err != 0) {
			device_printf(sc->dev,
			    "bus_bind_intr vector %d to CPU %d: %d\n",
			    i, cpu, err);
		}
	}
}
```

O código faz uma vinculação em round-robin: vetor 0 para a CPU 0, vetor 1 para a CPU 1, e assim por diante, com wrap modulo a contagem de CPUs. Em um sistema com duas CPUs e três vetores, o vetor 0 e o vetor 2 vão para a CPU 0; em um sistema com quatro CPUs, cada vetor fica com sua própria CPU.

Um driver mais sofisticado usa `bus_get_cpus(9)`:

```c
cpuset_t local_cpus;
int ncpus_local;

if (bus_get_cpus(sc->dev, LOCAL_CPUS, sizeof(local_cpus),
    &local_cpus) == 0) {
	/* Use only CPUs in local_cpus for binding. */
	ncpus_local = CPU_COUNT(&local_cpus);
	/* ... pick from local_cpus ... */
}
```

O argumento `LOCAL_CPUS` retorna as CPUs que estão no mesmo domínio NUMA que o dispositivo. O argumento `INTR_CPUS` retorna as CPUs adequadas para tratar interrupções de dispositivos (geralmente excluindo CPUs fixadas em trabalhos críticos). Um driver que se preocupa com o desempenho NUMA usa esses argumentos para posicionar os vetores em CPUs locais ao NUMA.

O driver do Capítulo 20 não usa `bus_get_cpus(9)` por padrão; a vinculação round-robin mais simples é suficiente para o laboratório. Um exercício desafio adiciona a vinculação com consciência de NUMA.

### O Resumo dmesg do MSI-X

O driver do Capítulo 20 imprime uma linha como:

```text
myfirst0: interrupt mode: MSI-X, 3 vectors
```

Com as vinculações de CPU por vetor visíveis em `vmstat -i` (os totais por CPU no vmstat -i não são por vetor; são agregados) e na saída de `cpuset -g -x <irq>` (uma consulta por vetor):

```sh
for irq in 256 257 258; do
    echo "IRQ $irq:"
    cpuset -g -x $irq
done
```

Saída típica:

```text
IRQ 256:
irq 256 mask: 0
IRQ 257:
irq 257 mask: 1
IRQ 258:
irq 258 mask: 2
```

(Os números de IRQ dependem da atribuição da plataforma.)

Um operador inspecionando a configuração de interrupções do driver pode ver quais vetores disparam e onde.

### bus_describe_intr Por Vetor

Cada vetor MSI-X deve ter uma descrição. O código da Seção 3 já as define por meio de `bus_describe_intr(9)`:

```c
bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
    "%s", vec->name);
```

Após isso, `vmstat -i` mostra cada vetor com seu papel:

```text
irq256: myfirst0:admin                 42         4
irq257: myfirst0:rx                 12345      1234
irq258: myfirst0:tx                  5432       543
```

O operador vê qual vetor é o admin, qual é o RX, qual é o TX e qual é o nível de atividade de cada um. Essa é uma observabilidade essencial para um driver com múltiplos vetores.

### Considerações sobre a Tabela MSI-X e as BARs

Um detalhe que vale mencionar, embora o driver não interaja com ele diretamente. A estrutura de capacidade MSI-X aponta para uma **tabela** e um **pending bit array** (PBA), cada um residindo em uma das BARs do dispositivo. A BAR que contém cada um é descobrível por meio de `pci_msix_table_bar(9)` e `pci_msix_pba_bar(9)`:

```c
int table_bar = pci_msix_table_bar(sc->dev);
int pba_bar = pci_msix_pba_bar(sc->dev);
```

Cada um retorna o índice da BAR (0 a 5) ou -1 se o dispositivo não tiver capacidade MSI-X. Para a maioria dos dispositivos, a tabela e o PBA estão na BAR 0 ou na BAR 1; para alguns dispositivos, eles compartilham uma BAR com os registradores mapeados em memória (a BAR 0 do driver).

O kernel cuida da programação da tabela internamente. A única interação do driver é:

- Garantir que a BAR que contém a tabela esteja alocada (para que o kernel possa acessá-la). Em alguns dispositivos, isso exige que o driver aloque BARs adicionais.
- Chamar `pci_alloc_msix` e deixar o kernel fazer o restante.

Para o driver do Capítulo 20, o alvo virtio-rnd (ou seu equivalente QEMU com MSI-X) tem a tabela na BAR 1 ou em uma região dedicada. O código do Capítulo 18 alocou a BAR 0; o kernel trata a BAR da tabela MSI-X implicitamente por meio da infraestrutura de alocação.

Um driver que deseja inspecionar a BAR da tabela:

```c
device_printf(sc->dev, "MSI-X table in BAR %d, PBA in BAR %d\n",
    pci_msix_table_bar(sc->dev), pci_msix_pba_bar(sc->dev));
```

Isso é útil para fins de diagnóstico.

### Alocando Menos Vetores do Que o Solicitado

Um caso sutil: o dispositivo anuncia 3 vetores MSI-X e o driver solicita 3, mas o kernel aloca apenas 2. O que o driver faz?

A resposta depende do design do driver. As opções são:

1. **Falhar no attach.** Se o driver não consegue funcionar com menos vetores, retorne um erro. Isso é raro em drivers flexíveis, mas possível em drivers com requisitos rígidos de hardware.
2. **Usar o que foi alocado.** Se o driver consegue funcionar com 2 vetores (combinando RX e TX em um único, por exemplo), use os 2 e ajuste a configuração. Isso é comum em drivers que precisam suportar uma variedade de hardware.
3. **Liberar e recorrer a outra alternativa.** Se 2 vetores MSI-X for pior do que 1 vetor MSI por algum motivo, libere o MSI-X e tente MSI. Isso é incomum.

O driver do Capítulo 20 segue a opção 1: se não obtiver exatamente `MYFIRST_MAX_VECTORS` (3) vetores, ele libera o MSI-X e recorre ao MSI. Um driver mais sofisticado usaria a opção 2; o Capítulo 20 foca no padrão mais simples por razões didáticas.

Drivers reais do FreeBSD frequentemente usam a opção 2 com uma função auxiliar que determina como distribuir os vetores alocados entre os papéis desejados. O driver `nvme(4)` é um exemplo: se ele solicita vetores suficientes para N filas de I/O e recebe menos, reduz o número de filas de I/O proporcionalmente.

### Testando MSI-X no bhyve vs QEMU

Um detalhe prático sobre o laboratório. O dispositivo legado virtio-rnd do bhyve (utilizado nos Capítulos 18 e 19) não expõe MSI-X; ele é um transporte virtio exclusivamente legado. Para exercitar MSI-X em um guest, você precisa de uma das seguintes opções:

- **QEMU com `-device virtio-rng-pci`** (não `-device virtio-rng`, que é legado). O virtio-rng-pci moderno expõe MSI-X.
- **Emulação moderna do bhyve** de um dispositivo não-virtio-rnd que tenha MSI-X. O Capítulo 20 não utiliza esse caminho.
- **Hardware real** que suporte MSI-X (a maioria dos dispositivos PCIe modernos).

O QEMU é a escolha prática para os laboratórios do Capítulo 20. A escada de fallback do driver garante que ele ainda funcione no bhyve (recorrendo ao legado); testar MSI-X especificamente requer QEMU ou hardware real.

### Erros Comuns na Configuração de MSI-X

Uma lista breve.

**Usar `pci_release_msix`.** Essa função não existe no FreeBSD; a liberação é tratada por `pci_release_msi(9)`, que funciona tanto para MSI quanto para MSI-X. Correção: use `pci_release_msi`.

**Vincular a uma CPU que o dispositivo não consegue alcançar.** Algumas plataformas (raramente) possuem CPUs que não fazem parte do conjunto roteável do controlador de interrupções. A chamada `bus_bind_intr` retorna um erro; ignore-o e continue. Correção: registre o erro, mas não faça o attach falhar.

**Esperar que `vmstat -i` mostre a distribuição por CPU.** O `vmstat -i` agrega contagens por evento. A distribuição por CPU está disponível via `cpuset -g -x <irq>` (ou `sysctl hw.intrcnt` na forma bruta). O operador precisa saber onde olhar. Correção: documente o caminho de observabilidade do seu driver.

**Não verificar `allocated` em relação a `wanted`.** Aceitar uma alocação parcial quando o driver não consegue lidar com ela leva a bugs sutis (vetores que deveriam disparar nunca disparam). Correção: decida a estratégia antecipadamente (falhar, adaptar ou liberar) e implemente de acordo.

### Encerrando a Seção 5

O MSI-X é o mecanismo mais completo: uma tabela endereçável por vetor que o kernel programa em nome do driver, com afinidade de CPU por vetor e mascaramento por vetor disponíveis para os drivers que precisam deles. A API espelha de perto a do MSI (`pci_msix_count` + `pci_alloc_msix` + `pci_release_msi`), e a alocação de recursos por vetor é a mesma do código MSI da Seção 3. O elemento novo é o `bus_bind_intr(9)` para afinidade de CPU e o `bus_get_cpus(9)` para consultas de CPUs locais ao NUMA.

Para o driver do Capítulo 20, o MSI-X é o nível preferido; a escada de fallback tenta MSI-X primeiro, recorre ao MSI e, por fim, ao INTx legado. Os handlers, contadores e tasks por vetor da Seção 3 funcionam sem alteração no MSI-X; apenas a chamada de alocação muda.

A Seção 6 é onde os eventos específicos por vetor se tornam explícitos. Cada vetor tem seu próprio propósito, sua própria lógica de filtro e seu próprio comportamento observável. O Estágio 3 do Capítulo 20 é o estágio em que o driver começa a se parecer com um dispositivo multi-fila real, mesmo que o hardware subjacente (o alvo virtio-rnd) seja mais simples.



## Seção 6: Tratando Eventos Específicos por Vetor

As Seções 2 a 5 construíram a infraestrutura para o tratamento multi-vetor. A Seção 6 é onde os papéis por vetor se tornam explícitos. Cada vetor trata uma classe específica de eventos; cada filtro executa uma verificação específica; cada task realiza um wake-up específico. O driver no Estágio 3 trata os vetores como entidades nomeadas e com propósito definido, e não como slots intercambiáveis.

Os três vetores do driver do Capítulo 20 têm responsabilidades distintas:

- **Vetor admin** trata eventos `ERROR`. O filtro lê o status, confirma o recebimento e, em caso de erros reais, registra uma mensagem. O trabalho de administração é infrequente, mas não pode ser descartado.
- **Vetor RX** trata eventos `DATA_AV` (dado disponível para recepção). O filtro confirma o recebimento e adia o trabalho de tratamento de dados para uma task por vetor que faz broadcast de uma variável de condição.
- **Vetor TX** trata eventos `COMPLETE` (transmissão concluída). O filtro confirma o recebimento e, opcionalmente, acorda uma thread aguardando a conclusão da transmissão. O filtro realiza o registro contábil inline.

Cada vetor pode ser testado de forma independente via sysctl de interrupção simulada, observado de forma independente por seus contadores e vinculado de forma independente a uma CPU. O driver começa a se parecer com um dispositivo multi-fila real de pequena escala.

### O Vetor Admin

O vetor admin trata eventos raros, porém importantes: mudanças de configuração, erros, mudanças de estado de link (para uma NIC) e alertas de temperatura (para um sensor). Seu trabalho costuma ser pequeno: registrar o evento, atualizar um flag de estado, acordar um waiter no espaço do usuário que faz polling do estado.

Para o driver do Capítulo 20, o vetor admin trata o bit `ERROR` do Capítulo 17. O filtro:

```c
int
myfirst_admin_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_ERROR) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_ERROR);
	atomic_add_64(&sc->intr_error_count, 1);
	return (FILTER_HANDLED);
}
```

Em um dispositivo real, o filtro admin também poderia examinar um registrador secundário (um registrador de código de erro, por exemplo) e decidir com base na gravidade se deve agendar uma task de recuperação. O driver do Capítulo 20 mantém a simplicidade: contar e confirmar o recebimento.

### O Vetor RX

O vetor RX é o vetor do caminho de dados. Para uma NIC, ele trataria os pacotes recebidos. Para um drive NVMe, as conclusões de requisições de leitura. Para o driver do Capítulo 20, ele trata o bit `DATA_AV` do Capítulo 17.

O filtro é pequeno (confirmar o recebimento e enfileirar uma task); a task realiza o trabalho de fato. A Seção 3 mostrou ambos. A task:

```c
static void
myfirst_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

Em um driver real, a task percorreria um anel de descritores de recepção, construiria mbufs e os passaria para a pilha de rede. Para a demonstração do Capítulo 20, ela lê `DATA_OUT`, armazena o valor, faz broadcast da variável de condição e permite que qualquer leitor cdev em espera acorde.

O argumento `npending` é o número de vezes que a task foi enfileirada desde sua última execução. Para um caminho RX de alta taxa, uma task que executou uma vez e encontrou `npending = 5` sabe que está atrasada (5 interrupções coalescidas em 1 execução da task) e pode dimensionar seu lote de acordo. A task do Capítulo 20 ignora `npending`; drivers reais o utilizam para batching.

### O Vetor TX

O vetor TX é o vetor de conclusão de transmissão. Para uma NIC, ele sinaliza que um pacote que o driver entregou ao hardware foi transmitido e o buffer pode ser recuperado. Para um drive NVMe, ele sinaliza que uma requisição de escrita foi concluída.

Para o driver do Capítulo 20, ele trata o bit `COMPLETE` do Capítulo 17. O filtro realiza o trabalho inline (nenhuma task é necessária):

```c
int
myfirst_tx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_COMPLETE) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);
	atomic_add_64(&sc->intr_complete_count, 1);
	return (FILTER_HANDLED);
}
```

O design exclusivamente inline do filtro TX é uma escolha deliberada. Em um caminho de conclusão TX real, o filtro poderia registrar a contagem de conclusões e a task percorreria o anel de descritores TX para recuperar buffers. Para a demonstração do Capítulo 20, a conclusão é simplesmente contada.

Um design alternativo faria o TX também utilizar uma task. Se isso vale a pena depende do trabalho que a task realizaria: se for substancial (percorrer um anel, recuperar dezenas de buffers), uma task compensa; se for trivial (um único decremento de um contador in-flight), inline no filtro é suficiente. O Capítulo 20 opta por inline para o TX a fim de ilustrar que nem todo vetor precisa de uma task.

### Mapeamento de Vetor para Evento

No MSI-X, cada vetor é independente; disparar o vetor 1 entrega ao filtro RX, disparar o vetor 2 entrega ao filtro TX. O mapeamento de vetor para evento é parte do design do driver, não uma escolha do kernel.

No MSI com múltiplos vetores, o kernel pode, em princípio, despachar múltiplos vetores em rápida sucessão se múltiplos eventos dispararem simultaneamente. Os filtros do driver devem, cada um, ler o registrador de status e reivindicar apenas os bits pertencentes ao seu vetor.

No INTx legado, há apenas um vetor e um filtro. O filtro trata as três classes de eventos em uma única passagem.

O código do Capítulo 20 trata os três casos: o filtro por vetor no MSI-X lê apenas seu próprio bit, o filtro por vetor no MSI faz o mesmo (com a mesma lógica de verificação de bits), e o filtro único no INTx legado trata os três bits.

### Eventos Simulados por Vetor

O sysctl de simulação da Seção 3 permite que você exercite cada vetor de forma independente. No espaço do usuário:

```sh
# Simulate an admin interrupt (ERROR).
sudo sysctl dev.myfirst.0.intr_simulate_admin=2

# Simulate an RX interrupt (DATA_AV).
sudo sysctl dev.myfirst.0.intr_simulate_rx=1

# Simulate a TX interrupt (COMPLETE).
sudo sysctl dev.myfirst.0.intr_simulate_tx=4
```

Cada sysctl escreve seu bit específico em `INTR_STATUS` e invoca o filtro do vetor correspondente. No nível MSI-X, os três filtros existem, então cada sysctl atinge seu próprio filtro por vetor e seu contador por vetor é incrementado. No nível MSI (vetor único no slot 0) e no nível INTx legado (vetor único no slot 0), os slots 1 e 2 não têm filtro registrado, então o auxiliar de simulação roteia a chamada pelo filtro do slot 0. O `myfirst_intr_filter` do Capítulo 19 trata os três bits inline, então os contadores globais `intr_count`, `intr_data_av_count`, `intr_error_count` e `intr_complete_count` ainda se movem corretamente. Os contadores por vetor nos slots 1 e 2 permanecem em zero nos níveis de handler único, o que é o sinal correto de observabilidade de que o driver não está operando com três vetores.

Você pode observar o pipeline no espaço do usuário:

```sh
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate_rx=1
    sleep 0.1
done &
watch sysctl dev.myfirst.0 | grep -E "vec|intr_"
```

Os contadores incrementam a aproximadamente 10 por segundo. O contador do vetor RX corresponde a `intr_data_av_count`; a contagem de invocações da task também corresponde.

### Atribuição Dinâmica de Vetores

Um ponto sutil, mas importante. O design do driver tem três vetores em um array fixo com papéis fixos. Um driver mais flexível poderia descobrir o número de vetores disponíveis em tempo de execução e atribuir papéis dinamicamente. O padrão tem a seguinte forma:

```c
/* Discover how many vectors we got. */
int nvec = actually_allocated_msix_vectors(sc);

/* Assign roles based on nvec. */
if (nvec >= 3) {
	/* Full design: admin, rx, tx. */
	sc->vectors[0].filter = myfirst_admin_filter;
	sc->vectors[1].filter = myfirst_rx_filter;
	sc->vectors[2].filter = myfirst_tx_filter;
	sc->num_vectors = 3;
} else if (nvec == 2) {
	/* Compact: admin+tx share one vector, rx has its own. */
	sc->vectors[0].filter = myfirst_admin_tx_filter;
	sc->vectors[1].filter = myfirst_rx_filter;
	sc->num_vectors = 2;
} else if (nvec == 1) {
	/* Minimal: one filter handles everything. */
	sc->vectors[0].filter = myfirst_intr_filter;
	sc->num_vectors = 1;
}
```

Essa adaptação dinâmica é o que os drivers de produção fazem. O driver do Capítulo 20 usa a abordagem fixa mais simples; um exercício desafio adiciona a variante dinâmica.

### Um Padrão do `nvme(4)`

Como exemplo real, o driver `nvme(4)` trata a fila admin separadamente das filas de I/O. Suas funções de filtro diferem por tipo de fila; seus contadores de interrupção são rastreados por fila. O padrão é:

```c
/* In nvme_ctrlr_construct_admin_qpair: */
qpair->intr_idx = 0;  /* vector 0 for admin */
qpair->intr_rid = 1;
qpair->res = bus_alloc_resource_any(ctrlr->dev, SYS_RES_IRQ,
    &qpair->intr_rid, RF_ACTIVE);
bus_setup_intr(ctrlr->dev, qpair->res, INTR_TYPE_MISC | INTR_MPSAFE,
    NULL, nvme_qpair_msix_handler, qpair, &qpair->tag);

/* For each I/O queue: */
for (i = 0; i < ctrlr->num_io_queues; i++) {
	ctrlr->ioq[i].intr_rid = i + 2;  /* I/O vectors at rid 2, 3, ... */
	/* ... similar bus_alloc_resource_any + bus_setup_intr ... */
}
```

Cada fila tem seu próprio `intr_rid`, seu próprio recurso, sua própria tag (cookie) e seu próprio argumento de handler. A fila admin usa um vetor; cada fila de I/O usa seu próprio vetor. O padrão escala linearmente com o número de filas.

O driver do Capítulo 20 é uma versão reduzida disso: três vetores fixos em vez de um admin mais N de I/O. A história de escalabilidade se transfere diretamente.

### Observabilidade: Taxa por Vetor

Um diagnóstico útil: calcular a taxa de cada vetor em uma janela deslizante:

```sh
#!/bin/sh
prev_admin=$(sysctl -n dev.myfirst.0.vec0_fire_count)
prev_rx=$(sysctl -n dev.myfirst.0.vec1_fire_count)
prev_tx=$(sysctl -n dev.myfirst.0.vec2_fire_count)
sleep 1
curr_admin=$(sysctl -n dev.myfirst.0.vec0_fire_count)
curr_rx=$(sysctl -n dev.myfirst.0.vec1_fire_count)
curr_tx=$(sysctl -n dev.myfirst.0.vec2_fire_count)

echo "admin: $((curr_admin - prev_admin)) /s"
echo "rx:    $((curr_rx    - prev_rx   )) /s"
echo "tx:    $((curr_tx    - prev_tx   )) /s"
```

A saída são as taxas por vetor no último segundo. Ao executar o sysctl de interrupção simulada em um loop, você pode ver as taxas aumentando; ao observar uma carga de trabalho real, vê qual vetor está ocupado.

### Encerrando a Seção 6

Tratar eventos específicos por vetor significa que cada vetor tem sua própria função de filtro, seus próprios contadores, sua própria task (opcional) e seu próprio comportamento observável. O padrão escala: três vetores para a demonstração do Capítulo 20, dezenas para uma NIC de produção, centenas para um controlador NVMe. A separação por vetor torna cada parte pequena, específica e manutenível.

A Seção 7 é a seção de desmontagem. Drivers multi-vetor precisam desmontar cada vetor individualmente, na ordem correta, e então chamar `pci_release_msi` uma vez ao final. A ordem é estrita, mas não complexa; a Seção 7 percorre cada passo.



## Seção 7: Desmontagem e Limpeza com MSI/MSI-X

A desmontagem do Capítulo 19 era um único par: `bus_teardown_intr` no vetor único e, em seguida, `bus_release_resource` no único recurso de IRQ. A desmontagem do Capítulo 20 é o mesmo par repetido por vetor, seguido de uma única chamada a `pci_release_msi` que desfaz a alocação no nível do dispositivo para MSI ou MSI-X.

A Seção 7 torna a ordenação precisa, percorre os casos de falha parcial e destaca as verificações de observabilidade que confirmam uma desmontagem limpa.

### A Ordem Obrigatória

Para um driver multi-vetor, a sequência de detach é:

1. **Recuse se ocupado.** Igual ao Capítulo 19: retorne `EBUSY` se o driver tiver descritores abertos ou trabalho em andamento.
2. **Marque o dispositivo como desanexado.**
3. **Destrua o cdev.**
4. **Desabilite as interrupções no dispositivo.** Limpe `INTR_MASK` para que o dispositivo pare de acionar o sinal de interrupção.
5. **Para cada vetor, em ordem inversa:**
   a. Chame `bus_teardown_intr` com o cookie do vetor.
   b. Chame `bus_release_resource` no recurso IRQ do vetor.
6. **Drene todas as tasks por vetor.** Cada task que tenha sido inicializada.
7. **Destrua o taskqueue.**
8. **Chame `pci_release_msi`** uma vez, incondicionalmente se `intr_mode` for MSI ou MSI-X.
9. **Desanexe a camada de hardware e libere o BAR** normalmente.
10. **Desinicialize o softc.**

Os passos novos são o 5 (laço por vetor em vez de um par único) e o 8 (`pci_release_msi`). Os passos 1 a 4 e 9 a 10 não sofreram alterações em relação ao Capítulo 19.

### Por Que Ordem Reversa por Vetor

O loop em ordem reversa por vetor é uma medida defensiva contra dependências entre vetores. Em um driver simples como o do Capítulo 20, os vetores são independentes: desmontar o vetor 2 antes do vetor 1 não representa problema. Em um driver onde o filtro do vetor 2 lê um estado que o filtro do vetor 1 escreve, a ordem importa: desmonte primeiro o escritor (vetor 1) e depois o leitor (vetor 2).

Para a correção do driver do Capítulo 20, tanto a ordem direta quanto a reversa são seguras. Para robustez diante de mudanças futuras, prefere-se a ordem reversa.

### O Código de Desmontagem por Vetor

Da Seção 3, o helper de desmontagem por vetor:

```c
static void
myfirst_intr_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

O helper é robusto contra configurações parciais: se o vetor nunca teve um cookie (a configuração falhou antes de `bus_setup_intr`), a verificação `if` pula a chamada de desmontagem. Se o recurso nunca foi alocado, o segundo `if` pula a liberação. O mesmo helper funciona tanto para o desfazimento de falha parcial durante a configuração quanto para a desmontagem completa durante o detach.

### A Desmontagem Completa

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down each vector's handler, in reverse. */
	for (i = sc->num_vectors - 1; i >= 0; i--)
		myfirst_intr_teardown_vector(sc, i);

	/* Drain and destroy the taskqueue, including per-vector tasks. */
	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the MSI/MSI-X allocation if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

A estrutura do código é direta: desabilita no dispositivo, desmontagem por vetor em ordem reversa, drenagem de tarefa por vetor, liberação do taskqueue, liberação do MSI se utilizado. O padrão se aplica diretamente a qualquer driver com múltiplos vetores.

### Desfazimento de Falha Parcial no Attach

Durante a configuração, se o vetor N falha ao registrar, o código precisa desfazer os vetores de 0 a N-1 que tiveram sucesso. O padrão:

```c
for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
	error = myfirst_intr_setup_vector(sc, i, i + 1);
	if (error != 0)
		goto fail_vectors;
}

/* Success, continue. */

fail_vectors:
	/* Undo vectors 0 through i-1. */
	for (i -= 1; i >= 0; i--)
		myfirst_intr_teardown_vector(sc, i);
	pci_release_msi(sc->dev);
	/* Fall through to next tier or final failure. */
```

O `i -= 1` é importante: após o `goto`, `i` é o vetor que falhou (ele está além das configurações bem-sucedidas). Desfazemos os vetores de 0 a i-1, que é o conjunto registrado com sucesso. O helper de desmontagem por vetor pode ser chamado com segurança no slot do vetor que falhou também, pois seus campos são NULL (a configuração não avançou o suficiente para preenchê-los).

### Observabilidade: Verificando uma Desmontagem Limpa

Após `kldunload myfirst`, o seguinte deve ser verdadeiro:

- `kldstat -v | grep myfirst` não retorna nada.
- `devinfo -v | grep myfirst` não retorna nada.
- `vmstat -i | grep myfirst` não retorna nada.
- `vmstat -m | grep myfirst` mostra zero alocações ativas.

Qualquer falha aponta para um bug de limpeza:

- Uma entrada restante em `vmstat -i` significa que `bus_teardown_intr` não foi chamado para aquele vetor.
- Um vazamento em `vmstat -m` significa que alguma tarefa por vetor não foi drenada ou que o taskqueue não foi liberado.
- Uma entrada restante em `devinfo -v` (raro) significa que o detach do dispositivo não foi concluído.

### Vazamento de Recurso MSI Entre Ciclos de Carga e Descarga

Uma preocupação específica para drivers MSI/MSI-X: esquecer `pci_release_msi` deixa o estado MSI do dispositivo alocado. O próximo `kldload` do mesmo driver (ou de um driver diferente para o mesmo dispositivo) falhará ao alocar vetores MSI porque o kernel acredita que eles já estão em uso.

O sintoma no `dmesg`:

```text
myfirst0: pci_alloc_msix returned EBUSY
```

ou similar. A correção é garantir que `pci_release_msi` seja executado em todo caminho de desmontagem, incluindo o desfazimento de falha parcial.

Um teste útil: carregue, descarregue, carregue de novo. Se o segundo carregamento tem sucesso com o mesmo modo MSI, a desmontagem está correta. Se o segundo carregamento recua para um nível inferior, a desmontagem teve vazamento.

### Erros Comuns na Desmontagem

Uma lista breve.

**Esquecer `pci_release_msi`.** O bug mais comum. Sintoma: as próximas tentativas de alocação MSI falham. Correção: sempre chamá-lo quando MSI ou MSI-X foi utilizado.

**Chamar `pci_release_msi` em um driver que usou apenas INTx legado.** Tecnicamente é uma operação nula, mas uma verificação explícita torna a intenção mais clara. Correção: verificar `intr_mode` antes de chamar.

**Ordem de desmontagem por vetor incorreta.** Para drivers com dependências entre vetores, o loop em ordem reversa é importante. Para o driver do Capítulo 20, a ordem não é crítica do ponto de vista de dependências, mas a disciplina de ordem reversa tem custo baixo e vale a pena manter.

**Drenar uma tarefa que nunca foi inicializada.** Se um vetor não tem `has_task`, drenar seu campo `task` não inicializado produz comportamento indefinido. Correção: verificar `has_task` antes de drenar.

**Vazar o taskqueue.** `taskqueue_drain` não libera o taskqueue; `taskqueue_free` faz isso. Ambos são necessários. Correção: chamar os dois.

**Desfazimento de configuração parcial que desfaz demais.** Se o vetor 2 falha e o código de desfazimento também desmonta o vetor 2 (que nunca foi configurado), seguem-se desreferências de NULL. As verificações de NULL do helper por vetor protegem contra isso, mas a lógica em cascata também deve ser cuidadosa. Correção: usar `i -= 1` para iniciar o desfazimento no vetor correto.

### Encerrando a Seção 7

A desmontagem de um driver com múltiplos vetores é feita por vetor em um loop, seguida de um único `pci_release_msi` ao final. O helper por vetor é compartilhado entre a desmontagem completa e o desfazimento de falha parcial. As verificações de observabilidade após a descarga são as mesmas usadas no Capítulo 19; qualquer vazamento aponta para um bug específico.

A Seção 8 é a seção de refatoração: separar o código de múltiplos vetores em `myfirst_msix.c`, atualizar `INTERRUPTS.md` para refletir as novas capacidades, incrementar a versão para `1.3-msi` e executar a passagem de regressão. O driver está funcionalmente completo após a Seção 7; a Seção 8 o torna manutenível.



## Seção 8: Refatorando e Versionando Seu Driver com Múltiplos Vetores

O handler de interrupção com múltiplos vetores está funcionando. A Seção 8 é a seção de organização. Ela separa o código MSI/MSI-X em seu próprio arquivo, atualiza os metadados do módulo, estende o documento `INTERRUPTS.md` com os novos detalhes de múltiplos vetores, incrementa a versão para `1.3-msi` e executa a passagem de regressão.

Este é o quarto capítulo consecutivo que encerra com uma seção de refatoração. As refatorações se acumulam: o Capítulo 16 separou a camada de hardware, o Capítulo 17 a simulação, o Capítulo 18 o attach PCI, o Capítulo 19 a interrupção legada. O Capítulo 20 adiciona a camada MSI/MSI-X. Cada responsabilidade tem seu próprio arquivo; o `myfirst.c` principal permanece aproximadamente constante em tamanho; o driver escala.

### O Layout Final dos Arquivos

Ao final do Capítulo 20:

```text
myfirst.c           - Main driver
myfirst.h           - Shared declarations
myfirst_hw.c        - Ch16 hardware access layer
myfirst_hw_pci.c    - Ch18 hardware-layer extension
myfirst_hw.h        - Register map
myfirst_sim.c       - Ch17 simulation backend
myfirst_sim.h       - Simulation interface
myfirst_pci.c       - Ch18 PCI attach
myfirst_pci.h       - PCI declarations
myfirst_intr.c      - Ch19 interrupt handler (legacy + filter+task)
myfirst_intr.h      - Ch19 interrupt interface + ICSR macros
myfirst_msix.c      - Ch20 MSI/MSI-X multi-vector layer (NEW)
myfirst_msix.h      - Ch20 multi-vector interface (NEW)
myfirst_sync.h      - Part 3 synchronisation
cbuf.c / cbuf.h     - Ch10 circular buffer
Makefile            - kmod build
HARDWARE.md, LOCKING.md, SIMULATION.md, PCI.md, INTERRUPTS.md, MSIX.md (NEW)
```

`myfirst_msix.c` e `myfirst_msix.h` são novos. `MSIX.md` é novo. O `myfirst_intr.c` do Capítulo 19 permanece; ele agora trata o fallback legado INTx enquanto `myfirst_msix.c` trata o caminho MSI e MSI-X.

### O Header myfirst_msix.h

```c
#ifndef _MYFIRST_MSIX_H_
#define _MYFIRST_MSIX_H_

#include <sys/taskqueue.h>

struct myfirst_softc;

enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX,
	MYFIRST_VECTOR_TX,
	MYFIRST_MAX_VECTORS
};

struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

int  myfirst_msix_setup(struct myfirst_softc *sc);
void myfirst_msix_teardown(struct myfirst_softc *sc);
void myfirst_msix_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_MSIX_H_ */
```

A API pública são três funções: setup, teardown, add_sysctls. Os tipos enum e a struct por vetor são exportados para que `myfirst.h` possa incluí-los e o softc possa ter o array por vetor.

### O Makefile Completo

```makefile
# Makefile for the Chapter 20 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.3-msi\"

.include <bsd.kmod.mk>
```

Um arquivo-fonte adicional na lista SRCS; a string de versão incrementada.

### A String de Versão

De `1.2-intr` para `1.3-msi`. O incremento reflete uma adição significativa de capacidade: tratamento de interrupção com múltiplos vetores. Um incremento de versão menor é apropriado; a interface visível ao usuário (o cdev) não mudou.

### O Documento MSIX.md

Um novo documento fica junto ao código-fonte:

```markdown
# MSI and MSI-X Support in the myfirst Driver

## Summary

The driver probes the device's MSI-X, MSI, and legacy INTx capabilities
in that order, and uses the first one that allocates successfully. The
driver's interrupt counters, data path, and cdev behaviour are
independent of which tier the driver ends up using.

## Setup Sequence

`myfirst_msix_setup()` tries three tiers:

1. MSI-X with MYFIRST_MAX_VECTORS (3) vectors. On success:
   - Allocates per-vector IRQ resources at rid=1, 2, 3.
   - Registers a distinct filter function per vector.
   - Calls bus_describe_intr with the per-vector name.
   - Binds each vector to a CPU (round-robin or NUMA-aware).
2. MSI with MYFIRST_MAX_VECTORS vectors. Same per-vector pattern.
3. Legacy INTx with a single handler that covers all three event
   classes at rid=0.

## Per-Vector Assignment

| Vector | Purpose | Handles                         | Inline/Deferred |
|--------|---------|---------------------------------|-----------------|
| 0      | admin   | INTR_STATUS.ERROR                | Inline          |
| 1      | rx      | INTR_STATUS.DATA_AV              | Deferred (task) |
| 2      | tx      | INTR_STATUS.COMPLETE             | Inline          |

On MSI-X, each vector has its own intr_event, its own CPU affinity
(via bus_bind_intr), and its own bus_describe_intr label ("admin",
"rx", "tx"). On MSI, the driver obtains a single vector and falls
back to the Chapter 19 single-handler pattern because MSI requires
a power-of-two vector count (pci_alloc_msi rejects count=3 with
EINVAL). On legacy INTx, a single filter covers all three bits.

## sysctls

- `dev.myfirst.N.intr_mode`: 0 (legacy), 1 (MSI), 2 (MSI-X).
- `dev.myfirst.N.vec{0,1,2}_fire_count`: per-vector fire counts.
- `dev.myfirst.N.vec{0,1,2}_stray_count`: per-vector stray counts.
- `dev.myfirst.N.intr_simulate_admin`, `.intr_simulate_rx`,
  `.intr_simulate_tx`: simulate per-vector interrupts.

## Teardown Sequence

1. Disable interrupts at the device (clear INTR_MASK).
2. Per-vector in reverse: bus_teardown_intr, bus_release_resource.
3. Drain and free per-vector tasks and the taskqueue.
4. If intr_mode is MSI or MSI-X, call pci_release_msi once.

## dmesg Summary Line

A single line on attach:

- "interrupt mode: MSI-X, 3 vectors"
- "interrupt mode: MSI, 1 vector (single-handler fallback)"
- "interrupt mode: legacy INTx (1 handler for all events)"

## Known Limitations

- MYFIRST_MAX_VECTORS is hardcoded at 3. A dynamic design that
  adapts to the allocated count is a Chapter 20 challenge exercise.
- CPU binding is round-robin. NUMA-aware binding via bus_get_cpus
  is a challenge exercise.
- DMA is Chapter 21.
- iflib integration is out of scope.

## See Also

- `INTERRUPTS.md` for the Chapter 19 legacy path details.
- `HARDWARE.md` for the register map.
- `LOCKING.md` for the full lock discipline.
- `PCI.md` for the PCI attach behaviour.
```

O documento oferece a um leitor futuro o quadro completo do design com múltiplos vetores em uma única página.

### A Passagem de Regressão

A regressão do Capítulo 20 é um superconjunto da do Capítulo 19:

1. Compilar sem erros. `make` produz `myfirst.ko` sem avisos.
2. Carregar. `kldload` exibe o banner de attach incluindo a linha `interrupt mode:`.
3. Verificar o modo. `sysctl dev.myfirst.0.intr_mode` retorna 0, 1 ou 2 (dependendo do ambiente virtual).
4. Attach por vetor. `vmstat -i | grep myfirst` mostra N linhas (1 para legado, 3 para MSI ou MSI-X).
5. Descrição por vetor. Cada entrada tem o nome correto (`admin`, `rx`, `tx` ou `legacy`).
6. Interrupções simuladas. O contador de cada vetor tica de forma independente.
7. Tarefa executada. A interrupção simulada do vetor RX aciona `intr_task_invocations`.
8. Detach limpo. `devctl detach myfirst0` desmonta todos os vetores.
9. Carga após descarga. Um segundo `kldload` usa o mesmo nível (testa que `pci_release_msi` funcionou).
10. `vmstat -m` não mostra vazamentos. Após a descarga, nenhuma alocação de myfirst permanece.

O script de regressão executa todas as dez verificações. No QEMU com virtio-rng-pci, o teste exercita o caminho MSI-X; no bhyve com virtio-rnd, exercita o fallback legado INTx. A escada de fallback do driver garante que ele funcione em ambos.

### O Que a Refatoração Conquistou

No início do Capítulo 20, o `myfirst` na versão `1.2-intr` tinha um handler de interrupção na linha legada. Ao final do Capítulo 20, o `myfirst` na versão `1.3-msi` tem uma escada de fallback em três níveis (MSI-X → MSI → legado), três filtros por vetor em MSI ou MSI-X, contadores por vetor, afinidade de CPU por vetor e um único caminho de desmontagem limpo. O número de arquivos do driver cresceu em dois; sua documentação cresceu em um; suas capacidades funcionais cresceram substancialmente.

O código é reconhecivelmente FreeBSD. Um contribuidor que abre o driver pela primeira vez encontra uma estrutura familiar: um array por vetor, funções de filtro por vetor, uma escada de configuração em três níveis, um `bus_describe_intr` para cada vetor e um único `pci_release_msi` na desmontagem. Esses padrões aparecem em todo driver FreeBSD com múltiplas filas.

### Encerrando a Seção 8

A refatoração segue a forma estabelecida: um novo arquivo para a nova camada, um novo header exportando a interface pública, um novo documento explicando o comportamento, um incremento de versão e uma passagem de regressão. A camada do Capítulo 20 é o tratamento de interrupção com múltiplos vetores; a do Capítulo 19 permanece como o fallback legado de vetor único. Juntos, formam a história completa de interrupções que o driver precisa.

O corpo instrucional do Capítulo 20 está completo. Laboratórios, desafios, solução de problemas, um encerramento e a ponte para o Capítulo 21 seguem a continuação.



## Lendo um Driver Real Juntos: virtio_pci.c

Antes dos laboratórios, uma breve leitura de um driver FreeBSD real que usa MSI-X extensivamente. `/usr/src/sys/dev/virtio/pci/virtio_pci.c` é o núcleo compartilhado de ambos os transportes virtio-PCI legado e moderno; ele contém a escada de alocação de interrupções que todo dispositivo virtio utiliza. Ler esse arquivo após o Capítulo 20 é um exercício rápido de reconhecimento de padrões; quase tudo na seção de interrupções corresponde a algo que o Capítulo 20 acabou de ensinar.

### A Escada de Alocação

`virtio_pci.c` tem um helper chamado `vtpci_alloc_intr_resources` (o nome exato varia ligeiramente conforme a versão do FreeBSD). Sua estrutura é:

```c
static int
vtpci_alloc_intr_resources(struct vtpci_common *cn)
{
	int error;

	/* Tier 0: MSI-X. */
	error = vtpci_alloc_msix(cn, nvectors);
	if (error == 0) {
		cn->vtpci_flags |= VTPCI_FLAG_MSIX;
		return (0);
	}

	/* Tier 1: MSI. */
	error = vtpci_alloc_msi(cn);
	if (error == 0) {
		cn->vtpci_flags |= VTPCI_FLAG_MSI;
		return (0);
	}

	/* Tier 2: legacy INTx. */
	return (vtpci_alloc_intx(cn));
}
```

Os três níveis são exatamente a escada do Capítulo 20. Cada nível, em caso de sucesso, define uma flag no estado comum e retorna 0. Em caso de falha, o próximo nível é tentado.

### O Helper de Alocação MSI-X

`vtpci_alloc_msix` consulta a contagem, decide quantos vetores solicitar (com base no número de virtqueues que o dispositivo usa) e chama `pci_alloc_msix`:

```c
static int
vtpci_alloc_msix(struct vtpci_common *cn, int nvectors)
{
	int error, count;

	if (pci_msix_count(cn->vtpci_dev) < nvectors)
		return (ENOSPC);

	count = nvectors;
	error = pci_alloc_msix(cn->vtpci_dev, &count);
	if (error != 0)
		return (error);
	if (count != nvectors) {
		pci_release_msi(cn->vtpci_dev);
		return (ENXIO);
	}
	return (0);
}
```

O padrão: verificar a contagem, alocar, confirmar que a alocação correspondeu ao pedido, liberar em caso de divergência. Se o dispositivo anuncia menos vetores do que o desejado, `ENOSPC` é retornado imediatamente. Se `pci_alloc_msix` aloca uma contagem menor do que a solicitada, o código libera e retorna `ENXIO`.

O código do Capítulo 20 segue exatamente essa lógica (a Seção 5 mostrou a versão completa).

### A Alocação de Recursos por Vetor

Uma vez que MSI-X é alocado, o virtio percorre os vetores e registra um handler por vetor:

```c
static int
vtpci_register_msix_vectors(struct vtpci_common *cn)
{
	int i, rid, error;

	rid = 1;  /* MSI-X vectors start at rid 1 */
	for (i = 0; i < cn->vtpci_num_vectors; i++) {
		cn->vtpci_vectors[i].res = bus_alloc_resource_any(
		    cn->vtpci_dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
		if (cn->vtpci_vectors[i].res == NULL)
			/* ... fail ... */;
		rid++;
		error = bus_setup_intr(cn->vtpci_dev,
		    cn->vtpci_vectors[i].res,
		    INTR_TYPE_MISC | INTR_MPSAFE,
		    NULL, vtpci_vq_handler,
		    &cn->vtpci_vectors[i], &cn->vtpci_vectors[i].cookie);
		if (error != 0)
			/* ... fail ... */;
	}
	return (0);
}
```

Dois aspectos coincidem com o Capítulo 20:

- `rid = 1` para o primeiro vetor, incrementando por vetor.
- O padrão de filtro (aqui `NULL`) e handler (`vtpci_vq_handler`). Note que o virtio usa um handler exclusivo de ithread (filter=NULL), não um pipeline de filtro mais tarefa. Esta é uma opção mais simples que funciona para o trabalho por vetor do virtio.

A função `vtpci_vq_handler` é o worker por vetor. Cada vetor recebe seu próprio argumento (`&cn->vtpci_vectors[i]`), e o handler usa esse argumento para identificar qual virtqueue deve ser atendida.

### A Desmontagem

A desmontagem do virtio segue o padrão do Capítulo 20:

```c
static void
vtpci_release_intr_resources(struct vtpci_common *cn)
{
	int i;

	for (i = 0; i < cn->vtpci_num_vectors; i++) {
		if (cn->vtpci_vectors[i].cookie != NULL) {
			bus_teardown_intr(cn->vtpci_dev,
			    cn->vtpci_vectors[i].res,
			    cn->vtpci_vectors[i].cookie);
		}
		if (cn->vtpci_vectors[i].res != NULL) {
			bus_release_resource(cn->vtpci_dev, SYS_RES_IRQ,
			    rman_get_rid(cn->vtpci_vectors[i].res),
			    cn->vtpci_vectors[i].res);
		}
	}

	if (cn->vtpci_flags & (VTPCI_FLAG_MSI | VTPCI_FLAG_MSIX))
		pci_release_msi(cn->vtpci_dev);
}
```

Desmontagem por vetor (`bus_teardown_intr` + `bus_release_resource`), seguida de um único `pci_release_msi` ao final. A ordem corresponde ao `myfirst_msix_teardown` do Capítulo 20.

Um detalhe que vale notar: o virtio usa `rman_get_rid` para recuperar o rid do recurso, em vez de armazená-lo separadamente. O driver do Capítulo 20 armazena o rid na struct por vetor; ambas as abordagens são válidas, mas a abordagem de armazenamento é mais clara e mais fácil de depurar.

### O Que a Leitura do Virtio Ensina

Três lições se transferem diretamente para o design do Capítulo 20:

1. **A escada de fallback de três níveis é o padrão.** Todo driver que precisa funcionar em uma variedade de hardware a implementa da mesma forma.
2. **O gerenciamento de recursos por vetor usa rids incrementais a partir de 1.** Isso é universal na infraestrutura PCI do FreeBSD.
3. **`pci_release_msi` é chamada uma única vez, independentemente do número de vetores.** O desmonte por vetor libera os recursos de IRQ; a liberação em nível de dispositivo cuida do estado MSI.

Um leitor que conseguir acompanhar `vtpci_alloc_intr_resources` do início ao fim terá internalizado o vocabulário do Capítulo 20. Para um exemplo mais rico, `/usr/src/sys/dev/nvme/nvme_ctrlr.c` mostra o mesmo padrão em maior escala, com um vetor de administração mais até `NCPU` vetores de I/O.

## Uma Visão Mais Aprofundada sobre Alocação de Vetores a CPUs

A Seção 5 apresentou `bus_bind_intr(9)` brevemente. Esta seção aprofunda o tema: por que a escolha da CPU importa, como drivers reais selecionam CPUs e quais são os trade-offs envolvidos.

### O Panorama NUMA

Em um sistema com um único soquete, todas as CPUs compartilham um único controlador de memória e uma única hierarquia de cache. A distribuição entre CPUs importa apenas para a afinidade de cache (o código e os dados do handler estarão "quentes" na CPU que o executou por último). A diferença de desempenho entre "CPU 0" e "CPU 3" é pequena.

Em um sistema NUMA com múltiplos soquetes, o panorama muda. Cada soquete possui seu próprio controlador de memória, seu próprio cache L3 e seu próprio complexo raiz PCIe. Um dispositivo PCIe conectado ao soquete 0 está no complexo raiz desse soquete; seus registradores são mapeados na memória para um intervalo de endereços gerenciado pelo controlador do soquete 0. Uma interrupção desse dispositivo dispara; o handler lê `INTR_STATUS`; a leitura vai até o BAR do dispositivo, que está no soquete 0; a CPU que executa o handler deve estar no soquete 0, caso contrário a leitura cruza a interconexão entre soquetes.

A interconexão entre soquetes (em sistemas Intel: UPI ou o antigo QPI; em AMD: Infinity Fabric) é muito mais lenta do que o acesso ao cache dentro do mesmo soquete. Um handler rodando no soquete errado vê leituras de registrador que levam dezenas de nanossegundos em vez de apenas alguns; uma fila de recepção cujos dados residem no soquete errado faz cada pacote cruzar a interconexão no caminho até o espaço do usuário.

Vetores bem posicionados mantêm o trabalho do handler no soquete onde o dispositivo está conectado.

### Consultando a Localidade NUMA

O FreeBSD expõe a topologia NUMA para os drivers por meio de `bus_get_cpus(9)`. A API:

```c
int bus_get_cpus(device_t dev, enum cpu_sets op, size_t setsize,
    struct _cpuset *cpuset);
```

O argumento `op` seleciona qual conjunto consultar:

- `LOCAL_CPUS`: CPUs no mesmo domínio NUMA que o dispositivo.
- `INTR_CPUS`: CPUs adequadas para tratar interrupções do dispositivo (geralmente `LOCAL_CPUS`, a menos que o operador tenha excluído algumas).

O parâmetro `cpuset` é de saída; em caso de sucesso, contém o bitmap das CPUs do conjunto consultado.

Exemplo de uso:

```c
cpuset_t local_cpus;
int num_local;

if (bus_get_cpus(sc->dev, INTR_CPUS, sizeof(local_cpus),
    &local_cpus) == 0) {
	num_local = CPU_COUNT(&local_cpus);
	device_printf(sc->dev, "device has %d interrupt-suitable CPUs\n",
	    num_local);
}
```

O driver usa `CPU_FFS(&local_cpus)` para encontrar a primeira CPU do conjunto, `CPU_CLR(cpu, &local_cpus)` para marcá-la como utilizada, e itera.

Um bind round-robin que respeita a localidade NUMA:

```c
static void
myfirst_msix_bind_vectors_numa(struct myfirst_softc *sc)
{
	cpuset_t local_cpus;
	int cpu, i;

	if (bus_get_cpus(sc->dev, INTR_CPUS, sizeof(local_cpus),
	    &local_cpus) != 0) {
		/* No NUMA info; round-robin across all CPUs. */
		myfirst_msix_bind_vectors_roundrobin(sc);
		return;
	}

	if (CPU_EMPTY(&local_cpus))
		return;

	for (i = 0; i < sc->num_vectors; i++) {
		if (CPU_EMPTY(&local_cpus))
			bus_get_cpus(sc->dev, INTR_CPUS,
			    sizeof(local_cpus), &local_cpus);
		cpu = CPU_FFS(&local_cpus) - 1;  /* FFS returns 1-based */
		CPU_CLR(cpu, &local_cpus);
		(void)bus_bind_intr(sc->dev,
		    sc->vectors[i].irq_res, cpu);
	}
}
```

O código obtém o conjunto de CPUs locais, seleciona a CPU de menor número, vincula o vetor 0 a ela, remove essa CPU do conjunto, seleciona a próxima de menor número, vincula o vetor 1 a ela, e assim por diante. Se o conjunto se esgotar (mais vetores do que CPUs locais), ele é recarregado e o processo continua.

O driver do Capítulo 20 não inclui esse binding com consciência de NUMA; um exercício desafio pede ao leitor que o adicione.

### A Perspectiva do Operador

Um operador pode substituir o posicionamento definido pelo kernel com `cpuset`:

```sh
# Get current placement for IRQ 257.
sudo cpuset -g -x 257

# Bind IRQ 257 to CPU 3.
sudo cpuset -l 3 -x 257

# Bind to a set of CPUs (kernel picks one when the interrupt fires).
sudo cpuset -l 2,3 -x 257
```

Esses comandos substituem qualquer coisa que o driver tenha definido com `bus_bind_intr`. Um operador pode fazer isso para fixar interrupções críticas longe das CPUs responsáveis por cargas do usuário (em aplicações de tempo real) ou para concentrar o tráfego em CPUs específicas (para fins de diagnóstico).

A chamada `bus_bind_intr` do driver define o posicionamento inicial; o operador pode sobrescrever. Um driver bem comportado define um padrão sensato e respeita as alterações do operador (o que acontece automaticamente, pois `bus_bind_intr` apenas escreve em um estado de afinidade de CPU gerenciado pelo sistema operacional, que o operador modifica em seguida).

### Medindo o Efeito

Uma forma concreta de observar o valor da localidade NUMA: execute uma carga de trabalho com alta taxa de interrupções com o handler fixado em uma CPU local e, depois, em uma CPU remota, e compare as latências. Em um sistema com dois soquetes, o handler na CPU remota tipicamente leva de 1,5 a 3 vezes mais tempo por interrupção, medido em ciclos de CPU.

O provedor DTrace do FreeBSD pode medir isso:

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_intr_filter:return /self->ts/ {
    @[cpu] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

A saída é um histograma por CPU das latências do filtro. O leitor pode executar esse script enquanto observa o posicionamento dos vetores e confirmar a diferença de latência.

### Quando o Posicionamento de Vetores Importa

- Altas taxas de interrupção (mais de alguns milhares por segundo por vetor).
- Grande footprint de linhas de cache no handler (o código e os dados do handler ocupam múltiplas linhas de cache).
- Caminhos de recepção compartilhados com processamento subsequente no mesmo soquete.
- Sistemas NUMA com mais de um soquete e dispositivos PCIe conectados a soquetes específicos.

### Quando o Posicionamento de Vetores Não Importa

- Interrupções de baixa frequência (dezenas por segundo ou menos).
- Sistemas com um único soquete.
- Handlers que fazem trabalho mínimo (o vetor admin do Capítulo 20).
- Drivers que rodam em uma única CPU independentemente (sistemas embarcados com uma única CPU).

O driver do Capítulo 20 se enquadra na categoria "não importa de verdade" para testes normais, mas os padrões que o capítulo ensina se transferem diretamente para drivers onde a questão é relevante.



## Uma Visão Mais Aprofundada sobre Estratégias de Atribuição de Vetores

A Seção 6 mostrou o padrão de atribuição fixa (vetor 0 = admin, 1 = rx, 2 = tx). Esta seção explora outras estratégias de atribuição que drivers reais utilizam.

### Um Vetor por Fila

A estratégia mais simples e mais comum. Cada fila (fila rx, fila tx, fila admin etc.) tem seu próprio vetor dedicado. O driver aloca `N+M+1` vetores para `N` filas de recepção, `M` filas de transmissão e 1 admin.

Vantagens:
- Lógica de handler simples por vetor.
- A taxa de interrupções de cada fila é independente.
- A afinidade de CPU é por fila (fácil de fixar na CPU local ao NUMA).

Desvantagens:
- Consome muitos vetores para drivers com muitas filas.
- A ithread de cada fila adiciona overhead em filas de baixa frequência.

Este é o padrão que `nvme(4)` utiliza.

### Vetor RX+TX Coalescido

Alguns drivers coalescam o RX e o TX de um único par de filas em um único vetor. Uma NIC com 8 pares de filas usaria 8 vetores coalescidos mais alguns para admin. Quando o vetor dispara, o filtro verifica os bits de status tanto de RX quanto de TX e despacha de acordo.

Vantagens:
- Metade dos vetores por par de filas.
- O RX e o TX do mesmo par de filas tendem a ser locais ao NUMA entre si (eles compartilham a mesma memória de anel de descritores).

Desvantagens:
- O filtro é ligeiramente mais complexo.
- RX e TX podem interferir sob carga (uma rajada de RX ocupa o tempo do handler, atrasando as conclusões de TX).

Este é um design intermediário, utilizado por algumas NICs de consumo.

### Um Vetor para Todas as Filas

Alguns dispositivos muito limitados (NICs de baixo custo, dispositivos embarcados pequenos) têm apenas um ou dois vetores MSI-X no total. O driver usa um único vetor para todas as filas e despacha para cada fila com base em um registrador de status.

Vantagens:
- Funciona em hardware com poucos vetores.
- Alocação simples.

Desvantagens:
- Sem afinidade por fila.
- O filtro faz mais trabalho para decidir o que despachar.

Este é o padrão que um driver em hardware de baixíssima capacidade utiliza.

### Atribuição Dinâmica por CPU

Um design inteligente: alocar um vetor por CPU e atribuir filas a vetores de forma dinâmica. Uma fila RX é "proprietária" de uma CPU por vez; ela processa no vetor dessa CPU. Se a carga de trabalho mudar, o driver pode remapear as filas para CPUs diferentes.

Vantagens:
- Afinidade de cache por CPU ideal.
- Adapta-se a mudanças na carga de trabalho.

Desvantagens:
- Lógica de alocação e remapeamento complexa.
- Difícil de raciocinar a respeito.

Alguns drivers de NICs de alto desempenho (série Mellanox ConnectX, Intel 800 Series) usam variantes desse padrão.

### A Estratégia do Capítulo 20

O driver do Capítulo 20 usa a estratégia de atribuição fixa com três vetores. É a estratégia mais simples que ilustra o design com múltiplos vetores sem entrar em detalhes de NUMA ou remapeamento dinâmico. Drivers reais frequentemente começam com esse design e evoluem para padrões mais sofisticados conforme as necessidades exigem.

Um exercício desafio pede ao leitor que implemente a estratégia de alocação dinâmica por CPU como extensão.



## Uma Visão Mais Aprofundada sobre Moderação e Coalescência de Interrupções

Um conceito adjacente ao MSI-X que merece uma breve menção. Dispositivos modernos de alto throughput frequentemente suportam **moderação de interrupções** ou **coalescência**: o dispositivo acumula eventos no buffer (pacotes recebidos, conclusões) e dispara uma única interrupção para múltiplos eventos, seja com base em um limiar de tempo ou de contagem.

### Por que a Moderação Importa

Uma NIC recebendo dez milhões de pacotes por segundo dispararia dez milhões de interrupções se cada pacote gerasse uma. Isso é muito mais do que o viável; a CPU gastaria todo o seu tempo entrando e saindo de handlers de interrupção. A solução é agrupar: a NIC dispara uma interrupção a cada 50 microssegundos, e durante esses 50 microssegundos a NIC acumula todos os pacotes que chegaram. O handler processa todos os pacotes acumulados de uma só vez.

A coalescência troca latência por throughput: cada pacote leva até 50 microssegundos a mais para ser entregue ao espaço do usuário, mas a CPU lida com milhões de pacotes por segundo com uma taxa de interrupções gerenciável.

### Como os Drivers Controlam a Moderação

O mecanismo é específico do dispositivo. Formas comuns:

- **Baseada em tempo:** o dispositivo dispara após um intervalo configurado (por exemplo, 50 microssegundos).
- **Baseada em contagem:** o dispositivo dispara após N eventos (por exemplo, 16 pacotes).
- **Combinada:** qualquer limiar que for atingido primeiro.
- **Adaptativa:** o dispositivo (ou o driver) ajusta os limiares com base nas taxas observadas.

O driver normalmente programa os limiares por meio de registradores do dispositivo. O mecanismo MSI-X em si não fornece moderação; trata-se de uma funcionalidade do dispositivo que funciona com MSI-X porque MSI-X permite atribuição por vetor.

### O Driver do Capítulo 20 Não Usa Moderação

O driver do Capítulo 20 não tem moderação. Cada interrupção simulada produz uma chamada ao filtro. Em hardware real, isso seria um problema em altas taxas; no laboratório, está tudo bem.

Drivers reais como `em(4)`, `ix(4)`, `ixl(4)` e `mgb(4)` possuem parâmetros de moderação. A interface `sysctl` os expõe como valores ajustáveis:

```sh
sysctl dev.em.0 | grep itr
```

Um leitor que adaptar o driver do capítulo para um dispositivo real deve estudar os controles de moderação para esse dispositivo. O mecanismo é ortogonal ao MSI-X; os dois se combinam para fornecer tratamento de interrupções de alto desempenho.



## Padrões de Drivers FreeBSD Reais

Um passeio pelos padrões de múltiplos vetores que aparecem em `/usr/src/sys/dev/`. Cada padrão é um trecho curto de um driver real, acompanhado de uma nota sobre o que ele ensina para o Capítulo 20.

### Padrão: Divisão entre Vetor Admin e I/O no `nvme(4)`

`/usr/src/sys/dev/nvme/nvme_ctrlr.c` tem o padrão canônico admin-mais-N:

```c
/* Allocate one vector for admin + N for I/O. */
num_trackers = MAX(1, MIN(mp_ncpus, ctrlr->max_io_queues));
num_vectors_requested = num_trackers + 1;  /* +1 for admin */
num_vectors_allocated = num_vectors_requested;
pci_alloc_msix(ctrlr->dev, &num_vectors_allocated);

/* Admin queue uses vector 0 (rid 1). */
ctrlr->adminq.intr_rid = 1;
ctrlr->adminq.res = bus_alloc_resource_any(ctrlr->dev, SYS_RES_IRQ,
    &ctrlr->adminq.intr_rid, RF_ACTIVE);
bus_setup_intr(ctrlr->dev, ctrlr->adminq.res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    NULL, nvme_qpair_msix_handler, &ctrlr->adminq, &ctrlr->adminq.tag);

/* I/O queues use vectors 1..N (rid 2..N+1). */
for (i = 0; i < ctrlr->num_io_queues; i++) {
	ctrlr->ioq[i].intr_rid = i + 2;
	/* same pattern ... */
}
```

Por que importa: o padrão admin-mais-N é a escolha certa quando um vetor lida com trabalho infrequente e de alta prioridade (erros, eventos assíncronos) e N vetores lidam com trabalho limitado por taxa, por fila. A divisão admin/rx/tx do Capítulo 20 é uma versão miniatura disso.

### Padrão: Vetor de Par de Filas do `ixgbe`

`/usr/src/sys/dev/ixgbe/ix_txrx.c` usa um design de par de filas onde cada vetor lida com o RX e o TX de um único par de filas:

```c
/* One vector per queue pair + 1 for link. */
for (i = 0; i < num_qpairs; i++) {
	que[i].rid = i + 1;
	/* Filter checks both RX and TX status bits and dispatches. */
	bus_setup_intr(..., ixgbe_msix_que, &que[i], ...);
}
/* Link-state vector is the last one. */
link.rid = num_qpairs + 1;
bus_setup_intr(..., ixgbe_msix_link, sc, ...);
```

Por que importa: o design coalescido de RX+TX por par de filas reduz à metade o número de vetores sem sacrificar a afinidade por fila. Adequado quando o dispositivo tem muitas filas, mas poucos vetores.

### Padrão: Vetor por Virtqueue no `virtio_pci`

`/usr/src/sys/dev/virtio/pci/virtio_pci.c` tem um vetor por virtqueue:

```c
int nvectors = ... /* count of virtqueues + 1 for config */;
pci_alloc_msix(dev, &nvectors);
for (i = 0; i < nvectors; i++) {
	vec[i].rid = i + 1;
	/* Each vector gets the per-virtqueue data as its arg. */
	bus_setup_intr(dev, vec[i].res, ..., virtio_vq_intr, &vec[i], ...);
}
```

Por que importa: a atribuição por virtqueue do virtio é o modelo para qualquer dispositivo paravirtualizado. O número de vetores é igual ao número de virtqueues mais admin/config.

### Padrão: Vetor por Porta no `ahci`

`/usr/src/sys/dev/ahci/ahci_pci.c` usa um vetor por porta SATA:

```c
for (i = 0; i < ahci->nports; i++) {
	ahci->ports[i].rid = i + 1;
	/* ... */
}
```

Por que importa: controladores de armazenamento frequentemente usam atribuições de vetor por porta para que conclusões de I/O em portas diferentes possam ser processadas concorrentemente em CPUs diferentes.

### Padrão: Gerenciamento de Vetores Oculto pelo `iflib`

Drivers que usam `iflib(9)` (como `em(4)`, `igc(4)`, `ix(4)`, `ixl(4)`, `mgb(4)`) não gerenciam vetores diretamente. Em vez disso, eles registram funções de handler por fila na tabela de registro do iflib, e o iflib faz a alocação e o binding:

```c
static struct if_shared_ctx em_sctx_init = {
	/* ... */
	.isc_driver = &em_if_driver,
	.isc_tx_maxsize = EM_TSO_SIZE,
	/* ... */
};

static int
em_if_msix_intr_assign(if_ctx_t ctx, int msix)
{
	struct e1000_softc *sc = iflib_get_softc(ctx);
	int error, rid, i, vector = 0;

	/* iflib has already called pci_alloc_msix; sc knows the count. */
	for (i = 0; i < sc->rx_num_queues; i++, vector++) {
		rid = vector + 1;
		error = iflib_irq_alloc_generic(ctx, ..., rid, IFLIB_INTR_RXTX,
		    em_msix_que, ...);
	}
	return (0);
}
```

Por que importa: o iflib abstrai a alocação de MSI-X e o binding por fila por trás de uma API limpa. Drivers que usam iflib são mais simples do que drivers MSI-X diretos, mas abrem mão de alguma flexibilidade. O padrão iflib é a escolha certa para novos drivers de rede FreeBSD; o padrão MSI-X direto é a escolha certa para dispositivos que não são de rede ou quando o iflib não se encaixa.

### O Que os Padrões Ensinam

Todos esses drivers seguem o mesmo padrão estrutural que o Capítulo 20 ensina:

1. Consultar o número de vetores.
2. Alocar os vetores.
3. Para cada vetor: alocar o recurso de IRQ em rid=i+1, registrar o handler, descrever.
4. Vincular os vetores às CPUs.
5. No teardown: fazer o teardown de cada vetor em ordem inversa e, em seguida, chamar `pci_release_msi`.

As diferenças entre os drivers estão em:

- Quantos vetores são usados (1, alguns poucos, dezenas ou centenas).
- Como os vetores são atribuídos (admin+N, par de filas, por porta, por virtqueue).
- Se o iflib cuida da alocação.
- O que cada função de filtro faz (admin versus caminho de dados).

Um leitor que já possui o vocabulário do Capítulo 20 consegue reconhecer essas diferenças imediatamente.

## Uma Observação de Desempenho: Medindo o Benefício do MSI-X

Uma seção que fundamenta as afirmações de desempenho do capítulo em uma medição concreta.

### A Configuração do Teste

Suponha que você tenha o driver do Capítulo 20 rodando no QEMU com `virtio-rng-pci` (com MSI-X ativo) e um guest com múltiplas CPUs. O sysctl `intr_simulate_rx` permite disparar interrupções a partir de um loop em espaço do usuário:

```sh
# In one shell, drive simulated RX interrupts as fast as possible.
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate_rx=1 >/dev/null 2>&1
done
```

### Medindo com DTrace

Em outro shell, meça o tempo de CPU do filtro por invocação e em qual CPU ele é executado:

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; self->c = cpu; }
fbt::myfirst_rx_filter:return /self->ts/ {
    @lat[self->c] = quantize(vtimestamp - self->ts);
    self->ts = 0;
    self->c = 0;
}'
```

A saída é um histograma por CPU das latências do filtro. Se `bus_bind_intr` alocou o vetor RX na CPU 1, o histograma deve mostrar todas as invocações na CPU 1, com latências na casa de centenas de nanossegundos a poucos microssegundos.

### O Que os Resultados Mostram

Em um vetor MSI-X bem alocado:

- Toda invocação ocorre na mesma CPU (a CPU vinculada).
- As latências são consistentemente baixas (as linhas de cache quentes permanecem em uma única CPU).
- Nenhum bouncing de cache entre CPUs.

Em uma linha INTx legada compartilhada:

- As invocações se distribuem entre CPUs (o kernel faz o roteamento de forma aleatória).
- As latências são mais variáveis (linhas de cache frias a cada nova CPU).
- Tráfego de cache entre CPUs aparece nos contadores de desempenho.

A diferença pode ser medida em nanossegundos por invocação. Para um driver que lida com algumas centenas de interrupções por segundo, a diferença é imperceptível. Para um driver que lida com um milhão de interrupções por segundo, a diferença é entre "funciona" e "não funciona".

### A Lição Geral

O maquinário do Capítulo 20 é excessivo para drivers de baixa taxa. É essencial para drivers de alta taxa. Os padrões que o capítulo ensina escalam de "driver de demonstração processando cem interrupções por segundo" até "NIC de produção processando dez milhões". Saber onde nessa escala um driver específico se encontra determina o quanto das recomendações do Capítulo 20 importa na prática.



## Um Olhar Mais Profundo sobre o Design da Árvore sysctl para Drivers com Múltiplos Vetores

O driver do Capítulo 20 expõe seus contadores por vetor como sysctls planos (`vec0_fire_count`, `vec1_fire_count`, `vec2_fire_count`). Para um driver com muitos vetores, um namespace plano se torna difícil de gerenciar. Esta seção mostra como usar `SYSCTL_ADD_NODE` para construir uma árvore sysctl por vetor.

### O Trade-off entre Plano e em Árvore

Namespace plano (o que o Capítulo 20 usa):

```text
dev.myfirst.0.vec0_fire_count: 42
dev.myfirst.0.vec1_fire_count: 9876
dev.myfirst.0.vec2_fire_count: 4523
dev.myfirst.0.vec0_stray_count: 0
dev.myfirst.0.vec1_stray_count: 0
dev.myfirst.0.vec2_stray_count: 0
```

Vantagens: simples, sem chamadas a `SYSCTL_ADD_NODE`.
Desvantagens: muitos irmãos no nível superior; sem agrupamento.

Namespace em árvore:

```text
dev.myfirst.0.vec.admin.fire_count: 42
dev.myfirst.0.vec.admin.stray_count: 0
dev.myfirst.0.vec.rx.fire_count: 9876
dev.myfirst.0.vec.rx.stray_count: 0
dev.myfirst.0.vec.tx.fire_count: 4523
dev.myfirst.0.vec.tx.stray_count: 0
```

Vantagens: agrupa o estado por vetor; escala para muitos vetores; usa nomes em vez de números.
Desvantagens: requer mais código para configurar.

### O Código de Construção da Árvore

```c
void
myfirst_msix_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid *parent = sc->sysctl_tree;
	struct sysctl_oid *vec_node;
	struct sysctl_oid *per_vec_node;
	int i;

	/* Create the "vec" parent node. */
	vec_node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "vec", CTLFLAG_RD, NULL,
	    "Per-vector interrupt statistics");

	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		/* Create "vec.<name>" node. */
		per_vec_node = SYSCTL_ADD_NODE(ctx,
		    SYSCTL_CHILDREN(vec_node),
		    OID_AUTO, sc->vectors[i].name,
		    CTLFLAG_RD, NULL,
		    "Per-vector statistics");

		/* Add fire_count under it. */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(per_vec_node),
		    OID_AUTO, "fire_count", CTLFLAG_RD,
		    &sc->vectors[i].fire_count, 0,
		    "Times this vector's filter was called");

		/* Add stray_count. */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(per_vec_node),
		    OID_AUTO, "stray_count", CTLFLAG_RD,
		    &sc->vectors[i].stray_count, 0,
		    "Stray returns from this vector");

		/* Other per-vector fields... */
	}
}
```

As chamadas a `SYSCTL_ADD_NODE` criam os nós intermediários; as chamadas subsequentes a `SYSCTL_ADD_U64` anexam os contadores folha abaixo deles. A estrutura em árvore torna-se visível na saída do `sysctl` automaticamente.

### Consultando a Árvore

```sh
# Show all per-vector stats.
sysctl dev.myfirst.0.vec

# Show just the rx vector.
sysctl dev.myfirst.0.vec.rx

# Show only fire counts.
sysctl -n dev.myfirst.0.vec.admin.fire_count dev.myfirst.0.vec.rx.fire_count dev.myfirst.0.vec.tx.fire_count
```

A estrutura em árvore torna o namespace sysctl muito mais legível, especialmente para drivers com muitos vetores (NVMe com 32 filas de I/O, ou uma NIC com 16 pares de filas).

### Quando Usar a Árvore

Para o driver de três vetores do Capítulo 20, o namespace plano é suficiente. Para um driver com oito ou mais vetores, a árvore se torna valiosa. Um leitor que esteja escrevendo um driver de produção deve usar a árvore.

### Erros Comuns

- **Vazar o nó pai.** `SYSCTL_ADD_NODE` registra o nó em `sc->sysctl_ctx`; ele é liberado junto com o restante do contexto. Nenhuma liberação explícita é necessária.
- **Esquecer `NULL` para o argumento de handler.** `SYSCTL_ADD_NODE` não é um CTLPROC; é um nó de agrupamento puro. O argumento de handler é `NULL`.
- **Passar o pai errado para chamadas filhas de `SYSCTL_ADD_*`.** Use `SYSCTL_CHILDREN(vec_node)` para filhos de `vec_node`, não `SYSCTL_CHILDREN(parent)`.

Esse padrão de design em árvore é a forma mais limpa de expor o estado de múltiplos vetores. O exercício desafio do Capítulo 20 sugere implementá-lo como uma extensão.



## Um Olhar Mais Profundo sobre os Caminhos de Erro na Configuração de MSI-X

A Seção 3 e a Seção 5 mostraram o código do caminho feliz. Esta seção percorre o que pode dar errado e como diagnosticar cada problema.

### Modo de Falha 1: pci_msix_count Retorna 0

Sintoma: a tentativa de MSI-X é ignorada porque o contador é 0.

Causa: o dispositivo não tem capacidade MSI-X, ou o driver de barramento PCI não a descobriu.

Correção: confirme com `pciconf -lvc`. Se o dispositivo anuncia MSI-X, mas `pci_msix_count` retorna 0, a configuração PCI do dispositivo está com defeito ou o probe do kernel não a encontrou; raro e difícil de corrigir no driver.

### Modo de Falha 2: pci_alloc_msix Retorna EINVAL

Sintoma: a alocação falha com `EINVAL`.

Causa: o driver está solicitando uma quantidade maior do que o máximo anunciado pelo dispositivo, ou está solicitando 0.

Correção: limite a quantidade solicitada ao valor retornado por `pci_msix_count`. Sempre solicite pelo menos 1.

### Modo de Falha 3: pci_alloc_msix Retorna Menos Vetores do que o Solicitado

Sintoma: `count` após a chamada é menor do que o solicitado.

Causa: o pool de vetores do kernel estava parcialmente esgotado; a alocação do dispositivo recebeu o que restava.

Correção: decida com antecedência se vai aceitar, adaptar ou liberar. O driver do Capítulo 20 libera e recai para MSI.

### Modo de Falha 4: bus_alloc_resource_any Retorna NULL para um Vetor MSI-X

Sintoma: após `pci_alloc_msix` ter tido sucesso, a alocação do IRQ por vetor falha.

Causas:
- rid errado (usando 0 em vez de i+1).
- Já liberado anteriormente (liberação dupla).
- Sem recursos de IRQ disponíveis na camada do barramento.

Correção: verifique se o rid é i+1. Audite o código de liberação. Registre o erro.

### Modo de Falha 5: bus_setup_intr Retorna EINVAL para um Handler por Vetor

Sintoma: `bus_setup_intr` falha.

Causas:
- Filtro e ithread ambos NULL.
- Flag `INTR_TYPE_*` ausente.
- Já configurado anteriormente (configuração dupla).

Correção: garanta que o argumento de filtro seja não-NULL. Inclua uma flag `INTR_TYPE_*`. Audite o código de configuração para detectar registros duplos.

### Modo de Falha 6: bus_bind_intr Retorna um Erro

Sintoma: `bus_bind_intr` retorna valor diferente de zero.

Causas:
- A plataforma não suporta revinculação.
- CPU fora do intervalo válido.
- Configuração do kernel (NO_SMP, NUMA desabilitado).

Correção: trate como não fatal (use `device_printf` para emitir um aviso e continue). O driver ainda funciona sem a vinculação.

### Modo de Falha 7: vmstat -i Mostra Vetores mas os Contadores Não Incrementam

Sintoma: o kernel enxerga os vetores, mas os filtros nunca disparam.

Causas:
- O `INTR_MASK` do dispositivo é zero (problema do capítulo 19).
- O dispositivo reiniciou seu estado de interrupção.
- Bug de hardware ou problema de configuração do bhyve/QEMU.

Correção: verifique o `INTR_MASK` do dispositivo. Use o sysctl de interrupção simulada para confirmar se o filtro funciona em geral.

### Modo de Falha 8: Segundo kldload Recai para uma Camada Inferior

Sintoma: o primeiro carregamento usa MSI-X; descarregamento; o segundo carregamento usa legado ou MSI.

Causa: `pci_release_msi` não foi chamado na desmontagem.

Correção: audite o caminho de desmontagem. Certifique-se de que `pci_release_msi` seja executado em todo caminho de alocação bem-sucedida.

### Modo de Falha 9: WITNESS Entra em Pânico durante a Configuração com Múltiplos Vetores

Sintoma: `WITNESS` reporta uma violação de ordem de lock ou "lock held during sleep" durante a configuração por vetor.

Causa: manter `sc->mtx` durante uma chamada a `bus_setup_intr`. Os hooks do barramento podem dormir, e manter um mutex durante um sleep é ilegal.

Correção: libere `sc->mtx` antes de chamar `bus_setup_intr`. Readquira depois, se necessário.

### Modo de Falha 10: Configuração Parcial Não Desfaz Corretamente

Sintoma: o attach falha; o segundo attach falha com "resource in use".

Causa: a cascata de goto de falha parcial não desfaz até o fim. Algum estado por vetor persiste.

Correção: garanta que a cascata desfaça até o vetor que falhou, não além dele. Use o helper por vetor de forma consistente.



## Solução de Problemas Adicionais

Alguns modos de falha extras que leitores do Capítulo 20 podem encontrar.

### "Guest QEMU não expõe MSI-X"

Causas: versão do QEMU muito antiga, ou o guest está inicializando com virtio legado.

Correção: atualize o QEMU para uma versão recente. No guest, verifique:

```sh
pciconf -lvc | grep -B 1 -A 2 'cap 11'
```

Se nenhuma linha `cap 11` aparecer, MSI-X não está disponível. Mude para o virtio-rng-pci moderno do QEMU com `-device virtio-rng-pci,disable-legacy=on`.

### "intr_simulate_rx incrementa fire_count mas a task nunca executa"

Causa: `TASK_INIT` da task não foi chamado, ou a taskqueue não foi iniciada.

Correção: verifique `TASK_INIT(&vec->task, 0, myfirst_rx_task_fn, vec)` na configuração. Verifique `taskqueue_start_threads(&sc->intr_tq, ...)`.

### "Contadores por vetor incrementam mas o stray count sobe proporcionalmente"

Causa: a verificação de status do filtro está errada, ou múltiplos vetores estão disparando no mesmo bit.

Correção: cada filtro deve verificar seus bits específicos. Se dois filtros tentam tratar `DATA_AV`, um vencerá e o outro verá stray.

### "cpuset -g -x $irq reporta mask 0 em todos os vetores"

Causa: `bus_bind_intr` não foi chamado, ou foi chamado com CPU 0 (mask 1).

Correção: se intencionalmente não vinculado, "mask 0" pode ser específico da plataforma. Se a vinculação foi tentada, verifique o valor de retorno de `bus_bind_intr`.

### "Carregamento do driver tem sucesso mas dmesg não mostra o banner de attach"

Causa: o `device_printf` ocorreu antes do flush do banner, ou o banner está em um buffer de boot muito inicial.

Correção: `dmesg -a` mostra o buffer de mensagens completo. Verifique com `dmesg -a | grep myfirst`.

### "Detach trava após configuração com múltiplos vetores"

Causa: o handler de algum vetor ainda está em execução quando a desmontagem tenta prosseguir. `bus_teardown_intr` bloqueia aguardando seu término.

Correção: certifique-se de que o `INTR_MASK` do dispositivo seja limpo *antes* de `bus_teardown_intr`, para que nenhum novo handler possa ser despachado. Certifique-se de que o filtro não entre em loop infinito; disciplina de tempo de execução curto.

### "pci_alloc_msix tem sucesso mas apenas alguns vetores disparam"

Causa: o dispositivo não está sinalando nos vetores que deveria. Pode ser um bug no driver (esqueceu de habilitar) ou uma peculiaridade do dispositivo.

Correção: use o sysctl de interrupção simulada para confirmar que o filtro funciona para cada vetor. Se o caminho simulado funciona mas eventos reais não disparam o vetor, o problema está no lado do dispositivo.



## Exemplo Trabalhado: Rastreando um Evento pelos Três Níveis

Para tornar a escada de fallback concreta, aqui está um rastreamento completo do mesmo evento (uma interrupção DATA_AV simulada) em cada um dos três níveis.

### Nível 3: INTx Legado

No bhyve com virtio-rnd (sem MSI-X exposto), o driver recai para INTx legado com um único handler em rid 0.

1. O usuário executa `sudo sysctl dev.myfirst.0.intr_simulate_admin=1` (ou `intr_simulate_rx=1`, etc.).
2. O handler do sysctl adquire `sc->mtx`, escreve o bit em INTR_STATUS, libera e chama o filtro.
3. O único `myfirst_intr_filter` (do Capítulo 19) é executado. Ele lê INTR_STATUS, vê o bit, faz o acknowledge e enfileira a task (para DATA_AV) ou trata inline (para ERROR/COMPLETE).
4. `intr_count` incrementa.
5. No modo legado, há apenas um vetor, portanto os três sysctls de interrupção simulada passam pelo mesmo filtro.

Observações:
- `sysctl dev.myfirst.0.intr_mode` retorna 0.
- `vmstat -i | grep myfirst` mostra uma linha.
- Os contadores por vetor não existem (o modo legado usa os contadores do Capítulo 19).

### Nível 2: MSI

Em um sistema que suporta MSI mas não MSI-X, o driver aloca um único vetor MSI. MSI exige uma quantidade de vetores que seja potência de dois, portanto o driver não pode solicitar 3 aqui; ele solicita 1 e usa o padrão de handler único do Capítulo 19.

1. O usuário executa `sudo sysctl dev.myfirst.0.intr_simulate_admin=1` (ou `intr_simulate_rx=1`, ou `intr_simulate_tx=4`).
2. Como apenas um vetor está configurado no nível MSI, os três sysctls de simulação por vetor são roteados pelo mesmo `myfirst_intr_filter` do Capítulo 19.
3. O filtro lê INTR_STATUS, vê o bit, faz o acknowledge e trata inline ou enfileira a task.

Observações:
- `sysctl dev.myfirst.0.intr_mode` retorna 1.
- `vmstat -i | grep myfirst` mostra uma linha (o único handler MSI em rid=1, rotulado "msi").
- Os contadores por vetor nos slots 1 e 2 permanecem em 0 porque apenas o slot 0 está em uso; os contadores globais do Capítulo 19 (`intr_count`, `intr_data_av_count`, etc.) são os que avançam.

### Nível 1: MSI-X

No QEMU com virtio-rng-pci, o driver aloca MSI-X com 3 vetores, cada um vinculado a uma CPU.

1. O usuário executa `sudo sysctl dev.myfirst.0.intr_simulate_rx=1`.
2. O sysctl chama o filtro rx diretamente (o caminho simulado não passa pelo hardware).
3. `myfirst_rx_filter` é executado (na CPU em que o sysctl foi invocado, porque a simulação não passa pelo despacho de interrupções do kernel).
4. Os contadores são incrementados; a task é executada.

Observações:
- `sysctl dev.myfirst.0.intr_mode` retorna 2.
- `vmstat -i | grep myfirst` exibe três linhas; cada uma tem um número de IRQ diferente.
- `cpuset -g -x <irq>` para cada IRQ exibe máscaras de CPU diferentes.

Uma interrupção MSI-X real (não simulada) seria despachada na CPU vinculada; o bypass da simulação faz com que ela seja executada na CPU da thread chamadora. Essa é uma limitação da técnica de simulação, mas não afeta a correção do comportamento.

### A Lição

Todos os três níveis acionam a mesma lógica de filtro e a mesma task. As únicas diferenças são:

- Qual rid o recurso de IRQ utiliza (0 para legacy, 1+ para MSI/MSI-X).
- Se `pci_alloc_msi` ou `pci_alloc_msix` foi bem-sucedido.
- Quantas funções de filtro estão registradas (1 para legacy, 3 para MSI/MSI-X).
- Em qual CPU as interrupções reais são despachadas.

Um driver bem escrito funciona de forma idêntica nos três níveis. A escada de fallback do Capítulo 20 garante isso.



## Laboratório Prático: Testes de Regressão nos Três Níveis

Um laboratório que exercita a escada de fallback para confirmar que os três níveis funcionam.

### Configuração

Você precisa de dois ambientes de teste:

- **Ambiente A**: bhyve com virtio-rnd. O driver faz fallback para legacy INTx.
- **Ambiente B**: QEMU com virtio-rng-pci. O driver utiliza MSI-X.

(Um terceiro ambiente com apenas MSI e sem MSI-X é difícil de construir de forma confiável em plataformas modernas. O caminho de MSI só é exercitado se você tiver um sistema em que o MSI-X falhe, mas o MSI funcione.)

### Procedimento

1. No Ambiente A, carregue `myfirst.ko`. Verifique:

```sh
sysctl dev.myfirst.0.intr_mode   # returns 0
vmstat -i | grep myfirst          # one line
```

2. Exercite o pipeline por meio dos sysctls de interrupção simulada. Os três devem funcionar, embora no modo legacy todos passem pelo mesmo filtro.

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2
sudo sysctl dev.myfirst.0.intr_simulate_rx=1
sudo sysctl dev.myfirst.0.intr_simulate_tx=4
sysctl dev.myfirst.0.intr_count   # should be 3
```

3. Descarregue o módulo. Verifique se não há vazamentos.

4. No Ambiente B, repita:

```sh
sysctl dev.myfirst.0.intr_mode   # returns 2
vmstat -i | grep myfirst          # three lines
for irq in <IRQs>; do cpuset -g -x $irq; done
```

5. Exercite o pipeline por vetor. Cada sysctl deve incrementar o contador do seu próprio vetor.

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2
sysctl dev.myfirst.0.vec.admin.fire_count  # 1
sysctl dev.myfirst.0.vec.rx.fire_count     # 0
sysctl dev.myfirst.0.vec.tx.fire_count     # 0
```

6. Descarregue o módulo. Verifique se não há vazamentos.

### Observações Esperadas

- Ambos os ambientes fazem attach sem erros.
- A linha de resumo do dmesg exibe o modo correto para cada um.
- Os contadores por vetor incrementam de forma independente no MSI-X.
- No modo legacy, um único contador cobre todos os eventos.
- Sem vazamentos após o descarregamento do módulo em nenhum dos ambientes.

### O que Fazer se um Nível Falhar

Se o nível de MSI-X falhar no Ambiente B:

1. Verifique se a versão do QEMU é suficientemente recente. Versões mais antigas (anteriores à 5.0) têm comportamentos inesperados.
2. Verifique `pciconf -lvc` no guest; a capacidade de MSI-X deve estar visível.
3. Verifique o `dmesg` em busca de erros provenientes de `pci_alloc_msix`.

Se o nível legacy falhar no Ambiente A:

1. Verifique `pciconf -lvc` para a configuração de linha de interrupção do dispositivo.
2. Certifique-se de que `virtio_rnd` não esteja já em attach (ressalva do Capítulo 18).
3. Procure falhas de `pci_alloc_resource` no `dmesg`.



## Desafio Estendido: Construindo um Driver de Qualidade de Produção

Um exercício opcional para leitores que desejam praticar o design multi-vetor em escala realista.

### O Objetivo

Pegue o driver do Capítulo 20 e o estenda para lidar dinamicamente com N filas, onde N é descoberto no momento do attach com base na quantidade de vetores MSI-X alocados. Cada fila tem:

- Seu próprio vetor (vetor MSI-X 1+queue_id).
- Sua própria função de filtro (ou uma compartilhada que identifica a fila pelo argumento de vetor).
- Seus próprios contadores.
- Sua própria task em seu próprio taskqueue.
- Seu próprio vínculo de CPU local ao NUMA.

### Esboço de Implementação

1. Substitua `MYFIRST_MAX_VECTORS` por uma contagem escolhida em tempo de execução.
2. Aloque o array `vectors[]` dinamicamente (usando `malloc`).
3. Aloque um taskqueue separado por vetor.
4. Use `bus_get_cpus(INTR_CPUS, ...)` para distribuir os vetores entre CPUs locais ao NUMA.
5. Adicione sysctls que se dimensionem com a quantidade de vetores.

### Testes

Execute o driver em um guest com diferentes contagens de vetores MSI-X. Para cada contagem, verifique:

- Os contadores de disparo incrementam para as interrupções simuladas.
- A afinidade de CPU respeita a localidade NUMA.
- O teardown é limpo.

### O que este Exercício Treina

- Gerenciamento de memória dinâmica em um driver.
- A API `bus_get_cpus`.
- Taskqueues por fila (desafio 3 de seções anteriores).
- Construção de árvore sysctl em tempo de execução (desafio 7 de seções anteriores).

Este é um exercício significativo e provavelmente levará várias horas. O resultado é um driver visivelmente semelhante a drivers de produção de NIC e NVMe.



## Referência: Valores de Prioridade para Interrupções e Tasks

Para referência rápida, as constantes de prioridade que um driver do Capítulo 20 pode utilizar (de `/usr/src/sys/sys/priority.h`):

```text
PI_REALTIME  = PRI_MIN_ITHD + 0   (highest; rarely used)
PI_INTR      = PRI_MIN_ITHD + 4   (common hardware interrupt level)
PI_AV        = PI_INTR            (audio/video)
PI_NET       = PI_INTR            (network)
PI_DISK      = PI_INTR            (block storage)
PI_TTY       = PI_INTR            (terminal/serial)
PI_DULL      = PI_INTR            (low-priority hardware)
PI_SOFT      = PRI_MIN_ITHD + 8   (soft interrupts)
```

As prioridades de hardware comuns mapeiam para `PI_INTR`; os nomes são distinções de intenção, não de prioridade de escalonamento. O driver do Capítulo 20 usa `PI_NET` para seu taskqueue; qualquer prioridade em nível de hardware funcionaria de forma equivalente.



## Referência: One-Liners Úteis de DTrace para Drivers MSI-X

Para leitores que desejam observar o comportamento do driver do Capítulo 20 de forma dinâmica.

### Contar invocações de filtro por CPU

```sh
sudo dtrace -n '
fbt::myfirst_admin_filter:entry, fbt::myfirst_rx_filter:entry,
fbt::myfirst_tx_filter:entry { @[probefunc, cpu] = count(); }'
```

Exibe qual filtro é executado em qual CPU.

### Tempo gasto em cada filtro

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_rx_filter:return /self->ts/ {
    @[probefunc] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Histograma do tempo de CPU do filtro RX.

### Taxa de interrupções simuladas versus reais

```sh
sudo dtrace -n '
fbt::myfirst_intr_simulate_vector_sysctl:entry { @sims = count(); }
fbt::myfirst_rx_filter:entry { @filters = count(); }'
```

Se `filters > sims`, algumas interrupções reais estão sendo disparadas.

### Latência de task

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_rx_task_fn:entry /self->ts/ {
    @lat = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Histograma do tempo entre o filtro e a invocação da task. Exibe a latência de escalonamento do taskqueue.



## Referência: Uma Nota Final Antes do Fim da Parte 4

Os Capítulos 16 a 20 construíram a história completa de interrupções e hardware para o driver `myfirst`. Cada capítulo adicionou uma camada:

- Capítulo 16: acesso a registradores.
- Capítulo 17: simulação de comportamento de dispositivo.
- Capítulo 18: attach PCI.
- Capítulo 19: tratamento de interrupções com vetor único.
- Capítulo 20: MSI/MSI-X multi-vetor.

O Capítulo 21 adicionará DMA, concluindo a camada de hardware da Parte 4. Nesse ponto, o driver `myfirst` será estruturalmente um driver real: um dispositivo PCI com interrupções MSI-X e transferência de dados baseada em DMA. O que o distingue de um driver de produção é o protocolo específico que ele fala (nenhum, na verdade; é uma demonstração) e o dispositivo que ele tem como alvo (uma abstração virtio-rnd).

Um leitor que internalizou esses cinco capítulos pode abrir qualquer driver do FreeBSD em `/usr/src/sys/dev/` e reconhecer os padrões. Esse reconhecimento é o maior ganho da Parte 4.



## Laboratórios Práticos

Os laboratórios são pontos de verificação graduais. Cada laboratório se baseia no anterior e corresponde a uma das etapas do capítulo. Um leitor que completar todos os cinco terá um driver multi-vetor completo, um ambiente de testes QEMU funcional para MSI-X e um script de regressão que valida os três níveis da escada de fallback.

Os orçamentos de tempo pressupõem que você já tenha lido as seções relevantes.

### Laboratório 1: Descobrindo as Capacidades de MSI e MSI-X

Tempo: trinta minutos.

Objetivo: Desenvolver intuição sobre quais dispositivos do seu sistema suportam MSI e MSI-X.

Passos:

1. Execute `sudo pciconf -lvc > /tmp/pci_caps.txt`. O flag `-c` inclui listas de capacidades.
2. Pesquise capacidades de MSI: `grep -B 1 "cap 05" /tmp/pci_caps.txt`.
3. Pesquise capacidades de MSI-X: `grep -B 1 "cap 11" /tmp/pci_caps.txt`.
4. Para três dispositivos que suportam MSI-X, anote:
   - O nome do dispositivo (`pci0:B:D:F`).
   - A quantidade de mensagens MSI-X suportadas.
   - Se o driver está usando MSI-X no momento (verifique `vmstat -i` em busca de múltiplas linhas com o mesmo nome de dispositivo).
5. Compare a quantidade total de dispositivos com suporte a MSI com a quantidade total de dispositivos com suporte a MSI-X. Sistemas modernos normalmente têm mais dispositivos MSI-X do que dispositivos apenas com MSI.

Observações esperadas:

- NICs geralmente anunciam MSI-X com muitos vetores (de 4 a 64).
- Controladores SATA e NVMe anunciam MSI-X (NVMe frequentemente com dezenas de vetores).
- Alguns dispositivos legacy (um chip de áudio, um controlador USB) anunciam apenas MSI.
- Alguns dispositivos muito antigos não anunciam nenhum e dependem de legacy INTx.

Este laboratório é sobre vocabulário. Sem código. O ganho é que as chamadas de alocação das Seções 2 e 5 tornam-se concretas.

### Laboratório 2: Etapa 1, Escada de Fallback MSI

Tempo: duas a três horas.

Objetivo: Estender o driver do Capítulo 19 com a escada de fallback com prioridade para MSI. Versão alvo: `1.3-msi-stage1`.

Passos:

1. A partir da Etapa 4 do Capítulo 19, copie o código-fonte do driver para um novo diretório de trabalho.
2. Adicione o campo `intr_mode` e o enum em `myfirst.h`.
3. Modifique `myfirst_intr_setup` (em `myfirst_intr.c`) para tentar alocar MSI primeiro, fazendo fallback para legacy INTx.
4. Modifique `myfirst_intr_teardown` para chamar `pci_release_msi` quando o MSI foi utilizado.
5. Adicione o sysctl `dev.myfirst.N.intr_mode`.
6. Atualize a string de versão no `Makefile` para `1.3-msi-stage1`.
7. Compile (`make clean && make`).
8. Carregue em um guest. Anote o modo que o driver reporta:

```sh
sudo kldload ./myfirst.ko
sudo dmesg | tail -5
sysctl dev.myfirst.0.intr_mode
```

No QEMU com virtio-rng-pci, o driver deve reportar `MSI, 1 vector` (ou similar). No bhyve com virtio-rnd, deve reportar `legacy INTx`.

9. Descarregue o módulo e verifique se não há vazamentos.

Falhas comuns:

- Ausência de `pci_release_msi`: o próximo carregamento falha ou faz fallback para legacy.
- rid incorreto (usando 0 para MSI): `bus_alloc_resource_any` retorna NULL.
- Não verificar a contagem retornada: o driver prossegue com menos vetores do que o esperado.

### Laboratório 3: Etapa 2, Alocação Multi-Vetor (MSI)

Tempo: três a quatro horas.

Objetivo: Estender para três vetores MSI com handlers por vetor. Versão alvo: `1.3-msi-stage2`.

Passos:

1. A partir do Laboratório 2, adicione a struct `myfirst_vector` e o array por vetor em `myfirst.h`.
2. Escreva três funções de filtro: `myfirst_admin_filter`, `myfirst_rx_filter`, `myfirst_tx_filter`.
3. Escreva os helpers `myfirst_intr_setup_vector` e `myfirst_intr_teardown_vector`.
4. Modifique `myfirst_intr_setup` para tentar `pci_alloc_msi` para `MYFIRST_MAX_VECTORS` vetores, configurando cada vetor de forma independente.
5. Modifique `myfirst_intr_teardown` para iterar por vetor.
6. Adicione sysctls de contador por vetor (`vec0_fire_count`, `vec1_fire_count`, `vec2_fire_count`).
7. Adicione sysctls de interrupção simulada por vetor (`intr_simulate_admin`, `intr_simulate_rx`, `intr_simulate_tx`).
8. Atualize a versão para `1.3-msi-stage2`.
9. Compile, carregue, verifique:

```sh
sysctl dev.myfirst.0.intr_mode   # should be 1 on QEMU
vmstat -i | grep myfirst          # should show 3 lines
```

10. Exercite cada vetor:

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2  # ERROR
sudo sysctl dev.myfirst.0.intr_simulate_rx=1     # DATA_AV
sudo sysctl dev.myfirst.0.intr_simulate_tx=4     # COMPLETE
sysctl dev.myfirst.0 | grep vec
```

O contador de cada vetor deve incrementar de forma independente.

11. Descarregue o módulo, verifique se não há vazamentos.

### Laboratório 4: Etapa 3, MSI-X com Vinculação de CPU

Tempo: três a quatro horas.

Objetivo: Priorizar MSI-X em vez de MSI, vincular cada vetor a uma CPU. Versão alvo: `1.3-msi-stage3`.

Passos:

1. A partir do Laboratório 3, altere a escada de fallback para tentar MSI-X primeiro (via `pci_msix_count` e `pci_alloc_msix`), MSI como segundo nível e legacy como último.
2. Adicione o helper `myfirst_msix_bind_vectors` que chama `bus_bind_intr` para cada vetor.
3. Chame o helper de vinculação após todos os vetores estarem registrados.
4. Atualize a linha de resumo do dmesg para distinguir MSI-X de MSI.
5. Atualize a versão para `1.3-msi-stage3`.
6. Compile, carregue no QEMU com `virtio-rng-pci`. Verifique:

```sh
sysctl dev.myfirst.0.intr_mode   # should be 2 on QEMU
sudo dmesg | grep myfirst | grep MSI-X
```

A linha de attach deve exibir `interrupt mode: MSI-X, 3 vectors`.

7. Verifique os vínculos de CPU por vetor:

```sh
# For each myfirst IRQ, show its CPU binding.
vmstat -i | grep myfirst
# (Note the IRQ numbers, then:)
for irq in <IRQ1> <IRQ2> <IRQ3>; do
    echo "IRQ $irq:"
    cpuset -g -x $irq
done
```

Em um guest multi-CPU, cada vetor deve estar vinculado a uma CPU diferente.

8. Exercite cada vetor (da mesma forma que no Laboratório 3).

9. Faça detach e reattach:

```sh
sudo devctl detach myfirst0
sudo devctl attach pci0:0:4:0
sysctl dev.myfirst.0.intr_mode  # should still be 2
```

10. Descarregue o módulo, verifique se não há vazamentos.

### Laboratório 5: Etapa 4, Refatoração, Regressão, Versão

Tempo: três a quatro horas.

Objetivo: Mover o código multi-vetor para `myfirst_msix.c`, escrever `MSIX.md`, executar a regressão. Versão alvo: `1.3-msi`.

Passos:

1. A partir do Laboratório 4, crie `myfirst_msix.c` e `myfirst_msix.h`.
2. Mova as funções de filtro por vetor, helpers, setup, teardown e o registro de sysctl para `myfirst_msix.c`.
3. Mantenha o fallback legacy-INTx em `myfirst_intr.c` (arquivo do Capítulo 19).
4. Em `myfirst_pci.c`, substitua as antigas chamadas de setup/teardown de interrupção por chamadas para `myfirst_msix.c`.
5. Atualize o `Makefile` para adicionar `myfirst_msix.c` ao SRCS. Atualize a versão para `1.3-msi`.
6. Escreva `MSIX.md` documentando o design multi-vetor.
7. Compile, carregue, execute o script de regressão completo (dos exemplos acompanhantes).
8. Confirme que os três níveis funcionam (testando no bhyve com virtio-rnd para legacy e no QEMU com virtio-rng-pci para MSI-X).

Resultados esperados:

- O driver na versão `1.3-msi` funciona tanto no bhyve (fallback legacy) quanto no QEMU (MSI-X).
- `myfirst_intr.c` agora contém apenas o caminho de fallback com handler único do Capítulo 19.
- `myfirst_msix.c` contém a lógica multi-vetor do Capítulo 20.
- `MSIX.md` documenta o design de forma clara.



## Exercícios Desafio

Os desafios se baseiam nos laboratórios e estendem o driver em direções que o capítulo não explorou.

### Desafio 1: Adaptação Dinâmica da Contagem de Vetores

Modifique a configuração para se adaptar ao número de vetores que o kernel realmente aloca. Se 3 forem solicitados mas apenas 2 forem alocados, o driver deve continuar funcionando com 2 (consolidando admin e tx em um único vetor combinado). Se apenas 1 for alocado, consolide tudo em um.

Este exercício ensina a estratégia "adapt" da escada de fallback.

### Desafio 2: Vinculação de CPU Ciente de NUMA

Substitua a vinculação de CPU em round-robin por uma vinculação ciente de NUMA usando `bus_get_cpus(dev, INTR_CPUS, ...)`. Verifique com `cpuset -g -x <irq>` que os vetores chegam a CPUs no mesmo domínio NUMA que o dispositivo.

Em um sistema de soquete único, o exercício é acadêmico; em um host de teste com múltiplos soquetes, é mensurável.

### Desafio 3: Taskqueues por Vetor

Cada vetor atualmente compartilha um único taskqueue. Modifique o driver para que cada vetor tenha seu próprio taskqueue (com sua própria thread de trabalho). Meça o impacto na latência com DTrace.

Este exercício apresenta workers por vetor e mostra quando eles ajudam versus quando prejudicam.

### Desafio 4: Controle de Máscara MSI-X por Vetor

O registrador de controle de vetor da tabela MSI-X tem um bit de máscara por vetor. Adicione um sysctl que permita ao operador mascarar um vetor individual em tempo de execução. Verifique que um vetor mascarado para de receber interrupções.

Dica: o bit de máscara é programado por meio de acesso direto à tabela MSI-X, o que é um tópico mais avançado do que o Capítulo 20 abrange. A implementação MSI-X do FreeBSD pode ou não expor isso diretamente; o leitor pode precisar usar `bus_teardown_intr` e depois `bus_setup_intr` como uma "soft mask" de nível mais alto.

### Desafio 5: Implementar Moderação de Interrupções

Para um driver simulado, a moderação é fácil de prototipar: um sysctl que agrega N interrupções simuladas em uma única execução de tarefa. Implemente a agregação e meça o trade-off entre latência e throughput.

### Desafio 6: Reatribuição de Vetores em Tempo de Execução

Adicione um sysctl que permita ao operador reatribuir qual vetor trata qual classe de evento (por exemplo, trocar RX e TX). Demonstre que, após a reatribuição, simulated-interrupt-RX aciona o filtro de TX e vice-versa.

### Desafio 7: Árvore de Sysctl por Fila

Reestruture os sysctls por vetor em uma árvore adequada: `dev.myfirst.N.vec.admin.fire_count`, `dev.myfirst.N.vec.rx.fire_count`, etc. Use `SYSCTL_ADD_NODE` para criar os nós da árvore.

### Desafio 8: Instrumentação com DTrace

Escreva um script DTrace que mostre a distribuição por CPU das invocações de filtro de cada vetor. Exiba a distribuição por CPU como um histograma. Este é o diagnóstico que confirma que a vinculação de CPU está funcionando.



## Solução de Problemas e Erros Comuns

### "pci_alloc_msix retorna EBUSY ou ENXIO"

Possíveis causas:

1. O dispositivo não está conectado de uma forma que suporte MSI-X (o virtio-rnd legado no bhyve, por exemplo). Verifique com `pciconf -lvc`.
2. Um carregamento anterior do driver não chamou `pci_release_msi` no teardown. Reinicie ou tente `kldunload` + `kldload` novamente.
3. O kernel ficou sem vetores de interrupção. Raro em x86 moderno, possível em plataformas com poucos vetores.

### "vmstat -i mostra apenas uma linha no guest MSI-X"

Causa provável: `pci_alloc_msix` teve sucesso, mas alocou apenas 1 vetor. Verifique a contagem retornada versus a solicitada. Aceite (concentre o trabalho em um único vetor) ou libere e recorra ao fallback.

### "Filtro dispara mas vec->fire_count permanece zero"

Causa provável: o argumento `sc` está sendo confundido com `vec`. O handler recebe `vec`, não `sc`. Verifique o argumento de `bus_setup_intr`.

### "Driver entra em pânico no kldunload após múltiplos ciclos de carga/descarga"

Causa provável: `pci_release_msi` não foi chamado no teardown. O estado MSI no nível do dispositivo vaza entre carregamentos; eventualmente, a contabilidade interna do kernel fica corrompida.

### "Vetores diferentes disparam todos na mesma CPU"

Causa provável: `bus_bind_intr` falhou silenciosamente. Verifique o valor de retorno e registre resultados diferentes de zero.

### "Alocação MSI-X tem sucesso mas vmstat -i não mostra eventos"

Causa provável: a escrita em `INTR_MASK` do dispositivo visou o registrador errado ou foi ignorada. Verifique se a máscara está configurada (diagnóstico do Capítulo 17/Capítulo 19).

### "Interrupções stray acumulam no vetor admin do MSI-X"

Causa provável: a verificação de status do filtro admin está incorreta; o filtro retorna `FILTER_STRAY` quando deveria tratar a interrupção. Verifique a checagem `status & MYFIRST_INTR_ERROR`.

### "Comportamento de IRQ compartilhado no fallback legado difere do MSI-X"

Esperado. No INTx legado, o único handler vê todos os bits de evento; no MSI-X, cada vetor vê apenas seu próprio evento. Os testes que exercitam contagens de stray por vetor diferem entre os dois modos.

### "Estágio 2 compila mas Estágio 3 falha com erro de link em `bus_get_cpus`"

Causa: `bus_get_cpus` pode não estar disponível em versões mais antigas do FreeBSD ou pode exigir um posicionamento específico de `#include <sys/bus.h>`. Verifique a ordem dos includes.

### "Guest QEMU não expõe MSI-X apesar de usar virtio-rng-pci"

Causa provável: versões mais antigas do QEMU usam virtio legado por padrão. Verifique `pciconf -lvc` no guest; se MSI-X não estiver listado, o guest está usando o modo legado. Atualize o QEMU ou use `-device virtio-rng-pci,disable-modern=off,disable-legacy=on`.



## Encerrando

O Capítulo 20 deu ao driver a capacidade de lidar com múltiplos vetores de interrupção. O ponto de partida foi `1.2-intr` com um único handler na linha INTx legada. O ponto de chegada é `1.3-msi` com uma escada de fallback de três níveis (MSI-X, MSI, legado), três handlers de filtro por vetor, contadores e tarefas por vetor, vinculação de CPU por vetor, um teardown limpo de múltiplos vetores, e um novo arquivo `myfirst_msix.c` mais o documento `MSIX.md`.

As oito seções percorreram a progressão completa. A Seção 1 apresentou MSI e MSI-X no nível de hardware. A Seção 2 adicionou MSI como uma alternativa de vetor único ao INTx legado. A Seção 3 estendeu para MSI com múltiplos vetores. A Seção 4 examinou as implicações de concorrência de múltiplos filtros em múltiplas CPUs. A Seção 5 avançou para MSI-X com vinculação de CPU por vetor. A Seção 6 codificou as funções de evento por vetor. A Seção 7 consolidou o teardown. A Seção 8 refatorou para o layout final.

O que o Capítulo 20 não fez foi DMA. O handler de cada vetor ainda apenas acessa registradores; o dispositivo ainda não tem a capacidade de alcançar a RAM. É no Capítulo 21 que isso muda. DMA introduz novas complicações (coerência, scatter-gather, mapeamento) que interagem com interrupções (interrupções de conclusão sinalizam que uma transferência DMA foi concluída). A maquinaria de interrupções do Capítulo 20 está pronta para lidar com interrupções de conclusão; o Capítulo 21 escreve o lado DMA.

O layout de arquivos cresceu: 14 arquivos-fonte (incluindo `cbuf`), 6 arquivos de documentação (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`), e um conjunto de testes de regressão em crescimento. O driver é estruturalmente paralelo a drivers FreeBSD de produção neste ponto.

### Uma Reflexão Antes do Capítulo 21

O Capítulo 20 foi o último capítulo da Parte 4 dedicado exclusivamente a interrupções. O Capítulo 21 avança para DMA, que trata de mover dados. Os dois são complementares: interrupções sinalizam eventos; DMA move os dados sobre os quais esses eventos tratam. Um driver de alto desempenho usa ambos juntos: descritores de recepção são preenchidos por DMA do dispositivo para a RAM, e então uma interrupção de conclusão sinaliza ao driver que deve processar os descritores.

Os handlers por vetor do Capítulo 20 já têm o formato certo para isso. A interrupção de conclusão de cada fila de recepção dispara seu próprio vetor; o filtro de cada vetor confirma e adia a tarefa; a tarefa percorre o anel de recepção (preenchido por DMA, Capítulo 21) e passa os pacotes para cima. O Capítulo 21 escreve o lado DMA; o lado de interrupções do Capítulo 20 já está no lugar.

O ensinamento do capítulo também se generaliza. Um leitor que internalizou a escada de fallback de três níveis do Capítulo 20, o design de estado por vetor, a vinculação de CPU e o teardown limpo encontrará padrões semelhantes em todo driver FreeBSD com múltiplas filas. O dispositivo específico difere; a estrutura não.

### O Que Fazer Se Você Estiver Travado

Três sugestões.

Primeiro, leia `/usr/src/sys/dev/virtio/pci/virtio_pci.c` cuidadosamente, focando na família de funções `vtpci_alloc_intr_resources`. O padrão corresponde exatamente ao Capítulo 20, e o código é compacto o suficiente para ser lido em uma única sessão.

Segundo, execute o teste de regressão do capítulo tanto em um guest bhyve (fallback legado) quanto em um guest QEMU (MSI-X). Ver o mesmo driver se comportar corretamente em ambos os alvos confirma que a escada de fallback está correta.

Terceiro, pule os desafios na primeira passagem. Os laboratórios são calibrados para o ritmo do Capítulo 20; os desafios pressupõem que o material está sólido. Volte a eles após o Capítulo 21 se parecerem inacessíveis agora.

O objetivo do Capítulo 20 era dar ao driver um caminho de interrupção com múltiplos vetores. Se isso foi alcançado, o trabalho de DMA do Capítulo 21 se torna um complemento em vez de um tópico completamente novo.



## Ponte para o Capítulo 21

O Capítulo 21 tem o título *DMA e Transferência de Dados em Alta Velocidade*. Seu escopo é o tópico que o Capítulo 20 deliberadamente não abordou: a capacidade do dispositivo de ler e escrever na RAM diretamente, sem que o driver precise intermediar cada palavra. Uma NIC com um anel de descritores de recepção de 64 entradas preenche essas entradas por DMA a partir da rede; uma única interrupção sinaliza "N entradas estão prontas". O handler do driver percorre o anel e processa as entradas. Sem DMA o driver teria que ler cada byte de um registrador de dispositivo, o que não escala.

O Capítulo 20 preparou o terreno de três maneiras específicas.

Primeiro, **você tem interrupções de conclusão por vetor**. As conclusões de recepção e transmissão de cada fila podem disparar um vetor dedicado. O trabalho de anel DMA do Capítulo 21 se conecta ao filtro e à tarefa por vetor do Capítulo 20; o filtro vê "conclusões N a M estão prontas" e a tarefa as processa.

Segundo, **você tem posicionamento de handler por CPU**. A memória de um anel DMA fica em um nó NUMA específico; o handler que a processa deve ser executado em uma CPU desse nó. O trabalho de `bus_bind_intr` do Capítulo 20 é o mecanismo. O Capítulo 21 estende isso: a memória DMA também é alocada com consciência de NUMA, de modo que o anel, o handler e o processamento acabam todos no mesmo nó.

Terceiro, **você tem a disciplina de teardown**. DMA adiciona mais recursos (tags DMA, mapas DMA, regiões de memória DMA), e cada um precisa de seu próprio passo de teardown. O padrão de teardown por vetor do Capítulo 19/Capítulo 20 se estende naturalmente para a limpeza DMA por fila.

Tópicos específicos que o Capítulo 21 cobrirá:

- O que é DMA, a diferença entre I/O mapeado em memória e DMA.
- `bus_dma(9)`: tags, mapas e a máquina de estados DMA.
- `bus_dma_tag_create` para descrever os requisitos de DMA (alinhamento, limites, faixa de endereços).
- `bus_dmamap_create` e `bus_dmamap_load` para configurar transferências DMA.
- Sincronização: `bus_dmamap_sync` ao redor de DMA.
- Bounce buffers: o que são e quando são usados.
- Coerência de cache: por que CPUs e dispositivos veem memória diferente em momentos diferentes.
- Listas scatter-gather: endereços físicos que não são contíguos.
- Ring buffers: o padrão de anel de descritores produtor-consumidor.

Você não precisa ler adiante. O Capítulo 20 é preparação suficiente. Traga seu driver `myfirst` em `1.3-msi`, seu `LOCKING.md`, seu `INTERRUPTS.md`, seu `MSIX.md`, seu kernel com `WITNESS` habilitado e seu script de regressão. O Capítulo 21 começa onde o Capítulo 20 terminou.

A conversa sobre hardware está se aprofundando. O vocabulário é seu; a estrutura é sua; a disciplina é sua. O Capítulo 21 adiciona a próxima peça que falta: a capacidade do dispositivo de mover dados sem precisar pedir.



## Referência: Cartão de Referência Rápida do Capítulo 20

Um resumo compacto do vocabulário, APIs, macros e procedimentos que o Capítulo 20 introduziu.

### Vocabulário

- **MSI (Message Signalled Interrupts)**: mecanismo PCI 2.2. De 1 a 32 vetores, contíguos, endereço único.
- **MSI-X**: mecanismo PCIe. Até 2048 vetores, endereço por vetor, máscara por vetor, tabela em um BAR.
- **vector**: uma única fonte de interrupção identificada por um índice.
- **rid**: o ID de recurso usado com `bus_alloc_resource_any`. 0 para INTx legado, 1 ou mais para MSI e MSI-X.
- **intr_mode**: o registro do driver sobre qual nível está sendo usado (legado, MSI ou MSI-X).
- **fallback ladder**: tenta MSI-X primeiro, depois MSI, depois INTx legado.
- **per-vector state**: contadores, filtro, tarefa, cookie, recurso por vetor.
- **CPU binding**: roteamento de um vetor para uma CPU específica via `bus_bind_intr`.
- **LOCAL_CPUS / INTR_CPUS**: consultas de conjunto de CPUs para posicionamento ciente de NUMA.

### APIs Essenciais

- `pci_msi_count(dev)`: consulta a quantidade de vetores MSI.
- `pci_msix_count(dev)`: consulta a quantidade de vetores MSI-X.
- `pci_alloc_msi(dev, &count)`: aloca vetores MSI.
- `pci_alloc_msix(dev, &count)`: aloca vetores MSI-X.
- `pci_release_msi(dev)`: libera vetores MSI ou MSI-X.
- `pci_msix_table_bar(dev)`, `pci_msix_pba_bar(dev)`: identifica os BARs da tabela e do PBA.
- `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)`: aloca o recurso de IRQ por vetor.
- `bus_setup_intr(dev, res, flags, filter, ihand, arg, &cookie)`: registra o handler por vetor.
- `bus_teardown_intr(dev, res, cookie)`: remove o registro do handler por vetor.
- `bus_describe_intr(dev, res, cookie, "name")`: rotula o handler por vetor.
- `bus_bind_intr(dev, res, cpu)`: vincula o vetor a um CPU específico.
- `bus_get_cpus(dev, op, size, &set)`: consulta os CPUs locais ao nó NUMA (op = `LOCAL_CPUS` ou `INTR_CPUS`).

### Macros Essenciais

- `PCIY_MSI = 0x05`: ID de capacidade MSI.
- `PCIY_MSIX = 0x11`: ID de capacidade MSI-X.
- `PCIM_MSIXCTRL_TABLE_SIZE = 0x07FF`: máscara para a contagem de vetores.
- `PCI_MSIX_MSGNUM(ctrl)`: macro para extrair a contagem de vetores do registrador de controle.
- `MYFIRST_MAX_VECTORS`: constante definida pelo driver (3 no Capítulo 20).

### Procedimentos Comuns

**Implementar a escada de fallback em três níveis:**

1. `pci_msix_count(dev)`; se > 0, tentar `pci_alloc_msix`.
2. Em caso de falha, `pci_msi_count(dev)`; se > 0, tentar `pci_alloc_msi`.
3. Em caso de falha, recorrer ao INTx legado com `rid = 0` e `RF_SHAREABLE`.

**Registrar handlers por vetor (MSI-X):**

1. Iterar de `i = 0` até `num_vectors - 1`.
2. Para cada um: `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)` com `rid = i + 1`.
3. `bus_setup_intr(dev, vec->irq_res, INTR_TYPE_MISC | INTR_MPSAFE, vec->filter, NULL, vec, &vec->intr_cookie)`.
4. `bus_describe_intr(dev, vec->irq_res, vec->intr_cookie, vec->name)`.
5. `bus_bind_intr(dev, vec->irq_res, target_cpu)`.

**Desmontar um driver com múltiplos vetores:**

1. Limpar `INTR_MASK` no dispositivo.
2. Para cada vetor (em ordem inversa): `bus_teardown_intr`, `bus_release_resource`.
3. Drenar cada task por vetor.
4. Liberar o taskqueue.
5. `pci_release_msi(dev)` se MSI ou MSI-X estava em uso.

### Comandos Úteis

- `pciconf -lvc`: listar dispositivos com suas listas de capacidades.
- `vmstat -i`: exibir contagens de interrupções por handler.
- `cpuset -g -x <irq>`: consultar a afinidade de CPU para um IRQ.
- `cpuset -l <cpu> -x <irq>`: definir a afinidade de CPU para um IRQ.
- `sysctl dev.myfirst.0.intr_mode`: consultar o modo de interrupção do driver.

### Arquivos para Manter como Referência

- `/usr/src/sys/dev/pci/pcivar.h`: wrappers inline para MSI/MSI-X.
- `/usr/src/sys/dev/pci/pcireg.h`: IDs de capacidades e campos de bits.
- `/usr/src/sys/dev/pci/pci.c`: implementação no kernel de `pci_alloc_msi`/`msix`.
- `/usr/src/sys/dev/virtio/pci/virtio_pci.c`: exemplo limpo de escada de fallback MSI-X.
- `/usr/src/sys/dev/nvme/nvme_ctrlr.c`: padrão MSI-X por fila em escala.



## Referência: Glossário dos Termos do Capítulo 20

**affinity**: o mapeamento de um vetor de interrupção para uma CPU específica (ou conjunto de CPUs).

**bus_bind_intr(9)**: função para rotear um vetor de interrupção para uma CPU específica.

**bus_get_cpus(9)**: função para consultar os conjuntos de CPUs associados a um dispositivo (local, adequadas para tratamento de interrupções).

**capability list**: a lista encadeada de capacidades de um dispositivo PCI no espaço de configuração.

**coalescing**: agrupamento de múltiplos eventos em uma única interrupção para reduzir a taxa de interrupções.

**cookie**: o handle opaco retornado por `bus_setup_intr(9)`, usado por `bus_teardown_intr(9)`.

**fallback ladder**: a sequência MSI-X → MSI → INTx legado que os drivers implementam.

**intr_mode**: enum no softc que registra qual nível de interrupção está ativo.

**INTR_CPUS**: valor do enum `cpu_sets`; CPUs adequadas para tratar interrupções do dispositivo.

**LOCAL_CPUS**: valor do enum `cpu_sets`; CPUs no mesmo domínio NUMA que o dispositivo.

**MSI**: Message Signalled Interrupts, PCI 2.2.

**MSI-X**: o mecanismo mais completo, PCIe.

**moderation**: buffer de interrupções no nível do dispositivo para trocar latência por throughput.

**NUMA**: Non-Uniform Memory Access; arquitetura de sistemas com múltiplos sockets.

**per-vector state**: os campos do softc específicos de um vetor (contadores, filtro, task, cookie, recurso).

**pci_msi_count(9) / pci_msix_count(9)**: consultas à contagem de capacidades.

**pci_alloc_msi(9) / pci_alloc_msix(9)**: alocação de vetores.

**pci_release_msi(9)**: liberação de MSI/MSI-X (trata ambos).

**rid**: resource ID (identificador de recurso). 0 para INTx legado, 1 ou mais para vetores MSI/MSI-X.

**stray interrupt**: uma interrupção que nenhum filtro reivindica.

**taskqueue**: primitiva de trabalho diferido do FreeBSD.

**vector**: uma única fonte de interrupção no mecanismo MSI ou MSI-X.

**vmstat -i**: diagnóstico que exibe contagens de interrupções por handler.



## Referência: O Passo a Passo Completo do myfirst_msix.c no Estágio 4

Para os leitores que desejam ter em um único lugar o arquivo final de múltiplos vetores com anotações, este apêndice percorre o `myfirst_msix.c` dos exemplos que acompanham o livro, mostrando cada função e explicando as escolhas de design.

### O Início do Arquivo

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/types.h>
#include <sys/smp.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"
#include "myfirst_msix.h"
```

A lista de includes é mais longa que a de `myfirst_intr.c`: `<dev/pci/pcireg.h>` e `<dev/pci/pcivar.h>` para a API MSI/MSI-X, `<sys/smp.h>` para `mp_ncpus`, e `<machine/atomic.h>` para os incrementos dos contadores por vetor. Note que `<dev/pci/pcireg.h>` é incluído mesmo que o arquivo não use diretamente `PCIY_MSI` ou constantes similares; as funções inline acessoras em `pcivar.h` dependem dele.

### Os Helpers por Vetor

```c
static int
myfirst_msix_setup_vector(struct myfirst_softc *sc, int idx, int rid)
{
	struct myfirst_vector *vec = &sc->vectors[idx];
	int error;

	vec->irq_rid = rid;
	vec->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &vec->irq_rid, RF_ACTIVE);
	if (vec->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, vec->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    vec->filter, NULL, vec, &vec->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    vec->irq_rid, vec->irq_res);
		vec->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
	    "%s", vec->name);
	return (0);
}

static void
myfirst_msix_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    vec->irq_rid, vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

Esses helpers formam um par simétrico da Seção 3. Cada um recebe um índice de vetor e opera no `vec` daquela posição. O helper de configuração é idempotente no sentido de que deixa o vetor em estado limpo em caso de falha; o helper de desmontagem é seguro para chamar mesmo que a configuração não tenha sido concluída.

### As Funções de Filtro por Vetor

Os três filtros diferem apenas no bit que verificam. Sua forma comum:

```c
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_DATA_AV) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_DATA_AV);
	atomic_add_64(&sc->intr_data_av_count, 1);
	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}
```

O filtro de administração verifica `MYFIRST_INTR_ERROR`, o filtro de transmissão verifica `MYFIRST_INTR_COMPLETE`. Cada um incrementa o contador global adequado e o contador por vetor. Apenas o filtro de recepção enfileira uma task.

### A Task de RX

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

A task é executada no contexto de thread e adquire `sc->mtx` com segurança. Ela verifica `sc->pci_attached` antes de tocar em estado compartilhado, protegendo contra uma condição de corrida em que a task seria executada durante o detach.

### A Função Principal de Configuração

A função de configuração orquestra a escada de fallback:

```c
int
myfirst_msix_setup(struct myfirst_softc *sc)
{
	int error, wanted, allocated, i;

	/* Initialise per-vector state common to all tiers. */
	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		sc->vectors[i].id = i;
		sc->vectors[i].sc = sc;
	}
	TASK_INIT(&sc->vectors[MYFIRST_VECTOR_RX].task, 0,
	    myfirst_msix_rx_task_fn,
	    &sc->vectors[MYFIRST_VECTOR_RX]);
	sc->vectors[MYFIRST_VECTOR_RX].has_task = true;
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_msix_admin_filter;
	sc->vectors[MYFIRST_VECTOR_RX].filter = myfirst_msix_rx_filter;
	sc->vectors[MYFIRST_VECTOR_TX].filter = myfirst_msix_tx_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].name = "admin";
	sc->vectors[MYFIRST_VECTOR_RX].name = "rx";
	sc->vectors[MYFIRST_VECTOR_TX].name = "tx";

	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	wanted = MYFIRST_MAX_VECTORS;

	/* Tier 0: MSI-X. */
	if (pci_msix_count(sc->dev) >= wanted) {
		allocated = wanted;
		if (pci_alloc_msix(sc->dev, &allocated) == 0 &&
		    allocated == wanted) {
			for (i = 0; i < wanted; i++) {
				error = myfirst_msix_setup_vector(sc, i,
				    i + 1);
				if (error != 0) {
					for (i -= 1; i >= 0; i--)
						myfirst_msix_teardown_vector(
						    sc, i);
					pci_release_msi(sc->dev);
					goto try_msi;
				}
			}
			sc->intr_mode = MYFIRST_INTR_MSIX;
			sc->num_vectors = wanted;
			myfirst_msix_bind_vectors(sc);
			device_printf(sc->dev,
			    "interrupt mode: MSI-X, %d vectors\n", wanted);
			goto enabled;
		}
		if (allocated > 0)
			pci_release_msi(sc->dev);
	}

try_msi:
	/*
	 * Tier 1: MSI with a single vector. MSI requires a power-of-two
	 * count, so we cannot request MYFIRST_MAX_VECTORS (3) here. We
	 * request 1 vector and fall back to the Chapter 19 single-handler
	 * pattern, matching the approach sys/dev/virtio/pci/virtio_pci.c
	 * takes in vtpci_alloc_msi().
	 */
	allocated = 1;
	if (pci_msi_count(sc->dev) >= 1 &&
	    pci_alloc_msi(sc->dev, &allocated) == 0 && allocated >= 1) {
		sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
		sc->vectors[MYFIRST_VECTOR_ADMIN].name = "msi";
		error = myfirst_msix_setup_vector(sc, MYFIRST_VECTOR_ADMIN, 1);
		if (error == 0) {
			sc->intr_mode = MYFIRST_INTR_MSI;
			sc->num_vectors = 1;
			device_printf(sc->dev,
			    "interrupt mode: MSI, 1 vector "
			    "(single-handler fallback)\n");
			goto enabled;
		}
		pci_release_msi(sc->dev);
	}

try_legacy:
	/* Tier 2: legacy INTx. */
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid = 0;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = bus_alloc_resource_any(
	    sc->dev, SYS_RES_IRQ,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res == NULL) {
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res);
		sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	sc->num_vectors = 1;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (1 handler for all events)\n");

enabled:
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

A função é longa porque lida com três níveis, cada um com sua própria alocação, loop de configuração por vetor e desfazimento em caso de falha parcial. Um leitor que acompanha o fluxo vê o MSI-X sendo tentado primeiro, caindo para o MSI em qualquer falha, e então caindo para o legado em qualquer falha nesse nível. O rótulo `enabled:` é alcançado a partir de qualquer nível bem-sucedido.

O nível legado é o caminho do Capítulo 19: um filtro (`myfirst_intr_filter` de `myfirst_intr.c`), `rid = 0`, `RF_SHAREABLE`. Os contadores por vetor não são realmente usados nesse nível; o código do Capítulo 19 faz sua própria contagem.

### A Função de Teardown

```c
void
myfirst_msix_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	for (i = sc->num_vectors - 1; i >= 0; i--)
		myfirst_msix_teardown_vector(sc, i);

	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

A função segue a ordenação estrita: desabilitar no dispositivo, desmontagem por vetor em ordem inversa, drenagem da task por vetor, liberação do taskqueue, liberação do MSI. Sem surpresas; a simetria é a recompensa.

### A Função de Bind

```c
static void
myfirst_msix_bind_vectors(struct myfirst_softc *sc)
{
	int i, cpu;
	int err;

	if (mp_ncpus < 2)
		return;

	for (i = 0; i < sc->num_vectors; i++) {
		cpu = i % mp_ncpus;
		err = bus_bind_intr(sc->dev, sc->vectors[i].irq_res, cpu);
		if (err != 0)
			device_printf(sc->dev,
			    "bus_bind_intr vec %d: %d\n", i, err);
	}
}
```

Vinculação em round-robin. Chamada apenas em MSI-X (a função não é útil em MSI ou no nível legado; a escada de configuração a ignora nesses casos). Em sistemas com uma única CPU, a função retorna antecipadamente sem realizar a vinculação.

### A Função sysctl

```c
void
myfirst_msix_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);
	char name[32];
	int i;

	SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "intr_mode",
	    CTLFLAG_RD, &sc->intr_mode, 0,
	    "0=legacy, 1=MSI, 2=MSI-X");

	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		snprintf(name, sizeof(name), "vec%d_fire_count", i);
		SYSCTL_ADD_U64(ctx, kids, OID_AUTO, name,
		    CTLFLAG_RD, &sc->vectors[i].fire_count, 0,
		    "Times this vector's filter was called");
		snprintf(name, sizeof(name), "vec%d_stray_count", i);
		SYSCTL_ADD_U64(ctx, kids, OID_AUTO, name,
		    CTLFLAG_RD, &sc->vectors[i].stray_count, 0,
		    "Stray returns from this vector");
	}

	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_admin",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate admin vector interrupt");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_rx",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_RX], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate rx vector interrupt");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_tx",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_TX], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate tx vector interrupt");
}
```

A função constrói três sysctls de contadores por vetor somente leitura e três sysctls de interrupções simuladas somente escrita. O estilo em árvore (Desafio 7) fica como exercício.

### Linhas de Código

O arquivo completo `myfirst_msix.c` tem cerca de 330 linhas. É um acréscimo substancial ao driver, mas compra todas as capacidades do Capítulo 20: fallback em três níveis, handlers por vetor, contadores por vetor, vinculação de CPU e desmontagem limpa.

Compare com o `myfirst_intr.c` do Capítulo 19, que tinha cerca de 250 linhas. O arquivo do Capítulo 20 não é muito maior em termos absolutos; a lógica por vetor adiciona complexidade, mas cada peça é pequena.



## Referência: Uma Nota Final sobre a Filosofia Multi-Vetor

Um parágrafo para encerrar o capítulo.

Um driver multi-vetor não difere fundamentalmente de um driver de vetor único. Ele tem o mesmo formato de filtro, o mesmo padrão de task, a mesma ordenação no teardown, a mesma disciplina de lock. O que muda é a contagem: N filtros em vez de um, N teardowns em vez de um, N tasks em vez de uma. A qualidade do design vem de quão limpa é a coexistência dessas N peças.

A lição do Capítulo 20 é que o tratamento multi-vetor é um exercício de simetria. Cada vetor se parece com todos os outros no nível estrutural; cada um tem seu próprio contador, seu próprio filtro, sua própria descrição. O código que aloca, o código que trata, o código que desmonta: todos percorrem os vetores em loop e fazem a mesma coisa N vezes. A simplicidade do loop é o que torna o driver de N vetores gerenciável; um driver em que cada vetor é especial é um driver que não escala.

Para este leitor e para os futuros leitores deste livro, o padrão multi-vetor do Capítulo 20 é uma parte permanente da arquitetura do driver `myfirst` e uma ferramenta permanente no conjunto de habilidades do leitor. O Capítulo 21 o pressupõe: anéis DMA por fila, interrupções de conclusão por fila, posicionamento de CPU por fila. O vocabulário é o vocabulário que todo driver FreeBSD de alto desempenho compartilha; os padrões são os padrões que os próprios drivers de teste do kernel utilizam; a disciplina é a disciplina com que os drivers de produção operam.

A habilidade que o Capítulo 20 ensina não é "como alocar MSI-X para virtio-rng-pci". É "como projetar um driver multi-vetor, alocar seus vetores, posicioná-los em CPUs, rotear eventos por vetor e desmontar tudo isso de forma limpa". Essa habilidade se aplica a todo dispositivo multi-fila em que o leitor venha a trabalhar.
