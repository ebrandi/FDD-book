---
title: "Tratamento de Interrupções"
description: "O Capítulo 19 transforma o driver PCI do Capítulo 18 em um driver com suporte a interrupções. Ele ensina o que são interrupções, como o FreeBSD as modela e roteia, como um driver reivindica um recurso IRQ e registra um handler por meio de bus_setup_intr(9), como dividir o trabalho entre um filtro rápido e uma ithread adiada, como tratar IRQs compartilhados com segurança usando FILTER_STRAY e FILTER_HANDLED, como simular interrupções para testes sem eventos IRQ reais e como desfazer o handler no detach. O driver evolui de 1.1-pci para 1.2-intr, ganha um novo arquivo específico para interrupções e deixa o Capítulo 19 preparado para MSI e MSI-X no Capítulo 20."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 19
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Tratamento de Interrupções

## Orientações ao Leitor e Objetivos

O Capítulo 18 terminou com um driver que havia finalmente encontrado hardware PCI de verdade. O módulo `myfirst` na versão `1.1-pci` faz o probe de um dispositivo PCI por vendor e device ID, faz o attach como filho legítimo do newbus em `pci0`, reivindica a BAR do dispositivo por meio de `bus_alloc_resource_any(9)` com `SYS_RES_MEMORY` e `RF_ACTIVE`, entrega a BAR à camada de acesso do Capítulo 16 para que `CSR_READ_4` e `CSR_WRITE_4` leiam e escrevam no silício real, cria um cdev por instância e desmonta tudo em ordem estritamente inversa no detach. A simulação do Capítulo 17 ainda está na árvore, mas não é executada no caminho PCI; seus callouts ficam silenciosos para não escreverem nos registradores reais do dispositivo.

O que o driver ainda não faz é reagir ao dispositivo. Até agora, todos os acessos a registradores foram iniciados pelo driver: um `read` ou `write` em espaço do usuário atinge o cdev, o handler do cdev adquire `sc->mtx`, o accessor lê ou escreve na BAR e o controle retorna ao espaço do usuário. Se o próprio dispositivo tiver algo a dizer, como "minha fila de recepção tem um pacote", "um comando foi concluído" ou "um limiar de temperatura foi ultrapassado", o driver não tem como ouvi-lo. O driver faz polling; ele não escuta.

É isso que o Capítulo 19 corrige. Dispositivos reais se comunicam **interrompendo** a CPU. O barramento transporta um sinal do dispositivo ao controlador de interrupções, o controlador de interrupções despacha o sinal para uma CPU, a CPU faz um breve desvio do que estava fazendo e um handler que o driver registrou executa por alguns microssegundos. O trabalho do handler é pequeno: descobrir o que o dispositivo quer, reconhecer a interrupção no dispositivo, fazer a pequena quantidade de trabalho que é seguro realizar ali e passar o restante para uma thread que tem liberdade para bloquear, dormir ou adquirir locks lentos. Esse "repasse" é a segunda metade da disciplina moderna de interrupções; a primeira metade é chegar ao handler.

O escopo do Capítulo 19 é precisamente o caminho central das interrupções: o que são interrupções no nível do hardware, como o FreeBSD as modela no kernel, como um driver aloca um recurso IRQ e registra um handler por meio de `bus_setup_intr(9)`, como funciona a divisão entre um handler de filtro rápido e um handler ithread diferido, a que o flag `INTR_MPSAFE` compromete o driver, como simular interrupções para testes quando eventos reais não são fáceis de produzir, como se comportar corretamente em uma linha IRQ compartilhada que outros drivers também escutam e como desmontar tudo isso no detach sem vazar recursos nem executar handlers sobre estado já liberado. O capítulo não avança até MSI e MSI-X, que pertencem ao Capítulo 20; esses mecanismos se apoiam no handler central que o leitor escreve aqui, e ensinar os dois ao mesmo tempo prejudicaria o aprendizado de ambos.

O Capítulo 19 não avança sobre o território que o trabalho com interrupções naturalmente toca. MSI e MSI-X, handlers por vetor, coalescing de interrupções e roteamento de interrupções por fila são assuntos do Capítulo 20. DMA e a interação entre interrupções e anéis de descritores DMA são os Capítulos 20 e 21. Estratégias avançadas de afinidade de interrupções em plataformas NUMA são mencionadas brevemente, mas o tratamento aprofundado pertence ao Capítulo 20 e além. O roteamento de interrupções específico de plataforma (GICv3 em arm64, APIC em x86, NVIC em alvos embarcados) é mencionado apenas para fins de vocabulário; o foco do livro é a API visível ao driver que oculta essas diferenças. O Capítulo 19 permanece dentro do terreno que consegue cobrir bem e repassa explicitamente os tópicos que merecem um capítulo próprio.

O modelo filtro mais ithread que o Capítulo 19 ensina não existe de forma isolada. O Capítulo 16 deu ao driver um vocabulário de acesso a registradores. O Capítulo 17 o ensinou a pensar como um dispositivo. O Capítulo 18 o apresentou a um dispositivo PCI real. O Capítulo 19 lhe dá ouvidos. Os Capítulos 20 e 21 lhe darão pernas: acesso direto à memória para que o dispositivo possa alcançar a RAM sem o driver como intermediário. Cada capítulo acrescenta uma camada. Cada camada depende das anteriores. O Capítulo 19 é onde o driver para de fazer polling e começa a escutar, e as disciplinas que a Parte 3 construiu são o que mantém essa escuta honesta.

### Por que o Tratamento de Interrupções Merece um Capítulo Próprio

Uma dúvida que surge aqui é se `bus_setup_intr(9)` e o modelo filtro mais ithread realmente justificam um capítulo completo. A simulação do Capítulo 17 usou callouts para produzir mudanças de estado autônomas; o driver do Capítulo 18 roda em PCI real, mas ignora completamente a linha de interrupção. Não poderíamos simplesmente continuar fazendo polling com callouts e evitar o assunto?

Dois motivos.

O primeiro é desempenho. Um callout que faz polling no dispositivo dez vezes por segundo desperdiça tempo de CPU quando não há nada a fazer e perde eventos que ocorrem entre as sondagens. Um dispositivo real pode produzir muitos eventos por milissegundo; um intervalo de polling de 100 milissegundos perde quase todos eles. As interrupções invertem o custo: nenhuma CPU é consumida quando nada está acontecendo, e o handler executa em questão de microssegundos após o evento. Todos os drivers sérios no FreeBSD usam interrupções pelo mesmo motivo; um driver que faz polling é um driver com uma justificativa especial.

O segundo é correção. Alguns dispositivos exigem que o driver responda dentro de uma janela de tempo estreita. O FIFO de recepção de uma placa de rede se enche em poucos microssegundos; se o driver não o esvaziar, a placa descarta pacotes. O FIFO de transmissão de uma porta serial se esvazia na taxa da linha; se o driver não o reabastecer, o transmissor fica sem dados. Fazer polling em qualquer intervalo longo o suficiente para ser barato é um intervalo curto o suficiente para perder prazos. As interrupções são o único mecanismo que permite ao driver atender requisitos de tempo real do dispositivo sem consumir uma CPU inteira o tempo todo.

O capítulo também se justifica por ensinar uma disciplina que se aplica muito além do PCI. O modelo de interrupções do FreeBSD (filtro mais ithread, `INTR_MPSAFE`, `bus_setup_intr(9)`, desmontagem limpa no detach) é o mesmo modelo que drivers USB usam, o mesmo que drivers SDIO usam, o mesmo que drivers virtio usam e o mesmo que drivers de SoC arm64 usam. Um leitor que entende o modelo do Capítulo 19 consegue ler o handler de interrupção de qualquer driver FreeBSD com compreensão. Essa generalidade é o que torna o capítulo digno de uma leitura cuidadosa, mesmo para leitores que não trabalharão com PCI.

### Onde o Capítulo 18 Deixou o Driver

Um breve ponto de verificação antes de continuar. O Capítulo 19 estende o driver produzido ao final do Estágio 4 do Capítulo 18, identificado como versão `1.1-pci`. Se algum dos itens abaixo parecer incerto, retorne ao Capítulo 18 antes de começar este capítulo.

- Seu driver compila sem erros e se identifica como `1.1-pci` em `kldstat -v`.
- Em um guest bhyve ou QEMU que expõe um dispositivo virtio-rnd (vendor `0x1af4`, device `0x1005`), o driver faz attach por meio de `myfirst_pci_probe` e `myfirst_pci_attach`, imprime seu banner, reivindica a BAR 0 como `SYS_RES_MEMORY` com `RF_ACTIVE`, percorre a lista de capabilities PCI e cria `/dev/myfirst0`.
- O softc mantém o ponteiro para o recurso BAR (`sc->bar_res`), o ID do recurso (`sc->bar_rid`) e o flag `pci_attached`.
- O caminho de detach destrói o cdev, quiesce quaisquer callouts e tasks ativos, desfaz o attach da camada de hardware, libera a BAR e desinicializa o softc.
- O script completo de regressão do Capítulo 18 passa: attach, exercitar o cdev, detach, unload, sem vazamentos.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md` e `PCI.md` estão atualizados.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados em seu kernel de teste.

Esse driver é o que o Capítulo 19 estende. As adições são novamente modestas em volume: um novo arquivo (`myfirst_intr.c`), um novo cabeçalho (`myfirst_intr.h`), um pequeno conjunto de novos campos no softc (`irq_res`, `irq_rid`, `intr_cookie`, um ou dois contadores), três novas funções no arquivo de interrupções (setup, teardown e o handler de filtro), um sysctl para interrupções simuladas, um incremento de versão para `1.2-intr` e um breve documento `INTERRUPTS.md`. A mudança no modelo mental é novamente maior do que a contagem de linhas sugere: o driver finalmente tem dois fluxos de controle em vez de um, e a disciplina que os impede de interferir entre si é nova.

### O que Você Vai Aprender

Ao final deste capítulo, você será capaz de:

- Explicar o que é uma interrupção no nível do hardware, a diferença entre sinalização edge-triggered e level-triggered e como o fluxo de tratamento de interrupções de uma CPU percorre o caminho do dispositivo até o handler do driver.
- Descrever como o FreeBSD representa eventos de interrupção: o que é um evento de interrupção (`intr_event`), o que é uma thread de interrupção (`ithread`), o que é um handler de filtro e por que a divisão entre filtros e ithreads é importante.
- Ler a saída de `vmstat -i` e `devinfo -v` e localizar as interrupções que seu sistema está tratando, seus contadores e os drivers vinculados a cada uma.
- Alocar um recurso IRQ por meio de `bus_alloc_resource_any(9)` com `SYS_RES_IRQ`, usando `rid = 0` em uma linha PCI legada e, no Capítulo 20, RIDs não nulos para vetores MSI e MSI-X.
- Registrar um handler de interrupção por meio de `bus_setup_intr(9)`, escolhendo entre um handler de filtro (`driver_filter_t`), um handler ithread (`driver_intr_t`) ou a combinação filtro mais ithread, e selecionando o flag `INTR_TYPE_*` correto para a classe de trabalho do dispositivo.
- Escrever um handler de filtro mínimo que lê o registrador de status do dispositivo, reconhece a interrupção no dispositivo, retorna `FILTER_HANDLED` ou `FILTER_STRAY` conforme apropriado e coopera com o maquinário de interrupções do kernel.
- Saber o que é seguro dentro de um filtro (apenas spin locks, sem `malloc`, sem dormir, sem locks blocantes) e o que o ithread relaxa (sleep mutexes, variáveis de condição, `malloc(M_WAITOK)`), e por que essas restrições existem.
- Definir `INTR_MPSAFE` apenas quando realmente for necessário, e entender a que o flag compromete o driver (sincronização própria, sem aquisição implícita do Giant, direito de executar em qualquer CPU concorrentemente).
- Passar trabalho diferido de um handler de filtro para uma task de taskqueue ou para o ithread, e preservar a disciplina de que o trabalho pequeno e urgente acontece no filtro enquanto o trabalho em volume acontece em contexto de thread.
- Simular interrupções por meio de um sysctl que invoca o handler diretamente sob as regras normais de locking, para que você possa exercitar a máquina de estados do handler sem precisar de um IRQ real disparando.
- Tratar linhas de interrupção compartilhadas corretamente: ler primeiro o registrador INTR_STATUS do dispositivo, decidir se esta interrupção pertence ao nosso dispositivo, retornar `FILTER_STRAY` se não pertencer e evitar interferir no trabalho de outro driver.
- Desmontar o handler de interrupção no detach com `bus_teardown_intr(9)` antes de liberar o IRQ com `bus_release_resource(9)`, e estruturar o caminho de detach de forma que nenhuma interrupção possa disparar contra estado já liberado.
- Reconhecer o que é uma tempestade de interrupções (interrupt storm), saber como o maquinário `hw.intr_storm_threshold` do FreeBSD detecta uma e entender as causas comuns no lado do dispositivo (falha ao limpar INTR_STATUS, linhas edge-triggered mal configuradas como level).
- Vincular uma interrupção a uma CPU específica por meio de `bus_bind_intr(9)` quando a afinidade importa, e descrever a interrupção para `devinfo -v` por meio de `bus_describe_intr(9)` para que operadores possam ver qual handler está em qual CPU.
- Separar o código relacionado a interrupções em seu próprio arquivo, atualizar a linha `SRCS` do módulo, identificar o driver como `1.2-intr` e produzir um breve documento `INTERRUPTS.md` que descreve o comportamento do handler e a disciplina de trabalho diferido.

A lista é longa; cada item é preciso. O ponto do capítulo é a composição.

### O que Este Capítulo Não Aborda

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 19 permaneça focado.

- **MSI e MSI-X.** `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, alocação de vetores, handlers por vetor e o layout da tabela MSI-X são temas do Capítulo 20. O Capítulo 19 trata da linha PCI INTx legada, alocada com `rid = 0`; o vocabulário se transfere, mas a mecânica por vetor não.
- **DMA.** Tags de `bus_dma(9)`, listas scatter-gather, bounce buffers, coerência de cache em torno de descritores DMA e a forma como as interrupções sinalizam a conclusão de transferências em anel de descritores são temas dos Capítulos 20 e 21. O handler do Capítulo 19 lê um registrador BAR e decide o que fazer; ele não interage com DMA.
- **Redes multi-fila com filas independentes.** NICs modernas possuem filas de recepção e transmissão separadas, com vetores MSI-X e handlers de interrupção independentes por fila. O framework `iflib(9)` se apoia nessa arquitetura; `em(4)`, `ix(4)` e `ixl(4)` fazem uso dele. O driver do Capítulo 19 possui uma única interrupção; o Capítulo 20 em diante desenvolve a narrativa multi-fila.
- **Afinidade avançada de interrupção em hardware NUMA.** O `bus_bind_intr` é apresentado; estratégias elaboradas para fixar interrupções em CPUs próximas ao root port PCIe do dispositivo ficam reservadas para capítulos posteriores sobre escalabilidade.
- **Suspend e resume do driver em torno de interrupções.** `bus_suspend_intr(9)` e `bus_resume_intr(9)` existem; são mencionados por completude, mas não são exercitados no driver do Capítulo 19.
- **Manipulação da prioridade de interrupção em tempo real.** O `intr_priority(9)` do FreeBSD e as flags `INTR_TYPE_*` influenciam a prioridade do ithread, mas o livro trata o sistema de prioridades como uma caixa-preta fora dos capítulos de tópicos avançados.
- **Interrupções somente por software (SWI).** O `swi_add(9)` cria uma interrupção puramente por software que um driver pode agendar a partir de qualquer contexto. O capítulo menciona SWIs ao discutir trabalho diferido, mas o padrão moderno preferido (um taskqueue) cobre os mesmos casos de uso com menos armadilhas.

Manter o foco dentro desses limites faz do Capítulo 19 um capítulo dedicado ao tratamento central de interrupções. O vocabulário é o que se transfere; os capítulos específicos que se seguem aplicam esse vocabulário a MSI/MSI-X, DMA e designs multi-fila.

### Estimativa de Tempo Necessário

- **Somente leitura**: quatro a cinco horas. O modelo de interrupções é conceitualmente pequeno, mas exige leitura cuidadosa, em especial no que diz respeito ao split entre filter e ithread e às regras de segurança dentro de um filter.
- **Leitura mais digitação dos exemplos trabalhados**: dez a doze horas distribuídas em duas ou três sessões. O driver evolui em quatro estágios; cada estágio é uma extensão pequena, porém real, sobre a base de código do Capítulo 18.
- **Leitura mais todos os laboratórios e desafios**: dezesseis a vinte horas ao longo de quatro ou cinco sessões, incluindo a configuração do laboratório bhyve (caso o ambiente do Capítulo 18 ainda não esteja em operação), a leitura do caminho de interrupção de `if_em.c` e do filter handler de `if_mgb.c`, e a execução da regressão do Capítulo 19 tanto no caminho de interrupção simulada quanto, quando possível, no caminho de interrupção real.

As Seções 3, 4 e 6 são as mais densas. Se a divisão entre filter e ithread parecer estranha na primeira leitura, isso é completamente normal. Pare, releia a árvore de decisão da Seção 3 e continue quando o formato tiver se consolidado.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Capítulo 18 Estágio 4 (`1.1-pci`). O ponto de partida pressupõe a camada de hardware do Capítulo 16, o backend de simulação do Capítulo 17, o attach PCI do Capítulo 18, a família completa de acessores `CSR_*`, o cabeçalho de sincronização e todos os primitivos introduzidos na Parte 3.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco e correspondendo ao kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está construído, instalado e inicializando sem erros.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível, e o ambiente de laboratório do Capítulo 18 (um guest FreeBSD com um dispositivo virtio-rnd em `-s 4:0,virtio-rnd`) é reproduzível sob demanda.
- As ferramentas `devinfo(8)`, `vmstat(8)` e `pciconf(8)` estão no seu path. As três fazem parte do sistema base.

Se algum item acima estiver instável, corrija-o agora, em vez de avançar pelo Capítulo 19 tentando raciocinar sobre uma base que ainda está em movimento. Bugs de interrupção frequentemente se manifestam como kernel panics ou corrupção silenciosa sob carga; o `WITNESS` do kernel de depuração, em particular, captura as classes mais comuns de erros de lock logo no início.

### Como Aproveitar ao Máximo Este Capítulo

Quatro hábitos darão resultado rapidamente.

Primeiro, mantenha `/usr/src/sys/sys/bus.h` e `/usr/src/sys/kern/kern_intr.c` marcados como favoritos. O primeiro arquivo define `driver_filter_t`, `driver_intr_t`, `INTR_TYPE_*`, `INTR_MPSAFE` e os valores de retorno `FILTER_*` que você usará em todo handler. O segundo arquivo contém a maquinaria de eventos de interrupção do kernel: o código que recebe o IRQ de baixo nível, despacha para os filters, acorda as ithreads e detecta tempestades de interrupção. Você não precisa ler `kern_intr.c` em profundidade, mas percorrer os primeiros mil linhas uma vez oferece uma visão clara do que acontece entre "o dispositivo asserta o IRQ 19" e "seu filter é chamado".

Segundo, execute `vmstat -i` no seu host de laboratório e no seu guest, e mantenha a saída aberta em um terminal enquanto lê. Cada conceito que as Seções 2 e 3 introduzem (contagens por handler, afinidade por CPU, convenções de nomeação de interrupções) é visível nessa saída. Um leitor que já olhou atentamente para o `vmstat -i` da sua própria máquina acha o roteamento de interrupções muito menos abstrato.

Terceiro, digite as alterações à mão e execute cada estágio. Código de interrupção é o lugar onde pequenos erros se tornam bugs silenciosos. Esquecer `FILTER_HANDLED` torna seu handler tecnicamente inválido; esquecer `INTR_MPSAFE` adquire silenciosamente o Giant ao redor do seu handler; esquecer de limpar INTR_STATUS produz uma tempestade de interrupções cinco milissegundos depois. Digitar cada linha, verificar a saída do `dmesg` após cada `kldload` e observar o `vmstat -i` entre as iterações é a forma de capturar esses erros no momento em que ainda são baratos.

Quarto, leia `/usr/src/sys/dev/mgb/if_mgb.c` (procure por `mgb_legacy_intr` e `mgb_admin_intr`) após a Seção 4. `mgb(4)` é o driver para os controladores Gigabit Ethernet LAN743x da Microchip. Seu caminho de interrupção é um exemplo limpo e legível de design filter mais ithread, e está no nível de complexidade que o Capítulo 19 ensina. Setecentas linhas de leitura cuidadosa rendem bons resultados ao longo do restante da Parte 4.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **O Que São Interrupções?** O quadro do hardware: o que é uma interrupção, disparo por borda versus por nível, o fluxo de despacho da CPU e o mínimo que um driver deve fazer quando uma interrupção chega. A fundação conceitual.
2. **Interrupções no FreeBSD.** Como o kernel representa eventos de interrupção, o que é uma ithread, como as interrupções são contadas e exibidas por meio de `vmstat -i` e `devinfo -v`, e o que acontece desde a linha IRQ até o handler do driver.
3. **Registrando um Interrupt Handler.** O código que o driver escreve: `bus_alloc_resource_any(9)` com `SYS_RES_IRQ`, `bus_setup_intr(9)`, flags `INTR_TYPE_*`, `INTR_MPSAFE`, `bus_describe_intr(9)`. O primeiro estágio do driver do Capítulo 19 (`1.2-intr-stage1`).
4. **Escrevendo um Interrupt Handler Real.** A forma do filter handler: leia INTR_STATUS, decida a propriedade, confirme o dispositivo, retorne o valor `FILTER_*` correto. A forma do ithread handler: adquira sleep locks, delegue a uma taskqueue, execute o trabalho lento. Estágio 2 (`1.2-intr-stage2`).
5. **Usando Interrupções Simuladas para Testes.** Um sysctl que invoca o handler de forma síncrona com a disciplina de lock real, para que você possa exercitar o handler sem um IRQ real. Estágio 3 (`1.2-intr-stage3`).
6. **Tratando Interrupções Compartilhadas.** Por que `RF_SHAREABLE` importa em uma linha PCI legada, como um filter handler deve decidir a propriedade em relação a outros handlers no mesmo IRQ, e como evitar starvation. Sem avanço de estágio; esta é uma disciplina, não um novo artefato de código.
7. **Liberando Recursos de Interrupção.** `bus_teardown_intr(9)` primeiro, depois `bus_release_resource(9)`. A sequência de detach agora tem mais dois passos, e a cascata de falha parcial ganha mais um rótulo.
8. **Refatorando e Versionando Seu Driver com Suporte a Interrupções.** A divisão final em `myfirst_intr.c`, um novo `INTERRUPTS.md`, o avanço de versão para `1.2-intr` e a passagem de regressão. Estágio 4.

Após as oito seções vêm os laboratórios práticos, os exercícios desafio, uma referência de resolução de problemas, um Encerrando que fecha a história do Capítulo 19 e abre a do Capítulo 20, e uma ponte para o Capítulo 20. O material de referência e resumo ao final do capítulo foi pensado para ser relido enquanto você trabalha nos Capítulos 20 e 21; o vocabulário do Capítulo 19 é a fundação sobre a qual ambos são construídos.

Se esta é sua primeira leitura, avance linearmente e faça os laboratórios em ordem. Se estiver revisitando, as Seções 3 e 4 são independentes e funcionam bem como leituras de uma única sessão.



## Seção 1: O Que São Interrupções?

Antes do código do driver, o quadro do hardware. A Seção 1 ensina o que é uma interrupção no nível da CPU e do barramento, sem nenhum vocabulário específico do FreeBSD. Um leitor que compreende a Seção 1 pode ler o restante do capítulo tendo o caminho de interrupção do kernel como um objeto concreto, e não como uma abstração vaga. O retorno é que todas as seções seguintes ficam mais fáceis.

O resumo em uma frase, que você pode carregar durante todo o capítulo: uma interrupção é uma forma de um dispositivo interromper o trabalho atual da CPU, executar o handler de um driver por um curto período e, em seguida, deixar a CPU retomar o que estava fazendo. Todo o resto é mecanismo em torno dessa frase.

### O Problema que as Interrupções Resolvem

Uma CPU executa um fluxo de instruções em ordem. Cada instrução é concluída, o contador de programa avança, a próxima instrução é executada, e assim por diante. Deixada por conta própria, a CPU executaria um programa até o fim, depois outro, e assim sucessivamente, sem jamais prestar atenção em nada que acontecesse fora do seu próprio fluxo de instruções.

Não é assim que computadores funcionam. Uma tecla pressionada por meio segundo gera quatro ou cinco eventos distintos; um pacote de rede chega a poucos microssegundos do anterior; um disco conclui uma leitura, o controlador do ventilador ultrapassa um limiar de temperatura, o valor de um sensor é atualizado, um temporizador expira. Cada um desses eventos acontece fora do controle direto da CPU, em um momento que ela não escolheu. A CPU precisa percebê-los.

Uma forma de perceber é o polling. A CPU pode consultar o registrador de status do dispositivo periodicamente. Se o registrador de status indicar "tenho dados", a CPU lê os dados. Se indicar "não tenho nada", a CPU segue em frente. O polling funciona para dispositivos cujos eventos são raros, previsíveis e não sensíveis ao tempo. Funciona mal para todo o resto. Um teclado verificado a cada cem milissegundos parece lento. Uma placa de rede verificada a cada milissegundo ainda perde a maioria dos pacotes. E o polling consome tempo de CPU proporcional à taxa de verificação, mesmo quando nada está acontecendo.

A outra forma de perceber é deixar o dispositivo avisar a CPU. Isso é uma interrupção. O dispositivo levanta um sinal em um fio ou envia uma mensagem pelo barramento. A CPU interrompe seu trabalho atual, registra onde estava, executa um pequeno trecho de código que pergunta ao dispositivo o que aconteceu, responde adequadamente e, em seguida, retoma o trabalho que estava fazendo. A disciplina para escrever esse "pequeno trecho de código" é o que o restante do Capítulo 19 ensina.

### O Que É Uma Interrupção de Hardware de Fato

Fisicamente, uma interrupção de hardware começa como um sinal em um fio (ou, mais comumente em sistemas modernos, como uma mensagem em um barramento). Um dispositivo asserta o sinal quando algo aconteceu e que o sistema operacional precisa saber. Exemplos:

- Uma placa de rede asserta sua linha IRQ quando um pacote chegou e está esperando no seu FIFO de recepção.
- Um UART serial asserta sua linha IRQ quando um byte chegou no receptor, ou quando o FIFO de transmissão caiu abaixo de um limiar.
- Um controlador SATA asserta sua linha IRQ quando uma entrada da fila de comandos foi concluída.
- Um chip de temporizador asserta sua linha IRQ quando um intervalo programado expirou.
- Um sensor de temperatura asserta sua linha IRQ quando um limiar programado é ultrapassado.

A assertiva é a forma que o dispositivo tem de dizer "algo que você precisa saber aconteceu". A CPU e o sistema operacional devem estar preparados para responder. O caminho desde "sinal assertado" até "handler chamado" passa pelo controlador de interrupções, pelo mecanismo de despacho de interrupções da CPU e pela maquinaria de eventos de interrupção do kernel. A Seção 2 percorre esse caminho inteiro; esta subseção permanece no nível do hardware.

Algumas informações úteis sobre a própria sinalização.

Primeiro, **linhas de interrupção geralmente são compartilhadas**. Uma CPU tem um número reduzido de entradas de interrupção, frequentemente de dezesseis a vinte e quatro em PCs legados e mais nas plataformas modernas por meio de APICs e GICs. Um sistema tipicamente possui mais dispositivos do que entradas de interrupção, então vários dispositivos compartilham uma única linha. Quando uma interrupção dispara em uma linha compartilhada, cada driver cujo dispositivo pode ser a origem precisa verificar: esta interrupção é minha? Se não, retorna uma indicação de "stray"; se sim, trata. A Seção 6 cobre o protocolo de interrupção compartilhada.

Segundo, **a sinalização de interrupção vem em dois tipos**. Sinalização por borda (edge-triggered) significa que a interrupção é sinalizada por uma transição no fio (de baixo para alto, ou de alto para baixo). Sinalização por nível (level-triggered) significa que a interrupção é sinalizada mantendo o fio em um nível específico (alto ou baixo) enquanto a interrupção estiver pendente. Os dois tipos têm consequências operacionais diferentes, que a próxima subseção explora.

Terceiro, **a interrupção é assíncrona em relação à CPU**. A CPU não sabe quando o dispositivo levantará o sinal. O handler do driver deve tolerar ser chamado em qualquer ponto durante o trabalho do próprio driver, e deve sincronizar-se com seu próprio código fora da interrupção de forma adequada. A disciplina de locking do Capítulo 11 é o que o driver usa para isso.

Quarto, **a interrupção carrega essencialmente nenhuma informação por si só**. O fio diz "algo aconteceu"; não diz o quê. O driver descobre o que aconteceu lendo o registrador de status do dispositivo. Uma única linha IRQ pode reportar muitos eventos diferentes (dados de recepção prontos, FIFO de transmissão vazio, mudança de estado do link, erro, entre outros), e é responsabilidade do driver decodificar os bits de status e decidir o que fazer.

### Edge-Triggered vs Level-Triggered

A diferença vale a pena compreender porque ela explica por que certos bugs produzem tempestades de interrupções, certos bugs produzem interrupções descartadas silenciosamente, e certos bugs produzem sistemas travados.

Uma interrupção **edge-triggered** (disparada por borda) dispara uma vez quando o sinal faz uma transição. O dispositivo puxa o fio para o nível baixo (para uma linha ativo-baixo); o controlador de interrupções percebe a transição; uma interrupção é enfileirada para a CPU. Se o dispositivo continuar mantendo o fio em nível baixo, nenhuma interrupção adicional dispara, porque o sinal não está fazendo transição, apenas continuando a ser afirmado. Para que uma nova interrupção dispare, o dispositivo deve soltar o fio e depois afirmá-lo novamente.

Interrupções edge-triggered são eficientes. O controlador de interrupções só precisa rastrear transições, não sinais contínuos. A desvantagem é a fragilidade: se uma interrupção dispara enquanto o controlador não está observando (porque outra interrupção está sendo tratada, por exemplo), a transição pode ser perdida. Controladores de interrupção modernos enfileiram interrupções edge-triggered para evitar a maior parte disso, mas o risco é real, e alguns drivers (ou alguns bugs de dispositivo) produzem configurações edge-triggered que ocasionalmente perdem um evento.

Uma interrupção **level-triggered** (disparada por nível) dispara continuamente enquanto o sinal está afirmado. Enquanto o dispositivo mantiver o fio no nível afirmado, o controlador de interrupções reporta uma interrupção. Quando o dispositivo solta o fio, o controlador de interrupções para de reportar. A CPU vê uma interrupção, o handler do driver executa, o handler lê o status do dispositivo e limpa a condição pendente, o dispositivo para de afirmar o sinal, e o controlador de interrupções para de reportar. Se o handler falhar em limpar a condição pendente, o sinal permanece afirmado, o controlador de interrupções continua reportando, e o handler do driver é chamado novamente imediatamente, em um loop que consome a CPU. Essa é a clássica **tempestade de interrupções**.

Interrupções level-triggered são robustas. Enquanto o dispositivo tiver algo a reportar, o OS saberá; não há janela onde um evento possa ser perdido. O custo é que um driver com bug pode produzir uma tempestade. O FreeBSD tem detecção de tempestade para mitigar isso (o apêndice *Uma Visão Aprofundada da Detecção de Tempestade de Interrupções* mais adiante neste capítulo o cobre); outros sistemas operacionais têm proteções similares. A regra geral mais comum: level-triggered é o padrão mais seguro, e as linhas INTx legadas do PCI são level-triggered por essa razão.

A distinção importa para autores de drivers em alguns lugares específicos:

- Um driver que falha em limpar o registrador INTR_STATUS do dispositivo antes de retornar do handler produzirá uma tempestade de interrupções em uma linha level-triggered. Em uma linha edge-triggered, o mesmo bug produz uma interrupção perdida.
- Um driver que lê e escreve INTR_STATUS corretamente funciona em ambos os tipos sem conhecimento especial.
- Um driver que manipula diretamente o modo de disparo do controlador de interrupções (raro; principalmente legado) deve entender a distinção.

Para o driver PCI do Capítulo 19, a sinalização é INTx level-triggered no caminho legado. Com MSI e MSI-X (Capítulo 20), a sinalização é baseada em mensagens e não corresponde diretamente a edge ou level, mas o padrão do driver é o mesmo: leia o status, reconheça o dispositivo, retorne.

### O Fluxo de Tratamento de Interrupções da CPU, Simplificado

O que acontece, passo a passo, quando a linha IRQ de um dispositivo é afirmada? Um rastreamento simplificado em um sistema x86 moderno:

1. O dispositivo afirma seu IRQ no barramento (ou envia um pacote MSI, para PCIe com MSI habilitado).
2. O controlador de interrupções do sistema (APIC no x86, GIC no arm64) recebe o sinal e determina qual CPU deve tratá-lo, com base na afinidade configurada. Em sistemas com múltiplas CPUs, essa é uma decisão que pode ser direcionada.
3. O hardware de interrupções da CPU escolhida detecta a interrupção pendente. Antes de concluir a instrução atual, a CPU salva estado suficiente (o contador de programa, o registrador de flags, e alguns outros campos) para retornar ao trabalho interrompido mais tarde.
4. A CPU salta para um vetor em sua tabela de descritores de interrupção. A entrada para esse vetor é um pequeno trecho de código do kernel chamado **trap stub**, que faz a transição para o modo supervisor, salva o conjunto de registradores da thread interrompida, e chama o código de despacho de interrupções do kernel.
5. O código de despacho de interrupções do kernel encontra o `intr_event` associado ao IRQ (essa é a estrutura do FreeBSD que a Seção 2 cobre) e chama os handlers do driver associados a ele.
6. O handler de filtro do driver executa. Ele lê o registrador de status do dispositivo, decide que tipo de evento ocorreu, escreve no registrador INTR_STATUS do dispositivo para reconhecer o evento (de modo que o dispositivo pare de afirmar a linha, para level-triggered), e retorna um valor que informa ao kernel o que fazer a seguir.
7. Se o filtro retornou `FILTER_SCHEDULE_THREAD`, o kernel agenda o ithread associado a essa interrupção. O ithread é uma thread do kernel que acorda, executa o handler secundário do driver, e volta a dormir.
8. Após todos os handlers executarem, o kernel envia um sinal de End-of-Interrupt (EOI) ao controlador de interrupções, o que rearma a linha IRQ.
9. A CPU retorna da interrupção. O conjunto de registradores da thread interrompida é restaurado e a thread retoma na instrução que estava prestes a executar quando a interrupção chegou.

Os passos 3 a 9 levam alguns microssegundos em hardware moderno para um handler simples. Todo o fluxo é invisível para a thread interrompida: código que não foi projetado para ser seguro em interrupções (digamos, um cálculo de ponto flutuante no espaço do usuário) executa corretamente em ambos os lados da interrupção, porque a CPU salva e restaura seu estado em torno de toda a sequência.

Da perspectiva do autor do driver, os passos 1 a 5 são de responsabilidade do kernel; os passos 6 e 7 são onde o código do driver executa. O handler do driver deve ser rápido (o EOI do passo 8 aguarda por ele), não deve dormir (a thread interrompida mantém recursos de CPU), e não deve adquirir locks que possam bloquear indiretamente na thread interrompida. A Seção 2 tornará essas restrições precisas em termos do FreeBSD; a Seção 1 agora estabeleceu o modelo mental.

