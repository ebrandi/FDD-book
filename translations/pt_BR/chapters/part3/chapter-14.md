---
title: "Taskqueues e Trabalho Diferido"
description: "Como os drivers do FreeBSD movem trabalho para fora de contextos que não podem dormir e o entregam a uma thread que pode: enfileirando tarefas com segurança a partir de temporizadores e interrupções, estruturando um taskqueue privado, consolidando rajadas de trabalho, drenando de forma limpa no detach e depurando os resultados."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 14
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Taskqueues e Trabalho Diferido

## Guia de Leitura e Objetivos

Ao final do Capítulo 13, seu driver `myfirst` adquiriu uma noção pequena mas real de tempo interno. Ele conseguia agendar trabalho periódico com `callout(9)`, emitir um heartbeat, detectar drenagem travada com um watchdog e injetar bytes sintéticos com uma fonte de ticks. Cada callback obedecia a uma disciplina rigorosa: adquirir o mutex registrado, verificar `is_attached`, executar trabalho curto e delimitado, possivelmente rearmar e liberar o mutex. Essa disciplina é o que tornava os timers seguros. É também o que os tornava limitados.

O Capítulo 14 confronta essa limitação diretamente. Um callback de callout é executado em um contexto que não pode dormir. Ele não pode chamar `uiomove(9)`, não pode chamar `copyin(9)`, não pode adquirir um lock `sx(9)` com suporte a sono, não pode alocar memória com `M_WAITOK` e não pode chamar `selwakeup(9)` enquanto o sleep mutex está mantido. Se o trabalho que um timer quer disparar precisa de qualquer uma dessas operações, o timer deve repassar o trabalho. A mesma limitação se aplica aos handlers de interrupção que você encontrará na Parte 4 e a vários outros contextos restritos que aparecem em todo o kernel. O kernel expõe uma única primitiva para esse repasse: `taskqueue(9)`.

Um taskqueue é, em sua forma mais simples, uma fila de pequenos itens de trabalho emparelhada com uma ou mais threads do kernel que consomem essa fila. Seu contexto restrito enfileira uma tarefa; a thread do taskqueue acorda e executa o callback da tarefa em contexto de processo, onde as regras normais do kernel se aplicam. A tarefa pode dormir, pode alocar livremente e pode tocar locks com suporte a sono. O subsistema taskqueue também sabe como agregar rajadas de enfileiramento, cancelar trabalho pendente, aguardar trabalho em andamento durante a desmontagem e agendar uma tarefa para um momento futuro específico. Tudo isso cabe em uma pequena superfície de API e é exatamente o que um driver que usa callout ou interrupções precisa.

Este capítulo ensina `taskqueue(9)` com o mesmo cuidado que o Capítulo 13 dedicou ao `callout(9)`. Começamos com a forma do problema, percorremos a API e depois evoluímos o driver `myfirst` em quatro estágios que adicionam adiamento baseado em tarefas à infraestrutura de timers existente. Ao final, o driver usará um taskqueue privado para mover todo trabalho que não pode ser executado em um contexto de callout ou de interrupção para fora desses contextos, e desmontará o taskqueue no detach sem vazar uma tarefa obsoleta, despertar uma thread morta ou corromper nada.

### Por Que Este Capítulo Ocupa Seu Próprio Espaço

Você poderia fingir que taskqueues não existem. Em vez de enfileirar uma tarefa, seu callout poderia tentar fazer o trabalho diferido inline, aceitar as consequências de causar um panic no kernel na primeira vez que `WITNESS` detectasse um sono com um spinlock mantido, e torcer para que ninguém jamais carregasse seu driver em um kernel de depuração. Essa não é uma opção real, e não vamos considerá-la. O propósito deste capítulo é oferecer a alternativa honesta, aquela que o restante do kernel realmente usa.

Você também poderia criar seu próprio framework de trabalho diferido com `kproc_create(9)` e uma variável de condição personalizada. Isso é tecnicamente possível e ocasionalmente inevitável, mas quase sempre é a escolha errada para começar. Uma thread personalizada é um recurso mais pesado do que uma tarefa e perde a observabilidade que vem de graça quando você usa o framework compartilhado. `ps(1)`, `procstat(1)`, `dtrace(1)`, `ktr(4)`, rastreamentos `wchan` e `ddb(4)` entendem as threads de taskqueue. Eles não entendem seu helper avulso a menos que você o instrumente por conta própria.

Taskqueues são a resposta certa em quase todos os casos em que um driver precisa mover trabalho para fora de um contexto restrito. O custo de não conhecê-los é maior do que o custo de aprendê-los, e o custo de aprendê-los é modesto: a API é menor do que a do `callout(9)`, as regras são regulares e os idiomas se transferem diretamente entre drivers. Uma vez que o modelo mental se encaixa, você começará a reconhecer o padrão em quase todos os drivers em `/usr/src/sys/dev/`.

### Onde o Capítulo 13 Deixou o Driver

Um checkpoint rápido antes de prosseguirmos. O Capítulo 14 estende o driver produzido ao final do Estágio 4 do Capítulo 13, não nenhum estágio anterior. Se algum dos itens abaixo parecer incerto, retorne ao Capítulo 13 antes de iniciar este capítulo.

- Seu driver `myfirst` compila sem erros e se identifica como versão `0.7-timers`.
- Ele possui três callouts declarados na softc: `heartbeat_co`, `watchdog_co` e `tick_source_co`.
- Cada callout é inicializado com `callout_init_mtx(&co, &sc->mtx, 0)` em `myfirst_attach` e drenado com `callout_drain` em `myfirst_detach` após `is_attached` ter sido limpo.
- Cada callout tem um sysctl de intervalo (`heartbeat_interval_ms`, `watchdog_interval_ms`, `tick_source_interval_ms`) que tem valor padrão zero (desabilitado) e reflete as transições de habilitação e desabilitação em seu handler.
- O caminho de detach é executado na ordem documentada: recusar em `active_fhs`, limpar `is_attached`, fazer broadcast em ambas as cvs, drenar `selinfo`, drenar todos os callouts, destruir dispositivos, liberar sysctls, destruir cbuf e contadores e cvs e o sx e o mutex.
- Seu `LOCKING.md` tem uma seção Callouts que nomeia cada callout, seu callback, seu lock e seu ciclo de vida.
- O kit de stress do Capítulo 13 (testadores do Capítulo 12 mais os exercitadores de timer do Capítulo 13) compila e executa sem problemas com `WITNESS` e `INVARIANTS` habilitados.

Esse é o driver que estendemos. O Capítulo 14 não retrabalha nenhuma dessas estruturas. Ele adiciona uma nova coluna à softc, uma nova chamada de inicialização, uma nova chamada de desmontagem e pequenas mudanças nos três callbacks de callout e em um ou dois outros lugares onde o driver se beneficiaria de mover trabalho para fora de um contexto restrito.

### O Que Você Aprenderá

Ao final deste capítulo, você será capaz de:

- Explicar por que alguns trabalhos não podem ser feitos dentro de um callback de callout ou de um handler de interrupção, e reconhecer as operações que forçam um repasse para um contexto diferente.
- Descrever as três coisas que um taskqueue é: uma fila de `struct task`, uma thread (ou um pequeno pool de threads) que consome a fila e uma política de enfileiramento e despacho que une os dois.
- Inicializar uma tarefa com `TASK_INIT(&sc->foo_task, 0, myfirst_foo_task, sc)`, entender o que cada argumento significa e posicionar a chamada no estágio correto do attach.
- Enfileirar uma tarefa com `taskqueue_enqueue(tq, &sc->foo_task)` a partir de um callback de callout, de um handler de sysctl, de um caminho de leitura ou escrita, ou de qualquer outro código do driver onde o adiamento é a resposta certa.
- Escolher entre os taskqueues de sistema predefinidos (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_swi_giant`, `taskqueue_fast`, `taskqueue_bus`) e um taskqueue privado que você cria com `taskqueue_create` e popula com `taskqueue_start_threads`.
- Entender o contrato de coalescência: quando uma tarefa é enfileirada enquanto já está pendente, o kernel incrementa `ta_pending` em vez de vinculá-la duas vezes, e o callback recebe a contagem final de pendências para que possa processá-las em lote.
- Usar a variante `struct timeout_task` com `taskqueue_enqueue_timeout` para agendar uma tarefa para um momento futuro específico e drená-la corretamente com `taskqueue_drain_timeout`.
- Bloquear e desbloquear um taskqueue em torno de etapas delicadas de desligamento, e colocá-lo em quiescência quando você precisar da garantia de que nenhuma tarefa está sendo executada em nenhum ponto da fila.
- Drenar todas as tarefas que um driver possui no detach, na ordem correta, sem causar deadlock com os callouts e cvs que você já drena.
- Separar as responsabilidades do código de timer e do código de tarefa dentro do código-fonte do driver, de modo que um novo leitor consiga identificar qual trabalho é executado em qual contexto apenas lendo o arquivo.
- Reconhecer e aplicar o padrão de driver de rede que usa `epoch(9)` e grouptaskqueues para caminhos de leitura sem lock, no nível de saber quando recorrer a eles e quando não.
- Depurar um driver que usa taskqueue com `procstat -t`, `ps ax`, `dtrace -l` e `ktr(4)`, e interpretar o que cada ferramenta mostra.
- Marcar o driver com a versão `0.8-taskqueues` e documentar a política de adiamento em `LOCKING.md` para que a próxima pessoa que herdar o driver consiga entendê-la.

É uma lista longa. A maioria dos itens se apoia nos anteriores, portanto a progressão dentro do capítulo é o caminho natural.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 14 permaneça focado.

- **Handlers de interrupção como tópico principal.** A Parte 4 apresenta `bus_setup_intr(9)` e a divisão entre handlers `FILTER` e `ITHREAD`. O Capítulo 14 menciona o contexto de interrupção ao explicar por que o trabalho diferido importa, e os padrões que ensina se transferem diretamente de callouts para handlers de interrupção reais, mas a própria API de interrupção é tarefa da Parte 4.
- **A história completa de variáveis de condição e semáforos.** O Capítulo 15 amplia o vocabulário de sincronização com semáforos contadores, bloqueio interrompível por sinais e handshakes entre componentes que coordenam timers, tarefas e threads de usuário. O Capítulo 14 usa a infraestrutura de cv existente como está e não adiciona novas primitivas de sincronização além do que os próprios taskqueues trazem.
- **Cobertura aprofundada de grouptaskqueues e iflib.** A família `taskqgroup` existe e este capítulo explica quando ela é a resposta certa, mas a história completa pertence aos drivers de rede da Parte 6 (Capítulo 28). A introdução aqui é intencionalmente superficial.
- **Caminhos de conclusão de DMA orientados por hardware.** Um taskqueue é o lugar natural para finalizar uma transferência DMA após o hardware sinalizar a conclusão via interrupção, e mencionamos esse padrão, mas a mecânica de gerenciamento de buffer DMA aguarda até os capítulos de bus-space e DMA.
- **Workloops, kthreads para polling por CPU e os hooks avançados do escalonador.** Estes são partes reais do cenário de trabalho diferido do kernel, mas são especializados e um driver raramente os encontra. Quando importam, os capítulos que os necessitam os apresentam.

Manter-se dentro dessas linhas preserva a coerência do modelo mental do capítulo. O Capítulo 14 lhe dá uma boa ferramenta, ensinada com cuidado. Os capítulos posteriores lhe darão as ferramentas vizinhas e os contextos de hardware reais que justificam seu uso.

### Tempo Estimado de Investimento

- **Somente leitura**: cerca de três horas. A superfície da API é menor do que a do `callout(9)`, mas a interação entre um taskqueue e o restante da história de locking do driver leva um pouco de tempo para se sedimentar.
- **Leitura mais digitação dos exemplos resolvidos**: seis a oito horas em duas sessões. O driver evolui em quatro estágios; cada estágio modifica aproximadamente um aspecto.
- **Leitura mais todos os laboratórios e desafios**: dez a catorze horas em três ou quatro sessões, incluindo o tempo necessário para observar as threads de taskqueue sob carga com `procstat`, `dtrace` e uma carga de stress.

Se você achar as regras de ordenação no início da Seção 4 desorientadoras, isso é normal. A sequência de detach com callouts, cvs, handlers de sel e agora tarefas tem quatro peças que precisam se encaixar. Percorreremos a ordem uma vez, a enunciaremos, a justificaremos e a reutilizaremos.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 13 (`stage4-final`). O ponto de partida pressupõe os três callouts, os três sysctls de intervalo, a disciplina `is_attached` em cada callback e a ordem de detach documentada.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco e correspondendo ao kernel em execução. Várias das referências de código-fonte neste capítulo são coisas que você deve realmente abrir e ler.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e inicializando sem problemas.
- O Capítulo 13 está bem assimilado. O callout com consciência de lock, a disciplina `is_attached` nos callbacks e a ordenação do detach são conhecimentos pressupostos aqui.
- Você executou o kit de stress do Capítulo 13 pelo menos uma vez com todos os timers habilitados e o viu passar sem erros.

Se algum desses pontos estiver instável, corrigi-lo agora é um investimento melhor do que avançar para o Capítulo 14 e tentar depurar a partir de uma base ainda em movimento. Os padrões do Capítulo 14 foram projetados especificamente para se combinar com os padrões do Capítulo 13; partir de um driver do Capítulo 13 que não está completamente correto torna cada etapa do Capítulo 14 mais difícil.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos darão retorno rapidamente.

Primeiro, mantenha `/usr/src/sys/kern/subr_taskqueue.c` e `/usr/src/sys/sys/taskqueue.h` nos seus favoritos. O header é curto, cerca de duzentas linhas, e é o resumo canônico da API. O arquivo de implementação tem cerca de mil linhas, bem comentadas, e ler `taskqueue_run_locked` com atenção vale o esforço na primeira vez que você precisar raciocinar sobre o que o contador `pending` de uma task realmente significa. Dez minutos com o header agora economizam dez horas de dúvida mais tarde.

Segundo, rode todas as mudanças de código sob `WITNESS`. O subsistema de taskqueue tem seu próprio lock (um spin mutex ou um sleep mutex, dependendo se a fila foi criada com `taskqueue_create` ou `taskqueue_create_fast`), e ele interage com os locks do seu driver de maneiras que o `WITNESS` entende. Uma aquisição de lock mal posicionada dentro de um callback de task é exatamente o tipo de bug que o `WITNESS` detecta instantaneamente em um kernel de debug e corrompe silenciosamente em um kernel de produção. Não rode o código do Capítulo 14 no kernel de produção antes de ele passar no kernel de debug.

Terceiro, digite as mudanças à mão. O código-fonte companion em `examples/part-03/ch14-taskqueues-and-deferred-work/` é a versão canônica, mas a memória muscular vale mais do que a leitura. O capítulo introduz edições incrementais pequenas; espelhe esse ritmo de passos curtos na sua própria cópia do driver. Quando o ambiente de teste passar em um estágio, faça um commit dessa versão e avance; quando um passo quebrar, o commit anterior é o seu ponto de recuperação.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. Por que usar trabalho diferido em um driver. A forma do problema: o que não pode ser feito em callouts, interrupções e outros contextos restritos; os casos do mundo real que forçam a transferência.
2. Introdução ao `taskqueue(9)`. As estruturas, a API, as filas predefinidas e a comparação com callouts.
3. Diferindo trabalho a partir de um timer ou interrupção simulada. O primeiro refactor, Estágio 1: adicionar uma única task que um callout enfileira.
4. Configuração e limpeza do taskqueue. Estágio 2: criar um taskqueue privado, conectar a sequência de detach e auditar o resultado com o `WITNESS`.
5. Priorização e coalescência de trabalho. Estágio 3: usar o comportamento de coalescência de `ta_pending` deliberadamente para batching, introduzir `taskqueue_enqueue_timeout` para tasks agendadas e discutir prioridades.
6. Padrões do mundo real usando taskqueues. Um tour pelos padrões que se repetem em drivers reais do FreeBSD, apresentados como pequenas receitas que você pode copiar para o seu próprio código.
7. Depurando taskqueues. As ferramentas, os erros comuns e um exercício guiado de quebrar e corrigir em um cenário realista.
8. Refatoração e versionamento. Estágio 4: consolidar o driver em um todo coerente, incrementar a versão para `0.8-taskqueues` e estender o `LOCKING.md`.

Após as oito seções principais, abordamos `epoch(9)`, grouptaskqueues e taskqueues por CPU em um nível introdutório leve; em seguida, laboratórios práticos, exercícios desafio, uma referência de resolução de problemas, uma seção de encerramento e uma ponte para o Capítulo 15.

Se esta é sua primeira leitura, avance linearmente e faça os laboratórios em ordem. Se você está revisitando, as Seções 4, 6 e 8 são independentes e funcionam bem como leituras de uma única sessão.



## Seção 1: Por Que Usar Trabalho Diferido em um Driver?

O Capítulo 13 terminou com um driver cujos callouts faziam todo o trabalho que o callback podia fazer com segurança. O heartbeat imprimia um relatório de status de uma linha. O watchdog registrava uma única contagem e opcionalmente imprimia um aviso. A fonte de ticks escrevia um único byte no buffer circular e sinalizava uma variável de condição. Cada callback levava microssegundos, mantinha o mutex por esses microssegundos e retornava. Esse é o contrato do callout no seu melhor: pequeno, previsível, consciente de locks e barato.

O trabalho real de um driver nem sempre cabe dentro desse contrato. Algumas tasks querem rodar no mesmo ritmo do timer que detecta a necessidade delas, mas querem fazer coisas que o timer não pode fazer com segurança. Outras tasks são disparadas por um contexto restrito diferente (um handler de interrupção, por exemplo, ou uma rotina de filtro na pilha de rede) mas enfrentam o mesmo problema de "não pode fazer isso aqui". Esta seção percorre a forma do problema: o que não pode ser feito em contextos restritos, que tipos de trabalho os drivers precisam diferir e que opções o kernel oferece para levar esse trabalho a um lugar onde ele possa realmente ser executado.

### O Contrato do Callout, Relido

Uma releitura curta da regra do callout, porque a história do taskqueue é inteiramente sobre as coisas que os callouts não podem fazer.

Um callback de `callout(9)` roda em um de dois modos. O modo padrão é o despacho a partir da thread de callout: a thread de callout dedicada do kernel para aquela CPU acorda em um limite de clock de hardware, percorre a roda de callouts, encontra os callbacks cujos prazos chegaram e os chama um a um. O modo alternativo, `C_DIRECT_EXEC`, roda o callback diretamente dentro do próprio handler de interrupção de clock de hardware. Seu driver raramente escolhe o modo alternativo; o padrão é o que quase todos os drivers usam.

Em ambos os modos, o callback roda com o lock registrado do callout mantido (para a família `callout_init_mtx`) e não pode cruzar certos limites de contexto. Ele não deve dormir. Dormir significa chamar qualquer primitiva que possa desagendar a thread e bloqueá-la por um período indeterminado esperando por uma condição. `mtx_sleep`, `cv_wait`, `msleep`, `sx_slock`, `malloc(..., M_WAITOK)`, `uiomove`, `copyin` e `copyout` todos dormem em seu caminho lento. `selwakeup(9)` não dorme per se, mas adquire o mutex por selinfo, que pode ser o mutex errado para o contexto em que o callout está rodando, e a prática padrão é chamá-lo sem nenhum mutex do driver mantido de qualquer forma. Nenhuma dessas chamadas pertence ao interior de um callback de callout.

Essas são regras rígidas no nível do kernel. `INVARIANTS` e `WITNESS` detectam muitas das violações em tempo de execução. Algumas delas corrompem o kernel silenciosamente, de maneiras difíceis de depurar depois. Em todos os casos, um driver que quer o efeito de uma dessas operações deve fazer a chamada a partir de um contexto que a permita. Esse contexto é o contexto que um taskqueue fornece.

O restante desta seção expande a mesma observação de ângulos diferentes: que tipo de trabalho um driver quer diferir, por que os contextos restritos merecem as restrições e quais facilidades do FreeBSD competem pelo trabalho.

### Contextos Restritos, Não Apenas Callouts

O callout é o primeiro contexto restrito que um driver no estilo `myfirst` encontra, mas não é o único. Vários outros lugares no kernel executam código que não pode dormir ou não pode fazer certos tipos de alocação. Um driver que quer tomar uma ação a partir de qualquer um deles enfrenta a mesma decisão de "diferir".

**Filtros de interrupção de hardware.** Quando um dispositivo real levanta uma interrupção, o kernel executa uma rotina de filtro de forma síncrona na CPU que recebeu a interrupção. Filtros não podem dormir, não podem adquirir sleep mutexes e não podem chamar a maioria das APIs normais do kernel. Eles geralmente são divididos em um filtro minúsculo (ler um registrador de status, decidir se a interrupção é nossa) que roda em contexto de hardware, mais uma ithread (interrupt thread) associada que roda o trabalho real em um contexto de thread completo. Encontraremos a divisão precisa filtro/ithread quando a Parte 4 apresentar `bus_setup_intr(9)`, mas a lição estrutural está clara mesmo agora: filtros de interrupção são outro lugar onde o trabalho deve ser transferido para algum outro lugar.

**Caminhos de entrada de pacotes de rede.** Partes do caminho de recepção de `ifnet(9)` rodam sob proteção de `epoch(9)`, o que restringe o tipo de aquisições de lock e operações de sleep que são seguras. Drivers de rede frequentemente enfileiram uma task quando querem fazer um trabalho não trivial que pertence ao contexto de processo.

**Callbacks de `taskqueue_fast` e `taskqueue_swi`.** Mesmo quando você já está dentro de um callback de task, se a task está rodando em uma fila apoiada por spin mutex (`taskqueue_fast`) ou uma fila de software-interrupt (`taskqueue_swi`), a mesma regra de proibição de sleep se aplica como para o contexto de origem. Callbacks de tasks no `taskqueue_thread` padrão não têm essa restrição; eles rodam em contexto de thread completo e podem dormir livremente. Essa distinção é importante e voltaremos a ela na Seção 2.

**Seções de leitura de `epoch(9)`.** Um caminho de código delimitado por `epoch_enter()` e `epoch_exit()` não pode dormir. Drivers de rede usam esse padrão extensivamente para tornar os caminhos de leitura livres de lock; o trabalho do lado de escrita é diferido para fora do epoch. O Capítulo 14 aborda epoch em um nível introdutório na parte posterior de "Tópicos adicionais" do capítulo.

O fio comum em todos esses contextos é que algo no ambiente ao redor proíbe operações de contexto de thread. O "algo" difere (um spin lock, um contexto de filtro, uma seção de epoch, um despacho de software-interrupt), mas o remédio é o mesmo: enfileirar uma task para ser executada mais tarde por uma thread que não está no contexto restrito.

### Razões do Mundo Real para Diferir

Breves tours pelos tipos de trabalho que os drivers empurram para fora de contextos restritos. Reconhecer as formas agora dá vocabulário para os padrões que a Seção 6 desenvolverá.

**`selwakeup(9)` não trivial.** `selwakeup` é a chamada do kernel para notificar todos os aguardadores de select/poll. A sabedoria tradicional diz para chamá-la sem nenhum mutex do driver mantido, e nunca a partir de um contexto que tem um spin lock mantido. Um callback de callout mantém um mutex; um filtro de interrupção não mantém nada, mas está ele mesmo em um lugar ruim. Drivers que querem notificar aguardadores de poll a partir desses contextos tipicamente enfileiram uma task cujo único trabalho é chamar `selwakeup`.

**`copyin` e `copyout` após um evento de hardware.** Depois que uma interrupção sinaliza que uma transferência DMA foi concluída, o driver pode querer copiar dados de ou para um buffer em espaço do usuário cujo endereço foi registrado com um ioctl anterior. Nem `copyin` nem `copyout` são legais em contexto de interrupção. O driver agenda uma task cujo callback faz a cópia em contexto de processo.

**Reconfiguração que requer um lock que pode dormir.** A configuração do driver é frequentemente protegida por um lock `sx(9)`, que pode dormir. Um callout ou interrupção não pode adquirir um lock que pode dormir diretamente. Se uma decisão orientada por timer implica uma mudança de configuração, o timer enfileira uma task; a task adquire o lock sx e realiza a mudança.

**Repetindo uma operação falha após um backoff.** Operações de hardware às vezes falham de forma transitória. A resposta sensata é esperar algum intervalo e tentar novamente. Um handler de interrupção não pode bloquear; ele enfileira uma `timeout_task` com um atraso igual ao intervalo de backoff. A timeout task dispara mais tarde em contexto de thread, tenta novamente a operação e, se falhar novamente, reagenda a si mesma com um atraso mais longo.

**Registrar um evento não trivial.** O `printf(9)` do kernel é surpreendentemente tolerante com contextos estranhos, mas `log(9)` e seus semelhantes não são. Um driver que quer emitir um diagnóstico de múltiplas linhas a partir de um contexto de interrupção escreve o mínimo necessário no handler (uma flag, um incremento de contador) e agenda uma task para fazer o registro real mais tarde.

**Drenando ou reconfigurando uma longa fila de hardware.** Um driver de rede que detecta bloqueio de cabeça de fila pode querer percorrer seu anel de transmissão, liberar descritores concluídos e resetar o estado por descritor. O trabalho é limitado, mas não trivial. Fazê-lo inline no caminho de interrupção monopoliza uma CPU em um contexto ruim. Fazê-lo em uma task deixa a interrupção retornar imediatamente e o trabalho real acontecer em uma thread.

**Teardown diferido.** Quando um driver faz o detach enquanto algum objeto ainda tem referências pendentes, o driver não pode liberar o objeto imediatamente. Um padrão comum: diferir a liberação para uma task que dispara após o contador de referências ser sabidamente zero, ou após um período de carência longo o suficiente para que quaisquer referências em andamento sejam drenadas.

Todos esses casos compartilham a estrutura: o contexto restrito detecta uma necessidade, possivelmente registra uma pequena quantidade de estado e enfileira uma task. A task roda mais tarde em contexto de thread, faz o trabalho real e opcionalmente se reenfileira ou agenda um follow-up.

### Polling Versus Execução Diferida

Uma pergunta razoável neste momento: se o contexto restrito não consegue realizar o trabalho, por que não organizar para que ele aconteça em algum lugar completamente fora desse contexto restrito? Por que não ter uma única thread de kernel dedicada que faz polling em busca de "algo a fazer" e acorda quando detecta um estado que requer ação?

É isso, em essência, o que um taskqueue faz. A thread do taskqueue dorme até que haja trabalho disponível e acorda para processá-lo. A diferença entre um taskqueue e uma thread de polling feita à mão é que o framework do taskqueue cuida de toda a logística para você. O enfileiramento é uma única operação atômica. A estrutura da tarefa armazena o callback e o contexto diretamente, então você não precisa projetar um tipo de "entrada de fila de trabalho". A coalescência de enfileiramentos redundantes é automática. Drenar uma tarefa é uma única chamada. A finalização é uma única chamada. A observabilidade pelas ferramentas padrão vem sem nenhum esforço adicional.

Uma thread de polling feita à mão pode realizar o mesmo trabalho, e em casos extremos é a escolha certa (se, por exemplo, o trabalho tem restrições de tempo real rígidas, ou se faz parte de um subsistema que precisa de uma prioridade dedicada). Para o trabalho comum de drivers, contornar `taskqueue(9)` é quase sempre um erro.

Uma pergunta separada, mas relacionada: por que não simplesmente criar uma nova thread de kernel para cada operação adiada? Isso é extremamente custoso: criar uma thread leva tempo, aloca uma pilha de kernel completa e entrega a nova thread ao escalonador. Para trabalho que acontece repetidamente, o design sensato é reutilizar uma thread, que é exatamente o que um taskqueue fornece. Para trabalho que acontece apenas uma vez, você pode usar `kproc_create(9)` e fazer a nova thread encerrar quando terminar, mas mesmo assim um taskqueue com `taskqueue_drain` costuma ser mais simples e quase tão eficiente.

### As Soluções do FreeBSD

O kernel oferece uma pequena família de recursos para trabalho diferido. O Capítulo 14 se concentra em um deles (`taskqueue(9)`) e menciona os demais no nível de detalhe adequado para que um desenvolvedor de drivers saiba quando cada um é apropriado. Um breve panorama aqui; as seções seguintes aprofundam cada um à medida que ele se torna relevante.

**`taskqueue(9)`.** Uma fila de entradas `struct task` e uma ou mais threads do kernel (ou um contexto de interrupção de software) que consomem a fila. A escolha dominante para trabalho diferido em drivers. Abordado em profundidade ao longo deste capítulo.

**`epoch(9)`.** Um mecanismo de sincronização de leitura sem locks que drivers de rede usam para permitir que leitores percorram estruturas de dados compartilhadas sem locks. Escritores adiam a limpeza via `epoch_call` ou `epoch_wait`. Não é um mecanismo geral de trabalho diferido para drivers arbitrários, mas é importante o suficiente para ser apresentado mais adiante neste capítulo, para que você o reconheça ao encontrá-lo em código de driver de rede.

**Grouptaskqueues.** Uma variação escalável dos taskqueues em que um grupo de tarefas relacionadas compartilha um pool de threads de trabalho por CPU. Drivers de rede fazem uso intenso disso; a maioria dos demais drivers não. Apresentado mais adiante neste capítulo.

**`kproc_create(9)` / `kthread_add(9)`.** Criação direta de uma thread do kernel. Útil quando o trabalho diferido é um loop de longa duração que não se encaixa no formato de "tarefa curta", e quando o trabalho merece uma prioridade dedicada ou afinidade de CPU. Quase sempre é exagero para adiamentos simples; um taskqueue é preferível.

**Handlers dedicados de SWI (software interrupt) via `swi_add(9)`.** Uma forma de registrar uma função que executa em um contexto de interrupção de software. Os taskqueues do sistema (`taskqueue_swi`, `taskqueue_swi_giant`, `taskqueue_fast`) são construídos sobre esse mecanismo. Código de driver raramente chama `swi_add` diretamente; a camada de taskqueue é a abstração correta.

**O próprio callout, reagendado para "zero a partir de agora".** Um padrão que não funciona: você não consegue "escapar" do contexto de callout agendando outro callout, porque o próximo callout ainda executa em contexto de callout. Reconhecer que isso é um beco sem saída já é útil por si só. Callouts agendam o momento; taskqueues fornecem o contexto.

No restante do Capítulo 14, salvo indicação em contrário, "adiar para uma tarefa" ou "enfileirar uma tarefa" significa "enfileirar um `struct task` em um `struct taskqueue`".

### Quando o Adiamento É a Resposta Errada

O adiamento é uma ferramenta, não um padrão. Várias situações se beneficiam de realizar o trabalho no lugar em vez de adiá-lo.

**O trabalho é genuinamente curto e seguro para o contexto atual.** Registrar uma estatística de uma linha com `device_printf(9)` a partir de um callout é perfeitamente aceitável. Também o é incrementar um contador. Ou sinalizar um cv. Adiar essas trivialidades para uma tarefa custa mais do que simplesmente executá-las. Adie apenas quando o trabalho realmente não pertence ao contexto atual.

**A temporização importa e o adiamento introduz variância.** Uma tarefa não executa imediatamente. Ela executa quando a thread do taskqueue for escalonada novamente, o que pode ser microssegundos ou milissegundos adiante, dependendo da carga do sistema. Se o trabalho tem requisitos rígidos de temporização (como reconhecer um evento de hardware dentro de um prazo, por exemplo), o adiamento pode fazer com que o prazo seja perdido. Para esse tipo de trabalho você precisa de um mecanismo mais rápido (uma conclusão em nível de hardware, um callout com `C_DIRECT_EXEC`, ou um SWI) ou de um design diferente.

**O adiamento acrescentaria uma etapa extra sem nenhum benefício.** Se o único trabalho do seu handler de interrupção já é seguro para fazer em contexto de interrupção, adicionar uma ida e volta pela tarefa dobra a latência sem melhorar nada. Adie apenas as partes do trabalho que precisam ser adiadas.

**O trabalho requer uma thread específica.** Se o trabalho precisa executar como um processo de usuário específico (por exemplo, para usar a tabela de descritores de arquivo daquele processo), uma thread genérica de taskqueue é o lugar errado. Essa situação é rara em drivers, mas existe.

Para todo o resto, o adiamento via taskqueue é a resposta correta, e o restante do capítulo trata de como fazê-lo bem.

### Um Exemplo Prático: Por Que a Fonte de Tick Não Consegue Acordar Observadores

Um exemplo concreto do driver do Capítulo 13, que vale a pena examinar com atenção porque é o primeiro ponto em que o Capítulo 14 realiza uma mudança real.

O callback `tick_source` do Capítulo 13, de `stage4-final/myfirst.c`, tem a seguinte aparência:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        /* selwakeup omitted: cannot be called from a
                         * callout callback while sc->mtx is held. */
                }
        }
        ...
}
```

Aquele comentário ao final não é hipotético. `selwakeup(9)` adquire o mutex por selinfo e pode chamar o subsistema kqueue, o que não é seguro fazer dentro de um callback de callout que já mantém um mutex diferente do driver. Um programa de usuário usando `select(2)`/`poll(2)` e aguardando legibilidade em `/dev/myfirst` portanto não é notificado quando a fonte de tick deposita um byte. O programa só acorda quando algum outro caminho chama `selwakeup`, por exemplo quando o `write(2)` de outra thread chega.

Esse é um bug real no driver do Capítulo 13. Deixamos sem correção no Capítulo 13 porque corrigi-lo exigia um primitivo que ainda não havíamos apresentado. O Capítulo 14 apresenta esse primitivo e corrige o bug.

A correção é pequena. Adicione um `struct task` ao softc. Inicialize-o no attach. Em vez de omitir `selwakeup` do callback tick_source, enfileire a tarefa; a tarefa executa em contexto de thread, sem nenhum mutex do driver mantido, e chama `selwakeup` com segurança. Drene a tarefa no detach após `is_attached` ter sido limpo, antes de liberar o selinfo.

Percorreremos cada passo dessa mudança na Seção 3. Por ora, o ponto é que a mudança é mecânica e sua necessidade não é forçada. A primeira tarefa real do Capítulo 14 é fornecer a você a ferramenta que esse tipo de bug necessita.

### Um Pequeno Modelo Mental

Uma imagem útil, apresentada aqui uma única vez e referenciada mais adiante.

Pense no seu driver como sendo composto de dois tipos de código. O primeiro tipo é o código que executa porque alguém o solicitou: o handler de `read(2)`, o handler de `write(2)`, o handler de `ioctl(2)`, os handlers de sysctl, os handlers de abertura e fechamento. Esse código executa em contexto de thread, com regras comuns, e pode dormir, alocar memória e acessar qualquer lock. Chame-o de "código de contexto de thread".

O segundo tipo é o código que executa porque o tempo ou o hardware o determinou: um callback de callout, um filtro de interrupção, uma leitura protegida por epoch. Esse código executa em um contexto restrito, com um conjunto mais estreito de regras, e deve manter seu trabalho curto e sem dormir. Chame-o de "código de contexto de borda".

A maior parte do trabalho real pertence ao código de contexto de thread. A maior parte do que o código de contexto de borda realmente precisa fazer é: detectar o evento, registrar uma pequena quantidade de estado e passar o trabalho para o código de contexto de thread. Um taskqueue é essa transferência. O callback da tarefa executa como código de contexto de thread, porque a thread do taskqueue está em contexto de thread. Tudo o que o callback faz segue as regras comuns.

Esse modelo mental permite que você leia cada seção subsequente como variações de uma única ideia: o código de contexto de borda detecta, o código de contexto de thread age, e o taskqueue é a costura entre eles. Uma vez que você veja o driver dessa forma, o restante do capítulo é detalhe de engenharia.

### Encerrando a Seção 1

Algum trabalho deve executar em um contexto restrito (callouts, filtros de interrupção, seções de epoch). As regras desses contextos proíbem dormir, alocações pesadas, aquisição de locks que dormem, e várias outras operações comuns. Drivers com responsabilidades reais frequentemente precisam realizar exatamente essas operações em resposta a eventos que chegam em contextos restritos. O remédio é enfileirar uma tarefa e deixar uma thread de trabalho realizar o trabalho real em um contexto onde as regras o permitem.

O kernel expõe esse padrão como `taskqueue(9)`. A API é pequena, os idiomas são regulares, e a ferramenta se compõe de forma limpa com os callouts e os primitivos de sincronização que você já conhece. A Seção 2 apresenta o primitivo.



## Seção 2: Introdução ao `taskqueue(9)`

`taskqueue(9)` é, como a maioria dos subsistemas maduros do kernel, uma pequena API sobre uma implementação cuidadosa. As estruturas de dados são compactas, o ciclo de vida é regular (init, enqueue, run, drain, free), e as regras são explícitas o suficiente para que você possa verificar o seu uso lendo o código-fonte. Esta seção percorre as estruturas, nomeia a API, lista as filas predefinidas que o kernel fornece gratuitamente, e compara taskqueues com os callouts do Capítulo 13 para que você veja quando cada um é a ferramenta certa.

### A Estrutura de Tarefa

A estrutura de dados está em `/usr/src/sys/sys/_task.h`:

```c
typedef void task_fn_t(void *context, int pending);

struct task {
        STAILQ_ENTRY(task) ta_link;     /* (q) link for queue */
        uint16_t ta_pending;            /* (q) count times queued */
        uint8_t  ta_priority;           /* (c) Priority */
        uint8_t  ta_flags;              /* (c) Flags */
        task_fn_t *ta_func;             /* (c) task handler */
        void    *ta_context;            /* (c) argument for handler */
};
```

Os campos se dividem em dois grupos. Os campos `(q)` são gerenciados pelo taskqueue sob seu próprio lock interno; o código do driver não os toca diretamente. Os campos `(c)` são constantes após a inicialização; o código do driver os define uma única vez por meio de um inicializador e jamais os modifica novamente.

`ta_link` é a ligação de lista usada quando a tarefa está enfileirada. Não é utilizado quando a tarefa está ociosa.

`ta_pending` é o contador de coalescência. Quando a tarefa é enfileirada pela primeira vez, ele vai de zero a um e a tarefa é colocada na lista. Se ela for enfileirada novamente antes de o callback executar, o contador simplesmente incrementa e a tarefa permanece na lista uma única vez. Quando o callback finalmente executa, a contagem final de pendências é passada como segundo argumento para o callback, e o contador é zerado. O pior erro que você pode cometer com `ta_pending` é assumir que uma tarefa executará N vezes se você a enfileirar N vezes; ela não executará. Ela executará uma vez e o callback saberá que foi enfileirada N vezes. A Seção 5 aborda as implicações de design em detalhes.

`ta_priority` ordena as tarefas dentro de uma única fila. Tarefas de prioridade mais alta executam antes de tarefas de prioridade mais baixa. Para a maioria dos drivers, o valor é zero (prioridade comum) e a fila é efetivamente FIFO.

`ta_flags` é um pequeno bitfield. O kernel o usa para registrar se a tarefa está atualmente enfileirada e, para tarefas de rede, se a tarefa deve executar dentro do epoch de rede. O código do driver não o toca após `TASK_INIT` ou `NET_TASK_INIT` tê-lo definido.

`ta_func` é a função de callback. Sua assinatura é `void (*)(void *context, int pending)`. O primeiro argumento é o que você armazenou em `ta_context` no momento da inicialização; o segundo é a contagem de coalescência.

`ta_context` é o argumento do callback. Para uma tarefa de driver de dispositivo, este é quase sempre o ponteiro para o softc.

A estrutura tem 32 bytes em amd64, mais ou menos padding. Você incorpora uma por padrão de trabalho diferido em seu softc. Um driver com três caminhos de adiamento tem três membros `struct task`.

### Inicializando uma Tarefa

O macro canônico é `TASK_INIT`, em `/usr/src/sys/sys/taskqueue.h`:

```c
#define TASK_INIT_FLAGS(task, priority, func, context, flags) do {      \
        (task)->ta_pending = 0;                                         \
        (task)->ta_priority = (priority);                               \
        (task)->ta_flags = (flags);                                     \
        (task)->ta_func = (func);                                       \
        (task)->ta_context = (context);                                 \
} while (0)

#define TASK_INIT(t, p, f, c)    TASK_INIT_FLAGS(t, p, f, c, 0)
```

Uma chamada típica da rotina attach de um driver tem a seguinte aparência:

```c
TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);
```

Os argumentos se leem como: "inicialize esta tarefa, com prioridade comum zero, para executar `myfirst_selwake_task(sc, pending)` quando ela disparar". Esse é o ritual completo de inicialização. Não há uma chamada "destroy" correspondente; uma tarefa volta a ficar ociosa quando seu callback termina, e sai de escopo quando o softc ao qual pertence é liberado.

Para tarefas no caminho de rede há uma variante, `NET_TASK_INIT`, que define o flag `TASK_NETWORK` para que o taskqueue saiba que deve executar o callback dentro do epoch `net_epoch_preempt`:

```c
#define NET_TASK_INIT(t, p, f, c) TASK_INIT_FLAGS(t, p, f, c, TASK_NETWORK)
```

A menos que você esteja escrevendo um driver de rede, `TASK_INIT` é o que você usará. O Capítulo 14 usa `TASK_INIT` ao longo de toda a sua extensão e retorna a `NET_TASK_INIT` apenas na seção "Tópicos adicionais".

### A Estrutura do Taskqueue, do Ponto de Vista do Driver

Do ponto de vista de um driver, um taskqueue é um `struct taskqueue *`. O ponteiro é um global predefinido (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_bus`, etc.) ou um que o driver criou com `taskqueue_create` e armazenou em seu softc. Em ambos os casos, o ponteiro é opaco. Todas as interações acontecem por meio de chamadas de API. O único detalhe interno que nos importa neste capítulo é o fato de que o taskqueue mantém seu próprio lock, que adquire ao enfileirar e quando sua thread de trabalho retira tarefas da lista.

Para fins de completude, a definição (de `/usr/src/sys/kern/subr_taskqueue.c`):

```c
struct taskqueue {
        STAILQ_HEAD(, task)     tq_queue;
        LIST_HEAD(, taskqueue_busy) tq_active;
        struct task            *tq_hint;
        u_int                   tq_seq;
        int                     tq_callouts;
        struct mtx_padalign     tq_mutex;
        taskqueue_enqueue_fn    tq_enqueue;
        void                   *tq_context;
        char                   *tq_name;
        struct thread         **tq_threads;
        int                     tq_tcount;
        int                     tq_spin;
        int                     tq_flags;
        ...
};
```

`tq_queue` é a lista de tarefas pendentes. `tq_active` registra quais tarefas estão em execução no momento, informação que a lógica de drenagem utiliza para aguardar a conclusão de todas elas. `tq_mutex` é o lock próprio da taskqueue. `tq_threads` é o array de threads de trabalho, de tamanho `tq_tcount`. `tq_spin` registra se o mutex é um spin mutex (para taskqueues criadas com `taskqueue_create_fast`) ou um sleep mutex (para taskqueues criadas com `taskqueue_create`). `tq_flags` registra o estado de desligamento.

Você não toca em nenhum desses campos a partir do código do driver. Eles são apresentados aqui uma única vez para que as chamadas de API no restante da seção tenham um referente concreto. O restante do capítulo trata a taskqueue como opaca.

### A API, Função por Função

As funções públicas são declaradas em `/usr/src/sys/sys/taskqueue.h`. Um driver tipicamente usa menos de uma dúzia delas. Vamos percorrer as mais importantes agora, agrupadas por finalidade.

**Criando e destruindo um taskqueue.**

```c
struct taskqueue *taskqueue_create(const char *name, int mflags,
    taskqueue_enqueue_fn enqueue, void *context);

struct taskqueue *taskqueue_create_fast(const char *name, int mflags,
    taskqueue_enqueue_fn enqueue, void *context);

int taskqueue_start_threads(struct taskqueue **tqp, int count, int pri,
    const char *name, ...);

void taskqueue_free(struct taskqueue *queue);
```

`taskqueue_create` cria um taskqueue que usa um sleep mutex internamente. As tasks enfileiradas nele são executadas em um contexto onde dormir é permitido (supondo que sejam despachadas via `taskqueue_thread_enqueue` e `taskqueue_start_threads`). Essa é a escolha certa para quase todo taskqueue de driver.

`taskqueue_create_fast` cria um taskqueue que usa um spin mutex internamente. Necessário apenas se você pretende enfileirar a partir de um contexto onde um sleep mutex seria inadequado (por exemplo, de dentro de um spin mutex ou de uma interrupção de filtro). O código de driver raramente precisa disso; o `taskqueue_fast` predefinido existe para os casos em que isso é necessário.

O callback `enqueue` é chamado pela camada do taskqueue quando uma task é adicionada a uma fila até então vazia, e é a maneira pela qual essa camada "acorda" um consumidor. Para filas atendidas por threads do kernel, a função de enqueue é `taskqueue_thread_enqueue`, fornecida pelo kernel. Para filas atendidas por interrupções de software, o kernel fornece `taskqueue_swi_enqueue`. O código de driver quase sempre passa `taskqueue_thread_enqueue` aqui.

O argumento `context` é passado de volta para o callback de enqueue. Ao usar `taskqueue_thread_enqueue`, a convenção é passar `&your_taskqueue_pointer`, para que a função possa encontrar o taskqueue que está acordando. Os exemplos do Capítulo 14 seguem essa convenção literalmente.

`taskqueue_start_threads` cria `count` threads do kernel que executam o despachante `taskqueue_thread_loop`, cada uma dormindo na fila até que uma task chegue. O argumento `pri` é a prioridade da thread. `PWAIT` (definido em `/usr/src/sys/sys/priority.h`, numericamente 76) é a escolha comum para taskqueues de driver; drivers de rede frequentemente passam `PI_NET` (numericamente 4) para executar com prioridade próxima à de interrupção. As threads de trabalho do Capítulo 14 usam `PWAIT`.

`taskqueue_free` encerra o taskqueue. Drena todas as tasks pendentes e em execução, termina as threads de trabalho e libera o estado interno. Deve ser chamado sem tasks pendentes que ainda não tenham sido drenadas; após seu retorno, o `struct taskqueue *` é inválido e não deve ser usado.

**Inicializando uma task.** `TASK_INIT` conforme mostrado acima. Não há um "destroy" correspondente porque a estrutura da task pertence ao chamador.

**Enfileirando uma task.**

```c
int taskqueue_enqueue(struct taskqueue *queue, struct task *task);
int taskqueue_enqueue_flags(struct taskqueue *queue, struct task *task,
    int flags);
int taskqueue_enqueue_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, int ticks);
int taskqueue_enqueue_timeout_sbt(struct taskqueue *queue,
    struct timeout_task *timeout_task, sbintime_t sbt, sbintime_t pr,
    int flags);
```

`taskqueue_enqueue` é o cavalo de batalha. Ele encadeia a task na fila e acorda a thread de trabalho. Se a task já estiver pendente, ele incrementa `ta_pending` e retorna. Retorna zero em caso de sucesso; raramente falha.

`taskqueue_enqueue_flags` é o mesmo, com flags opcionais:

- `TASKQUEUE_FAIL_IF_PENDING` faz o enqueue retornar `EEXIST` em vez de mesclar se a task já estiver pendente.
- `TASKQUEUE_FAIL_IF_CANCELING` faz o enqueue retornar `EAGAIN` se a task estiver sendo cancelada no momento.

O `taskqueue_enqueue` padrão mescla silenciosamente; a variante com flags permite detectar a situação quando isso for relevante.

`taskqueue_enqueue_timeout` agenda uma `struct timeout_task` para disparar após um determinado número de ticks. Nos bastidores, ele usa um `callout` interno cujo callback enfileira a task subjacente no taskqueue quando o atraso expira. A variante `sbt` aceita sbintime para precisão abaixo de um tick.

**Cancelando uma task.**

```c
int taskqueue_cancel(struct taskqueue *queue, struct task *task,
    u_int *pendp);
int taskqueue_cancel_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, u_int *pendp);
```

`taskqueue_cancel` remove uma task pendente da fila caso ela ainda não tenha começado a executar, e escreve a contagem de pendências anterior em `*pendp` se esse ponteiro não for NULL. Se a task estiver sendo executada no momento, a função retorna `EBUSY` e não aguarda; você deve usar `taskqueue_drain` em seguida se precisar esperar.

`taskqueue_cancel_timeout` faz o mesmo para timeout tasks.

**Drenando uma task.**

```c
void taskqueue_drain(struct taskqueue *queue, struct task *task);
void taskqueue_drain_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task);
void taskqueue_drain_all(struct taskqueue *queue);
```

`taskqueue_drain(tq, task)` bloqueia até que a task especificada não esteja mais pendente nem em execução. Se a task estava pendente, a drenagem aguarda que ela execute e conclua. Se a task estava em execução, a drenagem aguarda o retorno da invocação atual. Se a task estava ociosa, a drenagem retorna imediatamente. Esta é a chamada que você usa no detach para cada task que o seu driver possui.

`taskqueue_drain_timeout` faz o mesmo para timeout tasks.

`taskqueue_drain_all` drena todas as tasks e todas as timeout tasks do taskqueue. Útil quando você possui um taskqueue privado e quer ter certeza de que ele está completamente quieto antes de liberá-lo. O próprio `taskqueue_free` executa um trabalho equivalente internamente, portanto `taskqueue_drain_all` não é estritamente necessário antes de `taskqueue_free`, mas é útil quando você deseja silenciar um taskqueue sem destruí-lo.

**Bloqueando e desbloqueando.**

```c
void taskqueue_block(struct taskqueue *queue);
void taskqueue_unblock(struct taskqueue *queue);
void taskqueue_quiesce(struct taskqueue *queue);
```

`taskqueue_block` impede que a fila execute novas tasks. As tasks já em execução concluem; as tasks recém-enfileiradas se acumulam mas não são executadas até que `taskqueue_unblock` seja chamado. O par é útil para congelar temporariamente uma fila durante uma transição delicada sem destruí-la.

`taskqueue_quiesce` aguarda que a task atualmente em execução (se houver) conclua e que a fila esteja vazia de tasks pendentes. Equivalente a "drenar tudo, mas não destruir". Seguro de chamar com a fila em execução.

**Verificação de pertencimento.**

```c
int taskqueue_member(struct taskqueue *queue, struct thread *td);
```

Retorna verdadeiro se a thread fornecida for uma das threads de trabalho do taskqueue. Útil dentro de um callback de task quando você quer bifurcar em "estou executando no meu próprio taskqueue", embora o idioma mais comum seja usar `curthread` em relação a um ponteiro de thread armazenado.

Essa é toda a API que um driver usa normalmente. Existe um punhado de funções menos comuns (`taskqueue_set_callback` para hooks de init/shutdown, `taskqueue_poll_is_busy` para verificações no estilo de polling), mas a maioria dos drivers nunca as utiliza.

### Os Taskqueues Predefinidos

O kernel fornece um pequeno conjunto de taskqueues pré-configurados para drivers que não precisam de um privado. Eles são declarados em `/usr/src/sys/sys/taskqueue.h` com `TASKQUEUE_DECLARE`, que se expande para um ponteiro extern. Um driver os usa pelo nome:

```c
TASKQUEUE_DECLARE(thread);
TASKQUEUE_DECLARE(swi);
TASKQUEUE_DECLARE(swi_giant);
TASKQUEUE_DECLARE(fast);
TASKQUEUE_DECLARE(bus);
```

**`taskqueue_thread`** é a fila genérica de contexto de thread. Uma thread do kernel, prioridade `PWAIT`. O nome da thread aparece no `ps` como `thread taskq`. Seguro para qualquer task que queira um contexto de thread completo e não precise de propriedades especiais. A fila predefinida mais fácil de usar; uma primeira escolha bastante razoável se você não tiver certeza de qual fila precisa.

**`taskqueue_swi`** é despachado por um handler de interrupção de software, não por uma thread do kernel. As tasks nessa fila são executadas sem nenhum mutex de driver mantido, mas em contexto SWI, que ainda tem restrições (sem dormir). Útil para trabalho curto sem dormida que quer ser executado prontamente após um enqueue, sem a latência de escalonamento de acordar uma thread do kernel. O uso por drivers é incomum.

**`taskqueue_swi_giant`** é o mesmo que `taskqueue_swi`, mas executa com o lock histórico `Giant` mantido. Essencialmente nunca usado em código novo. Mencionado apenas por completude.

**`taskqueue_fast`** é uma fila de interrupção de software apoiada por spin mutex, usada para tasks que precisam ser enfileiráveis a partir de contextos onde um sleep mutex seria inadequado (por exemplo, de dentro de outro spin mutex). O próprio taskqueue usa um spin mutex para sua lista interna, portanto o enqueue é permitido em qualquer contexto. O callback da task, no entanto, é executado em contexto SWI, que ainda tem a restrição de não dormir. O uso por drivers é raro; contextos de interrupção de filtro que precisam enfileirar trabalho tipicamente usam `taskqueue_fast` ou, mais comumente hoje em dia, uma fila `taskqueue_create_fast` privada.

**`taskqueue_bus`** é uma fila dedicada para eventos de dispositivo do `newbus(9)` (inserção hot-plug, remoção, notificações de barramento filho). Drivers comuns não enfileiram nessa fila.

Para um driver como `myfirst`, as escolhas realistas são `taskqueue_thread` (a fila compartilhada) ou um taskqueue privado que você possui e desmonta no detach. A Seção 4 discute a escolha entre um e outro; o Estágio 1 da refatoração usa `taskqueue_thread` pela simplicidade e o Estágio 2 migra para uma fila privada.

### Comparando Taskqueues e Callouts

Uma comparação lado a lado, porque um leitor novo faz essa pergunta primeiro.

| Propriedade | `callout(9)` | `taskqueue(9)` |
|---|---|---|
| Dispara em | Um momento específico | Assim que uma thread de trabalho o pegar |
| Contexto do callback | Thread de callout (padrão) ou IRQ de hardclock (`C_DIRECT_EXEC`) | Thread do kernel (para `taskqueue_thread`, filas privadas) ou SWI (para `taskqueue_swi`, `taskqueue_fast`) |
| Pode dormir | Não | Sim, para filas apoiadas por thread; não, para filas apoiadas por SWI |
| Pode adquirir locks dormíveis | Não | Sim, para filas apoiadas por thread |
| Pode chamar `uiomove`, `copyin`, `copyout` | Não | Sim, para filas apoiadas por thread |
| Mescla envios redundantes | Não, cada reset substitui o prazo anterior | Sim, `ta_pending` é incrementado |
| Cancelável antes de disparar | `callout_stop(co)` | `taskqueue_cancel(tq, task, &pendp)` |
| Aguarda callback em andamento | `callout_drain(co)` | `taskqueue_drain(tq, task)` |
| Periódico | O callback se reagenda | Não; enfileirar novamente de outro lugar, ou usar um callout para enfileirar |
| Agendado para o futuro | `callout_reset(co, ticks, ...)` | `taskqueue_enqueue_timeout(tq, tt, ticks)` |
| Custo por disparo | Microssegundos | Microssegundos mais o despertar da thread (pode ser maior sob carga) |

A tabela ilustra a divisão. Um callout é o primitivo correto quando você precisa disparar em um momento específico e o trabalho é seguro para contexto de callout. Um taskqueue é o primitivo correto quando você precisa de trabalho em contexto de thread e está disposto a aceitar a latência de escalonamento que o taskqueue introduz. Muitos drivers usam os dois juntos: um callout dispara no prazo, o callout enfileira uma task, a task faz o trabalho real em contexto de thread.

### Comparando Taskqueues e uma Thread Privada do Kernel

Outra comparação que o capítulo lhe deve, porque um leitor que pergunta "por que não simplesmente criar uma thread do kernel" merece uma resposta direta.

Uma thread do kernel criada com `kproc_create(9)` é uma entidade escalonada completa: sua própria pilha (tipicamente 16 KB em amd64), sua própria prioridade, sua própria entrada `proc`, seu próprio estado. Um driver que quer executar um loop "a cada segundo, faça X" poderia criar tal thread e fazê-la percorrer o loop com `kproc_kthread_add` mais `cv_timedwait`. O código funciona, mas custa mais do que o trabalho normalmente merece. Um taskqueue com uma thread que fica ociosa na maior parte do tempo e acorda no enqueue é mais barato por item de trabalho pendente e mais fácil de desmontar.

Existem casos legítimos para `kproc_create`. Um subsistema de longa duração com seu próprio ajuste (prioridade, afinidade de CPU, grupo de processos) é um deles. Um trabalho periódico que genuinamente precisa de uma thread própria para observabilidade é outro. O padrão de trabalho diferido de um driver quase nunca é. Use um taskqueue até que um requisito específico o force a fazer algo diferente.

### A Regra do Enqueue com Task Já Pendente

Uma regra que vale a pena destacar cedo, pois é a fonte mais comum de surpresas para quem está começando com a API: uma task não pode estar pendente duas vezes. Se você chamar `taskqueue_enqueue(tq, &sc->t)` enquanto `sc->t` já estiver pendente, o kernel incrementa `sc->t.ta_pending` e retorna sucesso sem encadear a task uma segunda vez.

Isso tem duas implicações. Primeiro, seu callback será executado uma vez, não duas, mesmo que você tenha enfileirado duas vezes. Segundo, o argumento `pending` que o callback recebe é o número de vezes que a task foi enfileirada antes de o callback ser despachado; seu callback pode usar essa contagem para processar em lote o trabalho acumulado.

Se você quer executar o callback N vezes para N enqueues, uma única task é o modelo errado. Use N tasks separadas, ou enfileirar um sentinela em uma fila de propriedade do driver e processar cada sentinela no callback. Quase sempre o comportamento de mesclagem é o que você deseja; a Seção 5 mostra como explorá-lo deliberadamente.

### Um Exemplo Mínimo, do Início ao Fim

Uma task hello-world, para ser concreto. Se você digitar isso em um módulo de rascunho e carregá-lo, verá a linha de `device_printf` no `dmesg`:

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

static struct task example_task;

static void
example_task_fn(void *context, int pending)
{
        printf("example_task_fn: pending=%d\n", pending);
}

static int
example_modevent(module_t m, int event, void *arg)
{
        int error = 0;

        switch (event) {
        case MOD_LOAD:
                TASK_INIT(&example_task, 0, example_task_fn, NULL);
                taskqueue_enqueue(taskqueue_thread, &example_task);
                break;
        case MOD_UNLOAD:
                taskqueue_drain(taskqueue_thread, &example_task);
                break;
        default:
                error = EOPNOTSUPP;
                break;
        }
        return (error);
}

static moduledata_t example_mod = {
        "example_task", example_modevent, NULL
};
DECLARE_MODULE(example_task, example_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(example_task, 1);
```