### O Que um Driver Deve Fazer Quando uma Interrupção Chega

As obrigações do driver em uma interrupção são poucas em número, mas não em detalhe:

1. **Identificar a causa.** Leia o registrador de status de interrupção do dispositivo. Se nenhum bit estiver definido (o dispositivo não tem interrupção pendente), esta é uma chamada espúria de IRQ compartilhado; retorne `FILTER_STRAY` e deixe o kernel tentar o próximo handler na linha.
2. **Reconhecer a interrupção no dispositivo.** Escreva os bits de status de volta (tipicamente escrevendo 1 em cada bit, já que a maioria dos registradores INTR_STATUS é RW1C) para que o dispositivo desafirme a linha e o controlador de interrupções possa reabilitar o IRQ. Nem todo dispositivo exige que o reconhecimento seja feito dentro do filtro, mas fazê-lo aqui é o padrão seguro; a história da tempestade level-triggered depende de um reconhecimento oportuno.
3. **Decidir qual trabalho fazer.** Leia o suficiente do dispositivo para decidir. Foi um evento de recepção? Uma conclusão de transmissão? Um erro? Uma mudança de link? Os bits de status informam você.
4. **Fazer o trabalho urgente e pequeno.** Atualize um contador. Copie um byte de um FIFO para uma fila. Alterne um bit de controle. Qualquer coisa que possa ser feita em microssegundos sem adquirir um sleep lock é permitida aqui.
5. **Adiar o trabalho volumoso.** Se o evento desencadeia uma operação longa (processar um pacote recebido, decodificar um fluxo de dados, enviar um comando para o espaço do usuário), agende uma ithread ou uma tarefa de taskqueue e retorne. O trabalho adiado executa em contexto de thread, onde pode adquirir sleep locks, alocar memória, e levar seu tempo.
6. **Retornar o valor FILTER_* apropriado.** `FILTER_HANDLED` significa que a interrupção está completamente tratada; nenhuma ithread é necessária. `FILTER_SCHEDULE_THREAD` significa que a ithread deve executar. `FILTER_STRAY` significa que a interrupção não era para este driver. Esses três valores são o vocabulário que o kernel usa para despachar trabalho adicional.

Um driver que faz essas seis coisas corretamente em cada interrupção tem a estrutura que o restante do Capítulo 19 ensina. Um driver que pula qualquer uma delas tem um bug.

### Exemplos do Mundo Real

Um breve tour pelos eventos que o vocabulário do Capítulo 19 cobrirá.

**Pressionamentos de tecla.** Um controlador de teclado PS/2 dispara uma interrupção quando um scancode chega. O driver lê o scancode, passa-o para o subsistema de teclado, e reconhece. Todo o handler executa em alguns microssegundos; um taskqueue geralmente é desnecessário.

**Pacotes de rede.** Uma NIC dispara uma interrupção quando pacotes se acumulam na fila de recepção. O filtro do driver lê um registrador de status para confirmar eventos de recepção, agenda a ithread, e retorna. A ithread percorre o anel de descritores, constrói pacotes `mbuf`, e os passa para cima na pilha de rede. A divisão entre filtro e ithread importa aqui porque o processamento da pilha é lento o suficiente para que executá-lo dentro do filtro estenderia demasiado a janela de interrupção.

**Leituras de sensor.** Um sensor de temperatura conectado via I2C dispara uma interrupção quando uma nova medição está pronta. O driver lê o valor, atualiza um cache de sysctl, opcionalmente acorda quaisquer leitores pendentes do espaço do usuário, e reconhece. Simples e rápido.

**Porta serial.** Uma UART dispara uma interrupção nas condições de recepção ou de FIFO de transmissão vazio. O driver drena ou reabastece o FIFO, atualiza um buffer circular, e reconhece. Em altas taxas de baud, isso pode acontecer dezenas de milhares de vezes por segundo, então o handler deve ser enxuto.

**Conclusão de disco.** Um controlador SATA ou NVMe dispara uma interrupção quando um comando enfileirado é concluído. O driver percorre a fila de conclusão, associa cada conclusão a uma requisição de I/O pendente, acorda a thread em espera, e reconhece. A associação e o despertar às vezes são divididos entre filtro e ithread.

Cada um desses dispositivos chega ao vocabulário do Capítulo 19 da mesma forma: o filtro lê o status, o filtro decide o que aconteceu, o filtro reconhece, e o filtro ou trata ou adia. O layout específico dos registradores difere; o padrão não.

### Um Exercício Rápido: Encontre Dispositivos Acionados por Interrupção no Seu Host de Laboratório

Antes de passar para a Seção 2, um exercício rápido para tornar a imagem do hardware concreta.

No seu host de laboratório, execute:

```sh
vmstat -i
```

A saída é uma lista de fontes de interrupção com suas contagens desde o boot. Cada linha tem mais ou menos esta aparência:

```text
interrupt                          total       rate
cpu0:timer                      1234567        123
cpu1:timer                      1234568        123
irq9: acpi0                          42          0
irq19: uhci0+                     12345         12
irq21: ahci0                      98765         99
irq23: em0                       123456        123
```

Escolha três linhas da sua própria saída. Para cada uma, identifique:

- O nome da interrupção (uma mistura do número IRQ e a descrição definida pelo driver).
- A contagem total (quantas vezes a interrupção disparou desde o boot).
- A taxa (interrupções por segundo; uma taxa alta significa que o dispositivo está ocupado).

Execute `vmstat -i` uma segunda vez dez segundos depois. Compare as contagens. Quais interrupções estão contando ativamente? Quais estão essencialmente ociosas?

Agora associe interrupções a dispositivos com `devinfo -v`:

```sh
devinfo -v | grep -B 2 irq
```

Cada correspondência mostra um dispositivo que reivindica um IRQ. Compare com a saída de `vmstat -i` para ver qual driver é servido por cada linha.

Mantenha essa saída aberta enquanto lê a Seção 2. A entrada `em0` no exemplo é o controlador Ethernet Intel; se você estiver usando um sistema baseado em Intel com FreeBSD, `em0` ou `igc0` ou `ix0` provavelmente está executando uma versão do mesmo padrão que o Capítulo 19 ensina. Um NUC moderno rodando FreeBSD 14.3 mostra uma ou duas dúzias de fontes de interrupção; um servidor mostra muito mais. O sistema que você realmente tem é mais interessante de observar do que qualquer diagrama.

### Uma Breve História das Interrupções

Interrupções são uma das ideias mais antigas da arquitetura de computadores. O PDP-1 original as suportava em 1961 como uma forma de dispositivos de I/O sinalizarem a CPU sem que a CPU fizesse polling. O IBM 704 as tinha por volta da mesma época. Os primeiros sistemas de tempo compartilhado usavam interrupções para o tick de clock que acionava o escalonamento, e para cada conclusão de I/O.

Ao longo dos anos 1970 e 1980, os computadores pessoais herdaram esse padrão. O IBM PC original utilizava o 8259 Programmable Interrupt Controller (PIC), que suportava oito linhas de IRQ; o PC/AT estendeu esse número para quinze linhas utilizáveis ao encadear dois PICs. O conjunto de instruções x86 adicionou instruções específicas para tratamento de interrupções (`CLI`, `STI`, `INT`, `IRET`) que persistem até hoje em formas estendidas.

O PCI introduziu o conceito de um dispositivo anunciando sua interrupção por meio do espaço de configuração (os campos `INTLINE` e `INTPIN` discutidos no Capítulo 18). O PCIe adicionou MSI e MSI-X, que substituem a linha de IRQ física por uma mensagem de escrita na memória. Os três mecanismos coexistem em sistemas modernos; o INTx legado apresentado no Capítulo 19 é o mais antigo dos três e o único que compartilha linhas.

Os sistemas operacionais evoluíram em paralelo. O Unix primitivo era monolítico e single-threaded no kernel; as interrupções preemptavam o que quer que estivesse em execução. Os kernels modernos (FreeBSD incluído) contam com locking de granularidade fina, estruturas de dados por CPU e despacho diferido baseado em ithread. A disciplina de handler que o Capítulo 19 ensina é a destilação dessa evolução: rápido no filtro, lento na task, MP-safe por padrão, compartilhável, depurável.

Conhecer a história não é um requisito para escrever um driver. Mas o vocabulário (IRQ, PIC, EOI, INTx) vem de pontos específicos dessa história, e um autor de driver que sabe de onde as palavras vêm encontra menos mistério no campo.

### Encerrando a Seção 1

Uma interrupção é o mecanismo pelo qual um dispositivo interrompe o trabalho atual da CPU, executa um pequeno trecho de código do driver e permite que a CPU retome o que estava fazendo. Esse mecanismo passa pelo controlador de interrupções, pelo despacho de interrupções da CPU, pela infraestrutura de eventos de interrupção do kernel e, por fim, pelo handler do driver. A sinalização por borda e por nível têm consequências operacionais diferentes, sendo a mais visível delas a tempestade de interrupções quando uma linha ativada por nível não é devidamente reconhecida.

O handler de um driver tem seis obrigações: identificar a causa, reconhecer o dispositivo, decidir qual trabalho fazer, executar a parte urgente, adiar a parte mais longa e retornar o valor `FILTER_*` correto. Cada uma dessas obrigações é simples isoladamente e exigente em conjunto; o restante do Capítulo 19 trata de realizar cada uma delas corretamente nos termos do FreeBSD.

A Seção 2 percorre agora o modelo de interrupções do kernel do FreeBSD: o que é um `intr_event`, o que é um ithread, como `vmstat -i` e `devinfo -v` expõem a visão do kernel sobre as interrupções, e quais restrições o modelo impõe aos handlers dos drivers.



## Seção 2: Interrupções no FreeBSD

A Seção 1 estabeleceu o modelo de hardware. A Seção 2 apresenta o modelo de software. A infraestrutura de interrupções do kernel é a camada entre o controlador de interrupções e o handler do driver; compreendê-la com clareza é o que transforma o modelo de hardware em algo que o driver pode de fato usar. Ao final da Seção 2, o leitor deve ser capaz de responder a três perguntas em linguagem simples: o que é executado quando uma interrupção dispara, o que é executado posteriormente como trabalho diferido, e o que o driver precisa garantir para que ambos ocorram com segurança.

### O Modelo de Interrupções do FreeBSD em Uma Visão Geral

Um handler de driver não funciona de forma isolada. Ele opera dentro de um pequeno ecossistema de objetos do kernel que, em conjunto, tornam o tratamento de interrupções ordenado e depurável. Esse ecossistema tem três peças que vale nomear desde o início.

A primeira é o **evento de interrupção**, representado por `struct intr_event` em `/usr/src/sys/sys/interrupt.h` (com o código de despacho em `/usr/src/sys/kern/kern_intr.c`). Existe um `intr_event` por linha de IRQ (ou por vetor de MSI, no contexto do Capítulo 20). Ele é o coordenador central: mantém uma lista de handlers (as funções de filtro e as funções de ithread do driver), um nome legível por humanos, flags, um contador de laço usado para detecção de tempestades (`ie_count`), um limitador de taxa para mensagens de aviso (`ie_warntm`) e um vínculo de CPU. Quando o controlador de interrupções reporta um IRQ ao kernel, o kernel localiza o `intr_event` correspondente e percorre sua lista de handlers. Interrupções perdidas são contadas globalmente, não por evento; elas aparecem pela contabilização separada do `vmstat -i` e por mensagens no log do kernel, e não por um campo no evento.

A segunda é o **handler de interrupção**, representado por `struct intr_handler`. Existe um `intr_handler` por handler registrado em um `intr_event`. Uma única linha de IRQ pode ter muitos handlers (um por driver que compartilha a linha). O handler carrega a função de filtro fornecida pelo driver (se houver), a função de ithread fornecida pelo driver (se houver), os flags `INTR_*` (o mais importante deles, `INTR_MPSAFE`) e um ponteiro de cookie que o kernel mantém como referência para o driver.

A terceira é a **thread de interrupção**, normalmente chamada de **ithread**, representada por `struct intr_thread`. Ao contrário do evento e do handler, o ithread é uma thread do kernel de verdade, com sua própria pilha, sua própria prioridade de escalonamento e sua própria estrutura `proc`. Quando um filtro retorna `FILTER_SCHEDULE_THREAD` (ou quando um driver registrou um handler somente de ithread sem filtro), o ithread é agendado para execução. O ithread então chama a função de handler do driver em contexto de thread, onde mutexes de sleep normais e a operação de sleep em si são permitidos.

Os três juntos produzem o padrão clássico de interrupção em duas fases que o FreeBSD utiliza há mais de uma década: um filtro rápido executa no contexto primário de interrupção para fazer o trabalho urgente, e um handler em contexto de thread é executado depois para fazer o trabalho mais lento. O driver do Capítulo 19 utilizará ambas as fases.

### Atribuição e Roteamento de IRQ

Um sistema x86 moderno possui mais de um controlador de interrupções. O PIC 8259 legado foi substituído pelo Local APIC (um por CPU, tratando interrupções por CPU como o temporizador local) e pelo I/O APIC (uma unidade compartilhada que recebe IRQs do barramento de I/O e os roteia para as CPUs). Em arm64, o equivalente é o Generic Interrupt Controller (GIC), com redistributores por CPU e um distribuidor compartilhado. Em alvos embarcados, há um punhado de outros controladores. O FreeBSD abstrai todos eles por meio da interface `intr_pic(9)`; um autor de driver raramente interage diretamente com o controlador de interrupções.

O que o driver enxerga é um número de IRQ (nos caminhos legados de PCI, o número que um BIOS atribuiu no espaço de configuração) ou um índice de vetor (nos caminhos de MSI e MSI-X). O driver solicita um recurso de IRQ por esse número, o kernel aloca um `struct resource *`, e o driver passa o recurso para `bus_setup_intr(9)` a fim de vincular um handler. O kernel se encarrega de conectar o handler ao `intr_event` correto, configurar o controlador de interrupções para rotear o IRQ a uma CPU e armar a linha.

Do ponto de vista do autor do driver, o roteamento de IRQ é normalmente uma caixa-preta. O kernel cuida disso; o driver vê um handle e um handler. Uma exceção: em plataformas com muitas CPUs e múltiplos dispositivos, a **afinidade** importa. Uma interrupção que dispara em uma CPU distante do dispositivo produz cache misses e tráfego entre sockets; uma interrupção que dispara em uma CPU próxima ao dispositivo é mais eficiente. `bus_bind_intr(9)` permite que o driver solicite uma CPU específica; operadores usam `cpuset -x <irq> -l <cpu>` para sobrescrever a afinidade em tempo de execução, e `cpuset -g -x <irq>` para consultá-la. O apêndice *A Deeper Look at CPU Affinity for Interrupts* mais adiante no capítulo cobre ambos os caminhos com mais detalhes.

### SYS_RES_IRQ: O Recurso de Interrupção

O Capítulo 18 apresentou três tipos de recursos: `SYS_RES_MEMORY` para BARs mapeados em memória, `SYS_RES_IOPORT` para BARs de I/O e (mencionado de passagem) `SYS_RES_IRQ` para interrupções. A Seção 3 usará o terceiro tipo pela primeira vez. O vocabulário é o mesmo que para BARs:

```c
int rid = 0;                  /* legacy PCI INTx */
struct resource *irq_res;

irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
    RF_SHAREABLE | RF_ACTIVE);
```

Três pontos merecem destaque sobre essa alocação.

Primeiro, **`rid = 0`** é a convenção para uma linha PCI INTx legada. O driver do barramento PCI trata o recurso de IRQ de índice zero como a interrupção legada do dispositivo, configurada a partir do campo `PCIR_INTLINE` no espaço de configuração. Para MSI e MSI-X (Capítulo 20), o rid é 1, 2, 3 e assim por diante, correspondendo aos vetores alocados.

Segundo, **`RF_SHAREABLE`** solicita ao kernel que permita o compartilhamento da linha de IRQ com outros drivers. No PCI legado, esse é o caso comum: uma única linha física pode atender a múltiplos dispositivos. Sem `RF_SHAREABLE`, a alocação falha se outro driver já tiver um handler registrado na mesma linha. Passar `RF_SHAREABLE` não significa que o driver precisa tratar interrupções perdidas; significa que ele precisa tolerá-las. A Seção 6 trata exatamente dessa tolerância.

Terceiro, **`RF_ACTIVE`** ativa o recurso em uma única etapa, assim como na alocação de BARs. Sem ele, o driver precisaria chamar `bus_activate_resource(9)` separadamente. O Capítulo 19 sempre usa `RF_ACTIVE`.

Em caso de sucesso, o `struct resource *` retornado é um handle para o IRQ. Ele não é o número do IRQ; o kernel não expõe isso. O driver passa o handle para `bus_setup_intr(9)`, `bus_teardown_intr(9)` e `bus_release_resource(9)`.

### Handlers de Filtro vs. Handlers de ithread

Este é o núcleo conceitual da Seção 2 e do capítulo. Um leitor que internaliza a distinção entre filtro e ithread lê o código de interrupção de qualquer driver FreeBSD com compreensão plena.

Um **handler de filtro** é uma função C registrada pelo driver que executa no contexto primário de interrupção. Contexto primário de interrupção significa: a CPU saltou diretamente do que estava fazendo, um estado mínimo foi salvo, e o filtro está sendo executado com o contexto da thread interrompida ainda parcialmente em vigor. Especificamente:

- O filtro não pode fazer sleep. Não há thread para bloquear; o kernel está no meio do despacho de uma interrupção.
- O filtro não pode adquirir um mutex de sleep (`mtx(9)` por padrão é um mutex adaptativo que pode girar brevemente, mas eventualmente fará sleep). Mutexes de spin (`mtx_init` com `MTX_SPIN`) são seguros.
- O filtro não pode alocar memória com `M_WAITOK`; pode usar `M_NOWAIT`, que pode falhar.
- O filtro não pode chamar código que utilize qualquer um dos itens acima.

O filtro deve ser rápido (microssegundos), fazer o trabalho urgente (ler o status, reconhecer, atualizar um contador) e retornar. A convenção de valores de retorno do kernel é:

- `FILTER_HANDLED`: a interrupção é minha, foi totalmente tratada, nenhum ithread é necessário.
- `FILTER_SCHEDULE_THREAD`: a interrupção é minha, parte do trabalho foi feita, agende o ithread para o restante.
- `FILTER_STRAY`: a interrupção não é minha; tente o próximo handler nesta linha.

Um driver também pode especificar `FILTER_HANDLED | FILTER_SCHEDULE_THREAD` para indicar tanto "tratei parte disso" quanto "agende a thread para mais".

Um **handler de ithread** é uma função C diferente que executa em contexto de thread. A thread é agendada pelo kernel após um filtro retornar `FILTER_SCHEDULE_THREAD`, ou, se o handler for registrado somente como ithread (sem filtro), o ithread é agendado automaticamente quando o kernel despacha a interrupção.

No contexto de ithread, as restrições se afrouxam consideravelmente:

- O ithread pode fazer sleep brevemente em um mutex ou variável de condição.
- O ithread pode usar `malloc(M_WAITOK)`.
- O ithread pode chamar a maioria das APIs do kernel que utilizam locks dormíveis.
- O ithread ainda não pode dormir por um tempo arbitrariamente longo (é uma thread com prioridade em tempo real), mas pode realizar o trabalho normal de um driver.

A divisão permite que um driver separe o trabalho urgente e curto (no filtro) do trabalho mais lento e extenso (no ithread). O filtro de um driver de rede pode ler o registrador de status e reconhecer; seu ithread percorre o anel de descritores de recepção e passa os pacotes para cima na pilha. O filtro de um driver de disco pode registrar quais conclusões ocorreram e reconhecer; seu ithread combina conclusões com requisições pendentes e acorda as threads que aguardavam.

### Quando Usar Somente Filtro

Um driver usa somente filtro quando todo o trabalho que precisa realizar em uma interrupção pode ser executado no contexto primário de interrupção. Exemplos:

- **Um driver de teste mínimo** que incrementa um contador a cada interrupção e nada mais.
- **Um driver de sensor simples** que lê um registrador, armazena o valor em cache e acorda um leitor de sysctl. (Se `selwakeup` ou um broadcast de variável de condição exigir um mutex de sleep, isso se move para filtro mais ithread.)
- **Um driver de temporizador** cuja função é incrementar algum contador interno do kernel.

O driver do Capítulo 19 no Estágio 1 é somente filtro: lê INTR_STATUS, reconhece, atualiza um contador e retorna `FILTER_HANDLED`. Isso é suficiente para provar que o handler está devidamente conectado.

### Quando Usar Filtro Mais ithread

Um driver usa filtro mais ithread quando a interrupção requer um pequeno trabalho urgente seguido de um trabalho mais lento em volume. Exemplos:

- **Um driver de NIC.** O filtro reconhece e marca quais filas têm eventos. O ithread percorre os anéis de descritores, constrói mbufs e passa os pacotes adiante.
- **Um controlador de disco.** O filtro lê o status de conclusão e reconhece. O ithread combina conclusões com requisições de I/O e acorda os processos em espera.
- **Um controlador de host USB.** O filtro lê o status e reconhece. O ithread percorre a lista de descritores de transferência e completa quaisquer URBs pendentes.

O driver do Capítulo 19 no Estágio 2 migra para filtro mais ithread quando eventos simulados de "trabalho solicitado" são adicionados; o filtro registra o evento, e um worker diferido (via taskqueue; o primitivo do Capítulo 14) trata o trabalho.

### Quando Usar Somente ithread

Um driver usa somente ithread quando todo o trabalho precisa ser executado em contexto de thread. Isso é menos comum; a razão habitual é que o driver precisa adquirir um mutex de sleep a cada interrupção e não consegue fazer nada útil no contexto primário.

Registrar um handler exclusivo de ithread é simples: passe `NULL` para o argumento filter de `bus_setup_intr(9)`. O kernel escalonará o ithread sempre que a interrupção for disparada.

O driver do Capítulo 19 não utiliza o modo exclusivo de ithread; o filter é sempre rápido de executar.

### INTR_MPSAFE: O Que a Flag Promete

`INTR_MPSAFE` é um bit no argumento de flags de `bus_setup_intr(9)`. Defini-lo é uma promessa ao kernel de duas coisas:

1. O seu handler faz sua própria sincronização. O kernel não adquirirá o lock Giant ao redor dele.
2. O seu handler é seguro para execução concorrente em múltiplas CPUs (para handlers compartilhados por múltiplas CPUs, o que ocorre em cenários MSI-X e em algumas configurações de PIC).

Se você **não** definir `INTR_MPSAFE`, o kernel adquire o Giant antes de chamar o seu handler. Esse é o comportamento padrão do BSD antigo, preservado para compatibilidade retroativa com drivers pré-SMP que dependiam da proteção implícita do Giant. Drivers modernos sempre definem `INTR_MPSAFE`.

Deixar de definir `INTR_MPSAFE` tem um sintoma visível: o banner do `dmesg` no `kldload` inclui uma linha como `myfirst0: [GIANT-LOCKED]`. Essa é a forma do kernel dizer que o Giant está sendo adquirido ao redor do seu handler. Em sistemas em produção, isso serializa cada interrupção através de um único lock, o que é desastroso para a escalabilidade. A linha é uma cutucada deliberada do `bus_setup_intr` para que você perceba o problema.

Definir `INTR_MPSAFE` quando você ainda depende do Giant também é um bug, mas um mais silencioso. O kernel não adquirirá o Giant, de modo que qualquer caminho de código que antes era serializado por ele deixará de ser. Condições de corrida aparecem onde antes não existiam. A correção não é remover `INTR_MPSAFE` (isso mascara o bug); a correção é adicionar o locking correto ao handler e ao código que ele acessa.

O driver do Capítulo 19 sempre define `INTR_MPSAFE` e depende do `sc->mtx` existente para sincronização. A disciplina do Capítulo 11 se mantém.

### Flags INTR_TYPE_*

Além de `INTR_MPSAFE`, `bus_setup_intr` recebe uma flag de categoria que indica a classe da interrupção:

- `INTR_TYPE_TTY`: dispositivos tty e seriais.
- `INTR_TYPE_BIO`: I/O de bloco (disco, CD-ROM).
- `INTR_TYPE_NET`: rede.
- `INTR_TYPE_CAM`: SCSI (framework CAM).
- `INTR_TYPE_MISC`: miscelânea.
- `INTR_TYPE_CLK`: interrupções de clock e temporizador.
- `INTR_TYPE_AV`: áudio e vídeo.

A categoria influencia a prioridade de escalonamento do ithread. Historicamente, cada categoria tinha uma prioridade distinta; no FreeBSD moderno, apenas `INTR_TYPE_CLK` recebe uma prioridade elevada, e as demais são aproximadamente iguais. A categoria ainda vale a pena ser definida corretamente porque ela aparece na saída de `devinfo -v` e `vmstat -i`, tornando a interrupção autodescritiva.

Para o driver do Capítulo 19, `INTR_TYPE_MISC` é apropriado porque o alvo da demonstração não se enquadra em nenhuma das categorias mais específicas. O Capítulo 20 usará `INTR_TYPE_NET` quando o driver começar a ter como alvo NICs nos laboratórios.

### Interrupções Compartilhadas versus Exclusivas

No PCI legado, múltiplos dispositivos podem compartilhar uma única linha INTx. O kernel rastreia isso com duas flags de recurso:

- `RF_SHAREABLE`: este driver aceita compartilhar a linha com outros drivers.
- Ausência de `RF_SHAREABLE`: este driver quer a linha para si; a alocação falha se outro driver já a detém.

Um driver que deseja interrupções compartilháveis usa `RF_SHAREABLE | RF_ACTIVE` em sua chamada a `bus_alloc_resource_any`. Um driver que deseja acesso exclusivo (talvez por razões de latência) usa `RF_ACTIVE` sozinho, mas a requisição pode falhar em sistemas muito ocupados.

O kernel nunca impede o driver de compartilhar; ele impede que outro driver se junte caso este solicite exclusividade. No PCIe moderno com MSI-X, o compartilhamento é muito menos comum porque cada dispositivo tem seu próprio vetor com sinalização por mensagem.

O driver do Capítulo 19 define `RF_SHAREABLE` porque o virtio-rnd no bhyve pode ou não compartilhar sua linha com outros dispositivos emulados pelo bhyve, dependendo da topologia de slots. Ser compartilhável é o padrão seguro.

A flag `INTR_EXCL` passada pelo campo de flags de `bus_setup_intr` (não confundir com a flag de alocação de recursos) é um conceito relacionado, mas distinto: ela pede ao barramento que dê ao handler acesso exclusivo no nível do interrupt-event. Drivers PCI legados raramente precisam disso. Alguns drivers de barramento a usam internamente. Para o driver do Capítulo 19, não definimos `INTR_EXCL`.

### O Que vmstat -i Mostra

`vmstat -i` imprime os contadores de interrupção do kernel. Cada linha corresponde a um `intr_event`. As colunas são:

- **interrupt**: um identificador legível por humanos. Para interrupções de hardware, o nome é derivado do número de IRQ e da descrição do driver. Nomes no estilo `devinfo -v` (como `em0:rx 0`) aparecem quando vetores MSI-X estão em uso.
- **total**: o número de vezes que essa interrupção foi disparada desde o boot.
- **rate**: a taxa de interrupções por segundo, calculada como média de uma janela recente.

Algumas notas de interpretação. Uma coluna `total` que cresce rapidamente para um dispositivo ocioso é um sinal de alerta (tempestade de interrupções). Uma coluna `rate` em zero para um dispositivo que deveria estar tratando tráfego sugere que o handler não está conectado corretamente. Quando vários dispositivos compartilham uma linha INTx legada, `vmstat -i` mostra uma linha por `intr_event` (por fonte de IRQ), e o nome do driver nessa linha é a descrição do primeiro handler registrado; os outros drivers que compartilham a linha não recebem linhas próprias. Quando um dispositivo tem seus próprios vetores MSI ou MSI-X, cada vetor é seu próprio `intr_event` e cada um recebe sua própria linha. Interrupções por CPU, como o temporizador local, aparecem como linhas distintas por CPU (`cpu0:timer`, `cpu1:timer`) porque o kernel cria um evento por CPU para elas.

O kernel expõe os mesmos contadores por meio de `sysctl hw.intrcnt` e `sysctl hw.intrnames`, que são os dados brutos que `vmstat -i` formata. Um autor de driver raramente os lê diretamente; `vmstat -i` é a visão amigável.

### O Que devinfo -v Mostra Sobre Interrupções

`devinfo -v` percorre a árvore newbus e imprime cada dispositivo com seus recursos. Para um driver PCI com interrupção, a lista de recursos inclui uma entrada `irq:` ao lado da entrada `memory:`:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
        irq: 19
```

O número após `irq:` é o identificador de IRQ do kernel. No x86, é frequentemente o número de um pino do I/O APIC; no arm64, é um vetor do GIC; o significado exato depende da plataforma, mas o número é estável entre reboots do mesmo sistema.

Fazer corresponder `irq: 19` à entrada `irq19: ` do `vmstat -i` confirma que o driver está conectado à linha de interrupção esperada.

Para interrupções MSI-X (Capítulo 20), cada vetor tem sua própria entrada `irq:`, e `devinfo -v` os lista individualmente.

### Um Diagrama Simples do Caminho de uma Interrupção

Juntando tudo, eis o que acontece do dispositivo até o driver:

```text
  Device        IRQ line          Interrupt         CPU        intr_event         Handler
 --------     -----------       -controller-      --------   --------------      ---------
   |              |                  |                |             |                 |
   | asserts     |                  |                |             |                 |
   | IRQ line    | signal           |                |             |                 |
   |------------>|                  |                |             |                 |
   |             | latch            |                |             |                 |
   |             |----------------->|                |             |                 |
   |             |                  | steer to CPU   |             |                 |
   |             |                  |--------------->|             |                 |
   |             |                  |                | save state  |                 |
   |             |                  |                | jump vector |                 |
   |             |                  |                |             | look up         |
   |             |                  |                |------------>|                 |
   |             |                  |                |             | for each        |
   |             |                  |                |             | handler         |
   |             |                  |                |             |---------------->|
   |             |                  |                |             |                 | filter runs
   |             |                  |                |             |<----------------|
   |             |                  |                |             | FILTER_HANDLED  |
   |             |                  |                |             | or              |
   |             |                  |                |             | FILTER_SCHEDULE |
   |             |                  |                | EOI         |                 |
   |             |                  |<---------------|             |                 |
   |             |                  |                | restore     |                 |
   |             |                  |                | state       |                 |
   |             |                  |                | resume thread                 |
   |             |                  |                |             | ithread wakeup  |
   |             |                  |                |             | (if scheduled)  |
   |             |                  |                |             |                 | ithread runs
   |             |                  |                |             |                 | slower work
```

O diagrama omite vários detalhes (coalescência de interrupções, a troca da pilha da thread interrompida pela pilha do ithread, o próprio escalonamento do ithread), mas captura o formato geral. Um filter é executado no contexto de interrupção, um ithread (se escalonado) é executado depois no contexto de thread, o EOI acontece após o fim dos filters, e a thread interrompida retoma quando a CPU estiver livre.

### Restrições ao Que os Handlers Podem Fazer

Uma lista consolidada e resumida do que um handler de filtro pode e não pode fazer. Esta é a lista mais consultada do Capítulo 19; marque-a para consultas futuras.

**Handlers de filtro PODEM:**

- Ler e escrever registradores de dispositivo pela camada de acesso.
- Adquirir spin mutexes (`struct mtx` inicializado com `MTX_SPIN`).
- Ler campos do softc protegidos apenas por spin locks.
- Chamar as operações atômicas do kernel (`atomic_add_int`, etc.).
- Chamar `taskqueue_enqueue(9)` para escalonar trabalho no contexto de thread.
- Chamar `wakeup_one(9)` para acordar uma thread dormindo em um canal, quando o contexto permitir (a maioria permite).
- Retornar `FILTER_HANDLED`, `FILTER_SCHEDULE_THREAD`, `FILTER_STRAY`, ou combinações deles.

**Handlers de filtro NÃO PODEM:**

- Adquirir um sleep mutex (`struct mtx` inicializado com o padrão, `struct sx`, `struct rwlock`).
- Chamar qualquer função que possa dormir: `malloc(M_WAITOK)`, `tsleep`, `pause`, `cv_wait`, entre outras.
- Adquirir o Giant.
- Chamar código que possa indiretamente fazer qualquer um dos itens acima.
- Demorar muito (microssegundos estão bem; milissegundos são um bug).

**Handlers de ithread PODEM:**

- Tudo o que um handler de filtro pode, mais:
- Adquirir sleep mutexes, sx locks, rwlocks.
- Chamar `malloc(M_WAITOK)`.
- Chamar `cv_wait`, `tsleep`, `pause` e outras primitivas de bloqueio.
- Demorar mais (dezenas ou centenas de microssegundos são normais).
- Executar trabalho delimitado com tempo de conclusão imprevisível.

**Handlers de ithread NÃO DEVEM:**

- Dormir por períodos arbitrariamente longos. O ithread tem uma prioridade de escalonamento que pressupõe capacidade de resposta; um handler que dorme por segundos priva de recursos outros trabalhos no mesmo ithread.
- Bloquear o ithread aguardando eventos externos sem limite definido.

O filtro do Capítulo 19 respeita rigorosamente a primeira lista; qualquer violação é um bug que o kernel de depuração frequentemente detecta.

### Uma Nota Sobre ithreads por CPU versus ithreads Compartilhados

Para linhas PCI INTx legadas, o kernel geralmente atribui um ithread por `intr_event`, compartilhado entre todos os handlers desse evento. Para MSI-X (Capítulo 20), cada vetor tem seu próprio ithread. A diferença importa quando múltiplos handlers precisam ser executados concorrentemente no mesmo IRQ: no ithread compartilhado, eles se serializam; em vetores MSI-X separados, podem ser executados em paralelo.

O driver do Capítulo 19 usa PCI legado. Um IRQ, um ithread (se houver algum ithread), uma fila de trabalho diferido. A serialização é geralmente o que se deseja em um driver de dispositivo único.

### Concluindo a Seção 2

O modelo de interrupções do FreeBSD gira em torno de três objetos: o `intr_event` (um por linha de IRQ ou vetor MSI), o `intr_handler` (um por driver registrado nesse evento) e o ithread (um por evento, compartilhado entre os handlers). Um driver registra uma função de filtro, uma função de ithread, ou ambas, por meio de `bus_setup_intr(9)`, e promete conformidade com `INTR_MPSAFE` pelo argumento de flags. O kernel despacha os handlers de filtro no contexto primário de interrupção e escalona os ithreads após o filtro retornar.

As restrições sobre handlers de filtro são rígidas (sem dormir, sem sleep locks, sem chamadas lentas); as restrições sobre handlers de ithread são comparativamente brandas. Linhas PCI INTx compartilhadas permitem muitos drivers em um único IRQ, portanto os filtros precisam identificar se a interrupção pertence ao seu dispositivo e retornar `FILTER_STRAY` quando não pertencer. `vmstat -i` e `devinfo -v` expõem a visão do kernel para que operadores e autores de drivers possam ver o que está acontecendo.

A Seção 3 é onde o driver finalmente escreve código contra esse modelo. Ela aloca um recurso de IRQ com `SYS_RES_IRQ`, registra um handler de filtro por meio de `bus_setup_intr(9)`, define `INTR_MPSAFE` e registra uma mensagem curta a cada chamada. O Estágio 1 é a primeira vez que o driver é interrompido de verdade.



## Seção 3: Registrando um Handler de Interrupção

As Seções 1 e 2 estabeleceram os modelos de hardware e do kernel. A Seção 3 coloca o driver em ação. A tarefa é bem delimitada: estender o caminho de attach do Capítulo 18 para que, após alocar o BAR e percorrer a lista de capacidades, o driver também aloque um recurso de IRQ, registre um handler de filtro e defina `INTR_MPSAFE`. O caminho de detach cresce na direção inversa: desmonta o handler primeiro, depois libera o IRQ. Ao final da Seção 3, o driver está na versão `1.2-intr-stage1` e dispara um pequeno handler de filtro que incrementa um contador toda vez que sua linha de IRQ é ativada.

### O Que o Estágio 1 Produz

O handler do Estágio 1 é deliberadamente mínimo. O driver precisa de um filtro que seja correto em todo sentido formal (retorna o valor `FILTER_*` correto, respeita a regra do "sem dormir", é `INTR_MPSAFE`), mas que ainda não realize nenhum trabalho de verdade. O objetivo é provar que o handler está conectado corretamente antes de introduzir as complexidades da decodificação de status e do trabalho diferido.

O comportamento do handler no Estágio 1:

1. Adquirir um lock de contador seguro para spin (no nosso caso, uma simples operação atômica).
2. Incrementar um contador no softc.
3. Retornar `FILTER_HANDLED`.

Só isso. Sem leitura de registrador de status, sem reconhecimento, sem trabalho diferido. O contador permite que você observe se o handler é disparado e, em caso afirmativo, com que frequência. A saída do `dmesg` é silenciosa por padrão; a contagem é visível por meio de um sysctl que o estágio expõe.

A Seção 4 adiciona o trabalho real (decodificação de status, reconhecimento e escalonamento de ithread). A Seção 5 adiciona interrupções simuladas para teste. A Seção 6 estende o filtro para que ele seja seguro em cenários de IRQ compartilhado, verificando se a interrupção é realmente destinada ao nosso dispositivo. Mas a estrutura base é a contribuição desta etapa, e ela precisa estar correta antes que qualquer outra coisa seja acrescentada sobre ela.

### A Alocação do Recurso de IRQ

A primeira nova linha do attach, logo após a alocação do BAR:

```c
sc->irq_rid = 0;   /* legacy PCI INTx */
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
    RF_SHAREABLE | RF_ACTIVE);
if (sc->irq_res == NULL) {
	device_printf(dev, "cannot allocate IRQ\n");
	error = ENXIO;
	goto fail_hw;
}
```

Alguns pontos merecem atenção.

Primeiro, `rid = 0` é a convenção legada de INTx do PCI. Todo dispositivo PCI possui uma única linha de IRQ legada, anunciada pelos campos `PCIR_INTLINE` e `PCIR_INTPIN` do espaço de configuração; o driver de barramento PCI expõe isso como recurso rid 0. O Capítulo 20 usará rids diferentes de zero para vetores MSI e MSI-X, mas o driver do Capítulo 19 usa o caminho legado.

Segundo, a variável `rid` é atualizada por `bus_alloc_resource_any` caso o kernel tenha escolhido um rid diferente do solicitado. Para `rid = 0`, o kernel sempre retorna `rid = 0`, portanto a atualização não tem efeito prático, mas o padrão é consistente com a alocação de BAR do Capítulo 18.

Terceiro, `RF_SHAREABLE | RF_ACTIVE` é o conjunto de flags padrão. `RF_SHAREABLE` permite que o kernel coloque nosso handler em um `intr_event` compartilhado com outros drivers. `RF_ACTIVE` ativa o recurso em uma única etapa.

Quarto, a alocação pode falhar. O motivo mais comum em um sistema real é que os campos de interrupção do espaço de configuração PCI do dispositivo são zero (o firmware não roteou uma interrupção para o dispositivo). No bhyve com um dispositivo virtio-rnd, a alocação normalmente é bem-sucedida; em algumas configurações mais antigas do QEMU com `intx=off`, ela pode falhar. Se a alocação falhar, o caminho de attach se desfaz percorrendo a cascata de goto.

### Armazenando o Recurso no Softc

O softc ganha três novos campos:

```c
struct myfirst_softc {
	/* ... existing fields ... */

	/* Chapter 19 interrupt fields. */
	struct resource	*irq_res;
	int		 irq_rid;
	void		*intr_cookie;     /* for bus_teardown_intr */
	uint64_t	 intr_count;      /* handler invocation count */
};
```

`irq_res` é o handle para o recurso de IRQ alocado. `irq_rid` é o ID do recurso (para a chamada de liberação correspondente). `intr_cookie` é o cookie opaco que `bus_setup_intr(9)` retorna e `bus_teardown_intr(9)` consome; ele identifica o handler específico para que o kernel possa removê-lo corretamente mais tarde. `intr_count` é um contador de diagnóstico que o handler do Estágio 1 incrementa a cada chamada.

Os três campos seguem o mesmo padrão dos três campos de BAR (`bar_res`, `bar_rid`, `pci_attached`) adicionados no Capítulo 18. Esse paralelismo não é acidental: toda classe de recurso recebe um handle, um ID e os dados auxiliares de controle que o driver necessite.

### A Assinatura do Handler de Filter

O filter do driver é uma função com a seguinte assinatura:

```c
static int myfirst_intr_filter(void *arg);
```

O argumento é o ponteiro que o driver passa ao parâmetro `arg` de `bus_setup_intr`; por convenção, um ponteiro para o softc do driver. O valor de retorno é um OR bit a bit de `FILTER_STRAY`, `FILTER_HANDLED` e `FILTER_SCHEDULE_THREAD`, conforme descrito na Seção 2.

A implementação do Estágio 1:

```c
static int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;

	atomic_add_64(&sc->intr_count, 1);
	return (FILTER_HANDLED);
}
```

Uma linha de trabalho real. O contador é incrementado atomicamente porque o handler pode ser executado concorrentemente em múltiplos CPUs (cenários MSI-X) ou em paralelo com código fora de interrupção que lê o contador pelo sysctl. Um sleep lock seria um erro em um filter; uma operação atômica é a primitiva leve que é segura nesse contexto.

O valor de retorno é `FILTER_HANDLED` porque não temos trabalho de ithread a fazer e nenhum motivo para retornar `FILTER_STRAY` (a Seção 6 adiciona a verificação de stray; o Estágio 1 assume que o IRQ é nosso).

### Registrando o Handler com bus_setup_intr

Após a alocação do IRQ, o driver chama `bus_setup_intr(9)`:

```c
error = bus_setup_intr(dev, sc->irq_res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    myfirst_intr_filter, NULL, sc,
    &sc->intr_cookie);
if (error != 0) {
	device_printf(dev, "bus_setup_intr failed (%d)\n", error);
	goto fail_release_irq;
}
```

Os sete argumentos:

1. **`dev`**: o handle do dispositivo.
2. **`sc->irq_res`**: o recurso de IRQ que acabamos de alocar.
3. **`INTR_TYPE_MISC | INTR_MPSAFE`**: as flags. `INTR_TYPE_MISC` categoriza a interrupção (Seção 2). `INTR_MPSAFE` garante que o handler cuida de sua própria sincronização.
4. **`myfirst_intr_filter`**: nosso handler de filter. Não-NULL.
5. **`NULL`**: o handler de ithread. NULL porque o Estágio 1 usa apenas o filter.
6. **`sc`**: o argumento passado para ambos os handlers.
7. **`&sc->intr_cookie`**: parâmetro de saída onde o kernel armazena o cookie para uso na desmontagem posterior.

O valor de retorno é 0 em caso de sucesso, ou um errno em caso de falha. Uma falha nesse ponto é rara; a causa mais comum é uma restrição do controlador de interrupções ou específica da plataforma.

Um `bus_setup_intr` bem-sucedido combinado com o `device_printf` abaixo produz uma mensagem curta no `dmesg` quando o driver é carregado:

```text
myfirst0: attached filter handler on IRQ resource
```

O número do IRQ em si não aparece nessa linha; `devinfo -v` e `vmstat -i` o exibem (o número do IRQ depende da configuração do guest). Se você ver uma linha adicional `myfirst0: [GIANT-LOCKED]`, o argumento de flags está sem `INTR_MPSAFE` e o kernel está avisando que o Giant está sendo adquirido ao redor do handler; corrija isso.

### Descrevendo o Handler para o devinfo

Uma etapa opcional, mas recomendada. `bus_describe_intr(9)` permite que o driver associe um nome legível ao handler, que `devinfo -v` e os diagnósticos do kernel utilizarão:

```c
bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "legacy");
```

Após essa chamada, `vmstat -i` exibe a linha do handler como `irq19: myfirst0:legacy` em vez do simples `irq19: myfirst0`. O sufixo é o nome fornecido pelo driver. Para o driver de interrupção única do Capítulo 19, o sufixo é quase decorativo; para o driver MSI-X do Capítulo 20 com múltiplos vetores, ele se torna essencial para distinguir `rx0`, `rx1`, `tx0`, `admin` e assim por diante.

### A Cascata de Attach Estendida

Incorporando as novas partes ao attach do Estágio 3 do Capítulo 18:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error, capreg;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: walk PCI capabilities (informational). */
	if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0)
		device_printf(dev, "PCIe capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0)
		device_printf(dev, "MSI capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0)
		device_printf(dev, "MSI-X capability at 0x%x\n", capreg);

	/* Step 3: attach the hardware layer against the BAR. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release_bar;

	/* Step 4: allocate the IRQ. */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "cannot allocate IRQ\n");
		error = ENXIO;
		goto fail_hw;
	}

	/* Step 5: register the filter handler. */
	error = bus_setup_intr(dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		device_printf(dev, "bus_setup_intr failed (%d)\n", error);
		goto fail_release_irq;
	}
	bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "legacy");
	device_printf(dev, "attached filter handler on IRQ resource\n");

	/* Step 6: create the cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_teardown_intr;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 7: read a diagnostic word from the BAR. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_teardown_intr:
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	sc->intr_cookie = NULL;
fail_release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	sc->irq_res = NULL;
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

A sequência de attach agora possui sete etapas em vez de cinco. Dois novos rótulos de goto (`fail_teardown_intr`, `fail_release_irq`) estendem a cascata. O padrão é o mesmo do Capítulo 18: cada etapa desfaz a anterior, encadeando até a inicialização do softc.

### O Detach Estendido

O caminho de detach espelha o attach, com a desmontagem da interrupção inserida entre a desmontagem do cdev e o detach da camada de hardware:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Destroy the cdev so no new user-space access starts. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Tear down the interrupt handler before anything it depends on. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Quiesce callouts and tasks (includes Chapter 17 simulation if
	 * attached; includes any deferred taskqueue work). */
	myfirst_quiesce(sc);

	/* Release the Chapter 17 simulation if attached. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer. */
	myfirst_hw_detach(sc);

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

Duas mudanças em relação ao Capítulo 18: a chamada a `bus_teardown_intr` e a chamada a `bus_release_resource(..., SYS_RES_IRQ, ...)`. A ordem é importante. `bus_teardown_intr` deve ocorrer antes que qualquer coisa que o handler leia ou escreva seja liberada; em especial, antes de `myfirst_hw_detach` (que libera `sc->hw`). Após o retorno de `bus_teardown_intr`, o kernel garante que o handler não está em execução e não será chamado novamente; o driver pode então liberar tudo que o handler acessou.

A liberação do recurso de IRQ ocorre após a desmontagem e após o detach da camada de hardware. A posição exata entre o detach do hardware e a liberação do BAR é uma decisão de projeto: o BAR e o IRQ não dependem um do outro, portanto qualquer ordem funciona. O driver do Capítulo 19 libera o IRQ primeiro porque essa é a ordem inversa do attach (o attach alocou o IRQ após o BAR; o detach o libera antes do BAR).

A Seção 7 aborda o ordenamento com mais detalhes.

### O sysctl para o Contador de Interrupções

Um pequeno diagnóstico: um sysctl que expõe o campo `intr_count` para que o leitor possa acompanhar o crescimento do contador:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_count",
    CTLFLAG_RD, &sc->intr_count, 0,
    "Number of times the interrupt filter has run");
```

Após o carregamento, `sysctl dev.myfirst.0.intr_count` retorna a contagem atual. Para o dispositivo virtio-rnd sem interrupções sendo disparadas (o dispositivo ainda não tem nada a sinalizar), a contagem permanece em zero. As interrupções simuladas da Seção 5 farão a contagem subir sem a necessidade de eventos reais de IRQ.

O `sysctl` é legível por qualquer usuário (o `CTLFLAG_RD` o torna legível por todos no nível do sysctl; as permissões de arquivo no MIB do sysctl são definidas em outro lugar). O acesso é feito por:

```sh
sysctl dev.myfirst.0.intr_count
```

### O que o Estágio 1 Comprova

Carregar o driver do Estágio 1 em um guest bhyve com um dispositivo virtio-rnd produz:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: attached filter handler on IRQ resource
myfirst0: BAR[0x00] = 0x10010000
```

`vmstat -i | grep myfirst` exibe o evento de interrupção criado pelo kernel:

```text
irq19: myfirst0:legacy              0          0
```

(O número do IRQ e a taxa dependem do ambiente.)

A contagem inicial é zero porque o dispositivo virtio-rnd ainda não está gerando interrupções (não o programamos para isso). O driver está corretamente conectado, o handler está registrado e o filter está pronto para disparar. O trabalho do Estágio 1 está concluído.

### O que o Estágio 1 Não Faz

Diversas coisas estão deliberadamente ausentes do Estágio 1 e aparecem em estágios posteriores:

- **Leitura do registrador de status.** O filter não lê o INTR_STATUS do dispositivo; ele apenas incrementa um contador. A Seção 4 adiciona a leitura do status.
- **Reconhecimento.** O filter não escreve no INTR_STATUS para reconhecer a interrupção. Em uma linha disparada por nível com um dispositivo que está de fato enviando interrupções, isso é um bug. Em nosso alvo virtio-rnd no bhyve, o dispositivo não está disparando, portanto a ausência é invisível. A Seção 4 adiciona o reconhecimento e explica por que ele importa.
- **Handler de ithread.** Ainda não há trabalho diferido. A Seção 4 introduz um caminho diferido baseado em taskqueue e conecta o filter para agendá-lo.
- **Interrupções simuladas.** Não há como fazer o handler disparar sem um IRQ real do dispositivo. A Seção 5 adiciona um sysctl que invoca o filter diretamente sob as regras normais de lock do driver.
- **Disciplina de IRQ compartilhado.** O filter assume que toda interrupção pertence ao nosso dispositivo. A Seção 6 adiciona a verificação de `FILTER_STRAY` para dispositivos que compartilham uma linha.

Esses são os temas das Seções 4, 5 e 6. O Estágio 1 é deliberadamente incompleto; cada seção posterior adiciona algo específico que o Estágio 1 deixou de fora.

### Erros Comuns Nesta Etapa

Uma lista curta de armadilhas que iniciantes encontram no Estágio 1.

**Esquecer `INTR_MPSAFE`.** O handler é envolvido pelo Giant. A escalabilidade desaparece. O `dmesg` imprime `[GIANT-LOCKED]`. Correção: adicione `INTR_MPSAFE` ao argumento de flags.

**Passar o argumento errado para o filter.** Um ponteiro de função em C é exigente; passar `&sc` em vez de `sc` produz um ponteiro duplo que o filter desreferencia incorretamente. O resultado costuma ser um kernel panic. Correção: o `arg` em `bus_setup_intr` é `sc`; o filter recebe o mesmo valor como `void *`.

**Retornar 0 do filter.** O valor de retorno é um OR bit a bit dos valores `FILTER_*`. Zero equivale a "nenhuma flag", o que é ilegal (o kernel exige pelo menos um entre `FILTER_STRAY`, `FILTER_HANDLED` ou `FILTER_SCHEDULE_THREAD`). O kernel de debug faz um assert nesse caso. Correção: retorne `FILTER_HANDLED`.

**Usar um sleep lock no filter.** O filter adquire `sc->mtx` (um mutex de sleep comum). O `WITNESS` reclama; o kernel de debug entra em pânico. Correção: use operações atômicas ou mova o trabalho para um ithread.

**Desmontar o IRQ antes de consumir o cookie.** Chamar `bus_release_resource` no IRQ antes de `bus_teardown_intr` é um bug: o recurso foi liberado, mas o handler ainda está registrado nele. A próxima interrupção dispara e o kernel desreferencia um estado já liberado. Correção: sempre chame `bus_teardown_intr` primeiro.

**rid incompatível.** O rid passado para `bus_release_resource` deve corresponder ao rid que `bus_alloc_resource_any` retornou (ou ao rid passado inicialmente, para `rid = 0`). Uma incompatibilidade geralmente aparece como "Resource not found" ou uma mensagem do kernel. Correção: armazene o rid no softc ao lado do handle do recurso.

**Esquecer de drenar o trabalho diferido pendente antes da desmontagem.** Isso se aplica mais no Estágio 2, mas vale a pena mencionar aqui: se o filter agendou um item no taskqueue, o item deve ser concluído antes que o softc seja liberado. Uma desmontagem que libera o IRQ mas deixa um item pendente no taskqueue produz um use-after-free quando o item for executado.

### Verificação: Estágio 1 Funcionando

Antes da Seção 4, confirme que o Estágio 1 está funcionando:

- `kldstat -v | grep myfirst` exibe o driver na versão `1.2-intr-stage1`.
- `dmesg | grep myfirst` exibe o banner de attach incluindo `attached filter handler on IRQ resource`.
- Nenhum aviso `[GIANT-LOCKED]`.
- `devinfo -v | grep -A 5 myfirst` exibe tanto o BAR quanto o recurso de IRQ.
- `vmstat -i | grep myfirst` exibe a linha do handler.
- `sysctl dev.myfirst.0.intr_count` retorna `0` (ou um número pequeno, dependendo de o dispositivo gerar interrupções ou não).
- `kldunload myfirst` é executado sem problemas; sem panic, sem avisos.

Se alguma etapa falhar, retorne à subseção correspondente. As falhas são diagnosticadas da mesma forma que as do Capítulo 18: verifique o `dmesg` para os banners, `devinfo -v` para os recursos e a saída do `WITNESS` para problemas de ordenação de locks.

### Encerrando a Seção 3

Registrar um handler de interrupção envolve três novas chamadas (`bus_alloc_resource_any` com `SYS_RES_IRQ`, `bus_setup_intr`, `bus_describe_intr`), três novos campos no softc (`irq_res`, `irq_rid`, `intr_cookie`) e um novo contador (`intr_count`). A cascata de attach cresce com dois rótulos de erro; o caminho de detach cresce com a chamada `bus_teardown_intr`. O próprio handler é um incremento atômico de uma linha que retorna `FILTER_HANDLED`.

O objetivo do Estágio 1 não é o trabalho que o handler realiza. O que importa é que o handler esteja corretamente registrado, que `INTR_MPSAFE` esteja definido, que o contador incremente quando uma interrupção ocorrer e que o teardown execute de forma limpa durante o descarregamento do módulo. Todo estágio posterior é construído sobre esse andaime; acertar agora é o investimento que rende frutos ao longo do restante do capítulo.

A Seção 4 faz o handler realizar trabalho de verdade: ler o `INTR_STATUS`, decidir o que fazer, fazer o acknowledge do dispositivo e delegar o trabalho em maior volume para uma taskqueue. Esse é o núcleo de um handler de interrupção real, e o conteúdo mais relevante para o restante da Parte 4.

## Seção 4: Escrevendo um Tratador de Interrupção Real

O Estágio 1 provou que a conexão do tratador está correta. O Estágio 2 faz o tratador executar o trabalho que o filtro de um driver real realiza. A estrutura da Seção 4 é um percurso detalhado pelo modelo de hardware da simulação do Capítulo 17, com o filtro agora lendo e reconhecendo o registrador `INTR_STATUS` do dispositivo, tomando decisões com base em quais bits estão ativos, tratando o trabalho urgente e pequeno de forma inline e adiando o trabalho mais pesado para um taskqueue. Ao final da Seção 4, o driver está na versão `1.2-intr-stage2` e possui um pipeline de filtro mais task que se comporta como um pequeno driver real.

### O Mapa de Registradores

Um resumo rápido do layout dos registradores de interrupção da simulação do Capítulo 17 (veja `HARDWARE.md` para os detalhes completos). O offset `0x14` contém `INTR_STATUS`, um registrador de 32 bits com os seguintes bits definidos:

- `MYFIRST_INTR_DATA_AV` (`0x00000001`): um evento de dado disponível ocorreu.
- `MYFIRST_INTR_ERROR` (`0x00000002`): uma condição de erro foi detectada.
- `MYFIRST_INTR_COMPLETE` (`0x00000004`): um comando foi concluído.

O registrador tem semântica "write-one-to-clear" (RW1C): escrever 1 em um bit limpa esse bit; escrever 0 o deixa inalterado. Esta é a convenção padrão de status de interrupção PCI e é o que o tratador do Capítulo 19 espera.

O offset `0x10` contém `INTR_MASK`, um registrador paralelo que controla quais bits de `INTR_STATUS` efetivamente ativam a linha de IRQ. Definir um bit em `INTR_MASK` habilita essa classe de interrupção; limpá-lo a desabilita. O driver define `INTR_MASK` no momento do attach para habilitar as interrupções que deseja receber.

A simulação do Capítulo 17 pode acionar esses bits de forma autônoma. O driver PCI do Capítulo 18 opera contra um BAR real de virtio-rnd, onde os offsets têm um significado diferente (configuração legada de virtio, não o mapa de registradores do Capítulo 17). A Seção 4 escreve o tratador de acordo com a semântica do Capítulo 17; a Seção 5 mostra como exercitar o tratador sem eventos de IRQ reais; o dispositivo virtio-rnd não implementa esse layout de registradores, portanto, no laboratório de bhyve, o tratador é exercitado principalmente pelo caminho de interrupção simulada.

Essa é uma limitação honesta do alvo de ensino. Um leitor que adapte o driver para um dispositivo real que implemente registradores no estilo do Capítulo 17 veria o tratador disparar em interrupções reais diretamente. Para o alvo de bhyve com virtio-rnd, o mecanismo de disparo de interrupção simulada via sysctl da Seção 5 é a forma de exercitar o filtro do Estágio 2 na prática.

### O Filtro no Estágio 2

O filtro do Estágio 2 lê `INTR_STATUS`, decide o que aconteceu, reconhece os bits que está tratando e, ou faz o trabalho urgente de forma inline, ou agenda uma task para o trabalho mais pesado.

```c
int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;
	uint32_t status;
	int rv = 0;

	/*
	 * Read the raw status. The filter runs in primary interrupt
	 * context and cannot take sc->mtx (a sleep mutex), so the access
	 * goes through the specialised accessor that asserts the correct
	 * context. We use a local, spin-safe helper for the BAR access;
	 * Stage 2 uses a small inline instead of the lock-asserting
	 * CSR_READ_4 macro.
	 */
	status = bus_read_4(sc->bar_res, MYFIRST_REG_INTR_STATUS);
	if (status == 0)
		return (FILTER_STRAY);

	atomic_add_64(&sc->intr_count, 1);

	/* Handle the DATA_AV bit: small urgent work only. */
	if (status & MYFIRST_INTR_DATA_AV) {
		atomic_add_64(&sc->intr_data_av_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		taskqueue_enqueue(sc->intr_tq, &sc->intr_data_task);
		rv |= FILTER_HANDLED;
	}

	/* Handle the ERROR bit: log and acknowledge. */
	if (status & MYFIRST_INTR_ERROR) {
		atomic_add_64(&sc->intr_error_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_ERROR);
		rv |= FILTER_HANDLED;
	}

	/* Handle the COMPLETE bit: wake any pending waiters. */
	if (status & MYFIRST_INTR_COMPLETE) {
		atomic_add_64(&sc->intr_complete_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		rv |= FILTER_HANDLED;
	}

	/* If we didn't recognise any bit, this wasn't our interrupt. */
	if (rv == 0)
		return (FILTER_STRAY);

	return (rv);
}
```

Há várias coisas que merecem atenção cuidadosa.

**O acesso direto.** O filtro usa `bus_read_4` e `bus_write_4` (os acessores mais novos baseados em resource) diretamente, não os macros `CSR_READ_4` e `CSR_WRITE_4` do Capítulo 16. A razão é sutil. Os macros do Capítulo 16 adquirem `sc->mtx` via `MYFIRST_ASSERT`, que é um sleep mutex. Um filtro não pode adquirir um sleep mutex. A abordagem correta é usar os acessores `bus_space` diretamente (como mostrado) ou introduzir uma família paralela de macros CSR que não exijam nenhum lock. O refactor da Seção 8 introduz `ICSR_READ_4` e `ICSR_WRITE_4` ("I" de interrupt-context) para tornar essa distinção explícita; o Estágio 2 usa os acessores diretos.

**A verificação precoce de stray.** Um status igual a zero significa que nenhum bit está ativo; esta é uma chamada de IRQ compartilhado originada em outro driver. Retornar `FILTER_STRAY` permite que o kernel tente o próximo tratador. A verificação também é uma defesa contra uma condição de corrida real de hardware: se o controlador de interrupção ativa a linha, mas o dispositivo já limpou o status antes de o lermos, não devemos reivindicar a interrupção.

**O tratamento por bit.** Cada bit de interesse é verificado, contado e reconhecido. A ordem não importa (os bits são independentes), mas a estrutura é convencional: um `if` por bit.

**O reconhecimento.** Escrever o bit de volta em `INTR_STATUS` o limpa (RW1C). É isso que faz a linha de interrupção ser desativada. Deixar de reconhecer em uma linha disparada por nível produz uma tempestade de interrupções.

**O enfileiramento no taskqueue.** O bit `DATA_AV` dispara o trabalho adiado. O filtro enfileira uma task; a thread trabalhadora do taskqueue executa a task posteriormente em contexto de thread, onde pode adquirir sleep locks e fazer trabalho lento. O enfileiramento é seguro para chamar a partir de um filtro (os taskqueues usam spin locks internamente para esse caminho).

**O valor de retorno final.** Um OR bit a bit de `FILTER_HANDLED` para cada bit reconhecido, ou `FILTER_STRAY` se nada correspondeu. Se tivéssemos trabalho para uma ithread, incluiríamos `FILTER_SCHEDULE_THREAD` com OR; mas o Estágio 2 usa um taskqueue em vez da ithread, portanto o valor de retorno é apenas `FILTER_HANDLED`.

### Por Que Taskqueue e Não ithread?

O FreeBSD permite que um driver registre um tratador de ithread pelo quinto argumento de `bus_setup_intr(9)`. Por que o Estágio 2 usa um taskqueue em vez disso?

Dois motivos.

Primeiro, o taskqueue é mais flexível. Uma ithread está vinculada ao `intr_event` específico; ela executa a função de ithread do driver após o filtro. Um taskqueue permite que o driver agende uma task a partir de qualquer contexto (filtro, ithread, outras tasks, caminhos de ioctl do espaço do usuário) e a tenha executada em uma thread trabalhadora compartilhada. Para o driver do Capítulo 19, que exercita o tratador por meio de interrupções simuladas além das reais, o taskqueue é um primitivo de trabalho adiado mais uniforme.

Segundo, o taskqueue separa a prioridade do tipo de interrupção. A prioridade da ithread é derivada de `INTR_TYPE_*`; a prioridade do taskqueue é controlada por `taskqueue_start_threads(9)`. Para drivers que desejam que seu trabalho adiado ocorra em uma prioridade diferente da que a categoria de interrupção implica, o taskqueue oferece esse controle.

Drivers reais do FreeBSD usam ambos os padrões. Drivers simples com interrupções do tipo "dispara e esquece" frequentemente usam a ithread (menos código). Drivers com padrões de trabalho adiado mais ricos usam taskqueues. O framework `iflib(9)` usa uma espécie de híbrido.

O Capítulo 19 ensina o padrão com taskqueue porque ele se compõe melhor com o restante do livro. O Capítulo 17 já tem um taskqueue; o Capítulo 14 introduziu o padrão; a disciplina de trabalho adiado é um tema que percorre o livro inteiro.

### A Task de Trabalho Adiado

O filtro enfileirou `sc->intr_data_task` ao ver `DATA_AV`. Essa task é:

```c
static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);

	/*
	 * The data-available event has fired. Read the device's data
	 * register through the Chapter 16 accessor (which takes sc->mtx
	 * implicitly), update the driver's state, and wake any waiting
	 * readers.
	 */
	uint32_t data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_last_data = data;
	sc->intr_task_invocations++;

	/* Wake any thread sleeping on the data-ready condition. */
	cv_broadcast(&sc->data_cv);

	MYFIRST_UNLOCK(sc);
}
```

Algumas propriedades notáveis.

**A task executa em contexto de thread.** Ela pode adquirir `sc->mtx`, usar `cv_broadcast`, chamar `malloc(M_WAITOK)` e fazer trabalho lento.

**A task respeita a disciplina de locking do Capítulo 11.** O mutex é adquirido; o acesso ao CSR usa o macro padrão do Capítulo 16; o broadcast da variável de condição usa o primitivo do Capítulo 12.

**O argumento da task é o softc.** Igual ao filtro. Uma implicação sutil: a task não pode presumir que o driver não foi desanexado. Se o detach ocorrer após o filtro ter enfileirado a task, mas antes de ela ser executada, a task poderá ser executada contra um softc liberado. A Seção 7 cobre a disciplina que evita isso (drain antes da liberação).

**O argumento `npending`** é o número de vezes que a task foi enfileirada desde sua última execução. Para a maioria dos drivers, isso é útil como uma dica de coalescência: se `npending` for 5, o dispositivo sinalizou cinco eventos de dado-pronto que coalescem em uma única execução. A task do Estágio 2 o ignora; drivers maiores o usam para dimensionar operações em lote.

### Declarando e Inicializando a Task

O softc ganha campos relacionados à task:

```c
struct myfirst_softc {
	/* ... existing fields ... */

	/* Chapter 19 interrupt-related fields. */
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	uint64_t		 intr_count;
	uint64_t		 intr_data_av_count;
	uint64_t		 intr_error_count;
	uint64_t		 intr_complete_count;
	uint64_t		 intr_task_invocations;
	uint32_t		 intr_last_data;

	struct taskqueue	*intr_tq;
	struct task		 intr_data_task;
};
```

Em `myfirst_init_softc` (ou no caminho de inicialização):

```c
TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
    taskqueue_thread_enqueue, &sc->intr_tq);
taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
    "myfirst intr taskq");
```

O taskqueue é criado com uma thread trabalhadora na prioridade `PI_NET` (uma prioridade de interrupção; veja `/usr/src/sys/sys/priority.h`). O nome `"myfirst intr taskq"` aparece em `top -H` para diagnóstico. O `M_WAITOK` durante a criação é seguro porque `myfirst_init_softc` é executado no contexto de attach, antes de qualquer interrupção disparar.

### Habilitando as Interrupções no Dispositivo

Um detalhe frequentemente esquecido: o próprio dispositivo deve ser informado para entregar interrupções. Para o layout de registradores da simulação do Capítulo 17, isso é feito definindo bits no registrador `INTR_MASK`:

```c
/* After attaching the hardware layer, enable the interrupts we care
 * about. */
MYFIRST_LOCK(sc);
CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
    MYFIRST_INTR_COMPLETE);