O módulo faz cinco coisas, cada uma em uma única linha. No carregamento, `TASK_INIT` prepara a estrutura da task. `taskqueue_enqueue` pede ao `taskqueue_thread` compartilhado que execute o callback. O callback imprime uma mensagem. No descarregamento, `taskqueue_drain` aguarda o callback terminar, caso ainda não tenha terminado. O ciclo de vida completo é compacto.

Se você digitar esse código e carregar o módulo, o `dmesg` exibirá:

```text
example_task_fn: pending=1
```

O `pending=1` reflete o fato de que a task foi enfileirada uma vez antes de o callback disparar.

Agora experimente uma demonstração de coalescing: altere o trecho de `MOD_LOAD` para enfileirar a task cinco vezes seguidas e adicione uma pequena espera para que a thread do taskqueue tenha chance de acordar:

```c
for (int i = 0; i < 5; i++)
        taskqueue_enqueue(taskqueue_thread, &example_task);
pause("example", hz / 10);
```

Execute novamente e o `dmesg` mostrará:

```text
example_task_fn: pending=5
```

Uma única invocação, com pending de cinco. Essa é a regra de coalescing em ação.

Isso é suficiente para dar forma ao que os refatoramentos trabalhados nas próximas seções significam. O restante deste capítulo escala a mesma estrutura para o driver `myfirst` real, substitui o módulo de rascunho por quatro estágios de integração, adiciona o teardown, adiciona um taskqueue privado e percorre a história de depuração.

### Encerrando a Seção 2

Uma `struct task` guarda um callback e seu contexto. Uma `struct taskqueue` gerencia uma fila dessas tasks e uma ou mais threads (ou um contexto SWI) que as consomem. A API é pequena: criar, iniciar threads, enfileirar (opcionalmente com atraso), cancelar, drinar, liberar, bloquear, desbloquear, quiescer. O kernel disponibiliza um punhado de filas predefinidas que qualquer driver pode usar sem precisar criar a sua própria. A regra de enfileiramento quando já há uma task pendente colapsa submissões redundantes em uma única invocação cujo pending count é o total acumulado.

A Seção 3 leva essas ferramentas para o driver `myfirst` e deposita a primeira task no código, sob o callout `tick_source` que silenciosamente vinha pulando o `selwakeup`. A correção é pequena; o modelo mental é a parte importante.



## Seção 3: Deferindo Trabalho a partir de um Timer ou Interrupção Simulada

O Capítulo 13 deixou o driver `myfirst` com três callouts que respeitavam o contrato do callout de forma estrita. Nenhum deles tentava fazer algo que não pertence a um callback de callout. O callback `tick_source` em particular omitia a chamada a `selwakeup` que um driver real precisaria fazer quando novos bytes aparecem no buffer, e o arquivo até carregava um comentário dizendo isso. O Capítulo 14 remove essa omissão.

A Seção 3 é o primeiro refatoramento guiado. Ela introduz o Estágio 1 do driver do Capítulo 14: o driver ganha uma `struct task`, um callback de task, um enfileiramento a partir de `tick_source` e um drain no detach. O trabalho com taskqueue privada é deixado para a Seção 4; no Estágio 1 usamos o `taskqueue_thread` compartilhado. Usar a fila compartilhada primeiro mantém o primeiro passo pequeno e isola a mudança no padrão de deferred work em si.

### A Mudança em Uma Frase

Quando `tick_source` acabou de depositar um byte no buffer circular, em vez de omitir silenciosamente o `selwakeup`, enfileire uma task cujo callback execute `selwakeup` em contexto de thread.

Essa é toda a mudança. Todo o resto é a configuração ao redor.

### A Adição ao Softc

Adicione dois membros à `struct myfirst_softc`:

```c
struct task             selwake_task;
int                     selwake_pending_drops;
```

O `selwake_task` é a task que vamos enfileirar. O `selwake_pending_drops` é um contador de debug que incrementamos sempre que a task coalesce dois ou mais enfileiramentos em uma única disparo; a diferença entre "número de chamadas a enqueue" e "número de invocações do callback" nos diz com que frequência o tick source produziu dados mais rápido do que a thread do taskqueue os consumiu. Isso é puramente diagnóstico; você pode omiti-lo se preferir, mas ver uma contagem real de coalescing em ação é valioso.

Adicione um sysctl somente leitura para que possamos observar o contador a partir do espaço do usuário sem um build de debug:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "selwake_pending_drops", CTLFLAG_RD,
    &sc->selwake_pending_drops, 0,
    "Times selwake_task coalesced two or more enqueues into one firing");