MYFIRST_UNLOCK(sc);
```

O registrador `INTR_MASK` controla quais bits de `INTR_STATUS` efetivamente ativam a linha de IRQ. Sem ele, o dispositivo pode definir bits de `INTR_STATUS` internamente, mas nunca elevar a linha, de modo que o tratador nunca dispara. Definir os três bits habilita as três classes de interrupção.

Essa é outra limitação honesta do alvo de ensino. O offset `0x10` no BAR legado de virtio-rnd não é um registrador de máscara de interrupção. No layout virtio legado (veja `/usr/src/sys/dev/virtio/pci/virtio_pci_legacy_var.h`), o dword a partir do offset `0x10` é compartilhado por três campos pequenos: `queue_notify` no offset `0x10` (16 bits), `device_status` no offset `0x12` (8 bits) e `isr_status` no offset `0x13` (8 bits). Uma escrita de 32 bits com o padrão `DATA_AV | ERROR | COMPLETE` (`0x00000007`) nesse offset escreve `0x0007` em `queue_notify` (notificando um índice de virtqueue que o dispositivo não possui) e `0x00` em `device_status` (que a especificação virtio define como um **reset do dispositivo**). Escrever zero em `device_status` é como o driver virtio deve resetar o dispositivo antes da reinicialização.

Por esse motivo, a chamada `CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, ...)` como escrita é **segura, mas inútil** no alvo de bhyve com virtio-rnd: ela reseta a máquina de estado virtio do dispositivo (que o nosso driver não estava usando de qualquer forma) e nunca habilita nenhuma interrupção real, porque o registrador `INTR_MASK` do Capítulo 17 não existe nesse dispositivo. Se você planeja guiar um leitor por isso no bhyve, mantenha a escrita no código para garantir continuidade com um dispositivo real compatível com o Capítulo 17 e confie no sysctl de interrupção simulada da Seção 5 para os testes, em vez de esperar eventos de IRQ reais. Um leitor que adaptar o driver para um dispositivo real que corresponda ao mapa de registradores do Capítulo 17 veria a escrita da máscara funcionar corretamente.

### Desabilitando as Interrupções no Detach

O passo simétrico no detach:

```c
/* Disable all interrupts at the device before tearing down. */
MYFIRST_LOCK(sc);
if (sc->hw != NULL)
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
MYFIRST_UNLOCK(sc);
```

Essa escrita ocorre antes de `bus_teardown_intr` para que o dispositivo pare de ativar a linha antes de o tratador ser removido. O guarda contra `sc->hw == NULL` protege contra casos de attach parcial em que a camada de hardware falhou; o disable é ignorado se o hardware não estiver conectado.

### Um Fluxo Concreto

Um rastreamento concreto do que acontece quando um evento `DATA_AV` dispara (em um dispositivo que realmente implementa a semântica do Capítulo 17):

1. O dispositivo define `INTR_STATUS.DATA_AV`. Como `INTR_MASK.DATA_AV` está definido, o dispositivo ativa sua linha de IRQ.
2. O controlador de interrupção roteia o IRQ para uma CPU.
3. A CPU recebe a interrupção e salta para o código de despacho do kernel.
4. O kernel encontra o `intr_event` para o nosso IRQ e chama `myfirst_intr_filter`.
5. O filtro lê `INTR_STATUS`, vê `DATA_AV`, incrementa os contadores, escreve `DATA_AV` de volta em `INTR_STATUS` (limpando-o), enfileira `intr_data_task` e retorna `FILTER_HANDLED`.
6. O dispositivo desativa sua linha de IRQ (porque `INTR_STATUS.DATA_AV` agora está limpo).
7. O kernel envia EOI e retorna à thread interrompida.
8. Alguns milissegundos depois, a thread trabalhadora do taskqueue acorda, executa `myfirst_intr_data_task_fn`, lê `DATA_OUT`, atualiza o softc e faz o broadcast da variável de condição.
9. Qualquer thread que estava aguardando na variável de condição acorda e prossegue.

Os passos 1 a 7 levam alguns microssegundos. O passo 8 pode levar centenas de microssegundos ou mais, razão pela qual está em contexto de thread. Essa separação é o que permite que o caminho de interrupção permaneça rápido.

Para o alvo de bhyve com virtio-rnd, os passos 1 a 6 não acontecem (o dispositivo não corresponde ao layout de registradores do Capítulo 17). Os passos 4 a 9 ainda podem ser exercitados pelo caminho de interrupção simulada da Seção 5.

### Trabalho Urgente Inline vs Adiado

Uma forma útil de decidir o que vai no filtro versus na task: o filtro trata o que deve ser feito **por interrupção**, a task trata o que deve ser feito **por evento**.

Por interrupção (filter):
- Ler `INTR_STATUS` para identificar o evento.
- Confirmar o evento no dispositivo (escrever de volta em `INTR_STATUS`).
- Atualizar contadores.
- Tomar uma única decisão de escalonamento (enfileirar a task).

Por evento (task):
- Ler dados dos registradores do dispositivo ou dos buffers de DMA.
- Atualizar a máquina de estados interna do driver.
- Acordar as threads em espera.
- Passar os dados para a pilha de rede, a pilha de armazenamento ou a fila do cdev.
- Tratar erros que exigem recuperação lenta.

A regra geral: se o filter leva mais de cem ciclos de CPU de trabalho real (sem contar os acessos a registradores, que por si só são baratos), ele provavelmente está fazendo demais.

### `FILTER_SCHEDULE_THREAD` vs Taskqueue

Um leitor pode perguntar: quando eu usaria `FILTER_SCHEDULE_THREAD` em vez de um taskqueue?

Use `FILTER_SCHEDULE_THREAD` quando:
- Você quiser que a ithread do kernel por evento (uma por `intr_event`) execute o trabalho lento.
- Você não precisar agendar o trabalho a partir de nenhum lugar além do filter.
- Você quiser que a prioridade de escalonamento siga o `INTR_TYPE_*` da interrupção.

Use um taskqueue quando:
- Você quiser agendar o mesmo trabalho a partir de múltiplos caminhos (filter, ioctl, sysctl, timeout com sleep).
- Você quiser compartilhar a thread de trabalho entre múltiplos dispositivos.
- Você quiser controle explícito sobre a prioridade via `taskqueue_start_threads`.

Para o driver do Capítulo 19, o taskqueue é a escolha mais limpa porque a Seção 5 agendará a mesma task a partir de um caminho de interrupção simulada. Uma ithread não seria alcançável a partir dali.

### Quando o Próprio Taskqueue É a Escolha Errada

Uma ressalva. O taskqueue é ótimo para trabalhos diferidos curtos. Não é ideal para operações de longa duração. Se o driver precisar executar uma máquina de estados por vários segundos, ou bloquear aguardando uma transferência USB, ou processar uma longa cadeia de buffers, uma thread de trabalho dedicada é mais adequada. A thread de trabalho do taskqueue é compartilhada entre tasks; uma única task que bloqueie por um longo período atrasa todas as outras tasks na fila.

A task do Capítulo 19 executa em microssegundos. O taskqueue é perfeito. O driver MSI-X do Capítulo 20, com processamento de recebimento por fila, pode querer threads de trabalho por fila. A transferência em massa baseada em DMA do Capítulo 21 pode querer uma thread dedicada. Cada capítulo escolhe a primitiva certa para sua carga de trabalho; o Capítulo 19 usa a mais simples que se encaixa.

### Erros Comuns Nesta Etapa

Uma lista breve.

**Ler `INTR_STATUS` sem reconhecer a interrupção.** O handler lê, decide e retorna sem escrever de volta. Em uma linha disparada por nível, o dispositivo continua sinalizando; o handler dispara novamente imediatamente; tempestade de interrupções. Correção: reconheça cada bit tratado.

**Reconhecer bits demais.** Um handler descuidado escreve `0xffffffff` em `INTR_STATUS` a cada chamada para "limpar todos os bits". Isso também limpa eventos que o handler não processou, descartando dados ou confundindo a máquina de estados. Correção: reconheça apenas os bits que você de fato tratou.

**Tomar sleep locks no filter.** `MYFIRST_LOCK(sc)` adquire `sc->mtx`, que é um sleep mutex. No filter isso é um bug; o `WITNESS` causa panic. Correção: use operações atômicas no filter, e adquira o sleep mutex apenas na task (que executa em contexto de thread).

**Agendar uma task depois que o softc foi desmontado.** Se a task é agendada pelo filter mas o filter executa depois que o detach desmontou parcialmente o driver, a task opera sobre estado obsoleto. Correção: a Seção 7 trata desta ordem. Brevemente: `bus_teardown_intr` deve acontecer antes que a camada de hardware seja liberada, e `taskqueue_drain` deve acontecer antes que o taskqueue seja liberado.

**Usar `CSR_READ_4`/`CSR_WRITE_4` diretamente no filter.** Se o accessor do Capítulo 16 afirma que `sc->mtx` deve estar adquirido (o que ocorre em kernels de depuração), o filter causa panic. Correção: use `bus_read_4`/`bus_write_4` diretamente ou introduza um conjunto paralelo de macros CSR seguras para interrupção. A Seção 8 trata isso com `ICSR_READ_4`.

**Enfileirar a task sem `TASK_INIT`.** Uma task enfileirada antes de `TASK_INIT` tem um ponteiro de função corrompido. A primeira execução da task salta para lixo. Correção: inicialize a task no caminho do attach antes de habilitar as interrupções.

**Esquecer de habilitar as interrupções no dispositivo.** O handler está registrado e `bus_setup_intr` retornou com sucesso; `vmstat -i` ainda mostra zero disparos. O problema é que o registrador `INTR_MASK` do dispositivo ainda está zerado (ou com qualquer valor que tenha após o reset), então o dispositivo nunca sinaliza a linha. Correção: escreva em `INTR_MASK` durante o attach.

**Esquecer de desabilitar as interrupções no detach.** O handler foi desmontado, mas o dispositivo ainda sinaliza a linha. O kernel eventualmente reclamará de uma interrupção extraviada, ou (pior) outro driver que compartilha a linha verá atividade misteriosa. Correção: limpe `INTR_MASK` antes de `bus_teardown_intr`.

### Resultado do Estágio 2: Como é o Sucesso

Após carregar o Estágio 2 em um dispositivo real que produz interrupções, o `dmesg` mostra:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: attached filter handler on IRQ resource
myfirst0: interrupts enabled (mask=0x7)
myfirst0: BAR[0x00] = 0x10010000
```

A linha `interrupts enabled` é nova. Ela confirma que o driver escreveu em `INTR_MASK`.

Em um dispositivo real gerando interrupções, `sysctl dev.myfirst.0.intr_count` incrementará. No alvo bhyve virtio-rnd, o contador permanece em zero porque o dispositivo não dispara as interrupções esperadas pelo nosso driver. O caminho de interrupção simulada da Seção 5 é a forma de exercitar o handler a partir dali.

### Encerrando a Seção 4

Um handler de interrupção real lê `INTR_STATUS` para identificar a causa, trata cada bit de interesse, reconhece os bits tratados escrevendo de volta em `INTR_STATUS`, e retorna a combinação correta de valores `FILTER_*`. O trabalho urgente (acesso a registradores, atualização de contadores, reconhecimentos) acontece no filter. O trabalho lento (leituras de dados pelos accessors do Capítulo 16 que adquirem `sc->mtx`, broadcasts em variáveis de condição, notificações ao espaço do usuário) acontece em uma task do taskqueue que o filter enfileira.

O filter é curto (vinte a quarenta linhas de código real para um dispositivo típico). A task também é curta (dez a trinta linhas). A composição é o que torna o driver funcional: o filter trata interrupções na taxa de interrupção; a task trata eventos na taxa de thread; essa divisão mantém a janela de interrupção curta e o trabalho diferido livre para bloquear.

A Seção 5 é a seção que permite ao leitor exercitar essa maquinaria em um alvo bhyve onde o caminho real de IRQ não corresponde à semântica de registradores do Capítulo 17. Ela adiciona um sysctl que invoca o filter sob as regras normais de lock do driver, permite ao leitor disparar interrupções simuladas à vontade, e confirma que os contadores, a task, e o broadcast na variável de condição se comportam como projetado.



## Seção 5: Usando Interrupções Simuladas para Testes

O filter e a task da Seção 4 são código de driver real. Estão prontos para tratar interrupções reais em um dispositivo que corresponda ao layout de registradores do Capítulo 17. O problema que o alvo de laboratório do Capítulo 19 apresenta é que o dispositivo que temos (virtio-rnd sob bhyve) não corresponde a esse layout; escrever os bits de máscara de interrupção do Capítulo 17 no BAR do virtio-rnd tem efeitos definidos mas sem relação, e ler os bits de status de interrupção do Capítulo 17 do BAR do virtio-rnd retorna valores específicos do virtio que não têm nada a ver com nossa semântica simulada. Nesse alvo, o filter, se disparar, verá lixo.

A Seção 5 resolve isso ensinando o leitor a simular interrupções. A ideia central é simples: expor um sysctl que, quando escrito, invoca o handler do filter diretamente sob as regras normais de lock do driver, exatamente como o kernel o invocaria a partir de uma interrupção real. O filter lê o registrador `INTR_STATUS` (que o leitor também pode ter escrito por meio de outro sysctl, ou pelo backend de simulação do Capítulo 17 em um build somente de simulação), toma as mesmas decisões que tomaria em uma interrupção real, e conduz o pipeline completo de ponta a ponta.

### Por Que a Simulação Merece uma Seção

Um leitor que terminou o Capítulo 17 pode razoavelmente perguntar: o Capítulo 17 inteiro de simulação já não era uma forma de simular interrupções? Sim e não.

O Capítulo 17 simulou um **dispositivo autônomo**. Seus callouts alteravam valores de registradores por conta própria, seu callout de comando disparava quando o driver escrevia `CTRL.GO`, seu framework de injeção de falhas fazia o dispositivo simulado se comportar mal. O driver do Capítulo 17 era um driver somente de simulação; não havia `bus_setup_intr` porque não havia um barramento real.

O Capítulo 19 é diferente. O driver agora tem um handler `bus_setup_intr` real registrado em uma linha IRQ real. Os callouts do Capítulo 17 não estão envolvidos; no build PCI a simulação do Capítulo 17 não executa. O que queremos é uma forma de disparar o **handler do filter** diretamente, com a semântica exata de lock que uma interrupção real produziria, para que possamos validar o pipeline de filter e task da Seção 4 sem depender de um dispositivo que realmente produza as interrupções corretas.

A forma mais limpa de fazer isso, e a forma que muitos drivers FreeBSD adotam para finalidades similares, é uma escrita em sysctl que invoca a função do filter diretamente. O filter executa no contexto do chamador (contexto de thread, de onde origina a escrita no sysctl), mas o código do filter não se importa com o contexto externo desde que a disciplina de lock interna esteja correta. Um incremento atômico, uma leitura do BAR, uma escrita no BAR, um `taskqueue_enqueue`: todos esses funcionam a partir do contexto de thread também. A chamada simulada exercita os mesmos caminhos de código que o kernel exercitaria em uma interrupção real.

Há uma distinção sutil. Em uma interrupção real, o kernel garante que os filters no mesmo `intr_event` executem serialmente em uma CPU. Uma chamada simulada disparada por sysctl não tem essa garantia; outra thread poderia invocar o filter ao mesmo tempo. Para o driver do Capítulo 19 isso está bem porque o estado do filter é protegido por operações atômicas (não pela garantia do kernel de uma CPU por IRQ). Para um driver que depende da serialização implícita em uma CPU, a simulação por sysctl não seria um teste fiel. A lição é: drivers `INTR_MPSAFE` que usam atomics e spin locks se traduzem bem para simulação.

### O sysctl de Interrupção Simulada

O mecanismo é um sysctl somente de escrita que invoca o filter:

```c
static int
myfirst_intr_simulate_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	uint32_t mask;
	int error;

	mask = 0;
	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/*
	 * "mask" is the INTR_STATUS bits the caller wants to pretend
	 * the device has set. Set them in the real register, then call
	 * the filter directly.
	 */
	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	/* Invoke the filter directly. */
	(void)myfirst_intr_filter(sc);

	return (0);
}
```

E a declaração do sysctl em `myfirst_intr_add_sysctls`:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_simulate",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, myfirst_intr_simulate_sysctl, "IU",
    "Simulate an interrupt by setting INTR_STATUS bits and "
    "invoking the filter");
```

Escrever em `dev.myfirst.0.intr_simulate` faz o handler executar com os bits de `INTR_STATUS` especificados.

### Exercitando a Simulação

Com o sysctl em vigor, o leitor pode conduzir o pipeline completo a partir do espaço do usuário:

```sh
# Simulate a DATA_AV event.
sudo sysctl dev.myfirst.0.intr_simulate=1

# Check counters.
sysctl dev.myfirst.0.intr_count
sysctl dev.myfirst.0.intr_data_av_count
sysctl dev.myfirst.0.intr_task_invocations

# Simulate an ERROR event.
sudo sysctl dev.myfirst.0.intr_simulate=2

# Simulate a COMPLETE event.
sudo sysctl dev.myfirst.0.intr_simulate=4

# Simulate all three at once.
sudo sysctl dev.myfirst.0.intr_simulate=7
```

A primeira chamada incrementa `intr_count` (filter disparado), `intr_data_av_count` (bit `DATA_AV` reconhecido) e eventualmente `intr_task_invocations` (a task do taskqueue executou). A segunda incrementa `intr_count` e `intr_error_count`. A terceira incrementa `intr_count` e `intr_complete_count`. A quarta acerta os três.

O leitor pode verificar o pipeline completo:

```sh
# Watch counters in a loop.
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
    sleep 0.5
    sysctl dev.myfirst.0 | grep intr_
done
```

Os contadores avançam na taxa esperada. O driver se comporta como se interrupções reais estivessem chegando.

### Por Que Isso Não É um Brinquedo

Alguém pode pensar que esse caminho simulado é apenas um artifício didático. Não é. Muitos drivers reais mantêm um caminho similar para fins de diagnóstico. Os motivos valem ser nomeados:

**Testes de regressão.** Um caminho de interrupção simulada permite que um pipeline de CI exercite o handler sem precisar de hardware real. O Capítulo 17 fez o mesmo argumento para simular o comportamento do dispositivo; a Seção 5 faz o mesmo para simular o caminho de interrupção.

**Injeção de falhas.** Um sysctl de interrupção simulada permite que um teste injete padrões específicos de `INTR_STATUS` para exercitar o código de tratamento de erros. A resposta do driver a `INTR_STATUS = ERROR | COMPLETE` (ambos os bits definidos simultaneamente) é difícil de disparar com hardware real; um sysctl que define ambos os bits e chama o handler torna isso simples.

**Produtividade do desenvolvedor.** Quando um autor de driver está depurando a lógica do handler, ter um sysctl que dispara o handler sob demanda é imensamente útil. `dtrace -n 'fbt::myfirst_intr_filter:entry'` combinado com `sudo sysctl dev.myfirst.0.intr_simulate=1` oferece uma visão passo a passo do handler quando necessário.

**Inicialização com novo hardware.** O autor de um driver frequentemente tem um dispositivo protótipo que ainda não produz interrupções corretamente. Um caminho de interrupção simulada permite que as camadas superiores do driver sejam testadas antes de o hardware funcionar, o que significa que o driver e o hardware podem ser desenvolvidos em paralelo em vez de serialmente.

**Ensino.** Para os fins deste livro, o caminho simulado torna o filter e a task observáveis em um alvo de laboratório que naturalmente não produz as interrupções esperadas. O leitor pode ver o pipeline funcionar mesmo que o hardware não coopere.

### Locking no Caminho Simulado

Um detalhe que vale a pena examinar com cuidado. O sysctl escreve em `INTR_STATUS` enquanto mantém `sc->mtx`. O handler de filtro, quando invocado pelo caminho real de interrupção do kernel, executa sem `sc->mtx` mantido (o filtro usa `bus_read_4` / `bus_write_4` diretamente, sem os macros CSR que exigem o lock). Quando invocado pelo sysctl, qual é o contexto de chamada?

O handler do sysctl executa em contexto de thread. O `MYFIRST_LOCK(sc)` adquire o sleep mutex. Entre a aquisição do lock e a sua liberação, a thread mantém o mutex. Em seguida, o lock é liberado e `myfirst_intr_filter(sc)` é chamado. O filtro não adquire nenhum lock, usa apenas atômicos e `bus_read_4`/`bus_write_4`, enfileira uma tarefa e retorna. Toda a sequência é segura.

Seria seguro chamar o filtro com `sc->mtx` mantido? Sim, na verdade: o filtro não tenta adquirir o mesmo mutex, e o filtro executa em um contexto onde manter o lock não é em si ilegal (contexto de thread). Mas o filtro foi projetado para ser agnóstico em relação ao contexto; chamá-lo com um sleep lock mantido obscureceria esse contrato. O sysctl libera o lock antes de invocar o filtro justamente por clareza.

### Usando a Simulação do Capítulo 17 para Gerar Interrupções

Uma técnica complementar que vale mencionar. O backend de simulação do Capítulo 17, quando acoplado, produz mudanças de estado autônomas conforme seu próprio cronograma. Em particular, seu callout gerador de `DATA_AV` define `INTR_STATUS.DATA_AV`. Em um build apenas de simulação (`MYFIRST_SIMULATION_ONLY` definido em tempo de compilação), a simulação está ativa, o callout é disparado e o driver do Capítulo 17 pode até mesmo invocar o filtro diretamente do próprio callout.

O Capítulo 19 não altera o comportamento do Capítulo 17 em builds apenas de simulação. Um leitor que queira ver o filtro sendo acionado pela simulação do Capítulo 17 pode fazer o build com `-DMYFIRST_SIMULATION_ONLY`, carregar o módulo e observar os callouts definindo os bits de `INTR_STATUS`. O caminho acionado por sysctl da Seção 5 permanece disponível em ambos os builds.

No build PCI, a simulação do Capítulo 17 não está acoplada (conforme a disciplina do Capítulo 18), portanto os callouts do Capítulo 17 não são executados. O caminho de interrupção simulada é a única forma de acionar o filtro no build PCI.

### Estendendo o sysctl para Agendar em uma Taxa

Uma extensão útil para testes de carga: um sysctl que agenda interrupções simuladas periodicamente por meio de um callout. O callout é disparado a cada N milissegundos, define um bit em `INTR_STATUS` e invoca o filtro. Um leitor pode ajustar a taxa e observar o pipeline sob carga.

```c
static void
myfirst_intr_sim_callout_fn(void *arg)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);
	if (sc->intr_sim_period_ms > 0 && sc->hw != NULL) {
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		MYFIRST_UNLOCK(sc);
		(void)myfirst_intr_filter(sc);
		MYFIRST_LOCK(sc);
		callout_reset_sbt(&sc->intr_sim_callout,
		    SBT_1MS * sc->intr_sim_period_ms, 0,
		    myfirst_intr_sim_callout_fn, sc, 0);
	}
	MYFIRST_UNLOCK(sc);
}
```

O callout se reagenda enquanto `intr_sim_period_ms` for diferente de zero. Um sysctl expõe o período:

```sh
# Fire a simulated interrupt every 100 ms.
sudo sysctl hw.myfirst.intr_sim_period_ms=100

# Stop simulating.
sudo sysctl hw.myfirst.intr_sim_period_ms=0
```

Observe os contadores crescerem na taxa esperada:

```sh
sleep 10
sysctl dev.myfirst.0.intr_count
```

Após dez segundos com período de 100 ms, o contador deve marcar cerca de 100. Se marcar muito menos, o filtro ou a task é o gargalo (improvável nessa escala; mais preocupante em testes de alta taxa). Se marcar muito mais, algo está acionando o filtro de outro lugar.

### O que a Simulação Não Captura

Limites honestos da técnica.

**Disparos concorrentes.** O sysctl serializa interrupções simuladas a uma por escrita. Um caminho de interrupção real pode ver dois disparos consecutivos em CPUs diferentes, o que um teste via sysctl não reproduz. Para testar concorrência sob estresse, um teste separado que cria múltiplas threads, cada uma escrevendo no sysctl, é mais eficaz.

**Comportamento do controlador de interrupção.** A simulação contorna completamente o controlador de interrupção. Testes que dependem de temporização de EOI, mascaramento ou detecção de tempestades de interrupção não podem ser conduzidos dessa forma.

**Afinidade de CPU.** O filtro simulado é executado na CPU em que a thread que escreve no sysctl está rodando. Uma interrupção real é disparada na CPU selecionada pela configuração de afinidade. Testes de comportamento por CPU precisam de interrupções reais ou de outro mecanismo.

**Contenção com o caminho de interrupção real.** Se interrupções reais também estiverem sendo disparadas (talvez porque o dispositivo de fato gere algumas), o caminho simulado pode disputar com o caminho real em uma condição de corrida. Os contadores atômicos tratam isso corretamente; estados compartilhados mais complexos podem não tratar.

Esses são limites, não impedimentos. Para a maioria dos testes do Capítulo 19, o caminho simulado é suficiente. Para testes de estresse avançados, técnicas adicionais (rt-threads, invocações em múltiplas CPUs, hardware real) se aplicam.

### Observando a Task em Execução

Um diagnóstico que vale expor. O contador `intr_task_invocations` da task incrementa a cada execução. Um leitor pode compará-lo com `intr_data_av_count` para verificar se o taskqueue está acompanhando o ritmo:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=1    # fire DATA_AV
sleep 0.1
sysctl dev.myfirst.0.intr_data_av_count       # should be 1
sysctl dev.myfirst.0.intr_task_invocations    # should also be 1
```

Se o contador da task estiver atrasado em relação ao contador DATA_AV, o worker do taskqueue está sobrecarregado. Nessa escala isso não deve ocorrer; em taxas mais altas (milhares por segundo), pode acontecer.

Uma sonda mais sensível: adicione um caminho `cv_signal` em que um programa do espaço do usuário aguarde. O sysctl dispara a interrupção simulada; o filtro enfileira a task; a task atualiza `sc->intr_last_data` e faz broadcast; a thread do espaço do usuário que estava aguardando na variável de condição (via `read` do cdev) acorda. A latência de ida e volta desde a escrita no sysctl até o acordar da thread é, aproximadamente, a latência de interrupção até o espaço do usuário do driver, um número útil de se conhecer.

### Integração com o Framework de Falhas do Capítulo 17

Uma observação que vale registrar. O framework de injeção de falhas do Capítulo 17 (os registradores `FAULT_MASK` e `FAULT_PROB`) se aplica a comandos, não a interrupções. O Capítulo 19 pode estender o framework adicionando uma opção de "falha na próxima interrupção": um sysctl que faz a próxima chamada ao filtro ignorar o acknowledgment, causando uma tempestade em uma linha disparada por nível.

Esta é uma extensão opcional. Os exercícios desafio a mencionam; o corpo principal do capítulo não a exige.

### Encerrando a Seção 5

Simular interrupções é uma técnica simples, mas eficaz. Um sysctl escreve em `INTR_STATUS`, invoca o filtro diretamente e o filtro aciona o pipeline completo: atualização de contadores, acknowledgment, enfileiramento da task e execução da task. A técnica permite exercitar um driver de ponta a ponta em um alvo de laboratório que não produz naturalmente as interrupções esperadas, e é barato mantê-la em drivers de produção para testes de regressão e acesso diagnóstico.

A Seção 6 é a última peça conceitual do tratamento central de interrupções. Ela aborda interrupções compartilhadas: o que acontece quando múltiplos drivers escutam na mesma linha de IRQ, como um handler de filtro deve identificar se a interrupção pertence ao seu dispositivo e o que `FILTER_STRAY` significa na prática.



## Seção 6: Tratando Interrupções Compartilhadas

O filtro do Estágio 2 da Seção 4 já tem a forma correta de valor de retorno para IRQs compartilhadas: ele retorna `FILTER_STRAY` quando `INTR_STATUS` é zero. A Seção 6 explora por que essa verificação é toda a disciplina necessária, o que dá errado quando um handler erra nisso e quando vale a pena definir o flag `RF_SHAREABLE`.

### Por que Compartilhar uma IRQ?

Dois motivos.

Primeiro, **restrições de hardware**. A arquitetura clássica de PC tinha 16 linhas de IRQ de hardware; o I/O APIC expandiu isso para 24 em muitos chipsets. Um sistema com 30 dispositivos necessariamente tem alguns deles compartilhando linhas. Em sistemas x86 modernos a escassez é menos aguda (centenas de vetores), mas em PCI legado e em muitos SoCs arm64 o compartilhamento é normal.

Segundo, **portabilidade do driver**. Um driver que trata corretamente interrupções compartilhadas também trata corretamente interrupções exclusivas (o caminho compartilhado é um superconjunto). Um driver que pressupõe interrupções exclusivas quebra quando o hardware muda ou quando outro driver chega na mesma linha. Escrever para o caso compartilhado não tem custo prático e torna o driver mais robusto para o futuro.

Em PCIe com MSI ou MSI-X habilitado (Capítulo 20), cada dispositivo tem seus próprios vetores e o compartilhamento raramente é necessário. Mas mesmo assim, um driver que trata corretamente uma interrupção espúria (retornando `FILTER_STRAY`) é um driver melhor do que um que não trata. A disciplina se transfere.

### O Fluxo em uma IRQ Compartilhada

Quando uma IRQ compartilhada é disparada, o kernel percorre a lista de handlers de filtro acoplados ao `intr_event` na ordem de registro. Cada filtro é executado, verifica se a interrupção pertence ao seu dispositivo e retorna de acordo:

- Se o filtro reivindica a interrupção (retornando `FILTER_HANDLED` ou `FILTER_SCHEDULE_THREAD`), o kernel continua com o próximo filtro, se houver, e agrega os resultados. Em kernels modernos, um filtro que retorna `FILTER_HANDLED` não impede que filtros posteriores sejam executados; o kernel sempre percorre a lista inteira.
- Se o filtro retorna `FILTER_STRAY`, o kernel tenta o próximo filtro.

Após todos os filtros terem sido executados, se algum filtro reivindicou a interrupção, o kernel faz o acknowledge no controlador de interrupção e retorna. Se todos os filtros retornaram `FILTER_STRAY`, o kernel incrementa um contador de interrupções espúrias; se o contador ultrapassar um limiar, o kernel desabilita a IRQ (o último recurso drástico).

Um filtro que retorna `FILTER_STRAY` quando a interrupção era de fato para o seu dispositivo é um bug: a linha permanece asserted (disparada por nível), o mecanismo de tempestade entra em ação e o dispositivo não é atendido. Um filtro que retorna `FILTER_HANDLED` quando a interrupção não era para o seu dispositivo também é um bug: a interrupção de outro driver é marcada como atendida, seu handler nunca é executado, seus dados ficam parados no FIFO e a rede ou o disco do usuário para de funcionar.

A disciplina é decidir a propriedade com precisão, com base no estado do dispositivo, e retornar o valor correto.

### O Teste de INTR_STATUS

A forma padrão de decidir a propriedade é ler um registrador do dispositivo que indique se há uma interrupção pendente. Em um dispositivo com um registrador INTR_STATUS por dispositivo, a pergunta é: "algum bit em INTR_STATUS está definido?" Se sim, a interrupção é minha. Se não, não é.

O layout de registradores do Capítulo 17 torna isso simples:

```c
status = bus_read_4(sc->bar_res, MYFIRST_REG_INTR_STATUS);
if (status == 0)
	return (FILTER_STRAY);
```

É exatamente isso que o filtro do Estágio 2 já faz. O padrão é robusto: se o registrador de status é lido como zero, não há evento pendente deste dispositivo, portanto a interrupção não é nossa.

Um detalhe sutil: a leitura de `INTR_STATUS` deve acontecer antes de qualquer mudança de estado que possa mascarar ou redefinir os bits. Ler `INTR_STATUS` com o dispositivo em um estado intermediário é seguro (o registrador reflete a visão atual do dispositivo); escrever em outros registradores primeiro e depois ler `INTR_STATUS` pode perder bits que as escritas limparam inadvertidamente.

### Como "É Meu?" se Apresenta no Hardware Real

O teste de INTR_STATUS é de manual porque o layout de registradores do Capítulo 17 é de manual. Dispositivos reais vêm em variações.

**Dispositivos com INTR_STATUS limpo.** A maioria dos dispositivos modernos tem um registrador que, quando lido como zero, diz definitivamente "não é meu". A forma do filtro do driver do Capítulo 19 se aplica diretamente.

**Dispositivos com bits que ficam sempre definidos.** Alguns dispositivos têm bits de interrupção pendente que permanecem definidos entre interrupções (aguardando que o driver os redefina). O filtro deve mascarar esses bits ou verificar contra uma máscara por classe de interrupção. O layout de registradores do Capítulo 17 evita essa complicação; drivers reais ocasionalmente a enfrentam.

**Dispositivos sem INTR_STATUS.** Alguns dispositivos mais antigos exigem que o driver leia uma sequência separada de registradores (ou infira a partir de registradores de estado) se há uma interrupção pendente. Esses drivers são mais complexos; o filtro pode precisar adquirir um spinlock e ler vários registradores. O código-fonte do FreeBSD tem exemplos em alguns drivers embarcados.

**Dispositivos com INTR_STATUS global e registradores por fonte.** Um padrão comum em NICs: um registrador de nível superior informa qual fila tem um evento pendente, e registradores por fila contêm os detalhes do evento. O filtro lê o registrador de nível superior para decidir a propriedade; a ithread ou a task lê os registradores por fila para processar os eventos.

O driver do Capítulo 19 usa a primeira variação. A disciplina para as outras variações é a mesma: ler um registrador, decidir.

### Retornando FILTER_STRAY Corretamente

A regra é simples: se o filtro não reconhecer nenhum bit como pertencente a uma classe que ele trata, retorne `FILTER_STRAY`.

```c
if (rv == 0)
	return (FILTER_STRAY);
```

A variável `rv` acumula `FILTER_HANDLED` de cada bit reconhecido. Se nenhum bit for reconhecido, `rv` é zero e o filtro não tem o que retornar senão `FILTER_STRAY`.

Um corolário sutil: um filtro que reconhece alguns bits mas não outros retorna `FILTER_HANDLED` para os bits que reconheceu e não retorna `FILTER_STRAY` para os que não reconheceu. Definir um bit em `INTR_MASK` que o driver não vai tratar é um bug do driver; o kernel não pode ajudar.

Um caso de borda interessante: um bit está definido em `INTR_STATUS` mas o driver não o reconhece (talvez uma nova revisão do dispositivo tenha adicionado um bit que o código do driver antecede). O driver tem duas opções:

1. Ignorar o bit. Não fazer o acknowledge. Deixá-lo definido. Em uma linha disparada por nível isso produz uma tempestade porque o bit mantém a linha asserted para sempre. Ruim.

2. Fazer o acknowledge do bit sem realizar nenhum trabalho. Escrevê-lo de volta em `INTR_STATUS`. O dispositivo para de assertar para aquele bit, sem tempestade, mas o evento é perdido. Em um evento essencial isso é um bug funcional; em um evento de diagnóstico pode ser aceitável.

O padrão recomendado é a opção 2 com uma mensagem de log: reconheça os bits desconhecidos, registre-os com taxa reduzida (para evitar inundação do log caso o bit seja ativado continuamente) e siga em frente. Isso torna o driver robusto contra novas revisões de hardware ao custo de potencialmente perder informações sobre eventos desconhecidos.

```c
uint32_t unknown = status & ~(MYFIRST_INTR_DATA_AV |
    MYFIRST_INTR_ERROR | MYFIRST_INTR_COMPLETE);
if (unknown != 0) {
	atomic_add_64(&sc->intr_unknown_count, 1);
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, unknown);
	rv |= FILTER_HANDLED;
}
```

Este trecho não faz parte do filtro do Estágio 2; trata-se de uma extensão útil para o Estágio 3 ou além.

### O Que Acontece Quando Múltiplos Drivers Compartilham uma IRQ

Um cenário concreto. Suponha que o dispositivo virtio-rnd em um guest bhyve compartilhe a IRQ 19 com o controlador AHCI. Ambos os drivers registraram handlers. Uma interrupção chega na IRQ 19.

O kernel percorre a lista de handlers na ordem de registro. Suponha que o AHCI registrou primeiro, então seu filter executa primeiro:

1. Filter do AHCI: lê seu INTR_STATUS, vê bits definidos (o AHCI tem I/O pendente), reconhece, retorna `FILTER_HANDLED`.
2. Filter do `myfirst`: lê seu INTR_STATUS, lê zero, retorna `FILTER_STRAY`.

O kernel vê "ao menos um FILTER_HANDLED" e não marca a interrupção como stray.

Agora o caso inverso. O dispositivo virtio-rnd tem um evento:

1. Filter do AHCI: lê seu INTR_STATUS, vê zero, retorna `FILTER_STRAY`.
2. Filter do `myfirst`: lê seu INTR_STATUS, vê `DATA_AV`, reconhece, retorna `FILTER_HANDLED`.

O kernel vê um `FILTER_HANDLED` e fica satisfeito.

A propriedade fundamental é que cada filter verifica apenas o seu próprio dispositivo. Nenhum filter assume que a interrupção é sua; cada um decide com base no estado do seu dispositivo.

### O Que Acontece Quando um Driver Erra

Um filter AHCI com defeito que retorna `FILTER_HANDLED` sempre que dispara (sem verificar o status) tomaria posse da interrupção do `myfirst`. O filter do `myfirst` nunca executaria, `DATA_AV` nunca seria reconhecido e a linha entraria em storm.

A correção não está no lado do `myfirst`; está no lado do AHCI. Na prática, todos os principais drivers do FreeBSD fazem essa verificação corretamente porque o código foi auditado e testado por anos. A lição é que o protocolo de IRQ compartilhada exige cooperação: cada driver na linha deve verificar seu próprio estado corretamente.

A proteção contra um único driver defeituoso é o `hw.intr_storm_threshold`. Quando o kernel detecta uma sequência de interrupções todas marcadas como stray (ou todas retornando `FILTER_HANDLED` sem que nenhum dispositivo tenha trabalho real), ele eventualmente mascara a linha. O mecanismo é de detecção, não de prevenção.

### Coexistindo com Drivers que Não Compartilham

Um driver que aloca sua IRQ com `RF_SHAREABLE` pode coexistir com drivers que alocam sem compartilhamento, desde que o kernel consiga satisfazer ambas as requisições. Se o nosso driver `myfirst` alocar primeiro com `RF_SHAREABLE` e o AHCI tentar alocar exclusivamente depois, a alocação do AHCI falhará (a linha já está em uso por um driver que pode não ser exclusivo). Se o AHCI alocar primeiro sem compartilhamento, nossa alocação do `myfirst` (com `RF_SHAREABLE`) falhará.

Na prática, drivers modernos quase sempre usam `RF_SHAREABLE`. Drivers legados ocasionalmente omitem essa flag; se o driver de um leitor não puder ser carregado por causa de um conflito de alocação de interrupção, a correção costuma ser adicionar `RF_SHAREABLE` à alocação.

A alocação exclusiva é apropriada para:

- Drivers com requisitos rígidos de latência que não toleram outros handlers na linha.
- Drivers que usam `INTR_EXCL` por uma razão específica do kernel.
- Alguns drivers legados escritos antes de o suporte a IRQ compartilhada ter amadurecido.

Para o driver do Capítulo 19, `RF_SHAREABLE` é o padrão e nunca está errado.

### A Topologia de IRQ Virtio no bhyve

Um detalhe prático sobre o ambiente de laboratório do Capítulo 19. O emulador bhyve mapeia cada dispositivo PCI emulado para uma linha de IRQ com base no pino INTx do slot. Múltiplos dispositivos nas diferentes funções de um mesmo slot compartilham uma linha; slots diferentes geralmente têm linhas diferentes. O dispositivo virtio-rnd no slot 4 função 0 tem seu próprio pino.

Na prática, em um guest bhyve com apenas alguns dispositivos emulados, cada dispositivo geralmente tem sua própria linha de IRQ (sem compartilhamento). O driver `myfirst` com `RF_SHAREABLE` alocado em uma linha não compartilhada se comporta de forma idêntica a uma alocação não compartilhável; a flag não causa nenhum problema.

Para testar deliberadamente o comportamento de IRQ compartilhada no bhyve, o leitor pode empilhar múltiplos dispositivos virtio no mesmo slot (funções diferentes), forçando-os a compartilhar uma linha. Isso é avançado e não é necessário para os laboratórios básicos do Capítulo 19.

### O Problema de Starvation

Uma linha de IRQ compartilhada tem um potencial problema de starvation (inanição): um único driver que demora demais em seu filter pode atrasar todos os outros drivers na linha. Cada filter vê o estado do seu dispositivo como "inalterado" durante a execução do filter lento, e eventos podem se acumular sem serem detectados.

A disciplina é a mesma que a Seção 4 abordou: filters devem ser rápidos. Dezenas ou centenas de microssegundos de trabalho real é geralmente o máximo que um filter bem-comportado realiza; qualquer coisa mais lenta é delegada à task. Um filter que realiza trabalho demorado prejudica não apenas as camadas superiores do seu próprio driver, mas também todos os outros drivers na linha.

Com MSI-X (Capítulo 20), cada vetor tem seu próprio `intr_event`, então a preocupação com starvation desaparece para os pares de drivers específicos que usam MSI-X. Mas a disciplina ainda se aplica: um filter que leva um milissegundo está prejudicando a latência de cada interrupção subsequente.

### Falsos Positivos e Tratamento Defensivo

Uma propriedade útil da verificação do registrador de status é que ela tolera naturalmente falsos positivos do lado do kernel. Ocasionalmente, os controladores de interrupção reportam uma interrupção espúria quando nenhum dispositivo está de fato sinalizando (ruído na linha, uma condição de corrida entre o disparo por borda e o mascaramento, uma peculiaridade específica da plataforma). O kernel despacha, o filter lê o INTR_STATUS, que está em zero, o filter retorna `FILTER_STRAY` e o kernel segue em frente.

Isso é um no-op para o driver. O contador de interrupções stray aumenta; mais nada muda.

Alguns drivers adicionam uma mensagem de log com limitação de taxa para tornar as interrupções espúrias visíveis. Um padrão razoável é registrar apenas se a taxa exceder um limite:

```c
static struct timeval last_stray_log;
static int stray_rate_limit = 5;  /* messages per second */
if (rv == 0) {
	if (ppsratecheck(&last_stray_log, &stray_rate_limit, 1))
		device_printf(sc->dev, "spurious interrupt\n");
	return (FILTER_STRAY);
}
```

O utilitário `ppsratecheck(9)` limita a taxa de mensagens. Sem ele, uma linha em storm inundaria o `dmesg` com mensagens idênticas.

O driver do Capítulo 19 não inclui o log com limitação de taxa no seu filter do Estágio 2; ele é adicionado em um exercício desafio.

### Quando o Filter Deve Tratar e a Task Não Deve Executar

Um experimento mental. Imagine que o filter reconhece `ERROR` mas não `DATA_AV`. O filter trata `ERROR` (reconhece, incrementa o contador) e retorna `FILTER_HANDLED`. Nenhuma task é enfileirada. O dispositivo está satisfeito; a linha é desativada.

Mas `INTR_STATUS.DATA_AV` pode ainda estar definido, porque o filter não o reconheceu (o filter não identificou o bit como pertencente a uma classe que o driver trata). Em uma linha acionada por nível, o dispositivo continua sinalizando para `DATA_AV`, uma nova interrupção dispara e o ciclo se repete.

Isso é uma versão do problema de "storm de bit desconhecido". A correção é reconhecer cada bit que o driver está disposto a ver, mesmo que o driver não faça nada com alguns deles. Definir `INTR_MASK` apenas com os bits que o driver trata é a medida preventiva; reconhecer bits não identificados no filter é a medida defensiva.

### Encerrando a Seção 6

Interrupções compartilhadas são o caso comum no PCI legado e ainda a suposição correta para escrever em hardware moderno. Um filter em uma linha compartilhada deve verificar se a interrupção pertence ao seu dispositivo (geralmente lendo o registrador `INTR_STATUS` do dispositivo), tratar os bits que reconhece, reconhecer esses bits e retornar `FILTER_STRAY` se não reconheceu nada. A disciplina é pequena em código e grande em confiabilidade: um driver que a aplica corretamente coexiste com todos os outros drivers bem-comportados na sua linha, e `RF_SHAREABLE` na alocação é a única linha de código adicional de que ele precisa.

A Seção 7 é a seção de teardown. Ela é curta: `bus_teardown_intr` antes de `bus_release_resource`, drenar o taskqueue antes de liberar qualquer coisa que a task acesse, limpar `INTR_MASK` para que o dispositivo pare de sinalizar e verificar se os contadores fazem sentido. Mas a ordem é estrita, e o caminho de detach do Capítulo 19 se estende por exatamente esses passos.

---

## Seção 7: Limpando Recursos de Interrupção

O caminho de attach ganhou três novas operações (alocar IRQ, registrar handler, habilitar interrupções no dispositivo); o caminho de detach deve desfazer cada uma delas em ordem estritamente reversa. A Seção 7 é curta porque o padrão agora é familiar; mas a ordem importa de formas específicas que as seções anteriores não abordaram, e um erro aqui produz kernel panics que o kernel de debug é muito eficaz em capturar e igualmente eficaz em tornar confusos de diagnosticar.

### A Ordem Necessária

Do mais específico ao mais geral, a sequência de detach no Estágio 2 do Capítulo 19 é:

1. **Recusar se ocupado.** `myfirst_is_busy(sc)` retorna verdadeiro se o cdev estiver aberto ou se houver um comando em andamento.
2. **Marcar como não mais anexado** para que os caminhos do espaço do usuário recusem iniciar.
3. **Destruir o cdev** para que nenhum novo acesso do espaço do usuário comece.
4. **Desabilitar interrupções no dispositivo.** Limpar `INTR_MASK` para que o dispositivo pare de sinalizar.
5. **Desmontar o handler de interrupção.** `bus_teardown_intr(9)` em `irq_res` com o cookie salvo. Após o retorno, o kernel garante que o filter não executará novamente.
6. **Drenar o taskqueue.** `taskqueue_drain(9)` aguarda qualquer task pendente ser concluída e impede que novas iniciem.
7. **Destruir o taskqueue.** `taskqueue_free(9)` encerra as threads de trabalho.
8. **Silenciar os callouts de simulação do Capítulo 17** se `sc->sim` for não-NULL.
9. **Fazer detach da simulação do Capítulo 17** se estiver anexada.
10. **Fazer detach da camada de hardware** para que `sc->hw` seja liberado.
11. **Liberar o recurso de IRQ** com `bus_release_resource(9)`.
12. **Liberar o BAR** com `bus_release_resource(9)`.
13. **Desinicializar o softc.**

Treze passos. Cada um faz uma coisa. Os riscos estão na ordenação.

### Por Que Desabilitar no Dispositivo Antes de bus_teardown_intr

Limpar `INTR_MASK` antes de desmontar o handler é uma medida defensiva. Se desmontássemos o handler primeiro, uma interrupção pendente no dispositivo poderia disparar sem ter handler; o kernel a marcaria como stray e eventualmente desabilitaria a linha. Limpar `INTR_MASK` primeiro impede que o dispositivo sinalize, então o teardown remove o handler e nenhuma interrupção pode disparar no intervalo.

Para MSI-X (Capítulo 20), a lógica é ligeiramente diferente porque cada vetor é independente. Mas o princípio se mantém: pare a fonte antes de remover o handler.

Em hardware real, essa janela é de microssegundos; uma interrupção stray durante ela é rara. No bhyve, onde a taxa de eventos é baixa, ela essencialmente nunca ocorre. Mas drivers cuidadosos fecham essa janela de qualquer forma, porque drivers cuidadosos são os que você quer ver em produção.

### Por Que bus_teardown_intr Antes de Liberar o Recurso

`bus_teardown_intr` remove o handler do driver do `intr_event`. Após o retorno, o kernel garante que o filter não executará novamente. Mas o recurso de IRQ (o `struct resource *`) ainda é válido; o kernel ainda não o liberou. `bus_release_resource` é o que o libera.

Se liberássemos o recurso primeiro, a contabilidade interna do kernel em torno do `intr_event` veria um handler registrado em um recurso que não existe mais. Dependendo do timing, isso produz ou uma falha imediata durante `bus_release_resource` (o kernel detecta que o handler ainda está registrado) ou um problema adiado quando a linha tenta disparar.

A ordem segura é sempre `bus_teardown_intr` primeiro. A página de manual do `bus_setup_intr(9)` deixa isso explícito.

### Por Que Drenar o Taskqueue Antes de Liberar o Softc

O filter pode ter enfileirado uma task que ainda não executou. O ponteiro de função da task é armazenado na `struct task`, e o ponteiro de argumento é o softc. Se liberássemos o softc antes de a task executar, a task desreferenciaria um ponteiro liberado e causaria um panic.

`taskqueue_drain(9)` em uma task específica aguarda essa task ser concluída e impede que enfileiramentos futuros dessa task sejam executados. Chamar `taskqueue_drain` em `&sc->intr_data_task` é exatamente o correto: ele aguarda a task de dados disponíveis ser concluída.

Após o retorno de `taskqueue_drain`, nenhuma execução de task está em andamento. O softc pode ser liberado com segurança.

Um erro comum: drenar uma única task com `taskqueue_drain(tq, &task)` é diferente de drenar todo o taskqueue com `taskqueue_drain_all(tq)`. Para um driver com múltiplas tasks no mesmo taskqueue, cada task precisa do seu próprio drain, ou `taskqueue_drain_all` as trata como um grupo.

Para o driver do Capítulo 19, há apenas uma tarefa, portanto um único `taskqueue_drain` é suficiente.

### Por Que bus_teardown_intr Vem Antes de taskqueue_drain

O filtro pode ainda enfileirar uma tarefa entre o momento em que `INTR_MASK` foi limpo e o momento em que `bus_teardown_intr` retorna. Se déssemos drain no taskqueue antes de remover o handler, um filtro ainda em execução poderia enfileirar uma tarefa depois do drain, e a garantia do drain seria violada.

A ordem correta é: limpar `INTR_MASK` (impede novas interrupções), remover o handler (impede que o filtro seja executado novamente), dar drain no taskqueue (impede que qualquer tarefa previamente enfileirada seja executada). Cada passo restringe o conjunto de caminhos de código que podem tocar o estado.

### O Código de Cleanup

Colocando a ordem no detach da Stage 2 do Capítulo 19:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Destroy the cdev so no new user-space access starts. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Disable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the interrupt handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the interrupt taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Quiesce Chapter 17 callouts (if sim attached). */
	myfirst_quiesce(sc);

	/* Detach Chapter 17 simulation if attached. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer. */
	myfirst_hw_detach(sc);

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

Treze ações distintas, cada uma simples. O código é mais longo do que nos estágios anteriores apenas porque cada nova capacidade adiciona seu próprio passo de teardown.

### Tratando Falhas no Attach Parcial

O cascade de goto do caminho de attach na Seção 3 tinha labels para cada passo de alocação. Com o handler de interrupção registrado na Stage 2, o cascade ganha mais um:

```c
fail_teardown_intr:
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	sc->intr_cookie = NULL;
fail_release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	sc->irq_res = NULL;
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

Cada label do cascade desfaz o passo que teve sucesso antes dele. Um `bus_setup_intr` que falha salta para `fail_release_irq` (pula o teardown porque o handler não foi registrado). Um `make_dev` que falha (criação do cdev) salta para `fail_teardown_intr` (remove o handler antes de liberar o IRQ).

O taskqueue é inicializado em `myfirst_init_softc` e destruído em `myfirst_deinit_softc`, portanto o cascade não precisa tratá-lo explicitamente; qualquer label que alcance `fail_softc` tem o taskqueue limpo via deinit.

### Verificando o Teardown

Após `kldunload myfirst`, o kernel deve estar em um estado limpo. Verificações específicas:

- `kldstat -v | grep myfirst` não retorna nada (módulo descarregado).
- `devinfo -v | grep myfirst` não retorna nada (dispositivo desanexado).
- `vmstat -i | grep myfirst` não retorna nada (evento de interrupção limpo).
- `vmstat -m | grep myfirst` não retorna nada ou mostra zero `InUse` (tipo malloc drenado).
- `dmesg | tail` mostra o banner de detach e nenhum aviso ou panic.

Falhar em qualquer uma dessas verificações é um bug. A falha mais comum é `vmstat -i` mostrar uma entrada obsoleta; isso normalmente significa que `bus_teardown_intr` não foi chamado. A segunda mais comum é `vmstat -m` mostrando alocações vivas; isso normalmente significa que uma tarefa foi enfileirada e não drenada, ou que a simulação foi anexada e não desanexada.

### Tratando o Caso de "Handler Disparado Durante o Detach"

Um caso sutil que vale a pena considerar. Suponha que uma interrupção real dispare em uma linha de IRQ compartilhada entre a destruição do cdev e a escrita em `INTR_MASK`. O dispositivo de outro driver está afirmando a linha, nosso filtro é executado (porque a linha é compartilhada), nosso filtro lê `INTR_STATUS` (que é zero no nosso dispositivo) e retorna `FILTER_STRAY`. Nenhum estado é tocado, nenhuma tarefa é enfileirada.

Suponha agora que a interrupção veio do nosso dispositivo. Nosso `INTR_STATUS` tem um bit ativo. O filtro o reconhece, dá acknowledge, enfileira uma tarefa e retorna. O enfileiramento da tarefa acontece contra um taskqueue que ainda não foi drenado. A tarefa é executada posteriormente, adquire `sc->mtx`, lê `DATA_OUT` pela camada de hardware (que ainda está anexada porque ainda não chamamos `myfirst_hw_detach`). Tudo seguro.

Suponha que a interrupção chegue depois de `INTR_MASK = 0`, mas antes de `bus_teardown_intr`. O dispositivo parou de afirmar os bits que limpamos, mas uma interrupção já em voo (enfileirada no controlador de interrupções) ainda pode executar o filtro. O filtro lê `INTR_STATUS`, vê zero (porque a escrita na máscara foi antes do estado interno do dispositivo), retorna `FILTER_STRAY`. A interrupção é contada como stray; o kernel a ignora.

Suponha que a interrupção chegue após `bus_teardown_intr`. O handler foi removido. A contabilidade de interrupções stray do kernel registra o evento. Após strays suficientes, o kernel desabilita a linha. Esse é o cenário que o passo `INTR_MASK = 0` foi projetado para prevenir; se a máscara for limpa primeiro, nenhuma stray pode se acumular.

Os caminhos de código são todos defensivos. As asserções do kernel de debug capturam os erros mais comuns. Um driver que segue a ordem do Capítulo 19 realiza o teardown de forma limpa e confiável.

### O Que Acontece Quando o Teardown é Ignorado

Alguns cenários que produzem sintomas concretos.

**Handler não removido, recurso liberado.** `kldunload` chama `bus_release_resource` no IRQ sem `bus_teardown_intr`. O kernel detecta um handler ativo em um recurso sendo liberado e entra em panic com uma mensagem como "releasing allocated IRQ with active handler". O kernel de debug é confiável nesse caso.

**Handler removido, taskqueue não drenado.** A tarefa é enfileirada no filtro, a última chamada do filtro acontece logo antes do teardown, a tarefa ainda não foi executada. O driver libera `sc` (via deinit do softc) e é descarregado. A thread worker do taskqueue acorda, executa a função da tarefa, desreferencia o softc já liberado e entra em panic com uma falha de ponteiro nulo ou use-after-free. O `WITNESS` ou `MEMGUARD` do kernel de debug podem capturar isso; caso contrário, o crash ocorre no primeiro acesso de memória da função da tarefa.

**Taskqueue drenado, não liberado.** `taskqueue_drain` tem sucesso, mas `taskqueue_free` é ignorado. A thread worker do taskqueue continua em execução (ociosa). Um `vmstat -m` mostra a alocação. Não é um bug funcional, mas um leak que se acumula a cada ciclo de carga e descarga.

**Callouts da simulação não quiesced.** Se a simulação do Capítulo 17 está anexada (em um build somente de simulação), seus callouts estão em execução. Sem quiesce, eles disparam após o detach ter liberado o bloco de registradores, acessando memória inválida. A detecção pelo `WITNESS` ou `MEMGUARD` varia conforme o acerto; às vezes o sintoma é uma desreferência de ponteiro nulo simples.

**INTR_MASK não limpo.** Interrupções reais disparam após o início do detach. O filtro (brevemente, até o teardown) as trata; após o teardown, elas são strays que o kernel eventualmente usa para desabilitar a linha. O estado desabilitado da linha fica visível em `vmstat -i` (contagem crescente de strays) e em `dmesg` (avisos do kernel).

Cada um desses problemas pode ser corrigido ajustando a ordem do teardown. O código do Capítulo 19 está configurado corretamente; os perigos existem para o leitor que modificar essa ordem.

### Testando o Teardown de Forma Básica

Um teste simples que o leitor pode executar após escrever o código de detach:

```sh
# Load.
sudo kldload ./myfirst.ko

# Fire a few simulated interrupts, make sure tasks run.
for i in 1 2 3 4 5; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
done
sleep 1
sysctl dev.myfirst.0.intr_task_invocations  # should be 5

# Unload.
sudo kldunload myfirst

# Check nothing leaked.
vmstat -m | grep myfirst  # should be empty
devinfo -v | grep myfirst   # should be empty
vmstat -i | grep myfirst    # should be empty
```

Executar essa sequência em um loop (vinte iterações em um loop de shell) é um teste de regressão razoável: qualquer leak se acumula, qualquer crash se manifesta, qualquer padrão de falha se torna visível.

### Encerrando a Seção 7

Limpar recursos de interrupção resume-se a seis operações simples no caminho de detach: desabilitar `INTR_MASK`, remover o handler, dar drain e liberar o taskqueue, desanexar a camada de hardware, liberar o IRQ, liberar o BAR. Cada operação desfaz exatamente uma operação do caminho de attach. A ordem é o inverso do attach. O drain do taskqueue é uma preocupação nova importante, específica para drivers com filtro mais tarefa; um driver que o ignora tem um bug de use-after-free esperando pelo próximo ciclo de carga e descarga.

A Seção 8 é a seção de organização: dividir o código de interrupção em seu próprio arquivo, incrementar a versão para `1.2-intr`, escrever `INTERRUPTS.md` e executar o passe de regressão. O driver está funcionalmente completo após a Seção 7; a Seção 8 o torna manutenível.



## Seção 8: Refatorando e Versionando Seu Driver com Suporte a Interrupções

O handler de interrupção está funcionando. A Seção 8 é a seção de organização. Ela divide o código de interrupção em seu próprio arquivo, atualiza os metadados do módulo, adiciona um novo documento `INTERRUPTS.md`, introduz um pequeno conjunto de macros CSR para o contexto de interrupção para que o filtro possa acessar registradores sem as macros que exigem o lock, incrementa a versão para `1.2-intr` e executa o passe de regressão.

Um leitor que chegou até aqui pode estar tentado a pular esta seção. É a mesma tentação que a Seção 8 do Capítulo 18 alertou, e a mesma recusa: um driver cujo código de interrupção está misturado no arquivo PCI, cujo filtro usa `bus_read_4` diretamente de forma ad hoc, cuja configuração do taskqueue está espalhada por três arquivos, torna-se difícil de estender. O Capítulo 20 adiciona MSI e MSI-X; o Capítulo 21 adiciona DMA. Ambos constroem sobre o código de interrupção do Capítulo 19. Uma estrutura limpa agora economiza esforço ao longo de ambos.

### O Layout Final de Arquivos

Ao final do Capítulo 19, o driver consiste nos seguintes arquivos:

```text
myfirst.c         - Main driver: softc, cdev, module events, data path.
myfirst.h         - Shared declarations: softc, lock macros, prototypes.
myfirst_hw.c      - Ch16 hardware access layer: CSR_* accessors,
                     access log, sysctl handlers.
myfirst_hw_pci.c  - Ch18 hardware layer extension: myfirst_hw_attach_pci.
myfirst_hw.h      - Register map and accessor declarations.
myfirst_sim.c     - Ch17 simulation backend.
myfirst_sim.h     - Ch17 simulation interface.
myfirst_pci.c     - Ch18 PCI attach: probe, attach, detach,
                     DRIVER_MODULE, MODULE_DEPEND, ID table.
myfirst_pci.h     - Ch18 PCI declarations.
myfirst_intr.c    - Ch19 interrupt handler: filter, task, setup, teardown.
myfirst_intr.h    - Ch19 interrupt interface.
myfirst_sync.h    - Part 3 synchronisation primitives.
cbuf.c / cbuf.h   - Ch10 circular buffer.
Makefile          - kmod build.
HARDWARE.md       - Ch16/17 register map.
LOCKING.md        - Ch15 onward lock discipline.
SIMULATION.md     - Ch17 simulation.
PCI.md            - Ch18 PCI support.
INTERRUPTS.md     - Ch19 interrupt handling.
```

`myfirst_intr.c` e `myfirst_intr.h` são novos. `INTERRUPTS.md` é novo. Todos os outros arquivos ou já existiam antes ou foram estendidos levemente (o softc ganhou campos; o attach PCI delega para `myfirst_intr.c`).

A regra geral permanece: cada arquivo tem uma responsabilidade. `myfirst_intr.c` é responsável pelo handler de interrupção, pela tarefa adiada e pelo sysctl de interrupção simulada. `myfirst_pci.c` é responsável pelo attach PCI, mas delega a configuração e o teardown de interrupção para funções exportadas por `myfirst_intr.c`.

### O Makefile Final

```makefile
# Makefile for the Chapter 19 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.2-intr\"

# CFLAGS+= -DMYFIRST_SIMULATION_ONLY
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

Um arquivo-fonte adicional na lista SRCS; a string de versão incrementada; o restante sem alterações.

### A String de Versão

De `1.1-pci` para `1.2-intr`. O incremento reflete que o driver adquiriu uma nova capacidade significativa (tratamento de interrupções) sem alterar nenhuma interface visível ao usuário (o cdev ainda faz o que fazia). Um incremento de versão menor é apropriado.

Os capítulos seguintes continuam: `1.3-msi` após o trabalho de MSI e MSI-X do Capítulo 20; `1.4-dma` após os Capítulos 20 e 21 adicionarem DMA. Cada versão menor reflete uma adição significativa de capacidade.

### O Header myfirst_intr.h

O header exporta a interface pública da camada de interrupção para o restante do driver:

```c
#ifndef _MYFIRST_INTR_H_
#define _MYFIRST_INTR_H_

#include <sys/types.h>
#include <sys/taskqueue.h>

struct myfirst_softc;

/* Interrupt setup and teardown, called from the PCI attach path. */
int  myfirst_intr_setup(struct myfirst_softc *sc);
void myfirst_intr_teardown(struct myfirst_softc *sc);

/* Register sysctl nodes specific to the interrupt layer. */
void myfirst_intr_add_sysctls(struct myfirst_softc *sc);

/* Interrupt-context accessor macros. These do not acquire sc->mtx
 * and therefore are safe in the filter. They are NOT a replacement
 * for CSR_READ_4 / CSR_WRITE_4 in other contexts. */
#define ICSR_READ_4(sc, off) \
	bus_read_4((sc)->bar_res, (off))
#define ICSR_WRITE_4(sc, off, val) \
	bus_write_4((sc)->bar_res, (off), (val))

#endif /* _MYFIRST_INTR_H_ */
```

A API pública consiste em três funções (`myfirst_intr_setup`, `myfirst_intr_teardown`, `myfirst_intr_add_sysctls`) e dois macros de acesso (`ICSR_READ_4`, `ICSR_WRITE_4`). O prefixo "I" significa "interrupt-context"; esses macros não adquirem `sc->mtx`, portanto são seguros no filtro.

### O Arquivo myfirst_intr.c

O arquivo completo está na árvore de exemplos companion; aqui está a estrutura central:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"

/* Deferred task for data-available events. */
static void myfirst_intr_data_task_fn(void *arg, int npending);

/* The filter handler. Exported so the simulated-interrupt sysctl can
 * call it directly. */
int myfirst_intr_filter(void *arg);

int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie, "legacy");

	/* Enable the interrupts we care about at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}

void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable interrupts at the device. */
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
}

int
myfirst_intr_filter(void *arg)
{
	/* ... as in Section 4 ... */
}

static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	/* ... as in Section 4 ... */
}

void
myfirst_intr_add_sysctls(struct myfirst_softc *sc)
{
	/* ... counters and intr_simulate sysctl ... */
}
```

O arquivo tem cerca de 250 linhas na Stage 4. `myfirst_pci.c` encolhe correspondentemente: a alocação e a configuração da interrupção são movidas para fora.

### O Attach PCI Refatorado

Após mover o código de interrupção para `myfirst_intr.c`, `myfirst_pci_attach` passa a ser:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR 0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: attach the hardware layer. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release_bar;

	/* Step 3: set up interrupts. */
	error = myfirst_intr_setup(sc);
	if (error != 0) {
		device_printf(dev, "interrupt setup failed (%d)\n", error);
		goto fail_hw;
	}

	/* Step 4: create cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_intr;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 5: register sysctls. */
	myfirst_intr_add_sysctls(sc);

	sc->pci_attached = true;
	return (0);

fail_intr:
	myfirst_intr_teardown(sc);
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

O attach PCI fica mais curto; os detalhes de interrupção ficam ocultos por trás de `myfirst_intr_setup`. O cascade de goto tem quatro labels em vez de seis (os labels específicos de interrupção foram movidos para `myfirst_intr.c`).

### O Detach Refatorado

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	myfirst_intr_teardown(sc);

	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	myfirst_hw_detach(sc);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

O teardown específico de interrupção resume-se a uma chamada para `myfirst_intr_teardown`, que encapsula os passos de limpeza da máscara, remoção do handler, drain e liberação do recurso.

### O Documento INTERRUPTS.md

O novo documento fica ao lado do código-fonte do driver. Seu papel é descrever o tratamento de interrupções do driver para um leitor futuro sem que ele precise ler `myfirst_intr.c`:

```markdown
# Interrupt Handling in the myfirst Driver

## Allocation and Setup

The driver allocates a single legacy PCI IRQ through
`bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, `rid = 0`,
`RF_SHAREABLE | RF_ACTIVE`. The filter handler is registered through
`bus_setup_intr(9)` with `INTR_TYPE_MISC | INTR_MPSAFE`. A taskqueue
named "myfirst_intr" is created with one worker thread at `PI_NET`
priority.

On successful setup, `INTR_MASK` is written with
`DATA_AV | ERROR | COMPLETE` so the device will assert the line for
those three event classes.

## Filter Handler

`myfirst_intr_filter(sc)` reads `INTR_STATUS`. If zero, it returns
`FILTER_STRAY` (shared-IRQ defence). Otherwise it inspects each of
the three recognised bits, increments a per-bit counter atomically,
writes the bit back to `INTR_STATUS` to acknowledge the device, and
(for `DATA_AV`) enqueues `intr_data_task` on the taskqueue.

The filter returns `FILTER_HANDLED` if any bit was recognised, or
`FILTER_STRAY` otherwise.

## Deferred Task

`myfirst_intr_data_task_fn(sc, npending)` runs in thread context on
the taskqueue's worker thread. It acquires `sc->mtx`, reads
`DATA_OUT`, stores the value in `sc->intr_last_data`, broadcasts
`sc->data_cv` to wake pending readers, and releases the lock.

## Simulated Interrupt sysctl

`dev.myfirst.N.intr_simulate` is write-only; writing a bitmask to it
sets the corresponding bits in `INTR_STATUS` and invokes
`myfirst_intr_filter` directly. This exercises the full pipeline
without needing real IRQ events.

## Teardown

`myfirst_intr_teardown(sc)` runs during detach. It clears
`INTR_MASK`, calls `bus_teardown_intr`, drains and destroys the
taskqueue, and releases the IRQ resource. The order is strict:
mask-clear before teardown (so strays do not accumulate), teardown
before drain (so no new task enqueues happen), drain before free
(so no task runs against freed state).

## Interrupt-Context Accessor Macros

Since the filter runs in primary interrupt context, it cannot take
`sc->mtx`. Two macros in `myfirst_intr.h` hide the raw
`bus_read_4`/`bus_write_4` calls without asserting any lock: `ICSR_READ_4`
and `ICSR_WRITE_4`. Use them only in contexts where a sleep lock
would be illegal.

## Known Limitations

- Only the legacy PCI INTx line is handled. MSI and MSI-X are
  Chapter 20.
- The filter coalesces per-bit counters via atomic ops; the task
  runs at a single priority. Per-queue or per-priority designs are
  later-chapter topics.
- Interrupt storm detection is managed by the kernel
  (`hw.intr_storm_threshold`); the driver does not implement its
  own storm mitigation.
- Chapter 17 simulation callouts are not active on the PCI build;
  the simulated-interrupt sysctl is the way to drive the pipeline
  on a bhyve lab target.
```

Cinco minutos para ler; uma visão clara do formato da camada de interrupção.

### O Passe de Regressão

A regressão do Capítulo 19 é um superconjunto da do Capítulo 18:

1. Compilar sem erros. `make` tem sucesso; nenhum aviso.
2. Carregar. `kldload ./myfirst.ko` tem sucesso; `dmesg` mostra a sequência de attach.
3. Anexar a um dispositivo PCI real. `devinfo -v` mostra o BAR e o IRQ.
4. Nenhum aviso `[GIANT-LOCKED]`.
5. `vmstat -i | grep myfirst` mostra o `intr_event`.
6. `sysctl dev.myfirst.0.intr_count` começa em zero.
7. Interrupção simulada. `sudo sysctl dev.myfirst.0.intr_simulate=1`; contadores incrementam; tarefa é executada.
8. Teste de taxa. Definir `intr_sim_period_ms` como 100; verificar contadores após 10 segundos.
9. Desanexar. `devctl detach myfirst0`; `dmesg` mostra detach limpo.
10. Reanexar. `devctl attach pci0:0:4:0`; ciclo completo de attach é executado.
11. Descarregar. `kldunload myfirst`; `vmstat -m | grep myfirst` mostra zero alocações vivas; `vmstat -i | grep myfirst` não retorna nada.

Executar a regressão completa leva um ou dois minutos por iteração. Um job de CI que a execute vinte vezes em um loop é o tipo de proteção que captura regressões introduzidas pelas extensões dos Capítulos 20 e 21.

### O Que a Refatoração Alcançou

O código do Capítulo 19 tem um arquivo a menos do que teria sem a refatoração; um novo documento foi criado; e o número de versão avançou em um. O driver é reconhecidamente FreeBSD, estruturalmente paralelo a um driver de produção em `/usr/src/sys/dev/`, e está pronto para receber os mecanismos MSI-X do Capítulo 20 e os mecanismos DMA do Capítulo 21 sem precisar de outra reorganização.

### Encerrando a Seção 8

A refatoração segue o mesmo formato estabelecido pelos Capítulos 16 a 18. Um novo arquivo assume a nova responsabilidade. Um novo header exporta a interface pública. Um novo documento explica o comportamento. A versão sobe; a regressão passa; o driver permanece sustentável. Nada dramático: um pouco de arrumação, uma base de código limpa para construir.

O corpo instrucional do Capítulo 19 está completo. Laboratórios, desafios, resolução de problemas, um encerramento e a ponte para o Capítulo 20 vêm a seguir.



## Lendo um Driver Real Juntos: o Caminho de Interrupção do `mgb(4)`

Antes dos laboratórios, uma breve passagem por um driver real que utiliza o mesmo padrão de filtro mais tarefa ensinado no Capítulo 19. `/usr/src/sys/dev/mgb/if_mgb.c` é o driver FreeBSD para os controladores Ethernet gigabit LAN743x da Microchip. É legível, tem qualidade de produção e seu tratamento de interrupções está no nível de complexidade que o vocabulário do Capítulo 19 cobre.

Esta seção percorre as partes relevantes para interrupções de `mgb_legacy_intr` e do código de configuração, indicando onde cada peça corresponde a um conceito do Capítulo 19.

### O Filtro Legacy

O handler de filtro para o caminho de IRQ legacy do `mgb(4)`:

```c
int
mgb_legacy_intr(void *xsc)
{
	struct mgb_softc *sc;
	if_softc_ctx_t scctx;
	uint32_t intr_sts, intr_en;
	int qidx;

	sc = xsc;
	scctx = iflib_get_softc_ctx(sc->ctx);

	intr_sts = CSR_READ_REG(sc, MGB_INTR_STS);
	intr_en = CSR_READ_REG(sc, MGB_INTR_ENBL_SET);
	intr_sts &= intr_en;

	/* TODO: shouldn't continue if suspended */
	if ((intr_sts & MGB_INTR_STS_ANY) == 0)
		return (FILTER_STRAY);

	if ((intr_sts &  MGB_INTR_STS_TEST) != 0) {
		sc->isr_test_flag = true;
		CSR_WRITE_REG(sc, MGB_INTR_STS, MGB_INTR_STS_TEST);
		return (FILTER_HANDLED);
	}
	if ((intr_sts & MGB_INTR_STS_RX_ANY) != 0) {
		for (qidx = 0; qidx < scctx->isc_nrxqsets; qidx++) {
			if ((intr_sts & MGB_INTR_STS_RX(qidx))){
				iflib_rx_intr_deferred(sc->ctx, qidx);
			}
		}
		return (FILTER_HANDLED);
	}
	if ((intr_sts & MGB_INTR_STS_TX_ANY) != 0) {
		for (qidx = 0; qidx < scctx->isc_ntxqsets; qidx++) {
			if ((intr_sts & MGB_INTR_STS_RX(qidx))) {
				CSR_WRITE_REG(sc, MGB_INTR_ENBL_CLR,
				    MGB_INTR_STS_TX(qidx));
				CSR_WRITE_REG(sc, MGB_INTR_STS,
				    MGB_INTR_STS_TX(qidx));
				iflib_tx_intr_deferred(sc->ctx, qidx);
			}
		}
		return (FILTER_HANDLED);
	}

	return (FILTER_SCHEDULE_THREAD);
}
```

Vamos percorrê-lo. O filtro lê dois registradores (`INTR_STS` e `INTR_ENBL_SET`), faz o AND entre eles para obter o subconjunto de interrupções pendentes que estão habilitadas e verifica se algum bit está ativo. Se nenhum estiver, retorna `FILTER_STRAY`, seguindo a disciplina do Capítulo 19 para IRQs compartilhadas.

Para cada classe de interrupção (teste, recepção, transmissão), o filtro reconhece os bits relevantes em `INTR_STS` (escrevendo-os de volta) e agenda o processamento diferido. `iflib_rx_intr_deferred` é a forma do framework iflib de agendar trabalho na fila de recepção; conceitualmente equivale ao `taskqueue_enqueue` do Capítulo 19.

Uma linha que merece atenção: o handler de interrupção de teste escreve em `INTR_STS` mas também define um flag (`sc->isr_test_flag = true`). Essa é a forma do driver de sinalizar para código em espaço do usuário (via sysctl ou ioctl) que uma interrupção de teste foi disparada. O equivalente no Capítulo 19 é o contador `intr_count`.