```

O posicionamento importa apenas no sentido de que deve vir depois de `sc->sysctl_tree` ter sido criado e antes de a função retornar sucesso; a sequência de attach do Capítulo 13 já tem a estrutura correta, portanto a adição encaixa naturalmente ao lado das outras estatísticas.

### O Callback da Task

Adicione uma função:

```c
static void
myfirst_selwake_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (pending > 1) {
                MYFIRST_LOCK(sc);
                sc->selwake_pending_drops++;
                MYFIRST_UNLOCK(sc);
        }

        /*
         * No driver mutex held. Safe to call selwakeup(9) here.
         */
        selwakeup(&sc->rsel);
}
```

Há várias coisas a observar.

O callback recebe o softc via ponteiro `arg`, exatamente como os callbacks de callout fazem. Ele não precisa de `MYFIRST_ASSERT` no início porque o callback de task não é executado com nenhum lock do driver mantido; o framework do taskqueue não segura seu lock por você. Isso é diferente do padrão lock-aware de callout do Capítulo 13, e vale a pena pausar para refletir. Um callout inicializado com `callout_init_mtx(&co, &sc->mtx, 0)` é executado com `sc->mtx` mantido. Uma task nunca é. Dentro do callback de task, se você quiser tocar estado que o mutex protege, você adquire o mutex, faz o trabalho, libera o mutex e continua.

O callback atualiza condicionalmente `selwake_pending_drops` sob o mutex. A condição `pending > 1` significa "este callback está tratando pelo menos dois enfileiramentos coalesced". Incrementar um contador sob o mutex é rápido e seguro; fazer isso incondicionalmente faria o caso comum (pending == 1, sem coalescing) pagar o custo do lock desnecessariamente.

A própria chamada a `selwakeup(&sc->rsel)` é a razão pela qual estamos aqui. Ela é executada sem nenhum lock do driver mantido, que é o que `selwakeup` quer, e é executada em contexto de thread, que é o que `selwakeup` exige. O bug do Capítulo 13 está corrigido.

O callback não verifica `is_attached`. Ele não precisa. O caminho de detach drena a task antes de liberar o selinfo; quando `is_attached` chegaria a ser zero, o callback da task tem garantia de não estar em execução, e `selwakeup` verá estado válido. A ordenação do drain é o que torna a omissão segura, e é por isso que discutimos a ordenação tão cuidadosamente na Seção 4.

### A Edição em `tick_source`

Mude o callback `tick_source` de:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        /* selwakeup omitted: cannot be called from a
                         * callout callback while sc->mtx is held. */
                }
        }

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

Para:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;
        bool wake_sel = false;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        wake_sel = true;
                }
        }

        if (wake_sel)
                taskqueue_enqueue(taskqueue_thread, &sc->selwake_task);

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

Duas edições. Uma flag local `wake_sel` registra se um byte foi escrito; a chamada a `taskqueue_enqueue` acontece após o trabalho no cbuf. O comentário sobre "selwakeup omitido" torna-se obsoleto e é removido.

Por que usar uma flag em vez de chamar `taskqueue_enqueue` diretamente dentro do bloco `if (put > 0)`? Porque `taskqueue_enqueue` é seguro de chamar com `sc->mtx` mantido (ela adquire seu próprio mutex interno; não há problema de ordem de lock para o mutex interno do taskqueue porque ele não é ordenado em relação a `sc->mtx`), mas é boa higiene manter as seções com mutex mantido compactas e nomear o motivo do enfileiramento com uma variável local. A versão com a flag é mais fácil de ler e de estender caso estágios posteriores adicionem mais condições que devem disparar o wake.

`taskqueue_enqueue` é de fato seguro de chamar a partir de um callback de callout com `sc->mtx` mantido? Sim. O taskqueue usa seu próprio mutex interno (`tq_mutex`), completamente separado de `sc->mtx`; nenhuma ordem de lock é estabelecida entre eles, portanto o `WITNESS` não tem do que reclamar. Verificaremos isso no laboratório ao final desta seção. Para referência futura, a garantia relevante em `/usr/src/sys/kern/subr_taskqueue.c` é que `taskqueue_enqueue` adquire `TQ_LOCK(tq)` (um sleep mutex para `taskqueue_create`, um spin mutex para `taskqueue_create_fast`), executa a manipulação da lista e libera o lock. Sem sleeping, sem recursão no lock do chamador, sem dependência cruzada de locks.

### A Mudança no Attach

Em `myfirst_attach`, adicione uma linha após as inicializações de callout existentes:

```c
TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);
```

Coloque-a junto às chamadas de init dos callouts. O agrupamento conceitual ("aqui é onde preparamos os primitivos de deferred work do driver") torna o arquivo mais fácil de percorrer.

Inicialize `selwake_pending_drops` para zero no mesmo bloco onde outros contadores são zerados:

```c
sc->selwake_pending_drops = 0;
```

### A Mudança no Detach

Esta é a parte crítica do estágio. A sequência de detach do Capítulo 13, simplificada, é:

1. Recusar o detach se `active_fhs > 0`.
2. Limpar `is_attached`.
3. Fazer broadcast em `data_cv` e `room_cv`.
4. Drinar `rsel` e `wsel` via `seldrain`.
5. Drinar os três callouts.
6. Destruir dispositivos, liberar sysctls, destruir cbuf, liberar contadores, destruir cvs, destruir sx, destruir mtx.

O Estágio 1 do Capítulo 14 adiciona um passo: drinar `selwake_task` entre o drain dos callouts (passo 5) e as chamadas a `seldrain` (passo 4). Na verdade, a sutileza de ordenação é mais cuidadosa do que isso. Vamos pensar.

O callback de `selwake_task` chama `selwakeup(&sc->rsel)`. Se `sc->rsel` estiver sendo drenado concorrentemente, o callback pode gerar uma condição de corrida. A regra é: garantir que o callback da task não esteja em execução antes de chamar `seldrain`. Isso significa que `taskqueue_drain(taskqueue_thread, &sc->selwake_task)` deve acontecer antes de `seldrain(&sc->rsel)`.

Porém, a task ainda pode ser enfileirada por um callback de callout em voo até que os callouts sejam drenados. Se drinarmos a task primeiro e depois os callouts, um callout em voo poderia reenfileirar a task após tê-la drenado, e a task reenfileirada então tentaria executar após o `seldrain`.

A única ordenação segura é: drinar os callouts primeiro (o que garante que não haverá mais enfileiramentos), depois drinar a task (o que garante que o último enfileiramento foi concluído), depois chamar `seldrain`. Mas também é preciso limpar `is_attached` antes de drinar os callouts para que um callback em voo saia cedo em vez de se rearmar.

Juntando tudo, a ordenação do detach no Estágio 1 é:

1. Recusar o detach se `active_fhs > 0`.
2. Limpar `is_attached` (sob o mutex).
3. Fazer broadcast em `data_cv` e `room_cv` (liberar o mutex antes).
4. Drinar os três callouts (sem mutex mantido; `callout_drain` pode dormir).
5. Drinar `selwake_task` (sem mutex mantido; `taskqueue_drain` pode dormir).
6. Drinar `rsel` e `wsel` via `seldrain`.
7. Destruir dispositivos, liberar sysctls, destruir cbuf, liberar contadores, destruir cvs, destruir sx, destruir mtx.

Os passos 4 e 5 são a nova restrição de ordenação. Callouts primeiro, tasks segundo, sel terceiro. Violar essa ordem em um kernel de debug normalmente dispara uma asserção dentro de `seldrain`; em um kernel de produção é um use-after-free esperando para acontecer.

O código em `myfirst_detach` fica assim:

```c
/* Chapter 13: drain every callout. No lock held; safe to sleep. */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);

/* Chapter 14: drain every task. No lock held; safe to sleep. */
taskqueue_drain(taskqueue_thread, &sc->selwake_task);

seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Duas linhas de código mais um comentário. A ordenação é visível no código-fonte.

### O Makefile

Sem alteração. O `bsd.kmod.mk` busca os headers da API do taskqueue na árvore do sistema; nenhum arquivo fonte adicional é necessário para o Estágio 1.

### Compilando e Carregando

Neste ponto seu working copy deve ter:

- Os dois novos membros do softc (`selwake_task`, `selwake_pending_drops`).
- A função `myfirst_selwake_task`.
- A edição em `myfirst_tick_source`.
- A chamada a `TASK_INIT` e o zeramento do contador no attach.
- A chamada a `taskqueue_drain` no detach.
- O novo sysctl `selwake_pending_drops`.

Compile a partir do diretório do Estágio 1:

```text
# cd /path/to/examples/part-03/ch14-taskqueues-and-deferred-work/stage1-first-task
# make clean && make
```

Carregue:

```text
# kldload ./myfirst.ko
```

Verifique:

```text
# kldstat | grep myfirst
 7    1 0xffffffff82f30000    ... myfirst.ko
# sysctl dev.myfirst.0
dev.myfirst.0.stats.selwake_pending_drops: 0
...
```

### Observando a Correção

Para observar o Estágio 1 fazendo seu trabalho, inicie um waiter de `poll(2)` no dispositivo e faça o tick source gerar dados. Um poller simples está disponível em `examples/part-03/ch14-taskqueues-and-deferred-work/labs/poll_waiter.c`:

```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <err.h>

int
main(int argc, char **argv)
{
        int fd, n;
        struct pollfd pfd;
        char c;

        fd = open("/dev/myfirst", O_RDONLY);
        if (fd < 0)
                err(1, "open");
        pfd.fd = fd;
        pfd.events = POLLIN;

        for (;;) {
                n = poll(&pfd, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        err(1, "poll");
                }
                if (pfd.revents & POLLIN) {
                        n = read(fd, &c, 1);
                        if (n > 0)
                                write(STDOUT_FILENO, &c, 1);
                }
        }
}
```

Compile com `cc poll_waiter.c -o poll_waiter` (sem bibliotecas especiais). Execute-o em um terminal:

```text
# ./poll_waiter
```

Em um segundo terminal, habilite o tick source em um ritmo lento para que a saída seja fácil de acompanhar:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=500
```

O driver do Capítulo 13, sem a correção do Estágio 1, deixaria o `poll_waiter` travado. Os bytes do leitor se acumulariam no buffer, mas `poll(2)` nunca retornaria porque `selwakeup` nunca era chamado. Você não veria nada.

O driver do Estágio 1 chama `selwakeup`, via a task. Você deve ver caracteres `t` aparecendo no terminal do `poll_waiter` a cada meio segundo. Quando você encerrar o teste, o `poll_waiter` sairá limpo via `Ctrl-C`.

Agora acelere o tick source para estressar o taskqueue:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=1
```

Você deve ver um fluxo contínuo de caracteres `t`. Verifique o contador de coalescing:

```text
# sysctl dev.myfirst.0.stats.selwake_pending_drops
dev.myfirst.0.stats.selwake_pending_drops: <some number, growing slowly>
```

O número é a contagem de vezes que o callback da task tratou um pending count maior que um. Em uma máquina com pouca carga ele pode permanecer pequeno (a thread do taskqueue acorda rápido o suficiente para tratar cada enfileiramento individualmente). Sob contenção o número cresce, e você pode observar o comportamento de coalescing diretamente.

Se o contador permanecer em zero mesmo sob carga, a máquina é rápida o suficiente para que cada enfileiramento drene antes que o próximo chegue. Isso não é um bug; é um sinal de que o coalescing está presente mas não está sendo disparado. A Seção 5 introduz uma carga de trabalho deliberada que força o coalescing.

### Descarregando

Pare o tick source:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=0
```

Feche o `poll_waiter` com `Ctrl-C`. Descarregue:

```text
# kldunload myfirst
```

O descarregamento deve ser limpo. Se falhar com `EBUSY`, você ainda tem um descritor aberto em algum lugar; fechar e executar o próximo `kldunload` deve funcionar.

Se o descarregamento travar, algo no caminho de detach está bloqueado. A causa mais provável é que o `taskqueue_drain` está aguardando uma task que não consegue completar. Isso indicaria um bug, e a seção de depuração (Seção 7) mostra como identificá-lo. No fluxo normal, o descarregamento é concluído em milissegundos.

### O que Acabamos de Fazer

Um breve resumo antes de a Seção 4 escalar o trabalho.

A Etapa 1 adicionou uma task ao driver, a inicializou em attach, a enfileirou a partir de um callback de callout, a drenou em detach na ordem correta e observou o coalescimento em ação. A task é executada no `taskqueue_thread` compartilhado; ele divide essa fila com todos os outros drivers do sistema que também a utilizam. Para uma carga de trabalho com baixa taxa de eventos, isso é perfeitamente adequado. Para um driver que eventualmente realizará trabalho substancial em suas tasks, ou que queira isolar a latência de processamento das tasks do que mais o sistema estiver executando, um taskqueue privado é a resposta certa. A Seção 4 dá esse passo.

### Erros Comuns a Evitar

Uma lista curta de erros que iniciantes cometem ao escrever sua primeira task. Cada um já prejudicou drivers reais; cada um tem uma regra simples que o previne.

**Esquecer de drenar no detach.** Se você enfileira tasks mas não as drena, uma task em execução pode rodar após a softc ser liberada, e o kernel trava no callback da task com uma desreferência de memória já liberada. Sempre drene todas as tasks que o seu driver possui antes de liberar qualquer estado que a task acesse.

**Drenar na ordem errada em relação ao estado que a task usa.** A ordenação task-e-depois-sel discutida acima é um caso específico disso. A regra geral: drene todo produtor de enfileiramentos, depois drene a task, depois libere o estado que a task usa. Violar a ordem é uma condição de corrida, mesmo que ela seja rara.

**Assumir que a task roda imediatamente após o enfileiramento.** Ela não roda. A thread do taskqueue acorda com o enfileiramento e então o escalonador decide quando executá-la. Sob carga, isso pode levar milissegundos. Drivers que assumem latência zero quebram sob carga.

**Assumir que a task roda uma vez por enfileiramento.** Ela não roda. O coalescing funde submissões redundantes. Se você precisa da semântica "exatamente uma vez por evento", precisa de estado por evento dentro da softc (uma fila de itens de trabalho, por exemplo), não de uma task por evento.

**Adquirir um lock do driver na ordem errada no callback da task.** O callback da task é código de contexto de thread normal. Ele obedece à ordem de locks estabelecida pelo seu driver. Se a ordem do driver é `sc->mtx -> sc->cfg_sx`, o callback da task deve adquirir o mutex antes do sx. Violações dessa ordem são erros de `WITNESS` da mesma forma que seriam em qualquer outro lugar.

**Usar `taskqueue_enqueue` de dentro de um contexto de interrupção de filtro sem um fast taskqueue.** `taskqueue_enqueue(taskqueue_thread, ...)` adquire um sleep mutex no lock interno do taskqueue. Isso é ilegal em um contexto de interrupção de filtro. Interrupções de filtro precisam enfileirar em `taskqueue_fast` ou em uma fila criada com `taskqueue_create_fast`. Callbacks de callout não têm essa restrição porque rodam em contexto de thread; o problema é específico de interrupções de filtro. A Parte 4 revisita isso ao introduzir `bus_setup_intr`.

Cada um desses erros pode ser detectado por uma revisão de código, pelo `WITNESS`, ou por um teste de estresse bem escrito. Os dois primeiros em particular são o tipo de bug que parece inofensivo até o primeiro detach sob carga.

### Encerrando a Seção 3

O driver `myfirst` agora possui uma task. Ela é usada para mover o `selwakeup` para fora do callback do callout e colocá-lo em contexto de thread, corrigindo um bug real do Capítulo 13. A task é inicializada no attach, enfileirada a partir do callback `tick_source` e drenada no detach na ordem correta em relação aos callouts e à drenagem do selinfo.

O `taskqueue_thread` compartilhado foi o primeiro taskqueue usado porque já estava disponível. Para um driver que vai crescer com mais tasks e mais responsabilidades, um taskqueue privado oferece melhor isolamento e uma história de teardown mais limpa. A Seção 4 cria esse taskqueue privado.



## Seção 4: Configuração e Limpeza do Taskqueue

O Estágio 1 usou o `taskqueue_thread` compartilhado. Essa escolha manteve a primeira mudança pequena: uma task, um enfileiramento, uma drenagem e uma ordenação de detach a respeitar. O Estágio 2 cria um taskqueue privado, de propriedade do driver. A mudança é pequena em termos de código, mas traz um conjunto de propriedades que se tornam importantes à medida que o driver cresce.

Esta seção ensina o Estágio 2 do refactor, percorre a configuração e o teardown de um taskqueue privado, audita a ordenação do detach com cuidado e termina com um checklist de pré-produção que você pode reutilizar em todo driver com taskqueue que escrever.

### Por Que um Taskqueue Privado

Três razões para ter um taskqueue privado.

Primeiro, **isolamento**. A thread de um taskqueue privado executa apenas as tasks do seu driver. Se algum outro driver no sistema se comportar mal no `taskqueue_thread` (bloqueando por tempo demais no callback de sua task, por exemplo), as tasks do seu driver não são afetadas. Da mesma forma, se o seu driver se comportar mal, o problema fica contido.

Segundo, **observabilidade**. `procstat -t` e `ps ax` mostram cada thread de taskqueue com um nome distinto. Uma fila privada é fácil de identificar: ela aparece com o nome que você lhe deu (`myfirst taskq`, por convenção). O `taskqueue_thread` compartilhado aparece apenas como `thread taskq`, o que é compartilhado com todos os outros drivers.

Terceiro, **o teardown é autocontido**. Quando você faz o detach, drena e libera o seu próprio taskqueue. Você não precisa raciocinar sobre se algum outro driver tem uma task pendente pela qual sua drenagem possa ter de esperar. (Na prática você não esperaria pela task de outro driver na fila compartilhada, mas o modelo mental de "somos donos do nosso teardown" é mais fácil de raciocinar.)

O custo é pequeno. Um taskqueue e uma thread do kernel, criados no attach e destruídos no detach. Algumas páginas de memória e algumas entradas no escalonador. Nada mensurável em qualquer sistema realista.

Para um driver que eventualmente terá múltiplas tasks, um taskqueue privado é o padrão correto. Para um driver com uma única task trivial em um caminho de código raro, a fila compartilhada é suficiente. O `myfirst` se enquadra no primeiro caso: já temos uma task, e o capítulo adicionará mais.

### A Adição à Softc

Adicione um membro a `struct myfirst_softc`:

```c
struct taskqueue       *tq;
```

Nenhuma outra alteração na softc para o Estágio 2.

### Criando o Taskqueue no Attach

Em `myfirst_attach`, entre as inicializações de mutex, cv e sx e as inicializações de callout, adicione:

```c
sc->tq = taskqueue_create("myfirst taskq", M_WAITOK,
    taskqueue_thread_enqueue, &sc->tq);
if (sc->tq == NULL) {
        error = ENOMEM;
        goto fail_sx;
}
error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
    "%s taskq", device_get_nameunit(dev));
if (error != 0)
        goto fail_tq;
```

A chamada lê-se assim: crie um taskqueue chamado `"myfirst taskq"`, aloque com `M_WAITOK` para que a alocação não possa falhar (estamos no attach, que é um contexto em que é possível dormir), use `taskqueue_thread_enqueue` como dispatcher para que a fila seja atendida por threads do kernel, e passe `&sc->tq` como contexto para que o dispatcher possa encontrar a fila.

O nome `"myfirst taskq"` é o rótulo legível por humanos que aparece em `procstat -t`. A convenção nos exemplos do Capítulo 14 é `"<driver> taskq"` para um driver com uma única fila privada; drivers com múltiplas filas devem usar nomes mais específicos como `"myfirst rx taskq"` e `"myfirst tx taskq"`.

`taskqueue_start_threads` cria as threads trabalhadoras. O primeiro argumento é `&sc->tq`, um ponteiro duplo para que a função encontre o taskqueue. O segundo argumento é a contagem de threads; usamos uma thread para o `myfirst`. Um driver com trabalho pesado e paralelizável pode usar mais. O terceiro argumento é a prioridade; `PWAIT` é a escolha usual e equivalente ao que o `taskqueue_thread` predefinido usa. O nome variádico é uma string de formato para o nome de cada thread; `device_get_nameunit(dev)` fornece um nome por instância, de modo que múltiplas instâncias de `myfirst` tenham threads distinguíveis.

Os caminhos de falha merecem atenção. Se `taskqueue_create` retornar NULL (normalmente não ocorre com `M_WAITOK`, mas seja defensivo), pulamos para `fail_sx`. Se `taskqueue_start_threads` falhar, pulamos para `fail_tq`, que deve chamar `taskqueue_free` antes de continuar com o restante da limpeza. O código-fonte do Capítulo 14, Estágio 2 (consulte a árvore de exemplos), tem os rótulos na ordem correta.

### Atualizando os Pontos de Enfileiramento

Toda chamada `taskqueue_enqueue(taskqueue_thread, ...)` passa a ser `taskqueue_enqueue(sc->tq, ...)`. O mesmo vale para drenagens: `taskqueue_drain(taskqueue_thread, ...)` passa a ser `taskqueue_drain(sc->tq, ...)`.

Após o Estágio 1, o driver tem dois desses pontos de chamada: o enfileiramento em `myfirst_tick_source` e a drenagem em `myfirst_detach`. Ambos mudam em uma única passagem de busca e substituição.

### A Sequência de Teardown

A ordenação do detach cresce com duas linhas. A sequência completa para o Estágio 2 é:

1. Recusar o detach se `active_fhs > 0`.
2. Limpar `is_attached` sob o mutex, fazer broadcast em ambos os cvs, liberar o mutex.
3. Drenar os três callouts.
4. Drenar `selwake_task` no taskqueue privado.
5. Drenar `rsel` e `wsel` via `seldrain`.
6. Liberar o taskqueue privado com `taskqueue_free`.
7. Destruir dispositivos, liberar sysctls, destruir cbuf, liberar contadores, destruir cvs, destruir sx, destruir mtx.

As novas etapas são a 4 (que já existia no Estágio 1, agora apontando para `sc->tq`) e a 6 (que é nova no Estágio 2).

Uma dúvida natural: precisamos do `taskqueue_drain` explícito na etapa 4 se a etapa 6 vai drenar tudo de qualquer forma? Tecnicamente, não. `taskqueue_free` drena todas as tasks pendentes antes de destruir a fila. Mas manter a drenagem explícita tem dois benefícios. Primeiro, deixa a ordenação explícita: você vê que a drenagem da task acontece antes de `seldrain`, que é a ordenação que nos importa. Segundo, separa a questão "espere esta task específica terminar" da questão "destrua a fila inteira". Se estágios posteriores adicionarem mais tasks na mesma fila, cada uma terá sua própria drenagem explícita, e o código deixa claro o que está acontecendo.

O código relevante em `myfirst_detach`:

```c
/* Chapter 13: drain every callout. No lock held; safe to sleep. */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);

/* Chapter 14 Stage 1: drain every task. */
taskqueue_drain(sc->tq, &sc->selwake_task);

seldrain(&sc->rsel);
seldrain(&sc->wsel);

/* Chapter 14 Stage 2: destroy the private taskqueue. */
taskqueue_free(sc->tq);
sc->tq = NULL;
```

Definir `sc->tq` como `NULL` após a liberação é uma medida defensiva: um bug posterior que tente usar um ponteiro após a liberação vai desreferenciar `NULL` e travar no ponto de chamada, em vez de corromper memória não relacionada. Não tem custo algum e ocasionalmente salva uma tarde de depuração.

### O Caminho de Falha no Attach

Percorra com cuidado o caminho de falha no attach. O attach do Capítulo 13 tinha rótulos para os caminhos de falha do cbuf e do mutex. O Estágio 2 adiciona rótulos relacionados ao taskqueue:

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

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        sx_init(&sc->cfg_sx, "myfirst cfg");

        sc->tq = taskqueue_create("myfirst taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) {
                error = ENOMEM;
                goto fail_sx;
        }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0)
                goto fail_tq;

        MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
        MYFIRST_CO_INIT(sc, &sc->watchdog_co);
        MYFIRST_CO_INIT(sc, &sc->tick_source_co);

        TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);

        /* ... rest of attach as in Chapter 13 ... */

        return (0);

fail_cb:
        cbuf_destroy(&sc->cb);
fail_tq:
        taskqueue_free(sc->tq);
fail_sx:
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}
```

Os rótulos de falha encadeiam: `fail_cb` chama `cbuf_destroy` e então cai para `fail_tq`, que chama `taskqueue_free` e então cai para `fail_sx`, que destrói os cvs, o sx e o mutex. Cada rótulo desfaz tudo até o ponto em que a chamada de inicialização correspondente foi bem-sucedida. Se `taskqueue_start_threads` falhar, vamos diretamente para `fail_tq` (o taskqueue foi alocado mas não tem threads; `taskqueue_free` ainda lida com isso corretamente porque um taskqueue recém-criado e não iniciado tem zero threads a recolher).

Note também: `TASK_INIT` não tem um modo de falha (é uma macro que define campos) e não precisa de um contraponto de destruição. A task fica ociosa assim que `taskqueue_drain` for chamada sobre ela, e o armazenamento é simplesmente recuperado junto com a softc.

### Convenções de Nomeação de Threads

`taskqueue_start_threads` recebe uma string de formato e uma lista de argumentos variáveis, de modo que cada thread recebe seu próprio nome. A convenção de nomeação tem um efeito real na depurabilidade, por isso vale a pena um breve parágrafo sobre convenções.

A string de formato que usamos é `"%s taskq"` com `device_get_nameunit(dev)` como argumento. Para uma primeira instância de `myfirst`, a thread aparece como `myfirst0 taskq`. Para uma segunda instância, aparece como `myfirst1 taskq`. Isso torna a thread identificável em `procstat -t` e `ps ax`.

Um driver com múltiplas filas privadas deve escolher nomes que distingam as filas:

```c
taskqueue_start_threads(&sc->tx_tq, 1, PWAIT,
    "%s tx", device_get_nameunit(dev));
taskqueue_start_threads(&sc->rx_tq, 1, PWAIT,
    "%s rx", device_get_nameunit(dev));
```

Drivers de rede costumam nomear as threads por fila de hardware de forma ainda mais específica (`"%s tx%d"` com o índice da fila) para que `procstat -t` mostre o worker dedicado de cada fila de hardware.

### Escolhendo a Contagem de Threads

A maioria dos drivers cria um taskqueue privado de uma única thread. Um único worker faz com que as tasks rodem sequencialmente, o que simplifica o raciocínio sobre locking: dentro do callback da task, você pode assumir que nenhuma outra invocação do mesmo callback está rodando concorrentemente, sem nenhuma exclusão explícita.

Um driver com múltiplos canais de hardware que precisem de processamento paralelo pode criar múltiplas threads trabalhadoras no mesmo taskqueue. O taskqueue garante que uma única task esteja rodando em no máximo uma thread por vez (é isso que `tq_active` rastreia), mas tasks diferentes na mesma fila podem rodar em paralelo em threads diferentes. Para o `myfirst`, a configuração de uma única thread é a correta.

Taskqueues com múltiplas threads têm implicações para a contenção de lock: dois workers na mesma fila, cada um rodando uma task diferente, podem disputar pelo mesmo mutex do driver. Se a carga de trabalho é naturalmente paralelizável, uma fila com múltiplas threads acelera as coisas. Se a carga de trabalho é serializada de qualquer forma pelo mutex do driver, múltiplas threads adicionam complexidade sem benefício. Para um primeiro taskqueue, uma única thread é o padrão correto.

### Escolhendo a Prioridade da Thread

O argumento `pri` de `taskqueue_start_threads` é a prioridade de escalonamento da thread. Nos exemplos do Capítulo 14, usamos `PWAIT`. As opções disponíveis na prática são:

- `PWAIT` (numericamente 76): prioridade comum de driver, equivalente à prioridade de `taskqueue_thread`.
- `PI_NET` (numericamente 4): prioridade adjacente à rede, usada por muitos drivers ethernet.
- `PI_DISK`: constante histórica; território de `PRI_MIN_KERN`. Usada por drivers de armazenamento.
- `PRI_MIN_KERN` (numericamente 48): prioridade genérica de thread do kernel, usada quando as constantes acima não se encaixam bem.

Para um driver cujo trabalho de tarefa não é sensível à latência, `PWAIT` é suficiente. Para um driver que precisa executar seus callbacks de tarefa prontamente mesmo sob carga, elevar a prioridade mais próximo das threads de interrupção é às vezes justificável. `myfirst` usa `PWAIT`.

Se você estiver escrevendo um driver e não tiver certeza de qual prioridade a convenção espera, observe drivers do mesmo tipo em `/usr/src/sys/dev/`. Um driver de armazenamento que usa taskqueues provavelmente usa `PRI_MIN_KERN` ou `PI_DISK`; um driver de rede provavelmente usa `PI_NET`. Seguir o padrão dos drivers existentes é melhor do que inventar uma prioridade por conta própria.

### Um Exemplo Real da Fonte: `ale(4)`

Um driver real que usa exatamente o padrão ensinado nesta seção. De `/usr/src/sys/dev/ale/if_ale.c`:

```c
/* Create local taskq. */
sc->ale_tq = taskqueue_create_fast("ale_taskq", M_WAITOK,
    taskqueue_thread_enqueue, &sc->ale_tq);
taskqueue_start_threads(&sc->ale_tq, 1, PI_NET, "%s taskq",
    device_get_nameunit(sc->ale_dev));
```

O driver ethernet `ale` cria um taskqueue rápido (`taskqueue_create_fast`) com um spin mutex, porque precisa ser capaz de enfileirar a partir de seu handler de interrupção de filtro. Ele executa uma thread com prioridade `PI_NET`, seguindo a convenção de nomenclatura por unidade. A estrutura é exatamente a que usamos em `myfirst`, com a escolha entre rápido e regular e a prioridade refletindo o contexto do driver.

Do mesmo arquivo, o caminho correspondente de encerramento:

```c
taskqueue_drain(sc->ale_tq, &sc->ale_int_task);
/* ... */
taskqueue_free(sc->ale_tq);
```

`taskqueue_drain` na tarefa específica e, em seguida, `taskqueue_free` na fila. O mesmo idioma que utilizamos.

Vale a pena ler o setup e o teardown do `ale(4)` uma vez. É um driver real, fazendo trabalho real, usando o padrão que você está prestes a escrever no seu próprio driver. Todo driver em `/usr/src/sys/dev/` que usa taskqueues tem uma estrutura muito similar.

### Verificando a Regressão do Comportamento do Capítulo 13

O Stage 2 não deve quebrar nada que o Capítulo 13 estabeleceu. Antes de prosseguir, execute novamente o conjunto de testes de estresse do Capítulo 13 com o driver do Stage 2 carregado:

```text
# cd /path/to/examples/part-03/ch13-timers-and-delayed-work/stage4-final
# ./test-all.sh
```

O teste deve passar exatamente como passou ao final do Capítulo 13. Se não passar, a regressão está em algo que o Stage 2 alterou; reverta para o código pré-Stage-2 e encontre a diferença. A causa mais comum é uma atualização de chamada de enqueue esquecida (um enqueue ainda aponta para `taskqueue_thread` em vez de `sc->tq`). Esses compilam normalmente porque a API é a mesma; eles produzem bugs não relacionados em tempo de execução.

### Observando o Taskqueue Privado

Com o driver do Stage 2 carregado, `procstat -t` mostra a nova thread:

```text
# procstat -t | grep myfirst
  <PID> <THREAD>      0 100 myfirst0 taskq      sleep   -      -   0:00
```

O nome `myfirst0 taskq` é o nome da thread por instância que definimos em `taskqueue_start_threads`. O estado é `sleep` porque a thread está bloqueada aguardando uma tarefa. O wchan está vazio porque a thread está dormindo em seu próprio cv, que o `procstat` pode mostrar de forma diferente entre versões.

Ative a fonte de tick e observe novamente:

```text
# sysctl dev.myfirst.0.tick_source_interval_ms=100
# procstat -t | grep myfirst
  <PID> <THREAD>      0 100 myfirst0 taskq      run     -      -   0:00
```

Brevemente, você pode pegar a thread no estado `run` enquanto ela processa uma tarefa. Na maior parte do tempo ela fica em `sleep`. Ambos são esperados.

`ps ax` mostra a mesma thread:

```text
# ps ax | grep 'myfirst.*taskq'
   50  -  IL      0:00.00 [myfirst0 taskq]
```

Os colchetes indicam uma thread do kernel. A thread está sempre presente enquanto o driver está attached; ela desaparece no detach.

### A Lista de Verificação Pré-Produção para o Stage 2

Uma lista de verificação rápida para conferir antes de declarar o Stage 2 concluído. Cada item é uma pergunta; cada uma deve ser respondida com segurança.

- [ ] O `attach` cria o taskqueue antes de qualquer código que possa enfileirar nele?
- [ ] O `attach` inicia pelo menos uma thread de trabalho no taskqueue antes de qualquer código que espere que uma tarefa realmente execute?
- [ ] O `attach` possui rótulos de falha que chamam `taskqueue_free` na fila caso uma inicialização subsequente falhe?
- [ ] O `detach` drena todas as tarefas que o driver possui antes de liberar qualquer estado que essas tarefas acessem?
- [ ] O `detach` chama `taskqueue_free` após drenar todas as tarefas e antes de destruir o mutex?
- [ ] O `detach` define `sc->tq = NULL` após a liberação, para maior clareza defensiva?
- [ ] A prioridade da thread do taskqueue foi escolhida deliberadamente, com uma justificativa que corresponde ao tipo do driver?
- [ ] O nome da thread do taskqueue é informativo o suficiente para que a saída de `procstat -t` seja útil?
- [ ] Toda chamada a `taskqueue_enqueue` aponta para `sc->tq`, e não para `taskqueue_thread` (a menos que o enqueue seja em um caminho genuinamente compartilhado por um motivo específico)?
- [ ] Toda chamada a `taskqueue_drain` corresponde a um `taskqueue_enqueue` na mesma fila com a mesma tarefa?

Um driver que responde positivamente a cada item é um driver que gerencia corretamente seu taskqueue privado. Um driver que não consegue provavelmente está a um passo de um use-after-free no detach.

### Erros Comuns no Stage 2

Três erros que iniciantes cometem ao adicionar um taskqueue privado. Cada um pode ser evitado com um hábito.

**Criar o taskqueue depois de algo que enfileira.** Se o enqueue ocorrer antes de `taskqueue_create` retornar, o enqueue desreferencia um ponteiro `NULL`. Sempre coloque `taskqueue_create` no início do attach, antes de qualquer código que possa disparar um enqueue.

**Esquecer o `taskqueue_start_threads`.** Um taskqueue sem threads de trabalho é uma fila que aceita enqueues mas nunca executa os callbacks. As tarefas se acumulam silenciosamente. Se você achar que "minha tarefa nunca dispara", verifique se chamou `taskqueue_start_threads`.

**Chamar `taskqueue_free` sem antes limpar `is_attached`.** Se o taskqueue for liberado enquanto um callback de callout ainda está em execução e pode enfileirar, o enqueue do callout vai travar no taskqueue já liberado. Sempre limpe `is_attached`, drene os callouts, drene a tarefa e depois libere. A ordem é o que garante a segurança.

A Seção 7 percorrerá cada um desses erros ao vivo, com um exercício de quebrar e corrigir em um driver deliberadamente fora de ordem. Por enquanto, a regra é: siga a ordem desta seção e o ciclo de vida do taskqueue estará correto.

### Encerrando a Seção 4

O driver agora possui seu próprio taskqueue. Uma thread, um nome, um tempo de vida. O attach o cria e inicia um worker; o detach drena a tarefa, libera o taskqueue e desfaz tudo. A ordem em relação a callouts e selinfo é respeitada. `procstat -t` mostra a thread por um nome reconhecível. O driver é autossuficiente em sua abordagem de trabalho diferido.

A Seção 5 dá o próximo passo: exploraremos deliberadamente o comportamento de coalescing para batching, introduziremos a variante `timeout_task` para tarefas agendadas e discutiremos como a prioridade se aplica quando uma fila contém múltiplos tipos de tarefas.

---

## Seção 5: Priorização e Coalescing de Trabalho