O último retorno é `FILTER_SCHEDULE_THREAD`. Ele é acionado quando nenhuma das classes de bits específicas coincidiu, mas `MGB_INTR_STS_ANY` coincidiu. A ithread trata o caso residual. O driver do Capítulo 19 não tem esse caminho de queda porque não registra uma ithread; o `mgb(4)` sim.

### O que o Filtro do mgb Ensina

Três lições se aplicam diretamente ao filtro do Capítulo 19:

1. **Leitura com AND de máscara.** `intr_sts & intr_en` garante que o filtro reporte apenas interrupções que estavam de fato habilitadas. Um dispositivo pode internamente reportar eventos que o driver mascarou; o AND os filtra.
2. **Reconhecimento por bit.** Cada classe de bit é reconhecida individualmente (escrevendo os bits específicos de volta). O filtro não escreve `0xffffffff`; escreve apenas os bits que tratou.
3. **Trabalho diferido por fila.** Cada fila de recepção e de transmissão tem seu próprio caminho diferido. O driver mais simples do Capítulo 19 tem uma tarefa; o driver multi-fila do `mgb(4)` tem várias.

### Configuração de Interrupções no mgb

Pesquisando por `bus_setup_intr` em `if_mgb.c` encontram-se vários pontos de chamada, um para o caminho de IRQ legacy e um para cada vetor MSI-X:

```c
if (bus_setup_intr(sc->dev, sc->irq[0], INTR_TYPE_NET | INTR_MPSAFE,
    mgb_legacy_intr, NULL, sc, &sc->irq_tag[0]) != 0) {
	/* ... */
}
```

O padrão é exatamente o do Capítulo 19: handler de filtro, sem ithread, `INTR_MPSAFE`, softc como argumento, cookie retornado. A única diferença é `INTR_TYPE_NET` em vez de `INTR_TYPE_MISC` (o driver é voltado para rede).

### Desmontagem de Interrupções no mgb

O padrão de desmontagem está distribuído pelos helpers do `iflib(9)`, que tratam o drain e a liberação pelo driver. Um driver independente fora do framework iflib realiza a desmontagem explicitamente; o driver do Capítulo 19 também o faz de forma explícita.

### O que a Passagem Guiada Ensina

O caminho de interrupções do `mgb(4)` não é um brinquedo. É uma implementação de qualidade de produção do mesmo padrão que o driver do Capítulo 19 segue. Um leitor que consegue ler `mgb_legacy_intr` com compreensão internalizou o vocabulário do Capítulo 19. O arquivo está livremente disponível; ler o código ao redor (o caminho de attach, a ithread, a integração com iflib) aprofunda ainda mais o entendimento.

Vale ler após o `mgb(4)`: `/usr/src/sys/dev/e1000/em_txrx.c` para padrões multi-vetor MSI-X (material do Capítulo 20), `/usr/src/sys/dev/usb/controller/xhci_pci.c` para o caminho de interrupções de um controlador de host USB (Capítulo 21+) e `/usr/src/sys/dev/ahci/ahci_pci.c` para o caminho de interrupções de um controlador de armazenamento.



## Um Olhar Mais Profundo sobre o Contexto de Interrupção

A Seção 2 listou o que pode e o que não pode acontecer em um filtro. Esta seção vai um nível mais fundo e explica o porquê de cada regra. Um leitor que entende o motivo consegue raciocinar sobre restrições desconhecidas (novos locks, novas APIs do kernel) e prever se são seguras para uso em filtros.

### A Situação da Pilha

Quando a CPU recebe uma interrupção, ela salva os registradores da thread interrompida e muda para uma pilha de interrupção do kernel. A pilha de interrupção é pequena (alguns KB, dependente da plataforma), é por CPU e é compartilhada entre todas as interrupções naquela CPU. O filtro é executado nessa pilha.

Duas implicações:

Primeiro, o filtro tem espaço limitado de pilha. Um filtro que aloca um array grande na pilha (centenas de bytes ou mais) pode causar overflow da pilha de interrupção. O sintoma costuma ser um panic, às vezes uma corrupção silenciosa do que quer que esteja ao lado da pilha na memória. A regra é: filtros têm orçamentos pequenos de pilha. Arrays grandes pertencem à tarefa.

Segundo, a pilha é compartilhada entre interrupções. Um filtro que dormisse (hipoteticamente, pois não pode) deixaria a pilha ocupada; outras interrupções na mesma CPU não poderiam reutilizá-la. Mesmo que dormir fosse permitido, não seria gratuito. A restrição de pilha pequena é um dos motivos pelo qual o filtro deve ser curto.

### Por que Não Há Sleep Locks

Um sleep mutex (o `mtx(9)` padrão) pode bloquear: se outro thread tiver o mutex, `mtx_lock` coloca o thread chamador para dormir no mutex. No contexto do filtro:

- Não há "thread" chamador no sentido usual. A thread interrompida foi suspensa no meio de uma instrução; o filtro é uma excursão do kernel na CPU.
- Dormir a partir da interrupção travaria a CPU: a pilha de interrupção está ocupada, nenhuma outra interrupção pode ser executada nessa CPU e o escalonador não consegue facilmente agendar uma thread diferente sem um estado de kernel funcional.

O kernel poderia, em princípio, tratar esse caso (alguns kernels fazem isso). O design do FreeBSD é proibi-lo. A proibição é imposta pelo `WITNESS` em kernels de depuração: qualquer tentativa de adquirir um lock "sleepable" em contexto de interrupção produz um panic imediato.

Spin mutexes (mutexes com `MTX_SPIN`) são seguros porque não dormem; apenas giram em espera. Um filtro adquirindo um spin mutex está correto.

### Por que Não Há `malloc` com Sleep

`malloc(M_WAITOK)` chama o alocador de páginas da VM e pode dormir se o sistema estiver com pouca memória. Mesmo problema dos locks: o chamador não pode ser suspenso. `malloc(M_NOWAIT)` é a alternativa; pode falhar, mas nunca dorme.

No filtro, as únicas opções seguras são `M_NOWAIT`, zonas `UMA` (que têm seus próprios alocadores limitados) ou buffers pré-alocados. O driver do Capítulo 19 não aloca nada no filtro; toda a memória de que o filtro precisa está no softc, pré-alocada durante o attach.

### Por que Não Há Variáveis de Condição

`cv_wait` e `cv_timedwait` dormem. O filtro não pode dormir. `cv_signal` e `cv_broadcast` não dormem, mas adquirem internamente um sleep mutex na maioria dos usos; um filtro que os utilize deve ter cuidado. A tarefa do Capítulo 19 trata o `cv_broadcast`; o filtro apenas enfileira a tarefa.

### Por que o Filtro Não Pode Re-entrar a Si Mesmo

O dispatcher de interrupções do kernel desabilita novas interrupções na CPU enquanto o filtro está em execução (na maioria das plataformas; algumas arquiteturas usam níveis de prioridade em vez disso). Isso significa que o filtro não pode disparar recursivamente a si mesmo, mesmo que o dispositivo afirme a linha durante a execução. Qualquer afirmação desse tipo fica em fila e dispara após o retorno do filtro.

Uma consequência: o filtro não precisa de proteção interna contra re-entrada. Um `sc->intr_count++` simples é seguro do ponto de vista do filtro contra chamadas simultâneas do próprio filtro na mesma CPU. Ainda pode haver disputa com outro código (a tarefa, leituras do espaço do usuário), razão pela qual se usa a operação atômica, mas o filtro não corre contra si mesmo.

### Por que Operações Atômicas São Seguras

Operações atômicas no FreeBSD são implementadas como instruções de CPU que são, por definição, atômicas. Não tomam um lock; não dormem; não bloqueiam. São seguras em qualquer contexto, incluindo o filtro.

O Capítulo 19 usa `atomic_add_64` extensivamente: para o contador de interrupções, para cada contador por bit e para contagens de invocações de tarefas. As operações são baratas (poucos ciclos) e previsíveis (sem envolvimento do escalonador).

### Por que a ithread Tem Mais Liberdade

A ithread é executada em contexto de thread. Ela tem:

- Sua própria pilha (pilha de thread do kernel normal, muito maior que a pilha de interrupção).
- Sua própria prioridade de escalonamento (elevada, mas ainda uma thread normal).
- A capacidade de dormir se o escalonador assim decidir.

Suas restrições são as regras usuais de contexto de thread: mantenha locks "sleepables" em ordem (para evitar deadlocks), evite segurar um lock ao chamar código sem limite de tempo, use `M_WAITOK` se a alocação puder falhar de outro modo, e assim por diante. As disciplinas dos Capítulos 13 e 15 se aplicam.

A prioridade elevada da ithread significa que ela não deve bloquear por períodos arbitrariamente longos. Bloquear por microssegundos (uma breve contenção de mutex) é aceitável; bloquear por segundos priva todas as outras ithreads do sistema.

### Por que as Threads do taskqueue Têm Ainda Mais Liberdade

A thread trabalhadora de um taskqueue é uma thread regular do kernel, geralmente com prioridade normal (ou a prioridade que o driver especificou em `taskqueue_start_threads`). Ela pode dormir, pode bloquear em qualquer lock "sleepable" e pode alocar arbitrariamente. É o mais flexível dos três contextos.

O trade-off é que o trabalho do taskqueue é menos imediato do que o da ithread. A thread trabalhadora do taskqueue pode não ser executada imediatamente; o escalonador decide. Para trabalho crítico em latência, a ithread é melhor; para trabalho em volume, o taskqueue é mais simples.

O driver do Capítulo 19 usa o taskqueue porque o trabalho em volume que realiza (ler `DATA_OUT`, atualizar o softc, fazer broadcast de uma variável de condição) não é crítico em latência. Os drivers dos Capítulos 20 e 21 podem escolher ithreads ou taskqueues de forma diferente dependendo da carga de trabalho.

### Como o Contexto de Interrupção Interage com o Locking

A disciplina de locking introduzida na Parte 3 ainda se aplica, com um acréscimo: saiba quais locks você pode adquirir em cada contexto.

**Contexto de filtro.** Apenas spin locks, atômicos e algoritmos livres de lock. Sem sleep mutexes, sx locks, rwlocks ou `mtx` inicializado sem `MTX_SPIN`.

**Contexto de ithread.** Todos os tipos de lock. Obedece à ordenação de locks do projeto conforme definido em `LOCKING.md`.

**Contexto de worker do taskqueue.** Todos os tipos de lock. Obedece à ordenação de locks do projeto. Pode dormir arbitrariamente se necessário (embora o autor do driver não deva abusar disso).

**Contexto de thread em geral (handlers de open/read/write/ioctl de cdev, handlers de sysctl).** Todos os tipos de lock. Obedece à ordenação de locks do projeto.

Para o driver do Capítulo 19, o filtro não adquire locks (usa atômicos), a tarefa adquire `sc->mtx` (um sleep mutex) via `MYFIRST_LOCK`, e os handlers de sysctl também adquirem `sc->mtx`. A disciplina é preservada.

### A Nota de Rodapé sobre o "Giant"

BSDs mais antigos usavam um único lock global chamado Giant para serializar o kernel inteiro. Quando o FreeBSD introduziu o SMPng (locking de granularidade fina) no final da década de 1990 e início dos anos 2000, a maior parte do kernel foi convertida, mas alguns caminhos legados ainda mantêm o Giant. Drivers que não definem `INTR_MPSAFE` são automaticamente envoltos pela aquisição do Giant em torno de seus handlers; o `WITNESS` pode reclamar de problemas na ordenação de locks envolvendo o Giant.

O driver do Capítulo 19 define `INTR_MPSAFE` e não toca o Giant. As convenções modernas de desenvolvimento de drivers no FreeBSD depreciam o Giant em código de driver. A nota de rodapé está aqui porque um leitor que pesquisar "Giant" em `kern_intr.c` encontrará referências; elas são artefatos de compatibilidade com código legado.



## Um Olhar Mais Profundo sobre Afinidade de CPU para Interrupções

Você não incluiu o fragmento em markdown a ser traduzido. A mensagem descreve onde o apêndice se encaixa na estrutura do livro, mas não traz o conteúdo em si.

Por favor, cole o fragmento markdown que deseja traduzir e devolverei apenas a versão em português, pronta para concatenação.

### O Que É Afinidade

Afinidade de interrupção é o conjunto de CPUs em que uma interrupção pode disparar. Em sistemas com um único CPU, a afinidade é trivial (apenas um CPU). Em sistemas com múltiplos CPUs, a afinidade se torna interessante: rotear uma interrupção para um CPU específico (em vez de deixar o controlador de interrupções decidir) pode melhorar a localidade de cache, reduzir o tráfego entre sockets e alinhar o tratamento de interrupções com o posicionamento das threads.

No x86, o I/O APIC possui um campo de destino programável por IRQ; o kernel usa esse campo para rotear IRQs. No arm64, o GIC tem recursos similares. A função `bus_bind_intr(9)` do FreeBSD é a API portável que configura a afinidade de um recurso de IRQ específico.

### Comportamento Padrão

Sem um vínculo explícito, o FreeBSD distribui as interrupções entre os CPUs usando um algoritmo round-robin ou específico da plataforma. Para um driver com uma única interrupção como o do Capítulo 19, isso normalmente significa que a interrupção dispara no CPU que o kernel escolheu na inicialização. A afinidade atual pode ser visualizada com `cpuset -g -x <irq>`; o detalhamento por CPU dos disparos de um determinado IRQ não faz parte da saída padrão de `vmstat -i` (que agrega todos os disparos do `intr_event` em uma única contagem), mas pode ser reconstituído por ferramentas do kernel quando a plataforma oferece suporte.

Para muitos drivers, o padrão é suficiente. A taxa de interrupções é baixa o bastante para que a afinidade não importe, ou o trabalho é breve o suficiente para que os custos de migração entre CPUs sejam desprezíveis. O driver do Capítulo 19 se enquadra nessa categoria.

### Quando a Afinidade Importa

Três cenários em que o autor de um driver deseja afinidade explícita:

1. **Taxa de interrupções alta.** Uma NIC que processa dez gigabits de tráfego dispara dezenas de milhares de interrupções por segundo. O custo de mover o trabalho de interrupção entre CPUs se torna real. Vincular o vetor MSI-X de cada fila de recepção a um CPU específico mantém suas linhas de cache quentes.
2. **Localidade NUMA.** Em sistemas com múltiplos sockets, o complexo raiz PCIe do dispositivo está fisicamente conectado a um socket. Interrupções provenientes desse dispositivo são mais baratas de tratar em CPUs do mesmo nó NUMA que o complexo raiz. O posicionamento importa tanto para latência quanto para throughput.
3. **Restrições de tempo real.** Um sistema que exige resposta de baixa latência em CPUs específicos (para aplicações de tempo real) pode direcionar interrupções de manutenção para longe desses CPUs. O `bus_bind_intr` permite que o driver participe desse particionamento.

### A API bus_bind_intr

A assinatura da função:

```c
int bus_bind_intr(device_t dev, struct resource *r, int cpu);
```

`cpu` é um inteiro representando o ID do CPU no intervalo de 0 a `mp_ncpus - 1`. Em caso de sucesso, a interrupção é roteada para esse CPU. Em caso de falha, a função retorna um errno (mais comumente `EINVAL` se a plataforma não suporta revínculo ou se o CPU é inválido).

A chamada vai após `bus_setup_intr`:

```c
error = bus_setup_intr(dev, irq_res, flags, filter, ihand, arg,
    &cookie);
if (error == 0)
	bus_bind_intr(dev, irq_res, preferred_cpu);
```

O driver do Capítulo 19 não vincula sua interrupção. Um exercício desafio adiciona um sysctl que permite ao operador definir o CPU preferido.

### A Abstração de CPU-Set do Kernel

Uma API mais sofisticada: `bus_get_cpus(9)` permite que o driver consulte quais CPUs são considerados "locais" para um dispositivo, útil para drivers com múltiplas filas que desejam distribuir interrupções por um subconjunto de CPUs locais ao nó NUMA. Os cpusets `LOCAL_CPUS` e `INTR_CPUS` definidos em `/usr/src/sys/sys/bus.h` expõem essa informação.

O trabalho com MSI-X do Capítulo 20 usará `bus_get_cpus(9)` para posicionar interrupções por fila em CPUs diferentes do nó NUMA local do dispositivo. O driver de interrupção única do Capítulo 19 não precisa dessa complexidade.

### Observando a Afinidade

O comando `cpuset -g -x <irq>` exibe a máscara de CPUs atual de um IRQ. Para o driver `myfirst` em um sistema com múltiplos CPUs, obtenha o número do IRQ com `devinfo -v | grep -A 5 myfirst0`, vincule a interrupção ao (digamos) CPU 1 com `cpuset -l 1 -x <irq>` e confirme com `cpuset -g -x <irq>`.

Os detalhes são específicos da plataforma. No x86, o I/O APIC (ou o roteamento MSI) implementa a requisição; no arm64, o redistribuidor do GIC faz isso. Algumas arquiteturas recusam o revínculo e retornam um erro; um driver cooperativo trata a chamada a `bus_bind_intr` como uma dica não fatal.



## Uma Visão Aprofundada da Detecção de Interrupt Storm

O kernel do FreeBSD possui proteção interna contra um modo de falha específico: um IRQ disparado por nível que continua disparando continuamente porque o driver não faz o acknowledgment. Essa proteção é chamada de detecção de interrupt storm, está implementada em `/usr/src/sys/kern/kern_intr.c` e é controlada por um único sysctl.

### O Sysctl hw.intr_storm_threshold

```c
static int intr_storm_threshold = 0;
SYSCTL_INT(_hw, OID_AUTO, intr_storm_threshold, CTLFLAG_RWTUN,
    &intr_storm_threshold, 0,
    "Number of consecutive interrupts before storm protection is enabled");
```

O valor padrão é zero (detecção de storm desabilitada). Definir o sysctl para um valor positivo habilita a detecção: se um `intr_event` entregar mais de N interrupções consecutivas sem que nenhuma outra interrupção aconteça no mesmo CPU, o kernel assume que há um storm e limita a taxa do evento.

Limitar a taxa significa que o kernel faz uma pausa (via `pause("istorm", 1)`) antes de executar os handlers novamente. A pausa corresponde a um único tick do relógio, o que na maioria dos sistemas equivale a aproximadamente um milissegundo. O efeito é limitar a taxa com que uma fonte em storm pode consumir CPU.

### Quando Habilitar a Detecção

O padrão desabilitado é a configuração para produção. Habilitar a detecção de storm significa que o kernel pausa as interrupções quando julga que um storm está ocorrendo; se a detecção for equivocada (uma interrupção legítima de alta taxa, como em uma NIC de 10 gigabits), a pausa se torna um problema de desempenho.

Para desenvolvimento de drivers, habilitar a detecção de storm é útil: um acknowledgment esquecido no filter produz um interrupt storm que o kernel detecta, limita e registra no `dmesg`. Sem a detecção, o storm consome um CPU indefinidamente; com ela, o storm fica visível e é limitado.

Uma configuração razoável para desenvolvimento é `hw.intr_storm_threshold=1000`. Mil interrupções consecutivas no mesmo evento sem intercalação é incomum para tráfego legítimo e sinaliza um storm de forma confiável.

### Como um Storm Se Parece

No `dmesg`:

```text
interrupt storm detected on "irq19: myfirst0:legacy"; throttling interrupt source
```

Repetido em intervalos com taxa limitada (uma vez por segundo por padrão, controlado pelo `ppsratecheck` dentro do código de storm do kernel). A fonte da interrupção é identificada pelo nome; o driver pode ser reconhecido a partir dele.

O kernel não desabilita a linha permanentemente; ele controla a cadência do handler. Após o storm terminar (talvez porque o driver foi descarregado ou o dispositivo parou de fazer a asserção), o handler retoma em taxa plena.

### Mitigação de Storm no Lado do Driver

Um driver pode implementar sua própria mitigação de storm. A técnica clássica é:

1. Contar interrupções em uma janela deslizante.
2. Se a taxa exceder um limiar, mascarar as interrupções do dispositivo (via `INTR_MASK`) e agendar uma tarefa para reabilitá-las depois.
3. Na tarefa, inspecionar o dispositivo, limpar o que está causando o storm e reabilitar as interrupções.

Isso é mais invasivo do que o padrão do kernel. A maioria dos drivers não implementa essa técnica. O driver do Capítulo 19 também não; o limiar do kernel é suficiente para os cenários exercitados no capítulo.

### A Relação com IRQs Compartilhados

Em uma linha de IRQ compartilhada, o storm de um driver pode interferir nas interrupções legítimas de outro driver. A detecção de storm do kernel opera por evento, não por handler; portanto, se o handler de um driver for lento ou incorreto, o evento inteiro sofre limitação. Isso é um argumento forte para escrever filters corretos: o impacto do storm não se limita ao driver com o bug.



## Um Modelo Mental para Escolher Entre Filter e ithread

Iniciantes frequentemente têm dificuldade com a decisão entre filter exclusivo, filter mais ithread, filter mais taskqueue e ithread exclusivo. Esta seção fornece um framework de decisão que funciona na maioria das situações, baseado em perguntas que o autor de um driver pode responder sobre seu dispositivo específico.

### Quatro Perguntas

Faça estas perguntas sobre o trabalho da interrupção:

1. **Todo o trabalho pode ser feito em contexto primário?** Se sim (todo acesso a estado via spin locks ou atômicos; todos os acknowledgments via escritas no BAR; sem bloqueios de sleep), filter exclusivo é a escolha mais limpa.
2. **Alguma parte do trabalho exige um sleep lock ou um broadcast de condition variable?** Se sim, o trabalho principal deve ir para contexto de thread. A escolha é entre ithread e taskqueue.
3. **O trabalho diferido é agendado por algum lugar além da interrupção?** Se sim (handlers de sysctl, ioctl, callouts de timer, outras tarefas), um taskqueue é melhor. O mesmo trabalho pode ser agendado a partir de qualquer contexto.
4. **O trabalho diferido é sensível à classe de prioridade da interrupção?** Se sim (você quer a prioridade de ithread `INTR_TYPE_NET` para trabalho de rede), registre um handler de ithread. O ithread herda a prioridade da interrupção; um taskqueue roda com a prioridade que sua thread trabalhadora recebeu na criação.

### Aplicando o Framework

**Filter exclusivo se encaixa em:**
- Um driver de demonstração que apenas incrementa contadores.
- Um driver que só lê um registrador do dispositivo e passa o valor via um atômico.
- Um sensor muito simples cujos dados são produzidos raramente e lidos diretamente.

**Filter mais ithread se encaixa em:**
- Um driver simples em que o trabalho diferido importa apenas na interrupção.
- Um driver que se beneficia da classe de prioridade da interrupção.
- Um driver que deseja o ithread gerenciado pelo kernel sem a maquinaria extra do taskqueue.

**Filter mais taskqueue se encaixa em:**
- Um driver em que o mesmo trabalho diferido pode ser disparado por múltiplas fontes (interrupção, sysctl, ioctl).
- Um driver que precisa coalescer interrupções (o contador `npending` do taskqueue informa quantos enfileiramentos aconteceram desde a última execução).
- Um driver que deseja um número específico de threads trabalhadoras ou uma prioridade independente da categoria da interrupção.
- O caso-alvo do Capítulo 19: o driver `myfirst` agenda a mesma tarefa tanto a partir do filter quanto do sysctl de interrupção simulada.

**ithread exclusivo se encaixa em:**
- Um driver sem trabalho urgente em que toda ação precisa de um sleep lock.
- Um driver em que o filter seria trivial (apenas "agende a thread"); não registrar nenhum filter e deixar o kernel agendar o ithread economiza uma chamada de função.

### Exemplo Aplicado: Um Driver de Armazenamento Hipotético

Suponha que você está escrevendo um driver para um pequeno controlador de armazenamento. O dispositivo tem uma linha de IRQ. Quando uma operação de I/O é concluída, ele define `INTR_STATUS.COMPLETION` e lista os IDs dos comandos concluídos em um registrador de fila de conclusão.

As decisões:

- **Todo o trabalho pode ser feito em contexto primário?** Não. Acordar a thread que emitiu o I/O exige um broadcast de condition variable, o que exige o lock da thread. O filter não pode adquirir esse lock.
- **Qual mecanismo diferido usar?** O trabalho de tratamento de conclusão é agendado apenas pela interrupção, portanto filter mais ithread é uma escolha limpa. A classe de prioridade é `INTR_TYPE_BIO`, que o ithread herda.
- **Design final.** O filter lê `INTR_STATUS`, extrai os IDs dos comandos concluídos em uma fila por contexto de interrupção, faz o acknowledgment e retorna `FILTER_SCHEDULE_THREAD`. O ithread percorre a fila por contexto, associa IDs de comandos a requisições pendentes e acorda a thread de cada requisição.

### Exemplo Aplicado: Um Driver de Rede Hipotético

Uma NIC com quatro vetores MSI-X (duas filas de recepção, duas de transmissão). Cada vetor tem seu próprio filter.

As decisões:

- **Trabalho do filter?** Por fila: fazer o acknowledgment e registrar que a fila tem eventos.
- **Trabalho diferido?** Por fila: percorrer o anel de descritores, construir mbufs, passar para a pilha.
- **Múltiplas fontes?** Apenas a interrupção para operação normal; o modo de polling (para offload em carga alta) é uma segunda fonte. O taskqueue é melhor: tanto o filter quanto o timer de modo poll podem enfileirar.
- **Prioridade?** `INTR_TYPE_NET`, que a prioridade `PI_NET` da thread trabalhadora do taskqueue corresponde.
- **Design final.** Filter por vetor retorna `FILTER_HANDLED` após enfileirar a tarefa por fila. Taskqueue por fila de recepção, um trabalhador cada. Taskqueues configurados com prioridade `PI_NET`.

### Exemplo Aplicado: O Driver do Capítulo 19

Uma linha de IRQ, tipos de eventos simples, trabalho diferido baseado em taskqueue.

- **Trabalho do filter:** Lê `INTR_STATUS`, atualiza contadores por bit, faz o acknowledgment e enfileira a tarefa para `DATA_AV`.
- **Trabalho diferido:** Lê `DATA_OUT`, atualiza o softc e faz broadcast em `data_cv`.
- **Múltiplas fontes?** O filter e o sysctl de interrupção simulada precisam da tarefa. O taskqueue é a escolha certa.
- **Prioridade?** `PI_NET` é um padrão razoável mesmo que o driver não seja uma NIC; o framework de simulação espera responsividade.

### Quando Reconsiderar a Decisão

A decisão não é permanente. Um driver que começa apenas como filter pode ganhar uma task quando adquire uma nova capacidade; um driver que começa com taskqueue pode migrar para ithread quando a flexibilidade extra do taskqueue não é necessária. A refatoração costuma ser pequena (meia hora de reorganização de código).

O framework ajuda você a evitar uma escolha inicial claramente equivocada. Os detalhes são uma questão de julgamento que o autor do driver toma com base no dispositivo específico.

## Ordenação de Locks e o Caminho de Interrupção

A disciplina de locks da Parte 3 introduziu a ideia de que o driver possui uma ordem fixa de locks: `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`. O Capítulo 19 não adiciona novos locks, mas adiciona novos contextos em que os locks existentes são utilizados. Esta subseção examina se as adições do Capítulo 19 respeitam a ordem existente.

### O Filter Não Adquire Locks

O filter lê `INTR_STATUS`, atualiza contadores atômicos e enfileira uma task. Nenhum sleep lock é adquirido. O acesso do filter ao `INTR_STATUS` utiliza `ICSR_READ_4` e `ICSR_WRITE_4`, que não exigem nenhum lock. Portanto, o filter não participa da ordem de locks; ele é livre de locks.

Esta é a escolha mais simples possível. Um filter mais sofisticado poderia usar um spinlock (para proteger uma pequena estrutura de dados compartilhada); o filter do Capítulo 19 é mais simples do que isso.

### A Task Adquire sc->mtx

A função da task `myfirst_intr_data_task_fn` adquire `sc->mtx` (via `MYFIRST_LOCK`), realiza seu trabalho e o libera. Ela não adquire nenhum outro lock. Portanto, a task respeita a ordem de locks existente ao não introduzir nenhum novo padrão de aquisição de lock.

### O sysctl de Interrupção Simulada Adquire e Libera sc->mtx

O handler do sysctl adquire `sc->mtx` para definir `INTR_STATUS`, libera o lock e então invoca o filter. Isso não constitui uma violação da ordem de locks, pois o filter não adquire nenhum lock; nenhuma nova aresta é adicionada ao grafo de locks.

### Os Caminhos de Attach e Detach

O caminho de attach adquire `sc->mtx` brevemente para definir `INTR_MASK` e realizar a leitura de diagnóstico inicial. Ele não mantém o lock durante a chamada a `bus_setup_intr` (que poderia, em princípio, acionar outras partes do kernel que adquirem seus próprios locks; `bus_setup_intr` está documentado como lockable, o que significa que o chamador não pode manter nenhum lock próprio). O caminho de detach também mantém `sc->mtx` brevemente em torno da limpeza de `INTR_MASK`, liberando-o antes de chamar `bus_teardown_intr`.

### Uma Preocupação Sutil de Ordenação: bus_teardown_intr Pode Bloquear

Um detalhe que merece atenção. `bus_teardown_intr` aguarda a conclusão de qualquer invocação de filter ou ithread em andamento antes de retornar. Se o driver mantiver um lock que o filter precisa (por exemplo, um spinlock que o filter adquire brevemente), `bus_teardown_intr` pode bloquear indefinidamente porque o filter não consegue concluir sua execução.

O filter do Capítulo 19 não adquire spinlocks, portanto essa preocupação é teórica. Mas um driver que usa spinlocks no filter deve tomar cuidado: nunca mantenha o spinlock do filter durante uma chamada a `bus_teardown_intr`.

### WITNESS e o Caminho de Interrupção

O `WITNESS` do kernel de depuração rastreia a ordenação de locks em todos os contextos, incluindo o filter. Um filter que adquire um spinlock cria uma aresta de ordenação no grafo do `WITNESS`. Se algum código em contexto de thread adquirir o mesmo spinlock enquanto mantém um spinlock diferente, o `WITNESS` sinaliza um potencial deadlock.

Para o driver do Capítulo 19, nenhuma aresta é adicionada. O `WITNESS` permanece silencioso.

### O Que Documentar em LOCKING.md

O `LOCKING.md` de um bom driver documenta a ordem de locks de forma clara. As adições do Capítulo 19 são menores:

- O filter não adquire locks (apenas operações atômicas).
- A task adquire `sc->mtx` (folha da ordem existente).
- O sysctl de interrupção simulada adquire `sc->mtx` brevemente para definir o estado, libera-o e então invoca o filter (fora de qualquer lock).

Um parágrafo curto em `LOCKING.md` registra esses fatos. A ordem em si não muda.



## Observabilidade: O Que o Capítulo 19 Expõe aos Operadores

Um capítulo sobre interrupções também trata, indiretamente, de observabilidade. O usuário de um driver (um operador de sistema ou um autor de driver depurando um problema) quer enxergar o que o driver está fazendo. O Capítulo 19 expõe uma quantidade modesta de observabilidade por meio de contadores e do sysctl de interrupção simulada; esta subseção consolida o que é visível e como.

### O Conjunto de Contadores

Após o Estágio 4, o driver expõe estes sysctls somente de leitura:

- `dev.myfirst.N.intr_count`: total de invocações do filter.
- `dev.myfirst.N.intr_data_av_count`: eventos DATA_AV.
- `dev.myfirst.N.intr_error_count`: eventos ERROR.
- `dev.myfirst.N.intr_complete_count`: eventos COMPLETE.
- `dev.myfirst.N.intr_task_invocations`: execuções da task.
- `dev.myfirst.N.intr_last_data`: leitura mais recente de DATA_OUT feita pela task.

Os contadores fornecem uma visão concisa da atividade de interrupção. Observá-los ao longo do tempo (via `watch sysctl dev.myfirst.0` ou um loop de shell) mostra a atividade do driver em tempo real.

### Os Sysctls Graváveis

- `dev.myfirst.N.intr_simulate`: escreva uma máscara de bits para simular uma interrupção.

(O driver do Capítulo 19 expõe apenas este sysctl gravável para interrupções. Os exercícios desafio adicionam `intr_sim_period_ms` para simulação baseada em taxa e `intr_cpu` para afinidade.)

### A Visão no Nível do Kernel

`vmstat -i` e `devinfo -v` já mostram a visão do kernel:

- `vmstat -i` mostra a contagem total e a taxa do `intr_event`.
- `devinfo -v` mostra o recurso IRQ do dispositivo.

Esses recursos não são específicos do `myfirst`; estão disponíveis para todo driver. Aprender a interpretá-los faz parte da habilidade geral de operação do FreeBSD.

### Correlacionando as Visões

Um operador tentando diagnosticar um problema pode cruzar as informações dos contadores:

```sh
# The kernel's count of interrupts delivered to our handler.
vmstat -i | grep myfirst

# The driver's count of times the filter was invoked.
sysctl dev.myfirst.0.intr_count
```

Se esses números coincidirem, o caminho do kernel e o filter do driver estão em acordo. Se a contagem do kernel exceder a do driver, algumas interrupções estão sendo tratadas mas não reconhecidas (talvez por outro handler em uma linha compartilhada). Se a contagem do driver exceder a do kernel, algo está errado (o driver está contando invocações que o kernel não entregou; o sysctl de interrupção simulada é o suspeito mais provável se tiver sido acionado recentemente).

Uma diferença de um ou dois ao longo de um ciclo de carga e descarga é normal (questão de timing durante a descarga). Uma diferença crescente e consistente indica um bug.

### DTrace

O provedor `fbt` do kernel permite rastrear a entrada e a saída de qualquer função do kernel, incluindo `myfirst_intr_filter`:

```sh
sudo dtrace -n 'fbt::myfirst_intr_filter:entry { @[probefunc] = count(); }'
```

Isso imprime a contagem de invocações do filter observadas pelo DTrace. Compare com `intr_count`.