Toda tarefa que seu driver possui entra em uma fila. A fila possui uma política para decidir a ordem em que as tarefas executam e o que acontece quando a mesma tarefa é enfileirada duas vezes. A Seção 5 torna essa política explícita, mostra como usar o contrato de coalescing deliberadamente para agrupar trabalho em lotes e, por fim, apresenta `timeout_task` como o análogo do lado do taskqueue para o callout.

Esta seção é densa em ideias, mas curta em código. As duas mudanças no driver para o Stage 3 são uma nova tarefa no mesmo taskqueue e uma timeout task que conduz uma escrita periódica em lote. O valor está nas regras que você internaliza.

### A Regra de Ordenação por Prioridade

O campo `ta_priority` em `struct task` ordena as tarefas dentro de uma única fila. Tarefas com maior prioridade executam antes de tarefas com menor prioridade. Uma tarefa com prioridade 5 enfileirada depois de uma tarefa com prioridade 0 executa antes da tarefa de prioridade 0, mesmo que a tarefa de prioridade 0 tenha sido enfileirada primeiro.

A prioridade é um pequeno inteiro sem sinal (`uint8_t`, com intervalo de 0 a 255). A maioria dos drivers usa prioridade 0 para tudo, caso em que a fila é efetivamente FIFO. Um driver com urgências genuinamente diferentes para tarefas distintas pode atribuir prioridades diferentes e deixar o taskqueue reordenar.

Um exemplo rápido. Suponha que um driver tenha duas tarefas: uma `reset_task` que recupera de um erro de hardware e uma `stats_task` que consolida estatísticas acumuladas. Se ambas forem enfileiradas em um intervalo curto, o reset deve executar primeiro. Atribuir prioridade 10 a `reset_task` e prioridade 0 a `stats_task` consegue isso. A tarefa de reset executa primeiro mesmo que tenha sido enfileirada por último.

Use prioridades com parcimônia. Um driver com dez tipos de tarefas diferentes e dez prioridades diferentes é mais difícil de raciocinar do que um driver com dez tipos de tarefas que todos executam na ordem de enqueue. As prioridades existem para diferenciação real, não para ordenação estética.

### A Regra de Coalescing, Revisitada

Da Seção 2, e vale a pena repetir: se uma tarefa é enfileirada enquanto já está pendente, o kernel incrementa `ta_pending` e não a vincula uma segunda vez. O callback executa uma vez, com a contagem de pendências no segundo argumento.

O código preciso, de `/usr/src/sys/kern/subr_taskqueue.c`:

```c
if (task->ta_pending) {
        if (__predict_false((flags & TASKQUEUE_FAIL_IF_PENDING) != 0)) {
                TQ_UNLOCK(queue);
                return (EEXIST);
        }
        if (task->ta_pending < USHRT_MAX)
                task->ta_pending++;
        TQ_UNLOCK(queue);
        return (0);
}
```

O contador satura em `USHRT_MAX` (65535), que é o limite máximo de quantas vezes o coalescing pode acumular. Além disso, enqueues repetidos são perdidos da perspectiva do contador, embora ainda retornem sucesso. Na prática, ninguém atinge esse limite, porque uma tarefa que acumula 65535 vezes tem problemas muito mais profundos.

A regra de coalescing tem três consequências em torno das quais você projeta.

Primeiro, **uma tarefa trata no máximo "uma execução por despertar do escalonador"**. Se seu modelo de trabalho precisa de "um callback por evento", uma única tarefa está errada. Você precisa de estado por evento.

Segundo, **o callback deve ser capaz de tratar múltiplos eventos em uma única execução**. Escrever o callback como se `pending` fosse sempre 1 é um bug que só aparece sob carga. Use o argumento pending deliberadamente, ou estruture o callback para que ele processe tudo que estiver em uma fila de propriedade do driver até que a fila esteja vazia.

Terceiro, **você pode explorar o coalescing para batching**. Se um produtor enfileira uma tarefa uma vez por evento e o consumidor drena um lote por execução, o sistema converge naturalmente para a taxa que o consumidor consegue sustentar. Sob carga leve, o coalescing nunca é acionado (um evento, uma execução). Sob carga pesada, o coalescing comprime as rajadas em execuções únicas com lotes maiores. O comportamento é autoadaptável.

### Um Padrão de Batching Deliberado: Stage 3

O Stage 3 adiciona uma segunda tarefa ao driver: uma `bulk_writer_task` que escreve um número fixo de bytes de tick no buffer em uma única execução, conduzida por um callout que enfileira a tarefa periodicamente. O padrão é artificial (o driver real simplesmente usaria uma fonte de tick mais rápida), mas é a demonstração mais simples de batching deliberado.

A adição ao softc:

```c
struct task             bulk_writer_task;
int                     bulk_writer_batch;      /* bytes per firing */
```

O valor padrão de `bulk_writer_batch` é zero (desabilitado). Um sysctl o expõe para ajuste.

O callback:

```c
static void
myfirst_bulk_writer_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        int batch, written;
        char buf[64];

        MYFIRST_LOCK(sc);
        batch = sc->bulk_writer_batch;
        MYFIRST_UNLOCK(sc);

        if (batch <= 0)
                return;

        batch = MIN(batch, (int)sizeof(buf));
        memset(buf, 'B', batch);

        MYFIRST_LOCK(sc);
        written = (int)cbuf_write(&sc->cb, buf, batch);
        if (written > 0) {
                counter_u64_add(sc->bytes_written, written);
                cv_signal(&sc->data_cv);
        }
        MYFIRST_UNLOCK(sc);

        if (written > 0)
                selwakeup(&sc->rsel);
}
```

Algumas observações.

O callback adquire `sc->mtx`, lê o tamanho do lote e libera. Adquirir e liberar duas vezes está correto; o trabalho intermediário (memset) não precisa do lock. A segunda aquisição envolve a operação real de cbuf e a atualização do contador. O selwakeup ocorre sem nenhum lock mantido, como sempre.

O argumento `pending` não é usado neste callback simples. Para um design de batching diferente, `pending` informaria ao callback quantas vezes a tarefa foi enfileirada e, portanto, quanto trabalho acumulou. Aqui a política de batching é "sempre escrever exatamente `bulk_writer_batch` bytes por execução, independentemente de quantas vezes foi enfileirada", portanto `pending` não entra em cena.

O callback não verifica `is_attached`. Não precisa. O detach drena a tarefa antes de liberar qualquer coisa que ela acesse, e `sc->mtx` protege `sc->cb` até que a drenagem seja concluída.

### O Coalescing em Ação

Para demonstrar o coalescing deliberadamente, o Stage 3 adiciona um sysctl `bulk_writer_flood` cujo escritor tenta enfileirar `bulk_writer_task` mil vezes em um loop fechado:

```c
static int
myfirst_sysctl_bulk_writer_flood(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int flood = 0;
        int error, i;

        error = sysctl_handle_int(oidp, &flood, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (flood < 1 || flood > 10000)
                return (EINVAL);

        for (i = 0; i < flood; i++)
                taskqueue_enqueue(sc->tq, &sc->bulk_writer_task);
        return (0);
}
```

Execute:

```text
# sysctl dev.myfirst.0.bulk_writer_batch=32
# sysctl dev.myfirst.0.bulk_writer_flood=1000
```

Imediatamente depois, observe a contagem de bytes. Sem coalescing, mil enqueues de 32 bytes cada produziriam 32000 bytes. Com coalescing, o número real é uma execução de 32 bytes, porque os mil enqueues foram comprimidos em uma única tarefa pendente. O contador `bytes_written` do driver deve aumentar em 32, não em 32000.

Este é o contrato de coalescing funcionando como projetado. O produtor pediu mil execuções de tarefa; o taskqueue entregou uma. A execução única do callback refletiu todas as mil solicitações, mas realizou a quantidade fixa de trabalho que a política de batching especificou.

### Usando `pending` para Batching Adaptativo

Um padrão mais sofisticado usa o argumento `pending` para adaptar o tamanho do lote à profundidade da fila. Suponha que um driver queira escrever `pending` bytes por execução: um byte por enqueue, dobrado em execuções coalesced. O callback se torna:

```c
static void
myfirst_adaptive_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        char buf[64];
        int n;

        n = MIN(pending, (int)sizeof(buf));
        memset(buf, 'A', n);

        MYFIRST_LOCK(sc);
        (void)cbuf_write(&sc->cb, buf, n);
        counter_u64_add(sc->bytes_written, n);
        cv_signal(&sc->data_cv);
        MYFIRST_UNLOCK(sc);

        selwakeup(&sc->rsel);
}
```

O callback escreve `pending` bytes (até o tamanho do buffer). Sob carga leve, `pending` é 1 e o callback escreve um byte. Sob carga pesada, `pending` é a profundidade da fila no momento em que o callback começou, e o callback escreve essa quantidade de bytes em uma única passagem. O batching escala naturalmente com a carga.

Este design é útil quando cada enfileiramento corresponde a um evento real que requer uma unidade de trabalho, e o batching é uma otimização de desempenho em vez de uma mudança semântica. O handler de "conclusão de transmissão" de um driver de rede é um exemplo clássico: cada pacote transmitido gera uma interrupção que enfileira uma tarefa; o trabalho da tarefa é recuperar os descritores concluídos; em taxas de pacotes elevadas, muitas interrupções se condensam em uma única execução da tarefa que recupera muitos descritores de uma vez.

Não adicionaremos a tarefa de batching adaptativo ao `myfirst` na Etapa 3, porque a versão de lote fixo já demonstra o coalescimento. O padrão adaptativo vale a pena ter em mente para o trabalho real com drivers; os drivers reais do FreeBSD que você vai ler o utilizam com frequência.

### As Flags de Enfileiramento

`taskqueue_enqueue_flags` estende `taskqueue_enqueue` com dois bits de flag:

- `TASKQUEUE_FAIL_IF_PENDING`: se a tarefa já estiver pendente, retorna `EEXIST` em vez de fazer coalescência.
- `TASKQUEUE_FAIL_IF_CANCELING`: se a tarefa estiver sendo cancelada no momento, retorna `EAGAIN` em vez de esperar.

`TASKQUEUE_FAIL_IF_PENDING` é útil quando você precisa saber se o enfileiramento de fato produziu um novo estado pendente, para fins de contabilidade ou depuração. Um driver que conta "quantas vezes esta tarefa foi enfileirada" pode usar o flag, receber `EEXIST` nas chamadas redundantes, e contar apenas os enfileiramentos não redundantes.

`TASKQUEUE_FAIL_IF_CANCELING` é útil durante o encerramento. Se você está desmontando o driver e algum caminho de código tentaria enfileirar uma tarefa, você pode passar o flag e verificar `EAGAIN` para evitar reinserir uma tarefa que está no meio de um cancelamento. A maioria dos drivers não precisa disso na prática; a verificação de `is_attached` normalmente trata a condição equivalente.

Nenhum dos flags é usado em `myfirst`. Ambos existem, e um driver com necessidades específicas pode recorrer a eles. Para uso comum, o simples `taskqueue_enqueue` é a escolha correta.

### A Variante `timeout_task`

Às vezes você quer que uma tarefa dispare após um atraso específico. O callout seria o primitivo natural para isso, mas se o trabalho que o callback atrasado precisa fazer requer contexto de thread, você precisa do contexto da tarefa, não do callout. O kernel oferece `struct timeout_task` exatamente para esse caso.

`timeout_task` é definido em `/usr/src/sys/sys/_task.h`:

```c
struct timeout_task {
        struct taskqueue *q;
        struct task t;
        struct callout c;
        int    f;
};
```

A estrutura encapsula um `struct task`, um `struct callout` e um flag interno. Quando você agenda uma timeout task com `taskqueue_enqueue_timeout`, o kernel inicia o callout; quando o callout dispara, seu callback enfileira a tarefa subjacente na taskqueue. A tarefa então executa em contexto de thread, com todas as garantias habituais.

A inicialização usa `TIMEOUT_TASK_INIT`:

```c
TIMEOUT_TASK_INIT(queue, timeout_task, priority, func, context);
```

A macro se expande para uma chamada de função `_timeout_task_init` que inicializa tanto a tarefa quanto o callout com a vinculação adequada. Você deve passar a taskqueue no momento da inicialização porque o callout é configurado para enfileirar nessa fila específica.

O agendamento usa `taskqueue_enqueue_timeout(tq, &tt, ticks)`:

```c
int taskqueue_enqueue_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, int ticks);
```

O argumento `ticks` segue a mesma convenção do `callout_reset`: `hz` ticks equivale a um segundo.

A drenagem usa `taskqueue_drain_timeout(tq, &tt)`, que aguarda o callout expirar (ou o cancela se ainda estiver pendente) e então aguarda a tarefa subjacente ser concluída. A drenagem é uma única chamada, mas lida com as duas fases: a do callout e a da tarefa.

O cancelamento usa `taskqueue_cancel_timeout(tq, &tt, &pendp)`:

```c
int taskqueue_cancel_timeout(struct taskqueue *queue,
    struct timeout_task *timeout_task, u_int *pendp);
```

Retorna zero se o timeout foi cancelado sem problemas, ou `EBUSY` se a tarefa estiver em execução no momento. No caso de `EBUSY`, o caminho típico é chamar `taskqueue_drain_timeout` em seguida.

### Um Timeout Task no Stage 3: Reset com Atraso

O Stage 3 adiciona uma timeout task ao driver: um reset com atraso que dispara `reset_delay_ms` milissegundos após a escrita no sysctl de reset. O sysctl de reset existente executa de forma síncrona; a variante com atraso agenda o reset para um momento posterior. Útil para testes e para situações em que o reset não deve ocorrer antes que as operações de IO correntes sejam concluídas.

A adição ao softc:

```c
struct timeout_task     reset_delayed_task;
int                     reset_delay_ms;
```

A inicialização em attach:

```c
TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, 0,
    myfirst_reset_delayed_task, sc);
sc->reset_delay_ms = 0;
```

`TIMEOUT_TASK_INIT` recebe a taskqueue como primeiro argumento porque o callout dentro da timeout_task precisa saber em qual fila enfileirar quando disparar.

O callback:

```c
static void
myfirst_reset_delayed_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_LOCK(sc);
        MYFIRST_CFG_XLOCK(sc);

        cbuf_reset(&sc->cb);
        sc->cfg.debug_level = 0;
        counter_u64_zero(sc->bytes_read);
        counter_u64_zero(sc->bytes_written);

        MYFIRST_CFG_XUNLOCK(sc);
        MYFIRST_UNLOCK(sc);

        cv_broadcast(&sc->room_cv);
        device_printf(sc->dev, "delayed reset fired (pending=%d)\n", pending);
}
```

Mesma lógica do reset síncrono do Capítulo 13, mas em contexto de thread. Ele pode adquirir o `cfg_sx` passível de sleep sem as complicações que um callout enfrentaria. A contagem de `pending` é registrada para fins de diagnóstico.

O handler do sysctl que arma o reset com atraso:

```c
static int
myfirst_sysctl_reset_delayed(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int ms = 0;
        int error;

        error = sysctl_handle_int(oidp, &ms, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (ms < 0)
                return (EINVAL);
        if (ms == 0) {
                (void)taskqueue_cancel_timeout(sc->tq,
                    &sc->reset_delayed_task, NULL);
                return (0);
        }

        sc->reset_delay_ms = ms;
        taskqueue_enqueue_timeout(sc->tq, &sc->reset_delayed_task,
            (ms * hz + 999) / 1000);
        return (0);
}
```

Escritas com valor zero cancelam o reset com atraso pendente. Qualquer valor positivo agenda a tarefa para disparar após o número indicado de milissegundos. A conversão de ticks `(ms * hz + 999) / 1000` é a mesma conversão de teto que usamos para callouts.

O caminho de detach drena a timeout task:

```c
taskqueue_drain_timeout(sc->tq, &sc->reset_delayed_task);
```

O posicionamento da drenagem é o mesmo da drenagem da tarefa simples: após os callouts serem drenados, após `is_attached` ser zerado, antes de `seldrain` e antes de `taskqueue_free`.

### Observando o Reset com Atraso

Com o Stage 3 carregado, arme o reset com atraso para três segundos no futuro:

```text
# sysctl dev.myfirst.0.reset_delayed=3000
```

Três segundos depois, o `dmesg` mostra:

```text
myfirst0: delayed reset fired (pending=1)
```

O `pending=1` confirma que a timeout task disparou uma vez. Agora arme-a várias vezes em rápida sucessão:

```text
# sysctl dev.myfirst.0.reset_delayed=1000
# sysctl dev.myfirst.0.reset_delayed=1000
# sysctl dev.myfirst.0.reset_delayed=1000
```

Um segundo depois, apenas um reset dispara. O `dmesg` mostra:

```text
myfirst0: delayed reset fired (pending=1)
```

Por que apenas um disparo? Porque `taskqueue_enqueue_timeout` se comporta de forma consistente com `callout_reset`: armar uma timeout task pendente substitui o prazo anterior. Os três armamentos sucessivos produzem um único disparo agendado. O mesmo comportamento se aplicaria se usássemos `callout_reset` em um callout simples.

### Quando Usar `timeout_task` Versus Callout Combinado com Task

Uma timeout task é o primitivo certo quando você quer uma ação com atraso em contexto de thread e o atraso é o parâmetro principal. Um callout simples combinado com um enfileiramento de tarefa é o primitivo certo quando você quer uma ação com atraso e o atraso é um detalhe de implementação, por exemplo, quando o atraso é recalculado dinamicamente a cada vez. Ambos funcionam.

Os dois padrões têm formatos ligeiramente diferentes no código-fonte:

```c
/* timeout_task pattern */
TIMEOUT_TASK_INIT(tq, &tt, 0, fn, ctx);
...
taskqueue_enqueue_timeout(tq, &tt, ticks);
...
taskqueue_drain_timeout(tq, &tt);
```

```c
/* callout + task pattern */
callout_init_mtx(&co, &sc->mtx, 0);
TASK_INIT(&t, 0, fn, ctx);
...
callout_reset(&co, ticks, myfirst_co_fn, sc);
/* in the callout callback: taskqueue_enqueue(tq, &t); */
...
callout_drain(&co);
taskqueue_drain(tq, &t);
```

A versão com timeout_task é mais curta porque o kernel já encapsulou o padrão para você. A versão com callout+task é mais flexível porque o callback do callout pode decidir dinamicamente se enfileira a tarefa (por exemplo, com base em condições de estado que não existem no momento do agendamento).

Para o reset com atraso do `myfirst`, a timeout_task é a escolha certa porque a decisão de disparar é tomada no momento do agendamento (o escritor do sysctl solicitou) e nada no intervalo altera essa decisão.

### Ordenação por Prioridade Entre Tipos de Tarefa

Um driver com múltiplas tarefas na mesma taskqueue pode usar prioridades para ordená-las. No `myfirst` não precisamos disso; todas as tarefas têm prioridade igual. Mas o padrão vale a pena entender para quando for necessário.

Suponha que tivéssemos uma `high_priority_reset_task` que deve executar antes de qualquer outra tarefa pendente. Nós a inicializaríamos com uma prioridade maior que zero:

```c
TASK_INIT(&sc->high_priority_reset_task, 10,
    myfirst_high_priority_reset_task, sc);
```

E a enfileiramos normalmente:

```c
taskqueue_enqueue(sc->tq, &sc->high_priority_reset_task);
```

Se a fila tiver várias tarefas pendentes, incluindo a nova e várias tarefas de prioridade zero, a nova executa primeiro por causa de sua prioridade mais alta. A prioridade é uma propriedade da tarefa (definida na inicialização), não do enfileiramento (definida a cada chamada); se uma tarefa deve às vezes ser urgente e às vezes não, você precisa de duas estruturas de tarefa com duas prioridades, não de uma tarefa que você reajusta.

### Uma Nota Sobre Equidade

Uma taskqueue com uma única thread trabalhadora executa as tarefas estritamente em ordem de prioridade, com empates resolvidos pela ordem de enfileiramento. Uma taskqueue com múltiplas threads trabalhadoras pode executar várias tarefas em paralelo; a prioridade ainda ordena a lista, mas as trabalhadoras paralelas podem despachar tarefas fora da ordem estrita nas margens. Para a maioria dos drivers, isso não importa.

Se equidade estrita ou ordenação estrita por prioridade for necessária, uma única trabalhadora é a escolha certa. Se throughput ao custo de reordenação ocasional for aceitável, múltiplas trabalhadoras são adequadas. O `myfirst` usa uma única trabalhadora.

### Encerrando a Seção 5

O Stage 3 adicionou uma tarefa de agrupamento deliberado e uma timeout task. A tarefa de agrupamento demonstra coalescência ao colapsar mil enfileiramentos em um único disparo; a timeout task demonstra execução com atraso em contexto de thread. Ambas compartilham a taskqueue privada do Stage 2, ambas são drenadas no detach na ordem estabelecida, e ambas obedecem à disciplina de locking que o restante do driver utiliza.

As regras de prioridade e coalescência estão agora explícitas. A prioridade de uma tarefa a ordena dentro da fila; o contador `ta_pending` de uma tarefa dobra enfileiramentos redundantes em um único disparo cujo argumento `pending` carrega a contagem acumulada.

A Seção 6 retrocede da refatoração do `myfirst` para examinar os padrões que aparecem em drivers reais do FreeBSD. Os modelos mentais se acumulam; o driver não muda novamente até a Seção 8.



## Seção 6: Padrões do Mundo Real com Taskqueues

Até aqui, o Capítulo 14 desenvolveu um único driver em três estágios. Drivers reais do FreeBSD usam taskqueues em um punhado de formas recorrentes. Esta seção cataloga os padrões, mostra onde cada um aparece em `/usr/src/sys/dev/`, e explica quando recorrer a qual. Reconhecer os padrões transforma a leitura de código-fonte de driver de um quebra-cabeça em um exercício de vocabulário.

Cada padrão é apresentado como uma receita curta: o problema, o formato de taskqueue que o resolve, um esboço de código e uma referência a um driver real que você pode ler para ver a versão em produção.

### Padrão 1: Log ou Notificação Diferidos a Partir de um Contexto de Borda

**Problema.** Um callback de contexto de borda (callout, filtro de interrupção, seção epoch) detecta uma condição que deveria produzir uma mensagem de log ou uma notificação para o espaço do usuário. A chamada de log é pesada demais para o contexto de borda: `selwakeup`, `log(9)`, `kqueue_user_event`, ou um `printf` de várias linhas que mantém um lock que o contexto de borda não pode se dar ao luxo de segurar.

**Solução.** Um `struct task` por condição, inicializado em attach com um callback que realiza a chamada pesada em contexto de thread. O callback do contexto de borda registra a condição no estado do softc (um flag, um contador, um pequeno dado), enfileira a tarefa e retorna. A tarefa executa em contexto de thread, lê a condição do estado do softc, realiza a chamada e limpa a condição.

**Esboço de código.**

```c
struct my_softc {
        struct task log_task;
        int         log_flags;
        struct mtx  mtx;
        ...
};

#define MY_LOG_UNDERRUN  0x01
#define MY_LOG_OVERRUN   0x02

static void
my_log_task(void *arg, int pending)
{
        struct my_softc *sc = arg;
        int flags;

        mtx_lock(&sc->mtx);
        flags = sc->log_flags;
        sc->log_flags = 0;
        mtx_unlock(&sc->mtx);

        if (flags & MY_LOG_UNDERRUN)
                log(LOG_WARNING, "%s: buffer underrun\n",
                    device_get_nameunit(sc->dev));
        if (flags & MY_LOG_OVERRUN)
                log(LOG_WARNING, "%s: buffer overrun\n",
                    device_get_nameunit(sc->dev));
}

/* In an interrupt or callout callback: */
if (some_condition) {
        sc->log_flags |= MY_LOG_UNDERRUN;
        taskqueue_enqueue(sc->tq, &sc->log_task);
}
```

O campo de flags permite que o contexto de borda acumule múltiplas condições distintas antes que a tarefa execute. Quando a tarefa dispara, ela faz um snapshot dos flags, os limpa e emite uma linha de log por condição. A coalescência dobra enfileiramentos repetidos da mesma condição em uma única invocação do callback, que é exatamente o que você quer para evitar spam de log.

**Exemplo real.** `/usr/src/sys/dev/ale/if_ale.c` usa uma interrupt task (`sc->ale_int_task`) para tratar trabalho diferido do filtro de interrupção, incluindo condições que desejam fazer log ou notificar.

### Padrão 2: Reset ou Reconfiguração com Atraso

**Problema.** O driver detecta uma condição que exige um reset de hardware ou uma mudança de configuração, mas o reset não deve ocorrer imediatamente. Os motivos para o atraso incluem "dar uma chance às operações de IO em andamento de serem concluídas", "agrupar múltiplas causas em um único reset", ou "limitar a taxa de resets para evitar uma tempestade de resets".

**Solução.** Um `struct timeout_task` (ou um `struct callout` combinado com um `struct task`). O detector enfileira a timeout task com o atraso escolhido. Se a condição for resolvida antes que o atraso expire, o detector cancela a timeout task. Se a condição persistir, a tarefa dispara em contexto de thread e realiza o reset.

**Esboço de código.** Mesmo formato que a tarefa de reset com atraso do `myfirst` no Stage 3. A única variação é que o detector normalmente cancela a tarefa pendente sempre que o estado de "necessidade de reset" muda, de forma que o reset só ocorre quando a condição persistiu pelo atraso completo.

**Exemplo real.** Muitos drivers de armazenamento e rede usam esse padrão para recuperação. O driver Broadcom `/usr/src/sys/dev/bge/if_bge.c` usa timeout tasks para reavaliação do estado do link após um evento na camada física.

### Padrão 3: Processamento Pós-Interrupção (a Divisão Filter + Task)

**Problema.** Uma interrupção de hardware chega. O trabalho do filtro de interrupção é decidir "esta é nossa interrupção e o hardware de fato precisa de atenção". O filtro deve executar rapidamente e não pode dormir. O processamento real (leitura de registradores, atendimento de filas de conclusão, possivelmente `copyout` de resultados para o espaço do usuário) não pertence ao filtro.

**Solução.** Uma divisão em dois níveis. O handler de filtro executa de forma síncrona, lê um registrador de status, decide se a interrupção pertence a nós e, caso positivo, enfileira uma task. A task executa em contexto de thread e realiza o trabalho de fato. Essa é a divisão padrão filter-plus-ithread que `bus_setup_intr(9)` suporta nativamente, mas a variante com taskqueue é útil quando o driver quer mais controle sobre o contexto diferido do que o ithread oferece.

**Esboço de código.**

```c
static int
my_intr_filter(void *arg)
{
        struct my_softc *sc = arg;
        uint32_t status;

        status = CSR_READ_4(sc, STATUS_REG);
        if (status == 0)
                return (FILTER_STRAY);

        /* Mask further interrupts from the hardware. */
        CSR_WRITE_4(sc, INTR_MASK_REG, 0);

        taskqueue_enqueue(sc->tq, &sc->intr_task);
        return (FILTER_HANDLED);
}

static void
my_intr_task(void *arg, int pending)
{
        struct my_softc *sc = arg;

        mtx_lock(&sc->mtx);
        my_process_completions(sc);
        mtx_unlock(&sc->mtx);

        /* Unmask interrupts again. */
        CSR_WRITE_4(sc, INTR_MASK_REG, ALL_INTERRUPTS);
}
```

Alguns detalhes merecem atenção. O filtro mascara a interrupção no nível do hardware antes de enfileirar a task, de modo que o hardware não continue disparando enquanto a task estiver pendente. A task executa em contexto de thread, processa as conclusões e reabilita as interrupções ao final. O coalescing dobra múltiplas interrupções em um único disparo de task; o mascaramento impede que o hardware dispare sem limite. A Parte 4 percorrerá a configuração real de interrupções; o padrão mostrado aqui é aquele para o qual este capítulo está preparando você.

**Exemplo real.** `/usr/src/sys/dev/ale/if_ale.c`, `/usr/src/sys/dev/age/if_age.c` e a maioria dos drivers Ethernet utilizam esse padrão ou uma variante próxima.

### Padrão 4: `copyin`/`copyout` Assíncrono Após Conclusão de Hardware

**Problema.** O driver possui uma requisição em fila no espaço do usuário que forneceu endereços para dados de entrada ou saída. A conclusão de hardware chega como uma interrupção. O driver precisa copiar os dados entre os buffers do espaço do usuário e do kernel para finalizar a requisição. `copyin` e `copyout` dormem em seu caminho lento, portanto não podem ser executados em contexto de interrupção.

**Solução.** O caminho de interrupção registra o identificador da requisição e enfileira uma task. A task é executada em contexto de thread, identifica os endereços do espaço do usuário a partir do estado armazenado da requisição, realiza o `copyin` ou `copyout` e acorda a thread do usuário em espera.

**Esboço de código.**

```c
struct my_request {
        struct task finish_task;
        struct proc *proc;
        void *uaddr;
        void *kaddr;
        size_t len;
        int done;
        struct cv cv;
        /* ... */
};

static void
my_finish_task(void *arg, int pending)
{
        struct my_request *req = arg;

        (void)copyout(req->kaddr, req->uaddr, req->len);

        mtx_lock(&req->sc->mtx);
        req->done = 1;
        cv_broadcast(&req->cv);
        mtx_unlock(&req->sc->mtx);
}

/* In the interrupt task: */
taskqueue_enqueue(sc->tq, &req->finish_task);
```

A thread do espaço do usuário aguarda em `req->cv` após submeter a requisição; ela acorda quando a task marca `done` e faz o broadcast.

**Exemplo real.** Drivers de dispositivo de caracteres que implementam ioctls com grandes transferências de dados às vezes utilizam este padrão. A conclusão de transferências USB em modo bulk em `/usr/src/sys/dev/usb/` frequentemente adia a cópia de dados do espaço do usuário por meio de tasks.

### Padrão 5: Retentativa com Backoff após Falha Transitória

**Problema.** Uma operação de hardware falhou, mas a falha é conhecidamente transitória. O driver deseja tentar novamente após um intervalo de backoff, com backoff crescente em falhas consecutivas.

**Solução.** Uma `struct timeout_task` rearmada com atraso crescente a cada falha. O callback da task realiza a retentativa; em caso de sucesso, o driver limpa o backoff; em caso de falha, a task é reenfileirada com um atraso maior.

**Esboço de código.**

```c
struct my_softc {
        struct timeout_task retry_task;
        int retry_interval_ms;
        int retry_attempts;
        /* ... */
};

static void
my_retry_task(void *arg, int pending)
{
        struct my_softc *sc = arg;
        int err;

        err = my_attempt_operation(sc);
        if (err == 0) {
                sc->retry_attempts = 0;
                sc->retry_interval_ms = 10;
                return;
        }

        sc->retry_attempts++;
        if (sc->retry_attempts > MAX_RETRIES) {
                device_printf(sc->dev, "giving up after %d attempts\n",
                    sc->retry_attempts);
                return;
        }

        sc->retry_interval_ms = MIN(sc->retry_interval_ms * 2, 5000);
        taskqueue_enqueue_timeout(sc->tq, &sc->retry_task,
            (sc->retry_interval_ms * hz + 999) / 1000);
}
```

O intervalo inicial é de 10 ms, dobrando a cada falha, com limite máximo de 5 segundos e um número máximo de tentativas. A retentativa continua disparando até que haja sucesso ou desistência. Um caminho de código separado pode cancelar a retentativa (com `taskqueue_cancel_timeout`) caso as condições que a motivaram mudem.

**Exemplo real.** `/usr/src/sys/dev/iwm/if_iwm.c` e outros drivers sem fio utilizam timeout tasks para retentativas de carregamento de firmware e recalibração de link.

### Padrão 6: Teardown Adiado

**Problema.** Um objeto dentro do driver precisa ser liberado, mas algum outro caminho de código ainda pode manter uma referência. Liberar imediatamente seria um use-after-free. O driver precisa liberar posteriormente, após ter certeza de que todas as referências se foram.

**Solução.** Uma `struct task` cujo callback libera o objeto. O caminho de código que deseja liberar o objeto enfileira a task; a task é executada em contexto de thread, após qualquer referência pendente ter tido a oportunidade de ser concluída.

Em formas mais elaboradas, o padrão utiliza contagem de referências: a task decrementa um contador de referências e libera o objeto somente quando o contador chega a zero. Em formas mais simples, a ordenação FIFO do taskqueue é suficiente: todas as tasks enfileiradas anteriormente são concluídas antes que a task de teardown execute, de modo que, se as referências são sempre obtidas dentro de tasks, todas elas já terão se encerrado quando a task de teardown disparar.

**Esboço de código.**

```c
static void
my_free_task(void *arg, int pending)
{
        struct my_object *obj = arg;

        /* All earlier tasks on this queue have completed. */
        free(obj, M_DEVBUF);
}

/* When we want to free the object: */
static struct task free_task;
TASK_INIT(&free_task, 0, my_free_task, obj);
taskqueue_enqueue(sc->tq, &free_task);
```

Atenção: a própria `struct task` precisa existir até que o callback dispare, o que significa embuti-la no objeto (e liberar a estrutura que a contém) ou alocá-la separadamente.

**Exemplo real.** `/usr/src/sys/dev/usb/usb_hub.c` utiliza teardown adiado quando um dispositivo USB é removido enquanto ainda está sendo utilizado por um driver mais acima na pilha.

### Padrão 7: Rollover de Estatísticas Agendado

**Problema.** O driver mantém estatísticas acumuladas que precisam ser convertidas em uma taxa por intervalo em fronteiras regulares. O rollover envolve tirar um snapshot dos contadores, calcular o delta e armazenar o resultado em um ring buffer. Isso pode ser feito em um callback de timer, mas o cálculo acessa estruturas de dados protegidas por um lock que pode dormir.

**Solução.** Uma `timeout_task` periódica que trata o rollover em contexto de thread. A task se reenfileira ao final de cada disparo para o próximo intervalo.

Isso é, na prática, um "callout baseado em taskqueue". É um pouco mais pesado do que um callout simples porque depende de uma combinação callout mais task, mas pode fazer coisas que um callout simples não consegue. Útil apenas quando o callout por si só não seria suficiente.

**Esboço de código.**

```c
static void
my_stats_rollover_task(void *arg, int pending)
{
        struct my_softc *sc = arg;

        sx_xlock(&sc->stats_sx);
        my_rollover_stats(sc);
        sx_xunlock(&sc->stats_sx);

        taskqueue_enqueue_timeout(sc->tq, &sc->stats_task, hz);
}
```

O reenfileiramento automático ao final mantém a task disparando uma vez por segundo. Um caminho de controle que deseja interromper o rollover cancela a timeout task.

**Exemplo real.** Vários drivers de rede utilizam este padrão para timers adjacentes ao watchdog cujo trabalho requer um lock que pode dormir.

### Padrão 8: `taskqueue_block` Durante Configuração Delicada

**Problema.** O driver está realizando uma mudança de configuração que não deve ser interrompida pela execução de uma task. Uma task que dispare no meio da configuração poderia observar um estado inconsistente.

**Solução.** `taskqueue_block(sc->tq)` antes da mudança de configuração; `taskqueue_unblock(sc->tq)` depois. Enquanto bloqueado, novos enfileiramentos se acumulam, mas nenhuma task é despachada. A task que já estiver em execução (se houver) é concluída naturalmente antes que o bloqueio entre em vigor.

**Esboço de código.**

```c
taskqueue_block(sc->tq);
/* ... reconfigure ... */
taskqueue_unblock(sc->tq);
```

`taskqueue_block` é rápido. Ele não aguarda a conclusão das tasks em execução; apenas impede o despacho de novas. Para garantir que nenhuma task está em execução no momento, combine-o com `taskqueue_quiesce`:

```c
taskqueue_block(sc->tq);
taskqueue_quiesce(sc->tq);
/* ... reconfigure ... */
taskqueue_unblock(sc->tq);
```

`taskqueue_quiesce` aguarda a conclusão da task em execução e o esvaziamento da fila pendente. Combinado com `block`, você tem a garantia de que nenhuma task está em execução e nenhuma task iniciará até que você desbloqueie.

**Exemplo real.** Alguns drivers Ethernet utilizam este padrão durante transições de estado de interface (link up, link down, mudança de mídia).

### Padrão 9: `taskqueue_drain_all` em um Limite de Subsistema

**Problema.** Um subsistema complexo precisa estar completamente quieto em um determinado ponto. Todas as suas tasks pendentes, incluindo as que podem ter sido enfileiradas por outras tasks pendentes, precisam ser concluídas antes que o subsistema prossiga.

**Solução.** `taskqueue_drain_all(tq)` esvazia todas as tasks na fila, aguarda a conclusão de todas as tasks em voo e retorna quando a fila está quieta.

`taskqueue_drain_all` não é um substituto para `taskqueue_drain` por task no detach (porque a fila pode ter tasks de outros caminhos que não devem ser esvaziados), mas é útil para pontos internos de sincronização em que você quer "tudo concluído, ponto final".

**Exemplo real.** `/usr/src/sys/dev/wg/if_wg.c` utiliza `taskqueue_drain_all` em seu taskqueue por peer durante a limpeza de peer.

### Padrão 10: Geração de Eventos Sintéticos em Nível de Simulação

**Problema.** Durante os testes, o driver deseja gerar eventos sintéticos que exercitem o caminho completo de processamento de eventos. Uma chamada de função direta ignoraria o escalonador, perderia as condições de corrida e não estressaria o mecanismo de taskqueue. Um evento de hardware real, é claro, não está disponível em um ambiente de teste.

**Solução.** Um handler de sysctl que enfileira uma task. O callback da task invoca a mesma rotina do driver que um evento real teria invocado. Como a task passa pelo taskqueue, o evento sintético tem a mesma forma de execução que um real: é executado em contexto de thread, observa o mesmo locking e passa pelo mesmo coalescing.

É exatamente isso que o sysctl `bulk_writer_flood` do `myfirst` faz. O padrão se aplica a qualquer driver que queira autotestar seus caminhos de trabalho adiado sem precisar de hardware real para gerar o evento disparador.

### Uma Seleção de Drivers Reais

Os padrões acima não foram inventados para o capítulo. Um breve tour por `/usr/src/sys/dev/` que você deve explorar por conta própria, com uma ordem sugerida:

- **`/usr/src/sys/dev/ale/if_ale.c`**: Um driver Ethernet pequeno e legível que utiliza um taskqueue privado, uma divisão filtro-mais-task e uma única interrupt task. Boa primeira leitura.
- **`/usr/src/sys/dev/age/if_age.c`**: Padrão semelhante, família de drivers ligeiramente diferente. Ler os dois reforça o padrão.
- **`/usr/src/sys/dev/bge/if_bge.c`**: Um driver Ethernet maior com múltiplas tasks (interrupt task, link task, reset task). Demonstra como múltiplas tasks se compõem em uma única fila.
- **`/usr/src/sys/dev/usb/usb_process.c`**: A fila de processos dedicada por dispositivo do USB (`usb_proc_*`). Demonstra como um subsistema envolve trabalho adiado no estilo de tasks para seu próprio domínio.
- **`/usr/src/sys/dev/wg/if_wg.c`**: WireGuard utiliza grouptaskqueues para criptografia por peer. Leitura avançada, mas útil quando os padrões básicos fazem sentido.
- **`/usr/src/sys/dev/iwm/if_iwm.c`**: Driver sem fio com múltiplas timeout tasks para calibração, varredura e gerenciamento de firmware.
- **`/usr/src/sys/kern/subr_taskqueue.c`**: A implementação em si. Ler `taskqueue_run_locked` uma vez torna tudo o mais concreto.

Vinte minutos com qualquer um desses arquivos valem uma hora de explicações do capítulo que você pode pular. Os padrões ficam visíveis à primeira olhada assim que você sabe o que procurar.

### Encerrando a Seção 6

A mesma API pequena se compõe em uma grande família de padrões. Log adiado, interrupções filtro-mais-task, `copyin`/`copyout` assíncrono, retentativa com backoff, teardown adiado, rollover de estatísticas, `block` durante reconfiguração, `drain_all` em limites de subsistemas, geração de eventos sintéticos: cada padrão é uma variação sobre "borda detecta, task age", e cada um é produtivo sempre que o driver que você está escrevendo ou lendo se encaixa no formato.

A Seção 7 se volta para o outro lado da mesma moeda: quando o padrão dá errado, como você percebe? Quais ferramentas o FreeBSD fornece para inspecionar o estado de um taskqueue e quais são os bugs comuns que essas ferramentas ajudam a diagnosticar?



## Seção 7: Depurando Taskqueues

A maior parte do código de taskqueue é curta. Os bugs comuns não são sutis: tasks que nunca disparam, tasks que disparam com frequência excessiva, tasks que disparam após o softc ser liberado, deadlocks contra o mutex do driver e esvaziamento no ponto errado da sequência de detach. Esta seção nomeia os bugs, mostra como observá-los e percorre uma quebra e correção deliberada no `myfirst` para que você possa praticar o fluxo de depuração com algo concreto à sua frente.

### As Ferramentas

Um breve panorama das ferramentas a que você vai recorrer.

**`procstat -t`**: lista todas as threads do kernel com seu nome, prioridade, estado e canal de espera. A thread trabalhadora de um taskqueue privado aparece como `<name> taskq`, onde `<name>` é o que você passou para `taskqueue_start_threads`. Uma thread presa em um canal de espera não trivial é uma pista: o nome do canal frequentemente diz o que a thread está aguardando.

**`ps ax`**: equivalente para a maior parte do que `procstat -t` mostra, com uma saída menos específica para taskqueues. O nome da thread do kernel aparece entre colchetes.

**`sysctl dev.<driver>`**: a própria árvore de sysctl do driver. Se você adicionou um contador como `selwake_pending_drops`, seu valor fica visível aqui. Sysctls de diagnóstico são a forma mais barata de observabilidade; adicione-os sempre que a pergunta "com que frequência este caminho dispara" puder ser relevante mais tarde.

**`dtrace(1)`**: o framework de rastreamento do kernel. A atividade do taskqueue é rastreável via probes FBT (function boundary tracing) em `taskqueue_enqueue` e `taskqueue_run_locked`. Um pequeno script D pode contar enfileiramentos, medir atrasos entre enfileiramento e despacho, e assim por diante.

**`ktr(4)`**: o rastreador de eventos do kernel. Habilitado em tempo de compilação em kernels de depuração, fornece um ring buffer de eventos do kernel que pode ser despejado após uma pane ou inspecionado ao vivo. Útil para análise post-mortem.

**`ddb(4)`**: o depurador interno do kernel. Breakpoints, stack traces, inspeção de memória. Acessível via `kgdb` após um kernel panic, ou interativamente após um `sysctl debug.kdb.enter=1` se você construiu um kernel com KDB habilitado.

**`INVARIANTS` e `WITNESS`**: asserções em tempo de compilação e verificador de ordem de locks. Não são ferramentas que você invoca, mas a primeira linha de defesa. Um kernel de depuração captura a maioria dos bugs de taskqueue na primeira vez que você os encontra.

Os laboratórios do Capítulo 14 exercitam `procstat -t`, `sysctl` e `dtrace` explicitamente. `ktr` e `ddb` são mencionados por completude.

### Bug Comum 1: A Task Nunca Executa

**Sintomas.** Você enfileira uma task a partir de um handler de sysctl ou callback de callout; o `device_printf` do callback da task nunca aparece; o driver, fora isso, parece funcionar normalmente.

**Causa provável.** `taskqueue_start_threads` não foi chamado, ou o ponteiro de taskqueue no qual você enfileirou é `NULL`.

**Como verificar.**

```text
# procstat -t | grep myfirst
```

Se nenhuma thread `myfirst taskq` aparecer na listagem, o taskqueue não existe ou não possui threads. Verifique o caminho do attach: `taskqueue_create` é chamado? O valor de retorno é armazenado? `taskqueue_start_threads` é chamado logo após?

```text
# dtrace -n 'fbt::taskqueue_enqueue:entry /arg0 != 0/ { @[stack()] = count(); }'
```

Se o stack trace mostrar enfileiramentos no taskqueue do driver, a tarefa está sendo submetida. Se nada aparecer, o caminho de código que deveria enfileirar a tarefa não está sendo alcançado. Rastreie para trás e descubra o motivo.

### Bug Comum 2: A Task Executa com Frequência Demais

**Sintomas.** O callback da task faz mais trabalho do que o esperado, ou o driver registra uma contagem estranha.

**Causa provável.** O callback não respeita o argumento `pending`, ou o callback reenfileira a si mesmo sem uma condição, de modo que, uma vez iniciado, executa em loop para sempre.

**Como verificar.** Adicione um contador ao callback:

```c
static void
myfirst_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        static int invocations;

        atomic_add_int(&invocations, 1);
        if ((invocations % 1000) == 0)
                device_printf(sc->dev, "task invocations=%d\n", invocations);
        /* ... */
}
```

Se o contador cresce mais rápido do que a taxa de disparo esperada, a task está em loop ou o coalescing não está ocorrendo. Inspecione os pontos de chamada do enqueue.

### Bug Comum 3: Use-After-Free no Callback da Task

**Sintomas.** Kernel panic com um stack trace terminando no callback da sua task, em um ponto que acessa o estado do softc. O panic pode ocorrer durante o detach ou logo após ele.

**Causa provável.** O caminho de detach liberou o softc (ou algo que a task toca) antes de drenar a task. Um enqueue tardio vindo de um callout ou outro contexto marginal foi disparado após a drenagem, e a task executou contra um estado já liberado.

**Como verificar.** Revise o caminho de detach em relação à ordenação da Seção 4. Especificamente:

1. `is_attached` deve ser zerado antes de os callouts serem drenados, para que os callbacks de callout saiam sem reenfileirar.
2. Os callouts devem ser drenados antes das tasks, de modo que nenhum enqueue possa ocorrer após a drenagem das tasks.
3. As tasks devem ser drenadas antes de o estado que elas tocam ser liberado.
4. `taskqueue_free` deve ser chamado depois que todas as tasks na fila forem drenadas.

Um erro de ordenação em qualquer um desses pontos é um potencial use-after-free.

O kernel de debug detecta muitos desses casos via asserções `INVARIANTS` nas rotinas `cbuf_*`, `cv_*` e `mtx_*`. Execute o caminho de detach sob carga com `WITNESS` habilitado; um bug frequentemente aparece de imediato.

### Bug Comum 4: Deadlock Entre a Task e o Mutex do Driver

**Sintomas.** Um `read(2)` ou `write(2)` no dispositivo trava para sempre. O callback da task fica esperando por um lock. O mutex do driver está sendo mantido por uma thread diferente.

**Causa provável.** O callback da task tenta adquirir um lock que a thread que enfileirou a task já mantém, criando um ciclo. Por exemplo:

- A Thread A mantém `sc->mtx` e chama uma função que enfileira uma task.
- O callback da task adquire `sc->mtx` antes de executar seu trabalho.
- A task não consegue prosseguir porque a Thread A ainda mantém o mutex.
- A Thread A espera a task concluir.

A parte "a Thread A espera a task concluir" não se encaixa na arquitetura do `myfirst` (o driver não espera explicitamente por tasks dentro de caminhos que mantêm mutex), mas é um padrão comum em outros drivers. Evite isso não drenando tasks enquanto mantém um lock de que elas precisam.

**Como verificar.**

```text
# procstat -kk <pid of stuck read/write thread>
# procstat -kk <pid of taskqueue thread>
```

Compare os stack traces. Se um mostra `mtx_lock`/`sx_xlock` no callback da task e o outro mostra `msleep_sbt`/`sleepqueue` em um ponto que mantém o mesmo lock, você tem um deadlock.

### Bug Comum 5: Drenagem Trava para Sempre

**Sintomas.** O detach trava, `kldunload` não retorna. A thread do taskqueue está presa em algum lugar.

**Causa provável.** O callback da task está esperando por uma condição que não pode ser satisfeita porque o caminho de drenagem está bloqueando o produtor. Ou o callback da task está esperando por um lock que o caminho de detach mantém.

**Como verificar.**

```text
# procstat -kk <pid of kldunload>
# procstat -kk <pid of taskqueue thread>
```

A drenagem está em `taskqueue_drain`, que está em `msleep`. A task está em alguma espera. Identifique o canal de espera; o nome frequentemente indica em que a task está bloqueada. Se a task está bloqueada em algo que o caminho de detach mantém, o design tem um ciclo.

Um caso específico comum: o callback da task chama `seldrain`, o caminho de detach também chama `seldrain`, e os dois colidem. Evite garantindo que `seldrain` seja chamado exatamente uma vez, no caminho de detach, após a drenagem das tasks.

### O Exercício de Quebrar e Corrigir

Um exercício guiado de introdução e correção de bugs. O driver do Stage 1 está correto; modificamo-lo para introduzir cada um dos bugs acima, observamos o sintoma e o corrigimos.

#### Variante quebrada 1: `taskqueue_start_threads` ausente

Remova a chamada `taskqueue_start_threads` do attach. Reconstrua, carregue, habilite a fonte de tick e execute o `poll_waiter`. Você vai observar: nenhum dado aparece no `poll_waiter`, mesmo que `sysctl dev.myfirst.0.tick_source_interval_ms` esteja configurado.

Verifique com `procstat -t`:

```text
# procstat -t | grep myfirst
```

Nenhuma thread `myfirst taskq` aparece. O taskqueue existe (você o criou), mas não tem workers. A `selwake_task` enfileirada fica na fila para sempre.

Correção: restaure a chamada `taskqueue_start_threads`. Reconstrua. Confirme que a thread aparece em `procstat -t` e que o `poll_waiter` recebe os dados.

#### Variante quebrada 2: drenagem na ordem errada

Mova a chamada `taskqueue_drain` no detach para acontecer antes da drenagem dos callouts:

```c
/* WRONG ORDER: */
taskqueue_drain(sc->tq, &sc->selwake_task);
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Reconstrua, carregue, habilite a fonte de tick em alta frequência, deixe os dados fluir por alguns segundos e então descarregue. Na maioria das vezes o descarregamento funciona. Ocasionalmente, o descarregamento entra em panic com um stack em `selwakeup` sendo chamado após `seldrain`. A condição de corrida é rara, mas real.

O problema: `taskqueue_drain` retornou, mas então um callout `tick_source` em voo disparou (ainda não havia sido drenado) e reenfileirou a task. A nova task disparou após `seldrain` ter executado e tentou chamar `selwakeup` em um selinfo já drenado.

Correção: restaure a ordem correta (callouts primeiro, depois tasks, depois `seldrain`). Reconstrua, verifique que a condição de corrida desapareceu sob o mesmo estresse.

#### Variante quebrada 3: callback da task mantém o mutex por tempo demais

Altere `myfirst_selwake_task` para manter o mutex durante a chamada a `selwakeup`:

```c
static void
myfirst_selwake_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_LOCK(sc);        /* WRONG: holds mutex across selwakeup */
        selwakeup(&sc->rsel);
        MYFIRST_UNLOCK(sc);
}
```

Reconstrua. Carregue com um kernel de debug. Habilite a fonte de tick. Em segundos o kernel entra em panic com uma reclamação do `WITNESS` sobre ordem de lock (ou em algumas configurações com uma falha de asserção dentro do próprio `selwakeup`).

O problema: `selwakeup` adquire um lock que não faz parte da ordem de lock documentada do driver. O `WITNESS` percebe e reclama.

Correção: o `myfirst_selwake_task` correto chama `selwakeup` sem manter nenhum mutex do driver. Restaure isso, reconstrua, verifique que não há avisos do WITNESS.

#### Variante quebrada 4: esquecer de drenar a task no detach

Remova a linha `taskqueue_drain(sc->tq, &sc->selwake_task)` do detach. Reconstrua. Carregue, habilite a fonte de tick em alta frequência, execute o `poll_waiter` e imediatamente descarregue o driver.

Na maioria das vezes o descarregamento conclui. Ocasionalmente, uma task que estava em voo no momento do descarregamento executa contra um softc cujo selinfo já foi drenado e liberado. O sintoma costuma ser um kernel panic ou uma corrupção de memória que aparece mais tarde como um crash não relacionado.

Correção: restaure a drenagem. Reconstrua, verifique que carregamentos e descarregamentos repetidos sob carga são estáveis.

#### Variante quebrada 5: ponteiro de taskqueue errado

Um bug sutil do Stage 2. Após migrar para o taskqueue privado, esqueça de atualizar a chamada `taskqueue_drain` no detach. Ela ainda aponta para `taskqueue_thread`:

```c
/* WRONG: enqueue on sc->tq but drain on taskqueue_thread */
taskqueue_enqueue(sc->tq, &sc->selwake_task);
/* ... in detach ... */
taskqueue_drain(taskqueue_thread, &sc->selwake_task);
```

Reconstrua. Carregue, habilite a fonte de tick, execute o waiter, descarregue. O descarregamento normalmente conclui sem erro, mas `taskqueue_drain(taskqueue_thread, ...)` não espera de fato pela task que está rodando em `sc->tq`. Se a task estiver em voo quando o detach prosseguir, temos use-after-free.

Correção: faça o enqueue e a drenagem usarem o mesmo ponteiro de taskqueue. Reconstrua, teste.

### Um One-Liner de DTrace

Um one-liner útil para qualquer driver que use taskqueue. Ele mede o tempo entre o enqueue e o dispatch para cada task no sistema:

```text
# dtrace -n '
  fbt::taskqueue_enqueue:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:entry /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

A saída é uma distribuição da latência entre enqueue e dispatch por processo. Execute-o enquanto o seu driver está produzindo tasks e, em seguida, pressione Ctrl-C para ver o histograma quantizado. Resultados típicos em uma máquina com pouca carga: dezenas de microssegundos. Sob carga: milissegundos. Se você vir segundos, algo está errado.

Um segundo one-liner útil mede a duração do callback da task:

```text
# dtrace -n '
  fbt::taskqueue_run_locked:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:return /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

Mesma estrutura, temporização diferente. Informa quanto tempo cada invocação de `taskqueue_run_locked` leva (que é a duração do callback mais uma pequena sobrecarga constante).

### Sysctls de Diagnóstico para Adicionar

Contadores úteis para adicionar a qualquer driver que use taskqueue, com custo mínimo e alto valor diagnóstico.

```c
int enqueues;           /* Total enqueues attempted. */
int pending_drops;      /* Enqueues that coalesced. */
int callback_runs;      /* Total callback invocations. */
int largest_pending;    /* Peak pending count observed. */
```

Atualize os contadores no caminho de enqueue e no callback:

```c
static void
myfirst_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        sc->callback_runs++;
        if (pending > sc->largest_pending)
                sc->largest_pending = pending;
        if (pending > 1)
                sc->pending_drops += pending - 1;
        /* ... */
}

/* Enqueue site: */
sc->enqueues++;
taskqueue_enqueue(sc->tq, &sc->task);
```

Exponha cada um como um sysctl somente de leitura. Sob carga normal, `enqueues == callback_runs + pending_drops`. `largest_pending` indica o pior momento de coalescing; se ele cresce, o taskqueue está ficando para trás do produtor.

Esses contadores custam algumas operações atômicas por enqueue. Em qualquer carga de trabalho realista, o custo é imperceptível. O valor diagnóstico é substancial.

### A Obrigação do Kernel de Debug

Um lembrete que vale repetir: execute cada alteração do Capítulo 14 em um kernel com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED`. A maioria dos bugs de taskqueue difíceis de encontrar em um kernel de produção é detectada instantaneamente em um kernel de debug. O custo de rodar um kernel de debug é uma pequena penalidade de desempenho e um build ligeiramente maior; o custo de não rodar um é uma tarde inteira de depuração sempre que algo der errado.

### Encerrando a Seção 7

Depurar taskqueues é uma habilidade pequena que se combina com as ferramentas que você já tem para depurar callouts, mutexes e cvs. `procstat -t` e `ps ax` mostram a thread. `sysctl` expõe contadores de diagnóstico. `dtrace` mede a latência de enqueue até dispatch e a duração do callback. `WITNESS` detecta violações de ordem de lock em tempo de execução. Os bugs comuns (task nunca executa, ordem de drenagem errada, disciplina de lock incorreta no callback, drenagem esquecida) são todos detectáveis com um checklist e um kernel de debug.

A Seção 8 consolida o trabalho do Capítulo 14 no Stage 4, o driver final. Estendemos o `LOCKING.md`, incrementamos a string de versão e auditamos o driver contra uma passagem completa de estresse.



## Seção 8: Refatorando e Versionando Seu Driver com Taskqueue

O Stage 4 é a etapa de consolidação. Ele não adiciona funcionalidades novas além do que o Stage 3 estabeleceu; ele aprimora a organização do código, atualiza a documentação, incrementa a versão e executa a varredura completa de regressão. Se os Stages 1 a 3 são onde você construiu o driver, o Stage 4 é onde você o entrega.

Esta seção percorre a consolidação. O código-fonte do driver é unificado em um único arquivo bem estruturado; o `LOCKING.md` ganha uma seção de Tasks; a string de versão avança para `0.8-taskqueues`; e a passagem final de regressão confirma que todos os comportamentos dos Capítulos 12 e 13 ainda funcionam corretamente junto com as adições do Capítulo 14.

### Organização dos Arquivos

O capítulo não divide o driver em vários arquivos `.c`. O `myfirst.c` permanece como a única unidade de tradução, com uma responsabilidade adicionada (as tasks) agrupada ao lado dos callouts correspondentes. Se o driver ficasse muito maior, uma divisão natural seria `myfirst_timers.c` para o código de callout e `myfirst_tasks.c` para o código de task, com declarações compartilhadas em `myfirst.h`. Para o tamanho atual, o arquivo único é mais fácil de ler.

Dentro de `myfirst.c`, a organização do Stage 4 é:

1. Includes e macros globais.
2. Estrutura softc.
3. Estrutura de file-handle.
4. Declaração cdevsw.
5. Helpers de buffer.
6. Helpers de espera em condition variable.
7. Handlers de sysctl, agrupados:
   - Sysctls de configuração (nível de debug, limite soft de bytes, apelido).
   - Sysctls de intervalo de timer (heartbeat, watchdog, fonte de tick).
   - Sysctls de task (reset atrasado, lote do bulk writer, flood do bulk writer).
   - Sysctls de estatísticas somente de leitura.
8. Callbacks de callout.
9. Callbacks de task.
10. Handlers cdev (open, close, read, write, poll, destrutor de handle).
11. Métodos de dispositivo (identify, probe, attach, detach).
12. Infraestrutura do módulo (driver, `DRIVER_MODULE`, versão).

Um comentário de bloco no topo do arquivo lista as seções principais, para que um novo leitor possa saltar para a área correta sem usar grep. Dentro de cada seção, a ordem é estabelecida primeiro: heartbeat antes de watchdog, antes da fonte de tick, para os callouts que compartilham essa ordem no esboço.

### A Atualização do `LOCKING.md`

O `LOCKING.md` do Capítulo 13 tinha seções para o mutex, as cvs, o sx e os callouts. O Capítulo 14 adiciona uma seção de Tasks.

```markdown
## Tasks

The driver owns one private taskqueue (`sc->tq`) and three tasks:

- `selwake_task` (plain): calls `selwakeup(&sc->rsel)`. Enqueued from
  `myfirst_tick_source` when a byte is written. Drained at detach after
  callouts are drained and before `seldrain`.
- `bulk_writer_task` (plain): writes a configured number of bytes to the
  cbuf, signals `data_cv`, calls `selwakeup(&sc->rsel)`. Enqueued from
  sysctl handlers and from the tick_source callback when
  `bulk_writer_batch` is non-zero. Drained at detach after callouts.
- `reset_delayed_task` (timeout_task): performs a delayed reset of the
  cbuf, counters, and configuration. Enqueued by the
  `reset_delayed` sysctl. Drained at detach.

The taskqueue is created in `myfirst_attach` with `taskqueue_create`
and one worker thread started at `PWAIT` priority via
`taskqueue_start_threads`. It is freed in `myfirst_detach` via
`taskqueue_free` after every task has been drained.

All task callbacks run in thread context. Each callback acquires
`sc->mtx` explicitly if it needs state protected by the mutex; the
taskqueue framework does not acquire driver locks automatically.

All task callbacks call `selwakeup(9)` (when they call it at all) with
no driver lock held. The rule is the same as for the `myfirst_read` /
`myfirst_write` paths: drop the mutex before `selwakeup`.

## Detach Ordering

The detach sequence is:

1. Refuse detach if `sc->active_fhs > 0` (EBUSY).
2. Clear `sc->is_attached` under `sc->mtx`.
3. Broadcast `data_cv` and `room_cv`.
4. Release `sc->mtx`.
5. Drain `heartbeat_co`, `watchdog_co`, `tick_source_co`.
6. Drain `selwake_task`, `bulk_writer_task`, `reset_delayed_task`
   (the last via `taskqueue_drain_timeout`).
7. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
8. `taskqueue_free(sc->tq)`.
9. Destroy cdev and cdev alias.
10. Free sysctl context.
11. Destroy cbuf, free counters.
12. Destroy `data_cv`, `room_cv`, `cfg_sx`, `mtx`.

Violating the order risks use-after-free in task callbacks, selinfo
accesses after drain, or taskqueue teardown while a task is still
running.
```

A atualização é explícita quanto à ordenação porque a ordenação é o principal ponto em que algo pode dar errado. Um leitor que herdar o driver de você e quiser adicionar uma nova task encontrará a disciplina existente claramente definida.

### A Atualização de Versão

A string de versão no código-fonte passa de `0.7-timers` para `0.8-taskqueues`:

```c
#define MYFIRST_VERSION "0.8-taskqueues"
```

E a string de probe do driver é atualizada:

```c
device_set_desc(dev, "My First FreeBSD Driver (Chapter 14 Stage 4)");
```

A versão fica visível pelo sysctl `hw.myfirst.version`, que foi estabelecido no Capítulo 12.

### A Passagem Final de Regressão

O Estágio 4 deve passar em todos os testes que os Estágios 1 a 3 passaram, além dos conjuntos de testes dos Capítulos 12 e 13. Uma sequência de passagem compacta:

1. **Compile de forma limpa** sob o kernel de depuração (`make clean && make`).
2. **Carregue** com `kldload ./myfirst.ko`.
3. **Testes unitários do Capítulo 11**: leitura, escrita, abertura, fechamento e reset básicos.
4. **Testes de sincronização do Capítulo 12**: leituras bloqueantes com limite, escritas bloqueantes com limite, leituras com timeout, configuração protegida por sx, broadcasts de cv no detach.
5. **Testes de timer do Capítulo 13**: o heartbeat dispara na taxa configurada, o watchdog detecta drenagem travada, a fonte de tick injeta bytes.
6. **Testes de task do Capítulo 14**:
   - `poll_waiter` enxerga dados quando a fonte de tick está ativa.
   - O contador `selwake_pending_drops` cresce sob carga.
   - `bulk_writer_flood` dispara a coalescência em um único callback.
   - `reset_delayed` dispara após o atraso configurado.
   - Rearmar `reset_delayed` substitui o prazo (apenas um disparo).
7. **Detach sob carga**: fonte de tick a 1 ms, `poll_waiter` em execução, `bulk_writer_flood` emitindo floods e, em seguida, descarga imediata. Deve ser limpo.
8. **Passagem com WITNESS**: todos os testes acima, sem avisos de `WITNESS` no `dmesg`.
9. **Passagem com lockstat**: execute o conjunto de testes sob `lockstat -s 5` para medir a contenção de lock. O mutex interno do taskqueue deve aparecer apenas brevemente.

Todos os testes devem passar. Se algum falhar, a causa é quase certamente uma regressão introduzida entre o Estágio 3 e o Estágio 4, não um problema pré-existente; os Estágios 1 a 3 são validados de forma independente antes de o Estágio 4 começar.

### Mantendo a Documentação Sincronizada

Três lugares onde a documentação deve refletir o Capítulo 14:

- O comentário no topo do arquivo `myfirst.c`. Atualize o bloco "locking strategy" para mencionar o taskqueue.
- `LOCKING.md`. Atualize conforme a subseção anterior.
- Qualquer `README.md` por capítulo em `examples/part-03/ch14-taskqueues-and-deferred-work/`. Descreva os entregáveis de cada estágio e como construí-los.

Atualizar a documentação parece um trabalho extra. Não é. O leitor do ano que vem (muitas vezes, o seu eu futuro) depende da documentação para reconstruir o design. Escrever agora, enquanto o design está fresco, é uma ordem de grandeza mais barato do que escrever depois.

### Uma Auditoria Final

Antes de encerrar o Estágio 4, realize uma auditoria rápida.

- [ ] Todo drain de callout acontece antes de todo drain de task no detach?
- [ ] Todo drain de task acontece antes de `seldrain`?
- [ ] `taskqueue_free` acontece após todos os tasks serem drenados?
- [ ] O caminho de falha do attach chama `taskqueue_free` na fila se uma etapa de inicialização posterior falhar?
- [ ] Todo ponto de chamada de enqueue aponta para o ponteiro de taskqueue correto (privado, não compartilhado)?
- [ ] Todo ponto de chamada de drain está emparelhado com seu ponto de chamada de enqueue (mesmo taskqueue, mesmo task)?
- [ ] Todo callback de task está livre de suposições de que "executo exatamente uma vez por enqueue"?
- [ ] Todo callback de task está livre de suposições de que "seguro um lock do driver na entrada"?
- [ ] `LOCKING.md` lista todo task e seu callback, tempo de vida e caminhos de enqueue?
- [ ] A string de versão reflete o novo estágio?

Um driver que passa por essa auditoria, mais a auditoria do Capítulo 13 da Seção 7 daquele capítulo, é um driver que você pode passar para outro engenheiro com confiança.

### Encerrando a Seção 8

O Estágio 4 é a consolidação. O código do driver está organizado. `LOCKING.md` está atualizado. A string de versão reflete a nova capacidade. O conjunto completo de regressão passa sob um kernel de depuração. A lista de verificação da auditoria está limpa.

O driver `myfirst` percorreu um longo caminho. No Capítulo 10, ele começou como um dispositivo de caracteres de abertura única que movia bytes por um buffer circular. O Capítulo 11 lhe deu acesso concorrente. O Capítulo 12 lhe deu bloqueio limitado, canais de cv e configuração protegida por sx. O Capítulo 13 lhe deu callouts para trabalho periódico e watchdog. O Capítulo 14 lhe dá trabalho diferido, que é a ponte entre contextos de borda e contexto de thread e a peça que faltava para um driver que eventualmente enfrentará interrupções de hardware reais.