De forma ainda mais interessante, um script DTrace pode agregar o tempo por chamada:

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->t = timestamp; }
fbt::myfirst_intr_filter:return /self->t/ {
    @["filter_ns"] = quantize(timestamp - self->t);
    self->t = 0;
}'
```

A saída é um histograma dos tempos de execução do filter em nanosegundos. Um filter saudável leva entre algumas centenas de nanosegundos e alguns microssegundos; qualquer valor acima disso é um bug ou indica um dispositivo extremamente lento.

### ktrace e kgdb

Para depuração profunda, `ktrace` pode rastrear a atividade de chamadas de sistema; `kgdb` pode inspecionar um core dump de kernel gerado por um panic. O Capítulo 19 não os utiliza diretamente, mas um leitor cujo driver entre em panic no caminho de interrupção precisará deles.



Cada laboratório se baseia no anterior e corresponde a um dos estágios do capítulo. Um leitor que concluir todos os cinco terá um driver completo com suporte a interrupções, um pipeline de interrupção simulada e um script de regressão que valida tudo isso.

Os tempos estimados pressupõem que o leitor já tenha lido as seções relevantes.

### Laboratório 1: Explore as Fontes de Interrupção do Seu Sistema

Tempo estimado: trinta minutos.

Objetivo: Desenvolver uma intuição sobre quais interrupções o seu sistema está tratando e em que taxa.

Passos:

1. Execute `vmstat -i > /tmp/intr_before.txt`.
2. Realize algo que exercite o sistema por trinta segundos: execute `dd if=/dev/urandom of=/dev/null bs=1m count=1000`, abra uma página no navegador (em um sistema com sessão gráfica) ou transfira um arquivo de outro host via scp.
3. Execute `vmstat -i > /tmp/intr_after.txt`.
4. Calcule a diferença com `diff`:

```sh
paste /tmp/intr_before.txt /tmp/intr_after.txt
```

5. Para cada fonte que mudou, anote:
   - O nome da interrupção.
   - A contagem antes e depois.
   - A taxa inferida durante os trinta segundos.
6. Escolha uma fonte e identifique seu driver com `devinfo -v` ou `pciconf -lv`.

Observações esperadas:

- As interrupções de timer (`cpu0:timer` e assim por diante) são altas e estáveis, uma por CPU.
- As interrupções de rede (`em0`, `igc0`, etc.) são altas durante a atividade de `dd` ou `scp`, próximas de zero caso contrário.
- As interrupções de armazenamento (`ahci0`, `nvme0`, etc.) são altas durante a atividade de disco, baixas caso contrário.
- Algumas interrupções nunca mudam; esses são dispositivos que permanecem silenciosos durante o seu teste.

Este laboratório é sobre ler a realidade. Sem código. O benefício é que a saída de `vmstat -i` em todos os laboratórios seguintes será território familiar.

### Laboratório 2: Estágio 1, Registrar e Disparar o Handler

Tempo estimado: duas a três horas.

Objetivo: Adicionar a alocação de interrupção, o registro do filter e a limpeza ao driver do Capítulo 18. Versão alvo: `1.2-intr-stage1`.

Passos:

1. A partir do Estágio 4 do Capítulo 18, copie o código-fonte do driver para um novo diretório de trabalho.
2. Edite `myfirst.h` e adicione os quatro campos ao softc (`irq_res`, `irq_rid`, `intr_cookie`, `intr_count`).
3. Em `myfirst_pci.c`, adicione o handler filter mínimo (`atomic_add_64`; retorne `FILTER_HANDLED`).
4. Estenda o caminho de attach com as chamadas de alocação de IRQ, `bus_setup_intr` e `bus_describe_intr`. Adicione os rótulos goto correspondentes.
5. Estenda o caminho de detach com `bus_teardown_intr` e `bus_release_resource` para o IRQ.
6. Adicione um sysctl somente de leitura `dev.myfirst.N.intr_count`.
7. Atualize a string de versão para `1.2-intr-stage1`.
8. Compile: `make clean && make`.
9. Carregue em um guest bhyve. Verifique:
   - Nenhum aviso `[GIANT-LOCKED]` no `dmesg`.
   - `devinfo -v | grep -A 5 myfirst0` mostra tanto `memory:` quanto `irq:`.
   - `vmstat -i | grep myfirst` mostra o handler.
   - `sysctl dev.myfirst.0.intr_count` retorna um valor coerente (zero se o dispositivo estiver inativo).
10. Descarregue o módulo. Verifique que `vmstat -m | grep myfirst` mostra zero alocações ativas.

Falhas comuns:

- `INTR_MPSAFE` ausente: verifique se há `[GIANT-LOCKED]` no `dmesg`.
- Valor de `rid` incorreto: `bus_alloc_resource_any` retorna NULL. Confirme que `sc->irq_rid = 0`.
- Sleep lock no filter: o `WITNESS` entra em panic.
- Teardown ausente: `kldunload` entra em panic ou o kernel de depuração reclama sobre handlers ativos.

### Laboratório 3: Estágio 2, Filter Real e Task Diferida

Tempo estimado: três a quatro horas.

Objetivo: Estender o filter para ler INTR_STATUS, confirmar o recebimento e enfileirar uma task diferida. Versão alvo: `1.2-intr-stage2`.

Passos:

1. A partir do Laboratório 2, adicione os contadores por bit (`intr_data_av_count`, `intr_error_count`, `intr_complete_count`, `intr_task_invocations`, `intr_last_data`) ao softc.
2. Adicione os campos de taskqueue (`intr_tq`) e task (`intr_data_task`).
3. Em `myfirst_init_softc`, inicialize a task e crie o taskqueue.
4. Em `myfirst_deinit_softc`, drene a task e libere o taskqueue.
5. Reescreva o filter para ler `INTR_STATUS`, verificar cada bit, confirmar o recebimento, enfileirar a task para `DATA_AV` e retornar o valor `FILTER_*` correto.
6. Escreva a função de task (`myfirst_intr_data_task_fn`) que lê `DATA_OUT`, atualiza o softc e faz broadcast na variável de condição.
7. No caminho de attach, após o registro do filter, habilite `INTR_MASK` no dispositivo.
8. No caminho de detach, desabilite `INTR_MASK` antes de `bus_teardown_intr`.
9. Adicione sysctls somente de leitura para os novos contadores.
10. Atualize a versão para `1.2-intr-stage2`.
11. Compile, carregue e verifique a fiação básica (igual ao Laboratório 2).

Para observação, aguarde alguns segundos após o carregamento: se o dispositivo estiver produzindo interrupções reais que correspondam ao nosso layout de bits, os contadores avançarão. No alvo bhyve virtio-rnd, nenhuma interrupção real do tipo correto chega; verifique os contadores prosseguindo para o Laboratório 4.

### Laboratório 4: Estágio 3, Interrupções Simuladas via sysctl

Tempo estimado: duas a três horas.

Objetivo: Adicionar o sysctl `intr_simulate` e usá-lo para acionar o pipeline. Versão alvo: `1.2-intr-stage3`.

Passos:

1. A partir do Laboratório 3, adicione o handler do sysctl `intr_simulate` (o da Seção 5).
2. Registre-o em `myfirst_init_softc` ou na configuração do sysctl.
3. Compile e carregue.
4. Simule um único evento `DATA_AV`:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=1
sleep 0.1
sysctl dev.myfirst.0.intr_count
sysctl dev.myfirst.0.intr_data_av_count
sysctl dev.myfirst.0.intr_task_invocations
```

Os três contadores devem mostrar 1.

5. Simule dez eventos `DATA_AV` em um loop:

```sh
for i in 1 2 3 4 5 6 7 8 9 10; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
done
sleep 0.5
sysctl dev.myfirst.0.intr_task_invocations
```

A contagem da task deve ser próxima de 10 (pode ser menor se o taskqueue tiver consolidado múltiplos enfileiramentos em uma única execução; cada execução registra apenas uma invocação, mas `npending` seria maior).

6. Simule os três bits juntos:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=7
```

Os três contadores por bit incrementam.

7. Verifique se `intr_error_count` e `intr_complete_count` incrementam corretamente:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=2  # ERROR
sudo sysctl dev.myfirst.0.intr_simulate=4  # COMPLETE
sysctl dev.myfirst.0 | grep intr_
```

8. Implemente o callout opcional baseado em taxa (`intr_sim_period_ms`) e verifique a taxa:

```sh
sudo sysctl hw.myfirst.intr_sim_period_ms=100
sleep 10
sysctl dev.myfirst.0.intr_count  # around 100
sudo sysctl hw.myfirst.intr_sim_period_ms=0
```

### Lab 5: Stage 4, Refatoração, Regressão, Versão

Duração: três a quatro horas.

Objetivo: Mover o código de interrupção para `myfirst_intr.c`/`.h`, introduzir as macros `ICSR_*`, escrever `INTERRUPTS.md` e executar a regressão. Versão alvo: `1.2-intr`.

Etapas:

1. Partindo do Lab 4, crie `myfirst_intr.c` e `myfirst_intr.h`.
2. Mova o filter, a task, o setup, o teardown e o registro de sysctl para `myfirst_intr.c`.
3. Adicione as macros `ICSR_READ_4` e `ICSR_WRITE_4` a `myfirst_intr.h`.
4. Atualize o filter para usar `ICSR_READ_4`/`ICSR_WRITE_4` em vez de `bus_read_4`/`bus_write_4` diretamente.
5. Em `myfirst_pci.c`, substitua o código de interrupção inline por chamadas a `myfirst_intr_setup` e `myfirst_intr_teardown`.
6. Atualize o `Makefile` para adicionar `myfirst_intr.c` a SRCS. Atualize a versão para `1.2-intr`.
7. Escreva `INTERRUPTS.md` documentando o design do handler de interrupção.
8. Compile.
9. Execute o script de regressão completo (dez ciclos de attach/detach/unload com verificação de contadores; veja o exemplo de acompanhamento).
10. Confirme: sem avisos, sem vazamentos, contadores dentro do esperado.

Resultados esperados:

- O driver na versão `1.2-intr` tem o mesmo comportamento do Stage 3, mas uma estrutura de arquivos mais limpa.
- `myfirst_pci.c` é menor em 50 a 80 linhas.
- `myfirst_intr.c` tem aproximadamente 200 a 300 linhas.
- O script de regressão passa dez vezes seguidas.



## Exercícios Desafio

Os exercícios desafio são opcionais. Cada um parte de um dos labs e estende o driver em uma direção que o capítulo não explorou. Eles consolidam o material do capítulo e são uma boa preparação para o Capítulo 20.

### Exercício Desafio 1: Adicionar um Handler Filter-Plus-ithread

Reescreva o filter do Stage 2 para que retorne `FILTER_SCHEDULE_THREAD` em vez de enfileirar uma task no taskqueue. Registre um handler de ithread pelo quinto argumento de `bus_setup_intr(9)` que realize o trabalho que a task fazia. Compare as duas abordagens.

Este exercício é o caminho natural para internalizar a diferença entre o trabalho diferido baseado em ithread e o baseado em taskqueue. Ao concluí-lo, você será capaz de dizer quando cada abordagem é adequada.

### Exercício Desafio 2: Implementar Mitigação de Storm do Lado do Driver

Adicione um contador que rastreie o número de interrupções tratadas no milissegundo atual. Se a contagem ultrapassar um limiar (por exemplo, 10000), mascare as interrupções do dispositivo e agende uma task para reativá-las 10 ms depois.

Este exercício demonstra que a mitigação do lado do driver é possível e mostra por que o comportamento padrão do kernel (não fazer nada) é geralmente adequado.

### Exercício Desafio 3: Vincular a Interrupção a uma CPU Específica

Adicione um sysctl `dev.myfirst.N.intr_cpu` que aceite um ID de CPU. Quando for escrito, chame `bus_bind_intr(9)` para rotear a interrupção para aquela CPU. Verifique com `cpuset -g` ou com as contagens por CPU em `vmstat -i`.

Este exercício apresenta a API de afinidade de CPU e mostra como a escolha fica visível nas ferramentas de nível de sistema.

### Exercício Desafio 4: Estender as Interrupções Simuladas com Taxas por Tipo

Modifique o callout `intr_sim_period_ms` para aceitar um bitmask das classes de eventos a simular, não apenas `DATA_AV`. Você deve ser capaz de simular eventos `ERROR` e `COMPLETE` alternados em taxas diferentes.

O exercício consolida sua compreensão do tratamento por bit do filter do Stage 2.

### Exercício Desafio 5: Adicionar um Log de Interrupções Espúrias com Limitação de Taxa

Implemente o log de interrupções espúrias baseado em `ppsratecheck(9)` mencionado na Seção 6. Verifique que o log aparece na taxa esperada quando o driver recebe interrupções espúrias (você pode induzi-las desabilitando `INTR_MASK` enquanto o dispositivo gera eventos, ou chamando o filter manualmente com um status zero).

### Exercício Desafio 6: Implementar Alocação de MSI (Preview do Capítulo 20)

Adicione código ao caminho de attach que tente `pci_alloc_msi(9)` primeiro e recorra ao IRX legado se MSI não estiver disponível. O filter permanece o mesmo. Este é um preview do Capítulo 20; fazê-lo agora familiariza você com a API de alocação de MSI.

Observe que no alvo bhyve virtio-rnd, MSI tipicamente não está disponível (o transporte virtio legado do bhyve usa INTx). O `virtio-rng-pci` do QEMU expõe MSI-X; pode ser interessante migrar os labs para QEMU neste desafio.

### Exercício Desafio 7: Escrever um Teste de Latência

Use o caminho de interrupção simulada para medir a latência do driver do momento da interrupção até o espaço do usuário. Um programa do espaço do usuário abre `/dev/myfirst0` e emite um `read(2)` que dorme aguardando a variável de condição; um segundo programa escreve no sysctl `intr_simulate`, iniciando um temporizador de tempo real; o `read` do primeiro programa retorna, parando o temporizador. Plote a distribuição ao longo de muitas iterações.

Este exercício apresenta você à medição de desempenho no caminho diferido do driver. Em um sistema bem ajustado, as latências típicas ficam na casa de dezenas de microssegundos.

### Exercício Desafio 8: Compartilhar o IRQ Deliberadamente

Se você tiver um guest bhyve configurado com múltiplos dispositivos nas funções do mesmo slot, force deliberadamente o compartilhamento de um IRQ. Carregue ambos os drivers (o driver do sistema base para o outro dispositivo; nosso `myfirst` para virtio-rnd). Verifique com `vmstat -i` que eles compartilham a linha. Observe o comportamento quando qualquer um deles aciona a interrupção.

Este exercício é a demonstração mais clara da correção no compartilhamento de IRQ. Um driver que não seguir corretamente a disciplina da Seção 6 se comportará incorretamente aqui.



## Solução de Problemas e Erros Comuns

Uma lista consolidada de modos de falha específicos de interrupção, sintomas e correções. Serve de referência para consulta futura.

### "O driver carrega, mas nenhuma interrupção é contada"

Sintoma: `kldload` termina com sucesso, `dmesg` exibe o banner de attach, mas `sysctl dev.myfirst.0.intr_count` permanece em zero indefinidamente.

Causas prováveis:

1. O dispositivo não está produzindo interrupções. No alvo bhyve virtio-rnd, isso é normal porque o dispositivo não gera eventos no estilo do Capítulo 17. Use o sysctl de interrupção simulada para acionar o pipeline.
2. `INTR_MASK` não foi configurado. O handler está registrado, mas o dispositivo não está acionando a linha porque a máscara é zero. Verifique no caminho de attach a chamada `CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, ...)`.
3. O dispositivo está mascarado por outros meios. Verifique no registrador de comando se `PCIM_CMD_INTxDIS` (bit de desabilitação de interrupção) está ativo; se estiver, limpe-o.
4. O IRQ errado foi alocado. `rid = 0` deve produzir o INTx legado do dispositivo. Verifique se `devinfo -v | grep -A 5 myfirst0` exibe uma entrada `irq:`.

### "Aviso GIANT-LOCKED no dmesg"

Sintoma: `dmesg` após o `kldload` exibe `myfirst0: [GIANT-LOCKED]`.

Causa: `INTR_MPSAFE` não foi passado no argumento de flags de `bus_setup_intr`.

Correção: Adicione `INTR_MPSAFE` às flags. Verifique se o filter usa apenas operações seguras para spin (atômicas, spin mutexes). Verifique se a disciplina de lock no softc permite operação segura em SMP.

### "Kernel panic no filter"

Sintoma: Um kernel panic cujo backtrace exibe `myfirst_intr_filter`.

Causas prováveis:

1. `sc` é NULL ou está obsoleto. Verifique o argumento passado a `bus_setup_intr`; ele deve ser `sc`, não `&sc`.
2. Um sleep lock está sendo adquirido. `WITNESS` entra em panic nesse caso. A correção é remover o sleep lock ou mover o trabalho para um taskqueue.
3. O filter está sendo chamado com um softc já liberado. Isso geralmente significa que detach não desfez o handler antes de liberar o estado. Verifique a ordem de detach.
4. `sc->bar_res` é NULL. Uma condição de corrida entre o desfazimento de falha parcial no attach e a execução do filter. Proteja o primeiro acesso do filter com uma verificação.

### "A task executa, mas acessa estado já liberado"

Sintoma: Kernel panic na função de task, backtrace exibe `myfirst_intr_data_task_fn`.

Causa: A task foi enfileirada durante ou logo antes do detach, e o detach liberou o softc antes de a task ser executada.

Correção: Adicione `taskqueue_drain` ao caminho de detach, antes de liberar qualquer coisa que a task acessa. Veja a Seção 7.

### "Os bits de INTR_STATUS não param de disparar; storm detectada"

Sintoma: `dmesg` exibe `interrupt storm detected`.

Causa: O filter não está reconhecendo `INTR_STATUS` corretamente. Possibilidades:

1. O filter não escreve em `INTR_STATUS`. Adicione a escrita.
2. O filter escreve o valor errado. Escreva os bits específicos que você tratou, não `0` nem `0xffffffff`.
3. O filter trata apenas alguns bits; bits não reconhecidos permanecem ativos e continuam acionando. Reconheça todos os bits, ou reconheça explicitamente os não reconhecidos, ou desative-os em `INTR_MASK`.

### "A interrupção simulada não executa a task"

Sintoma: `sudo sysctl dev.myfirst.0.intr_simulate=1` incrementa `intr_count`, mas não `intr_task_invocations`.

Causas prováveis:

1. O bit simulado não corresponde ao que o filter procura. O filter do Stage 2 enfileira a task para `DATA_AV` (bit 0x1). Escrever `2` ou `4` ativa ERROR ou COMPLETE; esses valores não enfileiram. Escreva `1` ou `7`.
2. A função de task não está registrada. Verifique se `TASK_INIT` é chamado em `myfirst_init_softc`.
3. O taskqueue não foi criado. Verifique `taskqueue_create` e `taskqueue_start_threads` em `myfirst_init_softc`.

### "kldunload falha com Device busy"

Sintoma: `kldunload myfirst` falha com `Device busy`.

Causas: As mesmas do Capítulo 18. Um processo do espaço do usuário mantém o cdev aberto; um comando em andamento não foi concluído; a verificação de ocupado do driver tem um bug. Use `fstat /dev/myfirst0` para ver quem o mantém aberto.

### "vmstat -m mostra alocações vivas após o unload"

Sintoma: `vmstat -m | grep myfirst` retorna `InUse` diferente de zero após `kldunload`.

Causas prováveis:

1. O taskqueue não foi drenado. Verifique `taskqueue_drain` no caminho de detach.
2. O backend de simulação foi anexado (build somente de simulação) e não foi desanexado. Verifique se há `myfirst_sim_detach` no caminho de detach.
3. Um vazamento em `myfirst_init_softc` / `myfirst_deinit_softc`. Verifique se cada alocação tem um free correspondente.

### "O handler é acionado na CPU errada"

Sintoma: `cpuset -g` mostra que a interrupção foi acionada na CPU X; você queria na CPU Y.

Causa: `bus_bind_intr` não foi chamado, ou foi chamado com o argumento de CPU errado.

Correção: Adicione um sysctl que permita ao operador definir a CPU desejada e chamar `bus_bind_intr`. Veja o Exercício Desafio 3.

### "A escrita em INTR_MASK tem efeitos colaterais inesperados"

Sintoma: No alvo bhyve virtio-rnd, escrever no offset 0x10 (o offset de `INTR_MASK` do Capítulo 17) altera o estado do dispositivo de maneiras inesperadas.

Causa: O layout de registradores do Capítulo 17 não corresponde ao do virtio-rnd. O offset 0x10 no virtio-rnd é `queue_notify`, não `INTR_MASK`.

Correção: Isso é uma incompatibilidade de alvo, não um bug de driver. O capítulo reconhece o problema. Para um dispositivo real com o layout do Capítulo 17, a escrita está correta. Para o alvo de ensino do bhyve, a escrita é inofensiva (notifica um virtqueue ocioso), mas sem efeito prático.

### "Mensagens de interrupção espúria no dmesg"

Sintoma: `dmesg` mostra periodicamente mensagens sobre interrupções espúrias na linha de IRQ.

Causas prováveis:

1. O handler não está mascarando `INTR_MASK` no dispositivo corretamente durante o detach (interrupção espúria por nível).
2. O dispositivo está produzindo interrupções que o driver não habilitou. Verifique a configuração de `INTR_MASK`.
3. Outro driver compartilhando a linha está retornando o valor `FILTER_*` errado. Esse é um bug daquele driver, não do nosso.

### "O handler é chamado concorrentemente em múltiplas CPUs"

Sintoma: Contadores atômicos incrementam de forma não monotônica, sugerindo invocações concorrentes do filter.

Causa: Com MSI-X (Capítulo 20), o mesmo handler pode executar concorrentemente em diferentes CPUs. Isso é proposital. Para IRQs legados, isso é raro, mas possível em algumas configurações.

Correção: Garanta que todo acesso ao estado do filter seja atômico ou protegido por spin-lock. O driver do Capítulo 19 usa `atomic_add_64` em todo lugar; nenhuma alteração é necessária.

### "bus_setup_intr retorna EINVAL"

Sintoma: O valor de retorno de `bus_setup_intr` é `EINVAL` e o driver falha ao carregar.

Causas prováveis:

1. Os argumentos `filter` e `ihand` são ambos `NULL`. Pelo menos um deve ser diferente de NULL; caso contrário, o kernel não tem nada a chamar.
2. A flag `INTR_TYPE_*` foi omitida do argumento de flags. Exatamente uma categoria deve ser definida.
3. O recurso de IRQ não foi alocado com `RF_ACTIVE`. Um recurso não ativado não pode ter um handler associado.
4. O argumento de flags contém bits mutuamente exclusivos (raro; o autor do driver precisaria criar esse cenário intencionalmente).

Correção: leia a página de manual do `bus_setup_intr(9)`; o caso mais comum é que o argumento filter ou ithread está ausente, ou que a flag de categoria está ausente.

### "bus_setup_intr retorna EEXIST"

Sintoma: `bus_setup_intr` retorna `EEXIST` em um carregamento subsequente.

Causa: A linha de IRQ já possui um handler exclusivo instalado. Ou este driver foi carregado anteriormente e não foi desmontado corretamente, ou outro driver reivindicou a linha de forma exclusiva.

Correção: Primeiro, tente descarregar qualquer instância anterior (`kldunload myfirst`). Se o problema persistir, verifique `devinfo -v` para identificar qual driver está usando o IRQ no momento.

### "Debug kernel: panics no taskqueue_drain"

Sintoma: `taskqueue_drain` provoca um panic no kernel de debug.

Causas prováveis:

1. O taskqueue nunca foi criado. `sc->intr_tq` é NULL. Verifique `myfirst_init_softc`.
2. O taskqueue já foi liberado. Verifique se há double-free no caminho de desmontagem.
3. `TASK_INIT` nunca foi chamado. O ponteiro de função da task está com valor inválido.

Correção: Garanta que `TASK_INIT` seja executado antes de qualquer chamada a `taskqueue_enqueue`; garanta que `taskqueue_free` seja executado no máximo uma vez.

### "O filter é chamado, mas INTR_STATUS retorna 0xffffffff"

Sintoma: O filter é executado, lê `INTR_STATUS` e encontra `0xffffffff`.

Causas prováveis:

1. O dispositivo não está respondendo (talvez a VM bhyve tenha encerrado ou o dispositivo tenha sido removido a quente).
2. O mapeamento do BAR está incorreto. Verifique o caminho de attach.
3. Um erro PCI colocou o dispositivo em estado de falha.

Correção: Se o dispositivo estiver ativo, a leitura retorna bits de status reais. Se o valor for `0xffffffff`, há algum outro problema. O filter ainda deve retornar `FILTER_STRAY` (pois 0xffffffff dificilmente corresponde a um valor de status legítimo; consulte o datasheet do dispositivo para verificar as combinações de bits válidas).

### "As interrupções são contadas, mas o dispositivo não avança"

Sintoma: `intr_count` continua aumentando, mas a operação do dispositivo (transferência de dados, conclusão de tasks, etc.) não avança.

Causas prováveis:

1. O filter reconhece todos os bits, mas a task não é executada. Verifique `intr_task_invocations`; se for zero, o caminho de `taskqueue_enqueue` está com problema.
2. A task é executada, mas não acorda os waiters. Verifique `cv_broadcast` na task.
3. O dispositivo está sinalizando uma condição incomum. Verifique o conteúdo de `INTR_STATUS` (lido pelo caminho do sysctl ou no DDB).

Correção: Adicione log na task (via `device_printf`); verifique se a lógica da task corresponde ao comportamento real do dispositivo.

### "kldunload trava"

Sintoma: `kldunload myfirst` não retorna. Sem panic, sem saída.

Causas prováveis:

1. `bus_teardown_intr` está bloqueado aguardando um handler em execução (filter ou ithread). O handler está travado.
2. `taskqueue_drain` está bloqueado aguardando uma task que está travada.
3. A função de detach está aguardando uma variável de condição que nunca recebe broadcast.

Correção: Se o sistema responder normalmente nas demais operações, acesse o DDB (pressione a tecla NMI ou execute `sysctl debug.kdb.enter=1`) e use `ps` para localizar as threads travadas. O backtrace geralmente identifica a função em que o travamento ocorreu.

### "Acesso de memória não alinhado no filter"

Sintoma: Um kernel panic em uma arquitetura sensível a alinhamento (arm64, MIPS, SPARC), com um backtrace apontando para o filter.

Causa: O filter está lendo ou escrevendo em um registrador com offset não alinhado. Leituras e escritas no BAR PCI exigem alinhamento natural (4 bytes para leituras de 32 bits, 2 bytes para leituras de 16 bits).

Correção: Use `bus_read_4` / `bus_write_4` em offsets alinhados a 4 bytes. O mapa de registradores do Capítulo 17 é alinhado a 4 bytes em toda a sua extensão.

### "device_printf no filter deixa o sistema lento"

Sintoma: Adicionar chamadas a `device_printf` no filter torna o sistema visivelmente lento em taxas altas de interrupção.

Causa: `device_printf` adquire um lock e realiza uma impressão formatada. A dez mil interrupções por segundo, o overhead é perceptível.

Correção: Remova os prints de debug do filter antes de testes em produção. Use contadores e DTrace para observabilidade.

### "O driver passa em todos os testes, mas se comporta mal sob carga"

Sintoma: Os testes com thread única passam, mas testes de carga com muitos processos concorrentes provocam erros ocasionais ou corrupção de estado.

Causas prováveis:

1. Uma condição de corrida entre o filter e a task. O filter define um flag que a task lê; a task atualiza um estado que o filter lê. Sem sincronização adequada, um pode perder a atualização do outro.
2. Uma condição de corrida entre a task e outro caminho em contexto de thread (handler do cdev, sysctl). A task adquire `sc->mtx`; os outros caminhos também devem fazer o mesmo.
3. Uma variável atômica usada em uma operação composta sem um lock. `atomic_add_64` sozinho é atômico; `atomic_load_64` seguido de um cálculo seguido de `atomic_store_64` não é atômico como sequência.

Correção: Revise a disciplina de locking. `WITNESS` não detecta condições de corrida em variáveis atômicas puras; apenas uma revisão cuidadosa do código pode fazê-lo. Execute sob carga pesada com `INVARIANTS` ativado e observe falhas de asserção.

### "vmstat -i mostra muitos strays em uma linha que não é minha"

Sintoma: Um driver em uma linha compartilhada vê o contador de strays da linha crescendo continuamente.

Causas prováveis:

1. Outro driver na linha retorna `FILTER_STRAY` incorretamente (a interrupção é destinada a ele, mas ele nega).
2. Um dispositivo na linha está sinalizando eventos que os drivers não reconhecem, produzindo strays fantasmas.
3. Ruído de hardware ou modo de disparo mal configurado.

Correção: A solução geralmente está no driver que está retornando `FILTER_STRAY` de forma incorreta. O comportamento do seu próprio driver está correto desde que a verificação do registrador de status esteja certa.



## Observabilidade Avançada: Integração com DTrace

O DTrace do FreeBSD pode observar o caminho de interrupção em vários níveis. Esta subseção apresenta alguns one-liners e scripts DTrace úteis que um autor de driver pode usar durante o desenvolvimento.