O restante deste capítulo amplia um pouco a visão. A seção de tópicos adicionais apresenta `epoch(9)`, grouptaskqueues e taskqueues por CPU em nível introdutório. Os laboratórios práticos consolidam o material das Seções 3 a 8. Os exercícios desafio ampliam o horizonte do leitor. Uma referência de solução de problemas reúne os problemas comuns em um único lugar. Em seguida, o encerramento e a ponte para o Capítulo 15.



## Tópicos Adicionais: `epoch(9)`, Grouptaskqueues e Taskqueues por CPU

O corpo principal do Capítulo 14 ensinou os padrões de `taskqueue(9)` que um driver típico precisa. Três tópicos adjacentes merecem menção para leitores que eventualmente escreverão ou lerão drivers de rede, ou cujos drivers crescem a uma escala em que o simples taskqueue privado não é suficiente. Cada tópico é introduzido no nível de "saiba quando recorrer a ele". Os mecanismos completos pertencem a capítulos posteriores, especialmente os que cobrem drivers de rede na Parte 6 (Capítulo 28).

### `epoch(9)` em Uma Página

`epoch(9)` é um mecanismo de sincronização de leitura sem lock. Seu propósito é permitir que muitos leitores percorram uma estrutura de dados compartilhada de forma concorrente, sem adquirir nenhum lock exclusivo, enquanto garante que a estrutura de dados não desaparecerá sob seus pés.

O formato é este. O código que lê dados compartilhados entra em uma "seção de epoch" com `epoch_enter(epoch)` e a sai com `epoch_exit(epoch)`. Dentro da seção, os leitores podem desreferenciar ponteiros livremente. Escritores que querem modificar ou liberar um objeto compartilhado não o fazem diretamente; em vez disso, chamam `epoch_wait(epoch)` para bloquear até que todo leitor atual tenha saído da seção de epoch, ou registram um callback via `epoch_call(epoch, cb, ctx)` que é executado de forma assíncrona após todos os leitores atuais terem saído.

O benefício é a escalabilidade. Os leitores não pagam nenhum custo de operação atômica; eles simplesmente registram o estado local da thread na entrada e na saída. Os escritores pagam o custo de sincronização, mas escritas são raras em comparação com leituras, portanto o custo amortizado é baixo. Para estruturas de dados percorridas por muitas threads e modificadas apenas ocasionalmente, `epoch(9)` supera substancialmente os reader-writer locks.

O custo é disciplina. O código dentro de uma seção de epoch não deve dormir, não deve adquirir locks que permitam dormir e não deve chamar funções que possam fazer qualquer uma dessas coisas. Escritores que usam `epoch_wait` bloqueiam até que todo leitor atual tenha saído, o que significa que o escritor pode estar esperando por potencialmente muitos leitores.

Drivers de rede usam `epoch(9)` intensivamente. O epoch `net_epoch_preempt` protege leituras do estado de rede (listas de ifnet, entradas de roteamento, flags de interface). Um caminho de entrada de pacotes entra no epoch, percorre o estado e sai do epoch. Um escritor que quer remover uma interface adia a liberação via `NET_EPOCH_CALL` e a liberação ocorre em um mecanismo semelhante ao taskqueue assim que todos os leitores terminam.

Para a conexão com o taskqueue: quando um task é inicializado com `NET_TASK_INIT` em vez de `TASK_INIT`, o taskqueue executa o callback dentro do epoch `net_epoch_preempt`. O callback do task pode, portanto, percorrer o estado de rede sem entrar explicitamente no epoch. Da implementação em `/usr/src/sys/kern/subr_taskqueue.c`:

```c
if (!in_net_epoch && TASK_IS_NET(task)) {
        in_net_epoch = true;
        NET_EPOCH_ENTER(et);
} else if (in_net_epoch && !TASK_IS_NET(task)) {
        NET_EPOCH_EXIT(et);
        in_net_epoch = false;
}
task->ta_func(task->ta_context, pending);
```

O despachante do taskqueue percebe a flag `TASK_NETWORK` e entra ou sai do epoch ao redor do callback conforme necessário. Tasks de rede consecutivos compartilham uma única entrada de epoch, o que é uma pequena otimização que o framework faz de graça.

Para o `myfirst`, isso não é relevante. O driver não toca o estado de rede. Mas se você escrever depois um driver de rede ou ler código de driver de rede, `NET_TASK_INIT` e `TASK_IS_NET` são as macros que indicam que um task tem consciência de epoch.

### Grouptaskqueues em Uma Página

Um grouptaskqueue é uma generalização escalável de um taskqueue. A ideia básica: em vez de uma única fila com um pool único (ou pequeno) de workers, distribua os tasks por muitas filas por CPU, cada uma atendida por sua própria thread de trabalho. Um "grouptask" é um task que se vincula a uma dessas filas.

O cabeçalho é `/usr/src/sys/sys/gtaskqueue.h`:

```c
#define GROUPTASK_INIT(gtask, priority, func, context)   \
    GTASK_INIT(&(gtask)->gt_task, 0, priority, func, context)

#define GROUPTASK_ENQUEUE(gtask)                         \
    grouptaskqueue_enqueue((gtask)->gt_taskqueue, &(gtask)->gt_task)

void    taskqgroup_attach(struct taskqgroup *qgroup,
            struct grouptask *grptask, void *uniq, device_t dev,
            struct resource *irq, const char *name);
int     taskqgroup_attach_cpu(struct taskqgroup *qgroup,
            struct grouptask *grptask, void *uniq, int cpu, device_t dev,
            struct resource *irq, const char *name);
void    taskqgroup_detach(struct taskqgroup *qgroup, struct grouptask *gtask);
```

Um driver que usa grouptasks faz o seguinte no attach:

1. Inicialize cada grouptask com `GROUPTASK_INIT`.
2. Vincule cada grouptask a um `taskqgroup` com `taskqgroup_attach` ou `taskqgroup_attach_cpu`. O vínculo atribui o grouptask a uma fila por CPU específica e a seu worker.
3. No momento do evento, enfileire com `GROUPTASK_ENQUEUE`.
4. No detach, `taskqgroup_detach` desassocia o grouptask.

Por que usar grouptasks em vez de tasks simples? Dois motivos.

Primeiro, **escalabilidade com a contagem de CPUs**. Um taskqueue com uma única thread é um gargalo quando muitos produtores em diferentes CPUs enfileiram de forma concorrente. O mutex interno do taskqueue fica disputado. Um grouptaskqueue com filas por CPU permite que cada CPU enfileire em sua própria fila sem contenção entre CPUs.

Segundo, **localidade de cache**. Quando uma interrupção dispara na CPU N e enfileira um grouptask vinculado à CPU N, o task é executado na mesma CPU que processou a interrupção. Os dados do task já estão nos caches daquela CPU. Para drivers de rede de alta taxa, isso é um ganho de desempenho substancial.

O custo é complexidade. Grouptaskqueues exigem mais configuração, mais desmontagem e mais reflexão sobre a qual fila um task pertence. Para a maioria dos drivers, esse custo não se justifica. Para um driver Ethernet de alto desempenho que processa milhões de pacotes por segundo, o custo se paga sozinho.

`myfirst` não usa grouptasks. Não traria benefício algum. Mencionamos para que, quando você ler um driver como `/usr/src/sys/dev/wg/if_wg.c` ou `/usr/src/sys/net/iflib.c`, as macros pareçam familiares.

### Taskqueues por CPU em Uma Página

Um taskqueue por CPU é a versão simples da ideia do grouptaskqueue: um taskqueue por CPU, cada um com sua própria thread de trabalho. O driver cria N taskqueues (um por CPU), vincula cada um a uma CPU específica com `taskqueue_start_threads_cpuset` e despacha tasks para a fila apropriada com base na regra de localidade que o driver preferir.

A primitiva principal é `taskqueue_start_threads_cpuset`:

```c
int taskqueue_start_threads_cpuset(struct taskqueue **tqp, int count,
    int pri, cpuset_t *mask, const char *name, ...);
```

É como `taskqueue_start_threads`, mas com um `cpuset_t` descrevendo em quais CPUs as threads podem ser executadas. Para um vínculo com uma única CPU, a máscara tem exatamente um bit definido. Para flexibilidade com múltiplas CPUs, a máscara tem múltiplos bits.

Um driver que usa taskqueues por CPU normalmente mantém um array de ponteiros de taskqueue indexado por CPU:

```c
struct my_softc {
        struct taskqueue *per_cpu_tq[MAXCPU];
        ...
};

for (int i = 0; i < mp_ncpus; i++) {
        CPU_SETOF(i, &mask);
        sc->per_cpu_tq[i] = taskqueue_create("per_cpu", M_WAITOK,
            taskqueue_thread_enqueue, &sc->per_cpu_tq[i]);
        taskqueue_start_threads_cpuset(&sc->per_cpu_tq[i], 1, PWAIT,
            &mask, "%s cpu%d", device_get_nameunit(sc->dev), i);
}
```

E no momento do enfileiramento, escolhe a fila correspondente à CPU atual:

```c
int cpu = curcpu;
taskqueue_enqueue(sc->per_cpu_tq[cpu], &task);
```

Os benefícios são os mesmos dos grouptasks, sem o framework de grouptask: o trabalho permanece na CPU em que foi produzido, a contenção local à CPU é eliminada e os caches permanecem quentes. O custo é que o driver gerencia sua própria estrutura de dados por CPU.

Para o `myfirst`, isso é exagero. Para um driver cuja taxa de eventos excede dezenas de milhares de eventos por segundo, os taskqueues por CPU valem a consideração. Grouptaskqueues são mais gerais e geralmente preferidos quando a escalabilidade importa; os taskqueues por CPU são a alternativa mais leve.

### Quando Usar Qual

Uma árvore de decisão resumida.

- **Baixa taxa, contexto de thread, fila compartilhada é suficiente**: use `taskqueue_thread`. O mais simples.
- **Baixa taxa, contexto de thread, isolamento importa**: taskqueue privado com `taskqueue_create` e `taskqueue_start_threads`. O que `myfirst` usa.
- **Alta taxa, contenção é o gargalo**: taskqueues por CPU ou grouptaskqueues. Comece com taskqueues por CPU; recorra aos grouptasks se precisar dos recursos extras de escalabilidade.
- **Dados no caminho de rede**: `NET_TASK_INIT` e grouptaskqueues, seguindo os padrões dos drivers de rede.
- **Contexto de interrupção de filtro, deve enfileirar sem dormir**: `taskqueue_create_fast` ou `taskqueue_fast`, pois interrupções de filtro não podem usar um mutex que permita dormir.

A maioria dos drivers que você escrever ou ler se enquadrará em uma das duas primeiras linhas. Os demais são casos especializados, que seus respectivos capítulos abordarão em detalhes.

### Encerrando os Tópicos Adicionais

`epoch(9)`, grouptaskqueues e taskqueues por CPU são a história de escalabilidade das taskqueues. Eles compartilham o mesmo modelo mental da API básica: enfileirar a partir de um produtor, despachar em um worker, respeitar a disciplina de lock, drenar no teardown. As diferenças estão em quantas filas existem e em como as tarefas são distribuídas entre elas. Para a maioria dos drivers a API básica é suficiente; essas variantes avançadas existem para os casos em que ela não é.

O capítulo avança agora para os laboratórios práticos.



## Laboratórios Práticos

Os laboratórios consolidam o material do capítulo em quatro exercícios práticos. Cada laboratório usa o driver que você foi evoluindo ao longo dos Estágios 1 a 4, além de alguns pequenos auxiliares em espaço do usuário fornecidos em `examples/part-03/ch14-taskqueues-and-deferred-work/labs/`.

Reserve uma sessão para cada laboratório. Se o tempo for limitado, os Laboratórios 1 e 2 são os mais importantes; os Laboratórios 3 e 4 valem a pena, mas são mais elaborados.

### Laboratório 1: Observando a Thread Worker de uma Taskqueue

**Objetivo.** Confirmar que a taskqueue privada do driver possui uma thread worker, que essa thread dorme quando não há trabalho, e que ela acorda e executa o callback quando uma tarefa é enfileirada.

**Configuração.** Carregue o driver do Estágio 2 (ou do Estágio 4, ambos funcionam). Certifique-se de que nenhum outro processo usando taskqueue esteja sobrecarregando o sistema; quanto mais silencioso o sistema, mais fácil é a observação.

**Passos.**

1. Execute `procstat -t | grep myfirst`. Registre o PID e o TID exibidos. A thread deve estar no estado `sleep`.
2. Execute `sysctl dev.myfirst.0.heartbeat_interval_ms=1000`. Aguarde alguns segundos.
3. Execute `procstat -t | grep myfirst` novamente. A thread pode aparecer brevemente no estado `run` durante um disparo do heartbeat; na maior parte do tempo ela ainda estará em `sleep`, porque o heartbeat não enfileira uma tarefa. Confirme se é isso que você vê. Note que o heartbeat é executado na thread do callout, não na thread da taskqueue do driver.
4. Execute `sysctl dev.myfirst.0.tick_source_interval_ms=100`. Aguarde alguns segundos.
5. Execute `procstat -t | grep myfirst` novamente. A thread deve agora oscilar entre `sleep` e `run`, porque o tick source está enfileirando uma tarefa dez vezes por segundo.
6. Pare o tick source com `sysctl dev.myfirst.0.tick_source_interval_ms=0`. Confirme que a thread retorna ao estado permanente de `sleep`.
7. Pare o heartbeat. Descarregue o driver. Confirme que a thread desaparece do `procstat -t`.

**Resultado esperado.** Você observou diretamente o ciclo de vida da thread: criada no attach, dorme quando ociosa, executa quando despachada, destruída no detach. A observação vale mais do que as duas páginas de explicação que a produziram.

### Laboratório 2: Medindo o Coalescing Sob Carga

**Objetivo.** Produzir uma carga de trabalho que estresse a taskqueue o suficiente para acionar o coalescing, e então medir a taxa de coalescing usando o sysctl `selwake_pending_drops`.

**Configuração.** Carregue o driver do Estágio 4. Compile o `poll_waiter` conforme descrito na Seção 3.

**Passos.**

1. Inicie o `poll_waiter` em um terminal: `./poll_waiter > /dev/null`. Redirecionar para `/dev/null` evita que o terminal se torne um gargalo.
2. Em um segundo terminal, defina o tick source para uma taxa alta: `sysctl dev.myfirst.0.tick_source_interval_ms=1`.
3. Aguarde dez segundos.
4. Leia `sysctl dev.myfirst.0.stats.selwake_pending_drops`. Registre o valor.
5. Aguarde mais dez segundos e leia novamente. Calcule a taxa por segundo.
6. Aumente a taxa do tick source para verificar se o coalescing aumenta: o intervalo mínimo do tick source é 1 ms, mas você pode combinar com o sysctl `bulk_writer_flood` para produzir cargas mais em rajada:
   ```text
   # for i in $(seq 1 100); do sysctl dev.myfirst.0.bulk_writer_flood=1000; done
   ```
7. Leia `selwake_pending_drops` após o flood.

**Resultado esperado.** O número cresce com o tempo, mais sob carga em rajada, menos sob carga contínua. Se o número permanecer em zero mesmo sob carga agressiva, a thread da taskqueue é rápida o suficiente para acompanhar o ritmo; esse é um bom estado, não um bug.

**Variação.** Execute a mesma carga com um kernel de depuração (`WITNESS` habilitado) e observe se o `dmesg` exibe algum aviso do `WITNESS`. Não deveria.

### Laboratório 3: Verificando a Ordem do Detach

**Objetivo.** Confirmar que o caminho de detach drena corretamente as tarefas antes de liberar o estado que essas tarefas acessam. Introduza deliberadamente o bug da Seção 7 (drenagem de tarefas após `seldrain`) e observe a condição de corrida.

**Configuração.** Parta do Estágio 4. Faça uma cópia de trabalho de `myfirst.c`.

**Passos.**

1. Na sua cópia de trabalho, reordene as drenagens em `myfirst_detach` de modo que `seldrain` venha antes de `taskqueue_drain`:
   ```c
   /* BROKEN ORDER: */
   MYFIRST_CO_DRAIN(&sc->heartbeat_co);
   MYFIRST_CO_DRAIN(&sc->watchdog_co);
   MYFIRST_CO_DRAIN(&sc->tick_source_co);
   seldrain(&sc->rsel);
   seldrain(&sc->wsel);
   taskqueue_drain(sc->tq, &sc->selwake_task);
   /* ... rest ... */
   ```
   Isso está intencionalmente errado.
2. Recompile com a ordem incorreta.
3. Carregue o driver. Habilite o tick source em 1 ms. Execute o `poll_waiter`.
4. Após alguns segundos de fluxo de dados, descarregue o driver: `kldunload myfirst`.
5. Na maioria das vezes o descarregamento é bem-sucedido. Ocasionalmente, especialmente sob carga, o kernel entra em pânico. A pilha do pânico normalmente inclui `selwakeup` sendo chamado a partir de `myfirst_selwake_task`, depois que `seldrain` já foi executado.
6. Restaure a ordem correta. Recompile. Execute o mesmo estresse e repita o descarregamento muitas vezes.
7. Confirme que a ordem correta nunca causa pânico.

**Resultado esperado.** Você vivenciou a condição de corrida diretamente. A lição é que "funciona na maioria das vezes" não é o mesmo que "funciona". A ordem correta é um invariante que você preserva mesmo quando a ordem incorreta parece funcionar em testes casuais.

**Observação.** Em um kernel de produção o pânico pode não ocorrer; a corrupção de memória pode ficar oculta até que outra coisa quebre. Sempre execute esses experimentos em um kernel de depuração com `INVARIANTS` e `WITNESS` habilitados.

### Laboratório 4: Coalescing vs. Batching Adaptativo

**Objetivo.** Construir uma pequena modificação que usa o argumento `pending` para conduzir batching adaptativo e comparar seu comportamento com a tarefa `bulk_writer_task` de batch fixo do Estágio 3.

**Configuração.** Parta do Estágio 4.

**Passos.**

1. Adicione uma nova tarefa ao driver: `adaptive_writer_task`. Seu callback escreve `pending` bytes (limitado a 64) no buffer. Use o padrão da Seção 5.
2. Adicione um sysctl que enfileira `adaptive_writer_task` sob demanda:
   ```c
   static int
   myfirst_sysctl_adaptive_enqueue(SYSCTL_HANDLER_ARGS)
   {
           struct myfirst_softc *sc = arg1;
           int n = 0, i, error;

           error = sysctl_handle_int(oidp, &n, 0, req);
           if (error || req->newptr == NULL)
                   return (error);
           for (i = 0; i < n; i++)
                   taskqueue_enqueue(sc->tq, &sc->adaptive_writer_task);
           return (0);
   }
   ```
3. Inicialize a tarefa no attach e a drene no detach.
4. Recompile e carregue.
5. Emita 1000 enfileiramentos via sysctl: `sysctl dev.myfirst.0.adaptive_enqueue=1000`.
6. Leia `sysctl dev.myfirst.0.stats.bytes_written`. Observe quantos bytes foram escritos.
7. Compare com o `bulk_writer_flood` do Estágio 3 usando `bulk_writer_batch=1`. O batch fixo escreveria 1 byte (coalescing em um único disparo). O batch adaptativo escreve o que `pending` valesse, até 64.

**Resultado esperado.** A tarefa adaptativa escreve mais bytes sob carga em rajada porque usa a informação de coalescing que o kernel já calculou. Para cargas em que o trabalho por evento deve escalar com a contagem de eventos, esse padrão é preferível a um tamanho de batch fixo.

**Variação.** Adicione um contador que registra o maior valor de `pending` já visto. Exponha-o como um sysctl. Sob estresse, você verá o pico de pending crescer conforme a carga aumenta.



## Exercícios Desafio

Os desafios são extensões opcionais. Eles levam os padrões estabelecidos neste capítulo para um território que o texto principal não cobriu. Tome seu tempo com eles; o objetivo é consolidar o entendimento, não introduzir material novo.

### Desafio 1: Tarefa por Handle de Arquivo

Modifique o driver para que cada handle de arquivo aberto tenha sua própria tarefa. O trabalho da tarefa, quando enfileirada, é emitir uma linha de log identificando o handle. Escreva um sysctl que enfileire a tarefa de cada handle simultaneamente.

Dicas:
- A `struct myfirst_fh` alocada em `myfirst_open` é o local natural para a tarefa por handle.
- Inicialize a tarefa em `myfirst_open` após o `malloc`.
- Drene a tarefa em `myfirst_fh_dtor` antes do `free`.
- Enfileirar "a tarefa de cada handle" requer uma lista de handles abertos. `devfs_set_cdevpriv` não mantém essa lista; você terá que construí-la no softc, protegida pelo mutex.

Resultado esperado: uma demonstração de ownership de tarefas em uma granularidade mais fina que a do driver. O desafio testa seu entendimento sobre ordenação de tempo de vida.

### Desafio 2: Pipeline de Duas Tarefas

Adicione um pipeline com duas tarefas. A Tarefa A recebe dados do handler `write(2)`, os transforma (para simplificar, converte cada byte para maiúsculo) e enfileira a Tarefa B. A Tarefa B escreve os dados transformados em um buffer secundário e notifica os waiters.

Dicas:
- O trabalho de transformação ocorre no callback da tarefa, em contexto de thread. O handler `write(2)` não deve bloquear aguardando a transformação.
- Você precisará de uma pequena fila de transformações pendentes, protegida pelo mutex.
- A Tarefa A consome da fila, transforma e enfileira a Tarefa B com o estado por item. Alternativamente, a Tarefa B é executada uma vez a cada enfileiramento de A e processa tudo o que estiver na fila.

Resultado esperado: um modelo mental de como taskqueues podem formar pipelines, com cada estágio sendo executado em sua própria invocação. É assim que drivers complexos dividem o trabalho.

### Desafio 3: Ordenação de Tarefas por Prioridade

Adicione duas tarefas com prioridades diferentes ao driver. A `urgent_task` tem prioridade 10 e imprime "URGENT". A `normal_task` tem prioridade 0 e imprime "normal". Escreva um handler de sysctl que enfileire ambas as tarefas, primeiro a normal, depois a urgente.

Resultado esperado: a saída do `dmesg` mostra `URGENT` antes de `normal`, confirmando que a prioridade sobrepõe a ordem de enfileiramento dentro da fila.

### Desafio 4: Reconfiguração Bloqueante

Implemente um caminho de reconfiguração que usa `taskqueue_block` e `taskqueue_quiesce`. O caminho deve:

1. Bloquear a taskqueue.
2. Quiescer (aguardar as tarefas em execução terminarem).
3. Executar a reconfiguração (por exemplo, redimensionar o buffer circular).
4. Desbloquear.

Verifique com `dtrace` que nenhuma tarefa é executada durante a janela de reconfiguração.

Resultado esperado: experiência prática com `taskqueue_block` e `taskqueue_quiesce`, e compreensão de quando esses primitivos são apropriados.

### Desafio 5: Taskqueue Multithread

Modifique o Estágio 4 para usar uma taskqueue privada multithread (por exemplo, quatro threads worker em vez de uma). Execute o teste de coalescing do Laboratório 2. Observe o que muda.

Resultado esperado: sob carga, a taxa de coalescing diminui porque múltiplas threads worker drenam a fila mais rapidamente. Sob carga muito leve, nada muda de forma visível. O desafio mostra como a configuração da taskqueue troca desempenho sob diferentes cargas de trabalho.

### Desafio 6: Implementando um Watchdog com uma Timeout Task

Reimplemente o watchdog do Capítulo 13 usando uma `timeout_task` em vez de um callout simples. Cada disparo do watchdog reenfileira a si mesmo com o intervalo configurado. Uma operação de "kick" (outro sysctl, talvez `watchdog_kick`) cancela e reenfileira a timeout task para reiniciar o timer.

Resultado esperado: compreensão de como o primitivo `timeout_task` pode substituir um callout para trabalho periódico, e de quando cada um é preferível. (Resposta: timeout_task quando o trabalho precisa de contexto de thread; callout nos demais casos.)

### Desafio 7: Carregar um Driver Real e Ler seu Código

Escolha um dos drivers listados na Seção 6 (`/usr/src/sys/dev/ale/if_ale.c`, `/usr/src/sys/dev/age/if_age.c`, `/usr/src/sys/dev/bge/if_bge.c` ou `/usr/src/sys/dev/iwm/if_iwm.c`). Leia o uso de taskqueue nele. Identifique:

- Quais tarefas o driver possui.
- Onde cada tarefa é inicializada.
- Onde cada tarefa é enfileirada.
- Onde cada tarefa é drenada.
- Se o driver usa `taskqueue_create` ou `taskqueue_create_fast`.
- Qual prioridade de thread o driver usa.

Escreva um breve resumo (uma página aproximadamente) de como o driver usa a API de taskqueue. Guarde-o como referência.

Resultado esperado: a leitura de um driver real transforma o reconhecimento de padrões de abstrato em concreto. Depois de fazer isso uma vez com um driver, ler o próximo é dramaticamente mais rápido.



## Referência de Solução de Problemas

Uma lista de referência rápida com sintomas e soluções, para o momento em que um bug surge e você precisa da resposta rapidamente. Use esta referência em conjunto com as listas de erros comuns dentro de cada seção; juntas elas cobrem a maioria dos problemas reais.

### A tarefa nunca é executada

- **Você chamou `taskqueue_start_threads` após `taskqueue_create`?** Sem threads, a fila aceita enfileiramentos mas nunca os despacha.
- **O ponteiro do taskqueue é `NULL` no momento do enfileiramento?** Verifique o caminho de attach; `taskqueue_create` pode ter falhado silenciosamente se você não verificou seu valor de retorno.
- **O `is_attached` do driver é false quando o enfileiramento ocorre?** Alguns caminhos de código (como os callbacks de callout do Capítulo 13) saem antecipadamente se `is_attached` for false; se essa saída ocorrer antes do enfileiramento, a task não executa.
- **O taskqueue está bloqueado via `taskqueue_block`?** Nesse caso, ele aceita enfileiramentos mas não despacha. Desbloqueie-o.

### A tarefa executa duas vezes quando você esperava uma

- **A tarefa está se reenfileirando sozinha?** Um callback de tarefa que chama `taskqueue_enqueue` em si mesmo vai entrar em loop indefinidamente, a menos que o callback saia antecipadamente sob alguma condição.
- **Existe outro caminho de código também enfileirando?** Verifique cada ponto de chamada de `taskqueue_enqueue` para a tarefa. Duas fontes enfileirando produzirão a execução dupla esperada dependendo do momento.

### A tarefa executa uma vez quando você esperava duas ou mais

- **Seus enfileiramentos coalesceram?** A regra de enfileiramento-já-pendente une submissões redundantes. Se você precisar de semântica de exatamente-um-por-evento, use tarefas separadas ou uma fila por evento.
- **O argumento `pending` é reportado como maior que um?** Se for, o framework realizou a coalescência.

### Kernel panic no callback da tarefa

- **O callback está acessando estado já liberado?** A causa mais comum de um panic em um callback de tarefa é use-after-free. Verifique a ordem de detach: todo produtor de enfileiramentos deve ser drenado antes que a tarefa seja drenada; o estado que a tarefa acessa não deve ser liberado antes que a tarefa seja drenada.
- **O callback está segurando um lock que não deveria?** O `WITNESS` captura a maioria desses casos. Execute sob um kernel de debug e leia o `dmesg`.
- **O callback está chamando `selwakeup` com um mutex do driver em posse?** Não faça isso. `selwakeup` adquire seu próprio lock e não deve ser chamado com locks do driver não relacionados em posse.

### O detach trava

- **O `taskqueue_drain` está aguardando uma tarefa que não consegue terminar?** Verifique com `procstat -kk` o estado do worker do taskqueue. Se ele estiver em uma espera que depende de algo que o caminho de detach está segurando, o design tem um ciclo.
- **O `taskqueue_free` está aguardando tarefas que ainda estão sendo enfileiradas?** Verifique `is_attached`: se os callouts ainda estiverem rodando e ainda enfileirando, a drenagem não terminará. Certifique-se de que os callouts sejam drenados primeiro.

### `kldunload` retorna EBUSY imediatamente

- **Existe algum file descriptor ainda aberto?** O caminho de detach em `myfirst_detach` rejeita com `EBUSY` se `active_fhs > 0`. Feche quaisquer descritores abertos e tente novamente.

### A contagem de coalescência permanece em zero

- **A carga de trabalho é muito leve?** A coalescência ocorre apenas quando o produtor supera o consumidor. Em uma máquina com pouca carga, isso raramente acontece.
- **Sua medição está correta?** A coalescência é contada no callback, não no caminho de enfileiramento. Verifique sua lógica de contador.
- **O taskqueue é multi-threaded?** Mais threads significam consumo mais rápido e menos coalescência.

### A thread do taskqueue privado não aparece no `procstat -t`

- **`taskqueue_start_threads` retornou zero?** Se retornou um erro, as threads não foram criadas. Verifique o valor de retorno.
- **O driver está realmente carregado?** `kldstat` confirma.
- **O nome da thread é diferente do que você esperava?** A string de formato passada para `taskqueue_start_threads` controla o nome; certifique-se de que está buscando pelo nome correto.

### Deadlock entre a tarefa e o mutex do driver

- **O callback da tarefa adquire um lock que outra thread segura enquanto aguarda a tarefa?** Esse é o formato clássico de deadlock. Resolva movendo o enfileiramento da tarefa para fora da seção com lock em posse, ou reestruturando a espera para que ela não bloqueie na tarefa.

### `taskqueue_enqueue` falha com `EEXIST`

- **Você passou `TASKQUEUE_FAIL_IF_PENDING` e a tarefa já estava pendente.** A falha é intencional; verifique se esse flag é realmente o que você deseja.

### `taskqueue_enqueue_timeout` parece não disparar

- **O taskqueue está bloqueado?** Filas bloqueadas também não despacham tarefas de timeout.
- **A contagem de ticks é razoável?** Uma contagem de ticks igual a zero dispara imediatamente, mas uma conversão não inteira de milissegundos para ticks pode produzir atrasos inesperadamente longos. Use `(ms * hz + 999) / 1000` para arredondamento para cima.
- **A tarefa de timeout foi cancelada por um `taskqueue_cancel_timeout`?** Se sim, reenfileire-a.

### Rearmar um `timeout_task` não está substituindo o prazo

- **Cada `taskqueue_enqueue_timeout` substitui o prazo pendente.** Se o seu driver o chama várias vezes e apenas o primeiro parece ter efeito, você pode ter um problema de ordenação: tem certeza de que as chamadas subsequentes estão ocorrendo?

### O WITNESS reclama de uma ordem de lock envolvendo `tq_mutex`

- **O mutex interno do taskqueue está entrando na ordem de lock do seu driver.** Geralmente porque um callback de tarefa adquire um lock do driver, e algum outro caminho de código adquire esse lock do driver primeiro e depois enfileira.
- **A resolução geralmente é enfileirar antes de adquirir o lock do driver, ou reestruturar o código para que os dois locks nunca sejam mantidos na mesma thread na ordem errada.**

### `procstat -kk` mostra a thread do taskqueue dormindo em um lock

- **Um callback de tarefa está bloqueado em um lock que admite sleep.** Identifique o lock a partir do canal de espera. Verifique se o detentor desse lock também está aguardando algo; se sim, você tem uma cadeia de dependência.

### Os callbacks de tarefa estão lentos

- **Perfile com `dtrace`.** O one-liner da Seção 7 mede a duração do callback.
- **O callback está segurando um lock durante operações longas?** Mova a operação longa para fora do lock.
- **O callback está realizando IO síncrono?** Isso pertence aos handlers de `read(2)` / `write(2)` / `ioctl(2)`, não em callbacks de taskqueue, a menos que o IO seja genuinamente o objetivo da tarefa.

### Deadlock de taskqueue durante o boot

- **Você está enfileirando tarefas a partir de um `SI_SUB` que executa antes de `SI_SUB_TASKQ`?** Os taskqueues predefinidos são inicializados em `SI_SUB_TASKQ`. Handlers de `SI_SUB` anteriores não podem enfileirar neles.



## Encerrando

O Capítulo 14 ensinou um primitivo em profundidade. O primitivo é `taskqueue(9)`. Seu propósito é mover trabalho para fora de contextos que não conseguem realizá-lo e para dentro de um contexto que pode. Sua API é pequena: inicialize uma tarefa, enfileire-a, drene-a, libere a fila quando terminar. O modelo mental é igualmente pequeno: contextos de borda detectam, tarefas em contexto de thread agem.

O driver `myfirst` absorveu o novo mecanismo com naturalidade porque cada capítulo anterior havia preparado o scaffolding. O Capítulo 11 forneceu concorrência. O Capítulo 12 forneceu canais cv e configuração sx. O Capítulo 13 forneceu callouts e a disciplina de drenagem no detach. O Capítulo 14 adicionou tarefas como um quinto primitivo no mesmo formato: inicializar no attach, drenar no detach, obedecer às regras de locking estabelecidas e compor com o que veio antes. O driver agora está na versão `0.8-taskqueues`, com três tarefas e um taskqueue privado, e é encerrado de forma limpa sob carga.

Por trás dessas mudanças concretas, o capítulo estabeleceu vários pontos mais amplos. Um breve resumo de cada um.

**O trabalho diferido é a ponte entre contextos de borda e contextos de thread.** Callouts, filtros de interrupção e seções de epoch enfrentam a mesma restrição: não podem realizar trabalho que exija sleep ou aquisição de locks que admitem sleep. Um taskqueue resolve o problema de forma uniforme, aceitando uma pequena submissão do contexto de borda e executando o trabalho real em uma thread.

**O framework de taskqueue cuida da logística para que você não precise.** Alocação, locking da fila interna, despacho, coalescência, cancelamento, drenagem: tudo isso é tratado pelo framework. O seu driver fornece um callback e um ponto de enfileiramento. O restante é uma configuração curta no attach e uma desmontagem curta no detach.

**A coalescência é um recurso, não um defeito.** Uma tarefa une enfileiramentos redundantes em um único disparo cujo argumento `pending` carrega a contagem. Isso permite que rajadas de eventos sejam condensadas em uma única invocação de callback, o que é quase sempre o que se deseja em termos de desempenho. Um design que precisa de invocação por evento precisa de uma tarefa por evento ou de uma fila por evento, não de uma tarefa enfileirada várias vezes.

**A ordenação do detach é a maior nova disciplina que o capítulo acrescentou.** Callouts primeiro, tarefas segundo, selinfo terceiro, `taskqueue_free` por último. Violar a ordem é uma condição de corrida que pode não aparecer em testes tranquilos e aparecerá sob carga. O documento `LOCKING.md` é onde você registra a ordem; segui-la é onde você evita a condição de corrida.

**Drivers reais usam o mesmo conjunto de padrões.** Log diferido a partir de contexto de borda; divisão de interrupção em filtro mais tarefa; `copyin`/`copyout` assíncrono; retry com backoff; desmontagem diferida; rollover agendado; bloqueio durante reconfiguração. Cada um deles é uma variação do formato borda-detecta, tarefa-age. Ler `/usr/src/sys/dev/` é a maneira mais rápida de absorver esses padrões em profundidade.

**A história do taskqueue escala.** `epoch(9)`, grouptaskqueues e taskqueues por CPU lidam com os casos de escalabilidade que um taskqueue privado simples não consegue. Eles compartilham o modelo mental da API básica; as diferenças estão no número de filas, na estratégia de despacho e no scaffolding ao redor dos workers. Para a maioria dos drivers, a API básica é suficiente; para os casos mais avançados, as variantes avançadas estão disponíveis quando você precisar delas.

### Uma Reflexão Antes do Capítulo 15

Você iniciou o Capítulo 14 com um driver que conseguia agir por conta própria ao longo do tempo (callouts), mas cujas ações estavam limitadas ao que os callouts permitem. Você o encerra com um driver que consegue agir ao longo do tempo e também agir por meio de trabalho entregue a uma worker thread. Esses dois combinados cobrem quase todos os tipos de ação diferida que um driver precisa. A terceira peça é a sincronização que coordena os produtores e consumidores com segurança, e essa peça é o que o Capítulo 15 desenvolve.

O modelo mental é cumulativo. O Capítulo 12 apresentou cvs, mutexes e sx. O Capítulo 13 apresentou callouts e o contexto sem sleep. O Capítulo 14 apresentou tarefas e o contexto de adiamento para thread. O Capítulo 15 introduzirá os primitivos de coordenação avançada (semaphores, `cv_timedwait_sig`, handshakes entre componentes) que compõem as peças anteriores em padrões mais ricos. Cada capítulo adiciona um pequeno primitivo e a disciplina que o acompanha.

A forma cumulativa do driver é visível no `LOCKING.md`. Um driver do Capítulo 10 não tinha `LOCKING.md`. Um driver do Capítulo 11 tinha um único parágrafo. Um driver do Capítulo 14 tem um documento de múltiplas páginas com seções para o mutex, os cvs, o sx, os callouts e as tarefas, além de uma seção de ordenação do detach que nomeia cada etapa de drenagem na ordem correta. Esse documento é um artefato que você carrega para todos os capítulos futuros. Quando o Capítulo 15 adicionar semaphores, o `LOCKING.md` ganha uma seção de Semaphores. Quando a Parte 4 adicionar interrupções, ele ganha uma seção de Interrupts. O ciclo de vida do driver é o seu `LOCKING.md`.

### Uma Segunda Reflexão: A Disciplina

Um hábito que o capítulo quer que você internalize acima de todos os outros: cada novo primitivo no seu driver merece uma entrada no `LOCKING.md`, um par destruidor no attach/detach e um lugar na ordenação do detach documentada. Pular qualquer um desses cria um bug esperando para acontecer. A disciplina paga por si mesma na primeira vez que você entrega o driver a outra pessoa.

O inverso também é verdadeiro: toda vez que você ler o driver de outra pessoa, consulte primeiro o `LOCKING.md` dela. Se estiver faltando, leia as funções attach e detach para reconstruir a ordenação a partir do código. Se você ver um primitivo no attach sem um drain correspondente no detach, isso é um bug. Se você ver um drain sem um predecessor claro, provavelmente é um erro de ordenação. A disciplina é a mesma para escrever e para ler.

### Uma Breve Nota sobre Simplicidade

Taskqueues parecem simples. E são. A API é pequena, os padrões são regulares e os idiomas se transferem entre drivers. A simplicidade é deliberada; é o que torna a API utilizável na prática. A mesma simplicidade também torna as regras inegociáveis: uma regra ignorada produz uma condição de corrida difícil de depurar. Siga a disciplina e os taskqueues permanecerão simples para você. Improvise, e não serão.

### O Que Fazer Se Você Estiver Travado

Se algo no seu driver não está se comportando como esperado, percorra o guia de resolução de problemas da Seção 7 em ordem. Verifique o primeiro item que corresponde ao seu sintoma. Se nenhum item corresponder, releia a Seção 4 (configuração e limpeza) e audite a ordenação do seu detach contra o `LOCKING.md`. Se a ordenação estiver correta, use o `dtrace` no caminho de enfileiramento e veja se os eventos esperados estão ocorrendo.

Se o driver entrar em panic, use `gdb` no crash dump. Um `bt` mostra o stack. Um stack que inclui o seu callback de task é um bom ponto de partida; compare-o com os padrões da Seção 7.

Se nada mais funcionar, releia o driver real que você escolheu no Exercício Desafio 7 da seção anterior. Às vezes, o padrão que parece confuso no seu próprio código se lê de forma natural em um driver escrito por outra pessoa. Os padrões são universais; o driver que você está lendo não tem nada de especial.

## Ponte para o Capítulo 15

O Capítulo 15 tem como título *Mais Sincronização: Condições, Semáforos e Coordenação*. Seu escopo são os primitivos avançados de coordenação que compõem os mutexes, cvs, sx locks, callouts e tasks que você agora possui em padrões mais sofisticados.

O Capítulo 14 preparou o terreno de quatro maneiras específicas.

Primeiro, **você agora tem um par produtor/consumidor funcionando no driver**. O callout `tick_source` (e os outros sites de enfileiramento do Capítulo 14) é um produtor; a thread do taskqueue é um consumidor. Os canais cv do Capítulo 12 formam outro par produtor/consumidor: `write(2)` produz, `read(2)` consome. O Capítulo 15 generaliza o padrão e adiciona primitivos (semáforos de contagem, esperas cv com interrupção por sinal) que tratam versões mais elaboradas do mesmo formato.

Segundo, **você conhece a disciplina de drain-at-detach**. Cada primitivo que você adicionou até agora tem um drain correspondente. O Capítulo 15 apresenta semáforos, que têm seu próprio padrão de "drain" (liberar todos os aguardantes, depois destruir), e a disciplina se transfere diretamente.

Terceiro, **você sabe como pensar sobre os limites de contexto**. O contexto do callout, o contexto da task, o contexto da syscall: cada um tem suas próprias regras, e o design do seu driver as respeita. O Capítulo 15 adiciona esperas com interrupção por sinal, o que inclui o contexto de interação do usuário na mistura. O hábito de "em qual contexto estou e o que posso fazer aqui" se transfere.

Quarto, **seu `LOCKING.md` segue o ritmo de "uma seção por primitivo, mais uma seção de ordenação ao final"**. O Capítulo 15 adicionará uma seção sobre Semáforos e possivelmente uma seção sobre Coordenação. A estrutura está estabelecida; apenas o conteúdo muda.

Tópicos específicos que o Capítulo 15 abordará:

- Semáforos de contagem e semáforos binários por meio da API `sema(9)`.
- `cv_timedwait_sig` e bloqueio com interrupção por sinal.
- Padrões de leitores-escritores por meio de `sx(9)` em sua forma completamente geral.
- Handshakes entre componentes que coordenam timers, tasks e threads do usuário.
- Flags de estado e barreiras de memória para coordenação sem lock (em nível introdutório).
- Frameworks de teste para concorrência: scripts de estresse, injeção de falhas, reprodução de condições de corrida.

Você não precisa ler com antecedência. O Capítulo 14 é preparação suficiente. Traga seu driver `myfirst` no Estágio 4 do Capítulo 14, seu `LOCKING.md`, seu kernel com `WITNESS` habilitado e seu kit de testes. O Capítulo 15 começa de onde o Capítulo 14 terminou.

Uma pequena reflexão de encerramento. O driver com o qual você iniciou a Parte 3 entendia uma syscall por vez. O driver que você tem agora possui três callouts, três tasks, dois cvs, dois locks e um detach completo com ordenação. Ele lida com leitores e escritores concorrentes, eventos temporizados, trabalho diferido entre limites de contexto, rajadas coalescidas de eventos e teardown limpo sob carga. Tem uma história de locking documentada e um conjunto de regressões validado. Está começando a parecer um driver FreeBSD de verdade.

O Capítulo 15 encerra a Parte 3 adicionando os primitivos de coordenação que permitem que as peças se componham em padrões mais ricos. Depois vem a Parte 4: integração com hardware e plataforma. Interrupções reais. Registradores mapeados em memória reais. Hardware real que pode falhar, ter comportamento incorreto ou se recusar a cooperar. A disciplina que você construiu ao longo da Parte 3 é o que vai sustentá-lo daqui em diante.

Tire um momento antes de avançar. O salto do Capítulo 13 para o Capítulo 14 foi qualitativo: o driver adquiriu a capacidade de diferir trabalho para um contexto de thread, e os padrões aprendidos ao longo do caminho (ordenação no detach, coalescência, o modelo mental de borda/thread) são padrões que você reutilizará em todos os capítulos seguintes. A partir daqui, o Capítulo 15 consolida a história de sincronização, e a Parte 4 inicia a história do hardware. O trabalho que você realizou não é perdido; ele se acumula.

### Um Aparte Final Sobre a Forma do Kernel

Um último pensamento antes do Capítulo 15. Você já conheceu cinco dos primitivos de sincronização e diferimento do kernel: o mutex, a condition variable, o sx lock, o callout e o taskqueue. Cada um existe porque primitivos anteriores e mais simples não conseguiam resolver o mesmo problema. Um mutex não consegue expressar "espere por uma condição"; é para isso que existem os cvs. Um sleep mutex não pode ser mantido durante um sleep; é para isso que os sx locks existem. Um callout não consegue executar trabalho que precisa dormir; é para isso que os taskqueues existem.

O padrão é reconhecível em todo o kernel. Cada primitivo de sincronização existe por causa de uma lacuna específica nos anteriores. Ao ler código do kernel, muitas vezes você pode deduzir por que um determinado primitivo foi escolhido perguntando o que seus vizinhos não conseguem fazer. Um file descriptor não pode sofrer use-after-free porque o primitivo de contagem de referências o impede. Um pacote de rede não pode ser liberado enquanto um leitor percorre a lista porque o primitivo de epoch o impede. Uma task não pode ser executada após o detach porque o primitivo de drain o impede.

O kernel é um catálogo desses primitivos, cada um deliberado, cada um uma resposta a uma classe específica de problema. Seu driver acumula seu próprio catálogo conforme cresce. O Capítulo 14 adicionou mais um item à lista. O Capítulo 15 adiciona vários. A Parte 4 inicia o catálogo voltado ao hardware. Daqui em diante os primitivos se multiplicam, mas a forma da disciplina não muda. Defina o problema, escolha o primitivo, inicialize e destrua de forma limpa, documente a ordenação, verifique sob carga.

Esse é o ofício. O restante do livro o conduz por ele.


## Referência: Auditoria de Taskqueue Pré-Produção

Uma breve auditoria a realizar antes de promover um driver que usa taskqueue do ambiente de desenvolvimento para produção. Cada item é uma pergunta; cada uma deve ser respondida com confiança.

### Inventário de Tasks

- [ ] Listei todas as tasks que o driver possui em `LOCKING.md`?
- [ ] Para cada task, nomeei sua função de callback?
- [ ] Para cada task, documentei seu ciclo de vida (init no attach, drain no detach)?
- [ ] Para cada task, documentei seu gatilho (o que faz com que ela seja enfileirada)?
- [ ] Para cada task, documentei se ela se reenfileira ou executa uma vez por gatilho externo?
- [ ] Para cada task com timeout, nomeei o intervalo para o qual está agendada e o caminho de cancelamento?

### Inventário de Taskqueues

- [ ] O taskqueue é uma fila privada ou uma predefinida? A escolha está justificada?
- [ ] Se privado, o attach chama `taskqueue_create` (ou `taskqueue_create_fast`) antes de qualquer código que possa enfileirar?
- [ ] Se privado, o attach chama `taskqueue_start_threads` antes de qualquer código que espera um callback ser disparado?
- [ ] O número de threads trabalhadoras é adequado para a carga de trabalho?
- [ ] A prioridade das threads trabalhadoras é adequada para a carga de trabalho?
- [ ] O nome da thread trabalhadora é informativo o suficiente para que a saída de `procstat -t` seja útil?

### Inicialização

- [ ] Todo `TASK_INIT` ocorre depois que o softc é zerado e antes que a task possa ser enfileirada?
- [ ] Todo `TIMEOUT_TASK_INIT` referencia o taskqueue correto e um callback válido?
- [ ] O attach trata a falha de `taskqueue_create` desfazendo as inicializações anteriores?
- [ ] O attach trata a falha de `taskqueue_start_threads` liberando o taskqueue?

### Sites de Enfileiramento

- [ ] Todo site de enfileiramento aponta para o ponteiro de taskqueue correto?
- [ ] Todo enfileiramento a partir de um contexto de borda (callout, filtro de interrupção) confirma que o taskqueue existe antes de enfileirar?
- [ ] A chamada de enfileiramento é segura no contexto em que ocorre (não dentro de um spin mutex se o taskqueue é `taskqueue_create`, por exemplo)?
- [ ] O comportamento de coalescência é intencional em cada site de enfileiramento?

### Higiene dos Callbacks

- [ ] Todo callback tem a assinatura correta `(void *context, int pending)`?
- [ ] Todo callback adquire locks do driver explicitamente onde necessário?
- [ ] Todo callback libera os locks do driver antes de chamar `selwakeup`, `log` ou outras funções que adquirem locks não relacionados?
- [ ] Todo callback evita alocações `M_NOWAIT` onde `M_WAITOK` é seguro?
- [ ] O tempo total de trabalho do callback é limitado?

### Cancelamento

- [ ] Todo `taskqueue_cancel` / `taskqueue_cancel_timeout` ocorre sob o mutex correto quando a condição de corrida de cancelamento importa?
- [ ] Os casos em que cancel retorna `EBUSY` são tratados (geralmente por um drain subsequente)?

### Detach

- [ ] O detach limpa `is_attached` antes de fazer o drain dos callouts?
- [ ] O detach faz o drain de cada callout antes de fazer o drain de qualquer task?
- [ ] O detach faz o drain de cada task antes de chamar `seldrain`?
- [ ] O detach chama `seldrain` antes de `taskqueue_free`?
- [ ] O detach chama `taskqueue_free` antes de destruir o mutex?
- [ ] O detach define `sc->tq = NULL` após a liberação?

### Documentação

- [ ] Cada task está documentada em `LOCKING.md`?
- [ ] As regras de disciplina (enqueue-safe, callback-lock, drain-order) estão documentadas?
- [ ] O subsistema taskqueue está mencionado no README?
- [ ] Há sysctls expostos que permitem aos usuários observar o comportamento?

### Testes

- [ ] Executei o conjunto de regressões com `WITNESS` habilitado?
- [ ] Testei o detach com todas as tasks em execução?
- [ ] Executei um teste de estresse de longa duração com altas taxas de enfileiramento?
- [ ] Usei `dtrace` para verificar se a latência entre enfileiramento e despacho está dentro das expectativas?
- [ ] Usei `procstat -kk` sob carga para confirmar que a thread do taskqueue não está travada?

Um driver que passa por essa auditoria é um driver em que você pode confiar sob carga.



## Referência: Padronizando Tasks em um Driver

Para um driver com várias tasks, a consistência importa mais do que a criatividade. Uma disciplina breve.

### Uma Convenção de Nomes

Escolha uma convenção e siga-a. A convenção do capítulo:

- A struct da task é nomeada `<finalidade>_task` (por exemplo, `selwake_task`, `bulk_writer_task`).
- A struct da timeout-task é nomeada `<finalidade>_delayed_task` (por exemplo, `reset_delayed_task`).
- O callback é nomeado `myfirst_<finalidade>_task` (por exemplo, `myfirst_selwake_task`, `myfirst_bulk_writer_task`).
- O sysctl que enfileira a task (se existir) é nomeado `<finalidade>_enqueue` ou `<finalidade>_flood` para variantes em bulk.
- O sysctl que configura a task (se existir) é nomeado `<finalidade>_<parâmetro>` (por exemplo, `bulk_writer_batch`).

Um novo mantenedor pode adicionar uma nova task seguindo a convenção sem precisar pensar nos nomes. Por outro lado, uma revisão de código detecta desvios imediatamente.

### Um Padrão de Init/Drain

Cada task usa a mesma inicialização e drain:

```c
/* In attach, after taskqueue_start_threads: */
TASK_INIT(&sc-><purpose>_task, 0, myfirst_<purpose>_task, sc);

/* In detach, after callout drains, before seldrain: */
taskqueue_drain(sc->tq, &sc-><purpose>_task);
```

Para timeout tasks:

```c
/* In attach: */
TIMEOUT_TASK_INIT(sc->tq, &sc-><purpose>_delayed_task, 0,
    myfirst_<purpose>_delayed_task, sc);

/* In detach: */
taskqueue_drain_timeout(sc->tq, &sc-><purpose>_delayed_task);
```

Os sites de chamada são curtos e uniformes. Um revisor pode verificar o padrão e sinalizar desvios instantaneamente.

### Um Padrão de Callback

Todo callback de task segue a mesma estrutura:

```c
static void
myfirst_<purpose>_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        /* Optional: record coalescing for diagnostics. */
        if (pending > 1) {
                MYFIRST_LOCK(sc);
                sc-><purpose>_drops += pending - 1;
                MYFIRST_UNLOCK(sc);
        }

        /* ... do the work, acquiring locks as needed ... */
}
```

O registro de coalescência opcional torna o comportamento de coalescência visível por meio de sysctls. Omita-o se a task raramente coalescer ou se o contador não for útil.

### Um Padrão de Documentação

Cada task é documentada em `LOCKING.md` com os mesmos campos:

- Nome da task e tipo (plain ou timeout_task).
- Função de callback.
- Quais caminhos de código a enfileiram.
- Quais caminhos de código a cancelam (se houver).
- Onde ela é drenada no detach.
- Qual lock, se algum, o callback adquire.
- Por que essa task é diferida (ou seja, por que não pode ser executada no contexto de enfileiramento).

A documentação de uma nova task é mecânica. Uma revisão de código pode verificar a documentação em relação ao código.

### Por Que Padronizar

A padronização tem custos: um novo colaborador precisa aprender as convenções; desvios exigem uma razão especial. Os benefícios são maiores:

- Menor carga cognitiva. Um leitor que conhece o padrão entende instantaneamente cada task.
- Menos erros. O padrão estabelecido trata os casos comuns (init no attach, drain no detach, liberar o lock antes de selwakeup) corretamente; um desvio tem maior probabilidade de estar errado.
- Revisão mais fácil. Os revisores podem verificar a forma em vez de ler cada linha.
- Handoff mais fácil. Um mantenedor que nunca viu o driver pode adicionar uma nova task seguindo o template existente.

O custo da padronização é pago uma vez no momento do design. Os benefícios se acumulam para sempre. Sempre vale a pena.



## Referência: Leituras Adicionais sobre Taskqueues

Para leitores que querem se aprofundar mais.

### Páginas de Manual

- `taskqueue(9)`: a referência canônica da API.
- `epoch(9)`: o framework de sincronização epoch, relevante para tarefas de rede.
- `callout(9)`: o primitivo complementar; timeout_task é construído sobre ele.
- `swi_add(9)`: o registro de interrupção de software usado por `taskqueue_swi` e similares.
- `kproc(9)`, `kthread(9)`: criação direta de kernel thread, para quando um taskqueue não é suficiente.

### Arquivos de Código-Fonte

- `/usr/src/sys/kern/subr_taskqueue.c`: a implementação do taskqueue. Leia `taskqueue_run_locked` com atenção; é o coração do subsistema.
- `/usr/src/sys/sys/taskqueue.h`, `/usr/src/sys/sys/_task.h`: a API pública e as estruturas.
- `/usr/src/sys/kern/subr_gtaskqueue.c`, `/usr/src/sys/sys/gtaskqueue.h`: a camada grouptaskqueue.
- `/usr/src/sys/sys/epoch.h`, `/usr/src/sys/kern/subr_epoch.c`: o framework epoch.
- `/usr/src/sys/dev/ale/if_ale.c`: um driver Ethernet limpo que usa taskqueues.
- `/usr/src/sys/dev/bge/if_bge.c`: um driver Ethernet maior com múltiplas tarefas.
- `/usr/src/sys/dev/wg/if_wg.c`: o uso de grouptaskqueue pelo WireGuard.
- `/usr/src/sys/dev/iwm/if_iwm.c`: um driver wireless com tarefas de timeout.
- `/usr/src/sys/dev/usb/usb_process.c`: a fila de processo por dispositivo do USB (`usb_proc_*`).

### Páginas de Manual Para Ler em Ordem

Para um leitor que ainda não conhece o subsistema de trabalho deferido do FreeBSD:

1. `taskqueue(9)`: a API canônica.
2. `epoch(9)`: o framework de sincronização de leitura sem locks.
3. `callout(9)`: a primitiva irmã de execução temporizada.
4. `swi_add(9)`: a camada de software interrupt abaixo de alguns taskqueues.
5. `kthread(9)`: a alternativa de criação direta de threads.

Cada um constrói sobre o anterior; ler nessa ordem leva algumas horas e fornece um modelo mental sólido da infraestrutura de trabalho deferido do kernel.

### Material Externo

O capítulo sobre sincronização em *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) aborda a evolução histórica dos subsistemas de trabalho deferido. Útil como contexto; não é obrigatório.

A lista de discussão dos desenvolvedores FreeBSD (`freebsd-hackers@`) ocasionalmente discute melhorias e casos extremos relacionados ao taskqueue. Pesquisar o arquivo por "taskqueue" retorna contexto histórico relevante.

Para uma compreensão mais profunda do uso de `epoch(9)` e grouptaskqueues na pilha de rede, a documentação do framework `iflib(9)` e o código-fonte em `/usr/src/sys/net/iflib.c` valem a leitura. Estão além do nível deste capítulo, mas explicam por que os drivers de rede modernos são estruturados do jeito que são.

Por fim, código-fonte real de drivers. Escolha qualquer driver em `/usr/src/sys/dev/` que use taskqueues (a maioria usa), leia o código relacionado ao taskqueue e compare com os padrões deste capítulo. A correspondência é direta; você reconhecerá as formas imediatamente. Esse tipo de leitura transforma as abstrações do capítulo em conhecimento prático.



## Referência: Análise de Custo do Taskqueue

Uma breve discussão sobre o que os taskqueues realmente custam, útil na hora de decidir se vale diferir o trabalho ou criar uma fila privada.

### Custo em Repouso

Uma `struct task` que não foi enfileirada não tem custo além do tamanho da estrutura (32 bytes em amd64). O kernel não sabe da sua existência. Ela fica no seu softc, sem fazer nada.

Uma `struct taskqueue` alocada e ociosa custa:
- A própria estrutura do taskqueue (algumas centenas de bytes).
- Uma ou mais threads de trabalho (stack de 16 KB cada em amd64, mais o estado do escalonador).
- Nenhum custo por enfileiramento em repouso.

### Custo por Enfileiramento

Quando você chama `taskqueue_enqueue(tq, &task)`, o kernel faz:

1. Adquire o mutex interno do taskqueue. Microssegundos.
2. Verifica se a tarefa já está pendente. Tempo constante.
3. Se não estiver pendente, insere na lista e acorda o worker (via `wakeup` na fila). Tempo constante mais um evento no escalonador.
4. Se já estiver pendente, incrementa `ta_pending`. Uma única operação aritmética.
5. Libera o mutex.

O custo total é de microssegundos em uma fila sem contenção. Sob contenção, a aquisição do mutex pode levar mais tempo, mas o framework usa um mutex com alinhamento de padding para minimizar o false sharing, e o mutex raramente é mantido por mais do que algumas instruções.

### Custo por Despacho

Quando a thread de trabalho acorda e executa `taskqueue_run_locked`, o custo por tarefa é:

1. Vai ao início da fila. Tempo constante.
2. Remove a tarefa. Tempo constante.
3. Registra a contagem de pendentes e a zera. Tempo constante.
4. Libera o mutex.
5. Entra em qualquer epoch necessário (para tarefas de rede).
6. Chama o callback. O custo depende do callback.
7. Sai do epoch, se entrou.
8. Re-adquire o mutex para a próxima iteração.

Para um callback curto típico (microssegundos de trabalho), o overhead por despacho é dominado pelo próprio callback mais uma ida e volta no mutex e uma ida e volta no wakeup.

### Custo no Cancelamento/Drenagem

`taskqueue_cancel` é rápido: aquisição do mutex, remoção da lista se pendente, liberação do mutex. Microssegundos.

`taskqueue_drain` é rápido se a tarefa está ociosa. Se a tarefa está pendente, a drenagem aguarda ela ser executada e concluída; a duração depende da profundidade da fila e da duração do callback. Se a tarefa está em execução, a drenagem aguarda a invocação atual retornar.

`taskqueue_drain_all` é mais custoso: precisa aguardar todas as tarefas da fila. A duração é proporcional ao total de trabalho restante.

`taskqueue_free` drena a fila, encerra as threads e libera o estado. O encerramento das threads envolve sinalizar cada uma para sair e aguardar que ela termine sua tarefa atual. Microssegundos a milissegundos, dependendo da profundidade da fila.

### Implicações Práticas

Algumas notas práticas.

**Taskqueues de thread única são baratos.** O custo por instância é algumas centenas de bytes mais um stack de thread de 16 KB. Em qualquer sistema realista, isso é desprezível.

**Taskqueues compartilhados são mais baratos por driver, mas sofrem contenção.** O `taskqueue_thread` é usado por todo driver que não cria o seu próprio. Sob carga pesada, torna-se um gargalo serial. Para drivers com tráfego significativo de tarefas, uma fila privada evita a contenção.

**Taskqueues com múltiplas threads trocam memória por paralelismo.** Quatro threads equivalem a quatro stacks de 16 KB mais quatro entradas no escalonador. Vale a pena quando a carga de trabalho é naturalmente paralela; é desperdício quando a carga serializa em um único mutex de driver.

**A coalescência é desempenho gratuito.** Quando os enfileiramentos chegam mais rápido do que o taskqueue consegue despachar, a coalescência dobra as rajadas em invocações únicas. O driver paga uma invocação de callback para qualquer trabalho que o contador `pending` implique.

### Comparação com Outras Abordagens

Uma thread do kernel criada com `kproc_create` e gerenciada pelo driver custa:
- Stack de 16 KB mais entrada no escalonador (igual a uma thread de trabalho do taskqueue).
- Nenhum framework embutido de enfileiramento e despacho: o driver implementa sua própria fila e wakeup.
- Nenhuma coalescência ou cancelamento embutidos.

Para trabalho que se encaixa no modelo de tarefa (enfileirar, despachar, drenar), um taskqueue é sempre a escolha certa. Para trabalho que não se encaixa (um loop de longa duração com ritmo próprio), uma thread `kproc_create` pode ser mais adequada.

Um callout que enfileira uma tarefa combina os custos de ambas as primitivas. Vale a pena quando o trabalho precisa tanto de um prazo específico quanto de contexto de thread.

### Quando se Preocupar com o Custo

A maioria dos drivers não precisa se preocupar. Taskqueues são baratos; o kernel é bem ajustado. Preocupe-se com o custo somente quando:

- O profiling mostrar que as operações do taskqueue dominam o uso de CPU. (Use `dtrace` para confirmar.)
- Você estiver escrevendo um driver de alta taxa (milhares de eventos por segundo ou mais) e o taskqueue for o ponto de serialização.
- O sistema tiver muitos drivers competindo pelo `taskqueue_thread` e a contenção for mensurável.

Em todos os outros casos, escreva o taskqueue naturalmente e confie no kernel para gerenciar a carga.



## Referência: A Semântica de Coalescência de Tarefas, com Precisão

A coalescência é a funcionalidade que mais surpreende os iniciantes. Uma declaração precisa da semântica, com exemplos práticos, merece sua própria subseção de referência.

### A Regra

Quando `taskqueue_enqueue(tq, &task)` é chamado em uma tarefa que já está pendente (`task->ta_pending > 0`), o kernel incrementa `task->ta_pending` e retorna sucesso. A tarefa não é inserida na fila uma segunda vez. Quando o callback finalmente executa, ele executa exatamente uma vez, com o valor acumulado de `ta_pending` passado como segundo argumento (e o campo é zerado antes de o callback ser chamado).

A regra tem casos extremos que vale nomear.

**O limite máximo.** `ta_pending` é um `uint16_t`. Ele satura em `USHRT_MAX` (65535). Enfileiramentos além desse ponto ainda retornam sucesso, mas o contador não cresce mais. Na prática, atingir 65535 enfileiramentos coalescidos é um problema de design, não de desempenho.

**A flag `TASKQUEUE_FAIL_IF_PENDING`.** Se você passar essa flag para `taskqueue_enqueue_flags`, a função retorna `EEXIST` em vez de coalescer. Útil quando você quer saber se o enfileiramento produziu um novo estado pendente.

**Temporização.** A coalescência acontece no momento do enfileiramento. Se o enfileiramento A e o enfileiramento B ocorrem enquanto a tarefa está pendente, ambos coalescerão. Se o enfileiramento A faz a tarefa começar a executar e o enfileiramento B ocorre enquanto o callback está executando, o enfileiramento B torna a tarefa pendente novamente (pending=1) e o callback será invocado novamente após o retorno da invocação atual. A segunda invocação enxerga `pending=1` porque apenas B foi acumulado. Tanto a primeira quanto a segunda invocação ocorrem; nenhum enfileiramento é perdido.

**Prioridade.** Se duas tarefas diferentes estão pendentes na mesma fila e uma tem prioridade maior, a de maior prioridade executa primeiro, independentemente da ordem de enfileiramento. Dentro de uma única tarefa, a prioridade não é um fator; todas as invocações de uma determinada tarefa executam em sequência.

### Exemplos Práticos

**Exemplo 1: Enfileiramento único simples.**

```c
taskqueue_enqueue(tq, &task);
/* Worker fires the callback. */
/* Callback sees pending == 1. */
```

**Exemplo 2: Enfileiramento coalescido antes do despacho.**

```c
taskqueue_enqueue(tq, &task);
taskqueue_enqueue(tq, &task);
taskqueue_enqueue(tq, &task);
/* (Worker has not yet woken up.) */
/* Worker fires the callback. */
/* Callback sees pending == 3. */
```

**Exemplo 3: Enfileiramento durante a execução do callback.**

```c
taskqueue_enqueue(tq, &task);
/* Callback starts; pending is reset to 0. */
/* While callback is running: */
taskqueue_enqueue(tq, &task);
/* Callback finishes its first invocation. */
/* Worker notices pending == 1; fires callback again. */
/* Second callback invocation sees pending == 1. */
```

**Exemplo 4: Cancelamento antes do despacho.**

```c
taskqueue_enqueue(tq, &task);
taskqueue_enqueue(tq, &task);
/* Cancel: */
taskqueue_cancel(tq, &task, &pendp);
/* pendp == 2; callback does not run. */
```

**Exemplo 5: Cancelamento durante a execução.**

```c
taskqueue_enqueue(tq, &task);
/* Callback starts. */
/* During callback: */
taskqueue_cancel(tq, &task, &pendp);
/* Returns EBUSY; pending (if any future enqueues came in) may or may not be zeroed. */
/* The currently executing invocation completes; the cancellation affects only future runs. */
```

### Implicações de Design