### Contando Invocações do Filter

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { @invocations = count(); }'
```

Exibe o número total de vezes que o filter foi chamado desde que o DTrace foi iniciado. Compare com `sysctl dev.myfirst.0.intr_count`; os valores devem coincidir.

### Medindo a Latência do Filter

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_intr_filter:return /self->ts/ {
    @["filter_ns"] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

`vtimestamp` mede o tempo de CPU (não o tempo real do relógio), portanto o histograma reflete genuinamente o tempo de CPU do filter. Um filter saudável fica na faixa de centenas de nanossegundos a poucos microssegundos.

### Observando a Fila de Tasks

```sh
sudo dtrace -n '
fbt::myfirst_intr_data_task_fn:entry {
    @["task_runs"] = count();
    self->ts = vtimestamp;
}
fbt::myfirst_intr_data_task_fn:return /self->ts/ {
    @["task_ns"] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Exibe o número de invocações da task e o tempo de execução por invocação. A task é tipicamente uma ordem de grandeza mais lenta que o filter (pois adquire um sleep lock e realiza mais trabalho).

### Correlacionando o Filter e a Task

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry /!self->in_filter/ {
    self->in_filter = 1;
    self->filter_start = vtimestamp;
    @["filter_enters"] = count();
}
fbt::myfirst_intr_filter:return /self->in_filter/ {
    self->in_filter = 0;
}
fbt::myfirst_intr_data_task_fn:entry {
    @["task_starts"] = count();
}'
```

Se `filter_enters` é 100 e `task_starts` é 80, algumas invocações do filter não agendaram uma task (porque o evento era ERROR ou COMPLETE, e não DATA_AV).

### Rastreando Decisões de Agendamento do Taskqueue

A infraestrutura do taskqueue também possui probes DTrace; é possível observar como a task é enfileirada e quando a thread worker é executada:

```sh
sudo dtrace -n '
fbt::taskqueue_enqueue:entry /arg0 == $${tq_addr}/ {
    @["enqueues"] = count();
}'
```

onde `$${tq_addr}` é o endereço numérico de `sc->intr_tq`, obtido por combinações de `kldstat` / `kgdb`. Esse nível de detalhe geralmente é desnecessário para o driver do Capítulo 19.

### DTrace e o Caminho de Interrupção Simulada

As interrupções simuladas são distinguíveis das interrupções reais porque o caminho simulado passa pelo handler do sysctl:

```sh
sudo dtrace -n '
fbt::myfirst_intr_simulate_sysctl:entry { @["simulate"] = count(); }
fbt::myfirst_intr_filter:entry { @["filter"] = count(); }'
```

A diferença entre os dois contadores corresponde ao número de interrupções reais (chamadas ao filter não precedidas de uma chamada ao sysctl).



## Walkthrough Detalhado: Stage 2 de Ponta a Ponta

Para tornar o driver do Capítulo 19 concreto, apresentamos a seguir um walkthrough completo do que acontece quando um evento `DATA_AV` é simulado pelo sysctl, rastreado passo a passo.

### A Sequência

1. O usuário executa `sudo sysctl dev.myfirst.0.intr_simulate=1`.
2. A maquinaria do sysctl do kernel encaminha a escrita para `myfirst_intr_simulate_sysctl`.
3. O handler analisa o valor (1) e adquire `sc->mtx` via `MYFIRST_LOCK`.
4. O handler escreve `1` em `INTR_STATUS` no BAR.
5. O handler libera `sc->mtx`.
6. O handler chama `myfirst_intr_filter(sc)` diretamente.
7. O filter lê `INTR_STATUS` via `ICSR_READ_4`. O valor é `1` (DATA_AV).
8. O filter incrementa `intr_count` atomicamente.
9. O filter detecta o bit DATA_AV definido e incrementa `intr_data_av_count`.
10. O filter confirma o reconhecimento escrevendo `1` de volta em `INTR_STATUS` via `ICSR_WRITE_4`.
11. O filter enfileira `intr_data_task` em `intr_tq` via `taskqueue_enqueue`.
12. O filter retorna `FILTER_HANDLED`.
13. O handler do sysctl retorna 0 para a camada de sysctl do kernel.
14. O comando `sysctl` do usuário retorna com sucesso.

Enquanto isso, no taskqueue:

15. A thread worker do taskqueue (acordada por `taskqueue_enqueue`) é escalonada.
16. A thread worker chama `myfirst_intr_data_task_fn(sc, 1)`.
17. A task adquire `sc->mtx`.
18. A task lê `DATA_OUT` via `CSR_READ_4`.
19. A task armazena o valor em `sc->intr_last_data`.
20. A task incrementa `intr_task_invocations`.
21. A task faz broadcast em `sc->data_cv` (sem waiters neste exemplo).
22. A task libera `sc->mtx`.
23. A thread worker retorna ao estado de espera por mais trabalho.

Os passos 1 a 14 levam microssegundos; os passos 15 a 23 levam dezenas a centenas de microssegundos, dependendo do escalonamento.

### O Que os Contadores Mostram

Após uma interrupção simulada:

```text
dev.myfirst.0.intr_count: 1
dev.myfirst.0.intr_data_av_count: 1
dev.myfirst.0.intr_error_count: 0
dev.myfirst.0.intr_complete_count: 0
dev.myfirst.0.intr_task_invocations: 1
```

Se `intr_task_invocations` ainda for 0, a task ainda não foi executada (geralmente porque o sysctl retornou antes de a thread worker ser escalonada). Um breve `sleep 0.01` é suficiente.

### O Que o dmesg Mostra

Por padrão, nada. O driver do Stage 4 não é verboso. Um leitor que queira ver o filter disparar pode adicionar chamadas a `device_printf` para depuração, mas drivers de qualidade de produção normalmente não imprimem a cada interrupção.

### O Que vmstat -i Mostra

`vmstat -i | grep myfirst` exibe o contador total do `intr_event`. Ele conta apenas as interrupções reais entregues pelo kernel ao nosso filter. As interrupções simuladas invocadas pelo sysctl não passam pelo dispatcher de interrupções do kernel, portanto não aparecem na contagem de `vmstat -i`.

Essa é uma distinção importante: a simulação via sysctl é um mecanismo complementar, não um substituto. As interrupções reais continuam sendo contadas; as simuladas, não.

### Rastreando com Instruções de Print

Para depuração rápida, adicionar chamadas a `device_printf` no filter e na task oferece uma visão em tempo real:

```c
/* In the filter, temporarily for debugging: */
device_printf(sc->dev, "filter: status=0x%x\n", status);

/* In the task: */
device_printf(sc->dev, "task: data=0x%x npending=%d\n",
    data, npending);
```

Isso produz uma saída no `dmesg` como:

```text
myfirst0: filter: status=0x1
myfirst0: task: data=0xdeadbeef npending=1
```

Remova esses prints antes de ir para produção; o volume de mensagens em taxas altas de interrupção tem um custo significativo.



## Padrões de Drivers Reais do FreeBSD

Um tour conciso pelos padrões de interrupção que aparecem repetidamente em `/usr/src/sys/dev/`. Cada padrão é um trecho concreto de um driver real (levemente reescrito para maior legibilidade), acompanhado de uma observação sobre sua importância. A leitura destes padrões após o Capítulo 19 consolida o vocabulário.

### Padrão: Filter Rápido com Task Lenta

De `/usr/src/sys/dev/mgb/if_mgb.c`:

```c
int
mgb_legacy_intr(void *xsc)
{
	struct mgb_softc *sc = xsc;
	uint32_t intr_sts = CSR_READ_REG(sc, MGB_INTR_STS);
	uint32_t intr_en = CSR_READ_REG(sc, MGB_INTR_ENBL_SET);

	intr_sts &= intr_en;
	if ((intr_sts & MGB_INTR_STS_ANY) == 0)
		return (FILTER_STRAY);

	/* Acknowledge and defer per-queue work. */
	if ((intr_sts & MGB_INTR_STS_RX_ANY) != 0) {
		for (int qidx = 0; qidx < scctx->isc_nrxqsets; qidx++) {
			if (intr_sts & MGB_INTR_STS_RX(qidx))
				iflib_rx_intr_deferred(sc->ctx, qidx);
		}
		return (FILTER_HANDLED);
	}
	return (FILTER_SCHEDULE_THREAD);
}
```

Por que isso importa: o filter é curto, o trabalho diferido é por fila e a disciplina de IRQ compartilhado é mantida. O filter do Capítulo 19 segue o mesmo formato.

### Padrão: Handler Exclusivo de ithread

De `/usr/src/sys/dev/ath/if_ath_pci.c`:

```c
bus_setup_intr(dev, psc->sc_irq,
    INTR_TYPE_NET | INTR_MPSAFE,
    NULL, ath_intr, sc, &psc->sc_ih);
```

O argumento do filter é `NULL`; `ath_intr` é o handler do ithread. O kernel escala `ath_intr` a cada interrupção sem um filter intermediário.

Por que isso importa: às vezes todo o trabalho precisa de contexto de thread. Registrar NULL para o filter é mais simples do que escrever um filter trivial que apenas retorna `FILTER_SCHEDULE_THREAD`.

### Padrão: INTR_EXCL para Acesso Exclusivo

Alguns drivers precisam de acesso exclusivo a uma linha de interrupção:

```c
bus_setup_intr(dev, irq,
    INTR_TYPE_BIO | INTR_MPSAFE | INTR_EXCL,
    NULL, driver_intr, sc, &cookie);
```

Por que isso importa: em casos raros, um driver precisa ter a linha para si (a suposição do handler de ser o único ouvinte está embutida no design). `INTR_EXCL` solicita ao kernel que recuse outros drivers no mesmo evento.

### Padrão: Log de Debug Breve

Alguns drivers têm um modo verbose opcional que registra cada chamada ao filter:

```c
if (sc->sc_debug > 0)
	device_printf(sc->sc_dev, "interrupt: status=0x%x\n", status);
```

Por que isso importa: um driver em desenvolvimento se beneficia do log; um driver em produção quer o log suprimido. Um sysctl (`dev.driver.N.debug`) alterna entre os modos.

### Padrão: Vincular a uma CPU Específica

Drivers que conhecem sua topologia vinculam a interrupção a uma CPU local:

```c
/* After bus_setup_intr: */
error = bus_bind_intr(dev, irq, local_cpu);
if (error != 0)
	device_printf(dev, "bus_bind_intr: %d\n", error);
/* Non-fatal: some platforms do not support binding. */
```

Por que isso importa: handlers locais ao NUMA são mais rápidos. Um driver que se preocupa em fazer o bind oferece uma melhor história de escalabilidade em sistemas multi-socket.

### Padrão: Descrever o Handler para Diagnóstico

Todo driver deve chamar `bus_describe_intr`:

```c
bus_describe_intr(dev, irq, cookie, "rx-%d", queue_id);
```

Por que isso importa: `vmstat -i` e `devinfo -v` usam a descrição para distinguir handlers em eventos compartilhados. Um driver com N filas e N vetores MSI-X realiza N chamadas a `bus_describe_intr`.

### Padrão: Silenciar Antes do Detach

```c
mtx_lock(&sc->mtx);
sc->shutting_down = true;
mtx_unlock(&sc->mtx);

/* Let the interrupt handler drain. */
bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
```

Por que isso importa: o flag `shutting_down` oferece ao handler um caminho de saída rápida (o handler verifica o flag antes de realizar seu trabalho normal). O `bus_teardown_intr` é a drenagem definitiva, mas o flag torna essa drenagem mais rápida.

O driver do Capítulo 19 usa `sc->pci_attached` para uma finalidade similar.



## Referência: Guia de Erros Comuns

Uma lista compacta de erros específicos de interrupção e suas correções em uma linha. Útil como checklist ao revisar o seu próprio driver.

1. **Sem INTR_MPSAFE.** Correção: `flags = INTR_TYPE_MISC | INTR_MPSAFE`.
2. **Sleep lock no filter.** Correção: use operações atômicas ou um spin mutex.
3. **Reconhecimento ausente.** Correção: `bus_write_4(res, INTR_STATUS, bits_handled);`.
4. **Reconhecendo bits em excesso.** Correção: escreva de volta apenas os bits que você tratou.
5. **Retorno `FILTER_STRAY` ausente.** Correção: se o status for zero ou não reconhecido, retorne `FILTER_STRAY`.
6. **Retorno `FILTER_HANDLED` ausente.** Correção: `rv |= FILTER_HANDLED;` para cada bit reconhecido.
7. **Task usando softc obsoleto.** Correção: adicione `taskqueue_drain` ao detach.
8. **`bus_teardown_intr` ausente.** Correção: antes de `bus_release_resource(SYS_RES_IRQ, ...)`.
9. **`INTR_MASK = 0` ausente no detach.** Correção: limpe a máscara antes do teardown.
10. **`taskqueue_drain` ausente.** Correção: drene antes de liberar o estado do softc.
11. **Valor de retorno do filter incorreto.** Correção: deve ser `FILTER_HANDLED`, `FILTER_STRAY`, `FILTER_SCHEDULE_THREAD` ou um OR bit a bit.
12. **Enfileirando uma task antes do `TASK_INIT`.** Correção: inicialize a task no attach.
13. **Não configurar `INTR_MASK` no attach.** Correção: escreva os bits que deseja habilitar.
14. **rid incorreto para IRQ legada.** Correção: use `rid = 0`.
15. **Tipo de recurso incorreto.** Correção: use `SYS_RES_IRQ` para interrupções.
16. **`RF_SHAREABLE` ausente em linha compartilhada.** Correção: inclua o flag na alocação.
17. **Mantendo sc->mtx durante `bus_setup_intr`.** Correção: libere o lock antes da chamada.
18. **Mantendo um spin lock durante `bus_teardown_intr`.** Correção: nunca mantenha o spin lock de um filter durante o teardown.
19. **Taskqueue destruído enquanto a task ainda está na fila.** Correção: `taskqueue_drain` antes de `taskqueue_free`.
20. **Chamada a `bus_describe_intr` ausente.** Correção: adicione-a após `bus_setup_intr` para clareza diagnóstica.



## Referência: Prioridades de ithread e Taskqueue

O código do Capítulo 19 usa `PI_NET` para o taskqueue. O FreeBSD define diversas constantes de prioridade em `/usr/src/sys/sys/priority.h`. Uma visão simplificada:

```text
PI_REALTIME  = PRI_MIN_ITHD + 0   (highest ithread priority)
PI_INTR      = PRI_MIN_ITHD + 4   (the common "hardware interrupt" level)
PI_AV        = PI_INTR            (audio/video)
PI_NET       = PI_INTR            (network)
PI_DISK      = PI_INTR            (block storage)
PI_TTY       = PI_INTR            (terminal/serial)
PI_DULL      = PI_INTR            (low-priority hardware ithreads)
PI_SOFT      = PRI_MIN_ITHD + 8   (soft interrupts)
PI_SOFTCLOCK = PI_SOFT            (soft clock)
PI_SWI(c)    = PI_SOFT            (per-category SWI)
```

Um leitor que olha essa lista vai notar que a maioria dos aliases de "interrupção de hardware" (`PI_AV`, `PI_NET`, `PI_DISK`, `PI_TTY`, `PI_DULL`) resolve para o mesmo valor numérico (`PI_INTR`). O comentário no topo desse bloco em `priority.h` torna o motivo explícito: "Most hardware interrupt threads run at the same priority, but can decay to lower priorities if they run for full time slices". Os nomes de categoria existem porque cada um é lido de forma natural no ponto de chamada, não porque as prioridades numéricas diferem.

Apenas `PI_REALTIME` (ligeiramente acima de `PI_INTR`) e `PI_SOFT` (abaixo de `PI_INTR`) são de fato distintos do nível comum de interrupção de hardware.

A prioridade do ithread vem do flag `INTR_TYPE_*`; a prioridade do taskqueue é definida explicitamente. Passar `PI_NET` para `taskqueue_start_threads` coloca o worker no mesmo nível nominal de um ithread de rede, o que é a escolha certa para trabalho que coopera com o tratamento de interrupções em ritmo de rede. Um driver de armazenamento passaria `PI_DISK`; um driver de segundo plano de baixa prioridade passaria `PI_DULL`. Como as constantes mapeiam para o mesmo valor numérico, os nomes são pragmaticamente intercambiáveis em termos de corretude. Eles ainda importam para a legibilidade e para qualquer kernel futuro em que a distinção se torne real.



## Referência: Um Breve Tour por /usr/src/sys/kern/kern_intr.c

Um leitor curioso sobre o que acontece por trás de `bus_setup_intr(9)` e `bus_teardown_intr(9)` pode abrir `/usr/src/sys/kern/kern_intr.c`. O arquivo tem cerca de 1800 linhas e possui seções distintas:

- **Gerenciamento de intr_event** (`intr_event_create`, `intr_event_destroy`): criação e limpeza de nível superior da estrutura `intr_event`.
- **Gerenciamento de handler** (`intr_event_add_handler`, `intr_event_remove_handler`): as operações subjacentes chamadas por `bus_setup_intr` e `bus_teardown_intr`.
- **Despacho** (`intr_event_handle`, `intr_event_schedule_thread`): o código que efetivamente executa quando uma interrupção ocorre.
- **Detecção de tempestade** (`intr_event_handle`): a lógica de `intr_storm_threshold`.
- **Criação e escalonamento de ithread** (`ithread_create`, `ithread_loop`, `ithread_update`): o mecanismo de ithread por evento.
- **Gerenciamento de SWI (interrupção de software)** (`swi_add`, `swi_sched`, `swi_remove`): interrupções de software.

Um leitor não precisa entender o arquivo inteiro para escrever um driver. Percorrer a lista de funções de nível superior e ler os comentários em `intr_event_handle` (a função de despacho) é uma meia hora bem aproveitada.

### Funções-Chave em kern_intr.c

| Função | Finalidade |
|--------|-----------|
| `intr_event_create` | Alocar um novo `intr_event`. |
| `intr_event_destroy` | Liberar um `intr_event`. |
| `intr_event_add_handler` | Registrar um handler de filter/ithread. |
| `intr_event_remove_handler` | Desregistrar um handler. |
| `intr_event_handle` | Despacho: chamada a cada interrupção. |
| `intr_event_schedule_thread` | Acordar o ithread. |
| `ithread_loop` | O corpo de um ithread. |
| `swi_add` | Registrar uma interrupção de software. |
| `swi_sched` | Escalonar uma interrupção de software. |

As funções BUS_* expostas aos drivers (`bus_setup_intr`, `bus_teardown_intr`, `bus_bind_intr`, `bus_describe_intr`) chamam essas funções internas do kernel após os hooks de bus-driver específicos da plataforma.







## Encerrando

O Capítulo 19 deu ouvidos ao driver. No início, `myfirst` na versão `1.1-pci` estava anexado a um dispositivo PCI real, mas não o escutava: toda ação realizada pelo driver era iniciada pelo espaço do usuário, e os próprios eventos assíncronos do dispositivo (se houver) passavam despercebidos. Ao final, `myfirst` na versão `1.2-intr` possui um handler de filter conectado à linha IRQ do dispositivo, um pipeline de tarefas diferidas que trata o grosso do trabalho em contexto de thread, um caminho de interrupção simulada para testes em alvos de laboratório, uma disciplina de IRQ compartilhada que coexiste com outros drivers na mesma linha, um teardown limpo que libera todos os recursos na ordem correta e um novo arquivo `myfirst_intr.c` junto com o documento `INTERRUPTS.md`.

A transição percorreu oito seções. A Seção 1 apresentou interrupções no nível de hardware, abordando a sinalização por borda e por nível, o fluxo de despacho da CPU e as seis obrigações do handler de um driver. A Seção 2 apresentou o modelo de kernel do FreeBSD: `intr_event`, `intr_handler`, ithread, a divisão filter-mais-ithread, `INTR_MPSAFE` e as restrições do contexto de filter. A Seção 3 escreveu o filter mínimo e a conexão de attach/detach. A Seção 4 estendeu o filter com decodificação de status, reconhecimento por bit e trabalho diferido baseado em taskqueue. A Seção 5 adicionou o sysctl de interrupção simulada que permite ao leitor exercitar o pipeline sem eventos IRQ reais. A Seção 6 codificou a disciplina de IRQ compartilhada: verificar propriedade, retornar `FILTER_STRAY` corretamente, tratar bits não reconhecidos de forma defensiva. A Seção 7 consolidou o teardown: mascarar no dispositivo, desmontar o handler, drenar o taskqueue, liberar recursos. A Seção 8 refatorou tudo em uma estrutura manutenível.

O que o Capítulo 19 não fez foi MSI, MSI-X ou DMA. O caminho de interrupção do driver é uma única IRQ legada; o caminho de dados não usa DMA; o trabalho diferido é uma única task de taskqueue. O Capítulo 20 apresenta MSI e MSI-X (múltiplos vetores, filters por vetor, roteamento de interrupções mais rico). Os Capítulos 20 e 21 apresentam DMA e a interação entre interrupções e anéis de descritores DMA.

O que o Capítulo 19 conquistou é a divisão entre dois fluxos de controle. O filter do driver é curto, executa no contexto primário de interrupção e trata o trabalho urgente por interrupção. A task diferida do driver é mais longa, executa em contexto de thread e trata o grosso do trabalho por evento. A disciplina que os mantém cooperando (atômicos para estado do filter, sleep locks para estado da task, ordenação estrita para teardown) é a disciplina que o código de interrupção de todos os capítulos posteriores pressupõe.

A estrutura de arquivos cresceu: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw_pci.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_pci.c`, `myfirst_pci.h`, `myfirst_intr.c`, `myfirst_intr.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`, `myfirst.h`. A documentação cresceu: `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`. O conjunto de testes cresceu: o pipeline de interrupção simulada, o script de regressão do Estágio 4 e um conjunto de exercícios desafio para manter o leitor praticando.

### Uma Reflexão Antes do Capítulo 20

Uma pausa antes do próximo capítulo. O Capítulo 19 ensinou o padrão filter-mais-task, a promessa do `INTR_MPSAFE`, as restrições do contexto de interrupção e a disciplina de IRQ compartilhada. Os padrões que você praticou aqui (ler status, reconhecer, diferir trabalho, retornar o `FILTER_*` correto, desmontar de forma limpa) são padrões que todo handler de interrupção do FreeBSD usa. O Capítulo 20 acrescentará MSI-X por cima; o Capítulo 21 acrescentará DMA por cima. Nenhum dos dois capítulos substitui os padrões do Capítulo 19; ambos os ampliam.

Uma segunda observação que vale a pena fazer. A composição da simulação do Capítulo 17, do attach PCI real do Capítulo 18 e do tratamento de interrupções do Capítulo 19 é agora um driver completo no sentido arquitetural. Um leitor que compreende as três camadas pode abrir qualquer driver PCI do FreeBSD e reconhecer as partes: o mapa de registradores, o attach PCI, o filter de interrupção. Os detalhes diferem; a estrutura é constante. Esse reconhecimento é o que faz o investimento no livro render dividendos em toda a árvore de código-fonte do FreeBSD.

Terceira observação: o dividendo da camada de acesso do Capítulo 16 continua. Os macros `CSR_*` não mudaram no Capítulo 19; os macros `ICSR_*` foram adicionados para uso no contexto de filter, mas chamam o mesmo `bus_read_4` e `bus_write_4` subjacentes. A abstração agora rendeu dividendos três vezes: contra o backend de simulação do Capítulo 17, contra o BAR PCI real do Capítulo 18 e contra o contexto de filter do Capítulo 19. Um leitor que constrói camadas de acesso similares em seus próprios drivers encontrará o mesmo dividendo.

### O Que Fazer Se Estiver Travado

Três sugestões.

Primeiro, concentre-se no caminho de interrupção simulada. Se `sudo sysctl dev.myfirst.0.intr_simulate=1` faz os contadores incrementarem e a task executar, o pipeline está funcionando. Todo o resto do capítulo é opcional no sentido de que decora o pipeline, mas se o pipeline falhar, o capítulo inteiro não está funcionando e a Seção 5 é o lugar certo para diagnosticar.

Segundo, abra `/usr/src/sys/dev/mgb/if_mgb.c` e releia a função `mgb_legacy_intr` lentamente. São cerca de sessenta linhas de código de filter. Cada linha mapeia para um conceito do Capítulo 19. Lê-la uma vez após concluir o capítulo deve parecer território familiar.

Terceiro, pule os desafios na primeira leitura. Os laboratórios são calibrados para o ritmo do Capítulo 19; os desafios pressupõem que o material do capítulo está sólido. Volte a eles após o Capítulo 20 se parecerem fora de alcance agora.

O objetivo do Capítulo 19 era dar ao driver uma forma de escutar seu dispositivo. Se conseguiu, o mecanismo MSI-X do Capítulo 20 se torna uma especialização em vez de um tópico inteiramente novo, e o DMA do Capítulo 21 se torna uma questão de conectar conclusões de descritores ao caminho de interrupção que você já possui.



## Ponte para o Capítulo 20

O Capítulo 20 tem o título *Tratamento Avançado de Interrupções*. Seu escopo é a especialização que o Capítulo 19 deliberadamente não tomou: MSI (Message Signaled Interrupts) e MSI-X, os mecanismos modernos de interrupção do PCIe que substituem a linha INTx legada por vetores por dispositivo (ou por fila) entregues como escritas na memória.

O Capítulo 19 preparou o terreno de quatro maneiras específicas.

Primeiro, **você tem um filter handler funcional**. O filter do Capítulo 19 lê o status, trata bits, reconhece a interrupção e adia o processamento. O filter do Capítulo 20 é semelhante, mas replicado por vetor: cada vetor MSI-X tem seu próprio filter, e cada um trata um subconjunto específico dos eventos do dispositivo.

Segundo, **você compreende a cascata de attach/detach**. O Capítulo 19 expandiu a cascata com dois labels (`fail_release_irq`, `fail_teardown_intr`). O Capítulo 20 a expande ainda mais: um par de labels por vetor. O padrão não muda; apenas o número de entradas aumenta.

Terceiro, **você tem uma disciplina de desmontagem de interrupções**. O Capítulo 20 reutiliza a ordem do Capítulo 19: limpar as interrupções no dispositivo, `bus_teardown_intr` para cada vetor, `bus_release_resource` para cada recurso de IRQ. A natureza por vetor adiciona um pequeno loop; a ordem permanece a mesma.

Quarto, **você tem um ambiente de laboratório que expõe MSI-X**. No QEMU com `virtio-rng-pci`, o MSI-X está disponível; no bhyve com `virtio-rnd`, apenas o INTx legado é exposto. Os laboratórios do Capítulo 20 podem exigir a mudança para o QEMU ou para um dispositivo bhyve com emulação mais rica para exercitar o caminho MSI-X.

Tópicos específicos que o Capítulo 20 abordará:

- Por que MSI e MSI-X representam uma melhoria em relação ao INTx legado.
- Como MSI difere de MSI-X (vetor único versus tabela de vetores).
- `pci_alloc_msi(9)`, `pci_alloc_msix(9)`: alocação de vetores.
- `pci_msi_count(9)`, `pci_msix_count(9)`: consulta de capacidade.
- `pci_release_msi(9)`: a contraparte de desmontagem.
- Handlers de interrupção com múltiplos vetores: filters por fila.
- O layout da tabela MSI-X e como acessar entradas específicas.
- Afinidade de CPU entre vetores para suporte a NUMA.
- Coalescência de interrupções: redução da taxa de interrupções quando o dispositivo oferece suporte.
- Interação entre MSI-X e iflib (o framework moderno para drivers de rede).
- Migração do driver `myfirst` do Capítulo 19, saindo do caminho legado para um caminho MSI-X, com fallback para o modo legado em dispositivos que não suportam MSI-X.

Você não precisa ler adiante. O Capítulo 19 é preparação suficiente. Traga o seu driver `myfirst` na versão `1.2-intr`, o `LOCKING.md`, o `INTERRUPTS.md`, o kernel com WITNESS habilitado e o script de regressão. O Capítulo 20 começa exatamente onde o Capítulo 19 terminou.

O Capítulo 21 está um capítulo mais à frente e merece um breve apontamento. O DMA introduzirá mais uma interação com interrupções: interrupções de conclusão que sinalizam "a entrada N do anel de descritores está pronta". A disciplina de filter mais task que o Capítulo 19 ensinou permanece válida; o trabalho da task envolve agora percorrer um anel de descritores em vez de ler um único registrador.

O vocabulário é seu; a estrutura é sua; a disciplina é sua. O Capítulo 20 adiciona precisão a todos os três.

## Referência: Cartão de Referência Rápida do Capítulo 19

Um resumo compacto do vocabulário, APIs, macros e procedimentos introduzidos pelo Capítulo 19.

### Vocabulário

- **Interrupt**: um evento assíncrono sinalizado por hardware.
- **IRQ (Interrupt Request)**: o identificador de uma linha de interrupção.
- **Edge-triggered**: sinalizado por uma transição; uma interrupção por transição.
- **Level-triggered**: sinalizado por um nível mantido; uma interrupção dispara enquanto o nível é mantido.
- **intr_event**: a estrutura do kernel do FreeBSD para uma fonte de interrupção.
- **ithread**: a thread do kernel do FreeBSD que executa handlers de interrupção diferidos.
- **filter handler**: uma função que executa no contexto primário de interrupção.
- **ithread handler**: uma função que executa em contexto de thread após o filter.
- **FILTER_HANDLED**: o filter tratou a interrupção; nenhuma ithread é necessária.
- **FILTER_SCHEDULE_THREAD**: o filter tratou parcialmente; execute a ithread.
- **FILTER_STRAY**: a interrupção não era para este driver.
- **INTR_MPSAFE**: uma flag que indica que o handler faz sua própria sincronização.
- **INTR_TYPE_*** (TTY, BIO, NET, CAM, MISC, CLK, AV): dicas de categoria para o handler.
- **INTR_EXCL**: interrupção exclusiva.

### APIs Essenciais

- `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, flags)`: reclama um IRQ.
- `bus_release_resource(dev, SYS_RES_IRQ, rid, res)`: libera o IRQ.
- `bus_setup_intr(dev, res, flags, filter, ihand, arg, &cookie)`: registra um handler.
- `bus_teardown_intr(dev, res, cookie)`: cancela o registro do handler.
- `bus_describe_intr(dev, res, cookie, "name")`: nomeia o handler para as ferramentas.
- `bus_bind_intr(dev, res, cpu)`: direciona a interrupção para uma CPU.
- `pci_msi_count(dev)`, `pci_msix_count(dev)` (Capítulo 20).
- `pci_alloc_msi(dev, &count)`, `pci_alloc_msix(dev, &count)` (Capítulo 20).
- `pci_release_msi(dev)` (Capítulo 20).
- `taskqueue_create("name", M_WAITOK, taskqueue_thread_enqueue, &tq)`: cria uma taskqueue.
- `taskqueue_start_threads(&tq, n, PI_pri, "thread name")`: inicia as threads trabalhadoras.
- `taskqueue_enqueue(tq, &task)`: enfileira uma tarefa.
- `taskqueue_drain(tq, &task)`: aguarda a conclusão de uma tarefa e impede novos enfileiramentos.
- `taskqueue_free(tq)`: libera a taskqueue.
- `TASK_INIT(&task, pri, fn, arg)`: inicializa uma tarefa.

### Macros Essenciais

- `FILTER_HANDLED`, `FILTER_STRAY`, `FILTER_SCHEDULE_THREAD`.
- `INTR_TYPE_TTY`, `INTR_TYPE_BIO`, `INTR_TYPE_NET`, `INTR_TYPE_CAM`, `INTR_TYPE_MISC`, `INTR_TYPE_CLK`, `INTR_TYPE_AV`.
- `INTR_MPSAFE`, `INTR_EXCL`.
- `RF_SHAREABLE`, `RF_ACTIVE`.
- `SYS_RES_IRQ`.

### Procedimentos Comuns

**Alocar uma interrupção PCI legada e registrar um filter handler:**

1. `sc->irq_rid = 0;`
2. `sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);`
3. `bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE, filter, NULL, sc, &sc->intr_cookie);`
4. `bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "name");`

**Desativar um interrupt handler:**

1. Desabilite as interrupções no dispositivo (limpe `INTR_MASK`).
2. `bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);`
3. `taskqueue_drain(sc->intr_tq, &sc->intr_data_task);`
4. `taskqueue_free(sc->intr_tq);`
5. `bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);`

**Escrever um filter handler:**

1. Leia `INTR_STATUS`; se for zero, retorne `FILTER_STRAY`.
2. Para cada bit reconhecido, incremente um contador, confirme escrevendo de volta e, opcionalmente, enfileire uma tarefa.
3. Retorne `FILTER_HANDLED` (ou `FILTER_SCHEDULE_THREAD`), ou `FILTER_STRAY` se nada foi reconhecido.

### Comandos Úteis

- `vmstat -i`: lista as fontes de interrupção com suas contagens.
- `devinfo -v`: lista os dispositivos com seus recursos de IRQ.
- `sysctl hw.intrcnt` e `sysctl hw.intrnames`: contadores brutos.
- `sysctl hw.intr_storm_threshold`: habilita a detecção de tempestade de interrupções do kernel.
- `cpuset -g`: consulta a afinidade de CPU das interrupções (específico da plataforma).
- `sudo sysctl dev.myfirst.0.intr_simulate=1`: dispara uma interrupção simulada.

### Arquivos para Manter nos Favoritos

- `/usr/src/sys/sys/bus.h`: `driver_filter_t`, `driver_intr_t`, `FILTER_*`, `INTR_*`.
- `/usr/src/sys/kern/kern_intr.c`: a maquinaria de eventos de interrupção do kernel.
- `/usr/src/sys/sys/taskqueue.h`: a API de taskqueue.
- `/usr/src/sys/dev/mgb/if_mgb.c`: um exemplo legível de filter combinado com tarefa.
- `/usr/src/sys/dev/ath/if_ath_pci.c`: uma configuração mínima de interrupção usando apenas ithread.



## Referência: Tabela Comparativa ao Longo da Parte 4

Um resumo compacto de onde cada capítulo da Parte 4 se encaixa, o que acrescenta e o que pressupõe. Útil para leitores que estão entrando ou retornando ao longo da parte.

| Tópico | Cap. 16 | Cap. 17 | Cap. 18 | Cap. 19 | Cap. 20 (prévia) | Cap. 21 (prévia) |
|--------|---------|---------|---------|---------|------------------|------------------|
| Acesso a BAR | Simulado com malloc | Estendido com camada de simulação | BAR PCI real | Igual | Igual | Igual |
| Simulação do Cap. 17 | N/A | Introduzida | Inativa no PCI | Inativa no PCI | Inativa no PCI | Inativa no PCI |
| PCI attach | N/A | N/A | Introduzido | Igual + IRQ | Opção MSI-X | Inicialização de DMA adicionada |
| Tratamento de interrupções | N/A | N/A | N/A | Introduzido | MSI-X por vetor | Orientado por conclusão |
| DMA | N/A | N/A | N/A | N/A | Prévia | Introduzido |
| Versão | 0.9-mmio | 1.0-simulated | 1.1-pci | 1.2-intr | 1.3-msi | 1.4-dma |
| Novo arquivo | `myfirst_hw.c` | `myfirst_sim.c` | `myfirst_pci.c` | `myfirst_intr.c` | `myfirst_msix.c` | `myfirst_dma.c` |
| Disciplina principal | Abstração de acessadores | Dispositivo falso | Newbus attach | Divisão filter/tarefa | Handlers por vetor | Mapas de DMA |

A tabela torna a estrutura cumulativa do livro visível de relance. Um leitor que compreende a linha de um determinado tópico consegue prever como o trabalho do Capítulo 19 se encaixa no quadro geral.



## Referência: Páginas do Manual do FreeBSD para o Capítulo 19

Uma lista das páginas do manual mais úteis para o conteúdo do Capítulo 19. Abra cada uma com `man 9 <name>` (para APIs do kernel) ou `man 4 <name>` (para visões gerais de subsistemas) em um sistema FreeBSD.

### Páginas do Manual de APIs do Kernel

- **`bus_setup_intr(9)`**: registro de um interrupt handler.
- **`bus_teardown_intr(9)`**: desativação de um handler.
- **`bus_bind_intr(9)`**: vinculação a uma CPU.
- **`bus_describe_intr(9)`**: rotulação de um handler.
- **`bus_alloc_resource(9)`**: alocação de recursos (genérica).
- **`bus_release_resource(9)`**: liberação de recursos.
- **`atomic(9)`**: operações atômicas, incluindo `atomic_add_64`.
- **`taskqueue(9)`**: primitivas de taskqueue.
- **`ppsratecheck(9)`**: auxiliar de log com limitação de taxa.
- **`swi_add(9)`**: interrupções de software (mencionadas como alternativa).
- **`intr_event(9)`**: maquinaria de eventos de interrupção (quando disponível; algumas APIs são internas).

### Páginas do Manual de Subsistemas de Dispositivos

- **`pci(4)`**: subsistema PCI.
- **`vmstat(8)`**: `vmstat -i` para observar interrupções.
- **`devinfo(8)`**: árvore de dispositivos e recursos.
- **`devctl(8)`**: controle de dispositivos em tempo de execução.
- **`sysctl(8)`**: leitura e escrita de sysctls.
- **`dtrace(1)`**: rastreamento dinâmico.

A maioria delas foi referenciada no corpo do capítulo. Esta lista consolidada é para leitores que desejam um único lugar para encontrá-las.



## Referência: Frases Memoráveis sobre Drivers

Alguns aforismos que resumem a disciplina do Capítulo 19. Úteis para leitura e para revisão de código.

- **"Leia, confirme, adie, retorne."** As quatro coisas que um filter faz.
- **"FILTER_STRAY se você não reconheceu nada."** O protocolo de IRQ compartilhado.
- **"Mascare antes de desativar; desative antes de liberar."** A ordem de detach.
- **"O contexto do filter aceita apenas spinlock."** A regra de não usar sleep locks.
- **"Todo enfileiramento precisa de um drain antes da liberação."** O ciclo de vida da taskqueue.
- **"Um filter, um dispositivo, um estado."** O isolamento que mantém o código por dispositivo sensato.
- **"Se o WITNESS gerar um panic, acredite nele."** O kernel de depuração detecta erros sutis.
- **"PROD primeiro, interrupção depois."** Programe o dispositivo (`INTR_MASK`) antes de habilitar o handler.
- **"Pequeno no filter; grande na tarefa."** A disciplina de tamanho de trabalho.
- **"A detecção de tempestade é uma rede de segurança, não uma ferramenta de design."** Não dependa do throttling do kernel.

Nenhuma dessas frases é uma especificação completa. Cada uma é um lembrete compacto que se desdobra no tratamento detalhado do capítulo.



## Referência: Glossário de Termos do Capítulo 19

**ack (acknowledge)**: a operação de escrever de volta em INTR_STATUS para limpar o bit pendente e desassertar a linha de IRQ.

**driver_filter_t**: o typedef em C para uma função filter handler: `int f(void *)`.

**driver_intr_t**: o typedef em C para uma função ithread handler: `void f(void *)`.

**edge-triggered**: um modo de sinalização de interrupção em que a interrupção é sinalizada por uma transição de nível.

**FILTER_HANDLED**: valor de retorno de um filter significando "esta interrupção foi tratada; nenhuma ithread é necessária".

**FILTER_SCHEDULE_THREAD**: valor de retorno significando "agende a ithread para executar".

**FILTER_STRAY**: valor de retorno significando "esta interrupção não é para este driver".

**filter handler**: uma função C que executa no contexto primário de interrupção.

**Giant**: o lock global único legado do kernel; drivers modernos o evitam configurando INTR_MPSAFE.

**IE (interrupt event)**: abreviação de `intr_event`.

**INTR_MPSAFE**: uma flag que indica que o handler faz sua própria sincronização e é seguro sem o Giant.

**INTR_STATUS**: o registrador do dispositivo que rastreia as causas de interrupção pendentes (RW1C).

**INTR_MASK**: o registrador do dispositivo que habilita classes específicas de interrupção.

**intr_event**: estrutura do kernel que representa uma fonte de interrupção.

**ithread**: thread de interrupção do kernel; executa handlers diferidos em contexto de thread.

**level-triggered**: um modo de sinalização de interrupção em que a interrupção dispara enquanto o nível é mantido.

**MSI**: Message Signaled Interrupts; um mecanismo PCIe (Capítulo 20).

**MSI-X**: a variante mais rica de MSI com uma tabela de vetores (Capítulo 20).

**primary interrupt context**: o contexto de um filter handler; sem bloqueio, sem sleep locks.

**PCIR_INTLINE / PCIR_INTPIN**: campos do espaço de configuração PCI que especificam a linha e o pino de IRQ legados.

**RF_ACTIVE**: flag de alocação de recursos; ativa o recurso em uma única etapa.

**RF_SHAREABLE**: flag de alocação de recursos; permite compartilhar o recurso com outros drivers.

**stray interrupt**: uma interrupção para a qual nenhum filter retornou uma reivindicação; contada separadamente pelo kernel.

**storm**: uma situação em que uma interrupção level-triggered dispara continuamente porque o driver não realiza o acknowledge.

**SYS_RES_IRQ**: tipo de recurso para interrupções.

**taskqueue**: uma primitiva do kernel para executar trabalho diferido em contexto de thread.

**trap stub**: o pequeno trecho de código do kernel que executa quando a CPU assume um vetor de interrupção.

**EOI (End of Interrupt)**: o sinal enviado ao controlador de interrupções para rearmar a linha de IRQ.



## Referência: Uma Nota Final sobre a Filosofia de Tratamento de Interrupções

Um parágrafo para encerrar o capítulo, que vale a pena reler após os laboratórios.

O trabalho de um interrupt handler não é executar o trabalho do dispositivo. O trabalho do dispositivo (processar um pacote, concluir uma operação de I/O, ler um sensor) é feito pelo restante do driver, em contexto de thread, sob o conjunto completo de locks do driver. O trabalho do handler é mais estreito: perceber que o dispositivo tem algo a dizer, fazer o acknowledge do dispositivo para que a conversa possa continuar, agendar o trabalho real que acontecerá depois e retornar rápido o suficiente para que a CPU esteja livre para a thread interrompida ou para a próxima interrupção.

Um leitor que escreveu o driver do Capítulo 19 escreveu um interrupt handler. Ele é pequeno. O restante do driver é o que o torna útil. O Capítulo 20 especializará o handler para trabalho por vetor em MSI-X. O Capítulo 21 especializará a tarefa para percorrer um anel de descritores de DMA. Cada um desses é uma extensão, não uma substituição. O handler do Capítulo 19 é o esqueleto sobre o qual ambos são construídos.

A habilidade que o Capítulo 19 ensina não é "como tratar interrupções para o dispositivo virtio-rnd". É "como dividir o trabalho entre o contexto primário e o contexto de thread, como respeitar as restrições do filter, como realizar a desativação de forma limpa e como cooperar com outros drivers em uma linha compartilhada". Cada uma dessas é uma habilidade transferível. Todo driver na árvore do FreeBSD exercita algumas delas; a maioria dos drivers exercita todas elas.

Para você e para os futuros leitores deste livro, o filter e a task do Capítulo 19 são parte permanente da arquitetura do driver `myfirst`. Todos os capítulos seguintes os pressupõem. Todos os capítulos seguintes os estendem. A complexidade geral do driver vai crescer, mas o caminho de interrupção continuará sendo o que o Capítulo 19 fez dele: um trecho de código enxuto, rápido e corretamente ordenado que sai do caminho para que o restante do driver possa fazer seu trabalho.