Algumas implicações de design decorrem da regra.

**Seu callback deve ser idempotente em relação a `pending`.** Escrever um callback que assume `pending==1` quebra sob carga. Sempre use `pending` deliberadamente, seja percorrendo `pending` vezes ou fazendo uma única passagem que trate todo o estado acumulado.

**Não use "número de invocações do callback" como contador de eventos.** Use `pending` de cada invocação, somados. Ou, melhor ainda, use uma estrutura de estado por evento (uma fila dentro do softc) que o callback drena.

**A coalescência transforma trabalho por evento em trabalho por rajada.** Um callback que faz trabalho O(1) por invocação, com `pending` descartado, lida com a mesma quantidade de trabalho independentemente da taxa de enfileiramento. Isso normalmente é adequado para trabalhos do tipo "notificar waiters"; é incorreto para trabalhos do tipo "processar cada evento".

**A coalescência permite enfileirar livremente a partir de contextos de borda.** Um callout que dispara a cada milissegundo pode enfileirar uma tarefa a cada milissegundo; se o callback levar 10 ms para executar, nove enfileiramentos coalescem em uma invocação por callback. O sistema converge naturalmente para a taxa de transferência que o callback consegue sustentar.



## Referência: O Diagrama de Estados do Taskqueue

Um breve diagrama de estados para uma única tarefa, como auxílio para raciocinar sobre o ciclo de vida.

```text
        +-----------+
        |   IDLE    |
        | pending=0 |
        +-----+-----+
              |
              | taskqueue_enqueue
              v
        +-----------+           +--------+
        |  PENDING  | <--- enq--|  any   |
        | pending>=1|          +--------+
        +-----+-----+
              |
              | worker picks up
              v
        +-----------+
        |  RUNNING  |
        | (callback |
        | executing)|
        +-----+-----+
              |
              | callback returns
              v
        +-----------+
        |   IDLE    |
        | pending=0 |
        +-----------+
```

Uma tarefa está sempre em exatamente um de três estados: IDLE, PENDING ou RUNNING.

**IDLE.** Não está em nenhuma fila. `ta_pending == 0`. O enfileiramento a move para PENDING.

**PENDING.** Na lista de pendentes do taskqueue. `ta_pending >= 1`. A coalescência incrementa `ta_pending` sem sair de PENDING. O cancelamento a move de volta para IDLE.

**RUNNING.** Em `tq_active`, com o callback em execução. `ta_pending` foi zerado e o callback recebeu o valor anterior. Novos enfileiramentos fazem a transição de volta para PENDING (de forma que, após o callback retornar, o worker a dispara novamente). O cancelamento retorna `EBUSY` nesse estado.

As transições de estado são todas serializadas por `tq_mutex`. A qualquer instante, o kernel pode dizer em qual estado uma tarefa se encontra, e as transições são atômicas.

`taskqueue_drain(tq, &task)` aguarda até que a tarefa esteja IDLE e nenhum novo enfileiramento chegue antes de retornar. Essa é a garantia precisa que a drenagem fornece.



## Referência: Guia Rápido de Observabilidade

Para consulta rápida durante a depuração.

### Liste Todas as Threads do Taskqueue

```text
# procstat -t | grep taskq
```

### Liste a Taxa de Submissão de Tarefas com DTrace

```text
# dtrace -n 'fbt::taskqueue_enqueue:entry { @[(caddr_t)arg1] = count(); }' -c 'sleep 10'
```

O script conta o número de enfileiramentos por ponteiro de task ao longo de dez segundos. Os ponteiros de task são mapeados de volta aos drivers via `addr2line` ou `kgdb`.

### Medir a Latência de Despacho

```text
# dtrace -n '
  fbt::taskqueue_enqueue:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:entry /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
' -c 'sleep 10'
```

### Medir a Duração do Callback

```text
# dtrace -n '
  fbt::taskqueue_run_locked:entry { self->t = timestamp; }
  fbt::taskqueue_run_locked:return /self->t/ {
        @[execname] = quantize(timestamp - self->t);
        self->t = 0;
  }
' -c 'sleep 10'
```

### Stack Trace da Thread do Taskqueue Quando Travada

```text
# procstat -kk <pid>
```

### Tarefas Ativas Dentro do ddb

No prompt do `ddb`:

```text
db> show taskqueues
```

Lista cada taskqueue, sua tarefa ativa (se houver) e a fila de pendências.

### Knobs de Sysctl que Seu Driver Deve Expor

Para cada tarefa que o driver possui, considere expor:

- `<purpose>_enqueues`: total de enfileiramentos tentados.
- `<purpose>_coalesced`: quantidade de vezes em que o coalescing foi acionado.
- `<purpose>_runs`: total de invocações do callback.
- `<purpose>_largest_pending`: pico de pendências.

Em condições normais: `enqueues == runs + coalesced`. Com coalescing: `runs < enqueues`. Sem carga: `largest_pending == 1`. Sob carga pesada: `largest_pending` cresce.

Esses contadores transformam o comportamento opaco do driver em uma exibição legível via sysctl. O custo é alguns adds atômicos; o valor é alto.



## Referência: Um Template Mínimo de Tarefa Funcional

Para facilitar a cópia e adaptação. Cada peça foi apresentada no capítulo; o template as reúne em um esqueleto pronto para uso.

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>
#include <sys/mutex.h>
#include <sys/lock.h>

struct example_softc {
        device_t          dev;
        struct mtx        mtx;
        struct taskqueue *tq;
        struct task       work_task;
        int               is_attached;
};

static void
example_work_task(void *arg, int pending)
{
        struct example_softc *sc = arg;

        mtx_lock(&sc->mtx);
        /* ... do work under the mutex if state protection is needed ... */
        mtx_unlock(&sc->mtx);

        /* ... do lock-free work or calls like selwakeup here ... */
}

static int
example_attach(device_t dev)
{
        struct example_softc *sc = device_get_softc(dev);
        int error;

        sc->dev = dev;
        mtx_init(&sc->mtx, device_get_nameunit(dev), "example", MTX_DEF);

        sc->tq = taskqueue_create("example taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) {
                error = ENOMEM;
                goto fail_mtx;
        }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0)
                goto fail_tq;

        TASK_INIT(&sc->work_task, 0, example_work_task, sc);
        sc->is_attached = 1;
        return (0);

fail_tq:
        taskqueue_free(sc->tq);
fail_mtx:
        mtx_destroy(&sc->mtx);
        return (error);
}

static int
example_detach(device_t dev)
{
        struct example_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        sc->is_attached = 0;
        mtx_unlock(&sc->mtx);

        taskqueue_drain(sc->tq, &sc->work_task);
        taskqueue_free(sc->tq);
        mtx_destroy(&sc->mtx);
        return (0);
}

/* Elsewhere, a code path that wants to defer work: */
static void
example_trigger_work(struct example_softc *sc)
{
        if (sc->is_attached)
                taskqueue_enqueue(sc->tq, &sc->work_task);
}
```

Cada elemento é essencial. Remover qualquer um deles reintroduz um bug contra o qual este capítulo alertou.



## Referência: Comparação com Workqueues do Linux

Uma comparação breve para leitores que vêm do desenvolvimento do kernel Linux. Ambos os sistemas resolvem o mesmo problema; as diferenças estão nos nomes, na granularidade e nos padrões.

### Nomenclatura

| Conceito | FreeBSD | Linux |
|---|---|---|
| Unidade de trabalho diferido | `struct task` | `struct work_struct` |
| Fila | `struct taskqueue` | `struct workqueue_struct` |
| Fila compartilhada | `taskqueue_thread` | `system_wq` |
| Fila sem vínculo de CPU | `taskqueue_thread` com muitas threads | `system_unbound_wq` |
| Criar uma fila | `taskqueue_create` | `alloc_workqueue` |
| Enfileirar | `taskqueue_enqueue` | `queue_work` |
| Enfileirar com atraso | `taskqueue_enqueue_timeout` | `queue_delayed_work` |
| Aguardar o trabalho terminar | `taskqueue_drain` | `flush_work` |
| Destruir uma fila | `taskqueue_free` | `destroy_workqueue` |
| Prioridade | `ta_priority` | flag `WQ_HIGHPRI` |
| Comportamento de coalescing | Automático, contagem `pending` exposta | verificação `work_pending`, sem contagem |

### Diferenças Semânticas

**Visibilidade do coalescing.** O FreeBSD expõe a contagem de pendências ao callback; o Linux não. Um callback no Linux sabe que o trabalho foi disparado, mas não quantas vezes foi solicitado.

**Timeout task vs. delayed work.** O `timeout_task` do FreeBSD embute um callout; o `delayed_work` do Linux embute uma `timer_list`. Ambos se comportam da mesma forma do ponto de vista do usuário.

**Grouptaskqueues vs. percpu workqueues.** O `taskqgroup` do FreeBSD é explícito e separado; o `alloc_workqueue(..., WQ_UNBOUND | WQ_CPU_INTENSIVE)` do Linux tem semântica similar com knobs diferentes.

**Integração com Epoch.** O FreeBSD possui `NET_TASK_INIT` para tarefas que rodam dentro do network epoch; o Linux não tem um análogo direto (o framework RCU é similar, mas não idêntico).

Um driver portado do Linux para o FreeBSD (ou vice-versa) geralmente consegue traduzir o padrão de trabalho diferido quase um-a-um. As diferenças estruturais estão nas APIs ao redor (registro de dispositivo, alocação de memória, locking) mais do que no taskqueue em si.



## Referência: Quando Não Usar um Taskqueue

Uma lista breve de cenários em que outro primitivo é preferível.

**O trabalho tem requisitos rígidos de temporização.** Taskqueues introduzem latência de escalonamento. Para prazos na escala de microssegundos, um callout `C_DIRECT_EXEC` ou um `taskqueue_swi` é mais rápido. Para prazos na escala de nanossegundos, nenhum dos mecanismos de trabalho diferido é rápido o suficiente; o trabalho precisa acontecer inline.

**O trabalho é uma limpeza de uso único que não tem um produtor a ser associado.** Um simples `free` dentro de um caminho de teardown não precisa de um taskqueue; basta chamá-lo diretamente. Diferir por diferir não agrega valor.

**O trabalho deve rodar em uma prioridade de escalonamento específica maior que `PWAIT`.** Se o trabalho for genuinamente de alta prioridade (um driver de tempo real, uma tarefa no limiar de interrupção), use `kthread_add` com uma prioridade explícita em vez de um taskqueue genérico.

**O trabalho exige um contexto de thread específico que um worker genérico não pode fornecer.** Tarefas rodam em uma thread do kernel sem contexto de processo de usuário específico. Trabalho que precisa das credenciais de um usuário específico, da tabela de descritores de arquivo ou do espaço de endereço deve acontecer dentro daquele processo, não em uma tarefa.

**O driver tem apenas uma tarefa e ela raramente é executada.** Um único `kthread_add` com um loop `cv_timedwait` pode ser mais claro do que toda a configuração do taskqueue. Use o bom senso; para três ou mais tarefas, taskqueues são quase sempre mais claros.

Para todo o restante, use um taskqueue. O padrão é "use `taskqueue(9)`"; as exceções são raras.



## Referência: Uma Leitura Comentada de `subr_taskqueue.c`

Mais um exercício de leitura, porque entender a implementação torna o comportamento da API previsível.

O arquivo é `/usr/src/sys/kern/subr_taskqueue.c`. A estrutura, brevemente:

**`struct taskqueue`.** Definida perto do início do arquivo. Contém a fila de pendências (`tq_queue`), a lista de tarefas ativas (`tq_active`), o mutex interno (`tq_mutex`), o callback de enfileiramento (`tq_enqueue`), as threads trabalhadoras (`tq_threads`) e flags.

**Macros `TQ_LOCK` / `TQ_UNLOCK`.** Logo após a estrutura. Adquirem o mutex (spin ou sleep, dependendo de `tq_spin`).

**`taskqueue_create` e `_taskqueue_create`.** Alocam a estrutura, inicializam o mutex (`MTX_DEF` ou `MTX_SPIN`), retornam.

**`taskqueue_enqueue` e `taskqueue_enqueue_flags`.** Adquirem o mutex, verificam `task->ta_pending`, fazem coalescing ou encadeiam, acordam o worker (via callback de `enqueue`), liberam o mutex.

**`taskqueue_enqueue_timeout`.** Agenda o callout interno; o callback do callout chamará posteriormente `taskqueue_enqueue` na tarefa subjacente.

**`taskqueue_cancel` e `taskqueue_cancel_timeout`.** Remove da fila se pendente; retorna `EBUSY` se em execução.

**`taskqueue_drain` e variantes.** `msleep` em uma condição que é sinalizada quando a tarefa está ociosa e sem pendências.

**`taskqueue_run_locked`.** O coração do subsistema. Em um loop: retira uma tarefa da fila de pendências, registra `ta_pending`, limpa-o, move para ativo, libera o mutex, opcionalmente entra no net epoch, chama o callback, readquire o mutex, sinaliza os waiters de drain. Repete até a fila estar vazia.

**`taskqueue_thread_loop`.** O loop principal da thread trabalhadora. Adquire o mutex do taskqueue, aguarda trabalho (`msleep`) se a fila estiver vazia, chama `taskqueue_run_locked` quando o trabalho chega, repete.

**`taskqueue_free`.** Define o flag de "draining", acorda cada worker, aguarda cada worker sair, drena as tarefas restantes, libera a estrutura.

Esta leitura cita cada função pelo nome em vez de pelo número de linha, porque números de linha mudam entre releases do FreeBSD enquanto os nomes de símbolo sobrevivem. Se quiser coordenadas aproximadas para `subr_taskqueue.c` no FreeBSD 14.3, os principais pontos de entrada ficam próximos destas linhas: `_taskqueue_create` 141, `taskqueue_create` 178, `taskqueue_free` 217, `taskqueue_enqueue_flags` 305, `taskqueue_enqueue` 317, `taskqueue_enqueue_timeout` 382, `taskqueue_run_locked` 485, `taskqueue_cancel` 579, `taskqueue_cancel_timeout` 591, `taskqueue_drain` 612, `taskqueue_thread_loop` 820. Trate esses números como uma dica de rolagem; abra o arquivo e vá direto ao símbolo.

Ler essas funções uma vez é um bom investimento. Tudo que o Capítulo 14 ensinou sobre o comportamento da API está visível na implementação.



## Um Tour Final: Cinco Formas Comuns

Cinco formas que respondem pela maior parte do uso de taskqueues na árvore do FreeBSD. Reconhecê-las transforma a leitura de código-fonte de drivers de uma análise sintática em reconhecimento de padrões.

### Forma A: A Tarefa Solo

Uma tarefa, enfileirada de um único lugar, drenada no detach. A mais simples. Usada por drivers que precisam diferir exatamente um tipo de trabalho.

```c
TASK_INIT(&sc->task, 0, sc_task, sc);
/* ... */
taskqueue_enqueue(sc->tq, &sc->task);
/* ... */
taskqueue_drain(sc->tq, &sc->task);
```

### Forma B: A Divisão Filter-Plus-Task

O interrupt filter faz o mínimo e enfileira uma tarefa para o restante.

```c
static int
sc_filter(void *arg)
{
        struct sc *sc = arg;
        taskqueue_enqueue(sc->tq, &sc->intr_task);
        return (FILTER_HANDLED);
}
```

### Forma C: A Tarefa Periódica Guiada por Callout

Um callout dispara periodicamente e enfileira uma tarefa que executa o trabalho.

```c
static void
sc_periodic_callout(void *arg)
{
        struct sc *sc = arg;
        taskqueue_enqueue(sc->tq, &sc->periodic_task);
        callout_reset(&sc->co, hz, sc_periodic_callout, sc);
}
```

### Forma D: A Timeout Task

`timeout_task` para trabalho diferido em contexto de thread.

```c
TIMEOUT_TASK_INIT(sc->tq, &sc->delayed, 0, sc_delayed, sc);
/* ... */
taskqueue_enqueue_timeout(sc->tq, &sc->delayed, delay_ticks);
/* ... */
taskqueue_drain_timeout(sc->tq, &sc->delayed);
```

### Forma E: A Tarefa com Auto-Reenfileiramento

Uma tarefa que se agenda novamente a partir do próprio callback.

```c
static void
sc_self(void *arg, int pending)
{
        struct sc *sc = arg;
        /* work */
        if (sc->keep_running)
                taskqueue_enqueue_timeout(sc->tq, &sc->self_tt, hz);
}
```

Todo driver que você ler usará alguma combinação dessas cinco formas. Uma vez que elas se tornem familiares, o restante é detalhe de implementação.



## Resumo Final: O que o Capítulo Entregou

Um inventário breve, para o leitor que deseja a versão comprimida após percorrer o capítulo completo.

**Conceitos introduzidos.**

- O trabalho diferido como ponte entre contextos de borda e o contexto de thread.
- As estruturas de dados `struct task` / `struct timeout_task` e seu ciclo de vida.
- Taskqueues como o par fila + thread trabalhadora.
- Taskqueues privadas versus predefinidas e quando cada uma é a escolha certa.
- Coalescing via `ta_pending` e o argumento `pending`.
- Ordenação por prioridade dentro de uma fila.
- Os primitivos `block`/`unblock`/`quiesce`/`drain_all`.
- Ordenação do detach com callouts, tarefas, selinfo e teardown do taskqueue.
- Depuração via `procstat`, `dtrace`, contadores sysctl e `WITNESS`.
- Exposição introdutória a `epoch(9)`, grouptaskqueues e taskqueues por CPU.

**Mudanças no driver.**

- Estágio 1: uma tarefa enfileirada a partir de `tick_source`, drenada no detach.
- Estágio 2: um taskqueue privado de propriedade do driver.
- Estágio 3: uma tarefa de escrita em massa demonstrando coalescing deliberado, um `timeout_task` para reset com atraso.
- Estágio 4: consolidação, bump de versão para `0.8-taskqueues`, regressão completa.

**Mudanças na documentação.**

- Uma seção de Tarefas em `LOCKING.md`.
- Uma seção de ordenação do detach que enumera cada passo de drain.
- Documentação por tarefa listando callback, tempo de vida, caminhos de enfileiramento e caminhos de cancelamento.

**Padrões catalogados.**

- Log diferido.
- Reset com atraso.
- Divisão de interrupção filter-plus-task.
- `copyin`/`copyout` assíncrono.
- Retry com backoff.
- Teardown diferido.
- Rollover de estatísticas.
- Block durante reconfiguração.
- Drain-all em fronteira de subsistema.
- Geração sintética de eventos.

**Ferramentas de depuração utilizadas.**

- `procstat -t` para estado da thread do taskqueue.
- `ps ax` para inventário de threads do kernel.
- `sysctl dev.<driver>` para contadores expostos pelo driver.
- `dtrace` para latência de enfileiramento e duração do callback.
- `procstat -kk` para diagnóstico de thread travada.
- `WITNESS` e `INVARIANTS` como rede de segurança do kernel de depuração.

**Entregáveis.**

- `content/chapters/part3/chapter-14.md` (este arquivo).
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage1-first-task/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage2-private-taskqueue/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage3-coalescing/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/stage4-final/`.
- `examples/part-03/ch14-taskqueues-and-deferred-work/labs/` com `poll_waiter.c` e pequenos scripts auxiliares.
- `examples/part-03/ch14-taskqueues-and-deferred-work/LOCKING.md` com a seção de Tarefas.
- `examples/part-03/ch14-taskqueues-and-deferred-work/README.md` com instruções de build e teste por estágio.

Este é o fim do Capítulo 14. O Capítulo 15 dá continuidade à história de sincronização.


## Referência: Lendo `taskqueue_run_locked` Linha por Linha

O coração do subsistema de taskqueues é um loop curto dentro de `taskqueue_run_locked` em `/usr/src/sys/kern/subr_taskqueue.c`. Lê-lo uma vez, com calma, vale a pena sempre que for preciso raciocinar sobre o comportamento do subsistema. Uma passagem narrada segue abaixo.

A função é chamada a partir do loop principal da thread trabalhadora, `taskqueue_thread_loop`, com o mutex do taskqueue mantido. Seu trabalho é processar cada tarefa pendente, liberar o mutex ao redor do callback e retornar com o mutex ainda mantido quando a fila estiver vazia.

```c
static void
taskqueue_run_locked(struct taskqueue *queue)
{
        struct epoch_tracker et;
        struct taskqueue_busy tb;
        struct task *task;
        bool in_net_epoch;
        int pending;

        KASSERT(queue != NULL, ("tq is NULL"));
        TQ_ASSERT_LOCKED(queue);
        tb.tb_running = NULL;
        LIST_INSERT_HEAD(&queue->tq_active, &tb, tb_link);
        in_net_epoch = false;
```

A função começa verificando que o mutex está retido e inserindo uma estrutura local `taskqueue_busy` na lista de invocações ativas. A estrutura `tb` representa esta invocação de `taskqueue_run_locked`; o código subsequente a utiliza para rastrear o que esta invocação está executando no momento. O flag `in_net_epoch` rastreia se estamos atualmente dentro do epoch de rede, para não entrarmos nele de forma redundante quando tarefas consecutivas estão todas marcadas como de rede.

```c
        while ((task = STAILQ_FIRST(&queue->tq_queue)) != NULL) {
                STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
                if (queue->tq_hint == task)
                        queue->tq_hint = NULL;
                pending = task->ta_pending;
                task->ta_pending = 0;
                tb.tb_running = task;
                tb.tb_seq = ++queue->tq_seq;
                tb.tb_canceling = false;
                TQ_UNLOCK(queue);
```

O laço principal. Retira o elemento do início da fila de tarefas pendentes. Captura o contador de pendências em uma variável local e redefine o campo para zero (de modo que novos enfileiramentos que chegarem durante o callback incrementem a partir de zero novamente). Registra a tarefa na estrutura `tb` para que chamadores de drain possam ver o que está em execução. Incrementa um contador de sequência para detecção de drain obsoleto. Libera o mutex.

Observe que, entre este ponto e o próximo `TQ_LOCK`, o mutex não está retido. Esta é a janela em que o callback é executado; o restante do kernel pode enfileirar mais tarefas (que serão coalescidas ou aguardarão na fila), drenar outras tarefas (que verão `tb.tb_running == task` e aguardarão), ou executar seus próprios processos.

```c
                KASSERT(task->ta_func != NULL, ("task->ta_func is NULL"));
                if (!in_net_epoch && TASK_IS_NET(task)) {
                        in_net_epoch = true;
                        NET_EPOCH_ENTER(et);
                } else if (in_net_epoch && !TASK_IS_NET(task)) {
                        NET_EPOCH_EXIT(et);
                        in_net_epoch = false;
                }
                task->ta_func(task->ta_context, pending);

                TQ_LOCK(queue);
                wakeup(task);
        }
        if (in_net_epoch)
                NET_EPOCH_EXIT(et);
        LIST_REMOVE(&tb, tb_link);
}
```

A contabilidade do epoch: entra no epoch de rede se esta tarefa estiver marcada como de rede e ainda não estivermos nele; sai do epoch se tínhamos entrado para uma tarefa anterior, mas esta não está marcada como de rede. Isso permite que tarefas de rede consecutivas compartilhem uma única entrada de epoch, uma otimização que o framework oferece sem custo adicional.

Chama o callback com o contexto e o contador de pendências. Readquire o mutex. Acorda qualquer chamador de drain que esteja aguardando por esta tarefa específica. Repete o laço.

Após o laço, se ainda estivermos no epoch de rede, saímos dele. Remove a estrutura `tb` da lista ativa.

Sete observações da leitura desta função.

**Observação 1.** O mutex é liberado por exatamente o tempo em que o callback está em execução. Nenhum código interno do taskqueue é executado junto com o callback; se o callback leva milissegundos, o mutex do taskqueue fica livre por milissegundos.

**Observação 2.** `ta_pending` é redefinido antes de o callback ser executado, não depois. Um novo enfileiramento durante o callback torna a tarefa pendente novamente (pending=1). Após o callback retornar, o laço detecta o novo pending, retira-o da fila e executa o callback uma segunda vez com pending=1. Nenhum enfileiramento é perdido.

**Observação 3.** O valor de `pending` passado ao callback é o contador no momento em que a tarefa foi retirada da fila, não no momento em que as chamadas de enfileiramento ocorreram. Se enfileiramentos chegarem durante o callback, eles não contribuem para o `pending` desta invocação; contribuem para o `pending` da próxima invocação.

**Observação 4.** O wakeup no final do laço acorda os chamadores de drain que estão dormindo no endereço da tarefa. O drain usa `msleep(&task, &tq->mutex, ...)` e aguarda que a tarefa saia da fila e não esteja mais em execução. O wakeup aqui é o que faz o drain terminar.

**Observação 5.** O contador de sequência `tq_seq` e o `tb.tb_seq` permitem que o drain-all detecte se novas tarefas foram adicionadas desde o início do drain. Sem a sequência, o drain-all estaria sujeito a condições de corrida com novos enfileiramentos.

**Observação 6.** `tb.tb_canceling` é um flag que `taskqueue_cancel` define para informar a um aguardante que esta tarefa está sendo cancelada no momento; seu propósito é permitir que chamadas concorrentes de cancel e drain se coordenem. Não discutimos isso no texto principal porque a maioria dos drivers nunca o encontra.

**Observação 7.** Múltiplas threads trabalhadoras podem estar dentro de `taskqueue_run_locked` simultaneamente, cada uma despachando uma tarefa diferente. A lista `tq_active` mantém todas as estruturas `tb` delas. Tarefas diferentes na mesma fila são executadas em paralelo; a mesma tarefa não pode ser executada em paralelo consigo mesma, pois apenas uma thread trabalhadora a retira da fila por vez.

Essas observações, em conjunto, descrevem exatamente o que o taskqueue garante e o que não garante. Todo o comportamento descrito anteriormente neste capítulo é consequência deste pequeno laço.

## Referência: Um Passo a Passo de `taskqueue_drain`

Igualmente esclarecedor e igualmente breve. Em `/usr/src/sys/kern/subr_taskqueue.c`, aproximadamente:

```c
void
taskqueue_drain(struct taskqueue *queue, struct task *task)
{
        if (!queue->tq_spin)
                WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, ...);

        TQ_LOCK(queue);
        while (task->ta_pending != 0 || task_is_running(queue, task))
                TQ_SLEEP(queue, task, "taskqueue_drain");
        TQ_UNLOCK(queue);
}
```

A função adquire o mutex do taskqueue e então entra em loop até que a task não esteja nem pendente nem em execução. Cada iteração dorme no endereço da task; cada wakeup ao final de `taskqueue_run_locked` acorda o processo de drain para verificar novamente.

`task_is_running(queue, task)` percorre a lista de ativos (`tq_active`) e retorna verdadeiro se algum `tb.tb_running == task`. É O(N) em relação ao número de workers, mas para a maioria dos drivers N é 1 e isso é O(1).

A função não mantém o lock durante o sono; `TQ_SLEEP` (que se expande para `msleep` ou `msleep_spin`) libera o mutex durante o sono e o readquire ao acordar, o que corresponde ao padrão clássico de variável de condição.

Observações da leitura de `taskqueue_drain`.

**Observação 1.** O drain é uma espera por variável de condição, usando o ponteiro da task como canal de wakeup. O wakeup vem de `wakeup(task)` ao final de `taskqueue_run_locked`.

**Observação 2.** O drain não impede que novos enqueues ocorram. Se a task for enfileirada novamente enquanto o drain aguarda, o drain continuará esperando até que esse novo enqueue seja disparado e concluído. É por isso que a disciplina de detach exige drenar todos os produtores (callouts, outras tasks, handlers de interrupção) antes de drenar a task alvo.

**Observação 3.** O drain em uma task ociosa (nunca enfileirada, ou enfileirada e concluída) retorna imediatamente. É seguro chamar drain incondicionalmente no detach.

**Observação 4.** O drain mantém o mutex do taskqueue durante a verificação inicial e antes do sono, o que significa que o drain não pode entrar em condição de corrida com o enqueue de forma que ignore uma task recém-pendente. Se um enqueue chegar entre a verificação e o sono, `ta_pending` torna-se diferente de zero e o loop de drain reitera.

**Observação 5.** O `WITNESS_WARN` no início verifica que o chamador está em um contexto onde dormir é permitido. Se você tentar chamar `taskqueue_drain` de um contexto que não pode dormir (um callback de callout, por exemplo), o `WITNESS` irá reclamar.

Duas funções complementares são `taskqueue_cancel` (que remove a task da fila se estiver pendente e retorna `EBUSY` se estiver em execução) e `taskqueue_drain_timeout` (que também cancela o callout interno). Vale a pena ler suas implementações uma vez; elas são curtas.

---

## Referência: O Ciclo de Vida Visto pelo Softc

Mais uma perspectiva, por completude. As mesmas informações, organizadas em torno do softc em vez da API.

No momento do **attach**, o softc recebe:

- Um ponteiro de taskqueue (`sc->tq`), criado por `taskqueue_create` e alimentado por `taskqueue_start_threads`.
- Uma ou mais estruturas de task (`sc->foo_task`), inicializadas por `TASK_INIT` ou `TIMEOUT_TASK_INIT`.
- Contadores e flags para observabilidade (opcionais, mas recomendados).

Em **tempo de execução**, o estado do taskqueue no softc é:

- `sc->tq` é um ponteiro opaco; os drivers nunca leem seus campos.
- `sc->foo_task` pode estar IDLE, PENDING ou RUNNING a qualquer instante.
- As threads worker do taskqueue dormem a maior parte do tempo, acordam no enqueue, executam os callbacks e dormem novamente.

No momento do **detach**, o softc é desmontado nesta ordem:

1. Limpe `sc->is_attached` sob o mutex, faça broadcast nos cvs e libere o mutex.
2. Drene todos os callouts.
3. Drene todas as tasks.
4. Drene o selinfo.
5. Libere o taskqueue.
6. Destrua o cdev e seu alias.
7. Libere o contexto de sysctl.
8. Destrua o cbuf e os contadores.
9. Destrua os cvs, o sx e o mutex.

Após `taskqueue_free`, `sc->tq` torna-se inválido. Após o drain da task, as estruturas `sc->foo_task` ficam ociosas e seu armazenamento pode ser recuperado junto com o softc.

O tempo de vida do softc é determinado pelo attach/detach do dispositivo. As tasks não podem sobreviver ao seu softc. O drain no detach é o que garante essa propriedade.

---

## Referência: Um Glossário de Termos

Para consulta rápida.

**Task.** Uma instância de `struct task`; um callback mais contexto empacotados para enqueue em um taskqueue.

**Taskqueue.** Uma instância de `struct taskqueue`; uma fila de tasks pendentes associada a uma ou mais threads worker.

**Timeout task.** Uma instância de `struct timeout_task`; uma task mais um callout interno, usada para trabalhos agendados para o futuro.

**Enqueue.** Adicionar uma task a um taskqueue. Se a task já estiver pendente, incrementa seu contador de pendência em vez disso.

**Drain.** Aguardar até que uma task não esteja nem pendente nem em execução.

**Dispatch.** O ato do worker do taskqueue retirar uma task da lista de pendentes e executar seu callback.

**Coalesce.** Fundir enqueues redundantes em um único incremento de estado pendente em vez de duas entradas na lista.

**Pending count.** O valor de `ta_pending`, representando quantos enqueues coalesced estão acumulados nesta task.

**Idle task.** Uma task que não está pendente e não está em execução. `ta_pending == 0` e nenhum worker a possui.

**Worker thread.** Uma thread do kernel (geralmente uma por taskqueue) cujo trabalho é esperar por trabalho e executar os callbacks das tasks.

**Edge context.** Um contexto restrito (callout, filtro de interrupção, seção de epoch) onde algumas operações não são permitidas.

**Thread context.** Um contexto comum de thread do kernel onde dormir, adquirir locks com suporte a sono e todas as operações padrão são permitidas.

**Detach ordering.** A sequência em que as primitivas são drenadas e liberadas no detach do dispositivo, de modo que nenhuma primitiva seja liberada enquanto algo ainda possa referenciá-la.

**Drain race.** Um bug em que uma primitiva é liberada enquanto um callback ou handler ainda pode estar em execução, causado por ordenação incorreta de detach.

**Pending-drop counter.** Um contador de diagnóstico incrementado quando o argumento `pending` do callback é maior que um, indicando que ocorreu coalescing.

**Private taskqueue.** Um taskqueue de propriedade do driver, criado e liberado com o attach/detach, não compartilhado com outros drivers.

**Shared taskqueue.** Um taskqueue fornecido pelo kernel (`taskqueue_thread`, `taskqueue_swi`, etc.) usado por múltiplos drivers simultaneamente.

**Fast taskqueue.** Um taskqueue criado com `taskqueue_create_fast` que usa um spin mutex internamente, seguro para enqueue a partir do contexto de filter-interrupt.

**Grouptaskqueue.** Uma variação escalável onde as tasks são distribuídas em filas por CPU. Usado por drivers de rede de alta taxa.

**Epoch.** Um mecanismo de sincronização de leitura sem lock. O epoch `net_epoch_preempt` protege o estado da rede.

---

O capítulo 14 termina aqui. O próximo capítulo aprofunda ainda mais a história de sincronização.
